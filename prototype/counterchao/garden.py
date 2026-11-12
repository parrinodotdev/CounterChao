"""Find the Chao save image inside the running game and write items into its animal array."""
import re

from . import layout, winmem

FIXED_BASE = 0x19F3420   # where the image sat on every launch so far; verified before use, scan is the fallback
# Fallback scan signature: the two eggshell records of a fresh save (see docs/save-format.md)
SHELL1 = re.compile(b"\x0c\xab\x62\x41....\x53\xfe\x92\xc2", re.S)
SHELL1_OFF, SHELL2_OFF = 0x3520, 0x3534
SHELL2_X, SHELL2_Z = bytes.fromhex("9a8f1541"), bytes.fromhex("d862dcc2")


class GardenError(Exception):
    pass


def scan_for_base(handle):
    bases = set()
    for region_base, size in winmem.regions(handle):
        for off in range(0, size, winmem.CHUNK):
            data = winmem.read(handle, region_base + off, min(winmem.CHUNK + 16, size - off))
            for m in SHELL1.finditer(data):
                base = region_base + off + m.start() - SHELL1_OFF
                s2 = winmem.read(handle, base + SHELL2_OFF, 12)
                if len(s2) == 12 and s2[:4] == SHELL2_X and s2[8:12] == SHELL2_Z:
                    bases.add(base)
    return bases


def get_base(handle):
    raw = winmem.read(handle, FIXED_BASE + layout.ANIMALS_OFF, layout.SLOTS * layout.SLOT)
    if layout.array_ok(raw):
        return FIXED_BASE
    found = scan_for_base(handle)
    return next(iter(found)) if len(found) == 1 else None


def deliver(types, at=layout.DEFAULT_AT, partial=True, say=print):
    """Write items (ids) into free slots. Returns the number written. Raises GardenError on any problem."""
    pid = winmem.find_pid()
    if not pid:
        raise GardenError("sonic2app.exe is not running.")
    try:
        handle = winmem.open_process(pid, write=True)
    except OSError as e:
        raise GardenError(str(e)) from None
    try:
        base = get_base(handle)
        if base is None:
            raise GardenError("Could not find a valid Chao save image. Is the Chao World lobby or a garden loaded? "
                              "Nothing written.")
        raw = winmem.read(handle, base + layout.ANIMALS_OFF, layout.SLOTS * layout.SLOT)
        free = layout.free_slots(raw)
        say(f"image base 0x{base:X}; free animal slots: {len(free)} of {layout.SLOTS}")
        try:
            writes = layout.plan(raw, types, at, partial)
        except ValueError as e:
            raise GardenError(f"{e}. Nothing written.") from None
        if len(writes) < len(types):
            say(f"Only {len(free)} slot(s) free: delivering {len(writes)} of {len(types)}, the rest stay pending.")
        done = 0
        for slot, rec in writes:
            if not winmem.write(handle, base + layout.ANIMALS_OFF + slot * layout.SLOT, rec):
                raise GardenError(f"Write to slot {slot} did not verify; stopping after {done} item(s).")
            done += 1
        return done
    finally:
        winmem.close(handle)
