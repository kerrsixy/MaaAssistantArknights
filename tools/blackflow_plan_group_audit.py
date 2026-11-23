"""Check deployment group references against the actual recruitment catalog.

This is an offline declaration check, not roster, healing, range or battle
qualification. Operator-name filters do not create recruitment groups.
"""

import argparse
import hashlib
import json
from pathlib import Path


# RoguelikeBattleTaskPlugin handles this nameless map card without recruitment.
SYNTHETIC_GROUPS = frozenset({"工人"})


def audit(recruitment, plans):
    known = {group["name"] for group in recruitment["priority"]}
    references = []
    for source, plan in plans:
        for index, step in enumerate(plan.get("deploy_plan", [])):
            declared = set(step["groups"])
            resolved = declared & (known | SYNTHETIC_GROUPS)
            missing = declared - known - SYNTHETIC_GROUPS
            if missing or not resolved:
                references.append(
                    {
                        "source": str(source),
                        "stage": plan["stage_name"],
                        "plan_index": index,
                        "location": step["location"],
                        "resolved_groups": sorted(resolved),
                        "unknown_groups": sorted(missing),
                        "unreachable_group_reference": not resolved,
                    }
                )
    return {
        "scope": "declaration reachability only; no roster or battle qualification",
        "known_group_count": len(known),
        "synthetic_groups": sorted(SYNTHETIC_GROUPS),
        "dead_entries": [r for r in references if r["unreachable_group_reference"]],
        "mixed_alias_entries": [
            r for r in references if not r["unreachable_group_reference"]
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recruitment", type=Path)
    parser.add_argument("plans", type=Path)
    args = parser.parse_args()
    paths = sorted(args.plans.rglob("*.json"))
    inputs = [args.recruitment, *paths]
    read = lambda path: json.loads(path.read_text(encoding="utf-8"))
    result = audit(read(args.recruitment), [(p, read(p)) for p in paths])
    result["source_sha256"] = {
        str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return bool(result["dead_entries"])


if __name__ == "__main__":
    raise SystemExit(main())
