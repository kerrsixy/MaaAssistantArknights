"""Phase-1 guarded loop for an already-started solo SPA run.

This intentionally does not choose dynamic pacts, layer engines or equipment
synergies. It resumes from the visible preparation/battle/result page, starts
the next reviewed round, waits for the real battle transition, and records a
checkpoint after every observation. A leak or a final loss is still a valid
run outcome; an OCR/overlay interruption is a recovery state.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path


def run_action(args: argparse.Namespace, action: str, output: Path, extra: list[str] | None = None) -> dict:
    if extra is None:
        extra = []
    # observe_live.py owns creation of each action directory and deliberately
    # rejects pre-existing output paths.  Only ensure the parent exists here.
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        sys.executable,
        str(Path(__file__).with_name("observe_live.py")),
        "--core-dir",
        args.core_dir,
        "--adb",
        args.adb,
        "--address",
        args.address,
        "--output",
        str(output),
        "--task",
        action,
    ] + extra
    completed = subprocess.run(
        command,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
    )
    (output / "runner.stdout.txt").write_text(completed.stdout or "", encoding="utf-8")
    (output / "runner.stderr.txt").write_text(completed.stderr or "", encoding="utf-8")
    if completed.returncode:
        raise RuntimeError(f"{action} failed; inspect {output}")
    for line in reversed(completed.stdout.splitlines()):
        line = line.strip()
        if line.startswith("{") and line.endswith("}"):
            try:
                value = json.loads(line)
                observations_file = output / "observations.json"
                if observations_file.exists():
                    try:
                        saved = json.loads(observations_file.read_text(encoding="utf-8"))
                        if saved:
                            value = saved[-1]
                    except (OSError, json.JSONDecodeError):
                        pass
                return value
            except json.JSONDecodeError:
                pass
    return {}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core-dir", required=True)
    parser.add_argument("--adb", required=True)
    parser.add_argument("--address", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-observations", type=int, default=80)
    args = parser.parse_args()
    args.output.mkdir(parents=False, exist_ok=False)
    checkpoint = args.output / "checkpoint.json"
    rounds = []
    current_round = None

    for index in range(args.max_observations):
        observation_dir = args.output / f"step_{index:03d}"
        observation = run_action(args, "observe", observation_dir)
        rounds.append(observation)
        checkpoint.write_text(
            json.dumps({"last_observation": observation, "observations": rounds}, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )
        page = observation.get("page")
        if page == "result":
            (args.output / "terminal.json").write_text(
                json.dumps({"kind": "result", "observation": observation}, ensure_ascii=False, indent=2),
                encoding="utf-8",
            )
            return 0
        if page == "lobby":
            (args.output / "terminal.json").write_text(
                json.dumps({"kind": "lobby", "observation": observation}, ensure_ascii=False, indent=2),
                encoding="utf-8",
            )
            return 0
        if page == "battle":
            time.sleep(3)
            continue
        if page in ("equipment_supply", "bounty_selection"):
            # Phase 1 still resolves reward pages.  It does not optimize the
            # reward: equipment chooses the first uniquely OCR-visible item
            # from the reviewed current-season catalogue; bounty chooses the
            # first non-generic card token and confirms it in a second pass.
            texts = observation.get("texts", [])
            if page == "equipment_supply":
                catalog_path = Path(__file__).resolve().parents[2] / "resource" / "config" / "MiniGame" / "SPAEquipmentData.json"
                names = {item["name"] for item in json.loads(catalog_path.read_text(encoding="utf-8"))["items"]}
                candidate = next((item["text"] for item in texts if item.get("text") in names), None)
                if candidate:
                    run_action(args, "supply", args.output / f"supply_{index:03d}", ["--buy-name", candidate])
                    continue
            else:
                generic = {"选择悬赏", "确认", "取消", "领取", "悬赏", "敌方"}
                candidate = next(
                    (item["text"] for item in texts if item.get("text") and item["text"] not in generic and len(item["text"]) >= 2),
                    None,
                )
                if candidate:
                    run_action(args, "bounty", args.output / f"bounty_{index:03d}", ["--buy-name", candidate])
                    continue
            (args.output / "recovery-required.json").write_text(
                json.dumps({"kind": "reward-reconcile", "observation": observation}, ensure_ascii=False, indent=2),
                encoding="utf-8",
            )
            return 2
        if page != "preparation":
            (args.output / "recovery-required.json").write_text(
                json.dumps({"kind": "nonterminal", "observation": observation}, ensure_ascii=False, indent=2),
                encoding="utf-8",
            )
            return 2
        round_no = observation.get("round")
        remaining = observation.get("remaining_deployment")
        if not observation.get("round_known") or not observation.get("remaining_deployment_known"):
            (args.output / "recovery-required.json").write_text(
                json.dumps({"kind": "preparation-reconcile", "observation": observation}, ensure_ascii=False, indent=2),
                encoding="utf-8",
            )
            return 2
        current_round = round_no
        ready_dir = args.output / f"ready_{index:03d}"
        try:
            run_action(
                args,
                "ready",
                ready_dir,
                [
                    "--round",
                    str(round_no),
                    "--remaining-deployment",
                    str(remaining),
                    "--layout-reviewed",
                    "--spending-reviewed",
                    "--spending-reason",
                    "phase1 basic loop: no further reviewed purchase",
                    "--ready-funds",
                    str(observation.get("funds", 0)),
                ],
            )
        except RuntimeError:
            # The action log is already durable. Re-observe on the next loop
            # instead of replaying a possibly committed ready click.
            time.sleep(2)
            continue
    (args.output / "recovery-required.json").write_text(
        json.dumps({"kind": "observation-budget", "last_round": current_round}, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
