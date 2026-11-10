"""干员强度 / 模组 / 账号识别（黑流树海前置，M1）

用法：
    python tools/ops_scan.py --account                # 识别账号 ID 并落盘
    python tools/ops_scan.py --strength               # 全量练度（OperBox，慢）
    python tools/ops_scan.py --modules                # 特限(+机械师) 模组，半自动
    python tools/ops_scan.py --all                    # 依次跑三步

模组判定（主人 2026-11-03 给的规则）：
    面板里出现「解锁」两个字  -> 未开
    出现 MAX                  -> 已开且满级（3 级）
    出现其它数字 1/2/3        -> 已开且为该等级

产物：
    数据/accounts/<server>_<uid>.json     账号快照（练度 + 模组 + 元信息）
    日志/ops_scan/<name>.png              模组面板截图（人工复核用）
"""

from __future__ import annotations

import argparse
import ctypes
import datetime
import json
import os
import pathlib
import re
import subprocess
import sys
import threading
import time

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
DLL_DIR = REPO_ROOT / "build" / "bin" / "Debug"
DATA_ROOT = pathlib.Path(r"D:\dsh\明日方舟\数据")
LOG_ROOT = pathlib.Path(r"D:\dsh\明日方舟\日志")
SCAN_LOG = LOG_ROOT / "ops_scan"
ACCOUNTS = DATA_ROOT / "accounts"

ADB = r"D:\app\tool\mumu模拟器\MuMuPlayer-12.0\nx_main\adb.exe"
ADB_ADDR = "127.0.0.1:16384"
PACKAGE_BILI = "com.hypergryph.arknights.bilibili"
PACKAGE_OFFICIAL = "com.hypergryph.arknights"

# 模组扫描范围：特限干员 + 机械师（规则显式依赖她的模组三级）
MODULE_TARGETS = ["号角", "提丰", "水月", "灵知", "机械师"]
SPECIAL_TARGETS = {"号角", "提丰", "水月", "灵知"}


# ---------------------------------------------------------------- adb helpers
def adb(*args: str, timeout: int = 60) -> str:
    cmd = [ADB, "-s", ADB_ADDR, *args]
    out = subprocess.run(cmd, capture_output=True, timeout=timeout)
    return out.stdout.decode("utf-8", "replace")


def tap(x: int, y: int, wait: float = 1.2) -> None:
    adb("shell", f"input tap {int(x)} {int(y)}")
    time.sleep(wait)


def screenshot(path: pathlib.Path) -> pathlib.Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    adb("shell", "screencap -p /sdcard/_ops_scan.png")
    adb("pull", "/sdcard/_ops_scan.png", str(path))
    adb("shell", "rm -f /sdcard/_ops_scan.png")
    return path


def server_of_device() -> str:
    pkgs = adb("shell", "pm list packages")
    if PACKAGE_OFFICIAL in pkgs:
        return "official"
    if PACKAGE_BILI in pkgs:
        return "bilibili"
    return "unknown"


# ---------------------------------------------------------------- OCR
def eval_module():
    sys.path.insert(0, str(REPO_ROOT / "tools"))
    from maa_core_eval import CoreEval  # noqa: WPS433

    return CoreEval(dll_dir=str(DLL_DIR))


def ocr_entries(ev, image: pathlib.Path, roi=None) -> list[dict]:
    out: list[dict] = []
    for item in ev.ocr(images=[str(image)], roi=roi):
        for entry in item.get("results", []):
            out.append(entry)
    return out


def ocr_texts(ev, image: pathlib.Path, roi=None) -> list[str]:
    return [e.get("text", "") for e in ocr_entries(ev, image, roi)]


def tap_text(ev, image: pathlib.Path, needle: str, wait: float = 1.6) -> bool:
    """在全屏 OCR 结果里找包含 needle 的文本，点它的中心。"""
    for entry in ocr_entries(ev, image):
        if needle in entry.get("text", ""):
            x, y, w, h = entry["rect"]
            tap(x + w // 2, y + h // 2, wait)
            return True
    return False


# ---------------------------------------------------------------- snapshot
def load_snapshot(path: pathlib.Path) -> dict:
    if path.exists():
        try:
            return json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            pass
    return {}


def save_snapshot(path: pathlib.Path, snap: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(snap, ensure_ascii=False, indent=2), encoding="utf-8")


def resolve_snapshot(uid_hint: str | None) -> tuple[dict, pathlib.Path]:
    if uid_hint:
        cand = list(ACCOUNTS.glob(f"*_{uid_hint}.json")) if ACCOUNTS.exists() else []
        if cand:
            return load_snapshot(cand[0]), cand[0]
    cand = sorted(ACCOUNTS.glob("*.json")) if ACCOUNTS.exists() else []
    if len(cand) == 1:
        return load_snapshot(cand[0]), cand[0]
    if not cand:
        raise SystemExit("还没有账号快照，先跑 --account")
    raise SystemExit("存在多个账号快照，请用 --uid 指定")


def game_version() -> str:
    text = adb("shell", f"dumpsys package {PACKAGE_BILI}")
    m = re.search(r"versionName=(\S+)", text)
    return m.group(1) if m else ""


def now_iso() -> str:
    return datetime.datetime.now().astimezone().isoformat(timespec="seconds")


# ---------------------------------------------------------------- account
def ensure_home(ev, rounds: int = 8, gap: float = 12.0) -> bool:
    """把游戏推到主界面：加载页有 START 就点，直到 OCR 能读到「终端」或「好友」。"""
    shot = SCAN_LOG / "account_wait.png"
    for i in range(rounds):
        screenshot(shot)
        texts = ocr_texts(ev, shot)
        joined = " ".join(texts)
        if "终端" in joined or "好友" in joined:
            print(f"[account] 已在主界面（第 {i + 1} 次探测）", flush=True)
            return True
        if "START" in joined.upper():
            print("[account] 加载页，点 START", flush=True)
            tap(640, 660, 3.0)
        else:
            print(f"[account] 仍在加载/未知页面（第 {i + 1} 次），等 {gap:.0f}s", flush=True)
        time.sleep(gap)
    return False


# 皮肤/字体变化时 OCR 可能读不出文字，留一组固定坐标作为兜底（1280x720）
HOME_FRIENDS_ICON = (300, 545)
FRIENDS_ADD_ENTRY = (135, 308)
FRIENDS_QR_ICON = (1006, 129)
HOME_CARD_UID = (110, 431)
HOME_CARD_NICK = (107, 404)


def parse_home_card(entries: list[dict]) -> tuple[str, str]:
    """从首页左下角卡片里抠 UID 与昵称（OCR 常把「ID」读成「D:」）。"""
    uid, nick = "", ""
    for e in entries:
        text = e.get("text", "")
        m = re.search(r"[I1l]?D[:：]?\s*([0-9]{8,})", text)
        if m and not uid:
            uid = m.group(1)
        x, y, w, h = e.get("rect", [0, 0, 0, 0])
        if (
            not nick
            and x < 320
            and 340 <= y <= 460
            and re.fullmatch(r"[\u4e00-\u9fa5]{3,12}", text)
            and e.get("score", 0) >= 0.90
        ):
            nick = text
    return uid, nick


def scan_account(dry: bool = False) -> dict:
    """优先读首页左下角卡片（昵称 + ID）；读不到再走 好友 → 添加好友 → 二维码。"""
    SCAN_LOG.mkdir(parents=True, exist_ok=True)
    ev = eval_module()

    if not ensure_home(ev):
        print("[account] 进不到主界面，放弃", flush=True)
        return {"ok": False, "reason": "not at home"}

    shot = screenshot(SCAN_LOG / "account_home.png")
    entries = ocr_entries(ev, shot)
    uid, nick = parse_home_card(entries)

    if not uid:
        joined = " ".join(e.get("text", "") for e in entries)
        # OCR 会把「好友」读成「好女」；皮肤还会让文字更难读 -> 固定坐标兜底
        target = next((e for e in entries if e.get("text", "").startswith("好")), None)
        where = "OCR" if target else "固定坐标"
        print(f"[account] 首页卡片没读到 ID，走好友页（入口定位：{where}）", flush=True)
        if target:
            x, y, w, h = target["rect"]
            tap(x + w // 2, y + h // 2, 2.0)
        else:
            tap(*HOME_FRIENDS_ICON, 2.0)
        shot = screenshot(SCAN_LOG / "account_friends.png")
        fentries = ocr_entries(ev, shot)
        add = next((e for e in fentries if "添加好友" in e.get("text", "")), None)
        if add:
            x, y, w, h = add["rect"]
            tap(x + w // 2, y + h // 2, 1.8)
        else:
            tap(*FRIENDS_ADD_ENTRY, 1.8)
        tap(*FRIENDS_QR_ICON, 2.0)
        shot = screenshot(SCAN_LOG / "account_qr.png")
        joined = " ".join(ocr_texts(ev, shot, roi=[500, 570, 420, 100]))
        m_uid = re.search(r"ID\s*([0-9O]{6,12})", joined) or re.search(r"([0-9]{8,})", joined)
        uid = m_uid.group(1).replace("O", "0") if m_uid else ""
        nick_join = " ".join(ocr_texts(ev, shot, roi=[800, 15, 460, 70]))
        m_nick = re.search(r"([^\s]{1,16}#[0-9]{4})", nick_join)
        nick = nick or (m_nick.group(1) if m_nick else "")

    print(f"[account] uid={uid!r} nick={nick!r}", flush=True)
    server = server_of_device()
    if dry or not uid:
        print("[account] dry 或未识别到 uid，截图在 日志/ops_scan/", flush=True)
        return {"server": server, "uid": uid, "nick": nick, "ok": bool(uid)}

    path = ACCOUNTS / f"{server}_{uid}.json"
    snap = load_snapshot(path)
    snap.update({"server": server, "uid": uid, "nick": nick or snap.get("nick", "")})
    snap.setdefault("game_version", game_version())
    save_snapshot(path, snap)
    print(f"[account] 已写入 {path}", flush=True)
    return {"server": server, "uid": uid, "nick": nick, "ok": True, "path": str(path)}


# ---------------------------------------------------------------- strength
class OperBoxRunner:
    """用 MaaCore 的 OperBox 任务拉全量练度（名字/精英化/等级/潜能）。"""

    def __init__(self):
        os.environ["PATH"] = str(DLL_DIR) + os.pathsep + os.environ.get("PATH", "")
        self.lib = ctypes.WinDLL(str(DLL_DIR / "MaaCore.dll"))
        self.records: list[dict] = []
        self.done = threading.Event()
        self._declare()
        self.cb = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p)(self._on_cb)
        if not self.lib.AsstSetUserDir(str(SCAN_LOG).encode("utf-8")):
            raise RuntimeError("AsstSetUserDir failed")
        if not self.lib.AsstLoadResource(str(REPO_ROOT).encode("utf-8")):
            raise RuntimeError("AsstLoadResource failed")
        self.handle = self.lib.AsstCreateEx(self.cb, None)
        if not self.handle:
            raise RuntimeError("AsstCreateEx failed")

    def _declare(self):
        lib, c, h = self.lib, ctypes.c_char_p, ctypes.c_void_p
        lib.AsstSetUserDir.argtypes, lib.AsstSetUserDir.restype = [c], ctypes.c_bool
        lib.AsstLoadResource.argtypes, lib.AsstLoadResource.restype = [c], ctypes.c_bool
        lib.AsstCreateEx.restype = ctypes.c_void_p
        lib.AsstDestroy.argtypes, lib.AsstDestroy.restype = [h], None
        lib.AsstSetInstanceOption.argtypes = [h, ctypes.c_int, c]
        lib.AsstSetInstanceOption.restype = ctypes.c_bool
        lib.AsstConnect.argtypes, lib.AsstConnect.restype = [h, c, c, c], ctypes.c_bool
        lib.AsstAppendTask.argtypes, lib.AsstAppendTask.restype = [h, c, c], ctypes.c_int
        lib.AsstStart.argtypes, lib.AsstStart.restype = [h], ctypes.c_bool
        lib.AsstRunning.argtypes, lib.AsstRunning.restype = [h], ctypes.c_bool
        lib.AsstStop.argtypes, lib.AsstStop.restype = [h], ctypes.c_bool

    def _on_cb(self, msg, details, _arg):
        if not details:
            return
        try:
            parsed = json.loads(details.decode("utf-8"))
        except Exception:
            return
        if parsed.get("what") == "OperBoxInfo":
            self.records.append(parsed)
            info = parsed.get("details", {})
            print(
                f"[strength] 回调 done={info.get('done')} own={len(info.get('own_opers', []))}",
                flush=True,
            )
            if info.get("done"):
                self.done.set()
        elif msg == 3:  # AllTasksCompleted
            self.done.set()

    def run(self, timeout: int = 1800) -> dict[str, dict]:
        if not self.lib.AsstSetInstanceOption(self.handle, 6, b"Bilibili"):
            raise RuntimeError("set ClientType failed")
        if not self.lib.AsstConnect(self.handle, ADB.encode(), ADB_ADDR.encode(), b""):
            raise RuntimeError("AsstConnect failed")
        if self.lib.AsstAppendTask(self.handle, b"OperBox", b"{}") <= 0:
            raise RuntimeError("AsstAppendTask(OperBox) failed")
        if not self.lib.AsstStart(self.handle):
            raise RuntimeError("AsstStart failed")
        deadline = time.time() + timeout
        while not self.done.is_set() and time.time() < deadline:
            time.sleep(1.0)
        self.lib.AsstStop(self.handle)
        self.lib.AsstDestroy(self.handle)

        best: dict[str, dict] = {}
        for rec in self.records:
            for op in rec.get("details", {}).get("own_opers", []):
                best[op["name"]] = op
        return best


def scan_strength(uid_hint: str | None) -> dict:
    own = OperBoxRunner().run()
    print(f"[strength] 识别到 {len(own)} 名持有干员", flush=True)
    snap, path = resolve_snapshot(uid_hint)
    operators = snap.setdefault("operators", {})
    for name, op in own.items():
        entry = operators.setdefault(name, {})
        entry.update(
            {
                "elite": int(op.get("elite", 0)),
                "level": int(op.get("level", 0)),
                "potential": int(op.get("potential", 0)),
                "rarity": int(op.get("rarity", 0)),
                "own": True,
            }
        )
    snap["oper_count"] = sum(1 for v in operators.values() if v.get("own"))
    snap["game_version"] = game_version()
    snap["strength_scanned_at"] = now_iso()
    save_snapshot(path, snap)
    print(f"[strength] 已合并写入 {path}", flush=True)
    return {"count": len(own), "path": str(path)}


# ---------------------------------------------------------------- modules
def classify_module(ev, shot: pathlib.Path) -> tuple[str, int]:
    """按主人给的判据分类：解锁 -> 未开；MAX -> 3 级；数字 -> 对应等级。"""
    joined = " ".join(ocr_texts(ev, shot))
    if "解锁" in joined:
        return "locked", 0
    if re.search(r"MAX", joined, re.I):
        return "unlocked", 3
    m = re.search(r"(?<![0-9])([1-3])(?![0-9])", joined)
    if m:
        return "unlocked", int(m.group(1))
    return "unknown", 0


def scan_modules(uid_hint: str | None) -> dict:
    ev = eval_module()
    SCAN_LOG.mkdir(parents=True, exist_ok=True)
    snap, path = resolve_snapshot(uid_hint)
    operators = snap.setdefault("operators", {})

    print(f"[modules] 扫描名单：{MODULE_TARGETS}（特限 + 机械师）", flush=True)
    for name in MODULE_TARGETS:
        shot = SCAN_LOG / f"{name}.png"
        print(f"[modules] 请打开 {name} 的模组面板，然后回车…", flush=True)
        try:
            input()
        except EOFError:
            raise SystemExit(
                "模组扫描需要在模拟器打开当前干员的模组面板后确认；" 
                "请用交互终端运行 --modules，不要把 stdin 重定向为空。"
            )
        # 模组页可纵向滚动；一次截图会漏掉下方模组条目。采集首屏和两次
        # 纵向滚动后的页面，判据仍按「解锁 / MAX / 数字」取最强阳性证据。
        statuses: list[tuple[str, int]] = []
        for page in range(3):
            page_shot = shot if page == 0 else SCAN_LOG / f"{name}_{page}.png"
            screenshot(page_shot)
            statuses.append(classify_module(ev, page_shot))
            if page < 2:
                adb("shell", "input", "swipe", "640", "560", "640", "180", "500")
                time.sleep(1.0)
        positive = [x for x in statuses if x[0] == "unlocked"]
        if positive:
            status, level = max(positive, key=lambda x: x[1])
        elif any(x[0] == "locked" for x in statuses):
            status, level = "locked", 0
        else:
            status, level = "unknown", 0
        entry = operators.setdefault(name, {})
        entry["module_status"] = status
        entry["module_levels"] = {"primary": level}
        entry["module_screenshot"] = shot.name
        if name in SPECIAL_TARGETS:
            entry["special"] = True
        save_snapshot(path, snap)
        print(f"[modules] {name} -> {status} level={level}", flush=True)

    snap["module_scanned_at"] = now_iso()
    save_snapshot(path, snap)
    print(f"[modules] 已写入 {path}", flush=True)
    return {"path": str(path)}


# ---------------------------------------------------------------- main
def main() -> int:
    parser = argparse.ArgumentParser(description="干员强度/模组/账号识别")
    parser.add_argument("--account", action="store_true")
    parser.add_argument("--strength", action="store_true")
    parser.add_argument("--modules", action="store_true")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--uid", help="账号快照 uid（多个账号时指定）")
    parser.add_argument("--dry", action="store_true", help="只识别不落盘")
    args = parser.parse_args()

    adb("connect", ADB_ADDR)

    if args.all or args.account:
        scan_account(dry=args.dry)
    if args.all or args.strength:
        scan_strength(args.uid)
    if args.all or args.modules:
        scan_modules(args.uid)
    if not (args.all or args.account or args.strength or args.modules):
        parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

