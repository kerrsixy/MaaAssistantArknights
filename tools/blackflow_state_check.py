# -*- coding: utf-8 -*-
"""起轮前的游戏状态预检：确认当前画面是黑流树海（主界面/地图/局内），而不是
基建、活动页、普通关卡战斗。

背景：多局连跑时如果上一轮异常退出，游戏可能停在基建或活动页，MAA 的 `Begin`
既识别不出也退不回来，白白耗 90 秒并把状态搞得更乱。这里用 adb 截一张图，直接拿
仓库里的模板做一次匹配，命中才认为可以开轮。

用法::

    python tools/blackflow_state_check.py            # 检查并打印结论
    python tools/blackflow_state_check.py --json     # 机器可读

命中任一模板（阈值 0.65）即视为「在黑流界面」：
    BlackFlow@Roguelike@StartExplore.png   主界面「开始探索」
    BlackFlow@Roguelike@Continue.png       主界面「继续探索」

注意：不要拿 `MapZoomIn.png` / `MapZoomOut.png` 这类 14x13 的小图标当判据——它在
任何画面上都能刷到 0.78，会把首页误判成黑流界面（实测如此）。
"""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import tempfile

import cv2
import numpy as np

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
TEMPLATE_DIR = REPO_ROOT / "resource" / "template" / "Roguelike" / "BlackFlow"

ADB_PATH = r"D:\app\tool\mumu模拟器\MuMuPlayer-12.0\nx_main\adb.exe"
ADB_ADDRESS = "127.0.0.1:16416"

THRESHOLD = 0.65
# 只用够大、够独特的模板；14x13 的缩放按钮会在任何画面刷到 0.7+，实测误判。
TEMPLATES = (
    "BlackFlow@Roguelike@StartExplore.png",
    "BlackFlow@Roguelike@Continue.png",
)


def adb_screencap(address: str = ADB_ADDRESS) -> np.ndarray:
    with tempfile.TemporaryDirectory() as tmp:
        remote = "/sdcard/_bf_state_check.png"
        local = pathlib.Path(tmp) / "state.png"
        subprocess.run(
            [ADB_PATH, "-s", address, "shell", "screencap", "-p", remote],
            check=True,
            capture_output=True,
        )
        subprocess.run(
            [ADB_PATH, "-s", address, "pull", remote, str(local)],
            check=True,
            capture_output=True,
        )
        subprocess.run(
            [ADB_PATH, "-s", address, "shell", "rm", remote],
            check=False,
            capture_output=True,
        )
        image = cv2.imdecode(np.fromfile(str(local), dtype=np.uint8), cv2.IMREAD_COLOR)
        if image is None:
            raise RuntimeError("screencap 解码失败")
        return image


def check(screen: np.ndarray | None = None, *, address: str = ADB_ADDRESS) -> dict:
    image = adb_screencap(address) if screen is None else screen
    best = {"score": 0.0, "template": None, "rect": None}
    for name in TEMPLATES:
        path = TEMPLATE_DIR / name
        if not path.is_file():
            continue
        template = cv2.imdecode(
            np.fromfile(str(path), dtype=np.uint8), cv2.IMREAD_COLOR
        )
        if template is None:
            continue
        if template.shape[0] > image.shape[0] or template.shape[1] > image.shape[1]:
            continue
        result = cv2.matchTemplate(image, template, cv2.TM_CCOEFF_NORMED)
        _, max_val, _, max_loc = cv2.minMaxLoc(result)
        if max_val > best["score"]:
            best = {
                "score": float(max_val),
                "template": name,
                "rect": [
                    int(max_loc[0]),
                    int(max_loc[1]),
                    template.shape[1],
                    template.shape[0],
                ],
            }
    ok = best["score"] >= THRESHOLD
    return {
        "ok": ok,
        "threshold": THRESHOLD,
        "best": best,
        "address": address if screen is None else None,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="黑流树海起轮前状态预检")
    ap.add_argument("--json", action="store_true", help="输出 JSON")
    ap.add_argument("--image", default=None, help="改用本地图片而不是 adb 截图")
    ap.add_argument("--address", default=ADB_ADDRESS)
    args = ap.parse_args()

    screen = None
    if args.image:
        screen = cv2.imdecode(np.fromfile(args.image, dtype=np.uint8), cv2.IMREAD_COLOR)
        if screen is None:
            print(f"读不出图片：{args.image}", file=sys.stderr)
            return 2

    try:
        result = check(screen, address=args.address)
    except Exception as exc:  # noqa: BLE001 - 预检失败不应该让调用方崩
        print(f"预检失败：{exc}", file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(result, ensure_ascii=False))
    else:
        best = result["best"]
        state = "在黑流界面" if result["ok"] else "不在黑流界面"
        print(
            f"{state}：{best['template']} score={best['score']:.4f} "
            f"rect={best['rect']}（阈值 {result['threshold']}）"
        )
        if not result["ok"]:
            print("先把游戏恢复到黑流树海主界面再起轮，否则 MAA 的 Begin 会空转。")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
