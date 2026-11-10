"""在当前干员列表截图里按名字定位卡片，输出可点击坐标。

用法::

    python tools/pick_card.py <screenshot.png> <干员名>

识别走 MaaCore 的 `OperBoxNameOCR`（与线上一致），先找第一行名字带（y≈350），
再找第二行（y≈660）；命中即返回该卡片中心坐标，避免“凭印象点卡片”点错人。

输出 JSON::

    {"found": true, "name": "号角", "x": 437, "y": 240, "score": 0.99}
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NAME_ROWS = ((345, 40, 240), (660, 40, 560))  # (y, h, 卡片中心 y)
LINE = re.compile(r"^\s+(0\.\d+)\s+(\S.*?)\s+\[(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\]\s*$")


def ocr_strip(image: str, y: int, h: int) -> list[tuple[str, float, int, int, int]]:
    cmd = [
        sys.executable,
        str(ROOT / "tools" / "maa_core_eval.py"),
        "--mode",
        "ocr",
        image,
        "--task",
        "OperBoxNameOCR",
        "--roi",
        f"0,{y},1280,{h}",
    ]
    out = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
    hits = []
    for line in out.splitlines():
        m = LINE.match(line)
        if not m:
            continue
        score = float(m.group(1))
        name = m.group(2).strip()
        x, _yy, w, _h = (int(m.group(i)) for i in (3, 4, 5, 6))
        hits.append((name, score, x + w // 2, w, y))
    return hits


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    image, target = sys.argv[1], sys.argv[2]
    best = None
    for y, h, center_y in NAME_ROWS:
        for name, score, cx, _w, _y in ocr_strip(image, y, h):
            if name == target and score >= 0.75:
                if best is None or score > best["score"]:
                    best = {"found": True, "name": name, "x": cx, "y": center_y, "score": score}
    if best is None:
        # 把所有读到的名字回显，方便人工核对是“没在屏幕上”还是“OCR 认错”
        seen = []
        for y, h, _cy in NAME_ROWS:
            seen += [n for n, s, _x, _w, _yy in ocr_strip(image, y, h) if s >= 0.75]
        print(json.dumps({"found": False, "name": target, "seen": seen}, ensure_ascii=False))
        return 1
    print(json.dumps(best, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
