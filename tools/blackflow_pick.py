"""黑流树海开局抓位解算（规则 + 练度闸门）。

规则来源：``数据/blackflow_opening_rules.json``（由《黑流树海-开局抓位规则.md》机读化）
练度来源：``数据/accounts/bilibili_<uid>.json``（由 ``tools/ops_snapshot.py`` 生成）

口径（与《黑流树海-开局抓位评分机制.md》一致）：
    顺序是主键，分数只当闸门；不达标就顺延到下一位，不重排顺序。
    U = 0.45*E + 0.35*L + 0.20*M   （E 精英化 / L 等级进度 / M 模组）
    L = 等级 ÷ 该稀有度在该精英化下的等级上限
    M 未知时按 0.5 中性处理（模组没扫过不惩罚）。

用法：
    python tools\\blackflow_pick.py --table
    python tools\\blackflow_pick.py --squad 远程战术分队 --voucher 近卫
    python tools\\blackflow_pick.py --squad 堡垒战术分队 --voucher 重装 --json
"""

from __future__ import annotations

import argparse
import glob
import io
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "..", "..", "数据")
DATA = os.path.normpath(DATA)
RULES_PATH = os.path.join(DATA, "blackflow_opening_rules.json")
ACCOUNT_DIR = os.path.join(DATA, "accounts")


def load_rules() -> dict:
    with io.open(RULES_PATH, encoding="utf-8") as fp:
        return json.load(fp)


def load_account(uid: str | None = None, client: str | None = None) -> tuple[dict, str]:
    """Select by identity inside a roster snapshot, never by filename order."""
    package = {
        "Official": "com.hypergryph.arknights",
        "Bilibili": "com.hypergryph.arknights.bilibili",
    }.get(client)
    if client is not None and package is None:
        raise ValueError(f"Unsupported client: {client}")
    files = sorted(glob.glob(os.path.join(ACCOUNT_DIR, "*.json")))
    candidates = []
    for path in files:
        with io.open(path, encoding="utf-8") as fp:
            snap = json.load(fp)
        if not isinstance(snap, dict) or not isinstance(snap.get("operators"), dict):
            continue
        identity = snap.get("account", {})
        snapshot_uid = str(identity.get("uid", "")).strip()
        if not snapshot_uid or (uid is not None and snapshot_uid != str(uid)):
            continue
        if package is not None and snap.get("package") != package:
            continue
        candidates.append((snap, os.path.basename(path)))
    if len(candidates) != 1:
        raise SystemExit(
            f"账号快照必须唯一匹配 UID 与客户端：uid={uid}, client={client}, matches={len(candidates)}"
        )
    return candidates[0]


def score_oper(oper: dict, gates: dict) -> dict:
    elite = int(oper.get("elite") or 0)
    level = int(oper.get("level") or 0)
    rarity = str(oper.get("rarity") or 0)
    cap_by_rarity = gates["level_cap_by_rarity"].get(rarity) or {
        "e2": 90,
        "e1": 80,
        "e0": 50,
    }
    cap = cap_by_rarity.get(f"e{elite}") or cap_by_rarity["e0"]
    level_ratio = min(1.0, level / cap) if cap else 0.0

    e = float(gates["elite"].get(str(elite), 0.15))

    module = oper.get("module_status")
    if module == "unlocked":
        lv = int(oper.get("module_level") or 0)
        m = (
            gates["module"]["unlocked_and_level_ok"]
            if lv >= 3
            else gates["module"]["unlocked_level_low"]
        )
    elif module == "locked":
        m = gates["module"]["locked"]
    else:
        m = gates["module"]["unknown"]

    w = gates["weights_U"]
    u = w["elite"] * e + w["level"] * level_ratio + w["module"] * m
    return {
        "elite": elite,
        "level": level,
        "rarity": int(rarity or 0),
        "level_ratio": round(level_ratio, 3),
        "module": module or "unknown",
        "module_level": int(oper.get("module_level") or 0),
        "teixian_verified": bool(oper.get("teixian_verified")),
        "U": round(u, 3),
        "available": bool(oper.get("own", True)) and u >= gates["theta"],
    }


def flatten_order(order: list) -> list[str]:
    out: list[str] = []
    for item in order:
        if isinstance(item, str):
            out.append(item)
        elif isinstance(item, dict) and "any_of" in item:
            out.extend(item["any_of"])
    return out


SQUAD_RULE = {
    "堡垒战术分队": "R2",
    "破坏术士分队": "R3",
    "地面突袭分队": "R4",
    "突袭战术分队": "R1",
}


def pick_rule(rules: dict, squad: str, voucher_role: str) -> dict:
    # 1) 显式写了「分队 + 券」的规则优先
    for rule in rules["rules"]:
        for cond in rule["trigger"]["any_of"]:
            if cond.get("universal_precondition"):
                continue
            if cond.get("squad") == squad and cond.get("voucher") == voucher_role:
                return rule
    # 2) 只有「分队」这一条线时，按分队的固定归属取规则
    #    （规则 JSON 里 R2/R3/R4 的 trigger 都写成 universal，梯队归属靠这里补）
    target = SQUAD_RULE.get(squad)
    if target:
        for rule in rules["rules"]:
            if rule["id"] == target:
                return rule
    # 3) 近卫券在通用前置里归 R1
    if voucher_role == "近卫":
        for rule in rules["rules"]:
            if rule["id"] == "R1":
                return rule
    return rules["fallback"]


def requirement_blocks(req: dict | None, info: dict) -> str | None:
    """检查步骤级 operator_requirements。返回拦截原因，None 表示放行。"""
    if not req:
        return None
    want = req.get("module")
    if want == "特限" and not info.get("teixian_verified"):
        return "需「特限」模组，但具体槽位未逐槽确认（详情页只显示当前选中模组）"
    if want:
        status = info["module"]
        if status == "unknown":
            return f"需「{want}」模组，状态未知（模组未扫描）"
        if status != "unlocked":
            return f"需「{want}」模组，当前未解锁"
    return None


def resolve(
    rules: dict, snapshot: dict, squad: str, voucher_role: str, limit: int = 3
) -> dict:
    gates = rules["gates"]
    opers = snapshot.get("operators", {})
    rule = pick_rule(rules, squad, voucher_role)
    steps = sorted(rule["steps"], key=lambda s: s.get("priority", 99))

    chosen: list[dict] = []
    skipped: list[dict] = []
    for step in steps:
        step_voucher = step.get("voucher")
        # 只走「正好对得上这张券」的那一步：手里是术师券就不该去抓特种/重装名单
        if step_voucher not in (None, "任意", "对应职业", voucher_role):
            continue
        if step.get("kind") == "module_gate":
            name = step["operator"]
            info = score_oper(opers.get(name, {}), gates) if name in opers else None
            need = int(step.get("require", {}).get("module_level_min", 3))
            ok = bool(
                info
                and info["module"] == "unlocked"
                and info["module_level"] >= need
                and info["available"]
            )
            if ok:
                chosen.append(
                    {"name": name, "via": "module_gate", "rule": rule["id"], **info}
                )
            else:
                skipped.append(
                    {"name": name, "reason": "模组未达 3 级或未知", "rule": rule["id"]}
                )
            continue
        reqs = step.get("operator_requirements") or {}
        for name in flatten_order(step["order"]):
            if len(chosen) >= limit:
                break
            if name not in opers:
                skipped.append(
                    {
                        "name": name,
                        "reason": "快照里没有（未拥有/未识别到）",
                        "rule": rule["id"],
                    }
                )
                continue
            info = score_oper(opers[name], gates)
            req_block = requirement_blocks(reqs.get(name), info)
            if req_block:
                skipped.append({"name": name, "reason": req_block, "rule": rule["id"]})
                continue
            if info["available"]:
                chosen.append(
                    {"name": name, "via": "sequence", "rule": rule["id"], **info}
                )
            else:
                skipped.append(
                    {
                        "name": name,
                        "reason": f"U={info['U']} 低于阈值 {gates['theta']}",
                        "rule": rule["id"],
                    }
                )
        if len(chosen) >= limit:
            break

    if not chosen:
        for name in flatten_order(rules["fallback"]["steps"][0]["order"]):
            if len(chosen) >= limit:
                break
            if name not in opers:
                continue
            info = score_oper(opers[name], gates)
            if info["available"]:
                chosen.append({"name": name, "via": "fallback", "rule": "R5", **info})
    return {
        "rule": rule["id"],
        "rule_title": rule["title"],
        "squad": squad,
        "voucher": voucher_role,
        "chosen": chosen,
        "skipped": skipped,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="黑流树海开局抓位解算")
    parser.add_argument("--squad", default="远程战术分队")
    parser.add_argument("--voucher", default="近卫")
    parser.add_argument("--limit", type=int, default=3)
    parser.add_argument("--uid")
    parser.add_argument(
        "--client", choices=("Official", "Bilibili"), default="Official"
    )
    parser.add_argument("--json", action="store_true")
    parser.add_argument(
        "--table", action="store_true", help="逐条列出规则里的干员及其练度状态"
    )
    args = parser.parse_args()

    rules = load_rules()
    snapshot, filename = load_account(args.uid, args.client)
    gates = rules["gates"]
    opers = snapshot.get("operators", {})

    if args.table:
        names: list[str] = []
        for rule in rules["rules"] + [rules["fallback"]]:
            for step in rule["steps"]:
                if step.get("kind") == "module_gate":
                    names.append(step["operator"])
                else:
                    names.extend(flatten_order(step["order"]))
        seen: list[str] = []
        for n in names:
            if n not in seen:
                seen.append(n)
        print(f"快照：{filename}（{len(opers)} 名干员）  阈值 U≥{gates['theta']}")
        print(f"{'干员':<12}{'精英':>4}{'等级':>4}{'稀有':>4}{'模组':>8}{'U':>7}  状态")
        missing = 0
        for n in seen:
            if n not in opers:
                missing += 1
                print(f"{n:<12}{'-':>4}{'-':>4}{'-':>4}{'-':>8}{'-':>7}  快照里没有")
                continue
            info = score_oper(opers[n], gates)
            state = "可用" if info["available"] else f"闸门未过（U<{gates['theta']}）"
            print(
                f"{n:<12}{info['elite']:>4}{info['level']:>4}{info['rarity']:>4}"
                f"{info['module']:>8}{info['U']:>7}  {state}"
            )
        print(f"\n规则涉及 {len(seen)} 人，快照缺失 {missing} 人")
        return 0

    result = resolve(rules, snapshot, args.squad, args.voucher, args.limit)
    result["account"] = {
        "uid": snapshot["account"]["uid"],
        "client": args.client,
        "snapshot": filename,
        "scanned_at": snapshot.get("scanned_at"),
    }
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    print(
        f"[{result['rule']}] {result['rule_title']}  分队={args.squad}  券={args.voucher}"
    )
    for i, item in enumerate(result["chosen"], 1):
        print(
            f"  {i}. {item['name']}（精英{item['elite']} lv{item['level']} U={item['U']}，via {item['via']}）"
        )
    if result["skipped"]:
        print(
            "  跳过："
            + "；".join(f"{s['name']}（{s['reason']}）" for s in result["skipped"][:6])
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
