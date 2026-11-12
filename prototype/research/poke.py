"""Write bytes into a live save file, with an automatic backup and a log entry. Game must be closed.

Usage:  python poke.py FILENAME OFFSET HEXBYTES [--raw]
Example: python poke.py SONIC2B__S01 0x2870 e7030000      (rings = 999, little-endian)

Every poke first copies the untouched file into lab/archive/pre_poke_<time>/.
Undo by restoring a snapshot:  python save_lab.py restore <label>

SONIC2B__S01 checksum (INFERRED from our snapshots, not yet confirmed by the game accepting it):
the 16-bit little-endian value at 0x2840 moves by exactly the sum of the byte changes elsewhere in the file.
For pokes inside the verified range (0x284B-0x5C77) this tool applies that same change to the field.
Use --raw to skip the checksum update. The ALF file's checksum (8 bytes at 0xFAA4) is NOT handled.
"""
import shutil
import sys

from save_lab import ARCHIVE, log, require_closed, savedata_dir, stamp

S01_CHECK_AT = 0x2840
S01_SAFE_START, S01_SAFE_END = 0x284B, 0x5C78


def main():
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    name, offset, new = sys.argv[1], int(sys.argv[2], 0), bytes.fromhex(sys.argv[3])
    raw = len(sys.argv) == 5 and sys.argv[4] == "--raw"
    require_closed()
    path = savedata_dir() / name
    if not path.is_file():
        sys.exit(f"{path} not found")
    data = bytearray(path.read_bytes())
    end = offset + len(new)
    if end > len(data):
        sys.exit("offset runs past the end of the file")
    backup = ARCHIVE / f"pre_poke_{stamp()}"
    backup.mkdir(parents=True)
    shutil.copy2(path, backup / name)
    old = bytes(data[offset:end])
    data[offset:end] = new

    note = ""
    if name.upper().endswith("S01") and not raw:
        overlaps_field = offset < S01_CHECK_AT + 2 and end > S01_CHECK_AT
        if overlaps_field:
            note = " | edit touches the checksum field itself, so it was not adjusted"
        elif offset >= S01_SAFE_START and end <= S01_SAFE_END:
            delta = sum(new) - sum(old)
            before = int.from_bytes(data[S01_CHECK_AT:S01_CHECK_AT + 2], "little")
            after = (before + delta) % 65536
            data[S01_CHECK_AT:S01_CHECK_AT + 2] = after.to_bytes(2, "little")
            note = f" | S01 checksum 0x{before:04X} -> 0x{after:04X} (delta {delta:+d})"
        else:
            note = " | WARNING: outside the verified checksum range; checksum NOT updated, the game may reject this file"

    path.write_bytes(data)
    msg = f"poke {name} @0x{offset:X}: {old.hex(' ')} -> {new.hex(' ')}{note} (original in archive/{backup.name})"
    log(msg)
    print(msg)


if __name__ == "__main__":
    main()
