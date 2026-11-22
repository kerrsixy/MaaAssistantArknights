"""Evidence-gated three-win progression; does not start or abandon a game."""

import argparse
import hashlib
import json
import math
from functools import lru_cache
from pathlib import Path

from spa_checkpoint import verified_lifecycle_receipt
from spa_joint_acceptance import audit_joint_run, combat_evidence

PROVENANCE_CHECKS = {
    "provenance_reviewed",
    "same_run_provenance",
    "no_host_recovery",
    "no_manual_takeover",
    "review_bound_to_evidence",
}
COMMON_CHECKS = PROVENANCE_CHECKS | {
    "valid_stage",
    "no_recovery_request",
    "no_abandon_input",
    "single_execution_scope",
    "execution_boundaries_verified",
    "no_known_intervention",
    "no_outer_execution_failure",
}
SOLO_CHECKS = {
    "started_at_lobby",
    "ordered_native_run",
    "matching_completed_receipt",
    "returned_to_lobby",
}

SCOPE_KEYS = ("run_id", "run_execution_id", "host_session_id")
EXECUTION_STARTED = "SPARunExecutionStarted"
EXECUTION_FINISHED = "SPARunExecutionFinished"
STRATEGY_REPORT_PLUGIN = "asst::spa::StrategyReportTaskPlugin"
STRATEGY_OBSERVER = "asst::spa::ObserverTaskPlugin"


@lru_cache(maxsize=1)
def strategy_badge_requirements():
    path = (
        Path(__file__).resolve().parents[2]
        / "resource"
        / "config"
        / "MiniGame"
        / "SPACombatData.json"
    )
    model = json.loads(path.read_text(encoding="utf-8-sig"))
    rows = model["strategies"]
    requirements = {
        row["bandId"]: (row["victorCount"], row["masterVictorCount"]) for row in rows
    }
    if (
        model["season"] != "act3autochess/V3_1"
        or len(rows) != 61
        or len(requirements) != len(rows)
    ):
        raise ValueError("Expected the complete Sequence I strategy catalogue")
    return requirements


def evidence_hash(value):
    """Hash parsed evidence, so whitespace/key-order changes cannot earn credit twice."""
    encoded = json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def select_execution(events, execution_id):
    """Extract an explicitly requested child; never infer a successful child of a batch."""
    return [
        event
        for event in events
        if event.get("details", {}).get("run_execution_id") == execution_id
    ]


def outer_notification(event):
    """Recognize only the non-input shells emitted by Assistant::working_proc."""
    envelope = event.get("details", {})
    return (
        event.get("message") in (3, 10000, 10001, 10002, 10004)
        and not envelope.get("what")
        and not envelope.get("run_execution_id")
        and envelope.keys()
        <= {"taskchain", "taskid", "finished_tasks", "uuid", "details"}
        and envelope.get("details", {}).keys() <= {"error"}
    )


def pre_execution_preference(event, index, first_start):
    """set_params can report this pure preference fallback before run() owns a scope."""
    envelope = event.get("details", {})
    details = envelope.get("details", {})
    return (
        first_start is not None
        and index < first_start
        and not envelope.get("run_execution_id")
        and envelope.get("what") == "SPAPrimaryPactFallback"
        and envelope.keys()
        <= {
            "taskchain",
            "taskid",
            "class",
            "subtask",
            "details",
            "what",
            "uuid",
            "batch_run_id",
        }
        and details.keys()
        <= {"requested_primary_pact", "fallback", "reason", "input_issued"}
        and isinstance(details.get("requested_primary_pact"), str)
        and bool(details["requested_primary_pact"])
        and details.get("fallback") == "auto"
        and details.get("reason") == "Primary faction is banned in this simulation"
        and details.get("input_issued", False) is False
    )


def execution_scope(events, policy):
    scoped = []
    first_start = next(
        (
            index
            for index, event in enumerate(events)
            if event.get("details", {}).get("what") == EXECUTION_STARTED
        ),
        None,
    )
    for index, event in enumerate(events):
        envelope = event.get("details", {})
        what = envelope.get("what", "")
        # Assistant adds unscoped chain status and AllTasksCompleted after the
        # native run has restored its callback. Those are not child inputs.
        if outer_notification(event) or pre_execution_preference(
            event, index, first_start
        ):
            continue
        if (
            str(what).startswith("SPA")
            or envelope.get("run_execution_id")
            or envelope.get("taskchain") == "StrongholdProtocol"
        ):
            scoped.append((index, envelope))
    values = {
        key: {entry.get(key) for _, entry in scoped if isinstance(entry.get(key), str)}
        for key in SCOPE_KEYS
    }
    identity = {
        key: next(iter(items)) if len(items) == 1 else None
        for key, items in values.items()
    }
    task_ids = {entry.get("taskid") for _, entry in scoped}
    complete = bool(scoped) and all(
        len(values[key]) == 1
        and bool(identity[key])
        and all(entry.get(key) == identity[key] for _, entry in scoped)
        for key in SCOPE_KEYS
    )
    complete = (
        complete
        and identity["run_id"] == policy.get("run_id")
        and len(task_ids) == 1
        and all(type(task_id) is int and task_id > 0 for task_id in task_ids)
    )
    for _, entry in scoped:
        details = entry.get("details", {})
        # Older batch callbacks and transaction receipts also carry identities.
        # Conflicting aliases must not be silently replaced by envelope fields.
        if entry.get("batch_run_id") not in (None, identity["run_id"]):
            complete = False
        if details.get("run_id") not in (None, identity["run_id"]):
            complete = False
        if details.get("task_id") not in (None, entry.get("taskid")):
            complete = False
    starts = [(i, e) for i, e in scoped if e.get("what") == EXECUTION_STARTED]
    finishes = [(i, e) for i, e in scoped if e.get("what") == EXECUTION_FINISHED]
    boundaries = complete and len(starts) == len(finishes) == 1
    if boundaries:
        start_index, start = starts[0]
        finish_index, finish = finishes[0]
        boundaries = start_index < finish_index and all(
            start_index <= i <= finish_index for i, _ in scoped
        )
        for boundary in (start, finish):
            data = boundary.get("details", {})
            boundaries = (
                boundaries
                and data.get("mode") == policy.get("mode")
                and (
                    policy.get("mode") == "quick"
                    or data.get("difficulty") == policy.get("difficulty")
                )
            )
        boundaries = (
            boundaries
            and finish.get("details", {}).get("runner_success") is True
            and finish.get("details", {}).get("stop_requested") is False
        )
    return {
        **identity,
        "task_id": next(iter(task_ids)) if len(task_ids) == 1 else None,
        "complete": bool(complete),
        "boundaries_verified": bool(boundaries),
        "finish": finishes[0][1] if len(finishes) == 1 else None,
        "outer_execution_failures": [
            event.get("message")
            for event in events
            if outer_notification(event)
            and event.get("message") in (10000, 10004)
            and event.get("details", {}).get("taskchain") == "StrongholdProtocol"
            and event.get("details", {}).get("taskid") in task_ids
        ],
    }


def intervention_evidence(events, policy, provenance):
    """Known assistance overrides a review claiming no assistance; absence is not proof."""
    reasons = set()

    def inspect(value, source):
        if not isinstance(value, dict):
            return
        for key in (
            "assisted_run",
            "manual_takeover",
            "external_input_issued",
            "host_recovery",
        ):
            if key in value and value[key] is not False and value[key] is not None:
                reasons.add(f"{source}:{key}")
        if type(value.get("host_recoveries")) is int and value["host_recoveries"] > 0:
            reasons.add(f"{source}:host_recoveries")
        if value.get("input_source") in ("manual", "user", "external", "external_adb"):
            reasons.add(f"{source}:input_source")

    inspect(policy, "policy")
    inspect(provenance, "review")
    for event in events:
        envelope = event.get("details", {})
        what = envelope.get("what", "")
        if what in ("SPAManualIntervention", "SPAExternalInput", "SPAHostRecovery"):
            reasons.add(what)
        inspect(event, "event")
        inspect(envelope, str(what))
        inspect(envelope.get("details"), str(what))
    return sorted(reasons)


def receipt_events(events, scope):
    """Adapt a verified per-run finish to the older receipt reader's completion marker."""
    result = list(events)
    if scope["boundaries_verified"]:
        result.append(
            {
                "message": 10002,
                "details": {
                    "taskchain": "StrongholdProtocol",
                    "taskid": scope["task_id"],
                },
            }
        )
    return result


def stage_sequence():
    return (
        [("quick", None)]
        + [("independent", n) for n in range(1, 21)]
        + [("joint", n) for n in range(1, 5)]
    )


def fresh_solo_lobby_index(events, observations):
    """Accept a lobby at launch or one reached through verified front-end navigation."""
    navigation_pages = {
        "main_home",
        "navigation_menu",
        "terminal_overview",
        "protocol_entry",
    }
    entry_tasks = {"OpenLobby", "LifecycleEnterProtocol", "LifecycleTerminalBanner"}
    navigation_tasks = entry_tasks | {
        "LifecycleOpenMenu",
        "LifecycleHomeTerminal",
        "LifecycleMenuTerminal",
        "LifecycleMenuTerminalText",
        "LifecycleTerminalTab",
    }

    def completed_click(event, tasks):
        envelope = event.get("details", {})
        detail = envelope.get("details", {})
        return (
            event.get("message") == 20002
            and envelope.get("class") == "asst::ProcessTask"
            and detail.get("task") in tasks
            and detail.get("action") == "ClickSelf"
        )

    navigation_page_verified = False
    for offset, (index, observation) in enumerate(observations):
        page = observation.get("page")
        if page == "lobby" and not observation.get("contradictory", False):
            if offset == 0:
                return index
            break
        if (
            page not in navigation_pages | {"unknown"}
            or observation.get("setup_stage")
            or observation.get("round_known")
            or observation.get("contradictory", False)
        ):
            return None
        end = (
            observations[offset + 1][0]
            if offset + 1 < len(observations)
            else len(events)
        )
        steps = events[index + 1 : end]
        navigation_verified = False
        unknown_dispatch = False
        for event in steps:
            envelope = event.get("details", {})
            detail = envelope.get("details", {})
            if (
                event.get("message") == 20003
                and envelope.get("what") == "SPALifecycleTransition"
                and envelope.get("class") == "asst::spa::LifecycleTaskPlugin"
                and detail.get("next") == "MiniGame@SPA@LifecycleFindLobby"
                and detail.get("run_abandoned") is not True
            ):
                if detail.get("page") in navigation_pages:
                    navigation_page_verified = navigation_verified = True
                elif detail.get("page") == "unknown":
                    unknown_dispatch = True
            if unknown_dispatch and completed_click(event, navigation_tasks):
                navigation_verified = True
        if not navigation_verified:
            return None
    else:
        return None

    if navigation_page_verified and any(
        completed_click(event, entry_tasks)
        for event in events[observations[offset - 1][0] + 1 : index]
    ):
        return index
    return None


def hidden_core_completion_evidence(events, combat_samples, results, difficulty):
    """Bind the extra boss round to its native preparation, combat and result frames."""
    if any(result.get("passed_rounds") != 15 for _, result in results):
        return None
    saved_results = []
    for index, result in results:
        envelope = events[index]["details"]
        if (
            events[index].get("message") != 20003
            or envelope.get("taskchain") != "StrongholdProtocol"
            or result.get("passed_rounds") != 15
            or result.get("result_frame_saved") is not True
        ):
            continue
        if (
            envelope.get("class") == "asst::spa::ObserverTaskPlugin"
            and envelope.get("what") == "SPAObservation"
        ) or (
            envelope.get("class") == "asst::spa::RoundTaskPlugin"
            and envelope.get("what") == "SPARoundResultObserved"
            and result.get("round") == 15
        ):
            saved_results.append(index)
    if not saved_results:
        return None

    label = f"规程等级{difficulty}"
    preparation, start = None, None
    for index, event in enumerate(events[: saved_results[-1]]):
        envelope = event.get("details", {})
        detail = envelope.get("details", {})
        if (
            event.get("message") != 20003
            or envelope.get("taskchain") != "StrongholdProtocol"
        ):
            continue
        if (
            envelope.get("class") == "asst::spa::ObserverTaskPlugin"
            and envelope.get("what") == "SPAObservation"
            and detail.get("page") == "preparation"
            and detail.get("contradictory") is False
            and detail.get("round_known") is True
            and detail.get("round") == 15
            and detail.get("bonus_round") is True
            and detail.get("round_source") == "independent_hidden_core_phase"
        ):
            difficulty_titles, has_badge = set(), False
            for token in detail.get("texts", []):
                score, x, y = token.get("score"), token.get("x"), token.get("y")
                text = token.get("text")
                if (
                    type(score) not in (int, float)
                    or not math.isfinite(score)
                    or type(x) is not int
                    or type(y) is not int
                    or not isinstance(text, str)
                ):
                    continue
                if score >= 0.85 and 90 <= x < 250 and 0 <= y < 65:
                    title = text.replace(" ", "")
                    if title.startswith("规程等级"):
                        difficulty_titles.add(title)
                has_badge |= (
                    score >= 0.90
                    and 480 <= x < 530
                    and 0 <= y < 60
                    and text == "??"
                    and token.get("source") == "hidden_round_badge"
                )
            if difficulty_titles == {label} and has_badge:
                preparation = index
        elif (
            preparation is not None
            and envelope.get("class") == "asst::spa::RoundTaskPlugin"
            and envelope.get("what") == "SPARoundStarted"
            and detail.get("round") == 15
            and detail.get("hidden_core_preparation_verified") is True
        ):
            start = preparation, index
    if start is None:
        return None

    for sample in combat_samples:
        index = sample["event_index"]
        if sample.get("round") != 15 or not start[1] < index < saved_results[-1]:
            continue
        detail = events[index]["details"]["details"]
        titles = detail.get("regulation_hud_titles")
        if (
            detail.get("battle_frame_saved") is True
            and isinstance(titles, list)
            and titles
            and all(title == label for title in titles)
        ):
            return {
                "preparation_event_index": start[0],
                "battle_start_event_index": start[1],
                "battle_sample_event_index": index,
                "result_event_index": saved_results[-1],
            }
    return None


def verified_strategy_badge(detail, ordinary=3, master=6):
    wins = detail.get("wins")
    if (
        type(wins) is int
        and ordinary <= wins <= master
        and detail.get("page") == "strategy_report"
        and detail.get("contradictory") is False
        and all(
            detail.get(flag) is True
            for flag in (
                "wins_known",
                "badge_verified",
                "strategy_report_identity_verified",
                "strategy_progress_text_verified",
                "strategy_report_evidence_saved",
            )
        )
    ):
        tier = "silver" if wins == master else "orange"
        if detail.get("strategy_badge_tier") == tier:
            return wins, tier
    return None


def strategy_badge_completion_evidence(events, strategy_indices):
    """Bind the selected strategy to a fresh native badge report after settlement."""
    selected_ids = {
        events[index]["details"]["details"]["strategy_id"] for index in strategy_indices
    }
    if len(selected_ids) != 1:
        return None
    strategy = next(iter(selected_ids))
    settled = False
    saved_reports = {}
    for index, event in enumerate(events):
        envelope = event.get("details", {})
        if (
            event.get("message") != 20003
            or envelope.get("taskchain") != "StrongholdProtocol"
        ):
            continue
        name, detail = envelope.get("what"), envelope.get("details", {})
        source = envelope.get("class")
        if (
            name == "SPALifecycleCompleted"
            and detail.get("return_home_verified") is True
        ):
            settled = True
        if not settled:
            continue
        if (
            name == "SPAObservation"
            and source == STRATEGY_OBSERVER
            and detail.get("band_id") == strategy
        ):
            if badge := verified_strategy_badge(detail):
                saved_reports[badge[0]] = index
        if (
            name != "SPAStrategyBadgeScheduleSettled"
            or source != "asst::StrongholdProtocolTask"
        ):
            continue
        before, after = (
            detail.get("report_wins_before"),
            detail.get("report_wins_after"),
        )
        if (
            detail.get("requested_strategy")
            == detail.get("actual_strategy")
            == strategy
            and detail.get("same_strategy") is True
            and detail.get("settlement_victory_verified") is True
            and detail.get("report_win_delta_verified") is True
            and detail.get("three_win_badge_verified") is True
            and type(before) is int
            and type(after) is int
            and before + 1 == after
            and after in saved_reports
        ):
            return {
                "strategy_id": strategy,
                "badge_tier": "silver" if after == 6 else "orange",
                "report_wins_before": before,
                "report_wins_after": after,
                "report_event_index": saved_reports[after],
                "confirmation_event_index": index,
            }
    return None


def strategy_catalogue_badge_evidence(events):
    """Check a report-only scheduler execution against all in-game card receipts."""
    scan_start, scan_report, scan_completed, lobby_resumed = None, None, False, False
    observed = {}
    for index, event in enumerate(events):
        envelope = event.get("details", {})
        if envelope.get("taskchain") != "StrongholdProtocol":
            continue
        source, name, message = (
            envelope.get("class"),
            envelope.get("what"),
            event.get("message"),
        )
        detail = envelope.get("details", {})
        if source == STRATEGY_REPORT_PLUGIN and message == 20001:
            if scan_start is not None:
                return None
            scan_start = index
        elif (
            source == STRATEGY_REPORT_PLUGIN
            and message == 20003
            and name == "SPAStrategyReportScan"
        ):
            if scan_start is None or scan_report is not None or scan_completed:
                return None
            scan_report = (index, detail)
        elif source == STRATEGY_REPORT_PLUGIN and message == 20002:
            if scan_report is None or scan_completed:
                return None
            scan_completed = True
        elif (
            source == STRATEGY_OBSERVER
            and message == 20003
            and name == "SPAObservation"
        ):
            if (
                scan_start is not None
                and scan_report is None
                and detail.get("page") == "strategy_report"
            ):
                identity = detail.get("band_id")
                if isinstance(identity, str) and identity:
                    observed.setdefault(identity, []).append((index, detail))
            if (
                scan_completed
                and detail.get("page") == "lobby"
                and detail.get("contradictory") is False
            ):
                lobby_resumed = True
        elif (
            source == "asst::StrongholdProtocolTask"
            and message == 20003
            and name == "SPAStrategyBadgeSchedule"
        ):
            if (
                lobby_resumed
                and detail.get("report_and_lobby_verified") is True
                and detail.get("all_strategy_badges_verified") is True
                and detail.get("simulation_start_issued") is False
                and detail.get("requested_strategy") == ""
            ):
                report_index, report = scan_report
                return verify_strategy_catalogue_report(
                    report, observed, report_index, index
                )
    return None


def verify_strategy_catalogue_report(
    report, observations, report_index, schedule_index
):
    required = strategy_badge_requirements()
    rows = report.get("strategies")
    if (
        report.get("target_band_id") != ""
        or report.get("all_strategy_identities_observed") is not True
        or report.get("all_strategy_badges_verified") is not True
        or report.get("identified_strategies") != len(required)
        or report.get("expected_strategies") != len(required)
        or not isinstance(rows, list)
        or len(rows) != len(required)
    ):
        return None
    badges = {}
    for row in rows:
        if not isinstance(row, dict):
            return None
        identity = row.get("band_id")
        if (
            not isinstance(identity, str)
            or identity not in required
            or identity in badges
            or any(
                row.get(flag) is not True
                for flag in ("wins_known", "unlocked_verified", "badge_verified")
            )
            or type(row.get("wins")) is not int
        ):
            return None
        matches = [
            (index, badge)
            for index, detail in observations.get(identity, ())
            if (badge := verified_strategy_badge(detail, *required[identity]))
            and badge[0] == row["wins"]
        ]
        if not matches:
            return None
        index, (wins, tier) = matches[-1]
        badges[identity] = {
            "wins": wins,
            "badge_tier": tier,
            "observation_event_index": index,
        }
    return {
        "report_event_index": report_index,
        "schedule_event_index": schedule_index,
        "badges": badges,
    }


def terminal_pact_sample(events, run_id):
    """Match a terminal growth review to its preceding same-run saved HUD sample."""
    saved = set()
    result = None
    for event in events:
        envelope = event.get("details", {})
        if envelope.get("run_id") != run_id or not envelope.get("run_execution_id"):
            continue
        what = envelope.get("what")
        data = envelope.get("details", {})
        if (
            what == "SPABattleSample"
            and envelope.get("class") == "asst::spa::RoundTaskPlugin"
            and data.get("primary_layers_source") == "battle_hud"
            and data.get("battle_frame_saved") is True
            and type(data.get("round")) is int
            and isinstance(data.get("primary_pact"), str)
            and type(data.get("primary_layers")) is int
            and 0 <= data["primary_layers"] <= 999
        ):
            saved.add(
                (
                    envelope["run_execution_id"],
                    data.get("round"),
                    data.get("primary_pact"),
                    data["primary_layers"],
                )
            )
        elif what == "SPAPrimaryPactSettlementReview":
            layers = data.get("battle_sample_layers")
            pact = data.get("primary_pact")
            round_number = data.get("round")
            settlement = data.get("settlement", {})
            if (
                result is not None
                or type(layers) is not int
                or type(round_number) is not int
                or not isinstance(pact, str)
                or not pact
                or not isinstance(settlement, dict)
                or settlement.get("round") != round_number
                or settlement.get("result_frame_saved") is not True
                or settlement.get("result_text_verified") is not True
                or settlement.get("battle_seen") is not True
                or (envelope["run_execution_id"], round_number, pact, layers)
                not in saved
            ):
                return None
            result = {
                "primary_pact": pact,
                "strategy_id": data.get("strategy_id"),
                "round": round_number,
                "layers": layers,
                "met_300": layers >= 300,
                "met_500": layers >= 500,
                "observed_cap": layers == 999,
                "exact_final_count_verified": False,
            }
    return result


def ending_growth_review(events, run_id):
    """Return a route-quality diagnostic backed by one saved terminal-round HUD."""
    latest_sample = {}
    matching_review = None

    def layer_counts(value):
        return (
            value
            if isinstance(value, dict)
            and value
            and all(
                isinstance(pact, str)
                and pact
                and type(layers) is int
                and 0 <= layers <= 999
                for pact, layers in value.items()
            )
            else None
        )

    for event in events:
        envelope = event.get("details", {})
        execution = envelope.get("run_execution_id")
        if envelope.get("run_id") != run_id or not execution:
            continue
        what, data = envelope.get("what"), envelope.get("details", {})
        round_number = data.get("round")
        if type(round_number) is not int or not 1 <= round_number <= 20:
            continue
        sample_key = (execution, round_number)
        if (
            what == "SPABattleSample"
            and envelope.get("class") == "asst::spa::RoundTaskPlugin"
            and data.get("battle_frame_saved") is True
        ):
            if layers := layer_counts(data.get("pact_layers")):
                latest_sample[sample_key] = layers
        elif what == "SPABattleEndingGrowthReview":
            layers = layer_counts(data.get("battle_sample_pact_layers"))
            settlement = data.get("settlement")
            if (
                matching_review is not None
                or layers is None
                or layers != latest_sample.get(sample_key)
                or not isinstance(settlement, dict)
                or settlement.get("round") != round_number
                or settlement.get("result_frame_saved") is not True
                or settlement.get("result_text_verified") is not True
                or settlement.get("battle_seen") is not True
            ):
                return None
            highest = max(layers.values())
            matching_review = {
                "round": round_number,
                "strategy_id": data.get("strategy_id"),
                "primary_pact": data.get("primary_pact"),
                "observed_pacts": layers,
                "highest_observed_layers": highest,
                "needs_growth_review": highest < 300,
                "route_growth_accepted": highest >= 500,
                "opening_formation_active_pacts_all_read": data.get(
                    "opening_formation_active_pacts_all_read"
                )
                is True,
                "exact_final_count_verified": False,
            }
    return matching_review


def audit_run(events, policy, provenance):
    """Require one native execution and an evidence-bound no-known-intervention review."""
    scope = execution_scope(events, policy)
    event_digest, policy_digest = evidence_hash(events), evidence_hash(policy)
    interventions = intervention_evidence(events, policy, provenance)
    mode = policy.get("mode")
    difficulty = None if mode == "quick" else policy.get("difficulty")
    valid_stage = (mode, difficulty) in stage_sequence() and (
        mode == "quick" or type(difficulty) is int
    )
    observations = []
    strategy_indices, formation_indices, exit_inputs, recoveries = [], [], [], []
    for i, event in enumerate(events):
        envelope = event.get("details", {})
        what, details = envelope.get("what"), envelope.get("details", {})
        if what == "SPAObservation":
            observations.append((i, details))
        if (
            mode in ("quick", "independent")
            and what == "SPARoundResultObserved"
            and envelope.get("class") == "asst::spa::RoundTaskPlugin"
            and details.get("page") == "result"
            and details.get("battle_seen") is True
            and details.get("result_frame_saved") is True
            and details.get("return_home_verified") is False
        ):
            # A short-lived settlement can be captured by the battle watcher
            # before the lifecycle observer sees the lobby. The separately
            # observed lobby and matching terminal receipt are still required.
            observations.append((i, details))
        if (
            what == "SPAStrategyCardSelected"
            and details.get("strategy_id")
            and details.get("stage_two_verified") is True
            and details.get("stage_three_verified") is True
        ):
            strategy_indices.append(i)
        if (
            what == "SPAFormationSelected"
            and details.get("formation_page_verified") is True
            and details.get("readiness_committed") is True
        ):
            # Quick may legitimately start with no selected operators.
            formation_indices.append(i)
        if what == "SPALifecycleRecoveryRequired":
            recoveries.append(i)
        task = str(details.get("task", "")).split("@")[-1]
        if task.startswith(("ForceExit", "Abandon")) and details.get("action") not in (
            None,
            "DoNothing",
        ):
            exit_inputs.append(i)
    analysis_events = receipt_events(events, scope)
    receipt = verified_lifecycle_receipt(analysis_events)
    lifecycle_count = sum(
        event.get("details", {}).get("what") == "SPALifecycleCompleted"
        for event in events
    )
    checks = {
        "valid_stage": valid_stage,
        "provenance_reviewed": provenance.get("review_complete") is True,
        "same_run_provenance": bool(policy.get("run_id"))
        and provenance.get("run_id") == policy.get("run_id"),
        "no_host_recovery": type(provenance.get("host_recoveries")) is int
        and provenance["host_recoveries"] == 0,
        "no_manual_takeover": provenance.get("manual_takeover") is False,
        "review_bound_to_evidence": provenance.get("events_sha256") == event_digest
        and provenance.get("policy_sha256") == policy_digest
        and bool(scope["run_execution_id"])
        and provenance.get("run_execution_id") == scope["run_execution_id"]
        and bool(scope["host_session_id"])
        and provenance.get("host_session_id") == scope["host_session_id"],
        "single_execution_scope": scope["complete"],
        "execution_boundaries_verified": scope["boundaries_verified"]
        and lifecycle_count == 1,
        "no_known_intervention": not interventions,
        "no_outer_execution_failure": not scope["outer_execution_failures"],
        "no_recovery_request": not recoveries,
        "no_abandon_input": not exit_inputs,
    }
    rounds = None
    results = []
    combat_samples = []
    if mode == "joint":
        joint = audit_joint_run(analysis_events, policy, scope=scope)
        checks["complete_native_run"] = joint["same_host_full_run_candidate"]
        rounds = joint["personal_passed_rounds"]
        combat_samples = joint["combat_evidence"]
    else:
        label = "极速模拟" if mode == "quick" else f"规程等级{difficulty}"
        combat_samples = combat_evidence(events, label, scope)
        combats = [sample["event_index"] for sample in combat_samples]
        openings = [
            i
            for i, o in observations
            if o.get("page") == "preparation"
            and o.get("round_known") is True
            and o.get("round") == 1
            and not o.get("contradictory", False)
        ]

        def matching_result(o):
            return (
                o.get("page") == "result"
                and not o.get("contradictory", False)
                and o.get("result_text_verified") is True
                and o.get("result_joint") is False
                and o.get("result_completed") is True
                and o.get("result_interrupted") is False
                and o.get("passed_rounds_known") is True
                and type(o.get("passed_rounds")) is int
                and (
                    o.get("result_difficulty_name") == "极速模拟"
                    and o.get("result_difficulty_known") is False
                    and o.get("passed_rounds") == 9
                    if mode == "quick"
                    else o.get("result_difficulty_known") is True
                    and type(o.get("result_difficulty")) is int
                    and o.get("result_difficulty") == difficulty
                    and (
                        o.get("passed_rounds") == 14
                        or (5 <= difficulty <= 20 and o.get("passed_rounds") == 15)
                    )
                )
            )

        results = [(i, o) for i, o in observations if matching_result(o)]
        lobby_index = fresh_solo_lobby_index(events, observations)
        rounds = receipt.get("passed_rounds") if receipt else None
        checks.update(
            {
                "started_at_lobby": lobby_index is not None,
                "ordered_native_run": bool(
                    strategy_indices
                    and formation_indices
                    and openings
                    and combats
                    and results
                    and lobby_index is not None
                    and lobby_index
                    < strategy_indices[0]
                    < formation_indices[0]
                    < openings[0]
                    < combats[-1]
                    < results[-1][0]
                ),
                "matching_completed_receipt": bool(receipt)
                and matching_result({**receipt, "page": "result"})
                and bool(results)
                and rounds == results[-1][1].get("passed_rounds"),
                "returned_to_lobby": bool(results)
                and bool(receipt)
                and receipt.get("return_home_verified") is True
                and any(
                    i > results[-1][0] and o.get("page") == "lobby"
                    for i, o in observations
                ),
            }
        )
    candidate = all(
        value for key, value in checks.items() if key not in PROVENANCE_CHECKS
    )
    qualified = all(checks.values())
    hidden_core = (
        hidden_core_completion_evidence(events, combat_samples, results, difficulty)
        if qualified
        and mode == "independent"
        and 5 <= difficulty <= 20
        and rounds == 15
        else None
    )
    strategy_badge = (
        strategy_badge_completion_evidence(events, strategy_indices)
        if qualified
        and mode == "independent"
        and policy.get("schedule_strategy_badges") is True
        else None
    )
    catalogue_badges = (
        strategy_catalogue_badge_evidence(events)
        if mode == "independent"
        and policy.get("schedule_strategy_badges") is True
        and not strategy_indices
        and not formation_indices
        and not combat_samples
        and lifecycle_count == 0
        and scope["boundaries_verified"]
        and all(
            checks[key] for key in COMMON_CHECKS - {"execution_boundaries_verified"}
        )
        else None
    )
    terminal = bool(receipt)
    verified_failure = (
        scope["complete"]
        and scope["boundaries_verified"]
        and lifecycle_count == 1
        and bool(receipt)
        and receipt.get("contradictory") is False
        and receipt.get("result_completed") is False
        and receipt.get("result_interrupted") is True
    )
    outcome = (
        "known_intervention"
        if interventions
        else "victory_candidate"
        if candidate
        else "verified_failure"
        if verified_failure
        else "evidence_incomplete"
    )
    return {
        "audit_schema_version": 2,
        "run_id": policy.get("run_id", ""),
        "run_execution_id": scope["run_execution_id"],
        "host_session_id": scope["host_session_id"],
        "events_sha256": event_digest,
        "policy_sha256": policy_digest,
        "mode": mode,
        "difficulty": difficulty,
        "checks": checks,
        "qualified_win": qualified,
        "native_full_run_candidate": candidate,
        "no_known_intervention_candidate": candidate,
        "attempt_finished": terminal,
        "execution_finished": scope["finish"] is not None,
        "outcome": outcome,
        "interventions": interventions,
        "streak_action": "credit"
        if qualified
        else "break"
        if terminal or scope["finish"] is not None
        else "pending",
        "personal_passed_rounds": rounds,
        "combat_evidence": combat_samples,
        "terminal_primary_pact_sample": terminal_pact_sample(events, policy["run_id"])
        if scope["boundaries_verified"] and policy.get("run_id")
        else None,
        "ending_growth_review": ending_growth_review(events, policy["run_id"])
        if scope["boundaries_verified"] and policy.get("run_id")
        else None,
        "hidden_core_victory_verified": hidden_core is not None,
        "hidden_core_evidence": hidden_core,
        "hidden_boss_verified": False,
        "strategy_badge_verified": strategy_badge is not None,
        "strategy_badge_evidence": strategy_badge,
        "strategy_catalogue_badge_evidence": catalogue_badges,
        "limitations": [
            "A candidate and a hash-bound review do not detect unrecorded external inputs.",
            "Missing execution/host boundaries are insufficient evidence, not a proven loss.",
            "Settlement count, hidden-boss identity and strategy badges are separate claims.",
        ],
    }


def replay_progress(reports):
    """Replay ordered run audits. Invalid/intervened attempts break the streak."""
    sequence = stage_sequence()
    stages = [
        {"mode": m, "difficulty": d, "consecutive_wins": 0, "accepted": False}
        for m, d in sequence
    ]
    current, seen_ids, seen_executions, seen_evidence, accepted_runs, ignored = (
        0,
        set(),
        set(),
        set(),
        [],
        [],
    )
    verified_catalogue = None
    for report in reports:
        run_id, evidence = report.get("run_id"), report.get("events_sha256")
        execution = report.get("run_execution_id")
        if (
            report.get("attempt_finished") is not True
            and report.get("execution_finished") is not True
        ):
            ignored.append({"run_id": run_id, "reason": "attempt is not terminal"})
            continue
        if (
            not run_id
            or not evidence
            or run_id in seen_ids
            or (execution and execution in seen_executions)
            or evidence in seen_evidence
        ):
            ignored.append(
                {
                    "run_id": run_id,
                    "reason": "missing or duplicate run/evidence identity",
                }
            )
            continue
        seen_ids.add(run_id)
        if execution:
            seen_executions.add(execution)
        seen_evidence.add(evidence)
        catalogue = report.get("strategy_catalogue_badge_evidence")
        if (
            isinstance(catalogue, dict)
            and isinstance(catalogue.get("badges"), dict)
            and set(catalogue["badges"]) == strategy_badge_requirements().keys()
            and report.get("mode") == "independent"
            and bool(execution)
            and bool(report.get("host_session_id"))
            and report.get("execution_finished") is True
            and report.get("attempt_finished") is False
        ):
            verified_catalogue = {
                "run_id": run_id,
                "run_execution_id": execution,
                **catalogue,
            }
            continue  # A report-only execution makes no attempt at the current battle stage.
        if current == len(stages):
            ignored.append(
                {"run_id": run_id, "reason": "all progression stages already accepted"}
            )
            continue
        if (report.get("mode"), report.get("difficulty")) != sequence[current]:
            ignored.append(
                {
                    "run_id": run_id,
                    "reason": "attempt is outside the current permitted stage",
                }
            )
            continue
        checks = report.get("checks", {})
        required_checks = COMMON_CHECKS | (
            {"complete_native_run"} if report.get("mode") == "joint" else SOLO_CHECKS
        )
        passed = (
            report.get("audit_schema_version") == 2
            and bool(execution)
            and bool(report.get("host_session_id"))
            and required_checks <= checks.keys()
            and report.get("qualified_win") is True
            and bool(checks)
            and all(v is True for v in checks.values())
        )
        stages[current]["consecutive_wins"] = (
            stages[current]["consecutive_wins"] + 1 if passed else 0
        )
        accepted_runs.append(
            {
                "run_id": run_id,
                "run_execution_id": execution,
                "counted": passed,
                "hidden_core_victory_verified": passed
                and report.get("hidden_core_victory_verified") is True
                and bool(report.get("hidden_core_evidence")),
                "stage_index": current,
                "outcome": report.get("outcome", "evidence_incomplete"),
                "streak_action": "credit"
                if passed
                else "break_unverified_or_unsuccessful",
            }
        )
        if stages[current]["consecutive_wins"] == 3:
            stages[current]["accepted"] = True
            current += 1
    return {
        "required_consecutive_wins": 3,
        "stages": stages,
        "stage_index": current,
        "next_stage": stages[current] if current < len(stages) else None,
        "progression_complete": current == len(stages),
        "runs": accepted_runs,
        "ignored": ignored,
        "all_strategy_badges_verified": verified_catalogue is not None,
        "strategy_catalogue_badge_evidence": verified_catalogue,
        "hidden_core_victory_verified": any(
            run["hidden_core_victory_verified"] for run in accepted_runs
        ),
    }


def audit_directory(directory, provenance_path, execution_id=None):
    raw = (directory / "events.jsonl").read_bytes()
    events = [
        json.loads(line)
        for line in raw.decode("utf-8-sig").splitlines()
        if line.strip()
    ]
    policy = json.loads(
        (directory / "input-policy.json").read_text(encoding="utf-8-sig")
    )
    if execution_id:
        events = select_execution(events, execution_id)
        run_ids = {event.get("details", {}).get("run_id") for event in events}
        # The child id is recorded by the native batch, not invented by the review.
        if len(run_ids) == 1 and all(
            isinstance(item, str) and item for item in run_ids
        ):
            policy = {
                **policy,
                "batch_parent_run_id": policy.get("run_id"),
                "run_id": next(iter(run_ids)),
                "run_count": 1,
            }
    provenance = json.loads(provenance_path.read_text(encoding="utf-8-sig"))
    report = audit_run(events, policy, provenance)
    report.update(
        source=str(directory.resolve()),
        provenance_source=str(provenance_path.resolve()),
        selected_execution_id=execution_id,
        raw_events_sha256=hashlib.sha256(raw).hexdigest(),
    )
    return report


def reload_report(path):
    """Replay current source evidence, rather than trusting editable success flags."""
    saved = json.loads(path.read_text(encoding="utf-8-sig"))
    try:
        current = audit_directory(
            Path(saved["source"]),
            Path(saved["provenance_source"]),
            saved.get("selected_execution_id"),
        )
        if any(
            saved.get(key) != current.get(key)
            for key in (*SCOPE_KEYS, "events_sha256", "policy_sha256")
        ):
            raise ValueError("Saved report no longer matches its source evidence")
        return current
    except (KeyError, OSError, ValueError, TypeError):
        return {
            **saved,
            "qualified_win": False,
            "native_full_run_candidate": False,
            "no_known_intervention_candidate": False,
            "hidden_core_victory_verified": False,
            "hidden_core_evidence": None,
            "strategy_catalogue_badge_evidence": None,
            "terminal_primary_pact_sample": None,
            "ending_growth_review": None,
            "outcome": "evidence_incomplete",
            "checks": {**saved.get("checks", {}), "source_evidence_reverified": False},
        }


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    audit = sub.add_parser("audit")
    audit.add_argument("run_directory", type=Path)
    audit.add_argument("--provenance", type=Path, required=True)
    audit.add_argument("--run-execution-id")
    audit.add_argument("--output", type=Path, required=True)
    replay = sub.add_parser("replay")
    replay.add_argument("reports", type=Path, nargs="+")
    replay.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "audit":
        report = audit_directory(
            args.run_directory, args.provenance, args.run_execution_id
        )
    else:
        report = replay_progress([reload_report(path) for path in args.reports])
    args.output.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
