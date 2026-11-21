"""Audit logged input requests and reconciled spatial failures, without controlling a device."""

import argparse
from collections import Counter
import json
from pathlib import Path
import re


def local_failure_context(key):
    if key.startswith("spatial-local-v2:"):
        return key
    header, separator, members = key.partition("|")
    match = re.fullmatch(r"([^:]+):([^:]+):(\d+)\.(\d+)>(\d+)\.(\d+):([ENSW]):(.*)", header)
    if not separator or not match:
        return None
    source = (int(match[3]), int(match[4]))
    target = (int(match[5]), int(match[6]))
    nearby = []
    for member in members.split("|"):
        entity = re.fullmatch(r"(.+):(\d+)\.(\d+):(elite|initial)", member)
        if not entity:
            return None
        col, row = int(entity[2]), int(entity[3])
        if any(abs(col - x) <= 1 and abs(row - y) <= 1 for x, y in (source, target)):
            nearby.append(member)
    return "legacy-local:" + header + "|" + "|".join(sorted(nearby))


def receiver_failure_context(pending):
    if pending.get("receiver_fingerprint"):
        return pending["receiver_fingerprint"]
    header, separator, members = pending.get("attempt_fingerprint", "").partition("|")
    parts = header.split(":")
    if len(parts) != 7 or parts[0] != "spatial-local-v2" or parts[5] not in "ENSW" or not parts[5]:
        return None
    parts[0], parts[5] = "spatial-receiver-v1", "E"
    return ":".join(parts) + (separator + members if separator else "")


def audit_lines(lines):
    rounds = {}
    round_number = 0
    pending = None
    failed = set()
    repeats = Counter()
    local_failed = set()
    local_repeats = Counter()
    spatial = []
    unknown_east_plans = 0
    egir_unknown_east_plans = 0
    explicit_egir_plan = False
    observed_facings = 0
    malformed = 0
    last_bench_probe = None
    nearby_bench_duplicates = []
    marginal_layouts_deferred = []
    receiver_failed = set()
    repeated_receiver_failures = []
    pending_receiver_failed = False
    for line_number, line in enumerate(lines, 1):
        # Two hitboxes within half a bench slot returning the same identity
        # in one search are a duplicate-candidate signal. Same-name cards a
        # full slot apart, and revalidation after a transaction, are separate.
        if "SPA held drag begin" in line:
            last_bench_probe = None
        bench_match = re.search(r"SPA bench identity (.+?) (\d+) (\d+)\s*$", line)
        if bench_match:
            probe = {"name": bench_match[1], "x": int(bench_match[2]), "y": int(bench_match[3]),
                     "line": line_number, "round": round_number}
            if (last_bench_probe and probe["name"] == last_bench_probe["name"]
                    and probe["round"] == last_bench_probe["round"]
                    and abs(probe["y"] - last_bench_probe["y"]) <= 4
                    and 10 <= abs(probe["x"] - last_bench_probe["x"]) < 40):
                nearby_bench_duplicates.append({"previous": last_bench_probe, "repeated": probe})
            last_bench_probe = probe
        row = rounds.setdefault(str(round_number), {"click_requests": 0, "swipe_requests": 0})
        # Controller and minitouch log the same request at two layers. Count
        # only the controller entry; held-touch phases are not covered here.
        if "Click with scaled coordinates " in line:
            row["click_requests"] += 1
        if "Swipe with scaled coordinates " in line:
            row["swipe_requests"] += 1
        marker = "Assistant::append_callback | SubTaskExtraInfo "
        if marker not in line:
            continue
        try:
            event = json.loads(line.split(marker, 1)[1])
        except (ValueError, TypeError):
            malformed += 1
            continue
        details = event.get("details", {})
        what = event.get("what")
        if what in ("SPAShopPurchase", "SPAShopEquipmentPurchase", "SPAPromotionReconciled", "SPAReplacementReconciled"):
            last_bench_probe = None
        if what == "SPADynamicLayoutMarginalGainDeferred" and details.get("input_issued") is False:
            marginal_layouts_deferred.append(details)
        if (what == "SPAObservation" and details.get("round_known")
                and isinstance(details.get("round"), int) and details["round"] > 0):
            round_number = details["round"]
        elif what == "SPARoundStarted" and isinstance(details.get("round"), int) and details["round"] > 0:
            round_number = details["round"]
        if what == "SPAEgirFormationPlanned":
            explicit_egir_plan = details.get("found") is True
        elif what == "SPAEgirFormationAnalysis" and not details.get("pact_active_by_member_model"):
            explicit_egir_plan = False
        elif what == "SPAFieldFacingObserved" and details.get("extra_direction_input_issued") is False:
            observed_facings += len(details.get("members", []))
        elif what == "SPADynamicLayoutStep":
            unknown_east = (
                not details.get("relocation", False)
                and details.get("previous") in ("", "\0")
                and details.get("direction") == "E"
            )
            unknown_east_plans += unknown_east and not explicit_egir_plan
            egir_unknown_east_plans += unknown_east and explicit_egir_plan
        elif what == "SPASpatialOrientationPending":
            candidate = details.get("pending_orientation")
            if candidate and details.get("phase") == "before_held_input":
                pending = candidate
                pending_receiver_failed = False
                receiver_key = receiver_failure_context(pending)
                if receiver_key and receiver_key in receiver_failed:
                    repeated_receiver_failures.append({"round": round_number, "name": pending.get("name"),
                                                      "receiver_fingerprint": receiver_key,
                                                      "attempt_fingerprint": pending.get("attempt_fingerprint")})
                key = pending.get("attempt_fingerprint")
                if key and key in failed:
                    repeats[key] += 1
                local_key = local_failure_context(key) if key else None
                if local_key and local_key in local_failed:
                    local_repeats[local_key] += 1
        elif what == "SPASpatialDirectionObserved" and pending:
            pending_receiver_failed = details.get("same_cell") is False or details.get("identity_verified") is False
        elif what in ("SPASpatialOrientationDeferred", "SPASpatialOrientationReconciled") and pending:
            relocation = pending.get("relocation", False)
            success = bool(details.get("relocation_applied")) if relocation else bool(details.get("direction_input_committed"))
            key = pending.get("attempt_fingerprint", "")
            spatial.append({"round": details.get("round", round_number), "name": pending.get("name"),
                            "kind": "relocation" if relocation else "face", "input_effect_reconciled": success,
                            "fingerprint": key})
            if key and not success:
                failed.add(key)
                local_key = local_failure_context(key)
                if local_key:
                    local_failed.add(local_key)
                if pending_receiver_failed or details.get("receiver_failed_before_direction") is True:
                    receiver_key = receiver_failure_context(pending)
                    if receiver_key:
                        receiver_failed.add(receiver_key)
            pending = None
    return {
        "by_round": rounds,
        "unknown_default_east_face_plans": unknown_east_plans,
        "explicit_egir_unknown_east_face_plans": egir_unknown_east_plans,
        "facings_observed_without_extra_direction_input": observed_facings,
        "spatial_attempts": spatial,
        "repeated_previously_failed_fingerprints": dict(repeats),
        "repeated_local_failure_contexts": dict(local_repeats),
        "nearby_repeated_bench_identity_probes": nearby_bench_duplicates,
        "marginal_layouts_deferred_without_input": marginal_layouts_deferred,
        "repeated_pre_direction_receiver_failures": repeated_receiver_failures,
        "pending_spatial_at_end": pending,
        "malformed_callback_lines": malformed,
        "limitations": ["Counts controller requests, not all held-touch phases or proven device effects.",
                        "A click count does not classify that click as useless.",
                        "Nearby same-name bench probes need image review to establish a duplicate physical card.",
                        "Different rounds, boards and difficulties do not establish a reduction percentage.",
                        "Legacy keys omit member facings; normalized local repeats need review before calling an input useless."],
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    results = []
    for path in args.log:
        with path.open(encoding="utf-8-sig", errors="replace") as stream:
            results.append({"log": str(path), **audit_lines(stream)})
    args.output.write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
