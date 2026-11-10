# -*- coding: utf-8 -*-
"""汇总一个看护批次的实机日志：每局打到哪层、打了哪些关、无主地落点、失败标记。

用法::

    python tools/summarize_batch.py D:\\dsh\\eval\\bf_wd_20261104_022148
"""

import pathlib
import re
import sys

PAT_NEXT = re.compile(r"NextLevel\s+(\S+)")
PAT_NEXT_TEXT = re.compile(r"NextLevel\s+\[\{\s*text:\s*([^,]+),")
PAT_EVID = re.compile(
    r"Deployment evidence\s+(\S+)\s+(\S+)\s+\((\d+),\s*(\d+)\)\s+kind\s+(\d+)\s+"
    r"card_present\s+(-?\d+)\s+identity_matches\s+(-?\d+)\s+outcome\s+(\d+)"
)
PAT_FAIL = re.compile(r"MissionFailedFlag(?:2)?\.png.*score:\s*([0-9.]+)")
PAT_END = re.compile(r"RoguelikeCombatEnd")


def summarize(run_dir: pathlib.Path):
    log = run_dir / "debug" / "asst.log"
    if not log.exists():
        return None
    floors, stages, evidence, fails, ends = [], [], [], [], 0
    for line in log.open(encoding="utf-8", errors="replace"):
        m = PAT_NEXT_TEXT.search(line)
        if m:
            name = m.group(1).strip()
            if not floors or floors[-1] != name:
                floors.append(name)
            continue
        m = PAT_NEXT.search(line)
        if m:
            name = m.group(1)
            if name.startswith(("[", "{", "OCR", "->")):
                continue
            if not floors or floors[-1] != name:
                floors.append(name)
            continue
        m = PAT_EVID.search(line)
        if m:
            evidence.append((m.group(1), m.group(2), int(m.group(3)), int(m.group(4)), m.group(8)))
            continue
        m = PAT_FAIL.search(line)
        if m:
            try:
                if float(m.group(1)) > 0.9:
                    fails.append(float(m.group(1)))
            except ValueError:
                pass
            continue
        if PAT_END.search(line):
            ends += 1
    return {
        "floors": floors,
        "evidence": evidence,
        "fails": fails,
        "ends": ends,
    }


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    root = pathlib.Path(sys.argv[1])
    for run in sorted(root.glob("run_*")):
        info = summarize(run)
        if info is None:
            print(f"{run.name}: no log")
            continue
        print(f"== {run.name}  换层 {info['floors']}  战斗结束 {info['ends']} 次  真失败标记 {len(info['fails'])} 次")
        if info["evidence"]:
            seen = {}
            for stage, oper, x, y, outcome in info["evidence"]:
                seen.setdefault(stage, []).append(f"{oper}({x},{y}):{outcome}")
            for stage, items in seen.items():
                print(f"   {stage}: " + " ".join(items[:10]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
