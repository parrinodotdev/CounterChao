"""Pure knowledge of the Chao save image layout (file offset N lives at image base + N). See docs/save-format.md."""
import struct

SAVE_SIZE = 0x10000
ANIMALS_OFF, SLOTS, SLOT = 0x36F8, 10, 0x14   # 10 slots x 20 bytes: type, garden, 4 unknown, x, y, z
GARDEN_NEUTRAL = 1
DEFAULT_AT = (11.0, 0.0, -100.0)
SPACING = 2.5
EMPTY = b"\xff\xff\xff\xff"


def array_ok(raw):
    """True if raw looks like the animal array: every slot empty, or type <= 24 and garden in 1..3."""
    if len(raw) != SLOTS * SLOT:
        return False
    for i in range(SLOTS):
        t, g = struct.unpack_from("<HH", raw, i * SLOT)
        if not ((t == 0xFFFF and g == 0xFFFF) or (t <= 24 and g in (1, 2, 3))):
            return False
    return True


def free_slots(raw):
    return [i for i in range(SLOTS) if raw[i * SLOT:i * SLOT + 4] == EMPTY]


def position(slot, at=DEFAULT_AT):
    """Each slot gets its own cell on a 4-wide grid so items do not overlap. The game snaps y to the ground."""
    return at[0] + (slot % 4) * SPACING, at[1], at[2] + (slot // 4) * SPACING


def record(item_type, slot, at=DEFAULT_AT):
    x, y, z = position(slot, at)
    return struct.pack("<HHI3f", item_type, GARDEN_NEUTRAL, 0, x, y, z)


def plan(raw, types, at=DEFAULT_AT, partial=False):
    """Return [(slot, record_bytes)] for as many items as fit. With partial=False, raise if they do not all fit."""
    free = free_slots(raw)
    if len(types) > len(free) and not partial:
        raise ValueError(f"{len(types)} item(s) requested but only {len(free)} slot(s) are free")
    return [(slot, record(t, slot, at)) for slot, t in zip(free, types)]
