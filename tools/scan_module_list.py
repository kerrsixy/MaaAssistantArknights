"""在「档案 › 模组陈列室 › 干员模组总览」里按名字定位干员。

这一页比干员列表好用：所有已解锁模组的干员集中在一屏，左上角角标就是模组等级。

用法::

    python tools/scan_module_list.py 号角 灵知 提丰 [--max-pages 30]

输出每个目标命中的页码与头像坐标（JSON）。
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
SHOT = r"D:\dsh\eval\module_list.png"
LINE = re.compile(r"text:\s*(\S+?),\s*rect:\s*\[\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\]")


def adb(*args: str) -> str:
    return subprocess.run([ADB, "-s", DEV, *args], capture_output=True, text=True).stdout


def grab() -> str:
    adb("shell", "screencap", "-p", "/sdcard/ml.png")
    adb("pull", "/sdcard/ml.png", SHOT)
    return SHOT


def ocr(image: str) -> list[tuple[str, int, int, int]]:
    cmd = [
        sys.executable,
        str(ROOT / "tools" / "maa_core_eval.py"),
        "--mode",
        "ocr",
        image,
        "--task",
        "OperBoxNameOCR",
        "--roi",
        "0,60,1280,660",
    ]
    out = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
    hits = []
    for m in LINE.finditer(out):
        name, x, y = m.group(1), int(m.group(2)), int(m.group(3))
        hits.append((name, x, y, int(m.group(4))))
    return hits


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    targets = [a for a in sys.argv[1:] if not a.startswith("--")]
    max_pages = 30
    if "--max-pages" in sys.argv:
        max_pages = int(sys.argv[sys.argv.index("--max-pages") + 1])

    want = set(targets)
    found: dict[str, dict] = {}
    for page in range(1, max_pages + 1):
        hits = ocr(grab())
        for name, x, y, _w in hits:
            if name in want and name not in found:
                # 头像在名字左侧，垂直方向大致对齐
                found[name] = {"page": page, "avatar_x": max(20, x - 55), "avatar_y": y + 35}
        if len(found) == len(want):
            break
        adb("shell", "input", "swipe", "640", "600", "640", "180", "900")
        time.sleep(0.9)
    print(json.dumps({"found": found, "missing": sorted(want - found.keys())}, ensure_ascii=False))
    return 0 if not (want - found.keys()) else 1


if __name__ == "__main__":
    sys.exit(main())

