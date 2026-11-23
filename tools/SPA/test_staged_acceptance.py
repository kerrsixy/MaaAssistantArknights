import copy
import json
from pathlib import Path
import tempfile
import unittest

from spa_host_completion import audit_host_completion
from spa_staged_acceptance import (
    audit_directory,
    audit_run,
    ending_growth_review,
    evidence_hash,
    reload_report,
    replay_progress,
    select_execution,
    stage_sequence,
    strategy_badge_requirements,
    terminal_pact_sample,
)
from test_joint_acceptance import event, full_run


class GrowthReviewTests(unittest.TestCase):
    def test_alliance_review_counts_support_pacts_and_keeps_win_separate(self):
        run = "quality"
        layer_snapshot = {"伊比利亚": 101, "精准": 500}
        battle = event(
            "SPABattleSample",
            {"round": 14, "pact_layers": layer_snapshot, "battle_frame_saved": True},
        )
        battle["details"].update(
            run_id=run, run_execution_id="a", **{"class": "asst::spa::RoundTaskPlugin"}
        )
        review = event(
            "SPABattleEndingGrowthReview",
            {
                "round": 14,
                "primary_pact": "伊比利亚",
                "battle_sample_pact_layers": dict(layer_snapshot),
                "settlement": {
                    "round": 14,
                    "result_frame_saved": True,
                    "result_text_verified": True,
                    "battle_seen": True,
                    "result_completed": False,
                },
            },
        )
        review["details"].update(run_id=run, run_execution_id="a")
        proof = ending_growth_review([battle, review], run)
        self.assertTrue(proof["route_growth_accepted"])
        self.assertFalse(proof["needs_growth_review"])
        layer_snapshot["精准"] = 299
        self.assertIsNone(ending_growth_review([battle, review], run))
        review["details"]["details"]["battle_sample_pact_layers"]["精准"] = 299
        review["details"]["details"]["settlement"]["result_completed"] = True
        proof = ending_growth_review([battle, review], run)
        self.assertFalse(proof["route_growth_accepted"])
        self.assertTrue(proof["needs_growth_review"])
        self.assertIsNone(ending_growth_review([review, battle], run))
        review["details"]["run_execution_id"] = "other"
        self.assertIsNone(ending_growth_review([battle, review], run))

    def test_sample_thresholds_are_diagnostic_and_need_a_matching_battle_frame(self):
        run = "route-flame"
        battle = event(
            "SPABattleSample",
            {
                "round": 14,
                "primary_pact": "炎",
                "primary_layers": 500,
                "primary_layers_source": "battle_hud",
                "battle_frame_saved": True,
            },
        )
        battle["details"].update(
            run_id=run, run_execution_id="a", **{"class": "asst::spa::RoundTaskPlugin"}
        )
        review = event(
            "SPAPrimaryPactSettlementReview",
            {
                "round": 14,
                "primary_pact": "炎",
                "battle_sample_layers": 500,
                "settlement": {
                    "round": 14,
                    "result_frame_saved": True,
                    "result_text_verified": True,
                    "battle_seen": True,
                },
            },
        )
        review["details"].update(run_id=run, run_execution_id="a")
        self.assertTrue(terminal_pact_sample([battle, review], run)["met_500"])
        self.assertFalse(terminal_pact_sample([battle, review], run)["observed_cap"])
        self.assertIsNone(terminal_pact_sample([review], run))
        self.assertIsNone(terminal_pact_sample([review, battle], run))
        for layers, minimum, advanced, capped in (
            (299, False, False, False),
            (300, True, False, False),
            (999, True, True, True),
        ):
            battle["details"]["details"]["primary_layers"] = layers
            review["details"]["details"]["battle_sample_layers"] = layers
            proof = terminal_pact_sample([battle, review], run)
            self.assertEqual(
                (proof["met_300"], proof["met_500"], proof["observed_cap"]),
                (minimum, advanced, capped),
            )
        review["details"]["run_execution_id"] = "b"
        self.assertIsNone(terminal_pact_sample([battle, review], run))


def solo_run(mode="quick", difficulty=None):
    label = "极速模拟" if mode == "quick" else f"规程等级{difficulty}"
    result = {
        "page": "result",
        "result_joint": False,
        "result_text_verified": True,
        "result_completed": True,
        "result_interrupted": False,
        "passed_rounds_known": True,
        "passed_rounds": 9 if mode == "quick" else 14,
        "result_difficulty_known": mode != "quick",
        "result_difficulty": difficulty or 0,
        "result_difficulty_name": "极速模拟" if mode == "quick" else "",
        "return_home_verified": True,
        "battle_seen": True,
        "contradictory": False,
    }
    return [
        event("SPAObservation", {"page": "lobby"}),
        event(
            "SPAStrategyCardSelected",
            {
                "strategy_id": "band_weakness",
                "stage_two_verified": True,
                "stage_three_verified": True,
            },
        ),
        event(
            "SPAFormationSelected",
            {
                "formation_page_verified": True,
                "readiness_committed": True,
                "empty_formation": True,
            },
        ),
        event(
            "SPAObservation", {"page": "preparation", "round_known": True, "round": 1}
        ),
        event(
            "SPAObservation",
            {
                "page": "battle",
                "texts": [
                    {"text": label, "x": 130, "y": 20, "score": 0.999},
                    {"text": "2/2", "x": 640, "y": 20, "score": 0.999},
                ],
            },
        ),
        event("SPAObservation", copy.deepcopy(result)),
        event("SPAObservation", {"page": "lobby"}),
        event("SPALifecycleCompleted", result),
        {"message": 10002, "details": {"taskchain": "StrongholdProtocol", "taskid": 1}},
    ]


def scope_run(raw, policy, execution="execution-1", host="host-1"):
    boundary = {
        "run_id": policy["run_id"],
        "mode": policy["mode"],
        "difficulty": policy["difficulty"],
        "task_id": 1,
        "assisted_run": policy.get("assisted_run", False),
    }
    # A batch child ends before the Assistant's outer TaskChainCompleted.
    records = (
        [event("SPARunExecutionStarted", boundary)]
        + copy.deepcopy(raw[:-1])
        + [
            event(
                "SPARunExecutionFinished",
                {**boundary, "runner_success": True, "stop_requested": False},
            )
        ]
    )
    for record in records:
        record["details"].update(
            run_id=policy["run_id"], run_execution_id=execution, host_session_id=host
        )
    return records


def review_for(events, policy):
    scope = next(
        (
            event["details"]
            for event in events
            if event.get("details", {}).get("run_execution_id")
        ),
        {},
    )
    return {
        "run_id": policy["run_id"],
        "run_execution_id": scope.get("run_execution_id"),
        "host_session_id": scope.get("host_session_id"),
        "events_sha256": evidence_hash(events),
        "policy_sha256": evidence_hash(policy),
        "review_complete": True,
        "manual_takeover": False,
        "host_recoveries": 0,
    }


def native_battle_sample():
    sample = event(
        "SPABattleSample",
        {
            "round": 1,
            "enemies_total_known": True,
            "enemies_total": 3,
            "enemies_resolved_known": True,
            "enemies_resolved": 2,
        },
    )
    sample["details"].update(
        {"class": "asst::spa::RoundTaskPlugin", "taskchain": "StrongholdProtocol"}
    )
    return sample


def independent_extra_boss_run(difficulty):
    raw = solo_run("independent", difficulty)
    preparation = event(
        "SPAObservation",
        {
            "page": "preparation",
            "round": 15,
            "round_known": True,
            "bonus_round": True,
            "contradictory": False,
            "round_source": "independent_hidden_core_phase",
            "texts": [
                {"text": f"规程等级{difficulty}", "x": 138, "y": 22, "score": 0.998},
                {
                    "text": "??",
                    "x": 487,
                    "y": 25,
                    "score": 1.0,
                    "source": "hidden_round_badge",
                },
            ],
        },
    )
    preparation["details"]["class"] = "asst::spa::ObserverTaskPlugin"
    started = event(
        "SPARoundStarted", {"round": 15, "hidden_core_preparation_verified": True}
    )
    started["details"]["class"] = "asst::spa::RoundTaskPlugin"
    sampled = native_battle_sample()
    sampled["details"]["details"].update(
        round=15,
        battle_frame_saved=True,
        regulation_hud_titles=[f"规程等级{difficulty}"],
    )
    raw[5]["details"]["class"] = "asst::spa::ObserverTaskPlugin"
    raw[5]["details"]["details"].update(passed_rounds=15, result_frame_saved=True)
    raw[7]["details"]["details"].update(passed_rounds=15)
    for native_event in (preparation, started, raw[5]):
        native_event["details"]["taskchain"] = "StrongholdProtocol"
    raw[5:5] = [preparation, started, sampled]
    return raw


class StagedAcceptanceTests(unittest.TestCase):
    policy = {"run_id": "run-1", "mode": "quick", "difficulty": -1}

    def reviewed(self, raw=None, policy=None, execution="execution-1"):
        policy = policy or self.policy
        events = scope_run(raw or solo_run(), policy, execution)
        return audit_run(events, policy, review_for(events, policy))

    def test_verified_front_end_navigation_can_precede_the_first_lobby(self):
        navigation = event(
            "SPALifecycleTransition",
            {"page": "main_home", "next": "MiniGame@SPA@LifecycleFindLobby"},
        )
        navigation["details"]["class"] = "asst::spa::LifecycleTaskPlugin"
        opening = {
            "message": 20002,
            "details": {
                "taskid": 1,
                "class": "asst::ProcessTask",
                "details": {"task": "OpenLobby", "action": "ClickSelf"},
            },
        }
        before_lobby = event("SPAObservation", {"page": "unknown"})
        self.assertTrue(
            self.reviewed([before_lobby, navigation, opening] + solo_run())[
                "qualified_win"
            ]
        )
        self.assertFalse(
            self.reviewed([before_lobby, navigation] + solo_run())["qualified_win"]
        )
        self.assertFalse(
            self.reviewed([before_lobby, opening] + solo_run())["qualified_win"]
        )
        mid_run = event("SPAObservation", {"page": "unknown", "setup_stage": "1/3"})
        self.assertFalse(
            self.reviewed([mid_run, navigation, opening] + solo_run())["qualified_win"]
        )
        earlier_battle = event(
            "SPAObservation", {"page": "battle", "round_known": True, "round": 7}
        )
        self.assertFalse(
            self.reviewed([earlier_battle, navigation, opening] + solo_run())[
                "qualified_win"
            ]
        )

    def test_unclassified_navigation_frame_requires_completed_native_menu_action(self):
        def dispatch(page):
            result = event(
                "SPALifecycleTransition",
                {"page": page, "next": "MiniGame@SPA@LifecycleFindLobby"},
            )
            result["details"]["class"] = "asst::spa::LifecycleTaskPlugin"
            return result

        def click(task):
            return {
                "message": 20002,
                "details": {
                    "taskid": 1,
                    "class": "asst::ProcessTask",
                    "details": {"task": task, "action": "ClickSelf"},
                },
            }

        unknown = event("SPAObservation", {"page": "unknown"})
        prefix = [
            unknown,
            dispatch("main_home"),
            click("LifecycleOpenMenu"),
            unknown,
            dispatch("unknown"),
            click("LifecycleMenuTerminalText"),
            unknown,
            dispatch("terminal_overview"),
            click("LifecycleTerminalBanner"),
        ]
        self.assertTrue(self.reviewed(prefix + solo_run())["qualified_win"])
        self.assertFalse(
            self.reviewed(prefix[:5] + prefix[6:] + solo_run())["qualified_win"]
        )
        undescribed = prefix[:]
        undescribed[5] = click("LifecycleDispatch")
        self.assertFalse(self.reviewed(undescribed + solo_run())["qualified_win"])
        no_page_anchor = prefix[:]
        no_page_anchor[1] = no_page_anchor[7] = dispatch("unknown")
        self.assertFalse(self.reviewed(no_page_anchor + solo_run())["qualified_win"])

    def test_lobby_must_precede_strategy_and_formation(self):
        records = solo_run()
        records[0], records[1] = records[1], records[0]
        self.assertFalse(self.reviewed(records)["qualified_win"])

    def test_watcher_settlement_requires_saved_frame_and_separate_lobby(self):
        records = solo_run()
        original = records[5]["details"]["details"]
        records[5] = event(
            "SPARoundResultObserved",
            {
                **original,
                "return_home_verified": False,
                "result_frame_saved": True,
            },
        )
        records[5]["details"]["class"] = "asst::spa::RoundTaskPlugin"
        self.assertTrue(self.reviewed(records)["qualified_win"])
        without_lobby_return = records[:6] + records[7:]
        self.assertFalse(self.reviewed(without_lobby_return)["qualified_win"])
        records[5]["details"]["details"]["result_frame_saved"] = False
        self.assertFalse(self.reviewed(records)["qualified_win"])
        records[5]["details"]["details"]["result_frame_saved"] = True
        records[5]["details"]["class"] = "asst::ProcessTask"
        self.assertFalse(self.reviewed(records)["qualified_win"])

    def test_full_quick_run_requires_evidence_bound_review(self):
        events = scope_run(solo_run(), self.policy)
        review = review_for(events, self.policy)
        report = audit_run(events, self.policy, review)
        self.assertTrue(report["qualified_win"])
        self.assertFalse(report["hidden_boss_verified"])
        self.assertFalse(report["strategy_badge_verified"])
        unreviewed = audit_run(events, self.policy, {})
        self.assertTrue(unreviewed["no_known_intervention_candidate"])
        self.assertFalse(unreviewed["qualified_win"])
        for changed in [
            {"review_complete": False},
            {"manual_takeover": True},
            {"host_recoveries": 1},
            {"run_id": "other"},
            {"events_sha256": "0" * 64},
            {"policy_sha256": "0" * 64},
            {"run_execution_id": "other"},
            {"host_session_id": "other"},
        ]:
            self.assertFalse(
                audit_run(events, self.policy, {**review, **changed})["qualified_win"]
            )

    def test_strategy_badge_needs_the_selected_identity_and_a_fresh_report(self):
        policy = {
            "run_id": "scheduled-band",
            "mode": "independent",
            "difficulty": 1,
            "schedule_strategy_badges": True,
        }
        quick_wins = [
            self.reviewed(
                policy={**self.policy, "run_id": f"badge-quick-{attempt}"},
                execution=f"badge-quick-{attempt}",
            )
            for attempt in range(3)
        ]
        for wins, tier in ((3, "orange"), (6, "silver"), (14, "silver")):
            raw = solo_run("independent", 1)
            raw[-2]["details"]["taskchain"] = "StrongholdProtocol"
            badge = event(
                "SPAObservation",
                {
                    "page": "strategy_report",
                    "band_id": "band_weakness",
                    "wins": wins,
                    "wins_known": True,
                    "contradictory": False,
                    "badge_verified": True,
                    "strategy_report_identity_verified": True,
                    "strategy_progress_text_verified": True,
                    "strategy_report_evidence_saved": True,
                    "strategy_badge_tier": tier,
                },
            )
            badge["details"].update(
                {
                    "class": "asst::spa::ObserverTaskPlugin",
                    "taskchain": "StrongholdProtocol",
                }
            )
            settled = event(
                "SPAStrategyBadgeScheduleSettled",
                {
                    "requested_strategy": "band_weakness",
                    "actual_strategy": "band_weakness",
                    "same_strategy": True,
                    "settlement_victory_verified": True,
                    "report_win_delta_verified": True,
                    "three_win_badge_verified": True,
                    "report_wins_before": wins - 1,
                    "report_wins_after": wins,
                },
            )
            settled["details"].update(
                {
                    "class": "asst::StrongholdProtocolTask",
                    "taskchain": "StrongholdProtocol",
                }
            )
            raw[-1:-1] = [badge, settled]
            result = self.reviewed(raw, policy)
            self.assertTrue(result["qualified_win"])
            self.assertTrue(result["strategy_badge_verified"])
            self.assertEqual(result["strategy_badge_evidence"]["badge_tier"], tier)
            self.assertEqual(
                result["strategy_badge_evidence"]["report_wins_after"], wins
            )
            progress = replay_progress([*quick_wins, result])
            self.assertFalse(progress["all_strategy_badges_verified"])
            earned = progress["verified_strategy_badges"]
            self.assertEqual(set(earned), {"band_weakness"})
            self.assertEqual(earned["band_weakness"]["wins"], wins)
            self.assertEqual(earned["band_weakness"]["badge_tier"], tier)
            for field, value in (
                ("band_id", "band_amiya"),
                ("strategy_report_evidence_saved", False),
                ("strategy_badge_tier", "silver" if tier == "orange" else "orange"),
            ):
                invalid = copy.deepcopy(raw)
                invalid[-3]["details"]["details"][field] = value
                self.assertFalse(
                    self.reviewed(invalid, policy)["strategy_badge_verified"]
                )
            swapped = list(raw)
            swapped[-2], swapped[-3] = swapped[-3], swapped[-2]
            self.assertFalse(self.reviewed(swapped, policy)["strategy_badge_verified"])

    def test_hidden_core_requires_same_run_preparation_battle_and_saved_settlement(
        self,
    ):
        policy = {"run_id": "hidden-1", "mode": "independent", "difficulty": 5}
        raw = independent_extra_boss_run(5)
        report = self.reviewed(raw, policy)
        self.assertTrue(report["qualified_win"])
        self.assertTrue(report["hidden_core_victory_verified"])
        self.assertFalse(report["hidden_boss_verified"])
        self.assertEqual(
            report["hidden_core_evidence"],
            {
                "preparation_event_index": 6,
                "battle_start_event_index": 7,
                "battle_sample_event_index": 8,
                "result_event_index": 9,
            },
        )
        for index, field, wrong in (
            (5, "round_source", "joint_risk_bonus_phase"),
            (
                5,
                "texts",
                [
                    {
                        "text": "??",
                        "x": 487,
                        "y": 25,
                        "score": 1.0,
                        "source": "hidden_round_badge",
                    }
                ],
            ),
            (6, "hidden_core_preparation_verified", False),
            (7, "enemies_total_known", False),
            (7, "battle_frame_saved", False),
            (7, "regulation_hud_titles", ["规程等级5", "规程等级6"]),
            (7, "round", 14),
            (8, "result_frame_saved", False),
        ):
            with self.subTest(field=field):
                changed = copy.deepcopy(raw)
                changed[index]["details"]["details"][field] = wrong
                result = self.reviewed(changed, policy)
                self.assertTrue(result["qualified_win"])
                self.assertFalse(result["hidden_core_victory_verified"])
        reordered = copy.deepcopy(raw)
        reordered[6], reordered[7] = reordered[7], reordered[6]
        self.assertFalse(
            self.reviewed(reordered, policy)["hidden_core_victory_verified"]
        )
        normal_outcome = copy.deepcopy(raw)
        for i in (8, 10):
            normal_outcome[i]["details"]["details"]["passed_rounds"] = 14
        self.assertTrue(self.reviewed(normal_outcome, policy)["qualified_win"])
        self.assertFalse(
            self.reviewed(normal_outcome, policy)["hidden_core_victory_verified"]
        )
        low_level = self.reviewed(
            independent_extra_boss_run(4), {**policy, "difficulty": 4}
        )
        self.assertFalse(low_level["qualified_win"])
        self.assertFalse(low_level["hidden_core_victory_verified"])
        events = scope_run(raw, policy)
        self.assertFalse(audit_run(events, policy, {})["hidden_core_victory_verified"])

    def test_hidden_core_progress_only_credits_the_eligible_stage(self):
        reports = []
        for mode, difficulty in stage_sequence()[:5]:
            for index in range(3):
                run = f"{mode}-{difficulty}-{index}"
                policy = {"run_id": run, "mode": mode, "difficulty": difficulty}
                reports.append(self.reviewed(solo_run(mode, difficulty), policy, run))
        hidden = self.reviewed(
            independent_extra_boss_run(5),
            {"run_id": "hidden-1", "mode": "independent", "difficulty": 5},
            "first-hidden-execution",
        )
        self.assertFalse(replay_progress([hidden])["hidden_core_victory_verified"])
        self.assertFalse(replay_progress(reports)["hidden_core_victory_verified"])
        self.assertTrue(
            replay_progress([*reports, hidden])["hidden_core_victory_verified"]
        )
        incomplete = {**hidden, "hidden_core_evidence": None}
        self.assertFalse(
            replay_progress([*reports, incomplete])["hidden_core_victory_verified"]
        )

    def test_native_round_task_hud_preserves_scope_review_and_order_requirements(self):
        raw = solo_run()
        raw[4] = native_battle_sample()
        records = scope_run(raw, self.policy)
        report = audit_run(records, self.policy, {})
        self.assertTrue(report["no_known_intervention_candidate"])
        self.assertFalse(report["qualified_win"])
        self.assertEqual(
            report["combat_evidence"],
            [
                {
                    "event_index": 5,
                    "source": "native_round_task_hud",
                    "round": 1,
                    "enemies_resolved": 2,
                    "enemies_total": 3,
                }
            ],
        )
        self.assertTrue(self.reviewed(raw)["qualified_win"])
        for key in ("run_id", "run_execution_id", "host_session_id", "taskid"):
            records = scope_run(raw, self.policy)
            records[5]["details"][key] = 2 if key == "taskid" else "other"
            report = audit_run(records, self.policy, review_for(records, self.policy))
            self.assertFalse(report["native_full_run_candidate"], key)
            self.assertFalse(report["combat_evidence"], key)
        for position in (1, 5, 6):
            reordered = copy.deepcopy(raw)
            reordered[4], reordered[position] = reordered[position], reordered[4]
            self.assertFalse(self.reviewed(reordered)["qualified_win"], position)

    def test_native_round_task_hud_rejects_unknown_and_malformed_counters(self):
        for field, value in [
            ("round", 0),
            ("round", 21),
            ("round", True),
            ("round", "1"),
            ("enemies_total_known", False),
            ("enemies_resolved_known", False),
            ("enemies_total_known", 1),
            ("enemies_resolved_known", 1),
            ("enemies_total", 0),
            ("enemies_total", 1000),
            ("enemies_total", "3"),
            ("enemies_total", 3.0),
            ("enemies_resolved", -1),
            ("enemies_resolved", 4),
            ("enemies_resolved", True),
            ("enemies_resolved", 2.0),
            ("contradictory", True),
        ]:
            with self.subTest(field=field, value=value):
                raw = solo_run()
                raw[4] = native_battle_sample()
                raw[4]["details"]["details"][field] = value
                self.assertFalse(self.reviewed(raw)["native_full_run_candidate"])
        for missing in (
            ("enemies_total_known",),
            ("enemies_resolved_known",),
            ("enemies_total_known", "enemies_resolved_known"),
        ):
            raw = solo_run()
            raw[4] = native_battle_sample()
            for key in missing:
                del raw[4]["details"]["details"][key]
            self.assertFalse(self.reviewed(raw)["qualified_win"], missing)

    def test_input_and_start_receipts_or_wrong_plugin_are_not_combat(self):
        for key, value in [
            ("class", "asst::spa::ObserverTaskPlugin"),
            ("class", ""),
            ("taskchain", "OtherTask"),
            ("what", "SPARoundStarted"),
            ("what", "SPASimulationStartIssued"),
            ("what", "SPATransactionCommitted"),
        ]:
            raw = solo_run()
            raw[4] = native_battle_sample()
            raw[4]["details"][key] = value
            self.assertFalse(
                self.reviewed(raw)["native_full_run_candidate"], (key, value)
            )

    def test_joint_native_round_task_hud_still_requires_personal_result(self):
        policy = {**self.policy, "mode": "joint", "difficulty": 2}
        raw = full_run()
        raw[4] = native_battle_sample()
        self.assertTrue(self.reviewed(raw, policy)["qualified_win"])
        raw.pop(5)
        self.assertFalse(self.reviewed(raw, policy)["qualified_win"])

    def test_settlement_only_partial_rounds_and_misordered_trace_fail(self):
        self.assertFalse(self.reviewed(solo_run()[5:])["qualified_win"])
        for field, value in [
            ("passed_rounds", 8),
            ("passed_rounds_known", False),
            ("result_difficulty_name", "险境模拟"),
            ("result_interrupted", True),
        ]:
            events = solo_run()
            for i in (5, 7):
                events[i]["details"]["details"][field] = value
            self.assertFalse(self.reviewed(events)["qualified_win"])
        events = solo_run()
        events[1], events[4] = events[4], events[1]
        self.assertFalse(self.reviewed(events)["qualified_win"])

    def test_solo_difficulty_must_be_observed_not_requested_only(self):
        for difficulty in (1, 20):
            policy = {**self.policy, "mode": "independent", "difficulty": difficulty}
            self.assertTrue(
                self.reviewed(solo_run("independent", difficulty), policy)[
                    "qualified_win"
                ]
            )
            self.assertFalse(
                self.reviewed(
                    solo_run("independent", difficulty), {**policy, "difficulty": 2}
                )["qualified_win"]
            )

    def test_known_assistance_overrides_clean_review(self):
        policy = {**self.policy, "assisted_run": True}
        self.assertFalse(self.reviewed(policy=policy)["native_full_run_candidate"])
        for what, details in [
            ("SPAManualIntervention", {"manual_takeover": True}),
            ("SPAExternalInput", {"input_source": "external_adb"}),
            ("SPAHostRecovery", {"host_recoveries": 1}),
            ("SPARunState", {"assisted_run": True}),
        ]:
            raw = solo_run()
            raw.insert(5, event(what, details))
            report = self.reviewed(raw)
            self.assertFalse(report["native_full_run_candidate"])
            self.assertFalse(report["qualified_win"])
            self.assertEqual(report["outcome"], "known_intervention")

    def test_batch_run_host_and_task_identity_cannot_be_mixed(self):
        for key, value in [
            ("run_id", "second"),
            ("run_execution_id", "second"),
            ("host_session_id", "second"),
            ("batch_run_id", "second"),
            ("taskid", 2),
        ]:
            events = scope_run(solo_run(), self.policy)
            events[6]["details"][key] = value
            report = audit_run(events, self.policy, review_for(events, self.policy))
            self.assertFalse(report["native_full_run_candidate"], key)
            self.assertEqual(report["outcome"], "evidence_incomplete")

    def test_real_assistant_outer_completion_does_not_invalidate_native_scope(self):
        records = scope_run(solo_run(), self.policy)
        outer = {"taskchain": "StrongholdProtocol", "taskid": 1, "uuid": "device"}
        records.insert(0, {"message": 10001, "details": dict(outer)})
        records.append({"message": 10002, "details": dict(outer)})
        records.append({"message": 3, "details": {**outer, "finished_tasks": [1]}})
        report = audit_run(records, self.policy, review_for(records, self.policy))
        self.assertTrue(report["qualified_win"])

    def test_same_task_outer_error_or_stop_rejects_but_other_task_status_is_separate(
        self,
    ):
        for message in (10000, 10004):
            for task_id in (1, 2):
                records = scope_run(solo_run(), self.policy)
                records.append(
                    {
                        "message": message,
                        "details": {
                            "taskchain": "StrongholdProtocol",
                            "taskid": task_id,
                            "details": {"error": "StandardException"}
                            if message == 10000
                            else {},
                        },
                    }
                )
                report = audit_run(
                    records, self.policy, review_for(records, self.policy)
                )
                self.assertEqual(report["native_full_run_candidate"], task_id == 2)
                self.assertEqual(
                    report["checks"]["no_outer_execution_failure"], task_id == 2
                )

    def test_unscoped_input_is_not_discarded_as_an_outer_notification(self):
        for message in (3, 10001, 10002):
            records = scope_run(solo_run(), self.policy)
            records.append(
                {
                    "message": message,
                    "details": {
                        "taskchain": "StrongholdProtocol",
                        "taskid": 1,
                        "details": {"task": "Click", "action": "ClickSelf"},
                    },
                }
            )
            self.assertFalse(
                audit_run(records, self.policy, review_for(records, self.policy))[
                    "native_full_run_candidate"
                ]
            )

    def test_only_known_non_input_set_params_preference_can_precede_run_scope(self):
        fallback = event(
            "SPAPrimaryPactFallback",
            {
                "requested_primary_pact": "阿戈尔",
                "fallback": "auto",
                "reason": "Primary faction is banned in this simulation",
            },
        )
        fallback["details"].update(taskid=0, taskchain="StrongholdProtocol")
        records = [fallback] + scope_run(solo_run(), self.policy)
        self.assertTrue(
            audit_run(records, self.policy, review_for(records, self.policy))[
                "qualified_win"
            ]
        )
        for position, changes in [
            (0, {"input_issued": True}),
            (0, {"action": "ClickSelf"}),
            (3, {}),
        ]:
            records = scope_run(solo_run(), self.policy)
            changed = copy.deepcopy(fallback)
            changed["details"]["details"].update(changes)
            records.insert(position, changed)
            self.assertFalse(
                audit_run(records, self.policy, review_for(records, self.policy))[
                    "native_full_run_candidate"
                ]
            )
        records = [event("SPAShopPurchase", {"input_issued": True})] + scope_run(
            solo_run(), self.policy
        )
        self.assertFalse(
            audit_run(records, self.policy, review_for(records, self.policy))[
                "native_full_run_candidate"
            ]
        )

    def test_batch_requires_explicit_child_selection(self):
        first = scope_run(solo_run(), self.policy)
        second_policy = {**self.policy, "run_id": "run-2"}
        second = scope_run(solo_run(), second_policy, "execution-2")
        combined = first + second
        self.assertFalse(
            audit_run(combined, self.policy, {})["native_full_run_candidate"]
        )
        selected = select_execution(combined, "execution-2")
        self.assertTrue(
            audit_run(selected, second_policy, review_for(selected, second_policy))[
                "qualified_win"
            ]
        )

    def test_missing_duplicate_or_failed_native_boundary_is_not_candidate(self):
        original = scope_run(solo_run(), self.policy)
        cases = [original[1:], original[:-1], original[:1] + original]
        failed = copy.deepcopy(original)
        failed[-1]["details"]["details"]["runner_success"] = False
        cases.append(failed)
        canceled = copy.deepcopy(original)
        canceled[-1]["details"]["details"]["stop_requested"] = True
        cases.append(canceled)
        duplicate_receipt = copy.deepcopy(original)
        duplicate_receipt.insert(-1, copy.deepcopy(original[-2]))
        cases.append(duplicate_receipt)
        for events in cases:
            self.assertFalse(
                audit_run(events, self.policy, review_for(events, self.policy))[
                    "native_full_run_candidate"
                ]
            )

    def test_legacy_is_incomplete_not_a_proven_loss_or_candidate(self):
        raw = solo_run()
        review = {
            "run_id": "run-1",
            "review_complete": True,
            "manual_takeover": False,
            "host_recoveries": 0,
        }
        report = audit_run(raw, self.policy, review)
        self.assertFalse(report["native_full_run_candidate"])
        self.assertFalse(report["qualified_win"])
        self.assertTrue(report["attempt_finished"])
        self.assertEqual(report["outcome"], "evidence_incomplete")

    def test_three_consecutive_wins_and_no_duplicate_credit(self):
        reports = [
            self.reviewed(
                policy={**self.policy, "run_id": f"run-{i}"}, execution=f"execution-{i}"
            )
            for i in range(3)
        ]
        self.assertEqual(replay_progress(reports[:2])["next_stage"]["mode"], "quick")
        self.assertEqual(replay_progress(reports)["next_stage"]["difficulty"], 1)
        self.assertEqual(
            replay_progress([reports[0]] * 3)["stages"][0]["consecutive_wins"], 1
        )
        for duplicate in [
            {**reports[1], "events_sha256": reports[0]["events_sha256"]},
            {**reports[1], "run_execution_id": reports[0]["run_execution_id"]},
        ]:
            self.assertEqual(
                replay_progress([reports[0], duplicate])["stages"][0][
                    "consecutive_wins"
                ],
                1,
            )
        unverified = {
            **reports[1],
            "qualified_win": False,
            "outcome": "evidence_incomplete",
        }
        progress = replay_progress([reports[0], unverified, reports[2]])
        self.assertEqual(progress["stages"][0]["consecutive_wins"], 1)
        self.assertEqual(progress["runs"][1]["outcome"], "evidence_incomplete")

    def test_confirmed_failure_is_not_a_win_but_is_a_terminal_attempt(self):
        raw = solo_run()
        for index in (5, 7):
            raw[index]["details"]["details"].update(
                result_completed=False, result_interrupted=True, passed_rounds=4
            )
        report = self.reviewed(raw)
        self.assertEqual(report["outcome"], "verified_failure")
        self.assertTrue(report["attempt_finished"])
        self.assertFalse(report["qualified_win"])

    def test_pending_does_not_break_streak_or_consume_run_id(self):
        report = self.reviewed()
        pending = {
            **report,
            "attempt_finished": False,
            "execution_finished": False,
            "qualified_win": False,
        }
        progress = replay_progress([pending, report])
        self.assertEqual(progress["stages"][0]["consecutive_wins"], 1)
        self.assertEqual(len(progress["ignored"]), 1)

    def test_ended_execution_without_settlement_breaks_continuity_not_a_loss(self):
        first = self.reviewed()
        policy = {**self.policy, "run_id": "unknown-run"}
        events = scope_run(
            solo_run()[:5] + [solo_run()[-1]], policy, "unknown-execution"
        )
        events[-1]["details"]["details"]["runner_success"] = False
        unknown = audit_run(events, policy, review_for(events, policy))
        self.assertFalse(unknown["attempt_finished"])
        self.assertTrue(unknown["execution_finished"])
        self.assertEqual(unknown["outcome"], "evidence_incomplete")
        last = self.reviewed(
            policy={**self.policy, "run_id": "last-run"}, execution="last-execution"
        )
        progress = replay_progress([first, unknown, last])
        self.assertEqual(progress["stages"][0]["consecutive_wins"], 1)
        self.assertEqual(progress["runs"][1]["outcome"], "evidence_incomplete")

    def test_cannot_jump_to_joint_and_progression_is_not_badge_completion(self):
        self.assertEqual(len(stage_sequence()), 25)
        report = self.reviewed(
            full_run(), {**self.policy, "mode": "joint", "difficulty": 2}
        )
        self.assertTrue(report["qualified_win"])
        progress = replay_progress([report])
        self.assertEqual(progress["stage_index"], 0)
        self.assertFalse(progress["all_strategy_badges_verified"])

    def test_full_badge_scan_verifies_each_catalogue_entry_without_counting_a_battle(
        self,
    ):
        policy = {
            "run_id": "catalogue-review",
            "mode": "independent",
            "difficulty": 20,
            "schedule_strategy_badges": True,
        }

        def received(what, details, name, message=20003):
            callback = event(what, details)
            callback["message"] = message
            callback["details"].update(
                taskchain="StrongholdProtocol", **{"class": name}
            )
            return callback

        scanner = "asst::spa::StrategyReportTaskPlugin"
        observer = "asst::spa::ObserverTaskPlugin"
        rows, raw = [], [received("", {}, scanner, 20001)]
        for index, (band_id, (ordinary, master)) in enumerate(
            strategy_badge_requirements().items()
        ):
            wins = ordinary if index % 2 else master
            verified = {
                "band_id": band_id,
                "wins": wins,
                "wins_known": True,
                "badge_verified": True,
            }
            rows.append({**verified, "unlocked_verified": True})
            raw.append(
                received(
                    "SPAObservation",
                    {
                        **verified,
                        "page": "strategy_report",
                        "contradictory": False,
                        "strategy_report_identity_verified": True,
                        "strategy_progress_text_verified": True,
                        "strategy_report_evidence_saved": True,
                        "strategy_badge_tier": "silver" if wins == master else "orange",
                    },
                    observer,
                )
            )
        raw += [
            received(
                "SPAStrategyReportScan",
                {
                    "target_band_id": "",
                    "identified_strategies": len(rows),
                    "expected_strategies": len(rows),
                    "strategies": rows,
                    "all_strategy_identities_observed": True,
                    "all_strategy_badges_verified": True,
                },
                scanner,
            ),
            received("", {}, scanner, 20002),
            received(
                "SPAObservation", {"page": "lobby", "contradictory": False}, observer
            ),
            received(
                "SPAStrategyBadgeSchedule",
                {
                    "report_and_lobby_verified": True,
                    "all_strategy_badges_verified": True,
                    "simulation_start_issued": False,
                    "requested_strategy": "",
                },
                "asst::StrongholdProtocolTask",
            ),
            event("unused-by-scope", {}),
        ]
        report = self.reviewed(raw, policy, execution="catalogue-execution")
        self.assertFalse(report["qualified_win"])
        verified = report["strategy_catalogue_badge_evidence"]["badges"]
        self.assertEqual(set(verified), set(strategy_badge_requirements()))
        battle = self.reviewed()
        state = replay_progress([battle, report])
        self.assertTrue(state["all_strategy_badges_verified"])
        self.assertEqual(set(state["verified_strategy_badges"]), set(verified))
        self.assertEqual(state["stage_index"], 0)
        self.assertEqual(state["stages"][0]["consecutive_wins"], 1)

        for changed_event, patch in (
            (1, {"strategy_report_evidence_saved": False}),
            (1, {"strategy_badge_tier": "orange"}),
            (2, {"band_id": rows[0]["band_id"]}),
            (-5, {"target_band_id": rows[0]["band_id"]}),
            (-3, {"page": "strategy_report"}),
            (-2, {"report_and_lobby_verified": False}),
        ):
            with self.subTest(change=patch):
                changed = copy.deepcopy(raw)
                changed[changed_event]["details"]["details"].update(patch)
                report = self.reviewed(changed, policy)
                self.assertIsNone(report["strategy_catalogue_badge_evidence"])
                self.assertFalse(
                    replay_progress([report])["all_strategy_badges_verified"]
                )
        unreviewed = audit_run(scope_run(raw, policy), policy, {})
        self.assertFalse(replay_progress([unreviewed])["all_strategy_badges_verified"])

        targeted_prefix = [
            received("", {}, scanner, 20001),
            received(
                "SPAStrategyReportScan",
                {"target_band_id": rows[0]["band_id"]},
                scanner,
            ),
            received("", {}, scanner, 20002),
            received("SPAObservation", {"page": "lobby", "contradictory": False}, observer),
        ]
        recovered = self.reviewed([*targeted_prefix, *raw], policy)
        self.assertEqual(
            set(recovered["strategy_catalogue_badge_evidence"]["badges"]), set(verified)
        )
        failed_prefix = [
            received("", {}, scanner, 20001),
            received("", {}, scanner, 20000),
        ]
        failed_target = self.reviewed([*failed_prefix, *raw], policy)
        self.assertEqual(
            set(failed_target["strategy_catalogue_badge_evidence"]["badges"]), set(verified)
        )
        no_completed_fallback = self.reviewed(targeted_prefix + [raw[-2]], policy)
        self.assertIsNone(no_completed_fallback["strategy_catalogue_badge_evidence"])

        # Existing marks seen before a real game count individually. One
        # missing mark cannot turn this opening report into a full audit.
        partial = copy.deepcopy(raw[:-1])
        partial[1]["details"]["details"]["strategy_report_evidence_saved"] = False
        partial[-4]["details"]["details"]["all_strategy_badges_verified"] = False
        partial[-1]["details"]["details"].update(
            all_strategy_badges_verified=False, requested_strategy="band_weakness"
        )
        partial_policy = {**policy, "difficulty": 1, "run_id": "partial-verified"}
        initial_lobby = received(
            "SPAObservation", {"page": "lobby", "contradictory": False}, observer
        )
        partial_run = self.reviewed(
            [initial_lobby, *partial, *solo_run("independent", 1)],
            partial_policy,
            "partial-execution",
        )
        self.assertTrue(partial_run["qualified_win"], partial_run["checks"])
        visible = partial_run["opening_strategy_badge_evidence"]["badges"]
        self.assertEqual(set(visible), set(verified) - {rows[0]["band_id"]})
        quick = [
            self.reviewed(
                policy={**self.policy, "run_id": f"partial-quick-{attempt}"},
                execution=f"partial-quick-{attempt}",
            )
            for attempt in range(3)
        ]
        progress = replay_progress([*quick, partial_run])
        self.assertEqual(set(progress["verified_strategy_badges"]), set(visible))
        self.assertFalse(progress["all_strategy_badges_verified"])

        # The final batch child opens only the report. None of its receipts
        # counts as a game, but the fully verified catalogue ends the batch.
        child_policy = {**policy, "run_id": "badge-batch-1"}
        child = scope_run(raw, child_policy, "catalogue-child")
        for record in child:
            record["details"].update(taskchain="StrongholdProtocol", taskid=1)
        batch_stopped = received(
            "SPABatchStrategyCatalogueCompleted",
            {"settled_runs": 0, "requested_runs": 3, "report_run_id": "badge-batch-1"},
            "asst::StrongholdProtocolTask",
        )
        batch_stopped["details"]["taskid"] = 1
        root_completed = received("", {}, "asst::StrongholdProtocolTask", 10002)
        root_completed["details"]["taskid"] = 1
        batch = child + [batch_stopped, root_completed]
        host = audit_host_completion(batch, 3, schedule_strategy_badges=True)
        self.assertTrue(host["batch_complete"])
        self.assertTrue(host["strategy_catalogue_complete"])
        self.assertEqual(host["native_reported_settled_runs"], 0)
        self.assertEqual(host["settlement_observations"], [])
        self.assertFalse(audit_host_completion(batch, 3)["batch_complete"])
        self.assertFalse(
            audit_host_completion(batch[:-1], 3, schedule_strategy_badges=True)[
                "batch_complete"
            ]
        )

        altered = copy.deepcopy(batch)
        altered[2]["details"]["details"]["strategy_report_evidence_saved"] = False
        host = audit_host_completion(altered, 3, schedule_strategy_badges=True)
        self.assertFalse(host["batch_complete"])
        self.assertIn(
            "strategy_catalogue_completion_unverified", host["failure_reasons"]
        )

    def test_missing_checks_or_old_schema_cannot_earn_credit(self):
        report = self.reviewed()
        for changed in [{"checks": {"valid_stage": True}}, {"audit_schema_version": 1}]:
            self.assertEqual(
                replay_progress([{**report, **changed}])["stages"][0][
                    "consecutive_wins"
                ],
                0,
            )

    def test_event_and_policy_edits_invalidate_existing_review(self):
        events = scope_run(solo_run(), self.policy)
        review = review_for(events, self.policy)
        changed = copy.deepcopy(events)
        changed[1]["details"]["details"]["additional_observation"] = True
        self.assertFalse(audit_run(changed, self.policy, review)["qualified_win"])
        self.assertFalse(
            audit_run(events, {**self.policy, "allow_refresh": False}, review)[
                "qualified_win"
            ]
        )
        reformatted = json.loads(json.dumps(events, sort_keys=True, indent=4))
        self.assertEqual(evidence_hash(reformatted), evidence_hash(events))

    def test_replay_reloads_sources_not_saved_success_flags(self):
        events = scope_run(solo_run(), self.policy)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "input-policy.json").write_text(
                json.dumps(self.policy), encoding="utf-8"
            )
            event_file = directory / "events.jsonl"
            event_file.write_text(
                "\n".join(json.dumps(item) for item in events), encoding="utf-8"
            )
            review_file = directory / "provenance.json"
            review_file.write_text(
                json.dumps(review_for(events, self.policy)), encoding="utf-8"
            )
            report = audit_directory(directory, review_file)
            report_file = directory / "report.json"
            report_file.write_text(json.dumps(report), encoding="utf-8")
            self.assertTrue(reload_report(report_file)["qualified_win"])
            review_file.write_text("{}", encoding="utf-8")
            self.assertFalse(reload_report(report_file)["qualified_win"])
            event_file.unlink()
            self.assertEqual(
                reload_report(report_file)["outcome"], "evidence_incomplete"
            )

    def test_directory_batch_audit_binds_the_selected_child_policy(self):
        parent = {**self.policy, "run_id": "batch", "run_count": 2}
        child = {**parent, "run_id": "batch-2", "run_count": 1}
        events = scope_run(solo_run(), {**child, "run_id": "batch-1"}, "execution-1")
        events += scope_run(solo_run(), child, "execution-2")
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "input-policy.json").write_text(
                json.dumps(parent), encoding="utf-8"
            )
            (directory / "events.jsonl").write_text(
                "\n".join(json.dumps(item) for item in events), encoding="utf-8"
            )
            review_path = directory / "review.json"
            selected = select_execution(events, "execution-2")
            effective = {**child, "batch_parent_run_id": "batch"}
            review_path.write_text(
                json.dumps(review_for(selected, effective)), encoding="utf-8"
            )
            report = audit_directory(directory, review_path, "execution-2")
            self.assertTrue(report["qualified_win"])
            self.assertEqual(report["run_id"], "batch-2")
            self.assertFalse(
                audit_directory(directory, review_path)["native_full_run_candidate"]
            )


if __name__ == "__main__":
    unittest.main()
