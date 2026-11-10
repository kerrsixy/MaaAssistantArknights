# -*- coding: utf-8 -*-
"""把一个批次的跑局结果汇总成 markdown 表，供进度文档「多局回归」直接粘贴。

用法：python tools/batch_report.py D:\\dsh\\eval\\bf_wd_20261104_041437 [--out file.md]
"""
import argparse
import glob
import io
import os
import re
import sys


def parse_local(tag):
    # [2026-11-04 05:24:1 → 05:24:1
    m = re.search(r"(\d{2}):(\d{2}):(\d{2})", tag)
    return m.group(0) if m else tag


def analyze(path):
    lines = io.open(path, encoding="utf-8", errors="replace").read().splitlines()
    floors = []
    stages = []          # (time, name, size, won)
    cur = None
    combat_ends = 0
    fails = 0
    dep_ok = dep_bad = 0
    for l in lines:
        m = re.search(r"NextLevel\s+\[\{\s*text:\s*([^,]+),", l)
        if m:
            name = m.group(1).strip()
            if not floors or floors[-1][1] != name:
                floors.append((l[11:19], name))
        if '"what":"StageInfo"' in l:
            m = re.search(r'"name":"([^"]+)","size":(\d+)', l)
            if m:
                cur = [l[11:19], m.group(1), int(m.group(2)), None]
                stages.append(cur)
        if '"what":"RoguelikeCombatEnd"' in l:
            combat_ends += 1
            if stages and stages[-1][3] is None:
                stages[-1][3] = True
        if "MissionFailedFlag2.png [opencv] score: 0.9" in l:
            fails += 1
            if stages:
                stages[-1][3] = False
        if "Deployment evidence" in l:
            if re.search(r"outcome\s+2", l):
                dep_ok += 1
            elif re.search(r"outcome\s+0", l):
                dep_bad += 1
    return dict(floors=floors, stages=stages, combat_ends=combat_ends,
                fails=fails, dep_ok=dep_ok, dep_bad=dep_bad)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("batch")
    ap.add_argument("--out", default="")
    a = ap.parse_args()

    rows = []
    total = dict(runs=0, depth=0, combat=0, fails=0, ok=0, bad=0)
    for p in sorted(glob.glob(os.path.join(a.batch, "run_*", "debug", "asst.log"))):
        run = p.replace("\\", "/").split("/")[-3]
        r = analyze(p)
        if not r["floors"]:
            continue
        total["runs"] += 1
        total["depth"] = max(total["depth"], len(r["floors"]))
        total["combat"] += r["combat_ends"]
        total["fails"] += r["fails"]
        total["ok"] += r["dep_ok"]
        total["bad"] += r["dep_bad"]
        seq = " → ".join(n for _, n in r["floors"])
        lost = r["fails"]
        won = max(0, r["combat_ends"] - lost)
        last = r["floors"][-1][1]
        rows.append(f"| {run} | {seq} | {won}/{lost} | {r['dep_ok']}/{r['dep_bad']} | 停在 {last} |")

    md = ["| 局 | 换层序列 | 战斗 胜/负 | 落点 成功/失败 | 结局 |", "|---|---|---|---|---|"]
    md += rows
    ok, bad = total["ok"], total["bad"]
    pct = f"{ok / (ok + bad) * 100:.1f}%" if ok + bad else "-"
    md.append("")
    md.append(f"合计：{total['runs']} 局 / 最深层数 {total['depth']} / 战斗结束 {total['combat']} 次 / "
              f"真失败 {total['fails']} / 落点成功率 {pct}（{ok}/{ok + bad}）")
    text = "\n".join(md)

    if a.out:
        io.open(a.out, "w", encoding="utf-8").write(text + "\n")
        print("written", a.out)
    else:
        print(text)


if __name__ == "__main__":
    sys.exit(main())
