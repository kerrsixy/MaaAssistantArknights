import unittest

from blackflow_plan_group_audit import audit


class PlanGroupAuditTests(unittest.TestCase):
    def check_groups(self, groups, **extra):
        catalog = {"priority": [{"name": "奶", "opers": [{"name": "波登可"}]}]}
        plan = {
            "stage_name": "fixture",
            "deploy_plan": [{"groups": groups, "location": [1, 2], **extra}],
        }
        return audit(catalog, [("fixture.json", plan)])

    def test_operator_name_filter_cannot_rescue_unknown_groups(self):
        result = self.check_groups(["波登可", "纯奶"], operator_names=["波登可"])
        self.assertEqual(len(result["dead_entries"]), 1)
        self.assertEqual(result["dead_entries"][0]["plan_index"], 0)

    def test_mixed_alias_is_reported_without_rejecting_working_group(self):
        result = self.check_groups(["医疗", "奶"])
        self.assertFalse(result["dead_entries"])
        self.assertEqual(result["mixed_alias_entries"][0]["resolved_groups"], ["奶"])

    def test_runtime_worker_needs_no_recruitment_group(self):
        result = self.check_groups(["工人"])
        self.assertFalse(result["dead_entries"])
        self.assertFalse(result["mixed_alias_entries"])

    def test_empty_group_list_has_no_candidate_path(self):
        self.assertEqual(len(self.check_groups([])["dead_entries"]), 1)

    def test_exclusions_do_not_change_declaration_scope(self):
        result = self.check_groups(["奶"], excluded_operator_names=["波登可"])
        self.assertFalse(result["dead_entries"])
        self.assertIn("no roster", result["scope"])


if __name__ == "__main__":
    unittest.main()
