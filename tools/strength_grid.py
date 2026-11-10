"""从干员列表页批量读「名字 + 等级」（翻页直到重复）。

用法：python tools/strength_grid.py [--pages N] [--uid UID] [--dry]
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import ops_scan as S  # noqa: E402

NAME_Y_ROWS = (330, 385), (645, 700)  # 两行卡片名字的 y 区间
LEVEL_Y_OFFSET = 38  # 名字上方约 38px 是等级


def parse_page(entries: list[dict]) -> dict[str, int]:
    names, levels = [], []
    for e in entries:
        text = e.get("text", "").strip()
        x, y, w, h = e.get("rect", [0, 0, 0, 0])
        if re.fullmatch(r"[\u4e00-\u9fa5·]{2,12}", text) and e.get("score", 0) >= 0.80:
            if any(lo <= y <= hi for lo, hi in NAME_Y_ROWS):
                names.append((x + w // 2, y, text))
        elif re.fullmatch(r"[0-9OolI]{1,3}", text):
            norm = text.replace("O", "0").replace("o", "0").replace("l", "1").replace("I", "1")
            norm = norm.lstrip("0") or "0"
            if norm.isdigit() and 1 <= int(norm) <= 90:
                levels.append((x + w // 2, y, int(norm)))
    out: dict[str, int] = {}
    for nx, ny, name in names:
        cands = [(abs(lx - nx), ly, lv) for lx, ly, lv in levels if abs(ly - (ny - LEVEL_Y_OFFSET)) < 18]
        if cands:
            out[name] = min(cands)[2]
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--pages", type=int, default=40)
    ap.add_argument("--uid")
    ap.add_argument("--dry", action="store_true")
    args = ap.parse_args()

    S.adb("connect", S.ADB_ADDR)
    ev = S.eval_module()
    if not S.ensure_home(ev):
        print("not home")
        return 1
    S.tap(1085, 345, 3.0)  # 首页「干员」

    shot = S.SCAN_LOG / "strength_grid.png"
    seen: dict[str, int] = {}
    for page in range(args.pages):
        S.screenshot(shot)
        page_map = parse_page(S.ocr_entries(ev, shot))
        fresh = {k: v for k, v in page_map.items() if k not in seen}
        print(f"page {page + 1}: {len(page_map)} cards, {len(fresh)} new", flush=True)
        if not fresh:
            break
        seen.update(page_map)
        S.adb("shell", "input swipe 640 620 640 220 400")
        import time

        time.sleep(1.5)

    print(f"total {len(seen)} operators", flush=True)
    if args.dry or not seen:
        pathlib.Path(r"D:\dsh\eval\strength_grid.json").write_text(
            json.dumps(seen, ensure_ascii=False, indent=1), encoding="utf-8"
        )
        return 0

    snap, path = S.resolve_snapshot(args.uid)
    operators = snap.setdefault("operators", {})
    for name, level in seen.items():
        operators.setdefault(name, {})["level"] = level
        operators[name]["own"] = True
    snap["oper_count"] = len(seen)
    snap["strength_grid_scanned_at"] = S.now_iso()
    S.save_snapshot(path, snap)
    print(f"merged -> {path}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
