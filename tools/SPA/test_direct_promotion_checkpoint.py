import unittest

from spa_checkpoint import merge_receipt


class DirectPromotionCheckpointTests(unittest.TestCase):
    def test_reserved_initial_body_survives_other_transactions_and_new_round(self):
        pending = {
            "operator": "魔王",
            "equipment": "博士投影",
            "contact_round": 4,
            "effect_timing": "next_round_start",
            "recipient_initial_form_observed": True,
            "equipment_replay_allowed": False,
        }
        checkpoint = merge_receipt(
            {"run_id": "one"},
            {"run_id": "one", "direct_promotion_pending": pending},
        )
        checkpoint = merge_receipt(
            checkpoint,
            {"run_id": "one", "round": 5, "purchased": "other", "funds": 12},
        )
        self.assertEqual(checkpoint["direct_promotion_pending"], pending)

    def test_unknown_contact_is_not_cleared_by_missing_inventory_source(self):
        checkpoint = merge_receipt(
            {
                "run_id": "one",
                "direct_promotion_pending": {"effect_timing": "unknown"},
            },
            {"run_id": "one", "equipment_candidates": []},
        )
        self.assertIsNotNone(checkpoint["direct_promotion_pending"])

    def test_only_explicit_reconciliation_clears_same_run_pending(self):
        original = {"run_id": "one", "direct_promotion_pending": {"unknown": True}}
        reconciled = merge_receipt(
            original, {"run_id": "one", "direct_promotion_pending": None}
        )
        self.assertIsNone(reconciled["direct_promotion_pending"])
        self.assertNotIn(
            "direct_promotion_pending", merge_receipt(original, {"run_id": "two"})
        )


if __name__ == "__main__":
    unittest.main()
