"""Which matches were already rewarded, and items waiting for free garden slots."""
import json
from pathlib import Path


def load(path):
    p = Path(path)
    return json.loads(p.read_text(encoding="utf-8")) if p.exists() else None


def save(path, processed, pending):
    Path(path).write_text(json.dumps({"processed": sorted(processed), "pending": list(pending)}, indent=2),
                          encoding="utf-8")
