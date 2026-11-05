"""CounterChao: show bytes of the live Chao save image in the running game's memory. READ-ONLY.

Usage (game open, Chao save loaded):
    python mem_dump.py OFFSET LENGTH [--records]
Examples:
    python mem_dump.py 0x36F8 0x78 --records     # 6 object records starting at the drive (20 bytes each)
    python mem_dump.py 0x3AB6 0x10                # plain hex rows

OFFSET is a file offset (memory = base + offset). --records decodes 20-byte records as: tag, 4 unknown bytes, x/y/z floats.
"""
import struct
import sys

from find_base import find_pid, k32, read
from mem_poke import SAVE_SIZE, locate_base


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    records = "--records" in sys.argv
    if len(args) != 2:
        sys.exit(__doc__)
    offset, length = int(args[0], 0), int(args[1], 0)
    if offset < 0 or length <= 0 or offset + length > SAVE_SIZE:
        sys.exit("range falls outside the 64 KB save image")
    pid = find_pid()
    if not pid:
        sys.exit("sonic2app.exe is not running.")
    handle = k32.OpenProcess(0x0410, False, pid)
    if not handle:
        sys.exit("Could not open the game process.")
    bases = locate_base(handle)
    if len(bases) != 1:
        sys.exit(f"Expected exactly one verified save image in memory, found {len(bases)}. "
                 "The Chao save may not be loaded right now (are you in the lobby or a garden?).")
    base = next(iter(bases))
    data = read(handle, base + offset, length)
    k32.CloseHandle(handle)
    print(f"image base 0x{base:X}")
    if records:
        for i in range(0, len(data) - 19, 20):
            rec = data[i:i + 20]
            x, y, z = struct.unpack("<3f", rec[8:20])
            print(f"+0x{offset + i:04X}  tag {rec[:4].hex(' ')}  unk {rec[4:8].hex(' ')}  pos ({x:.2f}, {y:.2f}, {z:.2f})")
    else:
        for i in range(0, len(data), 16):
            print(f"+0x{offset + i:04X}  {data[i:i + 16].hex(' ')}")


if __name__ == "__main__":
    main()
