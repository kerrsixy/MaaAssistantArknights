# -*- coding: utf-8 -*-
"""模组库查询 CLI（数据来自 数据/accounts/modules_example.json）。

用法::

    python tools/module_db.py list                 # 所有已开启模组
    python tools/module_db.py list --limited       # 只看特限（α/β/Δ）
    python tools/module_db.py list --level 1       # 只看非满级
    python tools/module_db.py show 号角            # 某干员的全部槽位
    python tools/module_db.py check 号角 ISW α     # 某干员某模组是否开启/几级
    python tools/module_db.py stats                # 统计
"""
from __future__ import annotations

import json
import pathlib
import sys

DB = pathlib.Path(r"D:\dsh\明日方舟\数据\accounts\modules_example.json")


def load():
    return json.loads(DB.read_text(encoding="utf-8"))


def norm(code: str) -> str:
    return " ".join(code.replace("-", " ").split()).upper().replace("Α", "α").replace("Β", "β").replace("Δ", "Δ")


def main() -> int:
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    db = load()
    ops = db["operators"]
    cmd = args[0]

    if cmd == "stats":
        rec = db["recorded"]
        print(json.dumps({"recorded": rec, "overview": db["overview_stats"]}, ensure_ascii=False, indent=2))
        return 0

    if cmd == "list":
        only_limited = "--limited" in args
        only_low = "--level" in args
        low = int(args[args.index("--level") + 1]) if only_low else None
        rows = 0
        for name, entry in ops.items():
            for s in entry["slots"]:
                if only_limited and not s["limited"]:
                    continue
                if low is not None and s["level"] >= low:
                    continue
                print("%-10s %-7s %-6s %-3s %s" % (name, s["code"], s["type"],
                                                   s["level"], s["name"] or ""))
                rows += 1
        print("-- %d 条" % rows, file=sys.stderr)
        return 0

    if cmd == "show":
        name = args[1]
        entry = ops.get(name)
        if not entry:
            print("没记录：", name, file=sys.stderr)
            return 1
        for s in entry["slots"]:
            print("%-8s type=%-6s 特限=%-2s level=%d(%s) %s" % (
                s["code"], s["type"], "是" if s["limited"] else "否",
                s["level"], s["level_text"], s["name"] or ""))
        return 0

    if cmd == "check":
        name, code = args[1], " ".join(args[2:])
        entry = ops.get(name)
        if not entry:
            print(json.dumps({"operator": name, "found": False}, ensure_ascii=False))
            return 1
        target = norm(code)
        for s in entry["slots"]:
            if norm(s["code"]) == target:
                print(json.dumps({"operator": name, "code": s["code"], "unlocked": True,
                                  "level": s["level"], "limited": s["limited"]},
                                 ensure_ascii=False))
                return 0
        unlocked = [s["code"] for s in entry["slots"]]
        print(json.dumps({"operator": name, "code": code, "unlocked": False,
                          "unlocked_slots": unlocked}, ensure_ascii=False))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
