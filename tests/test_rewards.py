import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from counterchao import layout, leetify, rewards  # noqa: E402
from counterchao.items import ANIMALS, BY_NAME, parse_type  # noqa: E402

RULES = json.loads((ROOT / "rewards.json").read_text(encoding="utf-8"))


class RulesFile(unittest.TestCase):
    def test_shipped_rules_validate(self):
        self.assertEqual(rewards.validate(RULES), [])

    def test_unknown_item_is_reported(self):
        bad = json.loads(json.dumps(RULES))
        bad["animals"]["pool"].append("nope")
        self.assertTrue(rewards.validate(bad))


class RewardLogic(unittest.TestCase):
    STATS = {"rounds_count": 20, "total_kills": 22, "total_hs_kills": 8, "trade_kills_succeed": 1,
             "flashbang_hit_foe": 6, "multi3k": 1, "flash_assist": 2, "leetify_rating": 0.01}

    def test_short_match_skipped(self):
        items, why = rewards.rewards_for_match("m1", {**self.STATS, "rounds_count": 9}, RULES)
        self.assertEqual(items, [])
        self.assertIn("skipped", why[0])

    def test_deterministic_per_match(self):
        a = rewards.rewards_for_match("match-abc", self.STATS, RULES)
        b = rewards.rewards_for_match("match-abc", self.STATS, RULES)
        self.assertEqual(a, b)

    def test_counts(self):
        rules = {**RULES, "max_items_per_match": 99}
        items, _ = rewards.rewards_for_match("m", self.STATS, rules)
        drives = [i for i in items if i.endswith("_drive")]
        self.assertEqual(drives.count("red_drive"), 22 // 5)
        self.assertEqual(drives.count("purple_drive"), 8 // 4)
        self.assertEqual(drives.count("green_drive"), 1)
        self.assertEqual(drives.count("yellow_drive"), 6 // 3)
        self.assertEqual(len(items) - len(drives), 3)   # multi3k 1 + flash_assist 2 animals

    def test_cap_and_rare(self):
        stats = {**self.STATS, "leetify_rating": 0.2, "total_kills": 100}
        items, _ = rewards.rewards_for_match("m", stats, RULES)
        self.assertEqual(len(items), RULES["max_items_per_match"])
        self.assertIn(items[0], RULES["rare"]["pool"])

    def test_priority_drives_keeps_drives_when_capped(self):
        rules = {**RULES, "priority": "drives", "max_items_per_match": 3}
        items, _ = rewards.rewards_for_match("m", self.STATS, rules)
        self.assertTrue(all(i.endswith("_drive") for i in items))

    def test_missing_stat_counts_as_zero(self):
        items, _ = rewards.rewards_for_match("m", {"rounds_count": 20}, RULES)
        self.assertEqual(items, [])


class Items(unittest.TestCase):
    def test_parse(self):
        self.assertEqual(parse_type("red_drive"), 23)
        self.assertEqual(parse_type("23"), 23)
        with self.assertRaises(ValueError):
            parse_type("sonic")

    def test_tables_agree(self):
        self.assertEqual(len(ANIMALS), len(BY_NAME))


class LeetifyRequest(unittest.TestCase):
    def test_key_header_only_when_key_given(self):
        with_key = leetify.build_request("abc", "7656119")
        without = leetify.build_request("", "7656119")
        self.assertEqual(with_key.get_header("_leetify_key"), "abc")
        self.assertIsNone(without.get_header("_leetify_key"))
        self.assertIn("steam64_id=7656119", without.full_url)


class Layout(unittest.TestCase):
    def empty(self):
        return b"\xff" * (layout.SLOTS * layout.SLOT)

    def test_empty_array_ok_and_all_free(self):
        raw = self.empty()
        self.assertTrue(layout.array_ok(raw))
        self.assertEqual(layout.free_slots(raw), list(range(10)))

    def test_garbage_rejected(self):
        self.assertFalse(layout.array_ok(b"\x00" * 3))
        self.assertFalse(layout.array_ok(b"\x63\x00\x01\x00" + b"\xff" * 196))   # type 99

    def test_record_is_20_bytes_and_slots_do_not_overlap(self):
        self.assertEqual(len(layout.record(23, 0)), layout.SLOT)
        spots = {layout.position(s)[0::2] for s in range(layout.SLOTS)}
        self.assertEqual(len(spots), layout.SLOTS)

    def test_plan_uses_free_slots_and_respects_partial(self):
        raw = bytearray(self.empty())
        raw[0:20] = layout.record(0, 0)   # slot 0 taken
        writes = layout.plan(bytes(raw), [1, 2], partial=False)
        self.assertEqual([s for s, _ in writes], [1, 2])
        with self.assertRaises(ValueError):
            layout.plan(bytes(raw), [1] * 10, partial=False)
        self.assertEqual(len(layout.plan(bytes(raw), [1] * 10, partial=True)), 9)


if __name__ == "__main__":
    unittest.main()
