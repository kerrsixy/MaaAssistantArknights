"""Evidence-preserving catalog refresh and effect extraction regressions."""

import copy
import hashlib
import json
import os
import tempfile
import unittest
import urllib.error
from collections import Counter
from pathlib import Path
from unittest.mock import Mock, patch

import blackflow_catalog_sync as catalog


def game_fixture():
    def item(identifier, name, kind, usage=""):
        return {
            "id": identifier,
            "name": name,
            "type": kind,
            "usage": usage,
            "description": "",
            "rarity": "NORMAL",
        }

    items = {
        "rogue_6_gold": item("rogue_6_gold", "源石锭", "GOLD"),
        "coin": item("coin", "Immediate coins", "RELIC"),
        "future": item("future", "Future effect", "RELIC"),
        "role": item("role", "Caster strength", "RELIC"),
        "conditional": item("conditional", "Future regions", "RELIC"),
        "move": item(
            "move",
            "Renamed vehicle",
            "SCRAP",
            "装载后能够移动至相邻8格内任意节点（可移动3次）",
        ),
        "goods": item("goods", "Renamed natural object", "SCRAP", "每次作战后估价+2"),
        "ticket": item("ticket", "狙击招募券", "RECRUIT_TICKET"),
    }

    def buff(key, **board):
        return {
            "key": key,
            "blackboard": [
                {
                    "key": name,
                    "value": 0 if isinstance(value, str) else value,
                    "valueStr": value if isinstance(value, str) else None,
                }
                for name, value in board.items()
            ],
        }

    relics = {
        "coin": {
            "id": "coin",
            "buffs": [buff("immediate_reward", id="rogue_6_gold", count=10)],
        },
        "future": {"id": "future", "buffs": [buff("unknown_update_mechanic", value=9)]},
        "role": {
            "id": "role",
            "buffs": [
                buff(
                    "char_attribute_mul",
                    **{"selector.profession": "caster", "atk": 0.2},
                )
            ],
        },
        "conditional": {
            "id": "conditional",
            "buffs": [buff("zone_into_reward", id="rogue_6_gold", count=4)],
        },
    }
    scrap = {
        "scrapItemToType": {"move": "MOVE", "goods": "GOODS"},
        "moveScrapData": {
            "move": {
                "count": 3,
                "range": "range",
                "rangeType": "RANGE",
                "node": ["ALL"],
                "step": 1,
                "isRandomMove": False,
                "nodeChangeTargetType": "NONE",
                "scrapId": "move",
                "scrapDesc": items["move"]["usage"],
                "sellPrice": 1,
            }
        },
        "goodsScrapData": {
            "goods": {
                "scrapId": "goods",
                "scrapDesc": items["goods"]["usage"],
                "sellPrice": 2,
            }
        },
        "passiveScrapData": {},
        "moveScrapRangeData": {"range": {"grids": [{"row": 1, "col": 1}]}},
    }
    return {
        "details": {
            "rogue_6": {
                "items": items,
                "relics": relics,
                "recruitTickets": {
                    "ticket": {"professionList": ["SNIPER"], "rarityList": ["TIER_6"]}
                },
            }
        },
        "modules": {"rogue_6": {"scrap": scrap}},
    }


def source(raw):
    return {
        "sha256": hashlib.sha256(raw).hexdigest(),
        "revision": None,
        "game_version": None,
    }


def built_fixture():
    table = game_fixture()
    raw = json.dumps(table).encode()
    return catalog.build_catalog(table, source(raw))


class CatalogSyncTests(unittest.TestCase):
    def test_structured_effects_preserve_role_and_trigger(self):
        built = built_fixture()
        self.assertEqual(built["items"]["coin"]["features"], {"immediate_ingots": 10})
        self.assertNotIn("attack_pct", built["items"]["role"]["features"])
        self.assertEqual(
            built["items"]["role"]["features"]["role_buffs"],
            [{"role": "Caster", "features": {"attack_pct": 0.2}}],
        )
        later = built["items"]["conditional"]["features"]
        self.assertNotIn("immediate_ingots", later)
        self.assertEqual(
            later["effects"][0],
            {"kind": "ingots", "amount": 4, "trigger": "zone", "params": {}},
        )

    def test_unknown_update_is_retained_and_requires_review(self):
        built = built_fixture()
        future = built["items"]["future"]
        self.assertEqual(
            future["unsupported_effects"][0]["key"], "unknown_update_mechanic"
        )
        self.assertFalse(future["extraction_complete"])
        self.assertEqual(future["features"], {})

    def test_unknown_item_type_cannot_silently_disappear(self):
        table = game_fixture()
        detail = table["details"]["rogue_6"]
        detail["items"]["new_kind"] = {
            "id": "new_kind",
            "name": "Future shop type",
            "type": "UNKNOWN_NEW_TYPE",
            "usage": "new rule",
        }
        built = catalog.build_catalog(table, source(b"test"))
        self.assertEqual(built["items"]["new_kind"]["kind"], "unknown")
        self.assertFalse(built["items"]["new_kind"]["extraction_complete"])

    def test_valid_json_with_lost_items_or_changed_values_is_not_a_good_cache(self):
        built = built_fixture()
        damaged = copy.deepcopy(built)
        del damaged["items"]["coin"]
        with self.assertRaisesRegex(ValueError, "coverage"):
            catalog.validate_catalog(damaged)
        damaged = copy.deepcopy(built)
        damaged["items"]["coin"]["features"]["immediate_ingots"] = 999
        with self.assertRaisesRegex(ValueError, "fingerprint"):
            catalog.validate_catalog(damaged)

    def test_unknown_selector_does_not_become_global_attack(self):
        table = game_fixture()
        table["details"]["rogue_6"]["relics"]["role"]["buffs"][0]["blackboard"].append(
            {"key": "selector.new_condition", "value": 1, "valueStr": None}
        )
        result = catalog.build_catalog(table, source(b"test"))["items"]["role"]
        self.assertEqual(result["features"], {})
        self.assertFalse(result["extraction_complete"])

    def test_new_vehicle_name_and_range_are_derived_from_data(self):
        built = built_fixture()
        item = built["items"]["move"]
        self.assertTrue(item["extraction_complete"])
        self.assertEqual(item["features"]["movement"]["charges"], 3)
        self.assertEqual(
            item["features"]["movement"]["grid_offsets"], [{"row": 1, "col": 1}]
        )
        self.assertEqual(item["features"]["sale_value"], 1)
        self.assertIsNone(item["base_price"])

    def test_growth_is_conditional_sale_value_not_immediate_cash(self):
        item = built_fixture()["items"]["goods"]
        self.assertTrue(item["extraction_complete"])
        self.assertNotIn("immediate_ingots", item["features"])
        self.assertEqual(
            item["features"]["effects"],
            [{"kind": "sale_value", "amount": 2, "trigger": "battle", "params": {}}],
        )

    def test_passive_activation_does_not_turn_into_a_global_bonus(self):
        features = {}
        self.assertTrue(
            catalog.extract_passive(
                {
                    "node": "BATTLES",
                    "buffStack": 1,
                    "scrapDesc": "装载加工品移动至作战节点时，本次战斗我方单位攻击速度+35",
                },
                features,
            )
        )
        self.assertNotIn("attack_speed", features)
        self.assertIsNone(features["activation_condition"]["max_activations"])
        self.assertEqual(features["effects"][0]["trigger"], "next_battle")
        self.assertEqual(features["effects"][0]["amount"], 35)
        features = {}
        self.assertTrue(
            catalog.extract_passive(
                {
                    "node": "ALL",
                    "buffStack": 1,
                    "scrapDesc": "装载加工品移动至任意节点时，获得3点护盾（可激活1次）",
                },
                features,
            )
        )
        self.assertEqual(features["activation_condition"]["max_activations"], 1)
        self.assertNotIn("shield", features)

    def test_sale_trigger_cannot_be_mistaken_for_a_vehicle_move(self):
        features = {}
        self.assertTrue(
            catalog.extract_passive(
                {
                    "node": 2101248,
                    "buffStack": 1,
                    "scrapDesc": "卖出任意一件价值不低于50源石锭的自然物时，获得2个珍贵的随机收藏品（可激活1次）",
                },
                features,
            )
        )
        condition = features["activation_condition"]
        self.assertEqual(condition["trigger"], "sell_natural_scrap")
        self.assertEqual(condition["minimum_sale_value"], 50)
        self.assertEqual(features["effects"][0]["trigger"], "sell_natural_scrap")

    def test_partial_source_or_missing_range_cannot_be_published(self):
        table = game_fixture()
        del table["modules"]["rogue_6"]["scrap"]["moveScrapRangeData"]["range"]
        with self.assertRaisesRegex(ValueError, "Movement range missing"):
            catalog.build_catalog(table, source(b"test"))
        with self.assertRaisesRegex(ValueError, "does not contain BlackFlow"):
            catalog.build_catalog({"details": {}}, source(b"test"))

    def test_failed_refresh_keeps_previous_file_byte_for_byte(self):
        previous = built_fixture()
        for bad in (b"not-json", b'{"details":{}}'):
            with self.subTest(bad=bad), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / "catalog.json"
                catalog.atomic_json(output, previous)
                before = output.read_bytes()
                client = Mock()
                client.latest.return_value = (bad, source(bad))
                result = catalog.synchronize(output, client)
                self.assertEqual(result["status"], "cached")
                self.assertEqual(output.read_bytes(), before)
                self.assertFalse(result["direct_fallback"])

    def test_digest_mismatch_keeps_previous(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "catalog.json"
            catalog.atomic_json(output, built_fixture())
            before = output.read_bytes()
            client = Mock()
            client.latest.return_value = (b"changed", source(b"different"))
            self.assertEqual(catalog.synchronize(output, client)["status"], "cached")
            self.assertEqual(output.read_bytes(), before)

    def test_new_version_can_add_an_item_without_name_table_edits(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "catalog.json"
            catalog.atomic_json(output, built_fixture())
            table = game_fixture()
            detail = table["details"]["rogue_6"]
            item = copy.deepcopy(detail["items"]["coin"])
            item.update(id="added_in_new_version", name="Unknown new name")
            detail["items"][item["id"]] = item
            detail["relics"][item["id"]] = copy.deepcopy(detail["relics"]["coin"])
            detail["relics"][item["id"]]["id"] = item["id"]
            raw = json.dumps(table).encode()
            client = Mock()
            client.latest.return_value = (raw, source(raw))
            result = catalog.synchronize(output, client)
            self.assertEqual(result["status"], "updated")
            published = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(
                published["items"][item["id"]]["features"], {"immediate_ingots": 10}
            )

    def test_damaged_local_cache_can_be_repaired_from_valid_source(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "catalog.json"
            output.write_text("damaged", encoding="utf-8")
            raw = json.dumps(game_fixture()).encode()
            client = Mock()
            client.latest.return_value = (raw, source(raw))
            self.assertEqual(catalog.synchronize(output, client)["status"], "updated")
            catalog.validate_catalog(json.loads(output.read_text(encoding="utf-8")))

    def test_failed_atomic_publish_keeps_previous_catalog(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "catalog.json"
            catalog.atomic_json(output, built_fixture())
            before = output.read_bytes()
            raw = json.dumps(game_fixture()).encode()
            client = Mock()
            client.latest.return_value = (raw, source(raw))
            with patch(
                "blackflow_catalog_sync.os.replace",
                side_effect=PermissionError("locked"),
            ):
                self.assertEqual(
                    catalog.synchronize(output, client)["status"], "cached"
                )
            self.assertEqual(output.read_bytes(), before)
            self.assertEqual(list(output.parent.glob("*.tmp")), [])

    def test_proxy_is_explicit_for_both_protocols_and_cannot_be_remote(self):
        with (
            patch.dict(os.environ, {"NO_PROXY": "github.com"}),
            patch("blackflow_catalog_sync.urllib.request.build_opener") as build,
        ):
            client = catalog.ClashSource()
            handler = build.call_args.args[0]
            self.assertEqual(
                handler.proxies,
                {"http": catalog.DEFAULT_PROXY, "https": catalog.DEFAULT_PROXY},
            )
            self.assertEqual(os.environ["NO_PROXY"], "")
            self.assertEqual(client.proxy, catalog.DEFAULT_PROXY)
        for invalid in (
            "http://example.com:7897",
            "https://127.0.0.1:7897",
            "http://name:secret@127.0.0.1:7897",
        ):
            with self.subTest(proxy=invalid), self.assertRaises(ValueError):
                catalog.ClashSource(invalid)

    def test_api_quota_uses_same_proxied_raw_source_and_content_version(self):
        with (
            patch.dict(os.environ),
            patch("blackflow_catalog_sync.urllib.request.build_opener"),
        ):
            client = catalog.ClashSource()
        raw = b'{"data":"versioned"}'
        quota = urllib.error.HTTPError(
            "https://api.github.com", 403, "rate limit", {}, None
        )
        with patch.object(client, "fetch", side_effect=[quota, raw]) as fetch:
            actual, origin = client.latest()
        self.assertEqual(actual, raw)
        self.assertIn("raw.githubusercontent.com", fetch.call_args_list[1].args[0])
        self.assertIsNone(origin["revision"])
        self.assertIsNone(origin["game_version"])
        self.assertEqual(origin["transport"]["proxy"], catalog.DEFAULT_PROXY)
        self.assertFalse(origin["transport"]["direct_fallback"])

    def test_real_generated_catalog_keeps_raw_evidence_for_unknowns(self):
        path = (
            Path(__file__).resolve().parents[1]
            / "resource/roguelike/BlackFlow/valuation_catalog.json"
        )
        if not path.exists():
            self.skipTest("Generate the real catalog before validating its coverage")
        built = json.loads(path.read_text(encoding="utf-8"))
        catalog.validate_catalog(built)
        counts = Counter(item["kind"] for item in built["items"].values())
        self.assertEqual(dict(counts), built["coverage"]["by_kind"])
        self.assertGreater(counts["scrap"], 0)
        self.assertGreater(counts["relic"], 0)
        for item in built["items"].values():
            if item["kind"] == "scrap":
                self.assertIn("raw_scrap", item)
            elif item["kind"] == "relic":
                self.assertIn("raw_buffs", item)


if __name__ == "__main__":
    unittest.main()
