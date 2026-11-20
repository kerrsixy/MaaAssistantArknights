"""Compile current-season client terrain; never infer walking from buildability."""
import argparse
import hashlib
import json
from pathlib import Path


def board_record(path):
    raw = path.read_bytes()
    level = json.loads(raw)
    data = level["mapData"]
    rows = []
    homes = [(col, row) for row, cells in enumerate(data['map']) for col, tile in enumerate(cells[:11])
             if data['tiles'][tile]['tileKey'] == 'tile_end']
    if len(homes) != 1:
        raise ValueError('Expected one left-side player home anchor')
    origin = homes[0][1] - 4
    if origin < 0 or origin + 5 > len(data['map']):
        raise ValueError('Player home does not anchor a five-row field')
    for row in data["map"][origin:origin + 5]:
        result = ""
        for index in row[:11]:
            tile = data["tiles"][index]
            kind, build, passable = tile["tileKey"], tile["buildableType"], tile["passableMask"]
            if kind == "tile_start":
                code = "S"
            elif kind == "tile_end":
                code = "E"
            elif kind in ("tile_achand", "tile_acbag"):
                code = " "
            elif kind == "tile_mire":
                code = "M"
            elif build == "RANGED":
                code = "R"
            elif build in ("ALL", "MELEE"):
                code = "G" if passable == "ALL" else "F"
            else:
                code = "." if passable == "ALL" else " "
            result += code
        rows.append(result)
    devices = []
    for section in (level.get("predefines") or {}, level.get("hardPredefines") or {}):
        for token in section.get("tokenInsts") or []:
            if token.get("hidden", False):
                continue
            kind = token.get("inst", {}).get("characterKey", "")
            if kind not in ("trap_1105_accrate", "trap_1106_achplat"):
                continue
            # Client positions count upwards; the map array and our grid
            # count downwards. Only this side's visible five rows are used.
            row = len(data["map"]) - 1 - token["position"]["row"] - origin
            col = token["position"]["col"]
            if not (0 <= row < 5 and 0 <= col < 11):
                continue
            code = "X" if kind == "trap_1105_accrate" else "R"
            rows[row] = rows[row][:col] + code + rows[row][col + 1:]
            devices.append({"kind": kind, "row": row, "col": col, "terrain": code})
    return {"map_id": path.parent.name[-3:], "rows": rows, 'source_row_origin': origin,
            "devices": devices, "source": (Path(path.parent.name) / path.name).as_posix(), "sha256": hashlib.sha256(raw).hexdigest()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--levels", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    boards = [board_record(p) for p in sorted(args.levels.glob(
        "level_act3autochess_m*/level_act3autochess_m*_sub_01.json"))]
    boss_boards = [board_record(p) for p in sorted(args.levels.glob(
        "level_act3autochess_m*/level_act3autochess_m*_sub_03.json"))]
    if len(boards) != 10 or len(boss_boards) != 10 or any(
            len(b['rows']) != 5 or any(len(r) != 11 for r in b['rows']) for b in boards + boss_boards):
        raise ValueError("Expected ten current-season normal and ten boss preparation boards")
    lines = ["#pragma once", "", "#include <array>", "#include <string_view>", "",
             "namespace asst::spa", "{", "// Generated from home-anchored act3autochess sub_01 and sub_03 by generate_board_data.py.",
             "// G: ground; F: fence; R: ranged-only; M: mire; .: walk-only; X: blocking device.",
             "struct BoardTerrain { std::string_view id; std::array<std::string_view, 5> rows; };",
             "inline constexpr std::array<BoardTerrain, 10> BoardTerrains {{"]
    for b in boards:
        lines.append('    { "' + b["map_id"] + '", {{ ' + ", ".join(json.dumps(r) for r in b["rows"]) + ' }} },')
    lines += ['}};', 'inline constexpr std::array<BoardTerrain, 10> BossBoardTerrains {{']
    for b in boss_boards:
        lines.append('    { "' + b['map_id'] + '", {{ ' + ', '.join(json.dumps(r) for r in b['rows']) + ' }} },')
    lines += ["}};", "}", ""]
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text("\n".join(lines), encoding="utf-8")
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps({"coordinate_space": "home_anchored_player_row_col", "boards": boards,
                                        'boss_boards': boss_boards},
                                        ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"boards": len(boards), 'boss_boards': len(boss_boards),
                      "tiles": sum(len(r) for b in boards + boss_boards for r in b["rows"])}))


if __name__ == "__main__":
    main()
