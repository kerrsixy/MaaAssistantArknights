"""查询某干员放在某格、某朝向时的覆盖格。

用法：
    python tools\\range_cover.py 维什戴尔 <stage_tile_json> <x> <y> <direction> [--skill N] [--code <范围代号>]

例：
    python tools\\range_cover.py 维什戴尔 ../../resource/Arknights-Tile-Pos/ro6_n_1_4-obt-roguelike-ro6-level_rogue6_1-4.json 8 2 left
    python tools\\range_cover.py 斩业星熊 <json> 6 3 left --skill 3
    python tools\\range_cover.py - <json> 5 2 up --code 5-1

direction 取 up/right/down/left，与 autopilot 作业里的写法一致。

数据来源优先级：
1. `D:\\dsh\\明日方舟\\数据\\ak_data\\range_grids.json`：57 个范围代号的全量格形（PRTS 组件解码，推荐）
2. `operator_ranges.json`：手工补的技能范围（如斩业星熊三技能 `--skill 3`）
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

DATA = Path(r"D:\dsh\明日方舟\数据\ak_data")
if not DATA.exists():
    DATA = Path(__file__).resolve().parents[1] / "数据" / "ak_data"
RANGES = DATA / "operator_ranges.json"
GRIDS = DATA / "range_grids.json"
OPS = DATA / "ops_full.json"


def rotate(dx: int, dy: int, direction: str) -> tuple[int, int]:
    # 基准是「朝右」：前 = +x，上 = +y
    if direction == "right":
        return dx, dy
    if direction == "left":
        return -dx, dy
    if direction == "up":
        return dy, dx
    if direction == "down":
        return -dy, -dx
    raise SystemExit(f"未知朝向: {direction}")


def code_offsets(code: str) -> list[list[int]]:
    """范围代号 → 相对自身的 [dx, dy]（dy 正 = 上，与 range_cover 口径一致）。"""
    grids = json.loads(GRIDS.read_text(encoding="utf-8"))
    if code not in grids:
        raise SystemExit(f"range_grids.json 里没有代号 {code}")
    out = []
    for dc, dr in grids[code]["offsets"]:
        out.append([dc, -dr])  # 格形的 row 向下为正，这里翻成数学系
    return out


def operator_code(name: str) -> str | None:
    if not OPS.exists():
        return None
    for rec in json.loads(OPS.read_text(encoding="utf-8")):
        if rec.get("name") == name:
            rng = rec.get("range") or {}
            return (rng.get("精英2范围") or rng.get("精英1范围") or rng.get("精英0范围"))
    return None


def resolve_pattern(entry: dict, skill: int | None) -> list[list[int]]:
    """从 operator_ranges.json 的条目里取范围。

    兼容两种写法：
    - 旧写法：顶层 "pattern"
    - 新写法：'normal_attack' / 'skill3_range' 分开放，可用 --skill 选技能
    """
    if skill is None:
        if "pattern" in entry:
            return entry["pattern"]
        if "normal_attack" in entry:
            return entry["normal_attack"]["pattern"]
    else:
        key = f"skill{skill}_range"
        value = entry.get(key)
        if isinstance(value, dict) and "pattern" in value:
            return value["pattern"]
        raise SystemExit(f"该干员没有可用的技能{skill}范围（{key} 不是带 pattern 的对象）")
    raise SystemExit("条目里找不到范围数据")


def main() -> None:
    argv = sys.argv[1:]
    skill: int | None = None
    code: str | None = None
    positional: list[str] = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--skill":
            if i + 1 >= len(argv):
                raise SystemExit("--skill 后面要跟技能编号，例如 --skill 3")
            skill = int(argv[i + 1])
            i += 2
            continue
        if a.startswith("--skill="):
            skill = int(a.split("=", 1)[1])
            i += 1
            continue
        if a == "--code":
            if i + 1 >= len(argv):
                raise SystemExit("--code 后面要跟范围代号，例如 --code 5-1")
            code = argv[i + 1]
            i += 2
            continue
        if a.startswith("--code="):
            code = a.split("=", 1)[1]
            i += 1
            continue
        positional.append(a)
        i += 1

    if len(positional) != 5:
        print(__doc__)
        raise SystemExit(2)
    name, stage_path, sx, sy, direction = positional
    sx, sy = int(sx), int(sy)

    # 1) 显式 --code
    if code:
        pattern = [[dc, dy] for dc, dy in code_offsets(code)]
        label = f"code {code}"
    # 2) 技能范围：走手工表
    elif skill is not None:
        data = json.loads(RANGES.read_text(encoding="utf-8"))
        if name not in data:
            raise SystemExit(f"operator_ranges.json 里没有 {name}")
        pattern = resolve_pattern(data[name], skill)
        label = f"skill{skill}"
    # 3) 普攻：优先按干员的范围代号查全量格形
    else:
        op_code = operator_code(name)
        if op_code and GRIDS.exists():
            pattern = [[dc, dy] for dc, dy in code_offsets(op_code)]
            label = f"code {op_code}"
        else:
            data = json.loads(RANGES.read_text(encoding="utf-8"))
            if name not in data:
                raise SystemExit(f"既没有 {name} 的范围代号，operator_ranges.json 里也没有它")
            entry = data[name]
            pattern = entry["normal_attack"]["pattern"] if "normal_attack" in entry else entry["pattern"]
            label = "hand pattern"

    tiles = json.loads(Path(stage_path).read_text(encoding="utf-8"))["tiles"]
    rows, cols = len(tiles), len(tiles[0])

    covered = []
    for dx, dy in pattern:
        rx, ry = rotate(dx, dy, direction)
        x, y = sx + rx, sy + ry
        if 0 <= y < rows and 0 <= x < cols:
            t = tiles[rows - 1 - y][x]
            if t["tileKey"] != "tile_forbidden":
                covered.append((x, y, t["tileKey"]))
    covered.sort()

    print(f"{name} @ ({sx},{sy}) facing {direction} [{label}]")
    for x, y, key in covered:
        print(f"  ({x},{y}) {key}")
    print(f"total {len(covered)} tiles")


if __name__ == "__main__":
    main()
