# Save format and memory notes

Findings from reverse engineering SA2 (Steam, 2012 PC port). File offsets are decimal-free hex. Verified on one
install; treat everything as observed, not guaranteed. Layout matches the community Chao Island hacking guide.

## Files
`resource/gd_PC/SAVEDATA/` under the game install:
- `SONIC2B__S01`: 24,576 bytes, story save.
- `SONIC2B__ALF`: 65,536 bytes, Chao save.

## S01 checksum
16-bit little-endian value at `0x2840` = byte sum (mod 65536) of the rest of the file. Verified for edits in
`0x284B-0x5C77`. Rings: 4-byte LE at `0x2870`. Game rejects a mismatch ("This file is defective").

## ALF checksum
8 bytes at `0xFAA4`. Algorithm not identified (not CRC32/16, Adler, sum, XOR, or common hashes). The game validates
it when loading a garden; a mismatch gives "File not loaded!" and a blank garden. So files are never edited on disk.

## In-memory image
The whole ALF is kept in a static buffer. Observed base `0x19F3420` on every launch (memory address = base + file
offset). The image is loaded on entering the Chao World lobby and stays (stale) after leaving. The game rewrites the
file and checksum itself when you leave the garden or exit. Editing memory therefore needs no checksum work.

## Layout (file offsets)
| Offset | Content |
|--------|---------|
| `0x3108` | fruits (24) |
| `0x3428` | seeds (12) |
| `0x3518` | hats (24); type 16 = eggshell |
| `0x36F8` | animals/drives: 10 slots x 0x14 bytes |
| `0x3AA4 + n*0x800` | Chao slots (24); name at +0x12 |

Animal record, 20 bytes, `<HHI3f`: type, garden (1 = neutral), 4 unknown bytes, x, y, z. Empty slot: `ff ff ff ff`.
Types: 0-14 common animals, 15-20 rare, 21-24 drives (yellow, green, red, purple). The game snaps y to the ground.

## Process access
OpenProcess (0x0410 read, 0x0438 read/write), VirtualQueryEx, ReadProcessMemory, WriteProcessMemory via ctypes.
No admin rights needed when the game runs as the same user.

## Credits
Chao layout cross-checked against the Chao Island SA2 hacking guide.
