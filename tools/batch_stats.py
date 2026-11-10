# -*- coding: utf-8 -*-
"""多批次汇总：给「多局随机回归」出稳定性数字。

用法::

    python tools/batch_stats.py D:\\dsh\\eval\\bf_wd_20261104_013637 D:\\dsh\\eval\\bf_wd_20261104_022148
    python tools/batch_stats.py D:\\dsh\\eval --glob "bf_wd_*"
"""

import argparse
import pathlib
import re
import sys

PAT_NEXT = re.compile(r"NextLevel\s+\[\{\s*text:\s*([^,]+),")
PAT_EVID = re.compile(r"Deployment evidence\s+(\S+)\s+(\S+)\s+\((\d+),\s*(\d+)\).*outcome\s+(\d+)")
PAT_FAIL = re.compile(r"MissionFailedFlag(?:2)?\.png.*score:\s*([0-9.]+)")
PAT_END = re.compile(r"RoguelikeCombatEnd")


def scan(log: pathlib.Path):
    floors, battles = [], set()
    ends = fails = 0
    ok = bad = 0
    for line in log.open(encoding="utf-8", errors="replace"):
        m = PAT_NEXT.search(line)
        if m:
            name = m.group(1).strip()
            if not floors or floors[-1] != name:
                floors.append(name)
            continue
        m = PAT_EVID.search(line)
        if m:
            battles.add(m.group(1))
            if m.group(5) == "2":
                ok += 1
            elif m.group(5) == "0":
                bad += 1
            continue
        m = PAT_FAIL.search(line)
        if m:
            try:
                if float(m.group(1)) > 0.9:
                    fails += 1
            except ValueError:
                pass
            continue
        if PAT_END.search(line):
            ends += 1
    return floors, battles, ends, fails, ok, bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="*")
    ap.add_argument("--glob", default="bf_wd_*")
    args = ap.parse_args()

    roots = [pathlib.Path(d) for d in args.dirs]
    if not roots:
        base = pathlib.Path(r"D:\dsh\eval")
        roots = sorted(p for p in base.glob(args.glob) if p.is_dir())
    if not roots:
        print("no batch dirs")
        return 1

    total_runs = total_ends = total_fails = total_ok = total_bad = 0
    max_floor = 0
    stage_hits = {}
    for root in roots:
        for run in sorted(root.glob("run_*")):
            log = run / "debug" / "asst.log"
            if not log.exists():
                continue
            floors, battles, ends, fails, ok, bad = scan(log)
            total_runs += 1
            total_ends += ends
            total_fails += fails
            total_ok += ok
            total_bad += bad
            max_floor = max(max_floor, len(floors))
            for s in battles:
                stage_hits[s] = stage_hits.get(s, 0) + 1
            print(
                f"{root.name}/{run.name}: 层 {len(floors)} {floors} | 战斗结束 {ends} | 真失败 {fails} | "
                f"落点 成功{ok}/失败{bad}"
            )

    print("---")
    print(f"局数 {total_runs}；最深层数 {max_floor}；战斗结束 {total_ends}；真失败 {total_fails}；"
          f"落点成功 {total_ok} / 失败 {total_bad}")
    if stage_hits:
        top = sorted(stage_hits.items(), key=lambda kv: -kv[1])[:8]
        print("关卡出现次数：" + "，".join(f"{k}×{v}" for k, v in top))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
