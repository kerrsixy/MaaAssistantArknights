"""Compile solo boss routes from the same pinned client data as the season model."""

import argparse
import hashlib
import json
from pathlib import Path


def route_record(level, enemy_id):
    homes = [(col, len(level['mapData']['map']) - 1 - row)
             for row, cells in enumerate(level['mapData']['map']) for col, tile in enumerate(cells)
             if level['mapData']['tiles'][tile]['tileKey'] == 'tile_end']
    if not homes:
        raise ValueError('Boss template has no home anchor')
    home = min(homes)
    spawns = [action for wave in level['waves'] for fragment in wave['fragments']
              for action in fragment['actions']
              if action['actionType'] == 'SPAWN' and action['key'] == enemy_id]
    indices = {spawn['routeIndex'] for spawn in spawns}
    if len(indices) != 1 or any(spawn['count'] != 1 for spawn in spawns):
        raise ValueError('Boss needs a unique single-entity route')
    route = level['routes'][next(iter(indices))]
    time_limit = level['options']['maxPlayTime']
    wait = sum(c['time'] for c in route['checkpoints'] if c['type'] == 'WAIT_FOR_SECONDS')
    stationary = (route['startPosition'] == route['endPosition'] and time_limit > 0
                  and all(c['type'] == 'WAIT_FOR_SECONDS' for c in route['checkpoints']) and wait >= time_limit)
    if any(route['spawnRandomRange'].values()):
        raise ValueError('Random boss spawn must remain an explicit region')
    return dict(template_home=dict(col=home[0], row=home[1]), route=route,
                stationary_route=stationary, time_limit_seconds=time_limit)


def build_catalogue(activity, handbook, enemies, levels):
    season = activity['activity']['AUTOCHESS']['act3autochess']['V3_1']
    refs = {}
    for mode, rounds in season['battleDataDict'].items():
        if not mode.startswith('mode_single_'):
            continue
        for number, entries in rounds.items():
            for entry in entries:
                if entry['bossId']:
                    refs.setdefault((entry['bossId'], entry['levelId'].lower()), {}).setdefault(mode, set()).add(int(number))
    enemy_db = {row['Key']: row['Value'] for row in enemies['enemies']}
    result = []
    for (boss_id, level_id), rounds_by_mode in sorted(refs.items()):
        info = activity['autoChessData']['bossInfoDict'][boss_id]
        enemy_id = info['enemyId']
        level = levels[level_id.rsplit('/', 1)[1] + '.json']
        entry = season['bossInfoDict'][boss_id]
        record = route_record(level, enemy_id)
        db_level = next(ref['level'] for ref in level['enemyDbRefs'] if ref['id'] == enemy_id)
        stats = next(row['enemyData'] for row in enemy_db[enemy_id] if row['level'] == db_level)
        book = handbook['enemyData'][info['handbookEnemyId']]
        record.update(boss_id=boss_id, enemy_id=enemy_id, family_name=book['name'],
                      level_id=level_id, rounds=sorted(set().union(*rounds_by_mode.values())),
                      rounds_by_mode={mode: sorted(rounds) for mode, rounds in rounds_by_mode.items()},
                      hidden=entry['isHidingBoss'],
                      hp_by_mode=entry['bloodDataDict'], enemy_database_level=db_level,
                      enemy_base=stats, abilities=book['abilityList'])
        result.append(record)
    return dict(schema_version=1, season='act3autochess/V3_1', bosses=result,
                coordinate_rule='Translate template home to registered player home; invert client row upwards.',
                limitations=['Route waiting does not prove all scripted abilities keep the boss still.',
                             'Enemy base attributes exclude mode factors and runtime effects.',
                             'Advertised family alone does not identify its hidden variant.'])


def cpp_header(data):
    lines = ['#pragma once', '', '#include <array>', '#include <string_view>', '', 'namespace asst::spa', '{',
             '// Generated from pinned solo battle templates and season bossInfoDict.',
             'struct SeasonBossRecord {',
             'std::string_view id, enemy_id, family_name;',
             'bool hidden, stationary_route;',
             'int template_home_col, template_home_row, spawn_col, spawn_row;',
             'double spawn_offset_x, spawn_offset_y, time_limit_seconds;',
             'std::array<int, 20> hp_by_difficulty;', '};',
             f'inline constexpr std::array<SeasonBossRecord, {len(data["bosses"])}> SeasonBossCatalogue {{{{']
    for row in data['bosses']:
        home, start, offset = row['template_home'], row['route']['startPosition'], row['route']['spawnOffset']
        hp = [row['hp_by_mode'].get(f'mode_single_{d}', 0) for d in range(1, 21)]
        values = [json.dumps(row[k], ensure_ascii=False) for k in ['boss_id', 'enemy_id', 'family_name']]
        values += [str(row[k]).lower() for k in ['hidden', 'stationary_route']]
        values += [str(x) for x in [home['col'], home['row'], start['col'], start['row'],
                                   offset['x'], offset['y'], row['time_limit_seconds']]]
        lines.append('    { ' + ', '.join(values) + ', {{ ' + ', '.join(map(str, hp)) + ' }} },')
    lines += ['}};', '}', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--activity', type=Path, required=True)
    parser.add_argument('--source-dir', type=Path, required=True)
    parser.add_argument('--json-output', type=Path, required=True)
    parser.add_argument('--header-output', type=Path, required=True)
    args = parser.parse_args()
    sources = {}

    def read(path):
        raw = path.read_bytes()
        sources[path.name] = hashlib.sha256(raw).hexdigest()
        return json.loads(raw)

    activity = read(args.activity)
    handbook = read(args.source_dir / 'enemy_handbook_table.json')
    enemies = read(args.source_dir / 'enemy_database.json')
    levels = {path.name: read(path) for path in args.source_dir.glob('level_*.json')}
    data = build_catalogue(activity, handbook, enemies, levels)
    data['sources_sha256'] = sources
    args.json_output.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    args.header_output.write_text(cpp_header(data), encoding='utf-8')
    print(json.dumps(dict(bosses=len(data['bosses']), hidden=sum(row['hidden'] for row in data['bosses']))))


if __name__ == '__main__':
    main()
