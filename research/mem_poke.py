"""CounterChao: write bytes into the running game's in-memory Chao save (the image find_base.py located).

Usage (game open, standing in the Chao Garden):
    python mem_poke.py OFFSET HEXBYTES
Example (rename Chao slot 1 from ABCDEFG to BCDEFGH):
    python mem_poke.py 0x3AB6 22232425262728

OFFSET is a FILE offset: the game's memory copy mirrors the save file, so file offset N lives at base + N.
It finds the base the same way find_base.py does, shows the old bytes, writes, and reads back to verify.
Nothing touches the save file directly; the game writes its own file (with its own checksum) when you leave the garden.
"""
import ctypes
import sys
from ctypes import wintypes

from find_base import CHUNK, SHELL1, SHELL1_OFF, SHELL2_OFF, SHELL2_X, SHELL2_Z, find_pid, k32, read, regions
from save_lab import log

k32.WriteProcessMemory.restype = wintypes.BOOL
k32.WriteProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                   ctypes.POINTER(ctypes.c_size_t)]
SAVE_SIZE = 0x10000


def locate_base(handle):
    bases = set()
    for region_base, size in regions(handle):
        for off in range(0, size, CHUNK):
            data = read(handle, region_base + off, min(CHUNK + 16, size - off))
            for m in SHELL1.finditer(data):
                base = region_base + off + m.start() - SHELL1_OFF
                s2 = read(handle, base + SHELL2_OFF, 12)
                if len(s2) == 12 and s2[:4] == SHELL2_X and s2[8:12] == SHELL2_Z:
                    bases.add(base)
    return bases


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    offset, new = int(sys.argv[1], 0), bytes.fromhex(sys.argv[2])
    if offset < 0 or offset + len(new) > SAVE_SIZE:
        sys.exit("offset runs outside the 64 KB save image")
    pid = find_pid()
    if not pid:
        sys.exit("sonic2app.exe is not running. Start the game and stand in the Chao Garden first.")
    handle = k32.OpenProcess(0x0438, False, pid)  # QUERY | VM_READ | VM_WRITE | VM_OPERATION
    if not handle:
        sys.exit(f"Could not open the game process for writing (Windows error {ctypes.get_last_error()}).")
    bases = locate_base(handle)
    if len(bases) != 1:
        sys.exit(f"Expected exactly one verified save image in memory, found {len(bases)}. "
                 "Run find_base.py and send me the output. Nothing was written.")
    base = bases.pop()
    old = read(handle, base + offset, len(new))
    print(f"save image base: 0x{base:X}")
    print(f"before at +0x{offset:X}: {old.hex(' ')}")
    buf = ctypes.create_string_buffer(new, len(new))
    wrote = ctypes.c_size_t(0)
    ok = k32.WriteProcessMemory(handle, ctypes.c_void_p(base + offset), buf, len(new), ctypes.byref(wrote))
    after = read(handle, base + offset, len(new))
    print(f"after  at +0x{offset:X}: {after.hex(' ')}")
    k32.CloseHandle(handle)
    if not ok or after != new:
        sys.exit("Write did not verify. Tell me what this printed.")
    log(f"mem_poke: base 0x{base:X} +0x{offset:X}: {old.hex(' ')} -> {new.hex(' ')} (in-game memory only)")
    print("OK. Leave the garden normally so the game saves it.")


if __name__ == "__main__":
    main()
