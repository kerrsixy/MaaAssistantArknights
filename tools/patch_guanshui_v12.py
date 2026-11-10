# -*- coding: utf-8 -*-
"""灌水贤者 v12：第 1 手放宽到「先锋/近卫/领主」。

起因：run_02（06:13 灌水贤者）编队是 维什戴尔 + 结构性原理 + 晓歌 + 卡达，
第 1 手只认 `重装/机械师/挡人先锋/其他地面`，晓歌（先锋）不匹配 →
第 1 手落空，插件退回通用兜底，把晓歌丢到 (6,4) 这种非计划位置，最后输掉。

放宽后，先锋也能站 (7,3) 卡住 y=3 走廊；站不住总比没人站强。
"""
import io
import json
import pathlib
import shutil
import sys

PATH = pathlib.Path(
    r"D:\dsh\明日方舟\代码\MaaAssistantArknights\resource\roguelike\BlackFlow\autopilot\灌水贤者.json"
)
EXTRA = ["先锋", "近卫", "领主"]


def main():
    apply = "--apply" in sys.argv
    data = json.loads(io.open(PATH, encoding="utf-8").read())
    plan = data["deploy_plan"]
    first = plan[0]
    if any(g in first["groups"] for g in EXTRA):
        print("already patched")
        return 0
    first["groups"].extend(EXTRA)
    data.setdefault("doc", []).append(
        "v12：第 1 手 groups 追加『先锋/近卫/领主』——开局只有低费先锋时，"
        "让他们去卡 (7,3)，而不是因为组不匹配退回兜底乱放。"
    )
    print("hand1 groups ->", first["groups"])
    if not apply:
        print("dry-run; add --apply to write")
        return 0
    bak = PATH.with_suffix(".json.bak-v11")
    if not bak.exists():
        shutil.copy2(PATH, bak)
    io.open(PATH, "w", encoding="utf-8").write(
        json.dumps(data, ensure_ascii=False, indent=2) + "\n"
    )
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
