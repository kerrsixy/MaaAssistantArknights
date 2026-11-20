import json
import unittest

from spa_preparation_audit import audit_lines


def event(what, details=None, timestamp=None):
    row = {"message": 20003, "details": {"what": what, "details": details or {}}}
    if timestamp:
        row["timestamp"] = timestamp
    return json.dumps(row)


def native(second, kind, body, process=1):
    return (f"[2026-11-18 12:00:{second:02d}.000][INF][Px{process}][Tx2] "
            f"Assistant::append_callback | {kind} {json.dumps(body)}")


class PreparationAuditTests(unittest.TestCase):
    def test_missing_fields_and_malformed_records_remain_unknown(self):
        report = audit_lines(["{", "[]", json.dumps({"details": None}), event("SPAEquipmentInventoryInspected")])
        self.assertEqual(report["malformed_lines"], 3)
        self.assertEqual(report["events_with_timestamp"], 0)
        self.assertIsNone(report["by_run_round"][0]["round"])
        self.assertIsNone(report["by_run_round"][0]["observed_span_seconds"])
        self.assertEqual(report["candidate_issues"], [])
        self.assertEqual(report["confirmed_invariant_failures"], [])

    def test_inventory_receipt_does_not_double_count_actual_pass(self):
        report = audit_lines([
            event("SPAEquipmentInventoryInspected", {"round": 3, "fresh_title_probes": 2}),
            event("SPAAutomationEquipmentInventoryObserved", {"round": 3, "run_id": "run-A"}),
        ])
        counts = report["by_run_round"][0]["counts"]
        self.assertEqual(counts["equipment_discovery_passes"], 1)
        self.assertEqual(counts["equipment_inventory_receipts"], 1)
        self.assertEqual(counts["inventory_fresh_title_probes"], 2)
        self.assertEqual(report["by_run_round"][0]["run_id"], "run-A")

    def test_repeat_is_candidate_not_failure_and_mutation_is_reported(self):
        scan = event("SPAEquipmentInventoryInspected", {"round": 3})
        report = audit_lines([scan, scan, event("SPAAutomationEquipmentReconciled", {"round": 3}), scan])
        self.assertEqual(len(report["candidate_issues"]), 2)
        self.assertFalse(report["candidate_issues"][0]["confirmed_invariant_failure"])
        self.assertFalse(report["candidate_issues"][0]["intervening_mutation_logged"])
        self.assertTrue(report["candidate_issues"][1]["intervening_mutation_logged"])
        self.assertEqual(report["confirmed_invariant_failures"], [])

    def test_round_and_run_boundaries_do_not_share_discovery_state(self):
        report = audit_lines([
            event("SPAEquipmentInventoryInspected", {"run_id": "A", "round": 1}),
            event("SPAEquipmentInventoryInspected", {"run_id": "A", "round": 2}),
            event("SPAEquipmentInventoryInspected", {"run_id": "B", "round": 1}),
        ])
        self.assertEqual(len(report["by_run_round"]), 3)
        self.assertEqual(report["candidate_issues"], [])
        self.assertGreater(report["by_run_round"][0]["shopping_order_evidence_gaps"]["equipment_capacity"], 0)

    def test_unknown_observation_round_preserves_known_round(self):
        report = audit_lines([
            event("SPAObservation", {"round": 5, "round_known": True}),
            event("SPAObservation", {"round": 0, "round_known": False}),
            event("SPAHeldOperatorIdentity", {"verified": True}),
        ])
        self.assertEqual(len(report["by_run_round"]), 1)
        self.assertEqual(report["by_run_round"][0]["round"], 5)

    def test_cached_grid_is_not_fresh_identity_scan(self):
        report = audit_lines([event("SPASpatialGridSnapshotRefreshed", {
            "round": 4, "fresh_identity_probes": False, "complete": True})] * 3)
        self.assertEqual(report["candidate_issues"], [])
        self.assertEqual(report["by_run_round"][0]["counts"]["grid_receipts_without_fresh_identity_probes"], 3)

    def test_shopping_order_requires_all_exception_evidence(self):
        base = {"round": 4, "funds": 4, "shopping_complete": False,
                "bench_capacity_full": False, "equipment_capacity_full": False,
                "preparation_seconds_remaining_known": True, "preparation_seconds_remaining": 60}
        work = [event("SPAAutomationDeploymentDecision", {"round": 4}),
                event("SPAFieldGridBootstrapObserved", {"round": 4})]
        report = audit_lines([event("SPAObservation", {**base, "round_known": True})] + work)
        self.assertEqual(report["candidate_issues"][0]["kind"], "frequent_work_before_shopping_handoff")
        self.assertEqual(report["confirmed_invariant_failures"], [])
        for missing in ("equipment_capacity_full", "bench_capacity_full", "preparation_seconds_remaining_known"):
            incomplete = dict(base)
            del incomplete[missing]
            report = audit_lines([event("SPAObservation", {**incomplete, "round_known": True})] + work)
            self.assertEqual(report["candidate_issues"], [], missing)
        for exception in ({"bench_capacity_full": True}, {"funds": 0},
                          {"preparation_seconds_remaining": 5}, {"shopping_complete": True}):
            report = audit_lines([event("SPAObservation", {**base, **exception, "round_known": True})] + work)
            self.assertEqual(report["candidate_issues"], [], exception)

    def test_capacity_and_shop_offer_evidence_is_invalidated_by_purchase(self):
        report = audit_lines([
            event("SPAAutomationShopScan", {"round": 2, "funds": 5, "bench_capacity_full": False,
                  "equipment_capacity_full": False, "deadline_emergency": False,
                  "offers": [{"price": 2, "catalog_match": True}]}),
            event("SPAShopPurchase", {"round": 2, "funds_after": 3}),
            event("SPAAutomationDeploymentDecision", {"round": 2}),
            event("SPAFieldGridBootstrapObserved", {"round": 2}),
        ])
        self.assertEqual(report["candidate_issues"], [])

    def test_stale_shopping_context_is_not_an_invariant(self):
        report = audit_lines([
            event("SPAObservation", {"round": 2, "round_known": True, "funds": 5,
                  "bench_capacity_full": False, "equipment_capacity_full": False,
                  "deadline_emergency": False, "shopping_complete": False}),
            event("ignored"), event("ignored"),
            event("SPAAutomationDeploymentDecision", {"round": 2}),
            event("SPAFieldGridBootstrapObserved", {"round": 2}),
        ], freshness_events=1)
        self.assertEqual(report["candidate_issues"], [])

    def test_explicit_unknown_funds_invalidates_old_positive_observation(self):
        report = audit_lines([
            event("SPAObservation", {"round": 2, "round_known": True, "funds": 5,
                  "bench_capacity_full": False, "equipment_capacity_full": False,
                  "deadline_emergency": False, "shopping_complete": False}),
            event("SPAObservation", {"round": 2, "round_known": True, "funds": -1, "funds_known": False}),
            event("SPAAutomationDeploymentDecision", {"round": 2}),
            event("SPAFieldGridBootstrapObserved", {"round": 2}),
        ])
        self.assertEqual(report["candidate_issues"], [])
        self.assertEqual(report["by_run_round"][0]["shopping_order_evidence_gaps"]["funds"], 2)

    def test_explicit_deadline_guard_violation_is_separate(self):
        report = audit_lines([
            event("SPAJointPreparationDeadline", {"round": 2, "seconds_remaining": 5, "new_transaction_issued": True}),
            event("SPAJointPreparationDeadline", {"round": 2, "seconds_remaining": 5, "new_transaction_issued": False}),
            event("SPAJointPreparationDeadline", {"round": 2, "seconds_remaining": 5}),
        ])
        self.assertEqual(len(report["confirmed_invariant_failures"]), 1)
        self.assertEqual(report["candidate_issues"], [])

    def test_shop_receipts_do_not_imply_unlogged_toggle_inputs(self):
        report = audit_lines([
            event("SPAShopExpandedForDeployment", {"expanded_before_source_scan": False}),
            event("SPAShopExpandedForDeployment", {"expanded_before_source_scan": True}),
            event("SPADeployedPoseObserved", {"coordinate_frame": "collapsed"}),
        ])
        counts = report["by_run_round"][0]["counts"]
        self.assertEqual(counts["shop_deployment_visibility_receipts"], 2)
        self.assertEqual(counts["shop_expansion_inputs_reported"], 1)
        self.assertEqual(counts["shop_collapse_inputs_reported"], 0)

    def test_native_plugin_duration_reuses_existing_pairing(self):
        plugin = {"class": "asst::spa::ShopTaskPlugin", "taskid": 1}
        report = audit_lines([
            native(1, "SubTaskExtraInfo", {"what": "SPAObservation", "details": {"round": 2, "round_known": True}}),
            native(2, "SubTaskStart", plugin), native(5, "SubTaskCompleted", plugin),
            native(6, "SubTaskStart", plugin),
        ], native=True)
        timing = report["by_run_round"][0]["plugin_wall_time"]
        self.assertEqual(timing["plugins"][plugin["class"]]["total_seconds"], 3)
        self.assertEqual(timing["unfinished_calls"], 1)
        self.assertEqual(report["events_with_timestamp"], 4)

    def test_explicit_duration_missing_is_not_zero_and_processes_are_separate(self):
        report = audit_lines([
            event("SPAEquipmentInventoryInspected", {"round": 2, "duration_ms": 200}),
            event("SPAEquipmentInventoryInspected", {"round": 3}),
        ])
        self.assertEqual(report["by_run_round"][0]["explicit_duration_ms"]["SPAEquipmentInventoryInspected.duration_ms"]["total"], 200)
        self.assertEqual(report["by_run_round"][1]["explicit_duration_ms"], {})
        plugin = {"class": "asst::spa::ShopTaskPlugin", "taskid": 1}
        report = audit_lines([native(2, "SubTaskStart", plugin, 1), native(5, "SubTaskCompleted", plugin, 2)], native=True)
        self.assertEqual(len(report["by_run_round"]), 2)
        self.assertTrue(all(not row["plugin_wall_time"]["plugins"] for row in report["by_run_round"]))


if __name__ == "__main__":
    unittest.main()
