"""读取指定干员的「模组」面板：逐槽位读出 STAGE 等级。

为什么不用总览页的角标：角标数字只有 12×14px，OCR 读不稳；而干员详情页点开
模组卡片后的面板里，每个槽位都写着 `STAGE n`，字号正常，OCR 稳定，而且是
**逐槽位**的，能分清 X / Y / α / β 哪个是特限。

用法::

    python tools/read_module_panels.py 号角 灵知 提丰 水月

前置：游戏停在干员列表或首页，脚本自己导航。
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ADB = r"D:\app\tool\mumu模拟器\MuMuPlayer-12.0\nx_main\adb.exe"
DEV = "127.0.0.1:16416"
SHOT = r"D:\dsh\eval\module_panel.png"
LINE = re.compile(r"text:\s*(\S+?),\s*rect:\s*\[\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\]\s*,\s*score:\s*([0-9.]+)")
OCR_ROI = None


def adb(*args: str) -> str:
    return subprocess.run([ADB, "-s", DEV, *args], capture_output=True, text=True).stdout


def tap(x: int, y: int, wait: float = 1.6) -> None:
    adb("shell", "input", "tap", str(x), str(y))
    time.sleep(wait)


def grab(path: str = SHOT) -> str:
    adb("shell", "screencap", "-p", "/sdcard/mpn.png")
    adb("pull", "/sdcard/mpn.png", path)
    return path


def ocr(image: str) -> list[dict]:
    cmd = [sys.executable, str(ROOT / "tools" / "maa_core_eval.py"), "--mode", "ocr", image]
    out = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
    items = []
    for m in LINE.finditer(out):
        items.append(
            {
                "text": m.group(1),
                "x": int(m.group(2)),
                "y": int(m.group(3)),
                "w": int(m.group(4)),
                "h": int(m.group(5)),
                "score": float(m.group(6)),
            }
        )
    return items


def parse_panel(items: list[dict]) -> list[dict]:
    """面板里每个槽位一行：槽位代号 + STAGE + 数字。"""
    stages = [i for i in items if i["text"].upper().startswith("STAGE")]
    slots = []
    for st in stages:
        num = None
        for it in items:
            if abs(it["y"] - st["y"]) <= 18 and it["x"] > st["x"] and re.fullmatch(r"[0-9]", it["text"]):
                num = int(it["text"])
                break
        slots.append({"stage": num, "y": st["y"]})
    slots.sort(key=lambda s: s["y"])
    return slots


def find_and_open(name: str) -> bool:
    cmd = [sys.executable, str(ROOT / "tools" / "find_card.py"), name, "--tap", "--max-pages", "25"]
    out = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
    hit = json.loads(out.strip().splitlines()[-1]) if out.strip() else {"found": False}
    if not hit.get("found"):
        return False
    time.sleep(2.0)
    return True


def main() -> int:
    names = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not names:
        print(__doc__)
        return 2
    result: dict[str, dict] = {}
    for name in names:
        if not find_and_open(name):
            result[name] = {"error": "not found in list"}
            continue
        tap(1090, 545, 2.0)  # 点右下角「模组」卡片
        items = ocr(grab())
        slots = parse_panel(items)
        result[name] = {"slots": slots, "texts": [i["text"] for i in items if i["score"] > 0.8][:40]}
        tap(40, 40, 1.2)  # 返回详情页
        tap(40, 40, 1.6)  # 返回列表
    print(json.dumps(result, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())

