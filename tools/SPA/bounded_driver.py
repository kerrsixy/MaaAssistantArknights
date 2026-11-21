"""Bounded live research driver for the reviewed solo Amiya lineup.

Uses guarded MaaCore actions, reconciles after every action, and stops for
promotion/reward, equipment, lost life, new shop layouts, and unrecognized pages.
This is a development harness, not the final all-strategy MAA task.
"""

import argparse
import json
import math
import re
import subprocess
import sys
from pathlib import Path


def save(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    temporary.replace(path)


def shop_items(observation, prices):
    result = {}
    for token in observation.get("texts", []):
        score = token["score"]
        if not math.isfinite(score) or score < 0.85:
            continue
        if (
            125 <= token["x"] < 1100
            and 630 <= token["y"] <= 690
            and token["text"] in prices
        ):
            column = (1110 - token["x"]) // 164
            if column <= 5:
                result[column] = token["text"]
    return result


def upgrade_price(observation):
    texts = observation.get("texts", [])
    buttons = [
        t
        for t in texts
        if t["text"] == "升级" and t["score"] >= 0.9 and t["x"] < 450 and t["y"] > 630
    ]
    if len(buttons) != 1:
        return None
    values = {
        int(t["text"])
        for t in texts
        if t["score"] >= 0.85
        and t["text"].isdigit()
        and abs(t["x"] - buttons[0]["x"]) < 70
        and 510 <= t["y"] <= 550
    }
    validated = observation.get("upgrade_price")
    if observation.get("upgrade_price_known"):
        if type(validated) is not int or not 0 <= validated <= 999:
            return None
        values.add(validated)
    return values.pop() if len(values) == 1 else None


def choose_sale(state, funds, required_funds):
    if required_funds <= funds or not state.get("run_id"):
        return None
    valid = []
    seen_sources = set()
    for index, candidate in enumerate(state.get("sale_candidates", [])):
        unit = state["roster"].get(candidate.get("name"), {})
        source = candidate.get("source", [])
        credit = candidate.get("credit")
        if (
            not candidate.get("available")
            or candidate.get("run_id") != state["run_id"]
            or not candidate.get("pacts_preserved_verified")
            or candidate.get("protected", True)
            or candidate.get("equipped_count") != 0
            or not candidate.get("initial_copies_verified")
            or candidate.get("initial_copies") != 1
            or unit.get("bench_support_single")
            or unit.get("initial_copies", 0) >= 2
            or type(credit) is not int
            or not 1 <= credit <= 99
            or funds + credit > 999
            or len(source) != 2
            or any(type(v) is not int for v in source)
            or not (180 <= source[0] < 1000 and 420 <= source[1] < 510)
        ):
            continue
        if tuple(source) in seen_sources:
            continue
        seen_sources.add(tuple(source))
        valid.append((index, candidate))
    if sum(candidate["credit"] for _, candidate in valid) < required_funds - funds:
        return None
    for index, candidate in valid:
        return {
            "task": "sell",
            "sell_name": candidate["name"],
            "source": candidate["source"],
            "credit": candidate["credit"],
            "candidate_index": index,
            "reason": "Convert a reviewed unused bench unit without breaking pacts or promotion targets",
        }
    return None


def decide(state, observation, prices):
    def stop(reason):
        return {"task": "stop", "reason": reason}

    if state.get("review_required"):
        return stop("A previous action requires reconciliation")
    if (
        state.get("strategy") != "众志合一"
        or state.get("mode") != "independent"
        or state.get("difficulty") != 3
    ):
        return stop(
            "This reviewed research policy supports only the current solo difficulty3 Amiya run"
        )
    if observation.get("contradictory") or observation.get("page") != "preparation":
        return stop("A special or uncertain page needs its own executor")
    if not all(
        observation.get(key)
        for key in (
            "round_known",
            "funds_known",
            "remaining_deployment_known",
            "life_known",
        )
    ):
        return stop("Missing authoritative preparation counters")
    round_number, funds = observation["round"], observation["funds"]
    if round_number not in (state["last_round"], state["last_round"] + 1):
        return stop("Unexpected round change")
    if round_number >= state["stop_before_round"]:
        return stop("Reached the reviewed round boundary")
    if observation["life"] < state["last_life"]:
        return stop("Lost life requires a survival and formation review")
    if observation["remaining_deployment"] != 0 or not state.get("lineup_reviewed"):
        return stop("The deployed lineup changed")
    difficulties = {
        int(m.group(1))
        for t in observation.get("texts", [])
        if t["score"] >= 0.85
        and t["x"] < 240
        and t["y"] < 60
        and (m := re.fullmatch(r"规程等级([0-9]+)", t["text"]))
    }
    if difficulties != {state["difficulty"]}:
        return stop("Difficulty identity was not verified")
    if funds > 0 and state.get("bench_occupied", 0) >= 10:
        if sale := choose_sale(state, funds, funds + 1):
            return sale
        return stop(
            "Consolidate the full regular operator bench before further recruitment"
        )
    available = shop_items(observation, prices)
    choices = []
    unaffordable = []
    for column, name in available.items():
        unit = state["roster"].get(name)
        if not unit or unit["elite"] or unit["priority"] <= 0:
            continue
        support = unit.get("bench_support_single", False) or unit.get(
            "single_copy_candidate", False
        )
        if support:
            if unit["initial_copies"] != 0 or not unit.get("effect_verified"):
                continue
        elif not 1 <= unit["initial_copies"] <= 2:
            continue
        cost = prices[name]
        if cost <= funds:
            choices.append(
                (unit["initial_copies"] == 2, unit["priority"], -cost, column, name)
            )
        else:
            unaffordable.append((unit["priority"], cost))
    cost_upgrade = upgrade_price(observation)
    if choices:
        promotion, _, _, column, name = max(choices)
        if (
            promotion
            and state["shop_level"] < 5
            and cost_upgrade is not None
            and state.get("upgrade_preserves_target_verified")
            and cost_upgrade <= funds - prices[name]
        ):
            return {
                "task": "upgrade",
                "price": cost_upgrade,
                "reason": "Raise the free promotion reward tier before buying the third copy",
            }
        return {
            "task": "buy",
            "buy_name": name,
            "right_slot": column,
            "price": prices[name],
            "promotion_expected": promotion,
            "reason": "Improve a reviewed core unit without changing pact membership",
        }
    for _, cost in sorted(unaffordable, reverse=True):
        if sale := choose_sale(state, funds, cost):
            return sale
    if funds == 0:
        if round_number in state.get("battle_review_rounds", []):
            return stop(
                "Review the boss formation and equipment before issuing the final ready action"
            )
        return {
            "task": "ready",
            "reason": "Reviewed unchanged lineup and no unspent funds",
        }
    for column, name in available.items():
        configured = state.get("equipment_candidates", {}).get(name)
        if not configured or prices[name] > funds:
            continue
        candidates = configured if isinstance(configured, list) else [configured]
        for index, gear in enumerate(candidates):
            if not gear.get("effect_verified") or gear.get("owned_or_pending"):
                continue
            target = state["roster"].get(gear["target"], {})
            if not state.get("equipment_slots_reviewed") or target.get(
                "equipped_count"
            ) not in (0, 1):
                continue
            return {
                "task": "buy",
                "buy_name": name,
                "right_slot": column,
                "price": prices[name],
                "is_equipment": True,
                "candidate_index": index,
                "target": gear["target"],
                "reason": "Acquire a reviewed stacking item for a verified free equipment slot",
            }
    # A reviewed progression target prevents spending the entire late-round budget
    # on the same low-tier pool. Promotions and useful visible equipment retain priority.
    milestones = state.get("shop_milestones", [])
    target_level = max(
        (m["level"] for m in milestones if m["round"] <= round_number),
        default=state["shop_level"],
    )
    if state["shop_level"] < min(target_level, 6) and cost_upgrade is None:
        return stop(
            "Read the upgrade price before spending the progression budget on rerolls"
        )
    if (
        state["shop_level"] < min(target_level, 6)
        and cost_upgrade is not None
        and 0 < cost_upgrade <= funds
    ):
        return {
            "task": "upgrade",
            "price": cost_upgrade,
            "reason": "Reach the reviewed shop progression target before rolling the remaining budget",
        }
    wanted_prices = [
        prices[name]
        for name, unit in state["roster"].items()
        if name in prices
        and not unit["elite"]
        and unit["priority"] > 0
        and 1 <= unit["initial_copies"] <= 2
        and not unit.get("bench_support_single")
    ]
    if wanted_prices and funds <= min(wanted_prices):
        if sale := choose_sale(state, funds, min(wanted_prices) + 1):
            return sale
    # Ordinary cash is not carried into the next round.
    return {
        "task": "refresh",
        "price": 1,
        "reason": "No affordable reviewed core; spend one on a verified reroll",
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--core-dir", type=Path, required=True)
    parser.add_argument("--adb", type=Path, required=True)
    parser.add_argument("--address", required=True)
    parser.add_argument("--state", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-actions", type=int, default=30)
    args = parser.parse_args()
    if not 1 <= args.max_actions <= 60:
        parser.error("max-actions must be within 1..60")
    state = json.loads(args.state.read_text(encoding="utf-8"))
    if not state.get("run_id"):
        parser.error("A reviewed run identity is required")
    catalog = json.loads(args.catalog.read_text(encoding="utf-8"))
    prices = {
        entry["name"]: entry["price"]
        for group in ("ops", "weapons")
        for entry in catalog[group]
    }
    args.output.mkdir(parents=True, exist_ok=False)
    history = []

    def execute(action, observation=None):
        folder = args.output / f"step-{len(history):03d}-{action['task']}"
        command = [
            sys.executable,
            "-X",
            "utf8",
            str(Path(__file__).with_name("observe_live.py")),
            "--core-dir",
            str(args.core_dir),
            "--adb",
            str(args.adb),
            "--address",
            args.address,
            "--output",
            str(folder),
            "--task",
            action["task"],
        ]
        if observation:
            command += ["--round", str(observation["round"])]
            if action["task"] == "ready":
                command += ["--remaining-deployment", "0", "--layout-reviewed"]
            elif action["task"] == "sell":
                command += [
                    "--funds",
                    str(observation["funds"]),
                    "--sale-credit",
                    str(action["credit"]),
                    "--swap-name",
                    action["sell_name"],
                    "--source",
                    *map(str, action["source"]),
                    "--remaining-deployment",
                    "0",
                ]
            else:
                command += [
                    "--funds",
                    str(observation["funds"]),
                    "--price",
                    str(action["price"]),
                ]
            if action["task"] == "buy":
                command += [
                    "--buy-name",
                    action["buy_name"],
                    "--right-slot",
                    str(action["right_slot"]),
                ]
        print(json.dumps(action, ensure_ascii=False), flush=True)
        with folder.with_suffix(".console.txt").open("w", encoding="utf-8") as log:
            completed = subprocess.run(
                command, stdout=log, stderr=subprocess.STDOUT, timeout=360
            )
        events_path = folder / "events.json"
        events = (
            json.loads(events_path.read_text(encoding="utf-8"))
            if events_path.is_file()
            else []
        )
        observations = [
            event["details"]["details"]
            for event in events
            if event["details"].get("what") == "SPAObservation"
        ]
        history.append(
            {
                "action": action,
                "exit_code": completed.returncode,
                "events": str(events_path),
            }
        )
        save(args.output / "history.json", history)
        if completed.returncode != 0 or not observations:
            state["review_required"] = True
            state["review_reason"] = (
                "Action or follow-up observation failed; inspect events before issuing another input"
            )
            save(args.state, state)
            return None
        if action["task"] == "sell":
            state["sale_candidates"][action["candidate_index"]]["available"] = False
            state["bench_occupied"] = max(0, state.get("bench_occupied", 0) - 1)
            unit = state["roster"].get(action["sell_name"])
            if unit:
                unit["initial_copies"] = max(0, unit["initial_copies"] - 1)
        elif action["task"] == "buy" and action.get("is_equipment"):
            configured = state["equipment_candidates"][action["buy_name"]]
            candidate = (
                configured[action["candidate_index"]]
                if isinstance(configured, list)
                else configured
            )
            candidate["owned_or_pending"] = True
            state["pending_equipment"] = {
                "name": action["buy_name"],
                "target": action["target"],
            }
            state["review_required"] = True
            state["review_reason"] = (
                "New equipment needs target-panel verification before battle"
            )
        elif action["task"] == "buy":
            state["bench_occupied"] = state.get("bench_occupied", 0) + 1
            unit = state["roster"][action["buy_name"]]
            unit["initial_copies"] += 1
            if unit.get("single_copy_candidate"):
                state["review_required"] = True
                state["review_reason"] = (
                    "Inspect the new combat candidate and verify a legal deployment before continuing"
                )
            elif unit["initial_copies"] == 3:
                state["review_required"] = True
                state["review_reason"] = (
                    "Promotion expected: verify reward, equipment return and lineup before continuing"
                )
        elif action["task"] == "upgrade":
            state["shop_level"] += 1
            state["review_required"] = True
            state["review_reason"] = (
                "Verify the new shop level and preserved target before completing promotion"
            )
        save(args.state, state)
        return observations[-1]

    try:
        observation = execute(
            {"task": "observe", "reason": "Reconcile live state before any input"}
        )
        for _ in range(args.max_actions):
            if observation is None:
                break
            action = decide(state, observation, prices)
            if action["task"] == "stop":
                state["review_required"] = True
                state["review_reason"] = action["reason"]
                save(args.state, state)
                print(json.dumps(action, ensure_ascii=False), flush=True)
                break
            state["last_round"] = observation["round"]
            state["last_life"] = observation["life"]
            save(args.state, state)
            observation = execute(action, observation)
    finally:
        save(args.state, state)
        save(args.output / "history.json", history)


if __name__ == "__main__":
    main()
