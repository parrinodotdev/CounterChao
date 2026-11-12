"""CounterChao: test whether the ALF check value (8 bytes at 0xFAA4) behaves like a word-sum or XOR checksum.

For every pair of ALF samples it compares how the field changed with how the rest of the file changed,
using word sizes 1/2/4/8, both byte orders, and sum or XOR. Prints every combination that holds for ALL pairs.
Read-only. Usage: python analyze_alf_field.py
"""
from itertools import combinations
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent  # repo root
LAB = ROOT / "lab"
FIELD, FIELD_LEN, SIZE = 0xFAA4, 8, 0x10000


def load_samples():
    seen, out = set(), []
    paths = (list(LAB.glob("snapshots/*/SONIC2B__ALF"))
             + list(LAB.glob("archive/original_*/SONIC2B__ALF"))
             + list(LAB.glob("archive/early_experiments/*.ALF")))
    for p in sorted(paths):
        d = p.read_bytes()
        if len(d) == SIZE and d not in seen:
            seen.add(d)
            out.append(d)
    return out


def word_stats(a, b, w, order):
    """(sum of word differences, xor of word differences) over changed words outside the field."""
    total, xored = 0, 0
    for i in range(0, SIZE - w + 1, w):
        if i < FIELD + FIELD_LEN and i + w > FIELD:
            continue
        x, y = a[i:i + w], b[i:i + w]
        if x != y:
            xv, yv = int.from_bytes(x, order), int.from_bytes(y, order)
            total += yv - xv
            xored ^= xv ^ yv
    return total, xored


def field_parts(data, p, order):
    f = data[FIELD:FIELD + FIELD_LEN]
    return [int.from_bytes(f[k:k + p], order) for k in range(0, FIELD_LEN, p)]


def main():
    samples = load_samples()
    pairs = list(combinations(range(len(samples)), 2))
    print(f"{len(samples)} unique ALF samples, {len(pairs)} pairs")
    if len(pairs) < 3:
        print("Need at least 3 samples for a useful test.")
        return
    hits = []
    for w in (1, 2, 4, 8):
        for word_order in ("little", "big"):
            stats = {pr: word_stats(samples[pr[0]], samples[pr[1]], w, word_order) for pr in pairs}
            for p in (8, 4, 2, 1):
                mod = 1 << (8 * p)
                for part_order in ("little", "big"):
                    for op in ("sum", "xor"):
                        for j in range(FIELD_LEN // p):
                            ok = True
                            for (i, k) in pairs:
                                fa = field_parts(samples[i], p, part_order)[j]
                                fb = field_parts(samples[k], p, part_order)[j]
                                total, xored = stats[(i, k)]
                                if op == "sum":
                                    ok = ((fb - fa) - total) % mod == 0
                                else:
                                    ok = (fa ^ fb) == (xored % mod)
                                if not ok:
                                    break
                            if ok:
                                hits.append((w, word_order, op, p, part_order, j))
    if not hits:
        print("\nNO RELATION: the field is not a plain word-sum or XOR of the rest of the file.")
        print("That points to a CRC/hash with a custom polynomial, a keyed hash, or a checksum over a limited region.")
        return
    print(f"\n{len(hits)} combination(s) hold for every pair:")
    for w, wo, op, p, po, j in hits:
        print(f"  {op} of {w}-byte {wo}-endian words  ==  {p}-byte {po}-endian field part #{j}")


if __name__ == "__main__":
    main()
