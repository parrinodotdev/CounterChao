"""CounterChao step 2: (a) summarize your last 100 matches so reward thresholds can be calibrated,
(b) find out whether match-detail data includes clutches / bomb plants / defuses.

Run from the CounterChao folder:  python explore_data.py
Saves: history_summary.txt, match_detail_sample.json (if a detail endpoint works)
"""
import json
import statistics
import sys
import urllib.error
import urllib.parse
import urllib.request

BASE = "https://api-public.cs-prod.leetify.com"


def get(path, params, key):
    url = f"{BASE}{path}" + (f"?{urllib.parse.urlencode(params)}" if params else "")
    req = urllib.request.Request(url, headers={"_leetify_key": key, "User-Agent": "CounterChao/0.1"})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return r.status, json.load(r)
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")[:300]


def main():
    try:
        cfg = json.load(open("config.json", encoding="utf-8"))
    except FileNotFoundError:
        sys.exit("config.json not found.")
    key = cfg["leetify_api_key"]
    steam = sys.argv[1] if len(sys.argv) > 1 else cfg["steam64_id"]  # optional: another player's steam64 id

    status, matches = get("/v3/profile/matches", {"steam64_id": steam}, key)
    if status != 200:
        sys.exit(f"history failed: {status} {matches}")

    # --- (a) history summary ---
    dates = [m["finished_at"] for m in matches]
    lines = [
        f"matches: {len(matches)}",
        f"first in list: {dates[0]}   last in list: {dates[-1]}",
        f"list is newest-first: {dates[0] > dates[-1]}",
        "",
    ]
    fields = ["total_kills", "total_deaths", "kd_ratio", "total_assists", "dpr", "total_hs_kills",
              "multi3k", "multi4k", "multi5k", "flashbang_hit_foe", "flashbang_leading_to_kill",
              "he_foes_damage_avg", "trade_kills_succeed", "leetify_rating", "rounds_count"]
    for field in fields:
        vals = []
        for m in matches:
            mine = [s for s in m.get("stats", []) if s.get("steam64_id") == steam]
            if mine and mine[0].get(field) is not None:
                vals.append(mine[0][field])
        if vals:
            lines.append(f"{field:28} min {min(vals):>8.3f}  median {statistics.median(vals):>8.3f}  max {max(vals):>8.3f}")
    open(f"history_summary_{steam}.txt", "w", encoding="utf-8").write("\n".join(lines))
    print("\n".join(lines))

    # --- (b) look for a match-detail endpoint ---
    m = matches[0]
    candidates = [
        f"/v2/matches/{m['id']}",
        f"/v3/matches/{m['id']}",
        f"/v2/matches/{m['data_source']}/{m['data_source_match_id']}",
    ] if "data_source" in m else [f"/v2/matches/{m['id']}"]
    print("\n== match detail endpoints ==")
    for path in candidates:
        st, body = get(path, None, key)
        print(path, "->", st)
        if st == 200:
            json.dump(body, open(f"match_detail_sample_{steam}.json", "w", encoding="utf-8"), indent=2)
            print(f"saved match_detail_sample_{steam}.json")
            break


if __name__ == "__main__":
    main()
