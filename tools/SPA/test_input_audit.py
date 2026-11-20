import json
import unittest

from spa_input_audit import audit_lines, local_failure_context, receiver_failure_context


def callback(what, details):
    return "Assistant::append_callback | SubTaskExtraInfo " + json.dumps({"what": what, "details": details})


class InputAuditTests(unittest.TestCase):
    def test_counts_one_layer_and_preserves_round_across_unknown_frames(self):
        result = audit_lines([
            callback("SPAObservation", {"round": 7, "round_known": True}),
            "Click with scaled coordinates (4, 5) 1", "minitouch click: (4, 5)",
            callback("SPAObservation", {"round": 0, "round_known": False}),
            "Swipe with scaled coordinates (4, 5) (4, 8) 1", "minitouch swipe (4, 5) (4, 8)",
        ])
        self.assertEqual(result["by_round"]["7"], {"click_requests": 1, "swipe_requests": 1})

    def test_journal_phases_are_one_attempt_and_failure_survives_round_change(self):
        pending = {"name": "灰喉", "attempt_fingerprint": "map:actor:formation", "relocation": True}
        lines = []
        for round_number in (1, 2):
            lines += [callback("SPASpatialOrientationPending", {"phase": "before_held_input", "pending_orientation": pending}),
                      callback("SPASpatialOrientationPending", {"phase": "before_field_release", "pending_orientation": pending}),
                      callback("SPASpatialOrientationReconciled", {"round": round_number, "relocation_applied": False})]
        result = audit_lines(lines)
        self.assertEqual(len(result["spatial_attempts"]), 2)
        self.assertEqual(result["repeated_previously_failed_fingerprints"], {"map:actor:formation": 1})

    def test_unknown_direction_does_not_include_known_facing_or_real_move(self):
        result = audit_lines([
            callback("SPADynamicLayoutStep", {"previous": "\0", "direction": "E", "relocation": False}),
            callback("SPADynamicLayoutStep", {"previous": "E", "direction": "E", "relocation": False}),
            callback("SPADynamicLayoutStep", {"previous": "\0", "direction": "E", "relocation": True}),
        ])
        self.assertEqual(result["unknown_default_east_face_plans"], 1)

    def test_an_explicit_egir_chain_is_not_a_generic_default_turn(self):
        result = audit_lines([
            callback("SPAEgirFormationPlanned", {"found": True}),
            callback("SPADynamicLayoutStep", {"previous": "\0", "direction": "E", "relocation": False}),
        ])
        self.assertEqual(result["unknown_default_east_face_plans"], 0)
        self.assertEqual(result["explicit_egir_unknown_east_face_plans"], 1)

    def test_distant_recruits_do_not_hide_a_repeated_local_failure(self):
        first = "m12#normal:陨星:4.1>3.2:E:|杰克:3.3:initial|陨星:4.1:initial"
        second = first + "|卡达:4.4:initial|野鬃:3.4:initial"
        self.assertEqual(local_failure_context(first), local_failure_context(second))
        self.assertNotEqual(local_failure_context(first), local_failure_context(second.replace("杰克:3.3", "杰克:4.3")))
        lines = []
        for key in (first, second):
            lines += [callback("SPASpatialOrientationPending", {"phase": "before_held_input",
                      "pending_orientation": {"attempt_fingerprint": key, "relocation": True}}),
                      callback("SPASpatialOrientationReconciled", {"relocation_applied": False})]
        result = audit_lines(lines)
        self.assertEqual(result["repeated_previously_failed_fingerprints"], {})
        self.assertEqual(result["repeated_local_failure_contexts"], {local_failure_context(first): 1})

    def test_unrecognized_legacy_keys_remain_unknown(self):
        self.assertIsNone(local_failure_context("map:actor:formation"))
        self.assertIsNone(local_failure_context("m12:陨星:4.1>3.2:E:|bad-member"))
        key = "spatial-local-v2:m12#normal:0:actor"
        self.assertEqual(local_failure_context(key), key)

    def test_duplicate_bench_hitboxes_exclude_distinct_copies_and_transactions(self):
        result = audit_lines([
            "[INF] SPA bench identity 陨星 761 450",
            "[INF] SPA bench identity 陨星 842 450",
            "[INF] SPA bench identity 陨星 869 450",
            "[INF] SPA held drag begin 842 480 to 600 300 true",
            "[INF] SPA bench identity 陨星 842 450",
            callback("SPAShopPurchase", {}),
            "[INF] SPA bench identity 陨星 869 450",
        ])
        duplicates = result["nearby_repeated_bench_identity_probes"]
        self.assertEqual(len(duplicates), 1)
        self.assertEqual(duplicates[0]["previous"]["x"], 842)
        self.assertEqual(duplicates[0]["repeated"]["x"], 869)

    def test_marginal_layout_deferral_is_recorded_as_no_input(self):
        details = {"round": 2, "spatial_preference_improvement": 100, "preference_input_cost": 250,
                   "input_issued": False}
        result = audit_lines([callback("SPADynamicLayoutMarginalGainDeferred", details)])
        self.assertEqual(result["marginal_layouts_deferred_without_input"], [details])
        self.assertEqual(result["by_round"]["0"]["click_requests"], 0)

    def test_different_facings_do_not_hide_an_unchanged_failed_receiver(self):
        lines = []
        for direction in "ESN":
            pending = {"name": "斯卡蒂", "relocation": False,
                       "attempt_fingerprint": f"spatial-local-v2:m11#normal:2:斯卡蒂:3.4>3.4:{direction}:|斯卡蒂:3.4:elite:unknown"}
            lines += [callback("SPASpatialOrientationPending", {"phase": "before_held_input", "pending_orientation": pending}),
                      callback("SPASpatialDirectionObserved", {"same_cell": False, "identity_verified": False}),
                      callback("SPASpatialOrientationReconciled", {"direction_input_committed": False})]
        result = audit_lines(lines)
        self.assertEqual(result["repeated_previously_failed_fingerprints"], {})
        self.assertEqual(len(result["repeated_pre_direction_receiver_failures"]), 2)
        self.assertIsNone(receiver_failure_context({"attempt_fingerprint": "legacy-or-unknown"}))

    def test_a_failed_direction_input_does_not_imply_a_failed_receiver(self):
        pending = {"name": "斯卡蒂", "relocation": False,
                   "attempt_fingerprint": "spatial-local-v2:m11#normal:2:斯卡蒂:3.4>3.4:E:|斯卡蒂:3.4:elite:unknown"}
        lines = []
        for _ in range(2):
            lines += [callback("SPASpatialOrientationPending", {"phase": "before_held_input", "pending_orientation": pending}),
                      callback("SPASpatialDirectionObserved", {"same_cell": True, "identity_verified": True}),
                      callback("SPASpatialOrientationReconciled", {"direction_input_committed": False})]
        self.assertEqual(audit_lines(lines)["repeated_pre_direction_receiver_failures"], [])


if __name__ == "__main__":
    unittest.main()
