"""CounterChao step 3: find where the Chao Garden save stores things, by diffing two copies of it.

Usage:  python diff_saves.py before.ALF after.ALF
Prints every byte offset that changed (grouped into runs) with old/new values in decimal and hex.
Read-only: never modifies either file.
"""
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: python diff_saves.py before after")
    a, b = (open(p, "rb").read() for p in sys.argv[1:])
    print(f"sizes: {len(a)} vs {len(b)}")
    n = min(len(a), len(b))
    diffs = [i for i in range(n) if a[i] != b[i]]
    print(f"changed bytes: {len(diffs)}")
    if not diffs:
        return
    runs, start, prev = [], diffs[0], diffs[0]
    for i in diffs[1:]:
        if i - prev > 4:  # gap bigger than 4 bytes starts a new run
            runs.append((start, prev))
            start = i
        prev = i
    runs.append((start, prev))
    for s, e in runs[:60]:
        print(f"offset 0x{s:06X}-0x{e:06X} ({e - s + 1} bytes)")
        print("   before:", a[s:e + 1].hex(" "))
        print("   after: ", b[s:e + 1].hex(" "))
    if len(runs) > 60:
        print(f"... {len(runs) - 60} more runs not shown")


if __name__ == "__main__":
    main()
