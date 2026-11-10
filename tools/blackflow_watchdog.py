# -*- coding: utf-8 -*-
"""黑流树海实机跑局的看护进程。

盯三件事，出问题就把它拉回来：

1. 前台进程不是方舟（掉桌面 / 被回收）；
2. 画面黑屏或近乎全黑（模拟器渲染挂掉）；
3. MAA 进程已经退出但游戏还停在半路（交给人工/下一轮）。

处理动作：拉起方舟 → 等进游戏 → 用 `ensure_start_page.py` 摆回可起跑页面 →
重新起 `blackflow_live_run.py`。

用法::

    python tools/blackflow_watchdog.py --seconds 900 --max-restarts 3
    python tools/blackflow_watchdog.py --dry-run     # 只打印判断，不动手
"""

import argparse
import pathlib
import subprocess
import sys
import time

import cv2
import numpy as np

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
# MAA 进程的工作目录必须是纯 ASCII：中文 cwd 会让 MaaCore 在写调试图像路径时
# 抛 "多字节代码页无法映射 Unicode"（UnhandledException），今晚两次撞上。
ASCII_CWD = pathlib.Path(r"D:\dsh\eval\_maa_cwd")
ASCII_CWD.mkdir(parents=True, exist_ok=True)
PKG = "com.hypergryph.arknights"
ADB = r"D:\app\tool\mumu模拟器\MuMuPlayer-12.0\nx_main\adb.exe"
SER = "127.0.0.1:16416"
PY = sys.executable


def adb(*args, timeout=40):
    subprocess.run([ADB, "start-server"], capture_output=True, timeout=timeout)
    subprocess.run([ADB, "connect", SER], capture_output=True, timeout=timeout)
    return subprocess.run(
        [ADB, "-s", SER, *args], capture_output=True, timeout=timeout
    )


def focus_line():
    out = adb("shell", "dumpsys window | grep -E 'mCurrentFocus'").stdout or b""
    return out.decode("utf-8", "replace").strip()


def game_focused(line):
    return PKG in line


def grab(snap):
    # 用 exec-out 直接拿 PNG 字节：先写 /sdcard 再 pull 的做法在 adb 抖动时会
    # 静默失败、留下上一次的旧图（多次识别到同一帧的元凶）。
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
            cv2.imencode(".png", img)[1].tofile(str(snap))
        except Exception:
            pass
    return img


def is_black(img):
    if img is None:
        return True
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    return float(gray.mean()) < 8.0 and float(gray.std()) < 6.0


def relaunch(wait_s=70):
    adb(
        "shell",
        f"monkey -p {PKG} -c android.intent.category.LAUNCHER 1",
    )
    deadline = time.monotonic() + wait_s
    while time.monotonic() < deadline:
        if game_focused(focus_line()):
            return True
        time.sleep(3)
    return False


def ensure_page():
    script = REPO_ROOT / "tools" / "ensure_start_page.py"
    r = subprocess.run(
        [PY, str(script)], capture_output=True, text=True, encoding="utf-8", errors="replace",
        cwd=str(ASCII_CWD),
    )
    print((r.stdout or "").strip(), flush=True)
    return r.returncode == 0


def start_run(out_dir, seconds):
    script = REPO_ROOT / "tools" / "blackflow_live_run.py"
    return subprocess.Popen(
        [PY, str(script), "--seconds", str(seconds), "--out", str(out_dir)],
        cwd=str(ASCII_CWD),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )



def tap(x, y):
    adb("shell", "input tap {} {}".format(x, y))


def nudge_and_ensure(tries=3):
    """起跑页不认识时，先轻点几个推进点，再重新判定。"""
    for _ in range(tries):
        for x, y in ((640, 650), (640, 400), (48, 42)):
            tap(x, y)
            time.sleep(1.2)
        time.sleep(1.0)
        if ensure_page():
            return True
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=int, default=900, help="每局跑多久")
    ap.add_argument("--max-restarts", type=int, default=3)
    ap.add_argument("--check-every", type=float, default=12.0, help="看护轮询间隔秒")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    stamp = time.strftime("%Y%m%d_%H%M%S")
    root = pathlib.Path(r"D:\dsh\eval") / f"bf_wd_{stamp}"
    root.mkdir(parents=True, exist_ok=True)
    snap = root / "watch.png"

    restarts = 0
    proc = None
    try:
        while restarts <= args.max_restarts:
            if not ensure_page():
                print("start page not ready; 尝试轻点推进", flush=True)
                if args.dry_run:
                    return 1
                if not nudge_and_ensure():
                    print("still not ready; 停下等人工确认", flush=True)
                    break
            out_dir = root / f"run_{restarts + 1:02d}"
            out_dir.mkdir(parents=True, exist_ok=True)
            if args.dry_run:
                line = focus_line()
                img = grab(snap)
                print(
                    f"dry-run: focus={line[-60:]!r} black={is_black(img)}",
                    flush=True,
                )
                return 0
            print(f"start run -> {out_dir}", flush=True)
            proc = start_run(out_dir, args.seconds)

            deadline = time.monotonic() + args.seconds + 60
            bad = 0
            while time.monotonic() < deadline:
                time.sleep(args.check_every)
                line = focus_line()
                img = grab(snap)
                black = is_black(img)
                alive = proc is not None and proc.poll() is None
                print(
                    f"[{time.strftime('%H:%M:%S')}] focus={line[-60:]!r} black={black} maa_alive={alive}",
                    flush=True,
                )
                if not game_focused(line) or black:
                    bad += 1
                else:
                    bad = 0
                if bad >= 2:
                    print("=> 判定掉线/黑屏，准备重启", flush=True)
                    break
                if proc is not None and proc.poll() is not None:
                    print("=> MAA 已退出", flush=True)
                    break

            if proc is not None and proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    proc.kill()

            line = focus_line()
            img = grab(snap)
            need_restart = (not game_focused(line)) or is_black(img)
            if not need_restart:
                print("本轮结束，画面正常，准备下一轮", flush=True)
                restarts += 1
                continue

            restarts += 1
            if restarts > args.max_restarts:
                print("重启次数到上限，停止", flush=True)
                break
            print(f"=> 重启方舟（第 {restarts} 次）", flush=True)
            if args.dry_run:
                return 1
            if not relaunch():
                print("拉起失败，退出", flush=True)
                return 1
            time.sleep(20)
            ensure_page()
        return 0
    finally:
        print(f"watchdog 结束，日志目录 {root}", flush=True)


if __name__ == "__main__":
    sys.exit(main())


