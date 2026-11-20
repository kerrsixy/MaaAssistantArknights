# -*- coding: utf-8 -*-
"""实机驱动 MaaCore 跑黑流树海（BlackFlow），收集导航/移动/战斗闭环的实机证据。

与 tools/maa_core_eval.py 互补：那个只对离线图片做识别评估，这个连模拟器跑真任务链，
把 BlackFlow 的观测/决策/事务回调按 jsonl 落盘，方便事后核对行动力、代步、落点。

仅 Debug 构建的 MaaCore.dll 可用，且仅限 Windows。

用法::

    # 只连接并截一张当前画面，确认 adb / 资源 / 分辨率都对
    python tools/blackflow_live_run.py --probe

    # 连上后追加 Roguelike 任务跑黑流树海，最长 --seconds 秒
    python tools/blackflow_live_run.py --seconds 900

    # 指定输出目录（默认 D:\\dsh\\明日方舟\\日志\\blackflow_live_<时间戳>）
    python tools/blackflow_live_run.py --out D:\\dsh\\明日方舟\\日志\\bf_live_1
"""

import argparse
import ctypes
import datetime
import json
import os
import pathlib
import sys
import threading
import time

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
DLL_DIR = REPO_ROOT / "build" / "bin" / "Debug"
LOG_ROOT = pathlib.Path(r"D:\dsh\明日方舟\日志")

ADB_PATH = r"D:\app\tool\mumu模拟器\MuMuPlayer-12.0\nx_main\adb.exe"
ADB_ADDRESS = "127.0.0.1:16416"
ADB_CONFIG = "MuMuEmulator12"

MSG_CONNECTION_INFO = 2
MSG_ALL_TASKS_COMPLETED = 3
MSG_INTERNAL_ERROR = 0
MSG_TASK_CHAIN_ERROR = 10_000
MSG_TASK_CHAIN_COMPLETED = 10_002
MSG_SUBTASK_ERROR = 20_000
MSG_SUBTASK_EXTRA_INFO = 20_003

# 回调里值得落盘的消息：BlackFlow 遥测 + 一切报错 + 任务链起止
KEEP_MSGS = {
    MSG_CONNECTION_INFO,
    MSG_ALL_TASKS_COMPLETED,
    MSG_INTERNAL_ERROR,
    MSG_TASK_CHAIN_ERROR,
    MSG_TASK_CHAIN_COMPLETED,
    MSG_SUBTASK_ERROR,
    MSG_SUBTASK_EXTRA_INFO,
}


def default_out_dir():
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    return LOG_ROOT / f"blackflow_live_{stamp}"


class LiveDriver:
    """最小 ctypes 封装：连接模拟器、追加任务、收集回调。"""

    def __init__(self, out_dir, dll_dir=DLL_DIR):
        self.out_dir = pathlib.Path(out_dir)
        self.out_dir.mkdir(parents=True, exist_ok=True)
        self._records = []
        self._lock = threading.Lock()
        self._done = threading.Event()

        dll_dir = pathlib.Path(dll_dir)
        os.environ["PATH"] = str(dll_dir) + os.pathsep + os.environ.get("PATH", "")
        self._lib = ctypes.WinDLL(str(dll_dir / "MaaCore.dll"))
        self._declare_functions()

        def on_callback(msg, details, _arg):
            try:
                text = details.decode("utf-8") if details else ""
            except Exception:
                text = ""
            self._on_callback(int(msg), text)

        self._callback = ctypes.CFUNCTYPE(
            None, ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p
        )(on_callback)

        if not self._lib.AsstSetUserDir(str(self.out_dir).encode("utf-8")):
            raise RuntimeError("AsstSetUserDir failed")
        if not self._lib.AsstLoadResource(str(REPO_ROOT).encode("utf-8")):
            raise RuntimeError("AsstLoadResource failed")
        self._handle = self._lib.AsstCreateEx(self._callback, None)
        if not self._handle:
            raise RuntimeError("AsstCreateEx failed")

    def _declare_functions(self):
        lib = self._lib
        c = ctypes.c_char_p
        handle = ctypes.c_void_p

        lib.AsstSetUserDir.argtypes, lib.AsstSetUserDir.restype = [c], ctypes.c_bool
        lib.AsstLoadResource.argtypes, lib.AsstLoadResource.restype = [c], ctypes.c_bool
        # AsstHandle 是 64 位指针，restype 不声明会被截成 int32 导致后续调用野指针
        lib.AsstCreateEx.restype = ctypes.c_void_p
        lib.AsstDestroy.argtypes, lib.AsstDestroy.restype = [handle], None
        lib.AsstSetInstanceOption.argtypes = [handle, ctypes.c_int, c]
        lib.AsstSetInstanceOption.restype = ctypes.c_bool
        lib.AsstSetConnectionExtras.argtypes = [c, c]
        lib.AsstSetConnectionExtras.restype = ctypes.c_bool
        lib.AsstConnect.argtypes, lib.AsstConnect.restype = (
            [handle, c, c, c],
            ctypes.c_bool,
        )
        lib.AsstAppendTask.argtypes, lib.AsstAppendTask.restype = (
            [handle, c, c],
            ctypes.c_int,
        )
        lib.AsstStart.argtypes, lib.AsstStart.restype = [handle], ctypes.c_bool
        lib.AsstStop.argtypes, lib.AsstStop.restype = [handle], ctypes.c_bool
        lib.AsstRunning.argtypes, lib.AsstRunning.restype = [handle], ctypes.c_bool
        lib.AsstConnected.argtypes, lib.AsstConnected.restype = [handle], ctypes.c_bool
        lib.AsstGetImage.argtypes = [handle, ctypes.c_void_p, ctypes.c_size_t]
        lib.AsstGetImage.restype = ctypes.c_size_t

    def _on_callback(self, msg, text):
        record = {"t": time.time(), "msg": msg, "details": text}
        with self._lock:
            self._records.append(record)
        if msg == MSG_ALL_TASKS_COMPLETED:
            self._done.set()
        if msg not in KEEP_MSGS:
            return
        try:
            parsed = json.loads(text) if text else {}
        except Exception:
            parsed = {"_raw": text}
        what = parsed.get("what", "")
        subtask = parsed.get("subtask", "")
        cls = parsed.get("class", "")
        interesting = (
            msg != MSG_SUBTASK_EXTRA_INFO
            or what.startswith("BlackFlow")
            or "lackflow" in cls
            or "lackflow" in subtask
        )
        if not interesting:
            return
        line = f"[{msg}] {what or subtask or cls}"
        if msg == MSG_SUBTASK_EXTRA_INFO and what.startswith("BlackFlow"):
            line += " " + json.dumps(parsed.get("details", {}), ensure_ascii=False)
        elif msg in (MSG_SUBTASK_ERROR, MSG_TASK_CHAIN_ERROR, MSG_INTERNAL_ERROR):
            line += " " + json.dumps(parsed.get("details", {}), ensure_ascii=False)
        print(line, flush=True)

    # ---------------- 对外操作 ----------------

    def connect(self, client_type, address=ADB_ADDRESS, mumu_extras=False):
        if mumu_extras:
            extras = {
                "path": str(pathlib.Path(ADB_PATH).parent.parent),
                "touch": True,
                "client_type": client_type,
            }
            # Omit index: the controller derives it from the actual address.
            # Never carry another emulator's index into a new connection.
            if not self._lib.AsstSetConnectionExtras(
                ADB_CONFIG.encode("utf-8"), json.dumps(extras).encode("utf-8")
            ):
                raise RuntimeError("AsstSetConnectionExtras failed")
            if not self._lib.AsstSetInstanceOption(self._handle, 2, b"MumuExtras"):
                raise RuntimeError("MuMu extras touch mode unavailable")
        if not self._lib.AsstSetInstanceOption(
            self._handle, 6, client_type.encode("utf-8")
        ):
            raise RuntimeError(
                f"AsstSetInstanceOption(ClientType) failed: {client_type}"
            )
        if not self._lib.AsstConnect(
            self._handle,
            ADB_PATH.encode("utf-8"),
            address.encode("utf-8"),
            ADB_CONFIG.encode("utf-8"),
        ):
            raise RuntimeError("AsstConnect failed")
        return True

    def append_task(self, task_type, params):
        task_id = self._lib.AsstAppendTask(
            self._handle,
            task_type.encode("utf-8"),
            json.dumps(params, ensure_ascii=False).encode("utf-8"),
        )
        if task_id <= 0:
            raise RuntimeError(f"AsstAppendTask failed: {task_type} {params}")
        return task_id

    def start(self):
        return bool(self._lib.AsstStart(self._handle))

    def stop(self):
        return bool(self._lib.AsstStop(self._handle))

    def running(self):
        return bool(self._lib.AsstRunning(self._handle))

    def snapshot(self, name="snapshot.png"):
        buf = ctypes.create_string_buffer(16 * 1024 * 1024)
        size = self._lib.AsstGetImage(self._handle, buf, len(buf))
        if size == 0:
            return None
        path = self.out_dir / name
        path.write_bytes(buf.raw[:size])
        return path

    def wait(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if self._done.is_set() or not self.running():
                return True
            # Cooperative diagnostic stops retain the normal final screenshot,
            # callback save and handle cleanup instead of killing Python.
            if (self.out_dir / "stop.request").is_file():
                print("Stop requested; stopping native task in place", flush=True)
                self.stop()
                return False
            if self._done.wait(timeout=2.0):
                return True
        return False

    def save(self):
        with self._lock:
            records = list(self._records)
        path = self.out_dir / "callbacks.jsonl"
        with path.open("w", encoding="utf-8") as f:
            for record in records:
                f.write(json.dumps(record, ensure_ascii=False) + "\n")
        return path, len(records)

    def close(self):
        try:
            if self.running():
                self.stop()
                time.sleep(2)
        except Exception:
            pass
        try:
            self._lib.AsstDestroy(self._handle)
        except Exception:
            pass
        self._handle = None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out", default=None, help="输出目录，默认在日志根目录下按时间戳新建"
    )
    parser.add_argument("--probe", action="store_true", help="只连接并截图，不跑任务")
    parser.add_argument(
        "--seconds", type=int, default=600, help="跑任务的最长秒数，默认 600"
    )
    parser.add_argument(
        "--mode", type=int, default=0, help="Roguelike 模式，0=刷经验，默认 0"
    )
    parser.add_argument("--starts", type=int, default=1, help="开局次数上限，默认 1")
    parser.add_argument(
        "--strategy",
        default="ending1",
        choices=["ending1", "ending2", "ending3"],
        help="黑流树海路线，默认 ending1",
    )
    parser.add_argument(
        "--difficulty",
        type=int,
        default=2147483647,
        help="难度，INT_MAX 表示沿用游戏当前难度，默认 2147483647",
    )
    parser.add_argument(
        "--investment",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="购物后投资余钱，默认开启；--no-investment 可关闭",
    )
    parser.add_argument(
        "--client", choices=("Bilibili", "Official"), default="Official"
    )
    parser.add_argument("--address", default=ADB_ADDRESS)
    parser.add_argument("--core-dir", default=str(DLL_DIR))
    parser.add_argument("--mumu-extras", action="store_true")
    parser.add_argument(
        "--diagnostics", choices=("normal", "full", "detailed"), default="full"
    )
    parser.add_argument(
        "--squad", default="远程战术分队", help="远程分队；黑流资源会按账号可用分队回退"
    )
    parser.add_argument(
        "--roles", default="稳扎稳打", help="开局干员职能，默认沿用 GUI 配置"
    )
    parser.add_argument(
        "--core-char", default="维什戴尔", help="开局招募干员，空串表示不指定"
    )
    args = parser.parse_args()

    out_dir = pathlib.Path(args.out) if args.out else default_out_dir()
    print(f"out dir: {out_dir}", flush=True)
    driver = LiveDriver(out_dir, args.core_dir)
    try:
        if args.mumu_extras:
            driver.connect(args.client, args.address, mumu_extras=True)
        else:
            driver.connect(args.client, args.address)
        print("connected", flush=True)
        if args.probe:
            path = driver.snapshot()
            print(f"snapshot: {path}", flush=True)
            return 0

        # A tiny map control can match an ordinary battle. Require the large,
        # theme-specific lobby anchor before handing control to Begin.
        from blackflow_state_check import check as check_state
        import cv2
        import numpy as np

        initial = driver.snapshot("initial_screen.png")
        screen = (
            cv2.imdecode(np.fromfile(initial, dtype=np.uint8), cv2.IMREAD_COLOR)
            if initial
            else None
        )
        if screen is None:
            raise RuntimeError("Initial screenshot unavailable; no task was started")
        preflight = check_state(screen)
        preflight.update(address=args.address, client=args.client)
        (out_dir / "preflight.json").write_text(
            json.dumps(preflight, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        if not preflight["ok"]:
            print(
                "Preflight rejected: open the BlackFlow lobby before starting; no task was started",
                flush=True,
            )
            return 4

        params = {
            "theme": "BlackFlow",
            "mode": args.mode,
            "difficulty": args.difficulty,
            "starts_count": args.starts,
            "investment_enabled": args.investment,
            "blackflow_strategy": args.strategy,
            "blackflow_diagnostics": args.diagnostics,
            "stop_at_final_boss": False,
            "stop_at_max_level": False,
            "use_nonfriend_support": False,
            "refresh_trader_with_dice": False,
        }
        if args.squad:
            params["squad"] = args.squad
        if args.roles:
            params["roles"] = args.roles
        if args.core_char:
            params["core_char_list"] = [{"name": args.core_char, "use_support": False}]
        print("params: " + json.dumps(params, ensure_ascii=False), flush=True)
        driver.append_task("Roguelike", params)
        if not driver.start():
            print("AsstStart failed", flush=True)
            return 2
        print("task started", flush=True)
        finished = driver.wait(args.seconds)
        print(f"finished={finished} running={driver.running()}", flush=True)
        path = driver.snapshot("final_screen.png")
        print(f"final snapshot: {path}", flush=True)
        return 0
    finally:
        driver.close()
        saved, count = driver.save()
        print(f"callbacks: {count} -> {saved}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
