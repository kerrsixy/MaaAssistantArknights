"""Read-only consistency checks for committed native deployment snapshots."""

import argparse
import json
from collections import Counter
from pathlib import Path


def audit_deployment_ledger(events: list[dict]) -> dict:
    contradictions = []
    uncertain_drops = []
    verified_full_fields = []
    checked_snapshots = 0
    missing_snapshots = 0
    pending_replacement = None
    for index, event in enumerate(events):
        envelope = event.get("details", {})
        what = envelope.get("what")
        details = envelope.get("details", {})
        if "pending_replacement" in details:
            pending_replacement = details["pending_replacement"]
        if what == "SPADeploymentReconciliationRequired":
            uncertain_drops.append({"event_index": index, **details})
        if "field_positions" not in details:
            if what == "SPAAutomationDeploymentReconciled":
                missing_snapshots += 1
            continue
        positions = details["field_positions"]
        roster = details.get("roster")
        remaining = details.get("remaining_deployment")
        if not isinstance(positions, list) or not isinstance(roster, list):
            continue
        checked_snapshots += 1
        errors = []
        if isinstance(remaining, int) and 0 <= remaining <= 8 and len(positions) > 8 - remaining:
            errors.append("More tracked field positions than the observed population")
        owned = {unit["name"]: unit for unit in roster if isinstance(unit, dict) and "name" in unit}
        counts = Counter(point.get("name") for point in positions if isinstance(point, dict))
        for name, count in counts.items():
            unit = owned.get(name)
            if unit is None or not unit.get("deployed"):
                errors.append(f"Field position has no deployed roster identity: {name}")
                continue
            copies = unit.get("initial_copies")
            physical = unit.get("physical_copies")
            elites = unit.get("elite_copies")
            if "physical_copies" in unit or "elite_copies" in unit:
                if (type(copies) is not int or copies < 0
                        or ("elite_copies" in unit and (type(elites) is not int or elites < 0))
                        or ("physical_copies" in unit and (type(physical) is not int or not 0 <= physical <= 20))):
                    errors.append(f"Invalid explicit physical form counts: {name}")
                    continue
                if "elite_copies" in unit:
                    total = copies + elites
                    if (total > 20 or bool(unit.get("elite")) != (elites > 0)
                            or ("physical_copies" in unit and physical != total)):
                        errors.append(f"Contradictory explicit physical form counts: {name}")
                        continue
                else:
                    total = physical
            else:
                # Legacy callers used initial_copies for different things.
                # Keep the old upper bound; explicit native rows are exact.
                total = copies + int(bool(unit.get("elite"))) if type(copies) is int else None
            if total is not None and count > total:
                errors.append(f"Tracked field copies exceed owned copies: {name}")
        for offset, point in enumerate(positions):
            if not isinstance(point, dict):
                errors.append("Invalid field position entry")
                continue
            if not point.get("field_pose_verified"):
                continue
            for other in positions[:offset]:
                if not isinstance(other, dict) or not other.get("field_pose_verified"):
                    continue
                if (point.get("x"), point.get("y")) == (other.get("x"), other.get("y")):
                    errors.append("Two tracked occupants share one verified field position")
        if errors:
            contradictions.append({"event_index": index, "round": details.get("round"),
                                   "what": what, "errors": errors})
        if (not errors and not pending_replacement and remaining == 0 and len(positions) == 8
                and all(isinstance(point, dict) and point.get("field_pose_verified")
                        and point.get("coordinate_frame") == "collapsed"
                        and isinstance(point.get("x"), int) and 180 <= point["x"] < 1000
                        and isinstance(point.get("y"), int) and 140 <= point["y"] < 510
                        for point in positions)):
            verified_full_fields.append({"event_index": index, "round": details.get("round")})
    return {"read_only": True, "battle_gate": False, "checked_snapshots": checked_snapshots,
            "missing_deployment_snapshots": missing_snapshots, "contradictions": contradictions,
            "uncertain_drops": uncertain_drops, "verified_full_field_snapshots": verified_full_fields,
            "stable_victory_verified": False}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("events", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    events = [json.loads(line) for line in args.events.read_text(encoding="utf-8").splitlines() if line.strip()]
    result = json.dumps(audit_deployment_ledger(events), ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(result + "\n", encoding="utf-8")
    else:
        print(result)
