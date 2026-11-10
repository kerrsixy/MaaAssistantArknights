# -*- coding: utf-8 -*-
"""在 common_navigation 里加一条「前两层优先走招募节点」的偏好规则。

用法：
    python patch_strategy_employ.py            # 预览
    python patch_strategy_employ.py --apply    # 写盘（先备份）
"""
import io
import json
import shutil
import sys

PATH = r"D:\dsh\明日方舟\代码\MaaAssistantArknights\resource\roguelike\BlackFlow\strategy.json"
RULE = {
    "id": "prefer_employ_nodes_early",
    "description": "前两层编队还薄，优先经过「雇」节点补人",
    "kind": "prefer",
    "tier": "development",
    "rank": 20,
    "when": {"fact": "current_floor", "op": "le", "value": 2},
    "candidate_if": {
        "fact": "candidate.route_node_types",
        "op": "contains",
        "value": "employ",
    },
}

d = json.load(io.open(PATH, encoding="utf-8"))
mod = next(m for m in d["modules"] if m["id"] == "common_navigation")
rules = mod["rules"]
ids = [r.get("id") for r in rules]
print("existing rules:", ids)

if RULE["id"] in ids:
    print("already patched; nothing to do")
    sys.exit(0)

idx = ids.index("light_reveals_three") if "light_reveals_three" in ids else len(rules)
rules.insert(idx + 1, RULE)
print("will insert at", idx + 1, "total rules", len(rules))

if "--apply" not in sys.argv:
    print(json.dumps(RULE, ensure_ascii=False, indent=1))
    print("preview only; pass --apply to write")
    sys.exit(0)

shutil.copy2(PATH, PATH + ".bak-employ")
io.open(PATH, "w", encoding="utf-8").write(json.dumps(d, ensure_ascii=False, indent=2))
print("written")
