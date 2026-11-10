"""模组角标（等级数字）识别：几何定位 + 内层数字模板比对。

总览页每个已解锁模组槽左上角有一个约 20×20 的黑方块，白色数字即模组等级。
整页 OCR 读不到这种 14px 的小字，所以：
  1. 用干员名 OCR 拿到每一列的名字起点，角标位置由「名字 x + 9、名字 y + 43、
     每个槽位再 +55」推算；
  2. 取黑方块中间 16×16 的内层数字区，放大到 24×24；
  3. 与 `D:\\dsh\\eval\\badge_tpl\\in1.png` / `in2.png` / `in3.png` 比对取最高分。

模板缺失时（比如 `in2.png` 还没有样本）该档会被标成 `unknown`，方便回头补样。
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import cv2
import numpy as np

TPL_DIR = Path(r"D:\dsh\eval\badge_tpl")
SLOT_DX = 55
NAME_DX = 9
NAME_DY = 43


def load_tpls() -> dict[str, np.ndarray]:
    tpls: dict[str, np.ndarray] = {}
    for digit in ("1", "2", "3"):
        p = TPL_DIR / f"in{digit}.png"
        if p.exists():
            img = cv2.imread(str(p), cv2.IMREAD_GRAYSCALE)
            if img is not None:
                tpls[digit] = img
    return tpls


def classify(gray: np.ndarray, cx: int, cy: int, tpls: dict[str, np.ndarray]) -> dict:
    patch = gray[cy - 8 : cy + 8, cx - 8 : cx + 8]
    if patch.shape != (16, 16):
        return {"digit": "unknown", "score": 0.0}
    patch = cv2.resize(patch, (24, 24), interpolation=cv2.INTER_CUBIC)
    best, best_score = "unknown", -1.0
    for digit, tpl in tpls.items():
        score = float(cv2.matchTemplate(patch, tpl, cv2.TM_CCOEFF_NORMED)[0, 0])
        if score > best_score:
            best, best_score = digit, score
    if best_score < 0.60:
        return {"digit": "unknown", "score": round(best_score, 4)}
    return {"digit": best, "score": round(best_score, 4)}


def read_operator(gray: np.ndarray, name_x: int, name_y: int, tpls: dict[str, np.ndarray], max_slots: int = 3) -> list[dict]:
    out = []
    for i in range(max_slots):
        cx = name_x + NAME_DX + SLOT_DX * i
        cy = name_y + NAME_DY
        res = classify(gray, cx, cy, tpls)
        if res["digit"] != "unknown":
            out.append({"slot": i, **res})
        elif res["score"] > 0.25:
            out.append({"slot": i, **res})
    return out


def main() -> int:
    image = sys.argv[1]
    gray = cv2.imread(image, cv2.IMREAD_GRAYSCALE)
    tpls = load_tpls()
    print(json.dumps({"templates": sorted(tpls), "image": image}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
