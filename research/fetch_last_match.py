"""CounterChao step 1: confirm the Leetify API works and see what a match looks like.

Usage (PowerShell, from the CounterChao folder):
    copy config.example.json config.json   # then paste your key + steam id into config.json
    python fetch_last_match.py

Stdlib only, nothing to install.
"""
import json
import sys
import urllib.error
import urllib.parse
import urllib.request

BASE = "https://api-public.cs-prod.leetify.com"


def load_config():
    try:
        with open("config.json", encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        sys.exit("config.json not found. Copy config.example.json to config.json and fill it in.")


def get(path, params, key):
    url = f"{BASE}{path}?{urllib.parse.urlencode(params)}"
    req = urllib.request.Request(url, headers={"_leetify_key": key, "User-Agent": "CounterChao/0.1"})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return r.status, json.load(r)
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")


def main():
    cfg = load_config()
    key, steam = cfg["leetify_api_key"], cfg["steam64_id"]

    print("== profile ==")
    status, body = get("/v3/profile", {"steam64_id": steam}, key)
    print("status:", status)
    print(json.dumps(body, indent=2)[:1500] if not isinstance(body, str) else body)

    print("\n== match history ==")
    status, body = get("/v3/profile/matches", {"steam64_id": steam}, key)
    print("status:", status)
    if isinstance(body, str):
        print(body)
        return
    matches = body if isinstance(body, list) else body.get("matches", body)
    print(f"matches returned: {len(matches) if hasattr(matches, '__len__') else '?'}")
    first = matches[0] if isinstance(matches, list) and matches else matches
    with open("last_match_sample.json", "w", encoding="utf-8") as f:
        json.dump(first, f, indent=2)
    print("saved most recent match to last_match_sample.json")


if __name__ == "__main__":
    main()
