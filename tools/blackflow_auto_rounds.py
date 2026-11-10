"""黑流树海多局连跑：把 blackflow_live_run.py 串起来，用于稳定性回归。

用法：
    python blackflow_auto_rounds.py --rounds 3 --seconds 2400 \
        --squad 开拓者分队 --roles 先手必胜

每一轮结束后等待若干秒让 MAA 收尾，再由下一轮自己的 `Begin` 任务
（含 BeginRecovery_* 恢复链）从上一局的结算页/弹窗里脱身。

注意：
- 每轮复用同一个模拟器会话，不重启游戏；
- 若某轮在战斗中被打断（进程被杀），下一轮会卡在战斗页，需要人工处理；
- 每轮开跑前会做一次画面预检（`blackflow_state_check.py`），不在黑流界面就立刻中止，
  免得 `Begin` 空转几十秒还把游戏带到基建/活动页；
- 本脚本只负责串行调度，日志仍落在 --out 前缀 + 轮次序号。
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]  # tools/ 的上一级
RUNNER = REPO_ROOT / "tools" / "blackflow_live_run.py"
LOG_ROOT = Path(r"D:\dsh\明日方舟\日志")

sys.path.insert(0, str(Path(__file__).resolve().parent))
from blackflow_state_check import check as check_state  # noqa: E402



def main() -> int:
    ap = argparse.ArgumentParser(description="黑流树海多局连跑")
    ap.add_argument("--rounds", type=int, default=3, help="连跑轮数")
    ap.add_argument("--seconds", type=int, default=2400, help="每轮时长上限")
    ap.add_argument("--squad", default="开拓者分队")
    ap.add_argument("--roles", default="先手必胜")
    ap.add_argument("--gap", type=int, default=20, help="轮间等待秒数")
    ap.add_argument("--tag", default=None, help="输出目录 tag，默认按日期时间生成")
    ap.add_argument(
        "--quick-fail-minutes",
        type=float,
        default=4.0,
        help="用时短于该阈值的轮次视为「开局就崩」（多半是游戏不在黑流主界面），连续两次即中止连跑",
    )
    ap.add_argument("--max-quick-fails", type=int, default=2)
    ap.add_argument(
        "--skip-precheck",
        action="store_true",
        help="跳过开轮前的画面预检（只在你确定游戏已经在局内时才用）",
    )
    args = ap.parse_args()

    if not RUNNER.is_file():
        print(f"找不到 {RUNNER}", file=sys.stderr)
        return 2

    tag = args.tag or f"auto_{datetime.now():%Y%m%d_%H%M%S}"
    print(f"连跑 {args.rounds} 轮，tag={tag}，每轮上限 {args.seconds}s")

    quick_fails = 0
    for i in range(1, args.rounds + 1):
        out = LOG_ROOT / f"blackflow_round_{tag}_r{i:02d}"
        cmd = [
            sys.executable,
            str(RUNNER),
            "--seconds",
            str(args.seconds),
            "--out",
            str(out),
            "--squad",
            args.squad,
            "--roles",
            args.roles,
        ]
        print(f"\n=== 第 {i}/{args.rounds} 轮：{out.name} ===", flush=True)
        if not args.skip_precheck:
            state = check_state()
            best = state["best"]
            if not state["ok"]:
                print(
                    f"!!! 预检未通过：{best['template']} score={best['score']:.4f}，"
                    f"当前不在黑流树海主界面（阈值 {state['threshold']}）",
                    flush=True,
                )
                print("中止连跑；请先把游戏恢复到黑流树海主界面（终端→集成战略）。", flush=True)
                return 4
            print(f"预检通过：{best['template']} score={best['score']:.4f}", flush=True)
        started = time.time()
        code = subprocess.call(cmd, cwd=str(REPO_ROOT))
        elapsed = time.time() - started
        print(f"--- 第 {i} 轮结束，exit={code}，用时 {elapsed / 60:.1f} 分钟", flush=True)
        if elapsed / 60 < args.quick_fail_minutes:
            quick_fails += 1
            print(
                f"!!! 本轮用时不足 {args.quick_fail_minutes:g} 分钟，"
                f"连续快速失败 {quick_fails}/{args.max_quick_fails}",
                flush=True,
            )
            if quick_fails >= args.max_quick_fails:
                print("连续快速失败，中止连跑；请先把游戏恢复到黑流树海主界面后再跑。", flush=True)
                return 3
        else:
            quick_fails = 0
        if i < args.rounds:
            time.sleep(args.gap)

    print("\n全部轮次已跑完")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
