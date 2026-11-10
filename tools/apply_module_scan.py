# -*- coding: utf-8 -*-
"""把 modules_<uid>.json 的总览扫描结果合并进账号快照。

合并字段（每个干员）：
    module_status: unlocked / locked / unknown
    module_level:  已开启槽位里的最高等级（1~3），无则 0
    module_slots:  [{code, type, limited, level, level_text}, ...]
    teixian_verified: 有 α/β/Δ（特限）槽位且已开启时为 True
    module_slot_unverified: 逐槽来自总览页角标，若没开过面板则保留 True
"""
from __future__ import annotations

import datetime
import json
import pathlib
import shutil

ACCOUNTS = pathlib.Path(r"D:\dsh\明日方舟\数据\accounts")
SNAP = ACCOUNTS / "bilibili_11979813.json"
MODDB = ACCOUNTS / "modules_11979813.json"


def main():
    snap = json.loads(SNAP.read_text(encoding="utf-8"))
    db = json.loads(MODDB.read_text(encoding="utf-8"))
    ops = snap.get("operators") or {}
    mods = db["operators"]

    applied = 0
    for name, entry in mods.items():
        oper = ops.get(name)
        if not oper:
            print("WARN 快照里没有这个干员:", name)
            continue
        slots = entry["slots"]
        lv_max = max((s["level"] for s in slots), default=0)
        teixian = any(s["limited"] and s["level"] >= 3 for s in slots)
        oper["module_status"] = "unlocked"
        oper["module_level"] = lv_max
        oper["module_slots"] = [
            {"code": s["code"], "type": s["type"], "limited": s["limited"],
             "name": s["name"], "unlocked": True, "level": s["level"],
             "level_text": s["level_text"]}
            for s in slots
        ]
        oper["module_source"] = "overview_badge"
        if teixian:
            oper["teixian_verified"] = True
            oper["teixian_evidence"] = {
                "screen": "档案>模组陈列室>干员模组总览",
                "detail": "特限槽位已开启且等级 3（MAX）",
            }
        applied += 1

    snap["modules_scanned_at"] = datetime.datetime.now().isoformat(timespec="seconds")
    snap["modules_scan_source"] = str(MODDB)
    snap["module_scan_note"] = "逐槽等级来自「干员模组总览」角标，2026-11-03 深夜全量转录"
    snap["module_multi_slot_warning"] = (
        "一个干员可有多槽（X/Y/α/β/Δ）；module_level 取最高槽位等级，"
        "逐槽数据见 module_slots"
    )
    backup = SNAP.with_suffix(".json.bak-%s" % datetime.datetime.now().strftime("%Y%m%d-%H%M"))
    shutil.copy2(SNAP, backup)
    SNAP.write_text(json.dumps(snap, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"applied": applied, "backup": str(backup)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
