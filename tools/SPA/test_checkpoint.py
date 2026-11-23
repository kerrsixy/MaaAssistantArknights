import unittest

from spa_checkpoint import (
    committed_shop_level,
    merge_receipt,
    setup_context_receipt,
    setup_pact_receipt,
    verified_boss_context,
)


class SetupPactEvidenceTests(unittest.TestCase):
    def test_only_verified_boss_advertisement_survives_setup_handoff(self):
        boss = {
            "boss_family_name": "阿利斯泰尔，帝国余晖",
            "boss_advertisement_verified": True,
        }
        self.assertEqual(
            verified_boss_context({**boss, "boss_variant_verified": False}), boss
        )
        self.assertEqual(
            verified_boss_context({"boss_family_name": boss["boss_family_name"]}), {}
        )
        self.assertEqual(
            verified_boss_context({**boss, "boss_advertisement_verified": 1}), {}
        )
        self.assertEqual(verified_boss_context({**boss, "boss_family_name": " "}), {})

    def test_only_reconciled_upgrade_with_valid_level_updates_book(self):
        signed = {"shop_level_before": 3, "shop_level_after": 4}
        self.assertEqual(committed_shop_level("SPAShopUpgradeReconciled", signed), 4)
        self.assertIsNone(committed_shop_level("SPAShopUpgrade", signed))
        self.assertIsNone(
            committed_shop_level("SPAShopUpgradeReconciled", {**signed, "shop_level_after": 6})
        )
        fixed = {"schedule": "phase1_fixed", "shop_level_after": 2}
        self.assertEqual(committed_shop_level("SPAShopUpgradeReconciled", fixed), 2)

    def test_occupied_strategy_requires_a_verified_label(self):
        details = {"occupied_strategy_ids": ["band_mech", "band_mech"]}
        self.assertEqual(
            setup_context_receipt("SPAStrategyOccupiedObserved", details), {}
        )
        details["occupied_label_verified"] = True
        self.assertEqual(
            setup_context_receipt("SPAStrategyOccupiedObserved", details),
            {"occupied_strategy_ids": ["band_mech"]},
        )
        details["occupied_strategy_ids"] = [None]
        self.assertEqual(
            setup_context_receipt("SPAStrategyOccupiedObserved", details), {}
        )

    def test_occupied_cards_survive_same_run_retry_but_not_a_new_game(self):
        previous = {"run_id": "first", "occupied_strategy_ids": ["band_mech"]}
        receipt = {"run_id": "first", "occupied_strategy_ids": ["band_jesica"]}
        self.assertEqual(
            merge_receipt(previous, receipt)["occupied_strategy_ids"],
            ["band_mech", "band_jesica"],
        )
        receipt["run_id"] = "second"
        self.assertEqual(
            merge_receipt(previous, receipt)["occupied_strategy_ids"], ["band_jesica"]
        )

    def test_countdown_advance_does_not_claim_the_requested_pact(self):
        receipt = setup_pact_receipt(
            {
                "core_pact": "阿戈尔",
                "core_selection_mode": "explicit",
                "selection_verified": False,
                "advanced_before_task": True,
                "provisional_core": False,
                "destination_verified": True,
                "stage": "2/3",
            }
        )
        self.assertEqual(receipt["requested_core_pact"], "阿戈尔")
        self.assertEqual(receipt["core_pact"], "")
        self.assertFalse(receipt["core_selection_verified"])
        self.assertTrue(receipt["provisional_core"])

    def test_a_verified_choice_retains_its_evidence(self):
        receipt = setup_pact_receipt(
            {
                "core_pact": "阿戈尔",
                "selection_verified": True,
                "provisional_core": False,
                "stage": "2/3",
            }
        )
        self.assertEqual(receipt["core_pact"], "阿戈尔")
        self.assertTrue(receipt["core_selection_verified"])
        self.assertFalse(receipt["provisional_core"])

    def test_new_native_unverified_receipt_keeps_only_the_request(self):
        receipt = setup_pact_receipt({"core_pact": "", "requested_core_pact": "阿戈尔"})
        self.assertEqual(receipt["requested_core_pact"], "阿戈尔")
        self.assertEqual(receipt["core_pact"], "")
        self.assertTrue(receipt["provisional_core"])

    def test_preselection_gray_groups_are_not_final_bans(self):
        receipt = setup_pact_receipt(
            {
                "core_pact": "阿戈尔",
                "selection_verified": True,
                "disabled_pacts": ["阿戈尔", "灵巧"],
            }
        )
        self.assertFalse(receipt["pact_availability_verified"])
        self.assertEqual(receipt["disabled_pacts"], [])

    def test_final_selected_group_keeps_only_its_verified_bans(self):
        receipt = setup_pact_receipt(
            {
                "core_pact": "阿戈尔",
                "selection_verified": True,
                "availability_after_selection_verified": True,
                "disabled_pacts": ["乌萨斯", "灵巧"],
            }
        )
        self.assertTrue(receipt["pact_availability_verified"])
        self.assertEqual(receipt["disabled_pacts"], ["乌萨斯", "灵巧"])


if __name__ == "__main__":
    unittest.main()
