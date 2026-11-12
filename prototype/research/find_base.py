"""CounterChao: find where the running game keeps its Chao Garden save data in memory. READ-ONLY, writes nothing.

Run while the game is open and you are standing inside the Chao Garden (the game loads the Chao save then):
    python find_base.py

It scans the game's memory for the two eggshell records we found in the save file, then checks the Chao name
bytes (21 22 23 24 25 26 27 = "ABCDEFG" in the game's character table) and prints the drive record it finds.
If the in-memory data is laid out like the file, one hit will pass every check and we get a base address.
"""
import ctypes
import re
import struct
import subprocess
import sys
from ctypes import wintypes

GAME_EXE = "sonic2app.exe"
SHELL1 = re.compile(b"\x0c\xab\x62\x41....\x53\xfe\x92\xc2", re.S)  # shell 1 record: x, (y varies), z
SHELL1_OFF, SHELL2_OFF = 0x3520, 0x3534
SHELL2_X, SHELL2_Z = bytes.fromhex("9a8f1541"), bytes.fromhex("d862dcc2")
NAME_OFF, NAME = 0x3AB6, bytes(range(0x21, 0x28))
DRIVE_TAG_OFF, DRIVE_POS_OFF = 0x36F8, 0x3700
CHUNK = 4 * 1024 * 1024

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = wintypes.HANDLE
k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
k32.VirtualQueryEx.restype = ctypes.c_size_t
k32.VirtualQueryEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
k32.ReadProcessMemory.restype = wintypes.BOOL
k32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                  ctypes.POINTER(ctypes.c_size_t)]
k32.CloseHandle.argtypes = [wintypes.HANDLE]

_mbi_fields = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
               ("AllocationProtect", wintypes.DWORD)]
if ctypes.sizeof(ctypes.c_void_p) == 8:
    _mbi_fields.append(("PartitionId", wintypes.WORD))
_mbi_fields += [("RegionSize", ctypes.c_size_t), ("State", wintypes.DWORD),
                ("Protect", wintypes.DWORD), ("Type", wintypes.DWORD)]


class MBI(ctypes.Structure):
    _fields_ = _mbi_fields


def find_pid():
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {GAME_EXE}", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) > 1 and parts[0].lower() == GAME_EXE:
            return int(parts[1])
    return None


def read(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    got = ctypes.c_size_t(0)
    k32.ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(got))
    return buf.raw[:got.value]


def regions(handle):
    addr, mbi = 0, MBI()
    while k32.VirtualQueryEx(handle, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
        base, size = mbi.BaseAddress or 0, mbi.RegionSize
        if mbi.State == 0x1000 and not (mbi.Protect & 0x101):  # committed, not NOACCESS / GUARD
            yield base, size
        addr = base + size
        if addr >= 0x7FFFFFFFFFFF:
            break


def main():
    pid = find_pid()
    if not pid:
        sys.exit(f"{GAME_EXE} is not running. Start the game and stand in the Chao Garden first.")
    handle = k32.OpenProcess(0x0410, False, pid)  # QUERY_INFORMATION | VM_READ
    if not handle:
        sys.exit(f"Could not open the game process (Windows error {ctypes.get_last_error()}). "
                 "Use a normal (non-admin) PowerShell window if the game was started normally.")
    hits = set()
    scanned = 0
    for base, size in regions(handle):
        for off in range(0, size, CHUNK):
            data = read(handle, base + off, min(CHUNK + 16, size - off))
            scanned += len(data)
            for m in SHELL1.finditer(data):
                hits.add(base + off + m.start())
    k32.CloseHandle(handle)
    print(f"scanned {scanned / 1e6:.0f} MB of game memory, {len(hits)} candidate hit(s) for the shell-1 record")
    if not hits:
        print("No hits. Either the in-memory data is not laid out like the file, or the garden is not loaded.")
        return

    handle = k32.OpenProcess(0x0410, False, pid)
    found = False
    for hit in sorted(hits):
        base = hit - SHELL1_OFF
        s2 = read(handle, base + SHELL2_OFF, 12)
        shell2_ok = len(s2) == 12 and s2[:4] == SHELL2_X and s2[8:12] == SHELL2_Z
        name = read(handle, base + NAME_OFF, 7)
        name_ok = name == NAME
        print(f"\nhit at 0x{hit:X} -> candidate base 0x{base:X}: shell 2 {'OK' if shell2_ok else 'no'}, "
              f"name bytes {name.hex(' ')} {'OK' if name_ok else 'differ'}")
        if shell2_ok:
            tag = read(handle, base + DRIVE_TAG_OFF, 4)
            pos = read(handle, base + DRIVE_POS_OFF, 12)
            if len(pos) == 12:
                x, y, z = struct.unpack("<3f", pos)
                print(f"   drive record: tag {tag.hex(' ')}, position ({x:.2f}, {y:.2f}, {z:.2f})")
        if shell2_ok and name_ok:
            found = True
            print(f"   >>> FOUND: the Chao save is in memory starting at 0x{base:X} (file offset 0 = this address)")
    k32.CloseHandle(handle)
    if not found:
        print("\nNo candidate passed every check. Paste this output and tell me what you did in the garden.")


if __name__ == "__main__":
    main()
