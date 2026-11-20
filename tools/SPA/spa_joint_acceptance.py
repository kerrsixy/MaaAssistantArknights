"""Audit one host's joint run evidence; never count a settlement alone as stability."""

import argparse
import json
import math
from pathlib import Path
import re

from spa_checkpoint import verified_lifecycle_receipt

JOINT_DIFFICULTIES = {1: "标准模拟", 2: "险境模拟", 3: "绝境模拟", 4: "终极模拟"}


def combat_hud_verified(observation, difficulty_name="险境模拟"):
    if observation.get("page") != "battle" or observation.get("contradictory", False):
        return False
    mode = False
    counter = False
    for item in observation.get("texts", []):
        score = item.get("score", 0)
        if (
            not isinstance(score, (int, float))
            or not math.isfinite(score)
            or score < 0.9
        ):
            continue
        text, x, y = item.get("text", ""), item.get("x", -1), item.get("y", -1)
        if 0 <= x < 350 and 0 <= y < 70 and text == difficulty_name:
            mode = True
        if 420 <= x < 750 and 0 <= y < 70:
            match = re.fullmatch(r"(\d{1,3})/(\d{1,3})", text)
            if match and 0 <= int(match[1]) <= int(match[2]) and int(match[2]) > 0:
                counter = True
    return mode and counter


def native_battle_sample_verified(event, scope):
    """Accept RoundTask's parsed HUD only inside a verified native execution."""
    if not scope or not scope.get("complete") or not scope.get("boundaries_verified"):
        return False
    envelope = event.get("details", {})
    details = envelope.get("details", {})
    if (
        event.get("message") != 20003
        or envelope.get("what") != "SPABattleSample"
        or envelope.get("class") != "asst::spa::RoundTaskPlugin"
        or envelope.get("taskchain") != "StrongholdProtocol"
        or envelope.get("taskid") != scope.get("task_id")
        or any(
            not scope.get(key) or envelope.get(key) != scope[key]
            for key in ("run_id", "run_execution_id", "host_session_id")
        )
        or details.get("contradictory", False)
        or details.get("enemies_total_known") is not True
        or details.get("enemies_resolved_known") is not True
    ):
        return False
    round_number, resolved, total = (
        details.get("round"),
        details.get("enemies_resolved"),
        details.get("enemies_total"),
    )
    return (
        type(round_number) is int
        and 1 <= round_number <= 20
        and type(resolved) is int
        and type(total) is int
        and 0 <= resolved <= total <= 999
        and total > 0
    )


def combat_evidence(events, difficulty_name, scope=None):
    """Keep the two actual HUD sources distinct; input/start receipts are not HUDs."""
    samples = []
    for index, event in enumerate(events):
        envelope = event.get("details", {})
        details = envelope.get("details", {})
        if envelope.get("what") == "SPAObservation" and combat_hud_verified(
            details, difficulty_name
        ):
            samples.append({"event_index": index, "source": "observer_combat_hud"})
        elif native_battle_sample_verified(event, scope):
            samples.append(
                {
                    "event_index": index,
                    "source": "native_round_task_hud",
                    "round": details["round"],
                    "enemies_resolved": details["enemies_resolved"],
                    "enemies_total": details["enemies_total"],
                }
            )
    return samples


def audit_joint_run(events, policy, *, scope=None):
    difficulty = policy.get("difficulty")
    difficulty_name = (
        JOINT_DIFFICULTIES.get(difficulty, "") if type(difficulty) is int else ""
    )
    observations = []
    difficulty_indices = []
    start_indices = []
    recovery_requests = []
    exit_inputs = []
    for index, event in enumerate(events):
        envelope = event.get("details", {})
        what, details = envelope.get("what"), envelope.get("details", {})
        if what == "SPAObservation":
            observations.append((index, details))
        if (
            what == "SPADifficultySelected"
            and details.get("mode") == "joint"
            and details.get("difficulty") == difficulty
        ):
            difficulty_indices.append(index)
        if (
            what == "SPASimulationStartIssued"
            and details.get("mode") == "joint"
            and details.get("difficulty") == difficulty
        ):
            start_indices.append(index)
        if what == "SPALifecycleRecoveryRequired":
            recovery_requests.append(details)
        task = str(details.get("task", "")).split("@")[-1]
        if task.startswith("ForceExit") or task.startswith("Abandon"):
            if details.get("action") not in (None, "DoNothing"):
                exit_inputs.append(task)
    first_lobby = bool(observations) and observations[0][1].get("page") == "lobby"
    first_round = [
        i
        for i, o in observations
        if o.get("page") == "preparation"
        and o.get("round_known")
        and o.get("round") == 1
        and not o.get("contradictory", False)
    ]
    combat_samples = combat_evidence(events, difficulty_name, scope)
    combats = [sample["event_index"] for sample in combat_samples]
    result_indices = [
        i
        for i, o in observations
        if o.get("page") == "result"
        and o.get("result_text_verified")
        and o.get("result_joint")
        and o.get("result_difficulty_name") == difficulty_name
        and o.get("result_completed")
        and not o.get("result_interrupted")
        and o.get("passed_rounds_known")
        and not o.get("contradictory", False)
    ]
    receipt = verified_lifecycle_receipt(events)
    final_result = (
        bool(receipt)
        and receipt.get("result_joint") is True
        and receipt.get("result_difficulty_name") == difficulty_name
        and receipt.get("result_completed") is True
        and not receipt.get("result_interrupted")
        and not receipt.get("contradictory", False)
        and any(
            i in result_indices
            and o.get("passed_rounds") == receipt.get("passed_rounds")
            for i, o in observations
        )
    )
    # The alliance aggregate can be 13 while the local player completed 14.
    # Never use teammates' rounds, the aggregate, or roster levels as ours.
    summaries = [
        (i, personal_summary(o, difficulty_name))
        for i, o in observations
        if o.get("page") == "result_summary"
    ]
    summaries = [(i, s) for i, s in summaries if s is not None]
    personal = summaries[-1][1] if summaries else None
    personal_verified = (
        bool(personal) and personal["rounds"] in (14, 15) and personal["completed"]
    )
    if (
        personal_verified
        and receipt
        and receipt.get("personal_result_source") == "local_summary"
    ):
        personal_verified = (
            receipt.get("personal_passed_rounds") == personal["rounds"]
            and receipt.get("personal_result_identity") == personal["identity"]
            and receipt.get("personal_result_completed") is True
            and not receipt.get("personal_result_interrupted")
        )
    ordered = (
        bool(
            first_lobby
            and difficulty_indices
            and start_indices
            and first_round
            and combats
            and result_indices
        )
        and observations[0][0]
        < difficulty_indices[0]
        < start_indices[0]
        < first_round[0]
        < combats[-1]
        < result_indices[-1]
    )
    final_lobby = bool(result_indices) and any(
        i > result_indices[-1] and o.get("page") == "lobby" for i, o in observations
    )
    checks = {
        "requested_joint_difficulty": policy.get("mode") == "joint"
        and bool(difficulty_name),
        "started_at_lobby": first_lobby,
        "difficulty_selection_observed": bool(difficulty_indices),
        "simulation_start_observed": bool(start_indices),
        "opening_preparation_observed": bool(first_round),
        "real_combat_hud_observed": bool(combats),
        "completed_full_team_result": bool(result_indices) and final_result,
        "completed_personal_result": personal_verified,
        "personal_summary_ordered": bool(summaries and combats and result_indices)
        and combats[-1] < summaries[-1][0] < result_indices[-1],
        "ordered_run_evidence": ordered,
        "returned_to_lobby": final_lobby
        and bool(receipt)
        and receipt.get("return_home_verified") is True,
        "no_manual_recovery_request": not recovery_requests,
        "no_abandon_input": not exit_inputs,
    }
    return {
        "run_id": policy.get("run_id", ""),
        "checks": checks,
        "same_host_full_run_candidate": all(checks.values()),
        "stable_acceptance_verified": False,
        "hidden_boss_victory_verified": False,
        "passed_rounds": receipt.get("passed_rounds") if receipt else None,
        "passed_rounds_scope": "alliance",
        "alliance_passed_rounds": receipt.get("passed_rounds") if receipt else None,
        "personal_passed_rounds": personal["rounds"] if personal else None,
        "personal_result_identity": personal["identity"] if personal else None,
        "personal_result_source": "local_summary" if personal else None,
        "limitations": [
            "One run cannot establish repeated stability",
            "External/manual inputs require a separate provenance audit",
            "Round 15 completion alone does not verify hidden boss identity or badges",
        ],
        "preparation_rounds": sorted(
            {
                o["round"]
                for _, o in observations
                if o.get("page") == "preparation"
                and o.get("round_known")
                and isinstance(o.get("round"), int)
            }
        ),
        "combat_hud_samples": len(combats),
        "combat_evidence": combat_samples,
        "recovery_requests": recovery_requests,
        "abandon_inputs": exit_inputs,
    }


def personal_summary(observation, difficulty_name="险境模拟"):
    """Read the anchored local settlement, also for historical raw OCR logs."""
    if observation.get("page") != "result_summary" or observation.get(
        "contradictory", False
    ):
        return None
    texts = [
        t
        for t in observation.get("texts", [])
        if isinstance(t.get("score"), (int, float))
        and math.isfinite(t["score"])
        and t["score"] >= 0.9
    ]

    def values(left, top, right, bottom):
        return {
            t.get("text", "")
            for t in texts
            if left <= t.get("x", -1) < right and top <= t.get("y", -1) < bottom
        }

    banners = values(0, 150, 430, 250) & {"模拟完成", "模拟中断"}
    modes = values(100, 130, 430, 180) & {
        "标准模拟",
        "险境模拟",
        "绝境模拟",
        "终极模拟",
    }
    rounds = {
        int(t) for t in values(0, 290, 180, 390) if re.fullmatch(r"[0-9]{1,2}", t)
    }
    identities = {
        t for t in values(100, 440, 430, 495) if re.fullmatch(r"Dr\..+#[0-9]+", t)
    }
    if (
        len(banners) != 1
        or modes != {difficulty_name}
        or len(rounds) != 1
        or len(identities) != 1
        or "通过回合" not in values(0, 240, 220, 300)
        or "下一步" not in values(900, 590, 1280, 710)
    ):
        return None
    count = rounds.pop()
    if count > 20:
        return None
    return {
        "rounds": count,
        "completed": banners == {"模拟完成"},
        "identity": identities.pop(),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("run_directory", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    with (args.run_directory / "events.jsonl").open(encoding="utf-8") as stream:
        events = [json.loads(line) for line in stream if line.strip()]
    with (args.run_directory / "input-policy.json").open(
        encoding="utf-8-sig"
    ) as stream:
        policy = json.load(stream)
    report = json.dumps(audit_joint_run(events, policy), ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(report + "\n", encoding="utf-8")
    print(report)


if __name__ == "__main__":
    main()
