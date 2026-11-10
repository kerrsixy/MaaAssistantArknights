"""模组角标（1/2/3）识别：黑方块定位 + 模板比对。

总览页每个已解锁模组槽位的左上角有一个小黑方块，里面是白色等级数字。
数字只有约 14×16 px，MaaCore 的 OCR 读不稳；改用「黑方块定位 + 归一化模板比对」。

模板放在 `D:\\dsh\\eval\\badge_tpl\\<digit>.png`（20×20 灰度、由已知样本裁切归一化而来）。

用法::

    python tools/badge_detect.py <图片> [--debug 输出目录]
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import cv2
import numpy as np

TPL_DIR = Path(r"D:\dsh\eval\badge_tpl")
SIZE = 20


def normalize(patch: np.ndarray) -> np.ndarray:
    return cv2.resize(patch, (SIZE, SIZE), interpolation=cv2.INTER_AREA)


def load_templates() -> dict[str, np.ndarray]:
    tpls = {}
    for p in sorted(TPL_DIR.glob("*.png")):
        img = cv2.imread(str(p), cv2.IMREAD_GRAYSCALE)
        if img is None:
            continue
        tpls[p.stem] = normalize(img)
    return tpls


def find_badges(gray: np.ndarray) -> list[tuple[int, int, int, int]]:
    mask = (gray < 70).astype(np.uint8) * 255
    cnts, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    boxes = []
    for c in cnts:
        x, y, w, h = cv2.boundingRect(c)
        if not (17 <= w <= 26 and 17 <= h <= 26):
            continue
        roi = gray[y : y + h, x : x + w]
        white = float((roi > 185).mean())
        if not (0.10 <= white <= 0.45):
            continue
        boxes.append((x, y, w, h))
    return boxes


def classify(gray: np.ndarray, box: tuple[int, int, int, int], tpls: dict[str, np.ndarray]) -> tuple[str, float]:
    x, y, w, h = box
    patch = normalize(gray[y : y + h, x : x + w])
    best, best_score = "", -1.0
    for name, tpl in tpls.items():
        score = float(cv2.matchTemplate(patch, tpl, cv2.TM_CCOEFF_NORMED)[0, 0])
        if score > best_score:
            best, best_score = name, score
    return best, best_score


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    image = sys.argv[1]
    debug = None
    if "--debug" in sys.argv:
        debug = Path(sys.argv[sys.argv.index("--debug") + 1])
        debug.mkdir(parents=True, exist_ok=True)

    gray = cv2.imread(image, cv2.IMREAD_GRAYSCALE)
    if gray is None:
        print(json.dumps({"error": f"cannot read {image}"}, ensure_ascii=False))
        return 1
    tpls = load_templates()
    hits = []
    for box in find_badges(gray):
        name, score = classify(gray, box, tpls)
        hits.append({"x": box[0], "y": box[1], "w": box[2], "h": box[3], "digit": name, "score": round(score, 4)})
        if debug:
            x, y, w, h = box
            cv2.imwrite(str(debug / f"{name}_{x}_{y}.png"), gray[y : y + h, x : x + w])
    print(json.dumps({"count": len(hits), "hits": hits}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
