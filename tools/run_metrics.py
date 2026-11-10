# -*- coding: utf-8 -*-
"""按 run 统计：节点类型、招募/战斗次数（只算 SubTaskStart）、最深层、失败标志。

用法：
    python run_metrics.py <批次目录> [更多批次...]
"""
import glob
import io
import os
import re
import sys

NODE_RE = re.compile(r'"node_type":"([a-z_0-9]+)"')
RECRUIT_RE = re.compile(r'SubTaskStart \{"class":"asst::RoguelikeRecruitTaskPlugin"')
BATTLE_RE = re.compile(r'SubTaskStart \{"class":"asst::RoguelikeBattleTaskPlugin"')
FLOOR_MARK = {"玻利瓦尔": 1, "甜美的伤口": 2, "血色空脉": 3}


def one(log_path):
    s = io.open(log_path, encoding="utf-8", errors="replace").read()
    nodes = {}
    for m in NODE_RE.finditer(s):
        nodes[m.group(1)] = nodes.get(m.group(1), 0) + 1
    deep = 0
    for name, lv in FLOOR_MARK.items():
        if name in s:
            deep = max(deep, lv)
    return (
        nodes,
        len(RECRUIT_RE.findall(s)),
        len(BATTLE_RE.findall(s)),
        deep,
        s.count("MissionFailedFlag2.png [opencv] score"),
    )


def main():
    for batch in sys.argv[1:]:
        print("==", os.path.basename(batch.rstrip("\\/")))
        for log in sorted(glob.glob(os.path.join(batch, "run_*", "debug", "asst.log"))):
            run = os.path.basename(os.path.dirname(os.path.dirname(log)))
            nodes, recruits, battles, deep, fail = one(log)
            top = ", ".join(f"{k}={v}" for k, v in sorted(nodes.items(), key=lambda x: -x[1])[:5])
            print(f"  {run}: 深{deep} 招募{recruits} 战斗{battles} 失败标志{fail} | {top}")


if __name__ == "__main__":
    main()
