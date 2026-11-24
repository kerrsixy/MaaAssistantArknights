import copy
import unittest

from blackflow_operator_training_catalog import base_block_count, build_catalog


class TrainingCatalogTests(unittest.TestCase):
    def setUp(self):
        self.operator = {
            "name": "test", "profession": "CASTER",
            "phases": [{"maxLevel": 50}, {"maxLevel": 80}, {"maxLevel": 90}],
            "allSkillLvlup": [{}] * 6,
            "skills": [{"skillId": "skill_a", "unlockCond": {"phase": 0, "level": 1},
                        "levelUpCostCond": [{}, {}, {}]}],
        }

    def build(self, table=None):
        return build_catalog(table or {"char_test": self.operator}, "fixture", "a" * 64)

    def test_new_operator_and_conditions_need_no_name_exception(self):
        result = self.build()
        self.assertEqual(result["operators"]["char_test"]["max_levels"], [50, 80, 90])
        self.assertEqual(result["operators"]["char_test"]["base_skill_max"], 7)
        self.assertEqual(result["operators"]["char_test"]["skills"][0]["mastery_steps"], 3)
        self.assertFalse(result["account_training_included"])

    def test_slots_preserve_holes_instead_of_shifting_indices(self):
        self.operator["skills"].insert(0, None)
        result = self.build()["operators"]["char_test"]["skills"]
        self.assertFalse(result[0]["known"])
        self.assertEqual(result[1]["index"], 2)

    def test_same_name_forms_remain_distinct(self):
        second = copy.deepcopy(self.operator)
        second["profession"] = "WARRIOR"
        result = self.build({"char_one": self.operator, "char_two": second})
        self.assertEqual(result["names"]["test"], ["char_one", "char_two"])
        self.assertNotEqual(result["operators"]["char_one"]["profession"],
                            result["operators"]["char_two"]["profession"])

    def test_phase_enum_string_and_numeric_are_supported(self):
        self.operator["skills"][0]["unlockCond"]["phase"] = "PHASE_2"
        self.assertEqual(self.build()["operators"]["char_test"]["skills"][0]["unlock_elite"], 2)

    def test_future_unknown_conditions_are_not_invented(self):
        self.operator["skills"][0]["unlockCond"]["phase"] = "NEW_DLC_PHASE"
        result = self.build()
        self.assertFalse(result["operators"]["char_test"]["skills"][0]["known"])
        self.assertEqual(result["issues"][0]["reason"], "unknown_unlock_condition")

    def test_missing_level_cap_and_training_data_are_unknown(self):
        self.operator["phases"][0]["maxLevel"] = True
        del self.operator["allSkillLvlup"]
        result = self.build()["operators"]["char_test"]
        self.assertIsNone(result["max_levels"][0])
        self.assertIsNone(result["base_skill_max"])

    def test_blocking_is_derived_for_each_phase(self):
        for phase, count in zip(self.operator["phases"], [1, 2, 3]):
            phase["attributesKeyFrames"] = [
                {"level": 1, "data": {"blockCnt": count}},
                {"level": phase["maxLevel"], "data": {"blockCnt": count}},
            ]
        result = self.build()["operators"]["char_test"]
        self.assertEqual(result["base_block_counts"], [1, 2, 3])

    def test_unreadable_blocking_is_unknown_without_a_role_default(self):
        self.assertEqual(self.build()["operators"]["char_test"]["base_block_counts"], [None, None, None])

    def test_blocking_requires_integral_nonnegative_complete_keyframes(self):
        good = {"maxLevel": 50, "attributesKeyFrames": [
            {"level": 1, "data": {"blockCnt": 3}}, {"level": 50, "data": {"blockCnt": 3}}]}
        self.assertEqual(base_block_count(good), 3)
        for value in [-1, True, 3.5, None]:
            bad = copy.deepcopy(good)
            bad["attributesKeyFrames"][0]["data"]["blockCnt"] = value
            self.assertIsNone(base_block_count(bad))
        for levels in [[1, 49], [2, 50], [50, 1], [1, 1]]:
            bad = copy.deepcopy(good)
            for frame, level in zip(bad["attributesKeyFrames"], levels):
                frame["level"] = level
            self.assertIsNone(base_block_count(bad))

    def test_varying_blocking_is_not_silently_interpolated(self):
        phase = {"maxLevel": 50, "attributesKeyFrames": [
            {"level": 1, "data": {"blockCnt": 2}}, {"level": 50, "data": {"blockCnt": 3}}]}
        self.assertIsNone(base_block_count(phase))

    def test_zero_blocking_is_known_and_different_from_missing(self):
        phase = {"maxLevel": 1, "attributesKeyFrames": [{"level": 1, "data": {"blockCnt": 0}}]}
        self.assertEqual(base_block_count(phase), 0)


if __name__ == "__main__":
    unittest.main()
