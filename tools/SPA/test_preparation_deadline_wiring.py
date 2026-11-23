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
            "if (joint_clock_recheck || (joint_seconds && *joint_seconds <= 8 && !prioritize_joint_ready)) {", 1
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
        self.assertIn("if (!joint_clock_recheck && !sleep(400))", boundary)
        ready_gate = self.session.split("const bool prioritize_joint_ready =", 1)[1].split(";", 1)[0]
        self.assertIn("!joint_clock_recheck && m_auto_run", ready_gate)
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

    def test_timed_replacement_precedes_only_optional_shop_search(self):
        boundary = self.session.split("const bool can_defer_shop_search =", 1)[1].split(
            "std::string full_bench_refresh_quote_source;", 1
        )[0]
        self.assertIn("PreparationAction::Refresh || decision.action == spa::PreparationAction::Review", boundary)
        self.assertIn('m_mode == "joint" && m_auto_run', boundary)
        self.assertIn("!purchase_reserved_after_sale && !m_bench_funding_pair", boundary)
        self.assertIn("observe(round, std::nullopt, spa::PreparationDeadline::Clock::now())", boundary)
        self.assertIn("handle_full_field_replacement(replacement_state)", boundary)

    def test_funded_purchase_defers_equipment_until_live_quote_review(self):
        before_shop, quote_review = self.session.split(
            "bool purchase_reserved_after_sale = false;", 1
        )
        scheduling = before_shop.split("const bool resume_funded_purchase =", 1)[1]
        scheduling = " ".join(scheduling.split("if (m_shop_review_after_replacements", 1)[0].split())
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
            "!defer_ordinary_equipment && !prioritize_joint_ready && refresh_equipment_inventory(state)",
            scheduling,
        )
        self.assertIn(
            "const bool ordinary_equipment_due = field_finalization || equipment_capacity_blocked;",
            scheduling,
        )
        self.assertIn(
            "m_auto_run && ordinary_equipment_due && time_for_board_maintenance && !defer_ordinary_equipment &&",
            scheduling,
        )
        self.assertIn("!prioritize_joint_ready && handle_persistent_equipment(state)", scheduling)
        inspect_inventory = scheduling.index("refresh_equipment_inventory(state)")
        self.assertLess(
            scheduling.index("if (equipment_capacity_blocked && use_known_equipment())"),
            inspect_inventory,
        )
        self.assertGreater(
            scheduling.index("if (!equipment_capacity_blocked && use_known_equipment())"),
            inspect_inventory,
        )
        self.assertIn(
            "handle_cash_equipment(state, boss_preparation) || handle_delayed_cash_equipment(state)",
            scheduling,
        )
        pending = quote_review.split("const int bench_before_funding", 1)[0]
        self.assertIn("spa::unique_funding_quote(offers, reserved)", pending)
        self.assertIn("m_purchase_reserved_after_sale.reset()", pending)

    def test_replacements_share_one_shop_review_after_equipment_and_last_coin_review(self):
        source = TASK.read_text(encoding="utf-8")
        commit = source.split(
            "void asst::StrongholdProtocolTask::commit_full_field_replacement(", 1
        )[1].split("bool asst::StrongholdProtocolTask::handle_promotion_page(", 1)[0]
        self.assertIn(
            "m_shopping_complete && (m_policy.primary_locked_after_activation || m_shop_reviewed_last_ordinary_coin)",
            commit,
        )
        self.assertIn("m_shopping_complete = m_shop_review_after_replacements;", commit)
        self.assertIn("m_spending_reviewed = false;", commit)

        after_replacement = self.session.split(
            "handle_full_field_replacement(state))", 1
        )[1]
        equipment_and_cash, reopening = after_replacement.split(
            "if (m_shop_review_after_replacements && !prioritize_joint_ready)", 1
        )
        self.assertIn("refresh_equipment_inventory(state)", equipment_and_cash)
        self.assertIn("handle_cash_equipment(state, boss_preparation)", equipment_and_cash)
        self.assertIn("continue;", equipment_and_cash)
        reopening = reopening.split("if (!m_unreconciled_sale.empty())", 1)[0]
        self.assertIn("m_shop_review_after_replacements = false;", reopening)
        self.assertIn("m_shopping_complete = false;", reopening)
        self.assertIn("const bool reopen = m_shopping_complete && available_funds > 0;", reopening)
        self.assertIn("continue;", reopening)
        last_coin_review = self.session.split("m_shop_reviewed_last_ordinary_coin = m_policy.dynamic_decision_mode", 1)[
            1
        ].split(";", 1)[0]
        self.assertIn("funds == 1", last_coin_review)
        self.assertIn("ordinary_balance_rule_known", last_coin_review)
        self.assertIn("m_policy.free_refreshes == 0", last_coin_review)


    def test_boss_layout_replans_once_after_cancelled_held_registration(self):
        layout = TASK.with_name("StrongholdProtocolLayout.cpp").read_text(encoding="utf-8")
        reconciled = layout.split("if (!executor->field_pose_verified() ||", 1)[1]
        restored = reconciled.split("bool replan_after_held_failure = false;", 1)[0]
        self.assertIn("return false;", restored)
        self.assertIn("m_spatial_snapshot_round = -1;", restored)
        recovery = reconciled.split("replan_after_held_failure =", 2)[2].split(
            "m_dynamic_layout_target.clear();", 1
        )[0]
        self.assertIn('m_mode == "independent"', recovery)
        self.assertIn("m_spatial_boss_phase_round == round", recovery)
        self.assertIn("executor->spatial_held_registration_failed()", recovery)
        self.assertIn("!executor->committed_facing()", recovery)
        self.assertIn("m_boss_layout_fixed_unit.try_emplace(round, name).second", recovery)
        self.assertIn("if (!replan_after_held_failure)", recovery)
        self.assertIn("m_failed_layout_context =", recovery)
        self.assertIn("!healer_already_adjusted && !fixed_after_held_failure;", layout)


    def test_same_round_grid_recovery_checks_exactly_one_unknown_pose_before_full_bootstrap(self):
        layout = TASK.with_name("StrongholdProtocolLayout.cpp").read_text(encoding="utf-8")
        recovery = layout.split("const bool single_member_recovered =", 1)[1].split(
            "if (bootstrap && probe.field_pose_verified()", 1
        )[0]
        self.assertIn("!m_field_checkpoint_loaded", recovery)
        self.assertIn("unverified_poses == 1", recovery)
        self.assertIn("m_spatial_snapshot_round == round", recovery)
        self.assertIn("unchecked_member && observe_grid(unchecked_member)", " ".join(recovery.split()))
        self.assertIn("single_member_recovered || probe.probe_registered_grid(", recovery)
        executor = (
            TASK.parent.parent / "MiniGame" / "SPA" / "SPASwapTaskPlugin.cpp"
        ).read_text(encoding="utf-8").split("::observe_grid_snapshot(", 1)[1].split(
            "::revalidate_promoted_pose(", 1
        )[0]
        source_probe = executor.index("selected_field_identity(Point(member.x, member.y), member.name, selected)")
        closed = executor.index("capture_collapsed_preparation(current, selected)")
        registered = executor.index("registered_field_points(camera_fit, points)")
        self.assertLess(source_probe, closed)
        self.assertLess(closed, registered)
        self.assertIn("cv::norm(projected->at(index) - points[index]) > 12", executor)
        self.assertIn("registered_grid_members(*projection, aligned)", executor)
        self.assertNotIn("reorient_in_place()", executor)


if __name__ == "__main__":
    unittest.main()
