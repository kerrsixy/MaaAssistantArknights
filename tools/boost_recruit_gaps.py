"""给 BlackFlow 招募优先级加「缺位补偿」：缺先锋/回费、缺单奶/群奶时，对应职业小幅提权。

用法：python tools/boost_recruit_gaps.py [--apply]
不带 --apply 只打印将要改的内容。
"""
import io
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CFG = ROOT / "resource" / "roguelike" / "BlackFlow" / "recruitment.json"
BACKUP = CFG.with_suffix(".json.bak-20261104")

GAP_RULES = {
    # block name -> (offset, groups, doc)
    "投锋": (80, ["回费"], "缺回费（先锋）时小幅提权"),
    "挡人先锋": (80, ["回费"], "缺回费（先锋）时小幅提权"),
    "单奶": (80, ["单奶", "群奶"], "缺治疗时小幅提权"),
    "群奶": (80, ["单奶", "群奶"], "缺治疗时小幅提权"),
}


def main() -> int:
    apply = "--apply" in sys.argv
    data = json.loads(CFG.read_text(encoding="utf-8"))
    changed = 0
    for block in data["priority"]:
        name = block.get("name")
        if name not in GAP_RULES:
            continue
        offset, groups, doc = GAP_RULES[name]
        for oper in block["opers"]:
            if not oper.get("recruit_priority"):
                continue
            offsets = oper.setdefault("recruit_priority_offsets", [])
            if any(o.get("doc") == doc for o in offsets):
                continue
            offsets.append(
                {"groups": groups, "is_less": True, "threshold": 1, "offset": offset, "doc": doc}
            )
            changed += 1
            print(f"{name}/{oper['name']} +{offset} when {'/'.join(groups)}<=1")
    print(f"changed={changed}")
    if apply and changed:
        if not BACKUP.exists():
            shutil.copy2(CFG, BACKUP)
            print(f"backup -> {BACKUP.name}")
        CFG.write_text(json.dumps(data, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")
        print("applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
