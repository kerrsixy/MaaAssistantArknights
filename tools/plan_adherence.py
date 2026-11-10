# -*- coding: utf-8 -*-
"""从实机日志里核对「作业落点有没有被照做」。

用法::

    python tools/plan_adherence.py <asst.log> [--json]

做的事：
1. 抓 `Deployment evidence <关卡> <干员> (<x>, <y>)` 行；
2. 找到 `resource/roguelike/BlackFlow/autopilot/<关卡>.json`；
3. 判定每条 evidence 是否落在计划位上、该干员是否属于该手的 groups；
4. 打印「打卡表」：命中 / 位置不对 / 该手没有这个人 / 关卡没有作业。
"""

import argparse
import io
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
AUTO = ROOT / "resource" / "roguelike" / "BlackFlow" / "autopilot"
RECRUIT = ROOT / "resource" / "roguelike" / "BlackFlow" / "recruitment.json"


def load_oper_groups():
    """干员名 -> 它所属的所有 group 名（recruitment.json 每个块的 name 就是一个 group）。"""
    out = {}
    try:
        d = json.load(io.open(RECRUIT, encoding="utf-8"))
    except Exception:
        return out
    for block in d.get("priority", []):
        gname = block.get("name")
        for oper in block.get("opers", []):
            n = oper.get("name")
            if n:
                out.setdefault(n, set()).add(gname)
    return out

EVID = re.compile(r"Deployment evidence\s+(?P<stage>\S+)\s+(?P<oper>\S+)\s+\((?P<x>\d+),\s*(?P<y>\d+)\)")


def load_plans():
    plans = {}
    for p in AUTO.glob("*.json"):
        try:
            d = json.load(io.open(p, encoding="utf-8"))
        except Exception:
            continue
        name = d.get("stage_name") or p.stem
        plans[name] = d
    return plans


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log", help="run_x/debug/asst.log")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    text = io.open(args.log, encoding="utf-8", errors="replace").read()
    plans = load_plans()
    oper_groups = load_oper_groups()

    rows = []
    for m in EVID.finditer(text):
        rows.append(
            {
                "stage": m.group("stage"),
                "oper": m.group("oper"),
                "pos": [int(m.group("x")), int(m.group("y"))],
            }
        )

    by_stage = {}
    for r in rows:
        by_stage.setdefault(r["stage"], []).append(r)

    report = []
    for stage, evs in by_stage.items():
        plan = plans.get(stage)
        if not plan:
            report.append({"stage": stage, "status": "no_plan", "count": len(evs)})
            continue
        hands = plan.get("deploy_plan", [])
        loc2hand = {tuple(h.get("location", [])): i + 1 for i, h in enumerate(hands)}
        hits, off_plan, wrong_oper = 0, [], []
        for e in evs:
            h = loc2hand.get(tuple(e["pos"]))
            if not h:
                off_plan.append((e["oper"], e["pos"]))
                continue
            groups = hands[h - 1].get("groups", [])
            mine = oper_groups.get(e["oper"], set())
            if e["oper"] in groups or (mine & set(groups)):
                hits += 1
            else:
                wrong_oper.append((e["oper"], e["pos"], h, groups))
        report.append(
            {
                "stage": stage,
                "status": "checked",
                "evidence": len(evs),
                "hit": hits,
                "off_plan": off_plan,
                "oper_not_in_hand": wrong_oper,
                "hands": [
                    {"n": i + 1, "loc": h.get("location"), "dir": h.get("direction"), "groups": h.get("groups")}
                    for i, h in enumerate(hands)
                ],
                "first_evidence": evs[0] if evs else None,
            }
        )

    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return

    for r in report:
        print(f"== {r['stage']} ==")
        if r["status"] == "no_plan":
            print(f"  没有作业（evidence {r['count']} 条，全靠兜底）")
            continue
        print(f"  evidence {r['evidence']} 条，命中计划位 {r['hit']} 条")
        fe = r["first_evidence"]
        if fe:
            print(f"  第一条：{fe['oper']} {tuple(fe['pos'])}")
        if r["off_plan"]:
            print("  不在任何一手的位置：")
            for oper, pos in r["off_plan"][:10]:
                print(f"    {oper} {tuple(pos)}")
        if r["oper_not_in_hand"]:
            print("  位置对但该手没有这个人：")
            for oper, pos, h, groups in r["oper_not_in_hand"][:10]:
                print(f"    第{h}手 {tuple(pos)} {oper}（该手组：{'/'.join(groups)}）")
        print("  计划：")
        for h in r["hands"]:
            print(f"    {h['n']}. {tuple(h['loc'])} {h['dir']}  {'/'.join(h['groups'])}")
        print()


if __name__ == "__main__":
    sys.exit(main())
