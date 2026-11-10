# -*- coding: utf-8 -*-
"""给 BlackFlow 的商店购买表加「招募券」条目。

背景：诡意行商会卖 近卫/医疗/先锋/重装/狙击/术师/特种/辅助 招募券（4~8 零件），
用券招人不需要「希望」，而希望正是限制编队规模的东西。
原 shopping.json 的 buy_tables 里一条招募券都没有，所以 MAA 从来不会买。

插在移动类道具（老妈妈的融雪）之后、其余零件类道具之前。
"""
import io
import json
import pathlib
import shutil
import sys

PATH = pathlib.Path(
    r"D:\dsh\明日方舟\代码\MaaAssistantArknights\resource\roguelike\BlackFlow\shopping.json"
)

VOUCHERS = [
    {"name": "近卫招募券"},
    {"name": "重装招募券"},
    {"name": "医疗招募券"},
    {"name": "狙击招募券"},
    {"name": "术师招募券"},
    {"name": "特种招募券"},
    {"name": "先锋招募券"},
    {"name": "辅助招募券"},
]
ANCHOR = "老妈妈的融雪"


def insert_after(table, anchor, items):
    names = [it.get("name") for it in table]
    if any(v["name"] in names for v in items):
        return False
    idx = names.index(anchor) if anchor in names else -1
    pos = idx + 1 if idx >= 0 else len(table)
    table[pos:pos] = [dict(it) for it in items]
    return True


def main():
    apply = "--apply" in sys.argv
    data = json.loads(io.open(PATH, encoding="utf-8").read())
    changed = []
    for key, table in data.get("buy_tables", {}).items():
        if key in ("scrap_accumulate", "scrap_floor1", "scrap_movement"):
            if insert_after(table, ANCHOR, VOUCHERS):
                changed.append(key)
    print("changed tables:", changed or "(none)")
    if not changed:
        return 0
    if not apply:
        print("dry-run; add --apply to write")
        return 0
    bak = PATH.with_suffix(".json.bak-vouchers")
    if not bak.exists():
        shutil.copy2(PATH, bak)
    io.open(PATH, "w", encoding="utf-8").write(
        json.dumps(data, ensure_ascii=False, indent=4) + "\n"
    )
    print("written", PATH, "backup", bak.name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
