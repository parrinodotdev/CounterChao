"""Pure reward logic: match stats + rules -> list of item names. No I/O, no game access."""
import random

from .items import BY_NAME


def validate(rules):
    """Return a list of problems with a rules dict (empty list = fine)."""
    problems = []
    names = list(rules["drives"]) + rules["animals"]["pool"] + rules["rare"]["pool"]
    bad = [n for n in names if n not in BY_NAME]
    if bad:
        problems.append("unknown item names: " + ", ".join(bad))
    if rules.get("priority", "animals") not in ("animals", "drives"):
        problems.append('"priority" must be "animals" or "drives"')
    return problems


def stat(stats, name):
    v = stats.get(name)
    return v if isinstance(v, (int, float)) else 0


def rewards_for_match(match_id, stats, rules):
    """Return (items, reasons). The same match id always yields the same animals."""
    rounds = int(stat(stats, "rounds_count"))
    if rounds < rules["min_rounds"]:
        return [], [f"skipped: only {rounds} rounds"]
    reasons, drives = [], []
    for name, r in rules["drives"].items():
        n = int(stat(stats, r["stat"]) // r["per"])
        if n:
            drives += [name] * n
            reasons.append(f"{n} x {name} ({int(stat(stats, r['stat']))} {r['stat']})")
    parts = [(r["stat"], int(stat(stats, r["stat"]) // r["per"])) for r in rules["animals"]["stats"]]
    k = sum(n for _, n in parts)
    animals = []
    if k:
        rng = random.Random(match_id)
        animals = [rng.choice(rules["animals"]["pool"]) for _ in range(k)]
        reasons.append(f"{k} x animal (" + ", ".join(f"{s} {n}" for s, n in parts if n) + ")")
    rare = []
    rr = rules["rare"]
    if stat(stats, rr["stat"]) >= rr["at_least"]:
        rare = [random.Random(match_id + "rare").choice(rr["pool"])]
        reasons.append(f"1 x rare ({rr['stat']} {stat(stats, rr['stat']):.3f})")
    body = (drives + animals) if rules.get("priority") == "drives" else (animals + drives)
    return (rare + body)[: rules["max_items_per_match"]], reasons
