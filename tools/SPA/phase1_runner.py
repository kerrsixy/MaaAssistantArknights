"""Phase-one solo runner.

This is the deliberately conservative first delivery: enter the selected
solo difficulty, select a legal visible strategy/formation, then hand the run
to the native guarded controller.  The controller buys affordable visible
cards, deploys more than the initial formation when cells are verified,
resolves reward pages, waits through battles, and leaves a durable result
audit.  It does not claim a victory; a verified loss is still a completed
phase-one run.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

from spa_checkpoint import (
    checkpoint_preparation_transactions,
    checkpoint_roster,
    verified_boss_context,
)


def invoke(args: argparse.Namespace, task: str, output: Path, extra: list[str] | None = None) -> tuple[int, dict]:
    if output.exists():
        raise RuntimeError(f"output already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        sys.executable,
        str(Path(__file__).with_name("observe_live.py")),
        "--core-dir", args.core_dir,
        "--adb", args.adb,
        "--address", args.address,
        "--output", str(output),
        "--task", task,
    ] + (extra or [])
    completed = subprocess.run(command, text=True, encoding="utf-8", errors="replace", capture_output=True)
    (output.parent / f"{output.name}.stdout.txt").write_text(completed.stdout or "", encoding="utf-8")
    (output.parent / f"{output.name}.stderr.txt").write_text(completed.stderr or "", encoding="utf-8")
    observations_file = output / "observations.json"
    observations = []
    if observations_file.exists():
        try:
            observations = json.loads(observations_file.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            observations = []
    return completed.returncode, observations[-1] if observations else {}


def latest_page(args: argparse.Namespace, root: Path, index: int) -> dict:
    target = root / f"observe_{index:03d}"
    while target.exists():
        index += 1
        target = root / f"observe_{index:03d}"
    code, observation = invoke(args, "observe", target)
    if code and not observation:
        raise RuntimeError("observation failed without a recoverable page")
    return observation


def is_auth_expired(observation: dict) -> bool:
    return observation.get("page") == "authentication_expired" or "登录认证已失效" in str(observation.get("texts", ""))


def is_setup_page(observation: dict) -> bool:
    """Treat the explicit 1/3-3/3 header as setup evidence.

    During the animated transition the observer can classify the frame as
    ``unknown`` even though ``setup_stage`` is already present.  Starting the
    native round loop on that frame skips pact/strategy/formation selection.
    """
    stage = str(observation.get("setup_stage", ""))
    return observation.get("page") == "run_setup" or stage in {"1/3", "2/3", "3/3"}


def lifecycle_evidence(events: list[dict], home_verified: bool) -> dict:
    battle_seen = False
    result_verified = False
    passed_rounds = None
    result_difficulty = None
    for event in events:
        envelope = event.get("details", {})
        what = envelope.get("what", "")
        details = envelope.get("details", {})
        if what == "SPABattleSample" or (what == "SPARoundTransition" and details.get("battle_seen")):
            battle_seen = True
        if what in {"SPAObservation", "SPARoundResultObserved", "SPAAutomationResultObserved"}:
            if details.get("result_text_verified") and (details.get("result_completed") or details.get("result_interrupted")):
                result_verified = True
            if details.get("passed_rounds_known") and isinstance(details.get("passed_rounds"), int):
                passed_rounds = details["passed_rounds"]
            if details.get("result_difficulty_known") and isinstance(details.get("result_difficulty"), int):
                result_difficulty = details["result_difficulty"]
    return {
        "battle_seen": battle_seen,
        "result_page_verified": result_verified,
        "return_home_verified": home_verified,
        "passed_rounds": passed_rounds,
        "result_difficulty": result_difficulty,
        "phase1_complete": battle_seen and result_verified and home_verified,
    }


def recover_session(args: argparse.Namespace, root: Path, index: int) -> bool:
    """Reconnect the game session without abandoning an active SPA run.

    The recovery path is deliberately separate from result cleanup: startup
    and reconnect only restore the foreground account/session, then the
    caller reattaches the same policy to the live preparation/battle page.
    """
    startup_code, _ = invoke(args, "startup", root / f"session-startup-{index:02d}")
    if startup_code:
        return False
    # StartUp restores the game homepage, not the SPA lobby. Open the
    # current-season banner before searching for the existing run's reconnect
    # button. This never starts a new simulation or abandons the saved run.
    invoke(args, "open-lobby", root / f"session-open-lobby-{index:02d}")
    reconnect_code, _ = invoke(args, "reconnect-solo", root / f"session-reconnect-{index:02d}")
    return reconnect_code == 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core-dir", required=True)
    parser.add_argument("--adb", required=True)
    parser.add_argument("--address", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--difficulty", type=int, default=14)
    parser.add_argument("--mode", choices=("solo",), default="solo")
    parser.add_argument(
        "--dynamic",
        action="store_true",
        help="use the phase-two preparation planner while retaining the guarded phase-one executor",
    )
    parser.add_argument("--max-recovery", type=int, default=4)
    args = parser.parse_args()
    if args.mode != "solo":
        raise SystemExit("phase1 currently exposes solo mode only; joint mode is a phase-two mode gate")
    args.output.mkdir(parents=False, exist_ok=False)

    # Do not blindly click the mode toggle: the lobby always renders both
    # labels, and clicking the toggle while already in personal mode switches
    # into joint mode.  Try the unambiguous “独立模拟” start card first; only
    # toggle once when that card did not open the personal difficulty page.
    initial = latest_page(args, args.output, 0)
    # Keep the opening setup in one native task chain.  The previous runner
    # spawned three Python/MAA processes (pacts, strategy, formation), each of
    # which reloaded OCR/resources and added several seconds of startup time.
    # The combined chain still emits the same per-step receipts and guarded
    # recovery events, but avoids that repeated initialization.
    pact_dir = args.output / "pacts"
    strategy_dir = args.output / "strategy"
    receipt_path = pact_dir / "setup-receipt.json"
    if initial.get("page") == "lobby":
        # Fresh runs used to launch one native process for each of
        # open-mode, difficulty, start, and setup.  Each process reconnects
        # to ADB and reloads the OCR pack, so the fixed opening cost was
        # already close to the user's observed 20 seconds before any planner
        # decision.  The setup-flow chain performs the same guarded actions
        # in one process and keeps the per-stage callbacks.
        _, setup_observation = invoke(
            args,
            "setup-flow",
            args.output / "setup-flow",
            [
                "--difficulty", str(args.difficulty),
                "--core-pact", "auto",
                "--strategy-index", "-1",
                "--formation-operator", "auto",
            ],
        )
        if setup_observation:
            current = setup_observation
        else:
            try:
                current = latest_page(args, args.output, 1)
            except RuntimeError:
                current = {"page": "unknown", "transient": True, "setup_transition_committed": True}
        # setup-flow writes the same receipt at its own root.  Keep the
        # historical `pacts/setup-receipt.json` location for the policy merge
        # below without launching another observer process.
        flow_receipt = args.output / "setup-flow" / "setup-receipt.json"
        if flow_receipt.exists():
            pact_dir.mkdir(parents=True, exist_ok=True)
            receipt_path.write_text(flow_receipt.read_text(encoding="utf-8"), encoding="utf-8")
    elif initial.get("page") not in ("preparation", "battle", "result"):
        _, opened = invoke(args, "open-independent-start", args.output / "open-independent")
        # An action task can report RecognitionFailed after its click already
        # committed.  Always re-observe the live page before deciding to
        # toggle mode; otherwise a difficulty page is mistaken for a lobby and
        # the retry clicks the wrong control.
        current = opened if opened.get("page") else latest_page(args, args.output, 1)
        if current.get("page") not in ("difficulty", "preparation", "battle") and not is_setup_page(current):
            invoke(args, "open-independent-difficulty", args.output / "open-personal")
            current = latest_page(args, args.output, 2)
            if current.get("page") == "lobby":
                invoke(args, "open-independent-start", args.output / "open-independent-retry")
                current = latest_page(args, args.output, 3)
        if current.get("page") == "difficulty" or is_setup_page(current):
            invoke(args, "start-independent", args.output / "difficulty", ["--difficulty", str(args.difficulty)])
            current = latest_page(args, args.output, 4)
        if is_setup_page(current):
            _, setup_observation = invoke(
                args,
                "setup-remaining",
                pact_dir,
                [
                    "--core-pact", "auto",
                    "--strategy-index", "-1",
                    "--formation-operator", "auto",
                ],
            )
            # Formation selection may report setup-blocked after its 3/3 click
            # has already committed and entered preparation.  Re-observe before
            # deciding whether a retry is needed.  The first frame after an
            # empty opening hand can be an animation/black transition with no
            # OCR payload; that is a resumable handoff, not a non-combat run
            # failure.  Let the native observer attach and wait for the HUD.
            # The combined native chain already ends with its guarded
            # read-only Observe leaf. Reuse that observation instead of
            # starting a second Python/MAA process just to read the same
            # frame; fall back only when the transition produced no payload.
            if setup_observation:
                current = setup_observation
            else:
                try:
                    current = latest_page(args, args.output, 5)
                except RuntimeError:
                    current = {"page": "unknown", "transient": True, "setup_transition_committed": True}
    receipt = json.loads(receipt_path.read_text(encoding="utf-8")) if receipt_path.exists() else {}

    policy = {
        "run_id": f"phase1-{int(time.time())}",
        "mode": "independent",
        "difficulty": args.difficulty,
        **verified_boss_context(receipt),
        "primary_pact": "auto",
        "dynamic_primary_pact": True,
        "enabled_core_pacts": [],
        "disabled_pacts": receipt.get("disabled_pacts", [])
        if isinstance(receipt.get("disabled_pacts", []), list)
        else [],
        "carries": [],
        "roster": [],
        "bench_occupied": 0,
        "minimum_recruit_price": 1,
        "allow_refresh": True,
        "derive_support_pacts": True,
        "basic_mode": True,
        "dynamic_decision_mode": args.dynamic,
        "basic_upgrade_rounds": [] if args.dynamic else [4, 8, 12],
        "opening_strategy_selection": args.dynamic,
        "auto_run": True,
        "auto_deploy": True,
        "auto_deploy_direction": "E",
        "max_actions": 300,
        "selected_strategy_id": receipt.get("selected_strategy_id") or f"slot:{receipt.get('strategy_index', -1)}"
        if receipt_path.exists()
        else "slot:-1",
    }
    policy_path = args.output / "phase1-policy.json"
    policy_path.write_text(json.dumps(policy, ensure_ascii=False, indent=2), encoding="utf-8")

    native_dir = args.output / "native-run"
    native_args = ["--policy-file", str(policy_path)]
    # A 3/3 formation click can commit into the preparation HUD while the
    # formation task still reports a guarded recognition error.  Such a
    # receipt is diagnostic only; passing it to auto-preparation makes the
    # runner reject an already playable live run.  Use it only when the
    # receipt explicitly proves the final formation/battle transition.
    if receipt_path.exists() and receipt.get("formation_confirmed", False) and receipt.get("battle_hud_confirmed", False):
        native_args += ["--setup-receipt", str(receipt_path)]
    native_code, native_observation = invoke(
        args,
        "auto-preparation",
        native_dir,
        native_args,
    )

    # Native automation may deliberately return a resumable checkpoint after
    # a promotion window, an interrupted process, or a transient page.  A
    # preparation/battle observation is therefore not a terminal failure:
    # merge the durable roster/funds checkpoint and reattach the same policy
    # to the live run.  This is also how a temporary three-card elite shop is
    # crossed without opening a second simulation.
    def merge_checkpoint(checkpoint_root: Path) -> None:
        checkpoint = checkpoint_root / "recovery-state.json"
        state = {}
        if checkpoint.exists():
            try:
                loaded = json.loads(checkpoint.read_text(encoding="utf-8-sig"))
                if isinstance(loaded, dict):
                    state = loaded
            except (OSError, json.JSONDecodeError):
                state = {}
        if state.get("run_id") != policy["run_id"]:
            state = {}
        resume_context = {
            **verified_boss_context(state),
            **checkpoint_preparation_transactions(state),
        }
        policy.update(resume_context)
        changed = bool(resume_context)
        if isinstance(state.get("roster"), list):
            roster, pending_promotion = checkpoint_roster(state, policy.get("promotion_pending", ""))
            policy["roster"] = roster
            policy["promotion_pending"] = pending_promotion
            changed = True
        if isinstance(state.get("bench_occupied"), int):
            policy["bench_occupied"] = state["bench_occupied"]
            changed = True
        if type(state.get("bench_recount_pending")) is bool:
            policy["bench_recount_pending"] = state["bench_recount_pending"]
            changed = True
        if type(state.get("tactical_elite_purchase_pending")) is bool:
            policy["tactical_elite_purchase_pending"] = state["tactical_elite_purchase_pending"]
            changed = True
        if isinstance(state.get("pact_recruitment_pending"), str):
            policy["pact_recruitment_pending"] = state["pact_recruitment_pending"]
            changed = True
        pager_round = state.get("pact_recruitment_attempted_round")
        if type(pager_round) is int and 1 <= pager_round <= 20:
            policy["pact_recruitment_attempted_round"] = pager_round
            changed = True
        level = state.get("shop_level")
        if type(level) is int and 1 <= level <= 6:
            policy["shop_level"] = level
            changed = True
        if isinstance(state.get("round"), int) and state["round"] >= 1:
            policy["resume_after_round"] = state["round"]
            changed = True
        copy_target = state.get("strategy_copy_target")
        copy_unverified = state.get("strategy_copy_unverified")
        copy_retry_round = state.get("strategy_copy_retry_after_round", -1)
        if (
            isinstance(copy_target, str)
            and type(copy_unverified) is bool
            and (copy_target or not copy_unverified)
            and type(copy_retry_round) is int
            and (copy_retry_round == -1 or 1 <= copy_retry_round <= policy.get("resume_after_round", 1))
        ):
            policy["strategy_copy_target"] = copy_target
            policy["strategy_copy_unverified"] = copy_unverified
            policy["strategy_copy_retry_after_round"] = copy_retry_round
            changed = True
        if isinstance(state.get("remaining_deployment"), int) and 0 <= state["remaining_deployment"] <= 8:
            policy["remaining_deployment"] = state["remaining_deployment"]
            changed = True
        if isinstance(state.get("field_positions"), list):
            policy["field_positions"] = state["field_positions"]
            changed = True
        if isinstance(state.get("used_deploy_points"), list):
            policy["used_deploy_points"] = state["used_deploy_points"]
            changed = True
        if changed:
            policy_path.write_text(json.dumps(policy, ensure_ascii=False, indent=2), encoding="utf-8")

        blocked_file = checkpoint_root / "deployment-blocked.json"
        if blocked_file.exists():
            try:
                blocked = json.loads(blocked_file.read_text(encoding="utf-8-sig"))
            except (OSError, json.JSONDecodeError):
                blocked = {}
            if isinstance(blocked, dict) and isinstance(blocked.get("names"), list):
                existing = policy.get("deployment_blocked_names", [])
                if not isinstance(existing, list):
                    existing = []
                for name in blocked["names"]:
                    if isinstance(name, str) and name and name not in existing:
                        existing.append(name)
                policy["deployment_blocked_names"] = existing
                if isinstance(blocked.get("round"), int):
                    policy["deployment_blocked_round"] = blocked["round"]
                policy_path.write_text(json.dumps(policy, ensure_ascii=False, indent=2), encoding="utf-8")

    current = native_observation
    last_native_root = native_dir
    for recovery_index in range(args.max_recovery):
        if not is_auth_expired(current):
            break
        recovery_root = args.output / f"recovery-{recovery_index:02d}"
        if not recover_session(args, recovery_root, recovery_index):
            continue
        merge_checkpoint(last_native_root)
        last_native_root = recovery_root / "native-resume"
        native_code, current = invoke(
            args, "auto-preparation", last_native_root, native_args
        )

    for index in range(args.max_recovery + 4):
        if current.get("page") in ("result", "lobby"):
            break
        if is_auth_expired(current):
            recovery_root = args.output / f"recovery-observe-{index:02d}"
            if recover_session(args, recovery_root, index):
                merge_checkpoint(last_native_root)
                native_code, current = invoke(
                    args,
                    "auto-preparation",
                    recovery_root / "native-resume",
                    native_args,
                )
                last_native_root = recovery_root / "native-resume"
                continue
        # Prefer the checkpoint emitted by the last native invocation.  If
        # the native process did not emit one, a read-only observation still
        # gives us the current page and keeps the same live run intact.
        merge_checkpoint(last_native_root)
        continuation_root = args.output / f"continuation-{index:02d}"
        native_code, resumed = invoke(
            args,
            "auto-preparation",
            continuation_root / "native-resume",
            native_args,
        )
        current = resumed
        last_native_root = continuation_root / "native-resume"
        if not current:
            current = latest_page(args, args.output, 100 + index)

    if current.get("page") == "result":
        audit_code, _ = invoke(args, "return-result-home", args.output / "result-audit")
        if audit_code:
            raise RuntimeError("result page was reached but return-home audit failed")
    elif current.get("page") != "lobby":
        raise RuntimeError(f"phase-one run stopped before settlement: {current.get('page')}")

    timeline = []
    for event_file in args.output.rglob("events.jsonl"):
        for line in event_file.read_text(encoding="utf-8").splitlines():
            try:
                timeline.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    home_audit = args.output / "result-audit" / "result-audit.json"
    home_verified = False
    if home_audit.exists():
        home_verified = json.loads(home_audit.read_text(encoding="utf-8")).get("return_home_verified", False)
    (args.output / "phase1-summary.json").write_text(
        json.dumps(
            {
                "mode": args.mode,
                "difficulty": args.difficulty,
                "setup_completed": True,
                "native_return_code": native_code,
                "terminal_page": current.get("page"),
                **lifecycle_evidence(timeline, home_verified),
                "phase2_victory_required": True,
            },
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
