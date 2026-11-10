"""在干员列表里翻页找目标干员，找到后输出坐标（可选直接点击）。

用法::

    python tools/find_card.py <干员名> [--tap] [--max-pages 40]

流程：截图 → 用 MaaCore 的 OperBoxNameOCR 读卡片名字 → 命中即算坐标；
没命中就慢速左滑一页（900ms，与线上翻页参数一致）继续找。
这样避免“凭印象点卡片”点错人。
"""

from __future__ import annotations

import json
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pick_card import ocr_strip, NAME_ROWS  # noqa: E402

ADB = r"D:\app\tool\mumu模拟器\MuMuPlayer-12.0\nx_main\adb.exe"
DEV = "127.0.0.1:16416"
SHOT = r"D:\dsh\eval\find_card.png"


def adb(*args: str) -> str:
    return subprocess.run([ADB, "-s", DEV, *args], capture_output=True, text=True).stdout


def grab() -> str:
    adb("shell", "screencap", "-p", "/sdcard/fc.png")
    adb("pull", "/sdcard/fc.png", SHOT)
    return SHOT


def find(target: str, image: str) -> dict:
    best = None
    for y, h, center_y in NAME_ROWS:
        for name, score, cx, _w, _y in ocr_strip(image, y, h):
            if name == target and score >= 0.75 and (best is None or score > best["score"]):
                best = {"found": True, "name": name, "x": cx, "y": center_y, "score": score}
    return best or {"found": False}


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    target = sys.argv[1]
    do_tap = "--tap" in sys.argv
    max_pages = 40
    if "--max-pages" in sys.argv:
        max_pages = int(sys.argv[sys.argv.index("--max-pages") + 1])

    for page in range(1, max_pages + 1):
        hit = find(target, grab())
        if hit["found"]:
            hit["page"] = page
            if do_tap:
                adb("shell", "input", "tap", str(hit["x"]), str(hit["y"]))
                hit["tapped"] = True
            print(json.dumps(hit, ensure_ascii=False))
            return 0
        adb("shell", "input", "swipe", "1100", "350", "200", "350", "900")
        time.sleep(0.9)
    print(json.dumps({"found": False, "pages": max_pages}, ensure_ascii=False))
    return 1


if __name__ == "__main__":
    sys.exit(main())

