"""CounterChao: try to identify the checksum in SONIC2B__ALF (8 bytes at 0xFAA4).

Reads every unique ALF copy it can find in the lab folder, tries common checksum/hash functions over many
byte ranges, and prints any combination that reproduces the stored value on EVERY sample. Read-only.

Usage (game can be open or closed):  python find_alf_checksum.py
"""
import hashlib
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent  # repo root
LAB = ROOT / "lab"
FIELD, FIELD_LEN, SIZE = 0xFAA4, 8, 0x10000

STARTS = list(range(0, 0x100)) + [0x200, 0x400, 0x800, 0x1000, 0x2000, 0x3000, 0x3A00]
ENDS = [0xFAA4, 0xFAAC, 0x10000]


def load_samples():
    seen, out = set(), []
    paths = (list(LAB.glob("snapshots/*/SONIC2B__ALF"))
             + list(LAB.glob("archive/original_*/SONIC2B__ALF"))
             + list(LAB.glob("archive/early_experiments/*.ALF")))
    for p in sorted(paths):
        d = p.read_bytes()
        if len(d) == SIZE and d not in seen:
            seen.add(d)
            out.append((str(p.relative_to(ROOT)), d))
    return out


def region(data, start, end, mode):
    """mode: 'plain' (field not inside), 'zeroed' or 'skipped' (field inside the range)."""
    buf = data[start:end]
    if mode == "plain":
        return buf
    a, b = FIELD - start, FIELD + FIELD_LEN - start
    if mode == "zeroed":
        return buf[:a] + bytes(FIELD_LEN) + buf[b:]
    return buf[:a] + buf[b:]


def digests(buf):
    c = zlib.crc32(buf) & 0xFFFFFFFF
    yield "crc32-le", c.to_bytes(4, "little")
    yield "crc32-be", c.to_bytes(4, "big")
    j = c ^ 0xFFFFFFFF
    yield "jamcrc-le", j.to_bytes(4, "little")
    yield "jamcrc-be", j.to_bytes(4, "big")
    a = zlib.adler32(buf) & 0xFFFFFFFF
    yield "adler32-le", a.to_bytes(4, "little")
    yield "adler32-be", a.to_bytes(4, "big")
    s = sum(buf) & 0xFFFFFFFF
    yield "bytesum32-le", s.to_bytes(4, "little")
    yield "bytesum32-be", s.to_bytes(4, "big")
    for name in ("md5", "sha1", "sha256"):
        h = hashlib.new(name, buf).digest()
        yield f"{name}-first8", h[:8]
        yield f"{name}-last8", h[-8:]


def matches(stored, name, dg):
    if len(dg) == 8 and dg == stored:
        return [(name, "all 8 bytes")]
    if len(dg) == 4:
        found = []
        if dg == stored[:4]:
            found.append((name, "first 4 bytes"))
        if dg == stored[4:]:
            found.append((name, "last 4 bytes"))
        return found
    return []


def candidates(data):
    stored = data[FIELD:FIELD + FIELD_LEN]
    out = set()
    for start in STARTS:
        for end in ENDS:
            modes = ["plain"] if end <= FIELD else ["zeroed", "skipped"]
            for mode in modes:
                buf = region(data, start, end, mode)
                for name, dg in digests(buf):
                    for alg, where in matches(stored, name, dg):
                        out.add((alg, start, end, mode, where))
    return out


def still_matches(data, cand):
    alg, start, end, mode, where = cand
    stored = data[FIELD:FIELD + FIELD_LEN]
    for name, dg in digests(region(data, start, end, mode)):
        if name == alg and (where, ) and any(w == where for _, w in matches(stored, name, dg)):
            return True
    return False


def main():
    samples = load_samples()
    print(f"{len(samples)} unique ALF samples found")
    if len(samples) < 2:
        print("Need at least 2 samples (3+ is better). Take more snapshots with save_lab.py and rerun.")
        return
    cands = candidates(samples[0][1])
    print(f"{len(cands)} candidates matched the first sample ({samples[0][0]})")
    for path, data in samples[1:]:
        cands = {c for c in cands if still_matches(data, c)}
    if not cands:
        print("\nNO MATCH: none of the standard checksums/hashes over these ranges reproduce the stored value.")
        print("The ALF check is probably a custom or keyed algorithm.")
        return
    print(f"\n{len(cands)} candidate(s) match EVERY sample:")
    for alg, start, end, mode, where in sorted(cands):
        print(f"  {alg} over 0x{start:X}-0x{end:X} ({mode}) -> {where} of the field")


if __name__ == "__main__":
    main()
