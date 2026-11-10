"""运行的「练度快照」采集器。

用途：跑一次 OperStrength，把账号信息与全部已拥有干员的
（名字 / 精英化 / 等级 / 潜能 / 稀有度）落盘成账号快照 JSON，
供开局抓位闸门（练度不足顺延）与后续模组判断使用。

用法：
    python tools\\ops_snapshot.py            # 采集并写盘
    python tools\\ops_snapshot.py --dry      # 只打印，不写盘

依赖：MaaCore.dll（Debug 构建）、MuMu12 开着且停在主界面。
触控必须走 MuMuExtras 的 IPC 通道，否则干员列表滑不动。
"""
import argparse
import ctypes
import datetime
import io
import json
import os
import subprocess
import threading
import time

DLL_DIR = r"D:\dsh\明日方舟\代码\MaaAssistantArknights\build\bin\Debug"
RES_DIR = r"D:\dsh\明日方舟\代码\MaaAssistantArknights"
USER_DIR = r"D:\dsh\明日方舟\日志\ops_maa"
OUT_DIR = r"D:\dsh\明日方舟\数据\accounts"
ADB = r"D:\app\tool\mumu模拟器\MuMuPlayer-12.0\nx_main\adb.exe"
ADDR = "127.0.0.1:16416"
MUMU_PATH = r"D:/app/tool/mumu模拟器/MuMuPlayer-12.0"
CONN_CFG = "MuMuEmulator12"
PKG = "com.hypergryph.arknights"

account = {}
own = {}
done = threading.Event()
error = []


page_log = []


def _callback(msg, details, _):
    if details:
        try:
            payload = json.loads(details.decode("utf-8"))
        except Exception:
            payload = None
        if payload:
            what = payload.get("what")
            if what == "OperStrengthAccount":
                account.update(payload.get("details") or {})
                print("[account] uid=%r nick=%r" % (account.get("uid"), account.get("nick")), flush=True)
            elif what == "OperBoxInfo":
                det = payload.get("details") or {}
                page_names = []
                for oper in det.get("own_opers") or []:
                    if oper.get("own") is False:
                        continue
                    name = oper.get("name")
                    if name:
                        own[name] = oper
                        page_names.append(name)
                # 逐页快照：用来核对「某个干员到底有没有被扫到」
                if page_names:
                    page_log.append({"count": len(page_names), "total": len(own), "names": page_names})
                if det.get("done"):
                    done.set()
    if msg == 3:
        done.set()
    elif msg == 1:
        error.append("SubTaskError")


def prepare_home():
    """把游戏恢复到主界面。

    重置到主界面最稳的办法是重启进程：列表页的返回箭头与首页的齿轮
    在 (40,40) 重合，盲点会打开设置而不是退出，所以这里走 force-stop。
    """
    subprocess.run([ADB, "-s", ADDR, "shell", "am force-stop " + PKG],
                   capture_output=True, text=True, timeout=30)
    time.sleep(2)
    subprocess.run([ADB, "-s", ADDR, "shell",
                    "monkey -p %s -c android.intent.category.LAUNCHER 1" % PKG],
                   capture_output=True, text=True, timeout=30)
    # The official client first shows "开始唤醒" at (640,513), then exposes
    # START near the bottom after loading.  Tapping the old (640,660) point
    # twice leaves the process on the login page and produces an empty scan.
    time.sleep(30)
    subprocess.run([ADB, "-s", ADDR, "shell", "input tap 640 513"],
                   capture_output=True, text=True, timeout=20)
    time.sleep(45)
    subprocess.run([ADB, "-s", ADDR, "shell", "input tap 640 680"],
                   capture_output=True, text=True, timeout=20)
    time.sleep(60)


def game_version():
    try:
        out = subprocess.run(
            [ADB, "-s", ADDR, "shell", "dumpsys package %s | grep versionName" % PKG],
            capture_output=True, text=True, timeout=20,
        ).stdout
        for line in out.splitlines():
            if "versionName" in line:
                return line.split("=", 1)[1].strip()
    except Exception:
        pass
    return ""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry", action="store_true", help="只打印结果，不写盘")
    parser.add_argument("--keep-state", action="store_true", help="不重启游戏（假定已经在主界面）")
    parser.add_argument("--pages-out", default="", help="把逐页扫描结果写成 JSON，用于核对漏读")
    parser.add_argument(
        "--uid",
        default="",
        help="可选：用已核验的账号 UID 覆盖首页艺术字 OCR 结果，并写入快照",
    )
    args = parser.parse_args()

    os.makedirs(USER_DIR, exist_ok=True)
    os.environ["PATH"] = DLL_DIR + os.pathsep + os.environ.get("PATH", "")
    lib = ctypes.WinDLL(os.path.join(DLL_DIR, "MaaCore.dll"))
    global _CB
    cb_type = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p)
    _CB = cb_type(_callback)  # 必须持有引用，否则回调被 GC 后触发 access violation (0xC0000005)
    lib.AsstSetUserDir.argtypes, lib.AsstSetUserDir.restype = [ctypes.c_char_p], ctypes.c_bool
    lib.AsstLoadResource.argtypes, lib.AsstLoadResource.restype = [ctypes.c_char_p], ctypes.c_bool
    lib.AsstCreateEx.restype = ctypes.c_void_p
    lib.AsstSetInstanceOption.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p]
    lib.AsstSetInstanceOption.restype = ctypes.c_bool
    lib.AsstSetConnectionExtras.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    lib.AsstSetConnectionExtras.restype = ctypes.c_bool
    lib.AsstConnect.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
    lib.AsstConnect.restype = ctypes.c_bool
    lib.AsstAppendTask.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
    lib.AsstAppendTask.restype = ctypes.c_int
    lib.AsstStart.argtypes = [ctypes.c_void_p]
    lib.AsstStart.restype = ctypes.c_bool
    lib.AsstDestroy.argtypes = [ctypes.c_void_p]

    if not args.keep_state:
        prepare_home()

    assert lib.AsstSetUserDir(USER_DIR.encode()), "SetUserDir failed"
    assert lib.AsstLoadResource(RES_DIR.encode()), "LoadResource failed"
    handle = lib.AsstCreateEx(_CB, None)
    assert handle, "AsstCreateEx failed"
    try:
        # client_type 放进 extras：AdbController::init_mumu_extras 优先读它，
        # 用来把 [PackageName] 定位到 B 服包名，否则渲染器按官服包名找不到窗口。
        extras = json.dumps({"path": MUMU_PATH, "touch": True, "index": 0, "client_type": "Official"})
        lib.AsstSetConnectionExtras(CONN_CFG.encode(), extras.encode("utf-8"))
        lib.AsstSetInstanceOption(handle, 2, b"MumuExtras")
        # ClientType = 6：把连接配置里的 [PackageName] 换成 B 服包名，
        # 否则 MuMu 渲染器会按官服包名去找窗口，报 “target package is not rendering”
        # lib.AsstSetInstanceOption(handle, 6, b"Bilibili")  # 实测会让进程 Program terminating，先关掉；改用连接配置区分渠道
        assert lib.AsstConnect(handle, ADB.encode(), ADDR.encode(), CONN_CFG.encode()), "connect failed"
        assert lib.AsstAppendTask(handle, b"OperStrength", b"{}") > 0, "append failed"
        lib.AsstStart(handle)
        if not done.wait(600):
            print("[warn] timeout", flush=True)
    finally:
        lib.AsstDestroy(handle)

    uid = (account.get("uid") or "").strip()
    nickname = (account.get("nick") or "").strip()
    if args.uid:
        uid = args.uid.strip()
        print("[account] UID override=%r (caller-verified)" % uid, flush=True)
    dump_pages(args.pages_out)
    print("[result] uid=%r nick=%r owned=%d errors=%d" % (uid, nickname, len(own), len(error)), flush=True)
    if not own:
        return 1

    snapshot = {
        "schema": 1,
        "game": "国服官服",
        "package": PKG,
        "game_version": game_version(),
        "account": {"uid": uid, "nickname": nickname},
        "scanned_at": datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "scan_mode": "OperStrength/MuMuExtras",
        "oper_count": len(own),
        "modules": {"scanned": False, "watchlist": ["号角", "提丰", "水月", "灵知", "机械师"]},
        "operators": dict(sorted(own.items())),
    }
    if args.dry:
        print(json.dumps(snapshot["account"], ensure_ascii=False), flush=True)
        return 0
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, "bilibili_%s.json" % (uid or "unknown"))
    with open(path, "w", encoding="utf-8", newline="\n") as fp:
        json.dump(snapshot, fp, ensure_ascii=False, indent=2)
    print("[saved] %s" % path, flush=True)
    return 0


def dump_pages(path: str) -> None:
    if not path:
        return
    with io.open(path, "w", encoding="utf-8") as fp:
        json.dump(page_log, fp, ensure_ascii=False, indent=1)


if __name__ == "__main__":
    raise SystemExit(main())

