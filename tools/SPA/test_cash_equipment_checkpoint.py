import unittest
from pathlib import Path

from spa_checkpoint import merge_receipt


class CashEquipmentCheckpointTests(unittest.TestCase):
    def test_unknown_contact_survives_unrelated_receipts_and_restart(self):
        pending = {
            "equipment": "盟约之币",
            "source_round": 8,
            "contact_status_known": False,
        }
        checkpoint = merge_receipt(
            {"run_id": "run-a", "roster": []},
            {
                "run_id": "run-a",
                "cash_equipment_pending": pending,
                "cash_equipment_attempted_round": 8,
            },
        )
        checkpoint = merge_receipt(checkpoint, {"run_id": "run-a", "round": 9})
        self.assertEqual(checkpoint["cash_equipment_pending"], pending)
        self.assertEqual(checkpoint["cash_equipment_attempted_round"], 8)

    def test_only_explicit_reconciliation_clears_same_run_pending(self):
        previous = {"run_id": "run-a", "cash_equipment_pending": {"unknown": True}}
        resolved = merge_receipt(
            previous, {"run_id": "run-a", "cash_equipment_pending": None}
        )
        self.assertIsNone(resolved["cash_equipment_pending"])
        self.assertNotIn(
            "cash_equipment_pending", merge_receipt(previous, {"run_id": "run-b"})
        )

    def test_closed_attribution_survives_later_sale_receipt(self):
        pending = {
            "observation_checked": True,
            "wallet_observations": 2,
            "attribution_closed_reason": "restored_contact_unknown",
        }
        checkpoint = merge_receipt(
            {"run_id": "run-a", "cash_equipment_pending": pending},
            {"run_id": "run-a", "funds": 2, "round": 8, "sold": "irrelevant"},
        )
        self.assertEqual(checkpoint["cash_equipment_pending"], pending)

    def test_native_restore_closes_previous_contact_attribution(self):
        root = Path(__file__).resolve().parents[2]
        source = (
            root / "src/MaaCore/Task/Interface/StrongholdProtocolTask.cpp"
        ).read_text(encoding="utf-8")
        setter = source.split("bool asst::StrongholdProtocolTask::set_params", 1)[1]
        self.assertIn(
            "m_pending_cash_equipment = spa::restored_cash_pending(*pending);", setter
        )
        self.assertNotIn("m_pending_cash_equipment = *pending;", setter)

    def test_pending_reconciliation_precedes_every_preparation_transaction(self):
        root = Path(__file__).resolve().parents[2]
        source = (
            root / "src/MaaCore/Task/Interface/StrongholdProtocolTask.cpp"
        ).read_text(encoding="utf-8")
        session = source.split("bool asst::StrongholdProtocolTask::run_session(", 1)[1]
        reconcile = session.index("reconcile_pending_cash_equipment(state)")
        for action in (
            "if (stale_purchase_confirm)",
            "handle_promotion_page()",
            "handle_full_field_replacement(state)",
            "handle_full_bench_sale(state)",
            "handle_cash_equipment(state)",
            "handle_delayed_cash_equipment(state)",
            "handle_pact_recruitment_equipment(state)",
            "handle_persistent_equipment(state)",
        ):
            self.assertLess(reconcile, session.index(action), action)
        self.assertEqual(session.count("reconcile_pending_cash_equipment(state)"), 1)
        equipment = (
            root / "src/MaaCore/Task/Interface/StrongholdProtocolEquipment.cpp"
        ).read_text(encoding="utf-8")
        new_use = equipment.split(
            "bool asst::StrongholdProtocolTask::handle_cash_equipment", 1
        )[1]
        self.assertLess(
            new_use.index("if (m_pending_cash_equipment &&"),
            new_use.index("spa::SwapTaskPlugin use"),
        )
        self.assertNotIn("pending_cash_observation", new_use)

    def test_new_physical_coin_keeps_previous_unknown_contact_in_receipt(self):
        old = {
            "source_round": 8,
            "observation_checked": True,
            "attribution_closed_reason": "restored_contact_unknown",
        }
        new = {"source_round": 9, "contact_status_known": False}
        checkpoint = merge_receipt(
            {"run_id": "run-a", "cash_equipment_pending": old},
            {
                "run_id": "run-a",
                "cash_equipment_closed_contact": old,
                "closed_contact_effect_verified": False,
                "cash_equipment_pending": new,
            },
        )
        self.assertEqual(checkpoint["cash_equipment_closed_contact"], old)
        self.assertFalse(checkpoint["closed_contact_effect_verified"])
        self.assertEqual(checkpoint["cash_equipment_pending"], new)

    def test_failed_cancellation_still_records_possible_equipment_release(self):
        root = Path(__file__).resolve().parents[2]
        source = (
            root / "src/MaaCore/Task/MiniGame/SPA/SPASwapTaskPlugin.cpp"
        ).read_text(encoding="utf-8")
        release = source.split("// Always release the contact", 1)[1].split(
            'LogInfo << "SPA held drag end"', 1
        )[0]
        self.assertIn("if (m_equip_only)", release)
        self.assertNotIn("if (ok && m_equip_only)", release)
        self.assertLess(
            release.index("m_equipment_contact_attempted = true"),
            release.index("send(InputEvent::Type::TOUCH_UP)"),
        )


if __name__ == "__main__":
    unittest.main()
