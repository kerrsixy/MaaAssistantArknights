# -*- coding: utf-8 -*-
"""把第二个官服模拟器摆到 MAA 能起跑的黑流树海首页。

背景：MAA 的 Roguelike 任务不会从普通游戏首页点进集成战略，从首页启动会立刻
`RecognitionFailed`。实机验证过的路径是「首页 → 终端 → 集成战略 → 黑流树海卡片」。
脚本只连接第二个窗口（`127.0.0.1:16416`），并在每一步保存截图。

用法::

    python tools/ensure_start_page.py            # 需要时才导航到黑流树海
    python tools/ensure_start_page.py --force    # 不判断，按完整路径导航
    python tools/ensure_start_page.py --dry-run  # 只报告当前是什么页

退出码: 0 = 已经/成功摆好；1 = 识别不出当前页面（需要人工看一眼）。
"""

import argparse
import pathlib
import subprocess
import sys
import time

import cv2
import numpy as np

ADB = r"D:\app\tool\mumu模拟器\MuMuPlayer-12.0\nx_main\adb.exe"
SER = "127.0.0.1:16384"
EVAL = pathlib.Path(r"D:\dsh\eval")

TERMINAL_POINT = (531, 670)  # 普通首页底部「终端」
BLACKFLOW_CARD_POINT = (1100, 247)  # 终端页右侧的黑流树海卡片

# 参考图：游戏首页 / 终端页 / 黑流树海首页（各取一块稳定区域做模板）
REF_HOME = EVAL / "i1.png"
REF_TERMINAL = EVAL / "k2.png"
REF_BF = EVAL / "second_blackflow_from_terminal_20261104.png"
# 结算页（「查看数据回溯」那一屏）：MAA 退出时可能停在这里，点两下就能回首页
REF_SETTLE = EVAL / "s1.png"
SETTLE_BOX = (30, 0, 320, 60)
SETTLE_NEXT = (1220, 390)
SETTLE_OK = (640, 650)


def adb(*args, timeout=40):
    subprocess.run([ADB, "start-server"], capture_output=True, timeout=timeout)
    subprocess.run(
        [ADB, "connect", SER], capture_output=True, timeout=timeout
    )
    return subprocess.run(
        [ADB, "-s", SER, *args], capture_output=True, timeout=timeout
    )


def screencap(path):
    # exec-out 直接拿 PNG 字节，避免「写 /sdcard 再 pull」在 adb 抖动时静默
    # 失败、读回上一张旧图（会表现成三次数值一模一样）。
    r = subprocess.run(
        [ADB, "-s", SER, "exec-out", "screencap", "-p"],
        capture_output=True,
        timeout=60,
    )
    data = r.stdout or b""
    if len(data) < 1000 or not data.startswith(b"\x89PNG"):
        return None
    img = cv2.imdecode(np.frombuffer(data, np.uint8), cv2.IMREAD_COLOR)
    if img is not None:
        try:
            cv2.imencode(".png", img)[1].tofile(str(path))
        except Exception:
            pass
    return img


def score_against(img, ref_path, box):
    """在 img 的 box 区域里和参考图同区域做匹配，返回相似度 0~1。"""
    if not ref_path.exists():
        return -1.0
    ref = cv2.imdecode(np.fromfile(str(ref_path), np.uint8), cv2.IMREAD_COLOR)
    x, y, w, h = box
    a = img[y : y + h, x : x + w]
    b = ref[y : y + h, x : x + w]
    if a.shape != b.shape or a.size == 0:
        return -1.0
    a = cv2.cvtColor(a, cv2.COLOR_BGR2GRAY)
    b = cv2.cvtColor(b, cv2.COLOR_BGR2GRAY)
    res = cv2.matchTemplate(a, b, cv2.TM_CCOEFF_NORMED)
    return float(res.max())


# 首页右侧「终端」瓷砖所在区域；终端页同区域会换成活动横幅
HOME_BOX = (900, 140, 380, 120)
BF_BOX = (0, 80, 1280, 560)


def classify(img):
    s_home = score_against(img, REF_HOME, HOME_BOX)
    s_term = score_against(img, REF_TERMINAL, (560, 80, 380, 380))
    s_bf = score_against(img, REF_BF, BF_BOX)
    s_settle = score_against(img, REF_SETTLE, SETTLE_BOX)
    return s_home, s_term, s_bf, s_settle


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true", help="不判断，直接点一次终端")
    ap.add_argument("--dry-run", action="store_true", help="只报告，不操作")
    ap.add_argument("--snap", default=r"D:\dsh\eval\start_page.png")
    args = ap.parse_args()

    img = screencap(args.snap)
    if img is None:
        print("screencap failed")
        return 1
    s_home, s_term, s_bf, s_settle = classify(img)
    print(f"home={s_home:.3f} terminal={s_term:.3f} bf_home={s_bf:.3f} settle={s_settle:.3f}")

    # 结算页：点「>>」再点 ✓，然后重新识别
    if s_settle >= 0.85 and not args.force and not args.dry_run:
        print("state: settlement -> 点 >> 与 ✓")
        adb("shell", f"input tap {SETTLE_NEXT[0]} {SETTLE_NEXT[1]}")
        time.sleep(2.5)
        adb("shell", f"input tap {SETTLE_OK[0]} {SETTLE_OK[1]}")
        time.sleep(4.0)
        img = screencap(str(pathlib.Path(args.snap).with_name("start_page_after_settle.png")))
        if img is None:
            return 1
        s_home, s_term, s_bf, s_settle = classify(img)
        print(f"after settle: home={s_home:.3f} terminal={s_term:.3f} bf_home={s_bf:.3f}")

    if not args.force:
        if s_bf >= 0.6:
            print("state: blackflow-home (already ok)")
            return 0
        if s_home < 0.6 and s_term < 0.6:
            print("state: unknown; 请人工确认（可用 --force 按完整路径导航）")
            return 1
        print("state: game-home/terminal -> navigate to blackflow")

    if args.dry_run:
        print("dry-run: 不做操作")
        return 0

    # 普通首页时先打开终端；终端页直接点右侧黑流树海卡片。
    if s_home >= 0.6 or args.force:
        adb("shell", f"input tap {TERMINAL_POINT[0]} {TERMINAL_POINT[1]}")
        time.sleep(3.0)
    adb("shell", f"input tap {BLACKFLOW_CARD_POINT[0]} {BLACKFLOW_CARD_POINT[1]}")
    time.sleep(4.0)
    img2 = screencap(str(pathlib.Path(args.snap).with_name("start_page_after_blackflow.png")))
    if img2 is None:
        print("post screencap failed")
        return 1
    s_home2, s_term2, s_bf2, s_settle2 = classify(img2)
    print(
        f"after: home={s_home2:.3f} terminal={s_term2:.3f} "
        f"bf_home={s_bf2:.3f} settle={s_settle2:.3f}"
    )
    if s_bf2 >= 0.5:
        print("ok: 已到黑流树海首页")
        return 0
    print("warn: 点击后仍不像黑流树海首页，请人工确认")
    return 1


if __name__ == "__main__":
    sys.exit(main())

