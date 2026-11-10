# -*- coding: utf-8 -*-
"""按主人 2026-11-03 的决定，去掉「机械师必须 3 级模组」的闸门。

把 R2/R4 里的 module_gate(机械师, module_level_min=3) 换成普通 sequence，
保留顺位 priority，另加 note 说明。
"""
from __future__ import annotations

import datetime
import json
import pathlib
import shutil

P = pathlib.Path(r"D:\dsh\明日方舟\数据\blackflow_opening_rules.json")


def main():
    data = json.loads(P.read_text(encoding="utf-8"))
    changed = []
    for rule in data["rules"]:
        steps = rule.get("steps") or []
        new_steps = []
        for step in steps:
            if step.get("kind") == "module_gate" and step.get("operator") == "机械师":
                new_steps.append({
                    "kind": "sequence",
                    "voucher": step.get("voucher", "重装"),
                    "order": ["机械师"],
                    "priority": step.get("priority", 1),
                    "note": "主人 2026-11-03 决定：机械师不强求模组，可直接抓",
                })
                changed.append(rule["id"])
            else:
                new_steps.append(step)
        rule["steps"] = new_steps
    data["updated_at"] = datetime.datetime.now().isoformat(timespec="seconds")
    data["change_log"] = data.get("change_log", []) + [
        f"{datetime.datetime.now().strftime('%Y-%m-%d %H:%M')} 去掉机械师 module_level_min=3 闸门（主人指示）"
    ]
    backup = P.with_suffix(".json.bak-%s" % datetime.datetime.now().strftime("%Y%m%d-%H%M"))
    shutil.copy2(P, backup)
    P.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"changed_rules": changed, "backup": str(backup)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
