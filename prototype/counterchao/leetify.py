"""Leetify public API client (stdlib only)."""
import json
import urllib.error
import urllib.parse
import urllib.request

API = "https://api-public.cs-prod.leetify.com"


class LeetifyError(Exception):
    pass


def build_request(key, steam64):
    """The API key is optional: the header is only sent when a key is given."""
    url = f"{API}/v3/profile/matches?{urllib.parse.urlencode({'steam64_id': steam64})}"
    headers = {"User-Agent": "CounterChao/0.2"}
    if key:
        headers["_leetify_key"] = key
    return urllib.request.Request(url, headers=headers)


def fetch_matches(key, steam64):
    """Newest-first list of recent matches for a public profile."""
    req = build_request(key, steam64)
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        hint = ""
        if e.code in (401, 403, 429) and not key:
            hint = " (no API key was sent; adding one to config.json may help)"
        raise LeetifyError(f"Leetify returned {e.code}{hint}: {e.read().decode('utf-8', 'replace')[:200]}") from None
    except urllib.error.URLError as e:
        raise LeetifyError(f"Could not reach Leetify: {e.reason}") from None


def my_stats(match, steam64):
    return next((s for s in match.get("stats", []) if s.get("steam64_id") == steam64), None)
