"""Offline preparation audit: observed counts, review candidates and explicit guard violations.

Accepts observe_live.py events.jsonl and optional native asst.log files. Sources
are deliberately kept separate: a journal and native log often describe the
same input. No device input, OCR or inferred victory is performed.
"""

import argparse
from collections import Counter, defaultdict
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re

from audit_action_timing import audit_lines as audit_plugin_timing


NATIVE_CALLBACK = re.compile(
    r"^\[([^]]+)\].*?\[Px(\d+)\]\[Tx(\d+)\].*?"
    r"Assistant::append_callback \| (SubTask\w+) (\{.*\})$"
)
CATEGORIES = {
    "SPAEquipmentInventoryInspected": "equipment_discovery_passes",
    "SPAAutomationEquipmentInventoryObserved": "equipment_inventory_receipts",
    "SPAEquipmentInventoryScan": "equipment_target_searches",
    "SPAAutomationShopScan": "shop_offer_scans",
    "SPARegisteredFormationObserved": "field_registrations",
    "SPASpatialGridSnapshotRefreshed": "grid_snapshot_refreshes",
    "SPAFieldGridBootstrapObserved": "field_bootstrap_passes",
    "SPABoardRegistration": "board_registrations",
    "SPAHeldOperatorIdentity": "held_identity_checks",
    "SPABenchSourceVerified": "bench_identity_receipts",
    "SPADeployedPoseObserved": "deployed_pose_receipts",
    "SPAFieldFacingObserved": "field_facing_observations",
    "SPAShopPurchase": "purchase_payment_receipts",
    "SPAAutomationPurchaseReconciled": "operator_purchase_reconciliations",
    "SPAShopEquipmentPurchaseReconciled": "equipment_purchase_reconciliations",
    "SPASaleInputIssued": "sale_input_receipts",
    "SPASaleVerified": "sale_payment_receipts",
    "SPAAutomationSaleReconciled": "sale_reconciliations",
    "SPAAutomationDeploymentDecision": "deployment_decisions",
    "SPADeploymentVerified": "deployment_verification_receipts",
    "SPAAutomationDeploymentReconciled": "deployment_reconciliations",
    "SPAReplacementDecision": "replacement_decisions",
    "SPAReplacementReconciled": "replacement_reconciliations",
    "SPAShopExpandedForPreparation": "shop_preparation_expansion_receipts",
    "SPAShopExpandedForDeployment": "shop_deployment_visibility_receipts",
    "SPAShopCollapsed": "shop_collapse_receipts",
    "SPAJointPreparationDeadline": "preparation_deadline_deferrals",
}
WORK_EVENTS = {
    "SPAEquipmentInventoryInspected", "SPAFieldGridBootstrapObserved",
    "SPASpatialGridSnapshotRefreshed", "SPAAutomationDeploymentDecision",
    "SPAReplacementDecision",
}
MUTATIONS = {
    "SPAShopPurchase", "SPAShopEquipmentPurchase", "SPAAutomationPurchaseReconciled",
    "SPAShopEquipmentPurchaseReconciled", "SPASaleVerified", "SPAAutomationSaleReconciled",
    "SPADeploymentVerified", "SPAAutomationDeploymentReconciled", "SPAReplacementReconciled",
    "SPAPromotionReconciled", "SPAAutomationEquipmentReconciled", "SPAShopRefresh",
    "SPAShopUpgradeReconciled", "SPASpatialOrientationReconciled", "SPARoundStarted",
}
LIMITATIONS = [
    "Each source is audited separately; overlapping native logs and journals must not be added as unique inputs.",
    "Receipts, plans and reconciliations are separate counters, not distinct physical actions.",
    "Missing timestamps do not imply zero duration. Native plugin times include recognition/waits and can overlap.",
    "Known round/run context is inherited only within a source/process. Unknown context is reported explicitly.",
    "Positive funds and affordable offers do not prove a useful purchase exists; shopping-order findings need review.",
    "Missing capacity/deadline data remains unknown; grey cards alone are never interpreted as capacity evidence.",
    "Repeated discovery is a candidate, not proof of waste: external changes and unlogged invalidations may exist.",
    "Registered grid snapshots with fresh_identity_probes=false are not classified as full identity scans.",
    "Shop visibility receipts do not cover every native toggle; no collapse count is inferred from coordinates.",
]


def number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def valid_round(value):
    return isinstance(value, int) and not isinstance(value, bool) and value > 0


def parse_lines(lines, native=False):
    records, malformed = [], 0
    for line_number, line in enumerate(lines, 1):
        if not line.strip():
            continue
        match = NATIVE_CALLBACK.search(line) if native else None
        if native and not match:
            continue
        try:
            raw = json.loads(match[5] if native else line)
            if not isinstance(raw, dict):
                raise ValueError("event must be an object")
            body = raw if native or "what" in raw else raw.get("details", {})
            if not isinstance(body, dict):
                raise ValueError("callback body must be an object")
            details = body.get("details", {})
            if not isinstance(details, dict):
                raise ValueError("callback details must be an object")
        except (ValueError, TypeError):
            malformed += 1
            continue
        stamp = match[1] if native else raw.get("timestamp", raw.get("time"))
        try:
            stamp = datetime.fromisoformat(stamp) if isinstance(stamp, str) else None
        except ValueError:
            stamp = None
        records.append({
            "line": line_number, "what": body.get("what", ""), "details": details,
            "timestamp": stamp, "process": match[2] if native else "journal",
            "native_line": line if native else None,
        })
    return records, malformed


def audit_lines(lines, source="journal", native=False, freshness_events=20, frequent_threshold=2):
    records, malformed = parse_lines(lines, native)
    # One explicitly identified run in a source/process can name its opening
    # records too. Multi-run files retain an unknown prefix before the first ID.
    run_ids = defaultdict(set)
    for record in records:
        run = record["details"].get("run_id")
        if isinstance(run, str) and run:
            run_ids[record["process"]].add(run)
    contexts, groups, candidates, failures = {}, {}, [], []
    for sequence, record in enumerate(records):
        process, data, what = record["process"], record["details"], record["what"]
        if process not in contexts:
            ids = run_ids[process]
            contexts[process] = {"run": next(iter(ids)) if len(ids) == 1 else None, "round": None}
        context = contexts[process]
        explicit_run = data.get("run_id")
        if isinstance(explicit_run, str) and explicit_run and explicit_run != context["run"]:
            context.update(run=explicit_run, round=None)
        observed_round = data.get("round")
        if valid_round(observed_round) and (what != "SPAObservation" or data.get("round_known") is True):
            context["round"] = observed_round
        key = (process, context["run"], context["round"])
        if key not in groups:
            groups[key] = {
                "process": process, "run_id": context["run"], "round": context["round"],
                "counts": Counter(), "event_counts": Counter(), "first_line": record["line"],
                "last_line": record["line"], "timestamps": [], "native_lines": [],
                "explicit_duration_ms": defaultdict(list), "state": {}, "last_scans": {},
                "mutation_epoch": 0, "shopping_work": [], "shopping_order_evidence_gaps": Counter(),
            }
        group = groups[key]
        group["last_line"] = record["line"]
        group["event_counts"][what or "callback_without_what"] += 1
        if what in CATEGORIES:
            group["counts"][CATEGORIES[what]] += 1
        if record["timestamp"]:
            group["timestamps"].append(record["timestamp"])
        if record["native_line"]:
            group["native_lines"].append(record["native_line"])
        for field in ("duration_ms", "elapsed_ms", "inspection_ms", "scan_ms"):
            value = data.get(field)
            if number(value) and value >= 0:
                group["explicit_duration_ms"][what + "." + field].append(value)
        for field in ("fresh_title_probes", "hud_ocr_checks", "hud_cache_hits", "hud_fields_reused"):
            if what == "SPAEquipmentInventoryInspected" and number(data.get(field)) and data[field] >= 0:
                group["counts"]["inventory_" + field] += data[field]
        if what == "SPAShopExpandedForDeployment" and data.get("expanded_before_source_scan") is True:
            group["counts"]["shop_expansion_inputs_reported"] += 1
        if what == "SPAShopCollapsed" and data.get("input_issued") is True:
            group["counts"]["shop_collapse_inputs_reported"] += 1
        if what in ("SPARegisteredFormationObserved", "SPASpatialGridSnapshotRefreshed"):
            if data.get("fresh_identity_probes") is False:
                group["counts"]["grid_receipts_without_fresh_identity_probes"] += 1
            elif data.get("fresh_identity_probes") is True:
                group["counts"]["grid_receipts_with_fresh_identity_probes"] += 1

        state = group["state"]
        # Only explicit values can enable an exception or rule prerequisite.
        for field in ("funds", "bench_capacity_full", "equipment_capacity_full", "shopping_complete", "deadline_emergency"):
            value = data.get(field)
            if (field == "funds" and number(value) and value >= 0) or (field != "funds" and isinstance(value, bool)):
                state[field] = (value, sequence, record["line"])
            elif field in data:
                state.pop(field, None)
        if data.get("funds_known") is False:
            state.pop("funds", None)
        if number(data.get("funds_after")) and data["funds_after"] >= 0:
            state["funds"] = (data["funds_after"], sequence, record["line"])
        if data.get("preparation_seconds_remaining_known") is True and number(data.get("preparation_seconds_remaining")):
            state["seconds_remaining"] = (data["preparation_seconds_remaining"], sequence, record["line"])
        elif data.get("preparation_seconds_remaining_known") is False:
            state.pop("seconds_remaining", None)
        if what in ("SPAJointPreparationDeadline", "SPAJointDeploymentDeadline"):
            state["deadline_emergency"] = (True, sequence, record["line"])
        if what == "SPAAutomationPreparationDeferred":
            # Native Review handoff means no currently executable purchase.
            state["shopping_complete"] = (True, sequence, record["line"])
        if what == "SPAAutomationShopScan":
            offers = data.get("offers", [])
            prices = [offer["price"] for offer in offers if isinstance(offer, dict)
                      and offer.get("catalog_match") is True and number(offer.get("price")) and offer["price"] >= 0] if isinstance(offers, list) else []
            state["offered_prices"] = (prices, sequence, record["line"])

        def fresh(field):
            found = state.get(field)
            return found[0] if found and sequence - found[1] <= freshness_events else None

        full_scan = what == "SPAEquipmentInventoryInspected" or what == "SPAFieldGridBootstrapObserved" or (
            what == "SPASpatialGridSnapshotRefreshed" and data.get("fresh_identity_probes") is True)
        if full_scan and context["round"] is not None:
            previous = group["last_scans"].get(what)
            if previous:
                mutated = previous["epoch"] != group["mutation_epoch"]
                candidates.append({
                    "kind": "repeated_discovery_same_round" if mutated else "repeated_discovery_without_logged_mutation",
                    "source": source, "intervening_mutation_logged": mutated,
                    "run_id": context["run"], "round": context["round"], "process": process,
                    "event": what, "previous_line": previous["line"], "line": record["line"],
                    "previous_candidates_inspected": previous["complete"],
                    "current_candidates_inspected": data.get("detected_candidates_inspected"),
                    "confirmed_invariant_failure": False,
                })
            group["last_scans"][what] = {"epoch": group["mutation_epoch"], "line": record["line"],
                                         "complete": data.get("detected_candidates_inspected")}
        if what in WORK_EVENTS:
            funds, prices, seconds = fresh("funds"), fresh("offered_prices"), fresh("seconds_remaining")
            deadline = fresh("deadline_emergency")
            group["counts"]["shopping_order_work_events_checked"] += 1
            for missing, absent in (
                ("funds", funds is None),
                ("bench_capacity", fresh("bench_capacity_full") is None),
                ("equipment_capacity", fresh("equipment_capacity_full") is None),
                ("deadline", deadline is None and seconds is None),
                ("shopping_status_or_offers", fresh("shopping_complete") is None and prices is None),
            ):
                if absent:
                    group["shopping_order_evidence_gaps"][missing] += 1
            before_shopping = (fresh("shopping_complete") is False or
                               (fresh("shopping_complete") is not True and prices and number(funds)
                                and any(price <= funds for price in prices)))
            if (number(funds) and funds > 0 and before_shopping and fresh("bench_capacity_full") is False
                    and fresh("equipment_capacity_full") is False and
                    (deadline is False or (deadline is not True and number(seconds) and seconds > 25))):
                group["shopping_work"].append({
                    "line": record["line"], "event": what, "funds": funds,
                    "seconds_remaining": seconds, "shopping_complete": fresh("shopping_complete"),
                    "prerequisite_evidence_lines": {field: state[field][2] for field in (
                        "funds", "bench_capacity_full", "equipment_capacity_full", "shopping_complete",
                        "deadline_emergency", "seconds_remaining", "offered_prices") if field in state},
                })
        # This is deliberately narrower than a performance candidate: the
        # deadline receipt itself asserts that its no-new-transaction guard
        # was bypassed. Silence or an unrelated later action is not a failure.
        if (what == "SPAJointPreparationDeadline" and number(data.get("seconds_remaining"))
                and 0 <= data["seconds_remaining"] <= 8 and data.get("new_transaction_issued") is True):
            failures.append({"kind": "deadline_guard_reports_new_transaction", "source": source,
                             "run_id": context["run"], "round": context["round"], "process": process,
                             "line": record["line"], "evidence": data, "confirmed_invariant_failure": True})
        if what in MUTATIONS or data.get("inventory_mutated") is True or data.get("board_changed") is True:
            group["mutation_epoch"] += 1
            if what in MUTATIONS:
                state.pop("shopping_complete", None)
                state.pop("offered_prices", None)
                # Purchases/equips/sales invalidate prior capacity observations.
                state.pop("bench_capacity_full", None)
                state.pop("equipment_capacity_full", None)

    result_groups = []
    for group in groups.values():
        if len(group["shopping_work"]) >= frequent_threshold:
            candidates.append({"kind": "frequent_work_before_shopping_handoff", "source": source,
                               "run_id": group["run_id"], "round": group["round"], "process": group["process"],
                               "events": group["shopping_work"], "confirmed_invariant_failure": False})
        stamps = group.pop("timestamps")
        try:
            span = (max(stamps) - min(stamps)).total_seconds() if len(stamps) >= 2 else None
        except TypeError:
            span = None  # Mixed timezone-aware/naive stamps cannot be compared.
        group["observed_span_seconds"] = span
        native_lines = group.pop("native_lines")
        group["plugin_wall_time"] = audit_plugin_timing(native_lines) if native_lines else None
        group["explicit_duration_ms"] = {key: {"count": len(values), "total": sum(values), "max": max(values)}
                                         for key, values in group["explicit_duration_ms"].items()}
        group["shopping_order_prerequisites_complete_events"] = len(group.pop("shopping_work"))
        for internal in ("state", "last_scans", "mutation_epoch"):
            group.pop(internal)
        result_groups.append(group)
    return {
        "source": source, "format": "native_log" if native else "events_jsonl",
        "parsed_events": len(records), "malformed_lines": malformed,
        "events_with_timestamp": sum(record["timestamp"] is not None for record in records),
        "by_run_round": result_groups, "candidate_issues": candidates,
        "confirmed_invariant_failures": failures, "game_success_verified": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--events", type=Path, action="append", default=[])
    parser.add_argument("--native-log", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not args.events and not args.native_log:
        parser.error("at least one --events or --native-log is required")
    sources = []
    for path, native in [(path, False) for path in args.events] + [(path, True) for path in args.native_log]:
        content = path.read_bytes()
        audit = audit_lines(content.decode("utf-8-sig", errors="replace").splitlines(), str(path.resolve()), native)
        audit["source_sha256"] = hashlib.sha256(content).hexdigest()
        sources.append(audit)
    output = {"schema_version": 1, "sources": sources, "limitations": LIMITATIONS,
              "game_success_verified": False}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"output": str(args.output.resolve()), "sources": len(sources),
                      "candidate_issues": sum(len(item["candidate_issues"]) for item in sources),
                      "confirmed_invariant_failures": sum(len(item["confirmed_invariant_failures"]) for item in sources)},
                     ensure_ascii=False))


if __name__ == "__main__":
    main()
