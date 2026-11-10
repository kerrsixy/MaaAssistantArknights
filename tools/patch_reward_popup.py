"""给 BlackFlow 补「已领取所有奖励」弹窗页，并挂到 NextLevel / ScrapTradeDecision 的候选首位。

用法：python tools/patch_reward_popup.py [--apply]
"""
import io
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CFG = ROOT / "resource" / "tasks" / "Roguelike" / "BlackFlow.json"
BACKUP = CFG.with_suffix(".json.bak-reward-popup")

PAGE = "BlackFlow@Roguelike@RewardAllClaimed"

DEF = {
    "Doc": "战斗/节点结算后的「已领取所有奖励」弹窗，挡在地图上会遮蔽 NextLevel 的后续候选，必须优先点掉。",
    "algorithm": "OcrDetect",
    "text": ["已领取所有奖励"],
    "roi": [500, 390, 420, 70],
    "action": "ClickRect",
    "specificRect": [600, 486, 84, 72],
    "postDelay": 500,
    "next": ["BlackFlow@Roguelike@NextLevel"],
}

HOOKS = ["BlackFlow@Roguelike@NextLevel", "BlackFlow@Roguelike@ScrapTradeDecision"]


def main() -> int:
    apply = "--apply" in sys.argv
    data = json.loads(CFG.read_text(encoding="utf-8"))
    if PAGE in data:
        print("page already exists")
    else:
        data[PAGE] = DEF
        print(f"add page {PAGE}")
    for hook in HOOKS:
        node = data.get(hook)
        if not node:
            print(f"missing hook {hook}")
            continue
        nxt = node.get("next")
        if not isinstance(nxt, list):
            print(f"{hook} has no list next: {type(nxt)}")
            continue
        if PAGE in nxt:
            print(f"{hook} already hooked")
            continue
        nxt.insert(0, PAGE)
        print(f"hook -> {hook}.next[0]")
    if apply:
        if not BACKUP.exists():
            shutil.copy2(CFG, BACKUP)
            print(f"backup -> {BACKUP.name}")
        CFG.write_text(json.dumps(data, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")
        print("applied, pages =", len(data))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
