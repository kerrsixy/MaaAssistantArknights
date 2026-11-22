"""Run guarded SPA observation and actions on the explicitly authorized client."""

import argparse
import json
import os
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from spa_checkpoint import (
    committed_shop_level,
    merge_receipt,
    setup_context_receipt,
    setup_pact_receipt,
    verified_lifecycle_receipt,
)
from spa_deployment_audit import audit_deployment_ledger
from spa_joint_acceptance import audit_joint_run
from spa_staged_acceptance import STRATEGY_REPORT_PLUGIN, audit_run, execution_scope
from spa_device_guard import authorize_client
from spa_host_completion import audit_host_completion, require_host_completion
from spa_relocation import relocation_options, relocation_verified
from spa_task_timeout import AutoPreparationProgress, task_timeout_seconds


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--core-dir", type=Path, required=True)
    parser.add_argument("--adb", type=Path, required=True)
    parser.add_argument("--address", required=True)
    parser.add_argument(
        "--client-type", choices=["Bilibili", "Official"], default="Bilibili"
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--task",
        choices=[
            "observe",
            "navigate-lobby-step",
            "back-difficulty",
            "select-joint-mode",
            "open-joint-difficulty",
            "auto-preparation",
            "open-independent-difficulty",
            "open-independent-start",
            "prepare-independent",
            "select-difficulty",
            "start-independent",
            "setup-pacts",
            "strategy-card",
            "setup-flow",
            "setup-remaining",
            "formation",
            "inspect-strategy",
            "use-strategy",
            "buy",
            "upgrade",
            "ready",
            "swap",
            "move",
            "withdraw",
            "deploy",
            "face",
            "equip",
            "inspect",
            "terrain-probe",
            "refresh",
            "supply",
            "bounty",
            "sell",
            "sync",
            "startup",
            "open-lobby",
            "reconnect-solo",
            "abandon-home",
            "return-result-home",
            "open-report",
            "open-strategy-report",
            "scan-strategy-report",
            "report-back",
        ],
        default="observe",
    )
    parser.add_argument(
        "--difficulty", type=int, choices=[-1, *range(1, 21), 2147483647]
    )
    parser.add_argument(
        "--simulation-mode",
        choices=["independent", "quick", "joint"],
        default="independent",
    )
    parser.add_argument("--core-pact")
    parser.add_argument("--support-pact", action="append", default=[])
    parser.add_argument(
        "--strategy-index",
        type=int,
        choices=range(-1, 16),
        default=0,
        help="strategy card slot 0..15, or -1 to choose any currently available card",
    )
    parser.add_argument("--formation-operator", action="append", default=[])
    parser.add_argument("--swap-name")
    parser.add_argument("--replaced-name")
    parser.add_argument("--source", type=int, nargs=2)
    parser.add_argument("--target", type=int, nargs=2)
    parser.add_argument("--drag-target", type=int, nargs=2)
    parser.add_argument("--swap-origin", type=int, nargs=2)
    parser.add_argument("--drag-origin", type=int, nargs=2)
    parser.add_argument("--finalize-direction", action="store_true")
    parser.add_argument("--preserve-population", action="store_true")
    parser.add_argument("--relocation-file", type=Path)
    parser.add_argument("--find-bench-source", action="store_true")
    parser.add_argument("--direction", choices=["E", "W", "N", "S"], default="E")
    parser.add_argument(
        "--inspect-tab", choices=["特质", "技能", "特性", "天赋"], default="特质"
    )
    parser.add_argument("--probe-drag-pixels", type=int, choices=range(33), default=0)
    parser.add_argument("--layers-before", type=int)
    parser.add_argument("--pact-baseline", action="append", default=[])
    parser.add_argument("--target-elite", action="store_true")
    parser.add_argument("--confirm-selection", action="store_true")
    parser.add_argument("--policy-file", type=Path)
    parser.add_argument(
        "--setup-receipt",
        type=Path,
        help="verified setup-receipt.json whose confirmed formation is merged into auto-preparation roster",
    )
    parser.add_argument("--buy-name")
    parser.add_argument("--price", type=int)
    parser.add_argument("--sale-credit", type=int)
    parser.add_argument("--funds", type=int)
    parser.add_argument("--round", type=int)
    parser.add_argument("--right-slot", type=int, choices=range(6))
    parser.add_argument("--remaining-deployment", type=int)
    parser.add_argument("--layout-reviewed", action="store_true")
    parser.add_argument("--no-wait", action="store_true")
    parser.add_argument("--ready-funds", type=int, default=0)
    parser.add_argument("--spending-reviewed", action="store_true")
    parser.add_argument("--spending-reason", default="")
    args = parser.parse_args()
    relocation = None
    if args.find_bench_source and args.task != "deploy":
        parser.error("bench-source discovery is available only for guarded deployment")
    if args.relocation_file:
        if (
            args.task != "face"
            or not args.preserve_population
            or args.source != args.target
        ):
            parser.error(
                "registered relocation requires face, preserve-population and source=target"
            )
        relocation = relocation_options(
            json.loads(args.relocation_file.read_text(encoding="utf-8-sig")),
            args.swap_name,
            args.source,
        )
    if args.task == "ready" and (
        not 0 <= args.ready_funds <= 999
        or (
            args.ready_funds > 0
            and (not args.spending_reviewed or not args.spending_reason)
        )
    ):
        parser.error(
            "ready with remaining funds requires a reviewed budget and explicit reason"
        )
    pact_baselines = []
    seen_pacts = set()
    for entry in args.pact_baseline:
        pact, separator, digits = entry.partition("=")
        if (
            not separator
            or not pact
            or not digits.isascii()
            or not digits.isdigit()
            or pact in seen_pacts
        ):
            parser.error("pact-baseline requires unique PACT=INTEGER entries")
        value = int(digits)
        if not 0 <= value <= 999:
            parser.error("pact-baseline must be within 0..999")
        seen_pacts.add(pact)
        pact_baselines.append({"pact": pact, "layers": value})
    if (
        args.task in ("setup-pacts", "setup-flow", "setup-remaining")
        and not args.core_pact
    ):
        parser.error("setup-pacts/setup-flow/setup-remaining requires --core-pact")
    if args.task == "setup-flow" and args.difficulty is None:
        parser.error("setup-flow requires --difficulty")
    if args.task == "auto-preparation" and args.policy_file is None:
        parser.error("auto-preparation requires --policy-file")
    if args.task == "use-strategy" and (
        any(
            v is None
            for v in (
                args.swap_name,
                args.replaced_name,
                args.source,
                args.target,
                args.round,
                args.funds,
                args.remaining_deployment,
            )
        )
        or (
            args.layers_before is None
            and not pact_baselines
            and args.replaced_name != "全场聚焦"
        )
    ):
        parser.error(
            "use-strategy requires strategy/target identities, coordinates, state and layer baseline"
        )
    if args.task in ("supply", "bounty") and not args.buy_name:
        parser.error("supply requires --buy-name")
    if args.task in ("deploy", "face", "move") and any(
        v is None
        for v in (
            args.swap_name,
            args.source,
            args.target,
            args.round,
            args.funds,
            args.remaining_deployment,
        )
    ):
        parser.error(
            "deploy requires name, coordinates, round, funds and remaining deployment"
        )
    if (
        args.task == "deploy"
        and args.remaining_deployment is not None
        and args.remaining_deployment <= 0
    ):
        parser.error("full formation requires swap with the outgoing operator identity")
    if args.task == "move" and args.drag_origin is None:
        parser.error("move requires --drag-origin for failed-hint recovery")
    if args.task == "sell" and (
        args.sale_credit is None or not 1 <= args.sale_credit <= 99
    ):
        parser.error("sell requires --sale-credit within 1..99")
    if args.task in ("withdraw", "inspect", "terrain-probe", "sell") and any(
        v is None
        for v in (
            args.swap_name,
            args.source,
            args.round,
            args.funds,
            args.remaining_deployment,
        )
    ):
        parser.error(
            "unit action requires --swap-name, --source, --round, --funds and --remaining-deployment"
        )
    if args.task in ("swap", "equip") and any(
        v is None
        for v in (
            args.swap_name,
            args.replaced_name,
            args.source,
            args.target,
            args.round,
            args.funds,
            args.remaining_deployment,
        )
    ):
        parser.error(
            "swap requires identities, source/target coordinates, round, funds and remaining deployment"
        )
    if args.task == "ready" and (
        args.round is None
        or args.remaining_deployment is None
        or not args.layout_reviewed
    ):
        parser.error(
            "ready requires --round, --remaining-deployment and --layout-reviewed"
        )
    if args.task in ("upgrade", "refresh") and any(
        v is None for v in (args.price, args.funds, args.round)
    ):
        parser.error("upgrade requires --price, --funds and --round")
    if args.task == "buy" and any(
        v is None for v in (args.buy_name, args.price, args.funds, args.round)
    ):
        parser.error("buy requires --buy-name, --price, --funds and --round")
    if (
        args.task in ("prepare-independent", "select-difficulty", "start-independent")
        and args.difficulty is None
    ):
        parser.error("prepare-independent requires --difficulty")
    policy = (
        json.loads(args.policy_file.read_text(encoding="utf-8-sig"))
        if args.task == "auto-preparation"
        else None
    )
    try:
        task_timeout = task_timeout_seconds(args.task, policy)
    except ValueError as error:
        parser.error(str(error))
    activity = subprocess.run(
        [
            str(args.adb),
            "-s",
            args.address,
            "shell",
            "dumpsys",
            "activity",
            "activities",
        ],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=20,
        check=True,
    ).stdout
    resumed = [line for line in activity.splitlines() if "topResumedActivity=" in line]
    window = subprocess.run(
        [str(args.adb), "-s", args.address, "shell", "dumpsys", "window"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=20,
        check=True,
    ).stdout
    focused = [
        line
        for line in window.splitlines()
        if "mCurrentFocus=" in line and "=null" not in line
    ]
    package = authorize_client(args.client_type, resumed, focused, policy)

    repo = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(repo / "src" / "Python"))
    from asst.asst import Asst
    from asst.utils import Message

    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / "device.json").write_text(
        json.dumps(
            {
                "address": args.address,
                "client_type": args.client_type,
                "package": package,
                "resumed_activity": resumed,
                "focused_window": focused,
            },
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )
    observations = []
    events = []
    setup_receipt = {}
    checkpoint_context = {
        key: policy[key]
        for key in (
            "run_id",
            "mode",
            "difficulty",
            "selected_strategy_id",
            "occupied_strategy_ids",
            "disabled_pacts",
            "enabled_core_pacts",
            "boss_family_name",
            "boss_advertisement_verified",
        )
        if isinstance(policy, dict) and key in policy
    }

    def persist_json(name, value):
        if (
            name == "result-audit.json"
            and isinstance(policy, dict)
            and policy.get("assisted_run") is True
        ):
            value = {
                **value,
                "assisted_run": True,
                "autonomous_victory_candidate": False,
            }
        temporary = args.output / (name + ".tmp")
        with temporary.open("w", encoding="utf-8") as target:
            json.dump(value, target, ensure_ascii=False, indent=2)
            target.flush()
            os.fsync(target.fileno())
        temporary.replace(args.output / name)

    def persist_checkpoint(receipt):
        checkpoint_path = args.output / "recovery-state.json"
        previous = {}
        if checkpoint_path.exists():
            try:
                previous = json.loads(checkpoint_path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                pass
        merged = merge_receipt(previous, {**checkpoint_context, **receipt})
        if "roster" not in merged and isinstance(policy, dict):
            merged["roster"] = policy.get("roster", [])
        persist_json("recovery-state.json", merged)

    progress = (
        AutoPreparationProgress(policy.get("run_count", 1))
        if args.task == "auto-preparation"
        else None
    )

    @Asst.CallBackType
    def callback(message, details, _):
        observed_at = time.monotonic()
        timestamp = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
        value = json.loads(details.decode("utf-8"))
        event = {"timestamp": timestamp, "message": message, "details": value}
        events.append(event)
        # Preserve committed actions even if the host stops before finally runs.
        # Recovery must still re-observe the game before replaying any action.
        with (args.output / "events.jsonl").open("a", encoding="utf-8") as journal:
            journal.write(json.dumps(event, ensure_ascii=False) + "\n")
            journal.flush()
            os.fsync(journal.fileno())
        if progress is not None:
            progress.observe(value, observed_at)
        context_update = setup_context_receipt(
            value.get("what", ""), value.get("details", {})
        )
        if context_update and checkpoint_context.get("run_id"):
            checkpoint_context.update(context_update)
            # Native batch callbacks are scoped to a child simulation. Using
            # the parent policy ID here would erase that child's checkpoint.
            scope_run_id = value.get("run_id") or checkpoint_context["run_id"]
            persist_checkpoint({"run_id": scope_run_id})
        upgraded_level = committed_shop_level(value.get("what", ""), value.get("details", {}))
        if upgraded_level is not None and value.get("run_id"):
            persist_checkpoint({"run_id": value["run_id"], "shop_level": upgraded_level})
        if value.get("what") in (
            "SPAAutomationPurchaseReconciled",
            "SPAAutomationDeploymentReconciled",
            "SPAPromotionReconciled",
            "SPAReplacementReconciled",
            "SPAReplacementOriginalPreserved",
            "SPAAutomationSaleReconciled",
            "SPASessionRecoveryRequired",
            "SPAShopEquipmentPurchaseReconciled",
            "SPAAutomationEquipmentReconciled",
            "SPACashEquipmentDeferred",
            "SPAAutomationRewardSelected",
            "SPATacticalRecruitmentDeferred",
            "SPATacticalRecruitmentReconciled",
            "SPAStrategyCopyDeferred",
            "SPAStrategyCopyVerified",
            "SPAStrategyCopyUnchanged",
            "SPASupplySelectionPending",
            "SPAEquipmentAssignmentDeferred",
            "SPAReplacementDeferred",
            "SPASpatialOrientationPending",
            "SPASpatialOrientationReconciled",
            "SPASpatialOrientationDeferred",
            "SPAAutomationEquipmentInventoryObserved",
            "SPABossAdvertised",
            "SPASpatialGridSnapshotRefreshed",
            "SPAPactRecruitmentPending",
            "SPAPreparationCompletedBeforeBattle",
            "SPADelayedCashEquipmentPending",
            "SPADelayedCashFundsObserved",
            "SPADirectPromotionPending",
            "SPADirectPromotionObserved",
            "SPADirectPromotionReconciled",
            "SPABenchPopulationReconciled",
        ):
            receipt = value.get("details", {})
            if receipt.get("run_id") and isinstance(receipt.get("roster"), list):
                persist_checkpoint(receipt)
        if value.get("what") == "SPAAutomationDeploymentDeferred":
            # A failed guarded drag is intentionally suppressed for the
            # current preparation round. Persist that bounded suppression so
            # a continuation process does not replay the same bench card
            # from the first empty slot again.
            details = value.get("details", {})
            name = details.get("name")
            round_number = details.get("round")
            if isinstance(name, str) and name:
                blocked_path = args.output / "deployment-blocked.json"
                blocked = {"names": [], "round": round_number}
                if blocked_path.exists():
                    try:
                        loaded = json.loads(
                            blocked_path.read_text(encoding="utf-8-sig")
                        )
                        if isinstance(loaded, dict):
                            blocked = loaded
                    except (OSError, json.JSONDecodeError):
                        pass
                names = blocked.get("names", [])
                if not isinstance(names, list):
                    names = []
                if name not in names:
                    names.append(name)
                blocked["names"] = names
                if isinstance(round_number, int):
                    blocked["round"] = round_number
                persist_json("deployment-blocked.json", blocked)
        if value.get("what") in (
            "SPAAutomationRerollRequired",
            "SPARunSetupBlocked",
        ):
            persist_json("reroll-recommendation.json", value.get("details", {}))
        if value.get("what") == "SPAPromotionReconciliationRequired":
            persist_json("promotion-audit.json", value.get("details", {}))
        if value.get("what") in (
            "SPARunSetupVerified",
            "SPAStrategyCardSelected",
            "SPAFormationSelected",
            "SPAAutomationPrimaryPactSelected",
            "SPARunSetupProvisional",
            "SPABossAdvertised",
        ):
            details = value.get("details", {})
            if value.get("what") == "SPABossAdvertised":
                setup_receipt["boss_family_name"] = details.get("boss_family_name", "")
                setup_receipt["boss_advertisement_verified"] = (
                    details.get("boss_advertisement_verified") is True
                )
                setup_receipt["boss_variant_verified"] = False
            elif value.get("what") == "SPAAutomationPrimaryPactSelected":
                setup_receipt["primary_pact_selection"] = details
                setup_receipt["primary_pact"] = details.get("primary_pact", "")
            elif value.get("what") == "SPARunSetupVerified":
                setup_receipt.update(setup_pact_receipt(details))
            elif value.get("what") == "SPAStrategyCardSelected":
                setup_receipt["strategy_index"] = details.get("strategy_index", -1)
                if details.get("strategy_id"):
                    setup_receipt["selected_strategy_id"] = details["strategy_id"]
                    setup_receipt["strategy_name"] = details.get("strategy_name", "")
                    setup_receipt["strategy_leader"] = details.get(
                        "strategy_leader", ""
                    )
            else:
                setup_receipt["formation_operators"] = details.get("operators", [])
                setup_receipt["formation_confirmed"] = details.get(
                    "confirmation_clicked", False
                )
                setup_receipt["formation_battle_authorized"] = details.get(
                    "battle_authorized", False
                )
                setup_receipt["formation_activation_verified"] = details.get(
                    "activation_verified", False
                )
                setup_receipt["provisional_formation"] = details.get(
                    "provisional_formation",
                    value.get("what") == "SPARunSetupProvisional",
                )
            setup_receipt["last_event"] = value.get("what", "")
            persist_json("setup-receipt.json", setup_receipt)
        if value.get("what") == "SPAObservation":
            observations.append(value["details"])

    if not Asst.load(args.core_dir, user_dir=args.output):
        raise RuntimeError("Debug core or resource loading failed")
    instance = Asst(callback)
    try:
        if not instance.connect(str(args.adb), args.address):
            raise RuntimeError("MAA device connection failed")
        task = {
            "observe": "MiniGame@SPA@Observe",
            "navigate-lobby-step": "MiniGame@SPA@LifecycleFindLobby",
            "back-difficulty": "MiniGame@SPA@LifecycleDifficultyBack",
            "select-joint-mode": "MiniGame@SPA@LifecycleSelectJoint",
            "open-joint-difficulty": "MiniGame@SPA@LifecycleOpenJoint",
            "auto-preparation": "StrongholdProtocol",
            "open-independent-difficulty": "MiniGame@SPA@OpenIndependentDifficulty",
            "open-independent-start": "MiniGame@SPA@OpenIndependentStart",
            "prepare-independent": "MiniGame@SPA@PrepareIndependent",
            "select-difficulty": "MiniGame@SPA@SelectDifficulty",
            "start-independent": "MiniGame@SPA@StartIndependent",
            "setup-pacts": "MiniGame@SPA@SetupPacts",
            "strategy-card": "MiniGame@SPA@SelectStrategy",
            "setup-flow": "MiniGame@SPA@SetupPacts",
            "setup-remaining": "MiniGame@SPA@SetupPacts",
            "formation": "MiniGame@SPA@SelectFormation",
            "inspect-strategy": "MiniGame@SPA@InspectCurrentStrategy",
            "use-strategy": "MiniGame@SPA@SwapOperator",
            "buy": "MiniGame@SPA@BuyShopItem",
            "upgrade": "MiniGame@SPA@UpgradeShop",
            "ready": "MiniGame@SPA@ReadyRound",
            "swap": "MiniGame@SPA@SwapOperator",
            "move": "MiniGame@SPA@SwapOperator",
            "withdraw": "MiniGame@SPA@SwapOperator",
            "deploy": "MiniGame@SPA@SwapOperator",
            "face": "MiniGame@SPA@SwapOperator",
            "equip": "MiniGame@SPA@SwapOperator",
            "inspect": "MiniGame@SPA@SwapOperator",
            "terrain-probe": "MiniGame@SPA@SwapOperator",
            "refresh": "MiniGame@SPA@RefreshShop",
            "supply": "MiniGame@SPA@SelectSupply",
            "bounty": "MiniGame@SPA@SelectBounty",
            "sell": "MiniGame@SPA@SwapOperator",
            "sync": "MiniGame@SPA@AckSync",
            "startup": "StartUp",
            "open-lobby": "MiniGame@SPA@OpenLobby",
            "reconnect-solo": "MiniGame@SPA@ReconnectSolo",
            "abandon-home": "MiniGame@SPA@ForceExit",
            "return-result-home": "MiniGame@SPA@ReturnResultHome",
            "open-report": "MiniGame@SPA@OpenReport",
            "open-strategy-report": "MiniGame@SPA@OpenStrategyReport",
            "scan-strategy-report": "MiniGame@SPA@ScanStrategyReport",
            "report-back": "MiniGame@SPA@ReportBack",
        }[args.task]
        task_names = (
            [
                "MiniGame@SPA@OpenIndependentStart",
                "MiniGame@SPA@OpenIndependentDifficulty",
                "MiniGame@SPA@SelectDifficulty",
                "MiniGame@SPA@StartIndependent",
                "MiniGame@SPA@SetupPacts",
                "MiniGame@SPA@SelectStrategy",
                "MiniGame@SPA@SelectFormation",
            ]
            if args.task == "setup-flow"
            else (
                [
                    "MiniGame@SPA@SetupPacts",
                    "MiniGame@SPA@SelectStrategy",
                    "MiniGame@SPA@SelectFormation",
                ]
                if args.task == "setup-remaining"
                else (
                    [
                        "MiniGame@SPA@ReturnResultHome",
                    ]
                    if args.task == "return-result-home"
                    else [task]
                )
            )
        )
        params = {"task_names": task_names}
        if args.task == "bounty":
            params["params"] = {
                "spa": {
                    "bounty_enemy": args.buy_name,
                    "bounty_confirm": args.confirm_selection,
                }
            }
        if args.task == "supply":
            params["params"] = {"spa": {"supply_name": args.buy_name}}
        if args.task == "strategy-card":
            params["params"] = {"spa": {"strategy_index": args.strategy_index}}
        if args.task == "formation":
            if not args.formation_operator:
                parser.error("formation requires --formation-operator")
            params["params"] = {
                "spa": {
                    "formation_operators": args.formation_operator,
                    "formation_core_pact": args.core_pact or "",
                }
            }
        if args.task in ("setup-flow", "setup-remaining"):
            formation_operators = args.formation_operator or ["auto"]
            params["params"] = {
                "spa": {
                    "setup": {
                        "core_pact": args.core_pact,
                        "support_pacts": args.support_pact,
                        "strategy_index": args.strategy_index,
                    },
                    "strategy_index": args.strategy_index,
                    "formation_operators": formation_operators,
                    "formation_core_pact": args.core_pact,
                }
            }
            if args.task in ("setup-flow", "setup-remaining"):
                params["params"]["spa"]["difficulty"] = args.difficulty
        if args.task == "setup-pacts":
            params["params"] = {
                "spa": {
                    "setup": {
                        "core_pact": args.core_pact,
                        "support_pacts": args.support_pact,
                        "strategy_index": args.strategy_index,
                    }
                }
            }
        if args.task in (
            "prepare-independent",
            "select-difficulty",
            "start-independent",
        ):
            params["params"] = {
                "spa": {"difficulty": args.difficulty, "mode": args.simulation_mode}
            }
        if args.task in ("withdraw", "inspect", "terrain-probe", "sell"):
            args.target = [-1, -1]
        if args.task in (
            "swap",
            "move",
            "withdraw",
            "deploy",
            "face",
            "equip",
            "inspect",
            "terrain-probe",
            "sell",
            "use-strategy",
        ):
            params["params"] = {
                "spa": {
                    "swap": {
                        "strategy_only": args.task == "use-strategy",
                        "layers_before": args.layers_before
                        if args.layers_before is not None
                        else -1,
                        "pact_baselines": pact_baselines,
                        "target_elite": args.target_elite,
                        "withdraw_only": args.task == "withdraw",
                        "deploy_only": args.task == "deploy",
                        "find_bench_source": args.find_bench_source,
                        "move_only": args.task == "move",
                        "finalize_direction": args.finalize_direction,
                        "face_only": args.task == "face",
                        "equip_only": args.task == "equip",
                        "inspect_only": args.task == "inspect",
                        "terrain_probe_only": args.task == "terrain-probe",
                        "probe_drag_pixels": args.probe_drag_pixels,
                        "sell_only": args.task == "sell",
                        "sale_credit": args.sale_credit
                        if args.sale_credit is not None
                        else -1,
                        "inspect_tab": args.inspect_tab,
                        "direction": args.direction,
                        "preserve_population": args.preserve_population,
                        "name": args.swap_name,
                        "replaced": args.replaced_name or "",
                        "source_x": args.source[0],
                        "source_y": args.source[1],
                        "target_x": args.target[0],
                        "target_y": args.target[1],
                        "round": args.round,
                        "funds": args.funds,
                        "remaining_deployment": args.remaining_deployment,
                    }
                }
            }
        if relocation is not None:
            params["params"]["spa"]["swap"].update(relocation)
        if args.task == "move":
            params["params"]["spa"]["swap"].update(
                {
                    "drag_origin_x": args.drag_origin[0],
                    "drag_origin_y": args.drag_origin[1],
                }
            )
        if args.task == "face" and args.swap_origin:
            params["params"]["spa"]["swap"].update(
                {
                    "swap_origin_x": args.swap_origin[0],
                    "swap_origin_y": args.swap_origin[1],
                }
            )
        if (
            args.task in ("swap", "move", "deploy", "equip", "use-strategy")
            and args.drag_target
        ):
            params["params"]["spa"]["swap"].update(
                {
                    "drag_target_x": args.drag_target[0],
                    "drag_target_y": args.drag_target[1],
                }
            )
        if args.task == "buy":
            params["params"] = {
                "spa": {
                    "buy": {
                        "name": args.buy_name,
                        "price": args.price,
                        "funds": args.funds,
                        "round": args.round,
                    }
                }
            }
        if args.task in ("upgrade", "refresh"):
            params["params"] = {
                "spa": {
                    args.task: {
                        "price": args.price,
                        "funds": args.funds,
                        "round": args.round,
                    }
                }
            }
        if args.task == "buy" and args.right_slot is not None:
            params["params"]["spa"]["buy"]["right_slot"] = args.right_slot
        if args.task == "ready":
            params["params"] = {
                "spa": {
                    "ready": {
                        "round": args.round,
                        "remaining_deployment": args.remaining_deployment,
                        "layout_reviewed": args.layout_reviewed,
                        "wait_for_next": not args.no_wait,
                        "expected_funds": args.ready_funds,
                        "spending_reviewed": args.spending_reviewed,
                        "spending_reason": args.spending_reason,
                    }
                }
            }
        if args.task == "startup":
            registered = instance.append_task(
                "StartUp",
                {"client_type": args.client_type, "start_game_enabled": False},
            )
        elif args.task == "auto-preparation":
            policy["client_type"] = args.client_type
            if args.setup_receipt:
                receipt = json.loads(args.setup_receipt.read_text(encoding="utf-8-sig"))
                if receipt.get("strategy_index") is not None and not policy.get(
                    "selected_strategy_id"
                ):
                    policy["selected_strategy_id"] = f"slot:{receipt['strategy_index']}"
                operators = receipt.get("formation_operators", [])
                if not (
                    receipt.get("formation_confirmed", False)
                    and receipt.get("formation_battle_authorized", False)
                    and isinstance(operators, list)
                    and operators
                ):
                    raise RuntimeError(
                        "setup receipt does not prove a confirmed formation and battle HUD"
                    )
                roster = policy.setdefault("roster", [])
                if not isinstance(roster, list):
                    raise RuntimeError("policy roster must be a list")
                existing = {
                    entry.get("name")
                    for entry in roster
                    if isinstance(entry, dict) and entry.get("name")
                }
                for name in operators:
                    if not isinstance(name, str) or not name or name in existing:
                        continue
                    roster.append(
                        {
                            "name": name,
                            "initial_copies": 1,
                            "elite": False,
                            # Formation confirmation only proves that the
                            # operator is in the selectable roster.  It does
                            # not prove that the operator is on a map tile;
                            # the latter is reconciled from the preparation
                            # page's remaining-deployment counter and an
                            # explicit deployment target.
                            "deployed": False,
                            "source": "verified_setup_receipt",
                        }
                    )
                    existing.add(name)
            (args.output / "input-policy.json").write_text(
                json.dumps(policy, ensure_ascii=False, indent=2), encoding="utf-8"
            )
            registered = instance.append_task("StrongholdProtocol", policy)
        else:
            registered = instance.append_task("Custom", params)
        if not registered:
            raise RuntimeError("SPA observer task registration failed")
        if not instance.start():
            raise RuntimeError("SPA observer task did not start")
        # The native task owns all games in a batch. A single game's completion
        # callback must not end host waiting while the parent task is running.
        # Within automatic runs, new verified round/battle evidence can
        # finish an in-flight game beyond the original batch time allowance.
        base_deadline = time.monotonic() + task_timeout
        while instance.running() and time.monotonic() < (
            progress.deadline(base_deadline) if progress is not None else base_deadline
        ):
            time.sleep(0.2)
        if instance.running():
            # The native setup chain can finish its clicks and hand control to
            # the operation/preparation HUD while one of the legacy ProcessTask
            # leaves is still waiting for its old page marker.  That is a
            # harness timeout, not a gameplay failure and must not trigger a
            # reroll or be reported as a non-combat loss.  Stop the stale leaf,
            # then perform an isolated read-only observation before deciding
            # whether the setup actually reached a usable page.
            if args.task in ("setup-flow", "setup-remaining"):
                instance.stop()
                timeout_observe = args.output / "timeout-observe"
                try:
                    retry = subprocess.run(
                        [
                            sys.executable,
                            str(Path(__file__).resolve()),
                            "--core-dir",
                            str(args.core_dir),
                            "--adb",
                            str(args.adb),
                            "--address",
                            args.address,
                            "--client-type",
                            args.client_type,
                            "--output",
                            str(timeout_observe),
                            "--task",
                            "observe",
                        ],
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                        text=True,
                        encoding="utf-8",
                        errors="replace",
                        timeout=120,
                    )
                    observed_file = timeout_observe / "observations.json"
                    if retry.returncode == 0 and observed_file.exists():
                        recovered = json.loads(
                            observed_file.read_text(encoding="utf-8")
                        )
                        if recovered:
                            observations.extend(recovered)
                        recovered_page = (
                            recovered[-1].get("page") if recovered else None
                        )
                        if recovered_page in (
                            "run_setup",
                            "preparation",
                            "battle",
                            "result",
                        ):
                            persist_json(
                                "setup-flow-reached-page.json",
                                {"page": recovered_page, "source": "timeout-observe"},
                            )
                        else:
                            raise RuntimeError(
                                f"SPA setup timed out before a usable page was verified (page={recovered_page})"
                            )
                    else:
                        raise RuntimeError(
                            "SPA setup timed out and read-only recovery failed"
                        )
                except (
                    OSError,
                    subprocess.SubprocessError,
                    json.JSONDecodeError,
                ) as error:
                    raise RuntimeError(
                        "SPA setup timed out and read-only recovery failed"
                    ) from error
            else:
                raise RuntimeError("SPA observer timed out")
        image = instance.get_image(1280 * 720 * 4)
        if image:
            (args.output / "spa_observed.png").write_bytes(image)
        (args.output / "observations.json").write_text(
            json.dumps(observations, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        for observed in observations:
            print(
                json.dumps(
                    {k: v for k, v in observed.items() if k != "texts"},
                    ensure_ascii=False,
                )
            )
        persist_json("deployment-audit.json", audit_deployment_ledger(events))
        staged_audit = None
        if args.task == "auto-preparation":
            # Native trace completeness is distinct from the external input/
            # recovery review used by the staged acceptance ledger.
            staged_audit = audit_run(events, policy, {"run_id": policy.get("run_id")})
            persist_json("staged-run-audit.json", staged_audit)
        if args.task == "auto-preparation" and policy.get("mode") == "joint":
            persist_json(
                "joint-acceptance-audit.json",
                audit_joint_run(events, policy, scope=execution_scope(events, policy)),
            )
        if args.task == "auto-preparation":
            host_completion = audit_host_completion(
                events,
                policy.get("run_count", 1),
                schedule_strategy_badges=policy.get("schedule_strategy_badges") is True,
            )
            persist_json("host-completion-audit.json", host_completion)
            # A previous child's settlement or pending promotion cannot turn a
            # later native failure / incomplete batch into a successful exit.
            # Keep those settlement facts in the audit before raising.
            require_host_completion(host_completion)
            if host_completion["strategy_catalogue_complete"]:
                return
        lifecycle_receipt = verified_lifecycle_receipt(
            events, Message.TaskChainCompleted.value
        )
        if args.task == "auto-preparation" and lifecycle_receipt:
            joint_result = (
                audit_joint_run(events, policy)
                if lifecycle_receipt.get("result_joint")
                else None
            )
            persist_json(
                "result-audit.json",
                {
                    **{
                        key: lifecycle_receipt[key]
                        for key in (
                            "passed_rounds",
                            "result_difficulty",
                            "result_text_verified",
                            "result_completed",
                            "result_interrupted",
                            "return_home_verified",
                            "battle_seen",
                        )
                    },
                    "result_page_verified": True,
                    "result_joint": lifecycle_receipt.get("result_joint", False),
                    "result_difficulty_name": lifecycle_receipt.get(
                        "result_difficulty_name", ""
                    ),
                    "passed_rounds_scope": "alliance" if joint_result else "personal",
                    "personal_passed_rounds": joint_result.get("personal_passed_rounds")
                    if joint_result
                    else lifecycle_receipt["passed_rounds"],
                    "alliance_passed_rounds": lifecycle_receipt["passed_rounds"]
                    if joint_result
                    else None,
                    "valid_victory_candidate": staged_audit[
                        "no_known_intervention_candidate"
                    ],
                    "recovered_intermediate_errors": any(
                        event.get("message")
                        in (Message.SubTaskError.value, Message.TaskChainError.value)
                        for event in events
                    ),
                },
            )
            print(json.dumps(lifecycle_receipt, ensure_ascii=False))
            return
        task_reported_error = any(
            e["message"] in (Message.SubTaskError.value, Message.TaskChainError.value)
            for e in events
        )
        promotion_events = [
            e
            for e in events
            if e.get("details", {}).get("what") == "SPAPromotionReconciliationRequired"
        ]
        if promotion_events:
            # Promotion is a real preparation transition, but the temporary
            # free shop still needs its own identity/tier/confirmation
            # reconciler. Preserve the page and stop without starting battle.
            persist_json(
                "nonterminal-audit.json",
                {
                    **promotion_events[-1].get("details", {}),
                    "phase": "promotion",
                    "combat_result": "not_started",
                    "result_page_verified": False,
                    "resumable": True,
                },
            )
            return
        # A page transition can expose one or more black frames.  A read-only
        # observation must retry that transient state once before declaring the
        # page unknown; the retry is isolated so it cannot replay any action.
        if (
            args.task == "observe"
            and not observations
            and not os.environ.get("SPA_OBSERVE_RETRY")
        ):
            retry_output = args.output / "retry-observe"
            retry_env = os.environ.copy()
            retry_env["SPA_OBSERVE_RETRY"] = "1"
            retry = subprocess.run(
                [
                    sys.executable,
                    str(Path(__file__).resolve()),
                    "--core-dir",
                    str(args.core_dir),
                    "--adb",
                    str(args.adb),
                    "--address",
                    args.address,
                    "--client-type",
                    args.client_type,
                    "--output",
                    str(retry_output),
                    "--task",
                    "observe",
                ],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                env=retry_env,
                timeout=120,
            )
            retry_file = retry_output / "observations.json"
            if retry.returncode == 0 and retry_file.exists():
                try:
                    observations = json.loads(retry_file.read_text(encoding="utf-8"))
                except (OSError, json.JSONDecodeError):
                    observations = []
                (args.output / "observations.json").write_text(
                    json.dumps(observations, ensure_ascii=False, indent=2),
                    encoding="utf-8",
                )
                task_reported_error = False
        setup_blocked = any(
            e.get("details", {}).get("what") == "SPARunSetupBlocked" for e in events
        )
        difficulty_unavailable = next(
            (
                e.get("details", {}).get("details", {})
                for e in events
                if e.get("details", {}).get("what") == "SPADifficultyUnavailable"
            ),
            None,
        )
        # Setup recognition and locked difficulty are recoverable conditions.
        # Preserve the current game; only an explicitly requested abandon-home
        # task may leave an unfinished simulation.
        if args.task in ("setup-flow", "setup-remaining") and (
            setup_blocked or difficulty_unavailable is not None
        ):
            persist_json(
                "setup-recovery-required.json",
                {
                    "blocked": setup_blocked,
                    "difficulty_unavailable": difficulty_unavailable,
                    "last_observation": observations[-1] if observations else {},
                    "resumable": True,
                    "run_abandoned": False,
                    "valid_victory_candidate": False,
                },
            )
        if (
            args.task in ("setup-flow", "setup-remaining")
            and difficulty_unavailable is not None
        ):
            raise RuntimeError(
                "Requested difficulty is unavailable or locked: "
                f"difficulty={difficulty_unavailable.get('difficulty')}, "
                f"reason={difficulty_unavailable.get('reason')}, "
                f"visible_levels={difficulty_unavailable.get('visible_levels', '')}"
            )
        # Cleanup can reach the lobby before the native fallback chain has a
        # chance to emit its final Observe callback.  In that case the native
        # task may report RecognitionFailed even though the device is already
        # safe.  Re-observe the screen in a fresh, read-only instance before
        # deciding that cleanup failed; this never counts as a result or win.
        if args.task == "abandon-home" and task_reported_error and not observations:
            recovery_output = args.output / "post-error-observe"
            recovery = subprocess.run(
                [
                    sys.executable,
                    str(Path(__file__).resolve()),
                    "--core-dir",
                    str(args.core_dir),
                    "--adb",
                    str(args.adb),
                    "--address",
                    args.address,
                    "--client-type",
                    args.client_type,
                    "--output",
                    str(recovery_output),
                    "--task",
                    "observe",
                ],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=120,
            )
            recovery_observations = []
            recovery_file = recovery_output / "observations.json"
            if recovery_file.exists():
                try:
                    recovery_observations = json.loads(
                        recovery_file.read_text(encoding="utf-8")
                    )
                except (OSError, json.JSONDecodeError):
                    recovery_observations = []
            if recovery.returncode == 0 and any(
                observed.get("page") == "lobby" for observed in recovery_observations
            ):
                persist_json(
                    "result-audit.json",
                    {
                        "result_page_verified": False,
                        "result_completed": False,
                        "result_interrupted": False,
                        "result_text_verified": False,
                        "return_home_verified": True,
                        "cleanup_without_settlement": True,
                        "native_cleanup_error": True,
                        "post_error_observation": recovery_observations[-1],
                        "valid_victory_candidate": False,
                    },
                )
                print(json.dumps(recovery_observations[-1], ensure_ascii=False))
                return
        if task_reported_error:
            auth_observations = [
                observed
                for observed in observations
                if observed.get("page") == "authentication_expired"
            ]
            if auth_observations:
                persist_json(
                    "external-blocker.json",
                    {
                        "kind": "authentication_expired",
                        "message": "登录认证已失效，请重新登录",
                        "phase": auth_observations[-1].get("page"),
                        "result_page_verified": False,
                        "combat_result": "not_started",
                        "resumable": True,
                    },
                )
                print(json.dumps(auth_observations[-1], ensure_ascii=False))
                return
            # Setup actions can report a guarded OCR error after their final
            # click has already committed the transition.  In particular,
            # the formation confirmation fades through an ``unknown`` frame
            # while the game is already on the first preparation HUD.  Re-read
            # the live page before turning that into a fatal setup error.
            if args.task in (
                "setup-pacts",
                "setup-remaining",
                "strategy-card",
                "formation",
            ):
                recovery_root = args.output / "post-error-observe"
                recovery_root.mkdir(parents=True, exist_ok=True)
                recovery_observations = []
                for attempt in range(4):
                    probe_root = recovery_root / f"probe-{attempt:02d}"
                    if probe_root.exists():
                        continue
                    try:
                        recovery = subprocess.run(
                            [
                                sys.executable,
                                str(Path(__file__).resolve()),
                                "--core-dir",
                                str(args.core_dir),
                                "--adb",
                                str(args.adb),
                                "--address",
                                args.address,
                                "--client-type",
                                args.client_type,
                                "--output",
                                str(probe_root),
                                "--task",
                                "observe",
                            ],
                            stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL,
                            text=True,
                            encoding="utf-8",
                            errors="replace",
                            timeout=60,
                        )
                    except (OSError, subprocess.TimeoutExpired):
                        recovery = None
                    probe_file = probe_root / "observations.json"
                    if probe_file.exists():
                        try:
                            parsed = json.loads(probe_file.read_text(encoding="utf-8"))
                        except (OSError, json.JSONDecodeError):
                            parsed = []
                        if isinstance(parsed, list):
                            recovery_observations.extend(parsed)
                    if recovery_observations and recovery_observations[-1].get(
                        "page"
                    ) in ("preparation", "battle", "result", "lobby", "run_setup"):
                        break
                    time.sleep(0.5)
                if recovery_observations:
                    last = recovery_observations[-1]
                    if last.get("page") in ("preparation", "battle"):
                        persist_json(
                            "nonterminal-audit.json",
                            {
                                "phase": last.get("page"),
                                "round": last.get("round"),
                                "life": last.get("life"),
                                "combat_result": "not_settled",
                                "result_page_verified": False,
                                "valid_victory_candidate": False,
                                "resumable": True,
                                "setup_task_error": True,
                                "setup_task": args.task,
                            },
                        )
                        print(json.dumps(last, ensure_ascii=False))
                        return
                    if last.get("page") in ("run_setup", "unknown"):
                        persist_json(
                            "setup-transition-audit.json",
                            {
                                "page": last.get("page"),
                                "setup_stage": last.get("setup_stage"),
                                "resumable": True,
                                "setup_task_error": True,
                                "setup_task": args.task,
                            },
                        )
                        print(json.dumps(last, ensure_ascii=False))
                        return
            # StrongholdProtocol is resumable across preparation and battle
            # pages.  A native leaf can report an error after it has already
            # committed the page transition (for example, a deployment
            # candidate was rejected or the bounded action budget expired).
            # Do not turn that non-settlement state into a zero-round loss;
            # persist the live phase and let the next invocation reconcile it.
            if args.task == "auto-preparation" and observations:
                last = observations[-1]
                if last.get("page") in ("preparation", "battle"):
                    persist_json(
                        "nonterminal-audit.json",
                        {
                            "phase": last.get("page"),
                            "round": last.get("round"),
                            "life": last.get("life"),
                            "funds": last.get("funds"),
                            "remaining_deployment": last.get("remaining_deployment"),
                            "combat_result": "not_settled",
                            "result_page_verified": False,
                            "valid_victory_candidate": False,
                            "resumable": True,
                            "native_task_error": True,
                        },
                    )
                    print(json.dumps(last, ensure_ascii=False))
                    return
            raise RuntimeError(
                "SPA task reported an error; task completion alone is not success"
            )
        if args.task == "startup":
            if not any(
                e["message"] == Message.TaskChainCompleted.value
                and e["details"].get("taskchain") == "StartUp"
                for e in events
            ):
                raise RuntimeError("Startup did not report completion")
            return
        if args.task == "scan-strategy-report":
            scans = [
                (index, event["details"])
                for index, event in enumerate(events)
                if event["details"].get("what") == "SPAStrategyReportScan"
                and event["details"].get("class") == STRATEGY_REPORT_PLUGIN
                and event["details"].get("taskchain") == "Custom"
            ]
            if len(scans) != 1:
                raise RuntimeError("Strategy report did not produce one native scan receipt")
            index, scan = scans[0]
            task_id = scan.get("taskid")
            if type(task_id) is not int or not any(
                event["message"] == Message.TaskChainCompleted.value
                and event["details"].get("taskchain") == "Custom"
                and event["details"].get("taskid") == task_id
                for event in events[index + 1 :]
            ):
                raise RuntimeError("Strategy report scan did not complete")
            report = scan["details"]
            # Unreadable intermediate selections do not invalidate the native
            # report traversal. Each identity, win count and badge retains its
            # own evidence, independently of reaching both boundaries.
            coverage_verified = report.get("viewports_scanned", 0) > 0 and (
                report.get("all_strategy_identities_observed") is True
                or (
                    report.get("scrollbar_top_verified") is True
                    and report.get("scrollbar_bottom_verified") is True
                )
            )
            audit = {**report, "viewport_coverage_verified": coverage_verified}
            persist_json("strategy-report-scan.json", audit)
            if not coverage_verified:
                raise RuntimeError("Strategy report viewport coverage was not verified")
            print(json.dumps(audit, ensure_ascii=False))
            return
        if not observations or any(
            o["page"] == "unknown" or o["contradictory"] for o in observations
        ):
            raise RuntimeError("No reliable SPA page observation was produced")
        if args.task == "return-result-home":
            result_indices = [
                index
                for index, observed in enumerate(observations)
                if observed.get("page") == "result"
            ]
            result = observations[result_indices[-1]] if result_indices else None
            if result is None:
                # The return-home task intentionally clicks as soon as its
                # result banner is recognized.  On a fast transition the
                # read-only observer may only capture the lobby afterwards;
                # preserve the native OCR result event as the terminal proof.
                result_events = [
                    e.get("details", {}).get("details", {}).get("result", {})
                    for e in events
                    if e.get("details", {}).get("details", {}).get("task")
                    == "ReturnResultHome"
                    and e.get("details", {})
                    .get("details", {})
                    .get("result", {})
                    .get("text")
                ]
                banner = next(
                    (item for item in reversed(result_events) if item.get("text")), None
                )
                if banner is None:
                    raise RuntimeError(
                        "Result page was not observed before return-home"
                    )
                text = banner.get("text", "")
                result = {
                    "page": "result",
                    "result_text_verified": True,
                    "result_completed": "模拟完成" in text or "完成" in text,
                    "result_interrupted": "模拟中断" in text or "中断" in text,
                    "passed_rounds": 0,
                    "passed_rounds_known": False,
                }
                result_index = -1
            else:
                result_index = result_indices[-1]
            if not result.get("result_text_verified") or not (
                bool(result.get("result_completed"))
                ^ bool(result.get("result_interrupted"))
            ):
                raise RuntimeError(
                    "Result banner was not verified as completed or interrupted"
                )
            tail = (
                observations[result_index + 1 :] if result_index >= 0 else observations
            )
            if not any(observed.get("page") == "lobby" for observed in tail):
                raise RuntimeError(
                    "Return-home transition was not verified by a lobby observation"
                )
            persist_json(
                "result-audit.json",
                {
                    "result_page_verified": True,
                    "result_completed": bool(result.get("result_completed")),
                    "result_interrupted": bool(result.get("result_interrupted")),
                    "result_text_verified": True,
                    "return_home_verified": True,
                    "result_difficulty": result.get("result_difficulty"),
                    "result_joint": result.get("result_joint", False),
                    "result_difficulty_name": result.get("result_difficulty_name", ""),
                    "passed_rounds": result.get("passed_rounds"),
                    # This command saw only the settlement and return home.
                    # It cannot establish an independently executed full run.
                    "valid_victory_candidate": False,
                    "full_run_scope_observed": False,
                },
            )
        if args.task == "abandon-home":
            result_indices = [
                index
                for index, observed in enumerate(observations)
                if observed.get("page") == "result"
            ]
            if not result_indices:
                # Exiting from preparation can return directly to the SPA lobby.
                # This is a safe cleanup outcome, but it is not an interrupted
                # settlement and must never be counted as a result or victory.
                if any(observed.get("page") == "lobby" for observed in observations):
                    persist_json(
                        "result-audit.json",
                        {
                            "result_page_verified": False,
                            "result_completed": False,
                            "result_interrupted": False,
                            "result_text_verified": False,
                            "return_home_verified": True,
                            "cleanup_without_settlement": True,
                            "valid_victory_candidate": False,
                        },
                    )
                else:
                    raise RuntimeError(
                        "Abandon flow did not reach a verified result page or lobby"
                    )
            else:
                result = observations[result_indices[-1]]
                if not result.get("result_text_verified") or not result.get(
                    "result_interrupted"
                ):
                    raise RuntimeError(
                        "Abandon flow did not verify an interrupted settlement"
                    )
                if not any(
                    observed.get("page") == "lobby"
                    for observed in observations[result_indices[-1] + 1 :]
                ):
                    raise RuntimeError("Abandon flow did not return to the SPA lobby")
                persist_json(
                    "result-audit.json",
                    {
                        "result_page_verified": True,
                        "result_completed": False,
                        "result_interrupted": True,
                        "result_text_verified": True,
                        "return_home_verified": True,
                        "result_difficulty": result.get("result_difficulty"),
                        "passed_rounds": result.get("passed_rounds"),
                        "valid_victory_candidate": False,
                    },
                )
        if args.task == "use-strategy" and not any(
            e["details"].get("what")
            in ("SPAActiveStrategyEffectVerified", "SPATemporaryElitePaymentVerified")
            for e in events
        ):
            raise RuntimeError(
                "Active strategy input did not produce a verified effect"
            )
        if args.task == "buy" and not any(
            e["details"].get("what") == "SPAShopPurchase" for e in events
        ):
            raise RuntimeError("No verified shop purchase result was produced")
        if args.task == "bounty" and not any(
            e["details"].get("what")
            == (
                "SPABountySelectionConfirmed"
                if args.confirm_selection
                else "SPABountySelectionIssued"
            )
            for e in events
        ):
            raise RuntimeError("No bounty selection input was issued")
        if args.task == "supply" and not any(
            e["details"].get("what") == "SPASupplySelectionConfirmed" for e in events
        ):
            raise RuntimeError("No confirmed supply selection result was produced")
        if args.task == "inspect" and not any(
            e["details"].get("what") == "SPAUnitInspection" for e in events
        ):
            raise RuntimeError("No identity-verified unit inspection was produced")
        if args.task == "equip" and not any(
            e["details"].get("what")
            in ("SPAGearEquippedVerified", "SPAConsumableEffectVerified")
            for e in events
        ):
            raise RuntimeError("No verified equipment assignment was produced")
        if args.task == "face" and not any(
            relocation_verified(
                e["details"].get("what"), e["details"].get("details", {})
            )
            if relocation is not None
            else e["details"].get("what") == "SPADeploymentVerified"
            for e in events
        ):
            raise RuntimeError("No verified deployment result was produced")
        if args.task == "deploy" and not any(
            e["details"].get("what") == "SPADeploymentVerified"
            and e["details"].get("details", {}).get("direction_verified", False)
            for e in events
        ):
            raise RuntimeError(
                "No identity, population and facing verified deployment was produced"
            )
        if args.task == "withdraw" and not any(
            e["details"].get("what") == "SPAWithdrawalVerified" for e in events
        ):
            raise RuntimeError("No verified withdrawal result was produced")
        if args.task == "terrain-probe" and not any(
            e["details"].get("what") == "SPATerrainProbeCaptured" for e in events
        ):
            raise RuntimeError("No verified terrain probe capture was produced")
        if args.task == "sell" and not any(
            e["details"].get("what") == "SPASaleVerified" for e in events
        ):
            raise RuntimeError("No verified sale credit was produced")
        if args.task == "move" and not any(
            e["details"].get("what") in ("SPAMoveVerified", "SPAMoveDirectionPending")
            for e in events
        ):
            raise RuntimeError(
                "No verified move or explicit direction-pending stage was produced"
            )
        if args.task == "swap" and not any(
            e["details"].get("what") in ("SPASwapVerified", "SPASwapDirectionPending")
            for e in events
        ):
            raise RuntimeError(
                "No verified swap result or explicit direction-pending stage was produced"
            )
        if args.task == "refresh" and not any(
            e["details"].get("what") == "SPAShopRefresh" for e in events
        ):
            raise RuntimeError("No verified shop refresh was produced")
        if args.task == "upgrade" and not any(
            e["details"].get("what") == "SPAShopUpgrade" for e in events
        ):
            raise RuntimeError("No verified shop upgrade payment was produced")
        if args.task in (
            "prepare-independent",
            "select-difficulty",
            "start-independent",
        ):
            unavailable_event = next(
                (
                    e["details"]
                    for e in events
                    if e["details"].get("what") == "SPADifficultyUnavailable"
                ),
                None,
            )
            if unavailable_event is not None:
                details = unavailable_event.get("details", {})
                raise RuntimeError(
                    "Requested difficulty is unavailable or locked: "
                    f"difficulty={details.get('difficulty')}, "
                    f"reason={details.get('reason')}, "
                    f"visible_levels={details.get('visible_levels', '')}"
                )
            verified_event = any(
                e["details"].get("what") == "SPADifficultySelected"
                and e["details"].get("details", {}).get("difficulty") == args.difficulty
                for e in events
            )
            observed_difficulty = (
                observations[-1].get("selected_difficulty") if observations else None
            )
            if args.task == "start-independent":
                # Starting a run legitimately leaves the difficulty page.  The
                # emitted selection event plus a run setup/preparation page is
                # the stable verification boundary for this command.
                destination_ok = observations and observations[-1].get("page") in (
                    "run_setup",
                    "preparation",
                )
                if not (verified_event and destination_ok):
                    raise RuntimeError(
                        "The requested difficulty/start transition was not verified"
                    )
            elif observed_difficulty != args.difficulty and not verified_event:
                raise RuntimeError("The requested difficulty was not verified")
    finally:
        instance.stop()
        (args.output / "events.json").write_text(
            json.dumps(events, ensure_ascii=False, indent=2), encoding="utf-8"
        )


if __name__ == "__main__":
    # Keep redirected settlement output in the same UTF-8 encoding as the journal.
    sys.stdout.reconfigure(encoding="utf-8")
    main()
