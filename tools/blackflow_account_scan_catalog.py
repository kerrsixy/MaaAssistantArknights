"""Derive account module scan targets from recruitment gates and original game tables.

This generates identity metadata only. It never scans an account, changes a module,
or proves that a module is equipped in the current exploration.
"""

import argparse
import hashlib
import json
import os
import tempfile
from pathlib import Path


def required_text(value, field):
    if not isinstance(value, str) or not value.strip() or value != value.strip():
        raise ValueError(f"Invalid {field}")
    return value


def positive_level(value, field):
    if type(value) is not int or not 1 <= value <= 3:
        raise ValueError(f"Invalid {field}")
    return value


def build_catalog(recruitment, characters, equipment, battle_equipment, snapshot_label):
    if not isinstance(recruitment, dict) or recruitment.get("theme") != "BlackFlow":
        raise ValueError("Expected BlackFlow recruitment rules")
    groups = recruitment.get("priority")
    if not isinstance(groups, list):
        raise ValueError("Recruitment priority must be an array")
    if not isinstance(characters, dict) or not isinstance(battle_equipment, dict):
        raise ValueError("Expected original character and battle equipment tables")
    modules = equipment.get("equipDict") if isinstance(equipment, dict) else None
    owners = equipment.get("charEquip") if isinstance(equipment, dict) else None
    if not isinstance(modules, dict) or not isinstance(owners, dict):
        raise ValueError("Expected original equipment ownership table")

    gates, operator_gates = {}, {}
    for group in groups:
        if not isinstance(group, dict) or not isinstance(group.get("opers"), list):
            raise ValueError("Malformed recruitment group")
        for oper in group["opers"]:
            if not isinstance(oper, dict):
                raise ValueError("Malformed recruitment operator")
            if "account_module" not in oper:
                continue
            policy = oper["account_module"]
            if not isinstance(policy, dict):
                raise ValueError("Malformed account module policy")
            name = required_text(oper.get("name"), "operator name")
            module_id = required_text(policy.get("module_id"), "module ID")
            minimum = positive_level(
                policy.get("minimum_level", 3), "required module level"
            )
            gate = (name, minimum)
            if module_id in gates and gates[module_id] != gate:
                raise ValueError("Conflicting owners or levels for one module gate")
            if name in operator_gates and operator_gates[name] != (module_id, minimum):
                raise ValueError("Conflicting module gates for one operator")
            gates[module_id] = gate
            operator_gates[name] = (module_id, minimum)

    targets = []
    for module_id, (name, minimum) in sorted(gates.items()):
        module = modules.get(module_id)
        if not isinstance(module, dict) or module.get("uniEquipId") != module_id:
            raise ValueError(f"Missing or mismatched original module: {module_id}")
        title = required_text(module.get("uniEquipName"), "module display title")
        owner = required_text(module.get("charId"), "module owner ID")
        character = characters.get(owner)
        if not isinstance(character, dict) or character.get("name") != name:
            raise ValueError(
                f"Module owner does not match recruitment operator: {module_id}"
            )
        if not isinstance(owners.get(owner), list) or module_id not in owners[owner]:
            raise ValueError(
                f"Module absent from original owner equipment list: {module_id}"
            )
        battle = battle_equipment.get(module_id)
        phases = battle.get("phases") if isinstance(battle, dict) else None
        if not isinstance(phases, list) or not phases:
            raise ValueError(f"Missing original module upgrade phases: {module_id}")
        levels = [
            positive_level(phase.get("equipLevel"), "module phase")
            if isinstance(phase, dict)
            else positive_level(None, "module phase")
            for phase in phases
        ]
        maximum = max(levels)
        if sorted(levels) != list(range(1, maximum + 1)) or minimum > maximum:
            raise ValueError(
                f"Noncontiguous phases or unsupported requested level: {module_id}"
            )
        targets.append(
            {
                "operator_id": owner,
                "operator_name": name,
                "module_id": module_id,
                "display_title": title,
                "max_level": maximum,
                "minimum_level": minimum,
            }
        )
    return {
        "schema_version": 1,
        "theme": "BlackFlow",
        "snapshot_label": required_text(snapshot_label, "snapshot label"),
        "label_is_not_game_version_proof": True,
        "target_source": "recruitment.account_module joined to original equipment ownership and upgrade phases",
        "account_scan_performed": False,
        "current_battle_equipment_proven": False,
        "targets": targets,
    }


def generate(
    recruitment_path,
    character_path,
    equipment_path,
    battle_equipment_path,
    snapshot_label,
):
    sources = {
        "recruitment": Path(recruitment_path),
        "characters": Path(character_path),
        "equipment": Path(equipment_path),
        "battle_equipment": Path(battle_equipment_path),
    }
    contents = {key: path.read_bytes() for key, path in sources.items()}
    parsed = {key: json.loads(value) for key, value in contents.items()}
    catalog = build_catalog(
        parsed["recruitment"],
        parsed["characters"],
        parsed["equipment"],
        parsed["battle_equipment"],
        snapshot_label,
    )
    catalog["source_files"] = {
        key: {"name": path.name, "sha256": hashlib.sha256(contents[key]).hexdigest()}
        for key, path in sources.items()
    }
    return catalog


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--recruitment", required=True, type=Path)
    parser.add_argument("--characters", required=True, type=Path)
    parser.add_argument("--equipment", required=True, type=Path)
    parser.add_argument("--battle-equipment", required=True, type=Path)
    parser.add_argument("--snapshot-label", required=True)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args(argv)
    catalog = generate(
        args.recruitment,
        args.characters,
        args.equipment,
        args.battle_equipment,
        args.snapshot_label,
    )
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", dir=args.out.parent, suffix=".tmp", delete=False
    ) as stream:
        json.dump(catalog, stream, ensure_ascii=False, indent=4)
        stream.write("\n")
        temporary = Path(stream.name)
    os.replace(temporary, args.out)
    print(
        json.dumps(
            {"targets": len(catalog["targets"]), "account_scan_performed": False}
        )
    )


if __name__ == "__main__":
    main()
