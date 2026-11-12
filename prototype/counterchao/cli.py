"""Command-line entry point: python -m counterchao [--plan | --replay N | --yes] [--config PATH]"""
import argparse
import json
import sys
from pathlib import Path

from . import leetify, rewards, state
from .items import BY_NAME, parse_type
from .layout import DEFAULT_AT

ROOT = Path(__file__).resolve().parent.parent


def load_json(path, what):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except FileNotFoundError:
        sys.exit(f"{what} not found at {path}")


def parse_args(argv):
    p = argparse.ArgumentParser(prog="counterchao", description=__doc__)
    p.add_argument("--plan", action="store_true", help="show what would be given; touch nothing")
    p.add_argument("--replay", type=int, default=0, metavar="N", help="TEST: reward the latest N matches, save no state")
    p.add_argument("--yes", action="store_true", help='skip the "are you in the lobby?" question')
    p.add_argument("--config", default=str(ROOT / "config.json"))
    p.add_argument("--rules", default=str(ROOT / "rewards.json"))
    p.add_argument("--state", default=str(ROOT / "counterchao_state.json"))
    p.add_argument("--give", nargs="+", metavar="ITEM", help="TEST: deliver these items (name or id), skip Leetify")
    return p.parse_args(argv)


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if args.give:
        try:
            types = [parse_type(a) for a in args.give]
        except ValueError as e:
            sys.exit(str(e))
        _deliver_or_exit(types)
        return

    cfg = load_json(args.config, "config file")
    rules = load_json(args.rules, "rules file")
    problems = rewards.validate(rules)
    if problems:
        sys.exit("rewards.json: " + "; ".join(problems))
    steam = str(cfg.get("steam64_id", "")).strip()
    if not steam.isdigit():
        sys.exit("config: set steam64_id to your SteamID64 (a 17-digit number).")
    key = str(cfg.get("leetify_api_key", "")).strip()
    if key.startswith("PASTE_"):   # untouched placeholder from config.example.json
        key = ""
    try:
        matches = leetify.fetch_matches(key, steam)  # newest first
    except leetify.LeetifyError as e:
        sys.exit(str(e))
    if not matches:
        sys.exit("Leetify returned no matches.")

    st = state.load(args.state)
    if args.replay:
        new = list(reversed(matches[:args.replay]))
        print(f"REPLAY: using your latest {len(new)} match(es), ignoring history; no state will be saved.")
    elif st is None:
        new = matches[:1]
        print("First run: rewarding only your most recent match; older matches are marked as already handled.")
    else:
        seen = set(st["processed"])
        unseen = [m for m in matches if m["id"] not in seen]
        cap = rules["max_matches_per_run"]
        new = list(reversed(unseen[:cap]))
        if len(unseen) > cap:
            print(f"{len(unseen)} unrewarded matches; rewarding the newest {cap}, skipping {len(unseen) - cap} older ones.")

    pending = list(st["pending"]) if st and not args.replay else []
    for m in new:
        s = leetify.my_stats(m, steam)
        if not s:
            print(f"{m['finished_at'][:10]} {m['map_name']}: no stats for you in this match, skipped")
            continue
        items, reasons = rewards.rewards_for_match(m["id"], s, rules)
        st_ = rewards.stat
        print(f"\n{m['finished_at'][:10]} {m['map_name']}  K/D {int(st_(s, 'total_kills'))}/{int(st_(s, 'total_deaths'))}"
              f"  rating {st_(s, 'leetify_rating'):+.3f}")
        for line in reasons:
            print("   " + line)
        print(f"   -> {len(items)} item(s): {', '.join(items) if items else 'none'}")
        pending += items

    print(f"\nItems waiting to be delivered: {len(pending)}")
    if args.plan:
        print("(--plan: nothing delivered, nothing saved)")
        return

    delivered = 0
    if pending:
        if not args.yes:
            answer = input("Is the game sitting in the Chao World lobby right now (not a menu or a garden)? [y/N] ")
            if answer.strip().lower() != "y":
                sys.exit("Cancelled. Nothing delivered, nothing saved.")
        delivered = _deliver_or_exit([BY_NAME[n] for n in pending])
    left = pending[delivered:]
    if delivered:
        print(f"Delivered {delivered} item(s): {', '.join(pending[:delivered])}")
    if left:
        print(f"{len(left)} item(s) still pending (garden item slots are full): {', '.join(left)}")

    if args.replay:
        print("(--replay: state not saved)")
        return
    handled = {m["id"] for m in matches}   # everything fetched is now handled (older ones are retired, not rewarded)
    done = set(st["processed"]) | handled if st else handled
    state.save(args.state, done, left)
    print("State saved. Walk into the garden to see your rewards.")


def _deliver_or_exit(types):
    from . import garden   # imported here so everything else works off Windows (tests, --plan)
    try:
        n = garden.deliver(types, DEFAULT_AT, partial=True)
    except garden.GardenError as e:
        sys.exit(str(e))
    return n


if __name__ == "__main__":
    main()
