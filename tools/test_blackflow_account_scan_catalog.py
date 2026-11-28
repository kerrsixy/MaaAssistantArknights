"""Account scan identity mapping must fail on ownership and upgrade mismatches."""

import copy
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

import blackflow_account_scan_catalog as catalog


def fixture():
    return [
        {
            "theme": "BlackFlow",
            "priority": [
                {
                    "opers": [
                        {
                            "name": "Operator A",
                            "account_module": {
                                "module_id": "module_A",
                                "minimum_level": 3,
                            },
                        }
                    ]
                }
            ],
        },
        {"char_A": {"name": "Operator A"}},
        {
            "equipDict": {
                "module_A": {
                    "uniEquipId": "module_A",
                    "uniEquipName": "Exact badge A",
                    "charId": "char_A",
                }
            },
            "charEquip": {"char_A": ["module_A"]},
        },
        {"module_A": {"phases": [{"equipLevel": i} for i in [1, 2, 3]]}},
    ]


class AccountScanCatalogTest(unittest.TestCase):
    def build(self, data):
        return catalog.build_catalog(*data, "explicit snapshot label")

    def test_exact_owner_title_and_original_maximum(self):
        result = self.build(fixture())
        self.assertEqual(
            result["targets"],
            [
                {
                    "operator_id": "char_A",
                    "operator_name": "Operator A",
                    "module_id": "module_A",
                    "display_title": "Exact badge A",
                    "max_level": 3,
                    "minimum_level": 3,
                }
            ],
        )
        self.assertFalse(result["account_scan_performed"])
        self.assertFalse(result["current_battle_equipment_proven"])
        self.assertTrue(result["label_is_not_game_version_proof"])

    def test_same_policy_in_multiple_groups_is_one_scan(self):
        data = fixture()
        data[0]["priority"].append(copy.deepcopy(data[0]["priority"][0]))
        self.assertEqual(len(self.build(data)["targets"]), 1)

    def test_cross_owner_and_conflicting_policy_are_rejected(self):
        for change in [
            "owner",
            "name",
            "owner_list",
            "same_module_other_operator",
            "different_module_same_operator",
        ]:
            with self.subTest(change=change):
                data = fixture()
                oper = data[0]["priority"][0]["opers"][0]
                if change == "owner":
                    data[2]["equipDict"]["module_A"]["charId"] = "char_B"
                elif change == "name":
                    data[1]["char_A"]["name"] = "Another operator"
                elif change == "owner_list":
                    data[2]["charEquip"]["char_A"] = []
                else:
                    other = copy.deepcopy(oper)
                    if change == "same_module_other_operator":
                        other["name"] = "Another operator"
                    else:
                        other["account_module"]["module_id"] = "module_B"
                    data[0]["priority"][0]["opers"].append(other)
                with self.assertRaises(ValueError):
                    self.build(data)

    def test_missing_and_mismatched_modules_are_rejected(self):
        for replacement in [None, {}, {"uniEquipId": "different_ID"}]:
            data = fixture()
            data[2]["equipDict"]["module_A"] = replacement
            with self.subTest(replacement=replacement), self.assertRaises(ValueError):
                self.build(data)

    def test_empty_or_whitespace_titles_are_rejected(self):
        for title in [None, "", " ", " Exact badge A", "Exact badge A ", 5]:
            data = fixture()
            data[2]["equipDict"]["module_A"]["uniEquipName"] = title
            with self.subTest(title=title), self.assertRaises(ValueError):
                self.build(data)

    def test_phase_gaps_duplicates_and_future_levels_require_review(self):
        for levels in [
            [],
            [1, 3],
            [1, 2, 2, 3],
            [0, 1, 2],
            [1, 2, 3, 4],
            [True, 2, 3],
            [1.0, 2, 3],
            ["1", 2, 3],
        ]:
            data = fixture()
            data[3]["module_A"]["phases"] = [{"equipLevel": level} for level in levels]
            with self.subTest(levels=levels), self.assertRaises(ValueError):
                self.build(data)

    def test_minimum_level_is_checked_against_original_phases(self):
        for minimum in [0, 4, True, "3", 3.0]:
            data = fixture()
            data[0]["priority"][0]["opers"][0]["account_module"]["minimum_level"] = (
                minimum
            )
            with self.subTest(minimum=minimum), self.assertRaises(ValueError):
                self.build(data)
        data = fixture()
        data[3]["module_A"]["phases"] = [{"equipLevel": 1}, {"equipLevel": 2}]
        with self.assertRaises(ValueError):
            self.build(data)
        data[0]["priority"][0]["opers"][0]["account_module"]["minimum_level"] = 2
        self.assertEqual(self.build(data)["targets"][0]["max_level"], 2)

    def test_ungated_operators_do_not_create_scan_targets(self):
        data = fixture()
        data[0]["priority"][0]["opers"].append({"name": "Unrelated operator"})
        self.assertEqual(len(self.build(data)["targets"]), 1)

    def test_bad_policy_and_wrong_theme_fail_instead_of_partial_output(self):
        for policy in [None, False, [], "module_A"]:
            data = fixture()
            data[0]["priority"][0]["opers"][0]["account_module"] = policy
            with self.subTest(policy=policy), self.assertRaises(ValueError):
                self.build(data)
        data = fixture()
        data[0]["theme"] = "Another theme"
        with self.assertRaises(ValueError):
            self.build(data)

    def test_source_paths_and_hashes_are_explicit_not_date_guessed(self):
        with tempfile.TemporaryDirectory() as temp:
            paths = [
                Path(temp) / name
                for name in [
                    "rules.json",
                    "new-version-characters.json",
                    "equipment.json",
                    "battle.json",
                ]
            ]
            for path, value in zip(paths, fixture(), strict=True):
                path.write_text(json.dumps(value), encoding="utf-8")
            result = catalog.generate(*paths, "label only")
            for key, path in zip(
                ["recruitment", "characters", "equipment", "battle_equipment"],
                paths,
                strict=True,
            ):
                self.assertEqual(
                    result["source_files"][key]["sha256"],
                    hashlib.sha256(path.read_bytes()).hexdigest(),
                )
            self.assertEqual(
                result["source_files"]["characters"]["name"],
                "new-version-characters.json",
            )


if __name__ == "__main__":
    unittest.main()
