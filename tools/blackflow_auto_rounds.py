"""Run complete BlackFlow samples serially, stopping on incomplete evidence.

The default has no wall-clock cutoff. Each round must finish natively, settle,
and return to the lobby before another round can start. Module refresh runs only
in the first round; later rounds use that verified account's cache. Native game
pass flags are recorded without claiming an independently audited ending.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

from blackflow_live_run import (
    DLL_DIR,
    is_fresh_lobby,
    validate_pause_classification_summary,
)
from blackflow_state_check import ADB_ADDRESS
from blackflow_state_check import check as check_state

REPO_ROOT = Path(__file__).resolve().parents[1]
RUNNER = REPO_ROOT / "tools/blackflow_live_run.py"
LOG_ROOT = Path(r"D:\dsh\明日方舟\日志")


def verified_round(outcome, client, expected_uid=None, expected_difficulty=None):
    """Validate completion and account identity independently of process exit."""
    if not isinstance(outcome, dict):
        raise ValueError("Outcome is not an object")
    validate_pause_classification_summary(outcome)
    if any(
        type(outcome.get(key)) is not int
        for key in (
            "task_exit_code",
            "workflow_error_callbacks",
            "pause_callbacks",
            "fresh_exploration_starts",
            "assisted_roster_restorations",
        )
    ):
        raise ValueError("Completion/error counters must be typed integers")
    if (
        outcome.get("task_exit_code") != 0
        or outcome.get("finished") is not True
        or outcome.get("settlement_required") is not True
        or outcome.get("stop_requested") is not False
        or outcome.get("workflow_error_callbacks") != 0
        or outcome.get("pause_callbacks") != 0
        or outcome.get("fresh_exploration_starts") != 1
        or outcome.get("assisted_roster_restorations") != 0
        or outcome.get("assisted_continuation") is True
        or outcome.get("unattended_acceptance_eligible") is False
    ):
        raise ValueError(
            "Round stopped, failed, paused or has incomplete completion evidence"
        )
    lobby = outcome.get("final_lobby")
    settlements = outcome.get("settlements")
    if not is_fresh_lobby(lobby):
        raise ValueError("Final lobby was not verified")
    if (
        not isinstance(settlements, list)
        or not settlements
        or any(
            not isinstance(row, dict) or type(row.get("game_pass")) is not bool
            for row in settlements
        )
    ):
        raise ValueError("A valid native settlement is required")
    difficulty = settlements[-1].get("difficulty")
    if type(difficulty) is not int or difficulty < 0:
        raise ValueError("Actual settled difficulty is missing or malformed")
    if expected_difficulty is not None and difficulty != expected_difficulty:
        raise ValueError("Actual settled difficulty differs from the requested sample")
    bindings = outcome.get("account_bindings")
    if not isinstance(bindings, list) or not bindings:
        raise ValueError("Live account binding is missing")
    identity = None
    for binding in bindings:
        if not isinstance(binding, dict):
            raise ValueError("Malformed live account binding")
        uid, cache = binding.get("uid"), binding.get("cache_file")
        if (
            not isinstance(uid, str)
            or not uid.strip()
            or not isinstance(cache, str)
            or not cache.strip()
        ):
            raise ValueError("Live UID/cache is missing")
        if binding.get("client") != client or (
            expected_uid is not None and uid != expected_uid
        ):
            raise ValueError("Live account changed")
        current = (uid, cache)
        if identity is not None and current != identity:
            raise ValueError("Account/cache changed within one round")
        identity = current
    return identity, settlements[-1]["game_pass"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument(
        "--seconds", type=int, default=0, help="单轮时限；0 等待原生结束"
    )
    parser.add_argument("--squad", default="远程战术分队")
    parser.add_argument("--roles", default="稳扎稳打")
    parser.add_argument("--address", default=ADB_ADDRESS)
    parser.add_argument(
        "--client", choices=("Official", "Bilibili"), default="Official"
    )
    parser.add_argument("--difficulty", type=int, default=2_147_483_647)
    parser.add_argument("--core-dir", default=str(DLL_DIR))
    parser.add_argument("--core-char", default="维什戴尔")
    parser.add_argument("--account-cache-file")
    parser.add_argument(
        "--scan-account-modules", action=argparse.BooleanOptionalAction, default=True
    )
    parser.add_argument(
        "--life-game", action=argparse.BooleanOptionalAction, default=True
    )
    parser.add_argument("--mumu-extras", action="store_true")
    parser.add_argument(
        "--battle-observation-interval",
        type=float,
        default=1.0,
        help="只读缓存帧采样间隔；验证批次默认1秒，0关闭，不发出游戏输入",
    )
    parser.add_argument(
        "--battle-observation-limit",
        type=int,
        default=1200,
        help="每场采样次数上限，默认1200；缓存采样时间不是游戏帧时间",
    )
    parser.add_argument("--gap", type=int, default=0, help="轮间等待秒数，0 至 60")
    parser.add_argument("--tag", help="本批次唯一日志标记")
    parser.add_argument(
        "--skip-precheck", action="store_true", help="仅跳过画面检查，仍需原生账号核验"
    )
    args = parser.parse_args()
    if (
        args.rounds < 1
        or args.seconds < 0
        or args.difficulty < 0
        or not 0 <= args.gap <= 60
        or not math.isfinite(args.battle_observation_interval)
        or args.battle_observation_interval < 0
        or args.battle_observation_limit < 1
    ):
        parser.error(
            "rounds/observation limit must be positive; seconds/difficulty/finite observation interval nonnegative; gap in0..60"
        )
    tag = args.tag or f"auto_{datetime.now():%Y%m%d_%H%M%S}"
    if not re.fullmatch(r"[\w.-]+", tag):
        parser.error("tag must not contain path separators")
    if not RUNNER.is_file():
        print(f"Runner not found: {RUNNER}", file=sys.stderr)
        return 2
    report_path = LOG_ROOT / f"blackflow_regression_{tag}.json"
    if report_path.exists():
        print(f"Preserve existing regression evidence: {report_path}", file=sys.stderr)
        return 2
    report = {
        "client": args.client,
        "address": args.address,
        "core_dir": args.core_dir,
        "requested_rounds": args.rounds,
        "requested_difficulty": args.difficulty,
        "wait_limit_seconds": args.seconds,
        "battle_observation": {
            "enabled": args.battle_observation_interval > 0,
            "interval": args.battle_observation_interval,
            "limit": args.battle_observation_limit,
            "capture_source": "AsstGetImage cached frame; not engine frame time or scene classification",
            "gameplay_control_inputs": 0,
        },
        "samples": [],
        "status": "running",
        "unattended_first_ending_proven": False,
        "stable_n10_proven": False,
    }

    def save(status, error=None):
        report["status"] = status
        if error:
            report["error"] = error
        report["completed_samples"] = len(report["samples"])
        report["native_game_pass_count"] = sum(
            row["native_game_pass"] for row in report["samples"]
        )
        LOG_ROOT.mkdir(parents=True, exist_ok=True)
        report_path.write_text(
            json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )

    save("running")
    expected_uid, cache = None, args.account_cache_file
    for index in range(1, args.rounds + 1):
        out = LOG_ROOT / f"blackflow_round_{tag}_r{index:02d}"
        if out.exists():
            save("stopped", f"Preserve existing round evidence: {out}")
            return 2
        if not args.skip_precheck:
            state = check_state(address=args.address)
            if not is_fresh_lobby(state):
                save(
                    "stopped",
                    "Preflight rejected the current page; no round was started",
                )
                return 4
        command = [
            sys.executable,
            str(RUNNER),
            "--seconds",
            str(args.seconds),
            "--starts",
            "1",
            "--mode",
            "0",
            "--strategy",
            "ending1",
            "--out",
            str(out),
            "--squad",
            args.squad,
            "--roles",
            args.roles,
            "--address",
            args.address,
            "--client",
            args.client,
            "--difficulty",
            str(args.difficulty),
            "--core-dir",
            args.core_dir,
            "--core-char",
            args.core_char,
            "--battle-observation-interval",
            str(args.battle_observation_interval),
            "--battle-observation-limit",
            str(args.battle_observation_limit),
            "--scan-account-modules"
            if index == 1 and args.scan_account_modules
            else "--no-scan-account-modules",
            "--life-game" if args.life_game else "--no-life-game",
        ]
        if cache:
            command.extend(["--account-cache-file", cache])
        if args.mumu_extras:
            command.append("--mumu-extras")
        print(f"Round{index}/{args.rounds}: {out}", flush=True)
        started = time.monotonic()
        try:
            code = subprocess.call(command, cwd=str(REPO_ROOT))
            if code != 0:
                raise ValueError(
                    f"Runner exited with code{code}; no subsequent round started"
                )
            outcome = json.loads((out / "outcome.json").read_text(encoding="utf-8"))
            (uid, bound_cache), game_pass = verified_round(
                outcome,
                args.client,
                expected_uid,
                None if args.difficulty == 2_147_483_647 else args.difficulty,
            )
            if (
                cache is not None
                and Path(cache).resolve() != Path(bound_cache).resolve()
            ):
                raise ValueError(
                    "Native binding does not match the selected account cache"
                )
        except (OSError, ValueError) as error:
            save("stopped", str(error))
            print(f"Regression stopped: {error}", file=sys.stderr)
            return 3
        expected_uid, cache = uid, bound_cache
        report["samples"].append(
            {
                "round": index,
                "directory": str(out),
                "uid": uid,
                "native_game_pass": game_pass,
                "native_difficulty": outcome["settlements"][-1]["difficulty"],
                "elapsed_seconds": time.monotonic() - started,
            }
        )
        save("running")
        if index < args.rounds and args.gap:
            time.sleep(args.gap)
    save("complete")
    print(
        f"Completed{len(report['samples'])} terminal samples; ending audit remains separate",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
