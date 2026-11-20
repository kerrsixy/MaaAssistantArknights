"""Audit pinned pact display formulas without inventing runtime stacking rules.

This module is offline only. It deliberately returns independent contributions,
never a combined combat stat, expected DPS, survival estimate, or win verdict.
"""

import argparse
from decimal import Decimal
import hashlib
import json
from pathlib import Path


SEASON_PATH = "/activity/AUTOCHESS/act3autochess/V3_1"
PACT_IDS = ("egirShip", "preciShip", "steadShip")
EQUIPMENT_NAMES = ("阿戈尔重刃", "坚守盾牌", "精准狙击镜", "炎国短刀", "多功能补剂")
ATTRIBUTES = {
    "egirShip": (("max_hp_bonus_fraction", "base_max_hp", "max_hp_per_stack", "0%"),),
    "preciShip": (("attack_bonus_fraction", "base_atk", "atk_per_stack", "0%"),),
    "steadShip": (
        ("max_hp_bonus_fraction", "base_max_hp", "max_hp_per_stack", "0%"),
        ("retaliation_arts_pre_mitigation", "base_damage_value", "damage_value_per_stack", "0"),
    ),
}


def evidence(source, pointer, value):
    return {"source": source, "json_pointer": pointer, "value": value}


def plain_decimal(number):
    return format(number.normalize(), "f")


def linear_contribution(base, per_layer, layers):
    """Exact decimal display contribution; no cap or cross-buff composition."""
    if type(layers) is not int or layers < 0:
        raise ValueError("Layers must be an observed nonnegative integer")
    base, per_layer = Decimal(str(base)), Decimal(str(per_layer))
    if not base.is_finite() or not per_layer.is_finite() or base < 0 or per_layer < 0:
        raise ValueError("Unsupported non-finite or negative coefficient")
    return base + per_layer * layers


def blackboard_values(buffs):
    if len(buffs) != 1:
        raise ValueError("Multiple buff handlers need an explicit composition audit")
    result = {}
    for entry in buffs[0]["blackboard"]:
        key = entry["key"]
        if key in result:
            raise ValueError("Duplicate blackboard key")
        result[key] = entry["valueStr"] if entry["valueStr"] is not None else entry["value"]
    return result


def extract_model(season, catalogue, pact_id):
    pact = season["bondInfoDict"][pact_id]
    effect_id = pact["effectId"]
    metadata = season["effectInfoDataDict"][effect_id]
    parameters = season["effectDescParamDataDict"][effect_id]
    buffs = season["effectBuffInfoDataDict"][effect_id]
    projected = catalogue["effects"][effect_id]
    checks = {
        "pact": catalogue["pacts"][pact_id] == pact,
        "metadata": projected["metadata"] == metadata,
        "parameters": projected["parameters"] == parameters,
        "buffs": projected["buffs"] == buffs,
    }
    if not all(checks.values()):
        raise ValueError(f"Pinned activity and derived catalogue disagree: {pact_id}: {checks}")
    board = blackboard_values(buffs)
    formulas = []
    specs = ATTRIBUTES[pact_id]
    if len(parameters["descParamInfos"]) != len(specs):
        raise ValueError("Changed parameter layout needs an audit")
    for index, (attribute, base, per_stack, display_format) in enumerate(specs):
        param = parameters["descParamInfos"][index]
        token = "{" + str(index) + ":" + display_format + "}"
        if param["descParamBase"] != base or param["descParamPerStack"] != per_stack:
            raise ValueError("Unexpected display parameter binding")
        if token not in metadata["effectDesc"]:
            raise ValueError("Formula is not bound to the expected catalogue text token")
        formulas.append({
            "attribute": attribute,
            "expression": f"{base} + {per_stack} * observed_layers",
            "base": board[base], "per_layer": board[per_stack],
            "display_token": token,
            "display_parameter_binding": evidence(
                "activity", f"{SEASON_PATH}/effectDescParamDataDict/{effect_id}/descParamInfos/{index}", param),
            "raw_desc_param_max": param["descParamMax"],
            "cap_semantics": "unknown: zero is retained as raw data, not treated as a zero cap or proof of no cap",
            "runtime_formula_verified": False,
        })
    return {
        "pact_id": pact_id, "name": pact["name"], "effect_id": effect_id,
        "activation_distinct_members": pact["activeCount"],
        "milestone_distinct_members": int(board["power_bond_char_cnt"]),
        "pact_record": evidence("activity", f"{SEASON_PATH}/bondInfoDict/{pact_id}", pact),
        "effect_metadata": evidence("activity", f"{SEASON_PATH}/effectInfoDataDict/{effect_id}", metadata),
        "effect_buffs": evidence("activity", f"{SEASON_PATH}/effectBuffInfoDataDict/{effect_id}", buffs),
        "effect_parameters": evidence("activity", f"{SEASON_PATH}/effectDescParamDataDict/{effect_id}", parameters),
        "catalogue_crosscheck": checks,
        "blackboard": board,
        "formulas": formulas,
        "within_pact_display_scaling": "base + per_layer * observed_layers",
        "buff_wrapper_type": buffs[0]["key"],
        "runtime_handler": board["key"],
        "cross_pact_equipment_skill_stacking": {
            "status": "unknown",
            "reason": "The snapshot names a custom handler but does not include its implementation or stat modifier buckets.",
            "noStack_is_attribute_composition_proof": False,
            "effectCounterType_is_attribute_composition_proof": False,
        },
    }


def evaluate_contribution(model, *, layers, active, distinct_members, target_member=None, target_ranged=None):
    """Return an isolated target contribution under explicit observed inputs.

    The caller must establish field identity, class/location and actual pact
    activation. Distinct member count alone cannot override a ban or rule.
    An observed current layer count must not be added to temporary gear layers
    again; this function uses exactly the supplied count once.
    """
    result = {"status": "unknown", "runtime_verified": False, "combat_verdict": "unknown"}
    if active is False:
        return {**result, "status": "inactive", "contributions": {}}
    if active is not True or layers is None or distinct_members is None:
        return {**result, "reason": "Missing activation, layer or distinct-member observation"}
    if type(distinct_members) is not int or distinct_members < 0:
        raise ValueError("Invalid distinct-member count")
    linear_contribution(0, 0, layers)  # Strict integer/unknown validation.
    if distinct_members < model["activation_distinct_members"]:
        return {**result, "status": "contradictory", "reason": "Active assertion contradicts observed member count"}
    milestone = distinct_members >= model["milestone_distinct_members"]
    pact_id = model["pact_id"]
    if pact_id == "steadShip":
        eligible = True  # Description: all field operators, not only members.
    elif pact_id == "preciShip" and milestone:
        eligible = True if target_member is True or target_ranged is True else (
            False if target_member is False and target_ranged is False else None)
    else:
        eligible = target_member
    if eligible is None:
        return {**result, "reason": "Recipient membership/location is unknown"}
    if eligible is not True:
        return {**result, "status": "not_recipient", "contributions": {}}
    contributions = {}
    for formula in model["formulas"]:
        if formula["attribute"] == "retaliation_arts_pre_mitigation" and not milestone:
            continue
        # Steadfast grants HP to all, but retaliation belongs to Steadfast
        # recipients only. Unknown membership never implies a retaliation.
        if formula["attribute"] == "retaliation_arts_pre_mitigation" and target_member is not True:
            continue
        contributions[formula["attribute"]] = plain_decimal(
            linear_contribution(formula["base"], formula["per_layer"], layers))
    result.update({
        "status": "isolated_display_scenario", "observed_layers_used_once": layers,
        "milestone_active": milestone, "contributions": contributions,
        "combined_panel_stats": None, "attribute_bucket_stacking": "unknown",
        "assumptions": ["Default pact effect; no strategy replacement has been applied",
                        "Recipient is an identified field operator", "Custom runtime handler agrees with display parameters"],
    })
    board = model["blackboard"]
    if pact_id == "preciShip" and milestone:
        result["declared_penetration"] = {
            "defense_fraction": board["power_def_penetrate"],
            "magic_resistance_fraction": board["power_magic_resist_penetrate"],
            "composition_with_other_reductions": "unknown",
        }
    if pact_id == "steadShip" and milestone:
        result["milestone_mechanics"] = {
            "nonmember_damage_shared_fraction": board["damage_resistance"],
            "retaliation_interval_seconds": board["cd_duration"],
            "retaliation_cooldown_scope": "unknown: individual bearer, damage source, or shared group",
            "fragility_damage_scale": board["damage_scale"],
            "fragility_duration_seconds": board["weak[limit]"],
            "trigger_count_and_retaliation_dps": "unknown",
            "shared_damage_mitigation_order": "unknown",
        }
    if pact_id == "egirShip":
        result["consumption_mechanics"] = {
            "declared_physical_damage": board["damage_value"],
            "layer_increment_rule": board["bond_add_type"],
            "inherited_stats": ["base_attack", "block_count"],
            "successful_kill_guaranteed": False,
            "inheritance_vs_other_stat_modifiers_order": "unknown",
            "default_free_respawn_limit_if_milestone": int(board["max_free_respawn_cnt"]) if milestone else 0,
        }
    return result


def build_audit(activity, catalogue, equipment_catalogue):
    season = activity["activity"]["AUTOCHESS"]["act3autochess"]["V3_1"]
    models = {pact: extract_model(season, catalogue, pact) for pact in PACT_IDS}
    equipment = []
    for effect_id, meta in season["effectInfoDataDict"].items():
        if meta["effectType"] != "EQUIP" or meta["effectName"] not in EQUIPMENT_NAMES:
            continue
        buffs = season["effectBuffInfoDataDict"][effect_id]
        projected = catalogue["effects"][effect_id]
        if projected["metadata"] != meta or projected["buffs"] != buffs:
            raise ValueError("Equipment snapshot/catalogue disagreement")
        matching = [(index, item) for index, item in enumerate(equipment_catalogue["items"])
                    if item["name"] == meta["effectName"]]
        equipment.append({
            "effect_id": effect_id, "name": meta["effectName"],
            "metadata": evidence("activity", f"{SEASON_PATH}/effectInfoDataDict/{effect_id}", meta),
            "buffs": evidence("activity", f"{SEASON_PATH}/effectBuffInfoDataDict/{effect_id}", buffs),
            "blackboard": blackboard_values(buffs),
            "secondary_text_catalogue": [evidence("equipment_catalogue", f"/items/{i}", value) for i, value in matching],
            "combat_catalogue_exact_match": True,
            "cross_effect_stat_composition": "unknown",
        })
    scenarios = []
    for pact_id, model in models.items():
        for layers in (0, 48, 100, 183, 300, 384):
            scenarios.append({"pact_id": pact_id, "layers": layers, "result": evaluate_contribution(
                model, layers=layers, active=True,
                distinct_members=model["milestone_distinct_members"], target_member=True, target_ranged=False)})
    variant_id = "bondeffect_egir_2"
    variant = {container: evidence("activity", f"{SEASON_PATH}/{container}/{variant_id}", season[container][variant_id])
               for container in ("effectInfoDataDict", "effectBuffInfoDataDict", "effectDescParamDataDict")}
    return {
        "schema_version": 1, "season": "act3autochess/V3_1", "offline_only": True,
        "runtime_integration": False, "runtime_validation": False,
        "pacts": models, "equipment_context": equipment, "scenarios": scenarios,
        "egir_strategy_variant": variant,
        "egir_strategy_variant_binding": evidence(
            "activity", f"{SEASON_PATH}/bandDataListDict/band_egir", season["bandDataListDict"]["band_egir"]),
        "unknowns": [
            "Attribute modifier buckets and whether pact/equipment/skill percentages add or multiply",
            "Runtime meaning of descParamMax=0; display formulas do not establish uncapped runtime behavior",
            "Rounding, stat clamps, snapshots, and when temporary layers enter the observed counter",
            "Precision penetration ordering with other defense/resistance modifiers",
            "Steadfast shared-damage mitigation ordering, recursion, and retaliation cooldown scope",
            "Egir inherited base-attack ordering relative to cultivation, skill, gear, and other pact modifiers",
            "Actual skill uptime, damage-event counts, sustained survivability, and time-to-kill",
            "Live boss identity/mode HP selection and complete enemy mitigation/battle modifier state",
        ],
        "next_model_boundary": {
            "safe_now": "Independent typed contributions and recipient/milestone gates, using observed layers exactly once",
            "not_safe_now": "A single final HP/ATK/DPS/clear-probability score formed by blindly combining all contributions",
            "calibration": [
                "On an identified unchanged form, compare baseline vs one pact at two observed layer counts",
                "Then add exactly one identified equipment item to distinguish additive and multiplicative buckets",
                "Keep base stats, skill state, cultivation and temporary-layer timing controlled",
                "Record displayed panel rounding separately from actual damage evidence",
            ],
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--activity", required=True, type=Path)
    parser.add_argument("--catalogue", required=True, type=Path)
    parser.add_argument("--equipment-catalogue", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    sources = {name: path for name, path in (("activity", args.activity), ("catalogue", args.catalogue),
                                           ("equipment_catalogue", args.equipment_catalogue))}
    data = {name: json.loads(path.read_text(encoding="utf-8")) for name, path in sources.items()}
    digest = hashlib.sha256(args.activity.read_bytes()).hexdigest()
    if data["catalogue"]["sources_sha256"]["activity_table"] != digest:
        raise ValueError("Derived catalogue is not pinned to this activity snapshot")
    result = build_audit(data["activity"], data["catalogue"], data["equipment_catalogue"])
    result["sources"] = {name: {"path": str(path.resolve()), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                         for name, path in sources.items()}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(args.output), "pacts": len(result["pacts"]),
                      "equipment_effects": len(result["equipment_context"]),
                      "scenarios": len(result["scenarios"]), "runtime_validated": False}, ensure_ascii=False))


if __name__ == "__main__":
    main()
