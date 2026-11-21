"""Join this season's chess status with original client combat records.

Base stats deliberately exclude account training bonuses, potential, talents,
skills, modules and pact/equipment buffs. Those inputs remain separate so an
unobserved account setting cannot silently become a measured combat value.
"""

import argparse
import hashlib
import json
import math
import re
from pathlib import Path


STAT_KEYS = ("maxHp", "atk", "def", "magicResistance", "blockCnt",
             "baseAttackTime", "attackSpeed", "cost", "respawnTime")
INTEGER_STATS = {"maxHp", "atk", "def", "blockCnt", "cost", "respawnTime"}


def interpolate_stats(character, status):
    phase = int(status["evolvePhase"].removeprefix("PHASE_"))
    level = status["charLevel"]
    if not 0 <= phase < len(character["phases"]):
        raise ValueError("Chess training phase is absent from the character record")
    training = character["phases"][phase]
    frames = training["attributesKeyFrames"]
    if not frames or not 1 <= level <= training["maxLevel"]:
        raise ValueError("Chess training level is outside its keyframes")
    left = max((f for f in frames if f["level"] <= level), key=lambda f: f["level"])
    right = min((f for f in frames if f["level"] >= level), key=lambda f: f["level"])
    ratio = ((level - left["level"]) / (right["level"] - left["level"])
             if left["level"] != right["level"] else 0)
    result = {}
    for key in STAT_KEYS:
        value = left["data"][key] + ratio * (right["data"][key] - left["data"][key])
        if not math.isfinite(value):
            raise ValueError("Nonfinite combat stat")
        result[key] = math.floor(value + .5) if key in INTEGER_STATS else round(value, 6)
    if result["maxHp"] <= 0 or result["blockCnt"] < 0 or result["baseAttackTime"] <= 0:
        raise ValueError("Invalid baseline combat stats")
    return result


def build_catalogue(tables):
    season = tables["activity_table"]["activity"]["AUTOCHESS"]["act3autochess"]["V3_1"]
    chars = tables["character_table"]
    skills = tables["skill_table"]
    ranges = tables["range_table"]
    equips = tables["battle_equip_table"]
    bonds = season["bondInfoDict"]
    result = []
    excluded = []
    seen = set()
    for chess_id, shop in season["charShopChessDatas"].items():
        if not shop.get("charId"):
            excluded.append(dict(chess_id=chess_id, hidden=shop["isHidden"], type=shop["chessType"]))
            continue
        char = chars[shop["charId"]]
        name = char["name"]
        if not name or name in seen:
            raise ValueError("Duplicate visible operator identity")
        seen.add(name)
        row = dict(name=name, char_id=shop["charId"], tier=shop["chessLevel"],
                   hidden_in_shop=shop["isHidden"],
                   profession=char["profession"], sub_profession=char["subProfessionId"],
                   class_description=char["description"],
                   default_skill_index=shop["defaultSkillIndex"],
                   default_module_id=shop["defaultUniEquipId"], backup={
                       key: value for key, value in shop.items() if key.startswith("backup")},
                   potential_ranks=char["potentialRanks"],
                   trait=char.get("trait"), talents=char.get("talents"))
        for form, form_id in (("initial", chess_id), ("elite", shop["goldenChessId"])):
            chess = season["charChessDataDict"][form_id]
            status = chess["status"]
            if status["favorPoint"] != 0:
                raise ValueError("Nonzero trust needs an explicit attribute contribution")
            phase = int(status["evolvePhase"].removeprefix("PHASE_"))
            range_id = char["phases"][phase]["rangeId"]
            choices = []
            for index, skill in enumerate(char["skills"]):
                condition = skill["unlockCond"]
                unlock = int(condition["phase"].removeprefix("PHASE_"))
                if unlock > phase or (unlock == phase and condition["level"] > status["charLevel"]):
                    continue
                skill_id = skill["skillId"]
                data = skills[skill_id]["levels"][status["skillLevel"] - 1]
                description = re.sub(r'<[^>]*>', '', data['description'] or '')
                replacement = bool(data['rangeId'] and re.search(
                    r'攻击范围(?:扩大|加大|延伸|改变|延长|变大|扩展)|攻击距离(?:加长|延长)', description))
                movement = skill_id == 'skchr_ulpia_3' or bool(re.search(r'(?:自身|干员|会)移动到', description))
                choices.append(dict(index=index, skill_id=skill_id, data=data,
                    declared_range_grid=ranges[data['rangeId']]['grids'] if data['rangeId'] else [],
                    attack_range_replacement=(replacement and not movement and char['profession'] != 'MEDIC'
                                              and char['subProfessionId'] != 'bard'),
                    self_movement=movement))
            if not any(c["index"] == shop["defaultSkillIndex"] for c in choices):
                raise ValueError("Default skill is unavailable at the season training status")
            module_id = shop["defaultUniEquipId"]
            module = None
            if status["equipLevel"] > 0 and module_id:
                module = next((p for p in equips[module_id]["phases"]
                               if p["equipLevel"] == status["equipLevel"]), None)
                if module is None:
                    raise ValueError("Default module level is absent")
            row[form] = dict(chess_id=form_id, status=status,
                equipment_slots=chess['equipSlotNum'],
                stats_base=interpolate_stats(char, status), range_id=range_id,
                range_grid=ranges[range_id]["grids"], skills=choices,
                default_module=module, pacts=[bonds[p]["name"] for p in chess["bondIds"]],
                garrisons=[dict(id=g, **season["garrisonDataDict"][g]) for g in chess["garrisonIds"]])
        threshold = season["charChessDataDict"][chess_id]["upgradeNum"]
        if threshold not in (2, 3):
            raise ValueError("Unexpected promotion recipe requires explicit support")
        row["promotion_copies"] = threshold
        result.append(row)
    equipment = []
    for id, record in season['trapChessDataDict'].items():
        effect = season['effectInfoDataDict'][record['effectId']]
        equipment.append(dict(chess_id=id, name=effect['effectName'], **record))
    strategies = []
    for id, record in season['bandDataListDict'].items():
        strategies.append(dict(**tables['activity_table']['autoChessData']['bandDataDict'][id], **{
            key: value for key, value in record.items() if key != 'bandId'}))
    return dict(schema_version=1, season="act3autochess/V3_1",
        baseline_excludes=["account_cultivation", "potential", "talents", "skill_effects",
                           "module_effects", "pact_effects", "equipment_effects"],
        operators=sorted(result, key=lambda r: (r["tier"], r["initial"]["chess_id"])),
        excluded_shop_records=excluded,
        cultivation_effects=tables["activity_table"]["autoChessData"]["cultivateEffectList"],
        pacts=season['bondInfoDict'], strategies=strategies, equipment=equipment,
        effects={id: dict(metadata=record, parameters=season['effectDescParamDataDict'].get(id),
                         buffs=season['effectBuffInfoDataDict'].get(id, []),
                         skill=season['skillInfoDataDict'].get(id))
                 for id, record in season['effectInfoDataDict'].items()},
        choices=season['effectChoiceInfoDict'], modes=season['modeDataDict'],
        shop_parameters=season['shopCharChessInfoData'], shop_layout=season['shopLevelDataDict'],
        global_constants=tables['activity_table']['autoChessData']['constData'])


def cpp_header(catalogue):
    lines = ["#pragma once", "", "#include <array>", "#include <span>", "#include <string_view>", "#include <utility>", "",
        "// Generated from act3autochess/V3_1 chess training + character keyframes.",
        "// tools/SPA/generate_combat_catalogue.py; source SHA256 in SPACombatData.json.",
        "// Base values exclude account, potential, talent, module, skill and pact effects.",
        "namespace asst::spa", "{"]
    ranges = {form['range_id']: form['range_grid'] for row in catalogue['operators']
              for form in (row['initial'], row['elite'])}
    range_names = {id: f'SeasonAttackRange{index}' for index, id in enumerate(sorted(ranges))}
    for id in sorted(ranges):
        grid = ranges[id]
        lines += [f'inline constexpr std::array<std::pair<int, int>, {len(grid)}> {range_names[id]} {{{{',
                  '    ' + ', '.join(f'{{ {cell["col"]}, {cell["row"]} }}' for cell in grid), '}};', '']
    lines += ["struct SeasonCombatStats", "{",
        "    int phase;", "    int level;", "    int max_hp;", "    int attack;", "    int defense;",
        "    double magic_resistance;", "    int block_count;", "    double attack_interval;",
        "    int equipment_slots;",
        "    std::span<const std::pair<int, int>> basic_range;", "};", "",
        "struct SeasonOperatorCombatData", "{", "    std::string_view name;", "    int tier;",
        "    int promotion_copies;", "    SeasonCombatStats initial;", "    SeasonCombatStats elite;", "};", "",
        f"inline constexpr std::array<SeasonOperatorCombatData, {len(catalogue['operators'])}> SeasonOperatorCombatCatalogue {{{{"]
    def stats(form):
        v, s = form["stats_base"], form["status"]
        return "{ " + ", ".join(map(str, [int(s["evolvePhase"][-1]), s["charLevel"],
            v["maxHp"], v["atk"], v["def"], v["magicResistance"], v["blockCnt"], v["baseAttackTime"],
            form['equipment_slots'],
            range_names[form['range_id']]])) + " }"
    for row in catalogue["operators"]:
        lines.append("    { " + json.dumps(row["name"], ensure_ascii=False) +
            f", {row['tier']}, {row['promotion_copies']}, {stats(row['initial'])}, {stats(row['elite'])} }},")
    lines += ["}};", "}", ""]
    return "\n".join(lines)


def cpp_mode_header(catalogue):
    pacts = sorted(catalogue['pacts'])
    # The tutorial uses legacy bonds that are not in this season's catalogue.
    # Public solo/quick/joint modes must all use current known pact identities.
    modes = {id: mode for id, mode in catalogue['modes'].items()
             if id.startswith(('mode_single_', 'mode_multi_'))}
    if len(pacts) > 32:
        raise ValueError('Mode pact bit mask exceeds uint32_t')
    indexes = {pact: index for index, pact in enumerate(pacts)}
    lines = ['#pragma once', '', '#include <array>', '#include <cstdint>', '#include <string_view>', '',
             '// Generated from act3autochess/V3_1 modeDataDict and bondInfoDict.',
             '// Source SHA256 is stored in SPACombatData.json. Unknown modes remain unknown.',
             'namespace asst::spa', '{',
             f'inline constexpr std::array<std::string_view, {len(pacts)}> SeasonModePactNames {{{{',
             '    ' + ', '.join(json.dumps(catalogue['pacts'][pact]['name'], ensure_ascii=False) for pact in pacts),
             '}};', '', 'struct SeasonModePactData', '{', '    std::string_view id;',
             '    int difficulty;', '    std::uint32_t active_pacts;', '    std::uint32_t inactive_pacts;', '};', '',
             f'inline constexpr std::array<SeasonModePactData, {len(modes)}> SeasonModePactCatalogue {{{{']
    for id, mode in sorted(modes.items()):
        active, inactive = set(mode.get('activeBondIdList') or []), set(mode.get('inactiveBondIdList') or [])
        if active & inactive or (active | inactive) - indexes.keys():
            raise ValueError(f'Contradictory or unknown mode pact: {id}')
        masks = [sum(1 << indexes[pact] for pact in values) for values in (active, inactive)]
        lines.append(f'    {{ {json.dumps(id)}, {mode["difficulty"]}, 0x{masks[0]:08x}u, 0x{masks[1]:08x}u }},')
    lines += ['}};', '}', '']
    return '\n'.join(lines)


def cpp_skill_header(catalogue):
    lines = ['#pragma once', '', '#include <array>', '#include <span>', '#include <string_view>',
             '#include <utility>', '', 'namespace asst::spa', '{',
             '// Generated default skills for both season training forms. Timings exclude runtime modifiers.',
             'enum class SeasonSkillCharge { Time, Attack, Defense, Other };',
             'struct SeasonDefaultSkill {',
             'std::string_view operator_name, id, name;',
             'bool elite, attack_range_replacement, self_movement;',
             'SeasonSkillCharge charge;',
             'double sp_cost, initial_sp, recovery_per_second, duration_seconds;',
             'std::span<const std::pair<int, int>> declared_range;', '};']
    ranges = {}
    rows = []
    for operator in catalogue['operators']:
        for form in ['initial', 'elite']:
            skill = next(row for row in operator[form]['skills'] if row['index'] == operator['default_skill_index'])
            grid = tuple((g['col'], g['row']) for g in skill['declared_range_grid'])
            ranges.setdefault(grid, len(ranges))
            data, sp = skill['data'], skill['data']['spData']
            charge = {'INCREASE_WITH_TIME': 'Time', 'INCREASE_WHEN_ATTACK': 'Attack',
                      'INCREASE_WHEN_TAKEN_DAMAGE': 'Defense'}.get(sp['spType'], 'Other')
            values = [json.dumps(value, ensure_ascii=False) for value in [operator['name'], skill['skill_id'], data['name']]]
            values += [str(value).lower() for value in [form == 'elite', skill['attack_range_replacement'], skill['self_movement']]]
            values += ['SeasonSkillCharge::' + charge]
            values += [str(value) for value in [sp['spCost'], sp['initSp'], sp['increment'], data['duration']]]
            values += [f'SeasonDeclaredSkillRange{ranges[grid]}']
            rows.append('    { ' + ', '.join(values) + ' },')
    for grid, index in ranges.items():
        cells = ', '.join('{ ' + str(x) + ', ' + str(y) + ' }' for x, y in grid)
        lines += [f'inline constexpr std::array<std::pair<int, int>, {len(grid)}> SeasonDeclaredSkillRange{index} {{{{ {cells} }}}};']
    lines += [f'inline constexpr std::array<SeasonDefaultSkill, {len(rows)}> SeasonDefaultSkills {{{{', *rows, '}};', '}', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--json-output", type=Path, required=True)
    parser.add_argument("--header-output", type=Path, required=True)
    parser.add_argument("--mode-header-output", type=Path)
    parser.add_argument("--skill-header-output", type=Path)
    args = parser.parse_args()
    tables, sources = {}, {}
    for name in ("activity_table", "character_table", "skill_table", "battle_equip_table", "range_table"):
        raw = (args.source_dir / (name + ".json")).read_bytes()
        tables[name] = json.loads(raw)
        sources[name] = hashlib.sha256(raw).hexdigest()
    catalogue = build_catalogue(tables)
    catalogue["sources_sha256"] = sources
    args.json_output.write_text(json.dumps(catalogue, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    args.header_output.write_text(cpp_header(catalogue), encoding="utf-8")
    mode_output = args.mode_header_output or args.header_output.with_name('SPAModeData.h')
    mode_output.write_text(cpp_mode_header(catalogue), encoding='utf-8')
    skill_output = args.skill_header_output or args.header_output.with_name('SPAOperatorSkillData.h')
    skill_output.write_text(cpp_skill_header(catalogue), encoding='utf-8')
    print(json.dumps(dict(operators=len(catalogue["operators"]),
        excluded=len(catalogue["excluded_shop_records"]), source_sha256=sources), ensure_ascii=False))


if __name__ == "__main__":
    main()
