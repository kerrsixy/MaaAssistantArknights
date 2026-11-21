"""Extract pinned planning facts, retaining unknown probabilities/runtime multipliers."""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path


SEASON = "/activity/AUTOCHESS/act3autochess/V3_1"


def fact(source, path, value):
    return {"source": source, "json_pointer": path, "value": value}


def unknown(reason, inspected):
    return {"status": "unknown", "reason": reason, "inspected_json_pointers": inspected}


def build_facts(activity, bosses, waves, boards):
    season = activity["activity"]["AUTOCHESS"]["act3autochess"]["V3_1"]
    common = activity["autoChessData"]
    modes = season["modeDataDict"]
    refs = {}
    for mode, rounds in season["battleDataDict"].items():
        for number, entries in rounds.items():
            for index, entry in enumerate(entries):
                if entry["bossId"]:
                    refs.setdefault((entry["bossId"], entry["levelId"].lower()), []).append(
                        {"mode": mode, "round": int(number), "json_pointer":
                         f"{SEASON}/battleDataDict/{mode}/{number}/{index}"})
    boss_rows = []
    for index, boss in enumerate(bosses["bosses"]):
        original = season["bossInfoDict"][boss["boss_id"]]
        if original["bloodDataDict"] != boss["hp_by_mode"] or original["isHidingBoss"] != boss["hidden"]:
            raise ValueError("Derived boss HP/hidden flag disagrees with pinned season data")
        referenced = refs.get((boss["boss_id"], boss["level_id"].lower()), [])
        if not referenced or any(not ref["mode"].startswith("mode_single_") for ref in referenced):
            raise ValueError("Expected the existing boss catalogue to contain referenced solo templates")
        boss_rows.append({
            "boss_id": boss["boss_id"], "family_name": boss["family_name"],
            "season_record": fact("activity", f"{SEASON}/bossInfoDict/{boss['boss_id']}", original),
            "solo_template": [fact("boss_catalogue", f"/bosses/{index}/{key}", boss[key])
                              for key in ("enemy_id", "level_id", "rounds_by_mode", "template_home",
                                          "route", "stationary_route", "time_limit_seconds", "enemy_database_level")],
            "referenced_battle_entries": referenced,
            "base_attributes": fact("boss_catalogue", f"/bosses/{index}/enemy_base/attributes", boss["enemy_base"]["attributes"]),
            "runtime_hp_verified": False,
        })
    wave_rows = []
    for name, level in sorted(waves.items()):
        actions = []
        for wi, wave in enumerate(level["waves"]):
            for fi, fragment in enumerate(wave["fragments"]):
                for ai, action in enumerate(fragment["actions"]):
                    if action["actionType"] == "SPAWN":
                        route = action["routeIndex"]
                        actions.append({
                            "action": fact(name, f"/waves/{wi}/fragments/{fi}/actions/{ai}", action),
                            "wave_pre_delay": fact(name, f"/waves/{wi}/preDelay", wave["preDelay"]),
                            "fragment_pre_delay": fact(name, f"/waves/{wi}/fragments/{fi}/preDelay", fragment["preDelay"]),
                            "route": fact(name, f"/routes/{route}", level["routes"][route]),
                        })
        wave_rows.append({"file": name, "time_limit": fact(name, "/options/maxPlayTime", level["options"]["maxPlayTime"]),
                          "nominal_template_spawn_count": sum(item["action"]["value"]["count"] for item in actions),
                          "actions": actions, "enemy_refs": fact(name, "/enemyDbRefs", level["enemyDbRefs"]),
                          "runes": fact(name, "/runes", level.get("runes")),
                          "runtime_enemy_roster_verified": False})
    shop_units = []
    for chess_id, shop in season["charShopChessDatas"].items():
        chess = season["charChessDataDict"][chess_id]
        shop_units.append({
            "shop_record": fact("activity", f"{SEASON}/charShopChessDatas/{chess_id}", shop),
            "form_record": fact("activity", f"{SEASON}/charChessDataDict/{chess_id}", chess),
        })
    layer_records = []
    for bond_id, bond in season["bondInfoDict"].items():
        effect = bond["effectId"]
        layer_records.append({
            "bond": fact("activity", f"{SEASON}/bondInfoDict/{bond_id}", bond),
            "effect_parameters": fact("activity", f"{SEASON}/effectBuffInfoDataDict/{effect}",
                                      season["effectBuffInfoDataDict"].get(effect)),
        })
    return {
        "schema_version": 1, "season": "act3autochess/V3_1", "runtime_validation_performed": False,
        "difficulty": {
            "modes": [fact("activity", f"{SEASON}/modeDataDict/{key}", value) for key, value in modes.items()],
            "empty_factor_tables": [fact("activity", SEASON + "/" + key, season[key])
                                    for key in ("modeFactorInfo", "difficultyFactorInfo")],
            "global_enemy_named_factors": [fact("activity", "/autoChessData/constData/" + key, common["constData"][key])
                                           for key in ("enemyMaxHpFactor", "enemyAtkFactor", "enemyDefFactor", "enemyMagicResistanceFactor")],
            "runtime_multiplier_formula": unknown(
                "difficultyDesc is a display string, not a percentage. mode.factor and globally named enemy factors have no verified consumer/formula here; do not multiply raw stats by them.",
                [SEASON + "/modeDataDict", SEASON + "/modeFactorInfo", SEASON + "/difficultyFactorInfo", "/autoChessData/constData"]),
        },
        "shop": {
            "mode_level_records": fact("activity", SEASON + "/shopLevelDataDict", season["shopLevelDataDict"]),
            "tier_price_training_records": fact("activity", SEASON + "/shopCharChessInfoData", season["shopCharChessInfoData"]),
            "constants": [fact("activity", SEASON + "/constData/" + key, season["constData"][key])
                          for key in ("shopRefreshPrice", "maxDeckChessCnt", "maxDeckEquipmentChessCnt", "maxBattleChessCnt", "diyCharLimitForLevels")],
            "operator_records": shop_units,
            "item_shop_records": fact("activity", SEASON + "/trapShopChessDatas", season["trapShopChessDatas"]),
            "operator_tier_counts_including_hidden_and_diy": dict(Counter(str(row["chessLevel"]) for row in season["charShopChessDatas"].values())),
            "tier_and_card_draw_probabilities": unknown(
                "No tier distribution or per-card draw weight is present in shopLevelDataDict/charShopChessDatas/shopCharChessInfoData. Catalogue counts and stage/bond weights are not shop probabilities.",
                [SEASON + "/shopLevelDataDict", SEASON + "/charShopChessDatas", SEASON + "/shopCharChessInfoData"]),
            "shared_pool_and_runtime_eligibility": unknown(
                "Static tier, hidden flags, DIY limits, mode active/inactive bonds and strategy inactiveModeIdList are known. Exact server eligibility, shared copies, tier selection and depletion probabilities are not established; use live bans/offers.",
                [SEASON + "/modeDataDict", SEASON + "/bandDataListDict", SEASON + "/charShopChessDatas", SEASON + "/constData/diyCharLimitForLevels"]),
            "live_upgrade_price": unknown("initialUpgradePrice is a configured starting price; round discounts and strategy effects require current observed price.",
                                          [SEASON + "/shopLevelDataDict"]),
        },
        "strategy_mode_restrictions": [fact("activity", f"{SEASON}/bandDataListDict/{key}", row)
            for key, row in season["bandDataListDict"].items() if row["inactiveModeIdList"]],
        "maps_and_waves": {
            "stage_mode_weights": fact("activity", SEASON + "/stageDatasDict", season["stageDatasDict"]),
            "mode_round_template_bindings": fact("activity", SEASON + "/battleDataDict", season["battleDataDict"]),
            "board_files": [{"file": name, "row_count": len(level["mapData"]["map"]),
                             "column_count": len(level["mapData"]["map"][0]),
                             "map_source": fact(name, "/mapData", level["mapData"]),
                             "predefines": fact(name, "/predefines", level.get("predefines")),
                             "hard_predefines": fact(name, "/hardPredefines", level.get("hardPredefines"))}
                            for name, level in sorted(boards.items())],
            "normal_wave_templates": wave_rows,
            "enemy_templates": [fact("activity", "/autoChessData/constData/" + key, value)
                                for key, value in common["constData"].items() if key.startswith("templateEnemy")],
            "replacement_selection": [fact("activity", SEASON + "/" + key, season[key])
                                      for key in ("specialEnemyRandomTypeDict", "specialEnemyInfoDict")],
            "enemy_attribute_records": fact("activity", "/autoChessData/randomEnemyAttributeDict", common["randomEnemyAttributeDict"]),
            "fixed_map_does_not_prove_fixed_enemies": unknown(
                "stageDatasDict gives map eligibility/weight; battleDataDict selects mode/round templates separately. Special enemies, replacements, hidden branches and their engine selection remain runtime inputs.",
                [SEASON + "/stageDatasDict", SEASON + "/battleDataDict", SEASON + "/specialEnemyInfoDict"]),
        },
        "bosses": boss_rows,
        "boss_limitations": [
            "Existing generator intentionally filters mode_single_*; its coordinate routes are not verified multiplayer templates.",
            "bloodDataDict gives explicit configured HP per mode and must take precedence over a fabricated difficulty multiplier.",
            "A round-15 hidden-boss entry is availability, not proof the hidden encounter will trigger or was won.",
            "stationary_route describes checkpoints, not scripted skills or immunity uptime.",
            "Enemy attributes with m_defined=false are unset, not authoritative zero values.",
        ],
        "pact_layer_parameters": layer_records,
        "calculation_boundary": {
            "exact_static_or_conditional": [
                "Look up configured boss HP once exact mode and boss ID are known.",
                "Read map tiles, nominal wave spawn actions/routes/counts and configured time limit; do not substitute them for observed replacements.",
                "Use tier/base prices, promotion copy requirements, inventory/field limits and observed current quotes for transaction arithmetic.",
                "Read layer coefficients and activation/member restrictions; evaluate a stated algebraic scenario with known eligible recipients.",
            ],
            "requires_live_calibration": [
                "Normal enemy runtime HP/ATK/DEF/RES after mode scaling and random replacement.",
                "Shop tier/per-card probabilities, depleted shared pools and refresh expected value.",
                "Skill/talent/equipment/pact stacking order, survival, blocking, targeting, pathing and actual skill uptime.",
                "Joint support enemies and teammate states; exact hidden-boss trigger conditions.",
            ],
            "not_claimed": ["Complete combat simulator", "Predicted clear probability", "Any verified win"],
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--activity", type=Path, required=True)
    parser.add_argument("--boss-data", type=Path, required=True)
    parser.add_argument("--wave-dir", type=Path, required=True)
    parser.add_argument("--board-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    sources = {}

    def read(path, identity):
        raw = path.read_bytes()
        sources[identity] = {"path": str(path.resolve()), "sha256": hashlib.sha256(raw).hexdigest()}
        return json.loads(raw)

    activity = read(args.activity, "activity")
    bosses = read(args.boss_data, "boss_catalogue")
    if bosses["sources_sha256"]["activity_table.json"] != sources["activity"]["sha256"]:
        raise ValueError("Boss catalogue references a different activity snapshot")
    waves = {path.name: read(path, path.name) for path in args.wave_dir.glob("level_*.json")}
    board_paths = [path for path in args.board_dir.rglob("level_act3autochess_m*.json")
                   if path.name.endswith(("_sub_01.json", "_sub_03.json"))]
    if not board_paths:
        board_paths = list(args.board_dir.glob("level_act3autochess_m*.json"))
    boards = {path.name: read(path, path.name) for path in board_paths}
    result = build_facts(activity, bosses, waves, boards)
    result["sources"] = sources
    result["boss_activity_snapshot_hash_matches"] = True
    result["inspected_generator_sources"] = [{
        "path": str(Path(__file__).with_name(name).resolve()),
        "sha256": hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest(),
    } for name in ("generate_boss_catalogue.py", "generate_combat_catalogue.py", "generate_board_data.py")]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(args.output), "bosses": len(result["bosses"]),
                      "wave_templates": len(waves), "board_templates": len(boards),
                      "shop_probability_status": "unknown", "runtime_difficulty_multiplier_status": "unknown"}, ensure_ascii=False))


if __name__ == "__main__":
    main()
