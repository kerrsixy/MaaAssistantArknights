import ast
import copy
import os
import subprocess
import sys
import unittest
from pathlib import Path

from bounded_driver import choose_sale, decide
from phase1_runner import lifecycle_evidence
from spa_checkpoint import (
    checkpoint_roster,
    merge_receipt,
    setup_context_receipt,
    verified_lifecycle_receipt,
)
from spa_deployment_audit import audit_deployment_ledger


class DriverPolicyTests(unittest.TestCase):
    def test_deployment_audit_detects_duplicate_copy_after_untracked_swap(self):
        event = {
            "details": {
                "what": "SPAAutomationDeploymentReconciled",
                "details": {
                    "round": 4,
                    "remaining_deployment": 3,
                    "roster": [
                        {
                            "name": "承曦格雷伊",
                            "initial_copies": 1,
                            "elite": False,
                            "deployed": True,
                        }
                    ],
                    "field_positions": [
                        {
                            "name": "承曦格雷伊",
                            "x": x,
                            "y": 300,
                            "field_pose_verified": True,
                        }
                        for x in (803, 908)
                    ],
                },
            }
        }
        audit = audit_deployment_ledger([event])
        self.assertIn("exceed owned copies", audit["contradictions"][0]["errors"][0])
        self.assertFalse(audit["battle_gate"])

    def test_deployment_audit_accepts_actual_duplicate_initial_units(self):
        event = {
            "details": {
                "what": "SPAAutomationDeploymentReconciled",
                "details": {
                    "round": 3,
                    "remaining_deployment": 6,
                    "roster": [
                        {
                            "name": "幽灵鲨",
                            "initial_copies": 2,
                            "elite": False,
                            "deployed": True,
                        }
                    ],
                    "field_positions": [
                        {
                            "name": "幽灵鲨",
                            "x": x,
                            "y": 300,
                            "field_pose_verified": True,
                        }
                        for x in (803, 908)
                    ],
                },
            }
        }
        self.assertEqual(audit_deployment_ledger([event])["contradictions"], [])

    def test_deployment_audit_does_not_invent_positions_after_promotion(self):
        event = {
            "details": {
                "what": "SPAPromotionReconciled",
                "details": {
                    "roster": [
                        {
                            "name": "星极",
                            "initial_copies": 0,
                            "elite": True,
                            "deployed": True,
                        }
                    ],
                    "field_positions": [],
                    "remaining_deployment": 7,
                },
            }
        }
        audit = audit_deployment_ledger([event])
        self.assertEqual(audit["contradictions"], [])
        self.assertEqual(audit["verified_full_field_snapshots"], [])

    def test_deployment_audit_rejects_population_and_shared_position_conflicts(self):
        event = {
            "details": {
                "what": "SPAAutomationDeploymentReconciled",
                "details": {
                    "roster": [
                        {
                            "name": "幽灵鲨",
                            "initial_copies": 2,
                            "elite": False,
                            "deployed": True,
                        }
                    ],
                    "field_positions": [
                        {
                            "name": "幽灵鲨",
                            "x": 803,
                            "y": 300,
                            "field_pose_verified": True,
                        }
                    ]
                    * 2,
                    "remaining_deployment": 7,
                },
            }
        }
        errors = audit_deployment_ledger([event])["contradictions"][0]["errors"]
        self.assertEqual(len(errors), 2)

    def test_full_field_audit_requires_verified_coordinates_and_cannot_claim_victory(
        self,
    ):
        details = {
            "round": 9,
            "remaining_deployment": 0,
            "roster": [
                {"name": str(i), "initial_copies": 1, "deployed": True}
                for i in range(8)
            ],
            "field_positions": [
                {
                    "name": str(i),
                    "x": 180 + i * 80,
                    "y": 300,
                    "field_pose_verified": True,
                }
                for i in range(8)
            ],
        }
        event = {
            "details": {"what": "SPAAutomationDeploymentReconciled", "details": details}
        }
        self.assertEqual(
            audit_deployment_ledger([event])["verified_full_field_snapshots"], []
        )
        for point in details["field_positions"]:
            point["coordinate_frame"] = "collapsed"
        audit = audit_deployment_ledger([event])
        self.assertEqual(len(audit["verified_full_field_snapshots"]), 1)
        self.assertFalse(audit["stable_victory_verified"])
        pending = {
            "details": {
                "what": "SPAReplacementDeferred",
                "details": {
                    "pending_replacement": {"incoming": "new", "outgoing": "old"}
                },
            }
        }
        self.assertEqual(
            audit_deployment_ledger([pending, event])["verified_full_field_snapshots"],
            [],
        )
        reconciled = {
            "details": {
                "what": "SPAReplacementReconciled",
                "details": {"pending_replacement": None},
            }
        }
        self.assertEqual(
            len(
                audit_deployment_ledger([pending, reconciled, event])[
                    "verified_full_field_snapshots"
                ]
            ),
            1,
        )
        details["field_positions"][0]["x"] = 1200
        self.assertEqual(
            audit_deployment_ledger([event])["verified_full_field_snapshots"], []
        )

    def test_recovery_cannot_spawn_automatic_abandon_task(self):
        script = Path(__file__).with_name("observe_live.py")
        tree = ast.parse(script.read_text(encoding="utf-8"))
        for node in ast.walk(tree):
            if not isinstance(node, ast.Call) or not isinstance(
                node.func, ast.Attribute
            ):
                continue
            if (
                not isinstance(node.func.value, ast.Name)
                or node.func.value.id != "subprocess"
            ):
                continue
            arguments = [*node.args, *(keyword.value for keyword in node.keywords)]
            for argument in arguments:
                self.assertFalse(
                    any(
                        isinstance(value, ast.Constant)
                        and value.value == "abandon-home"
                        for value in ast.walk(argument)
                    ),
                    "Recovery must retain the live simulation instead of spawning abandon-home",
                )

    def test_intermediate_errors_need_verified_settlement_and_completed_root(self):
        receipt = {
            "return_home_verified": True,
            "battle_seen": True,
            "result_text_verified": True,
            "result_completed": False,
            "result_interrupted": True,
            "passed_rounds_known": True,
            "passed_rounds": 9,
            "result_difficulty_known": True,
            "result_difficulty": 14,
        }
        events = [
            {"message": 20000, "details": {}},
            {
                "message": 20003,
                "details": {
                    "what": "SPALifecycleCompleted",
                    "taskid": 1,
                    "details": receipt,
                },
            },
        ]
        self.assertIsNone(verified_lifecycle_receipt(events))
        events.append(
            {
                "message": 10002,
                "details": {"taskchain": "StrongholdProtocol", "taskid": 1},
            }
        )
        self.assertEqual(verified_lifecycle_receipt(events)["passed_rounds"], 9)
        receipt["result_completed"] = True
        self.assertIsNone(verified_lifecycle_receipt(events))

    def test_joint_settlement_keeps_named_difficulty_separate(self):
        receipt = {
            "return_home_verified": True,
            "battle_seen": True,
            "result_text_verified": True,
            "result_completed": True,
            "result_interrupted": False,
            "passed_rounds_known": True,
            "passed_rounds": 14,
            "result_difficulty_known": False,
            "result_difficulty": 0,
            "result_joint": True,
            "result_difficulty_name": "险境模拟",
        }
        events = [
            {
                "message": 20003,
                "details": {
                    "what": "SPALifecycleCompleted",
                    "taskid": 1,
                    "details": receipt,
                },
            },
            {
                "message": 10002,
                "details": {"taskchain": "StrongholdProtocol", "taskid": 1},
            },
        ]
        self.assertEqual(
            verified_lifecycle_receipt(events)["result_difficulty_name"], "险境模拟"
        )
        self.assertFalse(verified_lifecycle_receipt(events)["result_difficulty_known"])
        receipt["result_joint"] = False
        self.assertIsNone(verified_lifecycle_receipt(events))
        receipt["result_joint"] = True
        receipt["result_difficulty_name"] = "unknown"
        self.assertIsNone(verified_lifecycle_receipt(events))

    def test_quick_settlement_requires_its_observed_mode_and_round_number(self):
        receipt = {
            "return_home_verified": True,
            "battle_seen": True,
            "result_text_verified": True,
            "result_completed": True,
            "result_interrupted": False,
            "passed_rounds_known": True,
            "passed_rounds": 9,
            "result_difficulty_known": False,
            "result_joint": False,
            "result_difficulty_name": "极速模拟",
        }
        events = [
            {
                "message": 20003,
                "details": {
                    "what": "SPALifecycleCompleted",
                    "taskid": 1,
                    "details": receipt,
                },
            },
            {
                "message": 10002,
                "details": {"taskchain": "StrongholdProtocol", "taskid": 1},
            },
        ]
        self.assertEqual(verified_lifecycle_receipt(events)["passed_rounds"], 9)
        receipt["passed_rounds_known"] = False
        self.assertIsNone(verified_lifecycle_receipt(events))
        receipt["passed_rounds_known"] = True
        receipt["result_joint"] = True
        self.assertIsNone(verified_lifecycle_receipt(events))
        receipt["result_joint"] = False
        receipt["result_difficulty_name"] = ""
        self.assertIsNone(verified_lifecycle_receipt(events))

    def test_resume_retains_verified_bans_and_strategy_through_purchases(self):
        bans = {
            "complete": True,
            "availability_after_selection_verified": True,
            "disabled_pacts": ["阿戈尔", "叙拉古"],
        }
        context = {"run_id": "joint2", "mode": "joint", "difficulty": 2}
        context.update(setup_context_receipt("SPAPactAvailabilityScanned", bans))
        context.update(
            setup_context_receipt(
                "SPAStrategyCardSelected",
                {"strategy_id": "band_takecare", "joint_selection_committed": True},
            )
        )
        bought = merge_receipt(
            context, {"run_id": "joint2", "roster": [{"name": "深靛"}]}
        )
        self.assertEqual(bought["disabled_pacts"], ["阿戈尔", "叙拉古"])
        self.assertEqual(bought["selected_strategy_id"], "band_takecare")
        self.assertEqual(bought["mode"], "joint")
        self.assertEqual(
            setup_context_receipt(
                "SPAPactAvailabilityScanned",
                {**bans, "availability_after_selection_verified": False},
            ),
            {},
        )
        self.assertEqual(
            setup_context_receipt(
                "SPAStrategyCardSelected",
                {"strategy_id": "band_takecare", "joint_selection_committed": False},
            ),
            {},
        )

    def test_purchase_checkpoint_keeps_observed_deployment_geometry(self):
        deployed = {
            "run_id": "same",
            "roster": ["first"],
            "used_deploy_points": [{"x": 680, "y": 360}],
        }
        purchased = {
            "run_id": "same",
            "roster": ["first", "second"],
            "bench_occupied": 1,
        }
        merged = merge_receipt(deployed, purchased)
        self.assertEqual(merged["used_deploy_points"], deployed["used_deploy_points"])
        self.assertEqual(merged["roster"], purchased["roster"])
        self.assertNotIn(
            "used_deploy_points",
            merge_receipt(deployed, {"run_id": "other", "roster": []}),
        )

    def test_pending_promotion_keeps_all_initial_and_existing_elite_copies(self):
        unit = {
            "name": "幽灵鲨",
            "initial_copies": 3,
            "elite_copies": 1,
            "physical_copies": 4,
            "form_counts_known": True,
            "elite": True,
            "deployed": True,
        }
        purchase = {"run_id": "same", "roster": [unit], "promotion_pending": "幽灵鲨"}
        persisted = merge_receipt({"run_id": "same", "promotion_pending": ""}, purchase)
        roster, pending = checkpoint_roster(persisted)
        self.assertEqual(roster, [unit])
        self.assertEqual(roster[0]["initial_copies"], 3)
        self.assertEqual(pending, "幽灵鲨")
        completed = merge_receipt(
            persisted,
            {
                "run_id": "same",
                "promotion_pending": "",
                "roster": [
                    {
                        **unit,
                        "initial_copies": 0,
                        "elite_copies": 2,
                        "physical_copies": 2,
                    }
                ],
            },
        )
        restored, pending = checkpoint_roster(completed, "幽灵鲨")
        self.assertEqual(pending, "")
        self.assertEqual(restored[0]["elite_copies"], 2)

    def test_legacy_three_copy_recovery_does_not_erase_the_third_copy(self):
        state = {
            "roster": [
                {
                    "name": "幽灵鲨",
                    "initial_copies": 3,
                    "elite": False,
                    "deployed": True,
                }
            ]
        }
        original = copy.deepcopy(state)
        roster, pending = checkpoint_roster(state)
        self.assertEqual(pending, "幽灵鲨")
        self.assertEqual(roster[0]["initial_copies"], 3)
        self.assertEqual(state, original)

    def test_existing_elite_is_not_an_extra_initial_copy_for_synthesis(self):
        state = {
            "roster": [
                {
                    "name": "幽灵鲨",
                    "initial_copies": 2,
                    "elite_copies": 1,
                    "physical_copies": 3,
                    "elite": True,
                    "deployed": True,
                }
            ]
        }
        roster, pending = checkpoint_roster(state)
        self.assertEqual(pending, "")
        self.assertEqual(roster, state["roster"])

    def test_post_promotion_recount_stays_pending_until_a_fresh_counter(self):
        promoted = {
            "run_id": "same",
            "roster": [
                {
                    "name": "幽灵鲨",
                    "initial_copies": 0,
                    "elite_copies": 1,
                    "physical_copies": 1,
                    "elite": True,
                }
            ],
            "bench_occupied": 4,
            "bench_recount_pending": True,
        }
        waiting = merge_receipt(promoted, {"run_id": "same", "funds": 3})
        self.assertTrue(waiting["bench_recount_pending"])
        recounted = merge_receipt(
            waiting,
            {"run_id": "same", "bench_occupied": 5, "bench_recount_pending": False},
        )
        self.assertFalse(recounted["bench_recount_pending"])
        self.assertEqual(recounted["bench_occupied"], 5)

    def test_deployment_audit_counts_multiple_elite_bodies_exactly(self):
        unit = {
            "name": "幽灵鲨",
            "initial_copies": 0,
            "elite_copies": 2,
            "physical_copies": 2,
            "elite": True,
            "deployed": True,
        }
        points = [
            {"name": "幽灵鲨", "x": x, "y": 400, "field_pose_verified": True}
            for x in (500, 600, 700)
        ]
        event = {
            "details": {
                "what": "SPAReplacementReconciled",
                "details": {
                    "roster": [unit],
                    "field_positions": points[:2],
                    "remaining_deployment": 6,
                },
            }
        }
        self.assertEqual(audit_deployment_ledger([event])["contradictions"], [])
        event["details"]["details"]["field_positions"] = points
        event["details"]["details"]["remaining_deployment"] = 5
        errors = audit_deployment_ledger([event])["contradictions"][0]["errors"]
        self.assertTrue(any("exceed owned copies" in error for error in errors))
        unit["physical_copies"] = 3
        errors = audit_deployment_ledger([event])["contradictions"][0]["errors"]
        self.assertTrue(any("Contradictory explicit" in error for error in errors))

    def test_lobby_alone_cannot_complete_a_run(self):
        events = [{"details": {"what": "SPAObservation", "details": {"page": "lobby"}}}]
        self.assertFalse(lifecycle_evidence(events, True)["phase1_complete"])

    def test_pending_exchange_survives_purchases_until_explicitly_reconciled(self):
        pending = {
            "run_id": "same",
            "roster": ["old"],
            "pending_replacement": {
                "incoming": "水月",
                "outgoing": "风丸",
                "x": 928,
                "y": 439,
                "drag_replay_allowed": False,
            },
        }
        bought = merge_receipt(pending, {"run_id": "same", "roster": ["old", "new"]})
        self.assertEqual(bought["pending_replacement"], pending["pending_replacement"])
        reconciled = merge_receipt(
            bought, {"run_id": "same", "roster": ["new"], "pending_replacement": None}
        )
        self.assertIsNone(reconciled["pending_replacement"])

    def test_field_swap_journal_keeps_both_possible_outcomes_after_restart(self):
        initial = {
            "run_id": "same",
            "roster": ["tank", "caster"],
            "field_positions": [
                {"name": "tank", "x": 900, "y": 400},
                {"name": "caster", "x": 650, "y": 400},
            ],
            "pending_orientation": {
                "name": "tank",
                "x": 900,
                "y": 400,
                "target_x": 900,
                "target_y": 400,
                "displaced": "caster",
                "displaced_x": 650,
                "displaced_y": 400,
                "relocation": True,
                "remaining_deployment": 0,
            },
        }
        registered = dict(initial["pending_orientation"], target_x=652, target_y=401)
        before_release = merge_receipt(
            initial, {"run_id": "same", "pending_orientation": registered}
        )
        purchased = merge_receipt(before_release, {"run_id": "same", "funds": 1})
        self.assertEqual(purchased["pending_orientation"]["x"], 900)
        self.assertEqual(purchased["pending_orientation"]["target_x"], 652)
        self.assertEqual(purchased["pending_orientation"]["displaced_x"], 650)
        self.assertEqual(purchased["field_positions"], initial["field_positions"])
        settled = merge_receipt(
            purchased,
            {
                "run_id": "same",
                "pending_orientation": None,
                "field_positions": [
                    {"name": "tank", "x": 652, "y": 401},
                    {"name": "caster", "x": 900, "y": 400},
                ],
            },
        )
        self.assertIsNone(settled["pending_orientation"])
        self.assertEqual(settled["field_positions"][0]["x"], 652)

    def test_field_positions_and_held_exclusions_remain_separate_on_purchase(self):
        before = {
            "run_id": "same",
            "roster": [],
            "used_deploy_points": [{"x": 877, "y": 236}],
            "field_positions": [
                {
                    "name": "烈夏",
                    "x": 800,
                    "y": 185,
                    "field_pose_verified": True,
                    "coordinate_frame": "collapsed",
                }
            ],
        }
        after = merge_receipt(before, {"run_id": "same", "roster": ["next"]})
        self.assertEqual(after["field_positions"], before["field_positions"])
        self.assertNotEqual(
            after["field_positions"][0]["x"], after["used_deploy_points"][0]["x"]
        )

    def test_promotion_checkpoint_drops_consumed_field_positions(self):
        source = {"name": "星极", "x": 800, "y": 185, "field_pose_verified": True}
        before = {
            "run_id": "same",
            "field_positions": [source],
            "used_deploy_points": [{"x": 877, "y": 236}],
            "roster": [{"name": "星极", "elite": False, "initial_copies": 2}],
        }
        promoted = {
            "run_id": "same",
            "field_positions": [],
            "used_deploy_points": [{"x": 877, "y": 236}],
            "roster": [{"name": "星极", "elite": True, "initial_copies": 0}],
        }
        after = merge_receipt(before, promoted)
        self.assertEqual(after["field_positions"], [])
        self.assertTrue(after["roster"][0]["elite"])
        self.assertEqual(after["used_deploy_points"], before["used_deploy_points"])

    def test_lifecycle_requires_battle_settlement_and_home(self):
        events = [
            {"details": {"what": "SPABattleSample", "details": {"round": 4}}},
            {
                "details": {
                    "what": "SPAObservation",
                    "details": {
                        "page": "result",
                        "result_text_verified": True,
                        "result_interrupted": True,
                        "passed_rounds_known": True,
                        "passed_rounds": 4,
                        "result_difficulty_known": True,
                        "result_difficulty": 14,
                    },
                }
            },
        ]
        self.assertFalse(lifecycle_evidence(events, False)["phase1_complete"])
        audited = lifecycle_evidence(events, True)
        self.assertTrue(audited["phase1_complete"])
        self.assertEqual(audited["passed_rounds"], 4)
        self.assertEqual(audited["result_difficulty"], 14)

    def setUp(self):
        self.state = {
            "strategy": "众志合一",
            "mode": "independent",
            "difficulty": 3,
            "last_round": 8,
            "last_life": 24,
            "stop_before_round": 13,
            "lineup_reviewed": True,
            "shop_level": 3,
            "upgrade_preserves_target_verified": True,
            "roster": {
                "Miss.Christine": {"elite": False, "initial_copies": 2, "priority": 100}
            },
        }
        self.observation = {
            "page": "preparation",
            "contradictory": False,
            "round_known": True,
            "funds_known": True,
            "remaining_deployment_known": True,
            "life_known": True,
            "round": 8,
            "funds": 11,
            "remaining_deployment": 0,
            "life": 24,
            "texts": [{"text": "规程等级3", "score": 0.99, "x": 138, "y": 21}],
        }
        self.prices = {"Miss.Christine": 3}

    def test_safe_empty_budget_and_loss_review(self):
        self.observation["funds"] = 0
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "ready"
        )
        self.observation["life"] = 23
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "stop"
        )

    def test_wrong_mode_missing_counters_and_special_pages(self):
        for mutation in (
            {"page": "equipment_supply"},
            {"life_known": False},
            {"remaining_deployment": 1},
            {"contradictory": True},
            {"round": 14},
        ):
            observed = copy.deepcopy(self.observation)
            observed.update(mutation)
            self.assertEqual(decide(self.state, observed, self.prices)["task"], "stop")
        self.state["difficulty"] = 20
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "stop"
        )

    def test_third_copy_upgrades_first_only_when_affordable(self):
        self.observation["texts"] += [
            {"text": "Miss.Christine", "score": 0.99, "x": 973, "y": 665},
            {"text": "升级", "score": 0.99, "x": 200, "y": 645},
            {"text": "8", "score": 0.99, "x": 215, "y": 520},
        ]
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "upgrade"
        )
        self.observation["funds"] = 10
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "buy"
        )
        self.state["upgrade_preserves_target_verified"] = False
        self.observation["funds"] = 11
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "buy"
        )

    def test_no_unneeded_elite_copy_and_no_spurious_shop_text(self):
        self.observation["texts"].append(
            {"text": "Miss.Christine", "score": 0.99, "x": 973, "y": 665}
        )
        self.state["roster"]["Miss.Christine"]["elite"] = True
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "refresh"
        )
        self.state["roster"]["Miss.Christine"]["elite"] = False
        self.observation["texts"][-1]["y"] = 300
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "refresh"
        )

    def test_review_gate_prevents_any_new_input(self):
        self.state["review_required"] = True
        self.observation["funds"] = 0
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "stop"
        )

    def test_reviewed_shop_progression_does_not_roll_away_upgrade_budget(self):
        self.state["shop_milestones"] = [{"round": 8, "level": 4}]
        self.observation["texts"] += [
            {"text": "升级", "score": 0.99, "x": 200, "y": 645},
            {"text": "9", "score": 0.99, "x": 215, "y": 520},
        ]
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "upgrade"
        )
        self.observation["funds"] = 8
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "refresh"
        )
        self.observation["funds"] = 11
        self.state["shop_level"] = 4
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "refresh"
        )

    def test_upgrade_roi_fallback_and_conflict_preserve_progression_budget(self):
        self.state["shop_milestones"] = [{"round": 8, "level": 4}]
        self.observation["texts"].append(
            {"text": "升级", "score": 0.99, "x": 200, "y": 645}
        )
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "stop"
        )
        self.observation.update(upgrade_price_known=True, upgrade_price=8)
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "upgrade"
        )
        self.observation["texts"].append(
            {"text": "9", "score": 0.99, "x": 215, "y": 520}
        )
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "stop"
        )

    def test_bench_support_precedes_partial_economy_duplicate(self):
        self.state["roster"].update(
            {
                "星源": {
                    "elite": False,
                    "initial_copies": 0,
                    "priority": 70,
                    "bench_support_single": True,
                    "effect_verified": True,
                },
                "波登可": {"elite": False, "initial_copies": 1, "priority": 0},
            }
        )
        self.prices.update({"星源": 3, "波登可": 3})
        self.observation["texts"] += [
            {"text": "星源", "score": 0.99, "x": 485, "y": 665},
            {"text": "波登可", "score": 0.99, "x": 648, "y": 665},
        ]
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["buy_name"], "星源"
        )
        self.state["roster"]["星源"]["initial_copies"] = 1
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "refresh"
        )

    def test_equipment_owned_on_one_unit_does_not_block_another_reviewed_target(self):
        self.state["equipment_candidates"] = {
            "多功能补剂": [
                {
                    "target": "Miss.Christine",
                    "effect_verified": True,
                    "owned_or_pending": True,
                },
                {
                    "target": "凯瑟琳",
                    "effect_verified": True,
                    "owned_or_pending": False,
                },
            ]
        }
        self.state["equipment_slots_reviewed"] = True
        self.state["roster"]["凯瑟琳"] = {
            "elite": True,
            "initial_copies": 0,
            "priority": 0,
            "equipped_count": 1,
        }
        self.prices["多功能补剂"] = 2
        self.observation["texts"].append(
            {"text": "多功能补剂", "score": 0.99, "x": 318, "y": 665}
        )
        action = decide(self.state, self.observation, self.prices)
        self.assertEqual(action["target"], "凯瑟琳")
        self.assertEqual(action["candidate_index"], 1)
        self.state["roster"]["凯瑟琳"]["equipped_count"] = 2
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "refresh"
        )

    def test_sale_preserves_functional_units_and_rejects_insufficient_total_credit(
        self,
    ):
        self.state["run_id"] = "run1"
        candidate = {
            "name": "灰喉",
            "run_id": "run1",
            "available": True,
            "pacts_preserved_verified": True,
            "protected": False,
            "equipped_count": 0,
            "initial_copies": 1,
            "initial_copies_verified": True,
            "source": [940, 480],
            "credit": 1,
        }
        self.state["sale_candidates"] = [candidate]
        self.assertEqual(choose_sale(self.state, 2, 3)["sell_name"], "灰喉")
        self.assertIsNone(choose_sale(self.state, 1, 3))
        self.state["sale_candidates"] = [candidate, copy.deepcopy(candidate)]
        self.assertIsNone(choose_sale(self.state, 1, 3))
        for mutation in (
            {"protected": True},
            {"equipped_count": 1},
            {"run_id": "other"},
            {"pacts_preserved_verified": False},
            {"available": False},
            {"source": [540, 275]},
            {"initial_copies_verified": False},
            {"initial_copies": 2},
        ):
            self.state["sale_candidates"] = [dict(candidate, **mutation)]
            self.assertIsNone(choose_sale(self.state, 2, 3))
        self.state["sale_candidates"] = [candidate]
        self.state["roster"]["灰喉"] = {"initial_copies": 2}
        self.assertIsNone(choose_sale(self.state, 2, 3))
        self.state["roster"]["灰喉"] = {"bench_support_single": True}
        self.assertIsNone(choose_sale(self.state, 2, 3))

    def test_sale_funds_a_visible_core_and_preserves_the_purchase_reserve(self):
        self.state["run_id"] = "run1"
        self.state["sale_candidates"] = [
            {
                "name": "灰喉",
                "run_id": "run1",
                "available": True,
                "pacts_preserved_verified": True,
                "protected": False,
                "equipped_count": 0,
                "initial_copies": 1,
                "initial_copies_verified": True,
                "source": [940, 480],
                "credit": 1,
            }
        ]
        self.observation["funds"] = 2
        self.observation["texts"].append(
            {"text": "Miss.Christine", "score": 0.99, "x": 973, "y": 665}
        )
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "sell"
        )
        self.observation["funds"] = 3
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "buy"
        )
        self.observation["texts"].pop()
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "sell"
        )
        self.observation["funds"] = 0
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "ready"
        )

    def test_single_combat_candidate_is_not_a_bench_effect_or_duplicate_target(self):
        self.state["roster"]["逻各斯"] = {
            "elite": False,
            "initial_copies": 0,
            "priority": 120,
            "single_copy_candidate": True,
            "effect_verified": True,
        }
        self.prices["逻各斯"] = 4
        self.observation["texts"].append(
            {"text": "逻各斯", "score": 0.99, "x": 973, "y": 665}
        )
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["buy_name"], "逻各斯"
        )
        self.state["roster"]["逻各斯"]["initial_copies"] = 1
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "refresh"
        )

    def test_boss_shopping_does_not_implicitly_start_battle_after_spending(self):
        self.state["battle_review_rounds"] = [8]
        self.observation["funds"] = 0
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "stop"
        )
        self.observation["funds"] = 1
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "refresh"
        )

    def test_unreviewed_nonzero_ready_budget_is_rejected_before_connecting(self):
        result = subprocess.run(
            [
                sys.executable,
                str(Path(__file__).with_name("observe_live.py")),
                "--core-dir",
                "unused",
                "--adb",
                "does-not-exist",
                "--address",
                "unused",
                "--output",
                "unused",
                "--task",
                "ready",
                "--round",
                "14",
                "--remaining-deployment",
                "0",
                "--layout-reviewed",
                "--ready-funds",
                "11",
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            env={**os.environ, "PYTHONIOENCODING": "utf-8"},
            timeout=5,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("ready with remaining funds requires", result.stderr)

    def test_move_requires_recovery_origin_before_connecting(self):
        result = subprocess.run(
            [
                sys.executable,
                str(Path(__file__).with_name("observe_live.py")),
                "--core-dir",
                "unused",
                "--adb",
                "does-not-exist",
                "--address",
                "unused",
                "--output",
                "unused",
                "--task",
                "move",
                "--swap-name",
                "test",
                "--source",
                "600",
                "185",
                "--target",
                "750",
                "185",
                "--round",
                "7",
                "--funds",
                "1",
                "--remaining-deployment",
                "0",
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            env={**os.environ, "PYTHONIOENCODING": "utf-8"},
            timeout=5,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("move requires --drag-origin", result.stderr)

    def test_terrain_probe_requires_identity_and_state_before_connecting(self):
        result = subprocess.run(
            [
                sys.executable,
                str(Path(__file__).with_name("observe_live.py")),
                "--core-dir",
                "unused",
                "--adb",
                "does-not-exist",
                "--address",
                "unused",
                "--output",
                "unused",
                "--task",
                "terrain-probe",
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            env={**os.environ, "PYTHONIOENCODING": "utf-8"},
            timeout=5,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("unit action requires", result.stderr)

    def test_active_strategy_requires_effect_baseline_before_connecting(self):
        result = subprocess.run(
            [
                sys.executable,
                str(Path(__file__).with_name("observe_live.py")),
                "--core-dir",
                "unused",
                "--adb",
                "does-not-exist",
                "--address",
                "unused",
                "--output",
                "unused",
                "--task",
                "use-strategy",
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            env={**os.environ, "PYTHONIOENCODING": "utf-8"},
            timeout=5,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("use-strategy requires", result.stderr)

    def test_active_strategy_rejects_ambiguous_or_invalid_pact_baselines(self):
        for entries in (("坚守=4", "坚守=5"), ("坚守=-1",), ("坚守=1000",)):
            command = [
                sys.executable,
                str(Path(__file__).with_name("observe_live.py")),
                "--core-dir",
                "unused",
                "--adb",
                "does-not-exist",
                "--address",
                "unused",
                "--output",
                "unused",
                "--task",
                "use-strategy",
            ]
            for entry in entries:
                command.extend(("--pact-baseline", entry))
            result = subprocess.run(
                command,
                capture_output=True,
                text=True,
                encoding="utf-8",
                env={**os.environ, "PYTHONIOENCODING": "utf-8"},
                timeout=5,
            )
            self.assertEqual(result.returncode, 2)
            self.assertIn("pact-baseline", result.stderr)

    def test_full_formation_deploy_stops_before_connecting(self):
        result = subprocess.run(
            [
                sys.executable,
                str(Path(__file__).with_name("observe_live.py")),
                "--core-dir",
                "unused",
                "--adb",
                "does-not-exist",
                "--address",
                "unused",
                "--output",
                "unused",
                "--task",
                "deploy",
                "--swap-name",
                "车尔尼",
                "--source",
                "845",
                "465",
                "--target",
                "600",
                "240",
                "--round",
                "6",
                "--funds",
                "3",
                "--remaining-deployment",
                "0",
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            env={**os.environ, "PYTHONIOENCODING": "utf-8"},
            timeout=5,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("full formation requires swap", result.stderr)

    def test_equipment_requires_known_effect_and_target_slot(self):
        self.state["equipment_candidates"] = {
            "多功能补剂": {
                "target": "Miss.Christine",
                "effect_verified": True,
                "owned_or_pending": False,
            }
        }
        self.state["equipment_slots_reviewed"] = True
        self.state["roster"]["Miss.Christine"]["equipped_count"] = 1
        self.prices["多功能补剂"] = 2
        self.observation["texts"].append(
            {"text": "多功能补剂", "score": 0.99, "x": 318, "y": 665}
        )
        self.assertTrue(
            decide(self.state, self.observation, self.prices)["is_equipment"]
        )
        self.state["roster"]["Miss.Christine"]["equipped_count"] = 2
        self.assertEqual(
            decide(self.state, self.observation, self.prices)["task"], "refresh"
        )


if __name__ == "__main__":
    unittest.main()
