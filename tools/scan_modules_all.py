"""全量扫描「档案 › 模组陈列室 › 干员模组总览（已持有）」的模组等级。

整页 OCR 只能稳定读到干员名（名字 22px），模组图标上的小角标（数字 1/2/3）分辨率太低。
所以第二步按名字定位到每一行，把该行图标区放大 4 倍再 OCR，读角标数字与槽位代号。

用法::

    python tools/scan_modules_all.py [--max-pages 25] [--out D:\\dsh\\eval\\modules_owned.json]
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ADB = r"D:\app\tool\mumu模拟器\MuMuPlayer-12.0\nx_main\adb.exe"
DEV = "127.0.0.1:16416"
SHOT = r"D:\dsh\eval\module_page.png"
ROW = r"D:\dsh\eval\module_row.png"
OUT = r"D:\dsh\eval\modules_owned.json"
LINE = re.compile(r"text:\s*(\S+?),\s*rect:\s*\[\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\]\s*,\s*score:\s*([0-9.]+)")


def adb(*args: str) -> str:
    return subprocess.run([ADB, "-s", DEV, *args], capture_output=True, text=True).stdout


def grab() -> str:
    adb("shell", "screencap", "-p", "/sdcard/mp.png")
    adb("pull", "/sdcard/mp.png", SHOT)
    return SHOT


def ocr(image: str, roi: str | None = None) -> list[dict]:
    cmd = [sys.executable, str(ROOT / "tools" / "maa_core_eval.py"), "--mode", "ocr", image]
    if roi:
        cmd += ["--task", "OperBoxNameOCR", "--roi", roi]
    out = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
    items = []
    for m in LINE.finditer(out):
        items.append(
            {
                "text": m.group(1),
                "x": int(m.group(2)),
                "y": int(m.group(3)),
                "w": int(m.group(4)),
                "h": int(m.group(5)),
                "score": float(m.group(6)),
            }
        )
    return items


def is_name(t: str) -> bool:
    return 2 <= len(t) <= 6 and all("\u4e00" <= ch <= "\u9fff" for ch in t)


def read_row(image: str, name: dict) -> dict:
    """把某个干员的模组图标行放大 4 倍后 OCR，读角标数字与槽位代号。"""
    im = Image.open(image)
    x = max(0, name["x"] - 12)
    y = max(0, name["y"] + 36)
    crop = im.crop((x, y, min(im.width, x + 210), min(im.height, y + 42)))
    crop = crop.resize((crop.width * 4, crop.height * 4), Image.LANCZOS)
    crop.save(ROW)
    items = ocr(ROW)
    levels = []
    slots = []
    for it in items:
        t = it["text"].strip()
        if re.fullmatch(r"[0-9]{1,2}", t) and it["score"] >= 0.55:
            v = int(t)
            if 1 <= v <= 3:
                levels.append(v)
        elif 2 <= len(t) <= 6 and it["score"] >= 0.5 and re.search(r"[A-Za-zαβΔ]", t):
            slots.append(t)
    return {"levels": sorted(levels, reverse=True), "slots": slots}


def parse_page(image: str) -> dict[str, dict]:
    names = [i for i in ocr(image, "0,60,1280,660") if i["score"] >= 0.85 and is_name(i["text"])]
    result: dict[str, dict] = {}
    for n in names:
        row = read_row(image, n)
        result[n["text"]] = {"levels": row["levels"], "slots": row["slots"], "y": n["y"]}
    return result


def main() -> int:
    max_pages = 25
    if "--max-pages" in sys.argv:
        max_pages = int(sys.argv[sys.argv.index("--max-pages") + 1])
    out = OUT
    if "--out" in sys.argv:
        out = sys.argv[sys.argv.index("--out") + 1]

    every: dict[str, dict] = {}
    last_sig = None
    page = 0

    for _ in range(15):
        adb("shell", "input", "swipe", "640", "200", "640", "650", "900")
        time.sleep(0.5)
    time.sleep(0.8)

    for page in range(1, max_pages + 1):
        page_map = parse_page(grab())
        sig = tuple(sorted(page_map.keys()))
        new = sum(1 for k in page_map if k not in every)
        every.update(page_map)
        with_marks = sum(1 for v in page_map.values() if v["levels"])
        print(f"page {page}: {len(page_map)} ops ({with_marks} with level), new {new}, total {len(every)}", flush=True)
        if not new or sig == last_sig:
            break
        last_sig = sig
        adb("shell", "input", "swipe", "640", "600", "640", "180", "900")
        time.sleep(0.9)

    Path(out).write_text(json.dumps(every, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"done pages={page} ops={len(every)} -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

