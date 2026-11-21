"""Check native scheduling boundaries; clock behavior is tested in C++."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
TASK = ROOT / "src/MaaCore/Task/Interface/StrongholdProtocolTask.cpp"


class PreparationDeadlineWiringTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.session = TASK.read_text(encoding="utf-8").split(
            "bool asst::StrongholdProtocolTask::run_session(", 1
        )[1]

    def test_only_fresh_observer_calls_supply_frame_timestamp(self):
        boundary = self.session.split("json::value state_storage;", 1)[1].split(
            "const auto& state = state_storage;", 1
        )[0]
        uncached = boundary.index("if (!used_cached_deployment_observation)")
        timestamp = boundary.index(
            "observation_started = spa::PreparationDeadline::Clock::now();"
        )
        observe = boundary.index("if (!observer.run())")
        consume = boundary.index("m_joint_preparation_deadline.observe_frame(")
        self.assertLess(uncached, timestamp)
        self.assertLess(timestamp, observe)
        self.assertLess(observe, consume)
        self.assertEqual(boundary.count("observation_started ="), 1)
        self.assertRegex(boundary, r"read_now\s*=\s*observation_started\.has_value\(\)")
        self.assertIn(
            'state_storage["preparation_seconds_remaining_known"] = remaining.has_value();',
            boundary,
        )

    def test_expired_branch_reconciles_existing_pending_without_new_actions(self):
        boundary = self.session.split(
            'state.get("preparation_seconds_remaining", 0) <= 8) {', 1
        )[1].split("if (m_bench_recount_pending", 1)[0]
        self.assertIn(
            "if (m_pending_spatial_orientation) {\n"
            "                static_cast<void>(handle_spatial_reorientation(state));\n"
            "            }",
            boundary,
        )
        self.assertLess(
            boundary.index("handle_spatial_reorientation(state)"),
            boundary.index('basic_info_with_what("SPAJointPreparationDeadline")'),
        )
        self.assertIn('"new_transaction_issued", false', boundary)
        self.assertIn("if (!sleep(400))", boundary)
        self.assertTrue(boundary.rstrip().endswith("continue;\n        }"))

    def test_full_field_swap_waits_for_shop_review_but_reconciliation_does_not(self):
        boundary = self.session.split("const bool field_finalization =", 1)[1].split(
            "refresh_equipment_inventory(state)", 1
        )[0]
        self.assertTrue(
            boundary.startswith(" m_shopping_complete || available_funds == 0;")
        )
        self.assertIn(
            "((field_finalization && time_for_board_maintenance) || m_pending_field_replacement) &&\n"
            "            handle_full_field_replacement(state))",
            boundary,
        )

    def test_funded_purchase_defers_equipment_until_live_quote_review(self):
        before_shop, quote_review = self.session.split(
            "bool purchase_reserved_after_sale = false;", 1
        )
        scheduling = before_shop.split("const bool resume_funded_purchase =", 1)[1]
        self.assertIn(
            "m_purchase_reserved_after_sale->first == preparation_round", scheduling
        )
        self.assertIn(
            "m_purchase_reserved_after_sale->second.price <= available_funds",
            scheduling,
        )
        self.assertIn("!m_bench_recount_pending", scheduling)
        self.assertIn("prioritize_bench_space || resume_funded_purchase", scheduling)
        self.assertIn(
            "!defer_ordinary_equipment && refresh_equipment_inventory(state)",
            scheduling,
        )
        self.assertIn(
            "!defer_ordinary_equipment && handle_persistent_equipment(state)",
            scheduling,
        )
        pending = quote_review.split("const int bench_before_funding", 1)[0]
        self.assertIn("offer.right_slot == reserved.right_slot", pending)
        self.assertIn("quoted->name == reserved.name", pending)
        self.assertIn("quoted->price == reserved.price", pending)
        self.assertIn("m_purchase_reserved_after_sale.reset()", pending)


if __name__ == "__main__":
    unittest.main()
