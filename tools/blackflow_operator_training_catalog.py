"""Extract operator skill slots and unlock conditions from original game data.

This describes game rules, never an account's actual skill training or equipment.
Unknown source fields remain explicit; skill indices are never compacted.
"""

import argparse
import hashlib
import json
import os
import tempfile
from pathlib import Path


def phase(value):
    if type(value) is int and value >= 0:
        return value
    if isinstance(value, str) and value.startswith("PHASE_"):
        suffix = value.removeprefix("PHASE_")
        if suffix.isdecimal():
            return int(suffix)
    return None


def positive(value):
    return value if type(value) is int and value > 0 else None


def base_block_count(raw_phase):
    """Return a constant unmodified phase count only with complete keyframes."""
    if not isinstance(raw_phase, dict):
        return None
    maximum = positive(raw_phase.get("maxLevel"))
    points = raw_phase.get("attributesKeyFrames")
    if maximum is None or not isinstance(points, list) or not points:
        return None
    counts, levels = set(), []
    for point in points:
        if not isinstance(point, dict) or not isinstance(point.get("data"), dict):
            return None
        level, count = positive(point.get("level")), point["data"].get("blockCnt")
        if level is None or level > maximum or type(count) is not int or count < 0:
            return None
        counts.add(count)
        levels.append(level)
    if len(counts) != 1 or levels != sorted(set(levels)) or levels[0] != 1 or levels[-1] != maximum:
        return None
    return next(iter(counts))


def build_catalog(characters, revision, digest):
    if not isinstance(characters, dict) or not revision or len(digest) != 64:
        raise ValueError("Expected original character table and source binding")
    operators, names, issues = {}, {}, []
    for identity, raw in characters.items():
        if not identity.startswith("char_"):
            continue
        if not isinstance(raw, dict) or not isinstance(raw.get("name"), str):
            issues.append({"operator": identity, "reason": "missing_name"})
            continue
        name = raw["name"]
        maximums = [positive(item.get("maxLevel")) if isinstance(item, dict) else None
                    for item in (raw.get("phases") or [])]
        raw_skills = raw.get("skills")
        if not isinstance(raw_skills, list):
            issues.append({"operator": identity, "reason": "missing_skills"})
            raw_skills = []
        slots = []
        for index, skill in enumerate(raw_skills, 1):
            if not isinstance(skill, dict) or not isinstance(skill.get("skillId"), str):
                slots.append({"index": index, "known": False, "reason": "unresolved_slot"})
                issues.append({"operator": identity, "slot": index, "reason": "unresolved_slot"})
                continue
            unlock = skill.get("unlockCond") or {}
            elite, level = phase(unlock.get("phase")), positive(unlock.get("level"))
            known = elite is not None and elite < len(maximums) and level is not None
            mastery = skill.get("levelUpCostCond")
            slots.append({
                "index": index, "skill_id": skill["skillId"], "known": known,
                "unlock_elite": elite, "unlock_level": level,
                "mastery_steps": len(mastery) if isinstance(mastery, list) else None,
            })
            if not known:
                issues.append({"operator": identity, "slot": index, "reason": "unknown_unlock_condition"})
        ordinary = raw.get("allSkillLvlup")
        operators[identity] = {
            "name": name, "profession": raw.get("profession"),
            "max_levels": maximums,
            "base_block_counts": [base_block_count(item) for item in (raw.get("phases") or [])],
            "base_skill_max": len(ordinary) + 1 if isinstance(ordinary, list) else None,
            "skills": slots,
        }
        names.setdefault(name, []).append(identity)
    if not operators:
        raise ValueError("No operators in original table")
    return {
        "schema": 1, "game_data_revision": revision, "character_table_sha256": digest,
        "operators": operators, "names": {name: sorted(ids) for name, ids in names.items()},
        "issues": issues,
        "account_training_included": False, "current_equipment_included": False,
        "base_block_scope": "Unmodified character phase only; excludes skills, equipment, buffs, debuffs and occupied capacity",
    }


def write_atomic(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                                     prefix=path.name + ".", suffix=".tmp", delete=False) as stream:
        json.dump(value, stream, ensure_ascii=False, indent=4)
        stream.write("\n")
        temporary = Path(stream.name)
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--characters", type=Path, required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    data = args.characters.read_bytes()
    catalog = build_catalog(json.loads(data), args.revision, hashlib.sha256(data).hexdigest())
    write_atomic(args.output, catalog)
    print(json.dumps({"operators": len(catalog["operators"]), "issues": len(catalog["issues"]),
                      "account_training_included": False}))


if __name__ == "__main__":
    main()
