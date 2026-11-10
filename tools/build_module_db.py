# -*- coding: utf-8 -*-
"""把「干员模组总览」的人工转录表落地成 JSON / CSV。

数据来源：2026-11-03 深夜，游戏内 档案 > 模组陈列室 > 干员模组总览（全部 / 等级排序），
逐页截图（D:\\dsh\\eval\\mod_pages\\page_*.png 与 mod_pages2\\p_*.png）人工判读。
总览页统计：总收集模组 119 / STAGE3 模组 113 / 拥有模组干员 99。

每条记录 = 一个槽位。code 形如 `ISW X` / `ISW Y` / `ISW α` / `SUX β` / `APF Δ`，
type 由后缀推出：x / y / alpha(α) / beta(β) / delta(Δ)。
level: 1/2/3；3 在游戏里显示为 MAX。
"""
import csv
import json
import pathlib

OUT_JSON = pathlib.Path(r"D:\dsh\明日方舟\数据\accounts\modules_11979813.json")
OUT_CSV = pathlib.Path(r"D:\dsh\明日方舟\数据\accounts\modules_11979813.csv")

# 名字: [(code, level), ...]  —— 只记已开启的槽位
RAW = """
风笛: CHG X:3
可露希尔: TAC X:3
凛御银灰: CSI X:3
缪尔赛思: TAC X:3
推进之王: SOL X:3, SOL Y:3
伊内丝: ADC Y:3
艾丽妮: ISW α:3
百炼嘉维尔: CEX X:3, RAI α:3
陈: SWX X:3
重岳: FGT X:1
丰川祥子: LOG Y:3
煌: CBN X:3
棘刺: LOG X:3
玛恩纳: LIB X:3
怒潮凛冬: HAI X:3
佩佩: RAI α:3
仇白: LOG X:3
山: FGT Y:3
史尔特尔: APF X:3
斯卡蒂: ORY Y:3
维娜·维多利亚: APF X:3
薇薇安娜: APF Δ:3
乌尔比安: CRL X:3
耀骑士临光: DRE X:3
隐德来希: RED Y:3
银灰: LOG X:3
涤火杰西卡: SPY X:3
号角: ISW α:3
机械师: SO α:3
年: PRO X:3, PRO Y:3
泥岩: UNY X:3
塞雷娅: GUL X:3, GUL Y:3
森蚺: RAI α:3
珊比: PRP X:3
黍: GUA X:3
星熊: PRO X:3, PRO Y:3
余: PRP X:1
斩业星熊: AST X:3
菲亚梅塔: ISW α:3
黑: ARC Y:3
鸿雪: ARC Y:3
假日威龙陈: RPG Y:3
空弦: MAR X:3, MAR Y:3, ISW α:3
蕾缪安: DEA Y:3
娜仁图亚: LPX:3
能天使: MAR X:3, MAR Y:3
提丰: SIE X:3, ISW α:3
维什戴尔: BOM X:3
焰狐龙梓兰: ARC X:3
艾雅法拉: CCO X:3
澄闪: FLY X:3
荒芜拉普兰德: FLY X:3
刻俄柏: CCO X:3
逻各斯: CCO Δ:3
谬因: BLY Y:3
莫斯提马: SPY Y:3
妮芙: PRP X:3
圣聆初雪: FLY Y:3
夕: SPY Y:3, ISW α:3
伊芙利特: BLX X:3
异客: RAI α:3
真言: PRI X:3
纯烬艾雅法拉: WDM X:3
凯尔希: PHX X:3, ISW α:3
流明: WAR Y:1
Mon3tr: XAH X:3
闪灵: PHY Y:1
缇缇: DNC X:3
夜莺: RED X:3
安洁莉娜: DEC X:3, DEC Y:3, ISW α:3
电弧: SO α:3, SO β:3
令: SUM Y:3
铃兰: DEC X:3
灵知: ISW α:3
麦哲伦: SUM Y:3
娜斯提: CRA X:2
塑心: RIT X:3
遥: BLS Y:3
艾拉: TRP Δ:3
阿斯卡纶: AMB X:3
歌蕾蒂娅: HOK X:3, HOK Y:3
归溟幽灵鲨: PUM X:3, PUM Y:3, ISW α:3
缄默德克萨斯: DEX X:3, DEX Y:3
结城理: PUM Y:2
傀影: ISW α:3
老鲤: MBS X:3
琳琅诗怀雅: ISW α:3
麒麟R夜刀: DEX X:3
弑君者: DEX Y:3
水月: ISW α:3
望: TRP X:3
新约能天使: GES X:3
德克萨斯: SO Y:3
羽毛笔: REX X:3
蓝毒: MAR Y:3
阿米娅: DNC X:3
白面鸮: RED X:3
调香师: RHY Y:3
星极: APF Y:3
"""

MODULE_NAMES = {
    "号角:ISW α": "号角特限证章",
    "提丰:ISW α": "提丰特限证章",
    "灵知:ISW α": "灵知特限证章",
    "风笛:CHG X": "破城矛弹夹",
    "风笛:CHG Y": "棍棒与口袋",
    "号角:FOR Y": "旧日新装",
    "灵知:UMD Y": "一号项目模型",
    "提丰:SIE Y": "冰原的影子",
}

SLOT_TYPE = {"X": "x", "Y": "y", "α": "alpha", "β": "beta", "Δ": "delta"}


def parse():
    ops = {}
    for line in RAW.strip().splitlines():
        line = line.strip()
        if not line:
            continue
        name, rest = line.split(":", 1)
        name = name.strip()
        slots = []
        for item in rest.split(","):
            item = item.strip()
            if not item:
                continue
            code, lv = item.rsplit(":", 1)
            code = code.strip()
            lv = int(lv.strip())
            suffix = code.split()[-1]
            slots.append({
                "code": code,
                "type": SLOT_TYPE.get(suffix, "?"),
                "limited": suffix in ("α", "β", "Δ"),
                "name": MODULE_NAMES.get("%s:%s" % (name, code)),
                "unlocked": True,
                "level": lv,
                "level_text": "MAX" if lv == 3 else "STAGE %d" % lv,
            })
        ops[name] = slots
    return ops


def main():
    ops = parse()
    total = sum(len(v) for v in ops.values())
    stage3 = sum(1 for v in ops.values() for s in v if s["level"] == 3)
    doc = {
        "schema": "blackflow.modules.v1",
        "account": {"uid": "11979813", "nick": "可儿四系楼", "server": "bilibili"},
        "scanned_at": "2026-11-03T23:55:00+08:00",
        "source": "游戏内 档案 > 模组陈列室 > 干员模组总览（全部 / 等级排序），逐页截图人工判读",
        "evidence_screens": [r"D:\dsh\eval\mod_pages", r"D:\dsh\eval\mod_pages2"],
        "overview_stats": {"total_collected": 119, "stage3": 113, "operators_with_modules": 99},
        "recorded": {"operators": len(ops), "modules": total, "stage3": stage3,
                     "non_stage3": total - stage3, "missing_vs_overview": 119 - total},
        "slot_types": {
            "x": "普通模组 X（第一槽）",
            "y": "普通模组 Y（第二槽）",
            "alpha": "特限模组 α（适配限定模式，黑流树海/集成战略专用）",
            "beta": "特限模组 β（部分干员写成 β）",
            "delta": "特限模组 Δ（部分干员写成 Δ）",
        },
        "unlock_rules": {
            "stage1": "需要干员精英化二（游戏内在未精二时显示「干员精二后查看」）",
            "stage2": "需模组数据块 / 材料（面板「解锁」页显示，未逐条采集）",
            "stage3": "满级，界面显示 MAX",
            "max_level": 3,
            "note": "模组等级只与本槽位有关；详情页右下角 STAGE 只代表当前选中槽，不能当特限证据",
        },
        "operators": {k: {"slots": v} for k, v in ops.items()},
        "caveats": [
            "vs 总览统计 119 少 2 个槽位：总览逐页截图存在跳行风险，缺口待补扫（见 D:\\dsh\\明日方舟\\文档\\黑流树海-进度跟踪.md 第十九节）",
            "模组中文名仅记录了 8 个（来自面板截图），其余留空",
        ],
    }
    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    OUT_JSON.write_text(json.dumps(doc, ensure_ascii=False, indent=2), encoding="utf-8")

    with OUT_CSV.open("w", encoding="utf-8-sig", newline="") as fp:
        w = csv.writer(fp)
        w.writerow(["干员", "模组代号", "槽位类型", "特限", "模组名", "是否开启", "等级", "等级文本"])
        for name, slots in ops.items():
            for s in slots:
                w.writerow([name, s["code"], s["type"], "是" if s["limited"] else "否",
                            s["name"] or "", "已开启", s["level"], s["level_text"]])
    print(json.dumps({"operators": len(ops), "modules": total, "stage3": stage3,
                      "non_stage3": total - stage3}, ensure_ascii=False))
    print(OUT_JSON)
    print(OUT_CSV)


if __name__ == "__main__":
    main()
