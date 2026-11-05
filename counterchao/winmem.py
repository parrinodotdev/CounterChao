"""Windows process-memory access via ctypes (stdlib). Needs only a normal user account, no admin."""
import ctypes
import subprocess
from ctypes import wintypes

GAME_EXE = "sonic2app.exe"
QUERY_READ = 0x0410          # PROCESS_QUERY_INFORMATION | PROCESS_VM_READ
QUERY_READ_WRITE = 0x0438    # + PROCESS_VM_WRITE | PROCESS_VM_OPERATION
CHUNK = 4 * 1024 * 1024

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = wintypes.HANDLE
k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
k32.VirtualQueryEx.restype = ctypes.c_size_t
k32.VirtualQueryEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
k32.ReadProcessMemory.restype = wintypes.BOOL
k32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                  ctypes.POINTER(ctypes.c_size_t)]
k32.WriteProcessMemory.restype = wintypes.BOOL
k32.WriteProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
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


def open_process(pid, write=False):
    handle = k32.OpenProcess(QUERY_READ_WRITE if write else QUERY_READ, False, pid)
    if not handle:
        raise OSError(f"Could not open the game process (Windows error {ctypes.get_last_error()}).")
    return handle


def read(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    got = ctypes.c_size_t(0)
    k32.ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(got))
    return buf.raw[:got.value]


def write(handle, addr, data):
    buf = ctypes.create_string_buffer(data, len(data))
    wrote = ctypes.c_size_t(0)
    ok = k32.WriteProcessMemory(handle, ctypes.c_void_p(addr), buf, len(data), ctypes.byref(wrote))
    return bool(ok) and read(handle, addr, len(data)) == data   # verified by reading back


def regions(handle):
    addr, mbi = 0, MBI()
    while k32.VirtualQueryEx(handle, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
        base, size = mbi.BaseAddress or 0, mbi.RegionSize
        if mbi.State == 0x1000 and not (mbi.Protect & 0x101):  # committed, not NOACCESS / GUARD
            yield base, size
        addr = base + size
        if addr >= 0x7FFFFFFFFFFF:
            break


def close(handle):
    k32.CloseHandle(handle)
