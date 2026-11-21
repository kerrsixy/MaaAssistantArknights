"""Build a versioned BlackFlow valuation catalog without changing live decisions.

Network refreshes use an explicit Clash proxy and never retry directly. Unknown
buffs remain in the catalog as unsupported evidence, rather than zero-value items.
"""

import argparse
import hashlib
import json
import math
import os
import re
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

SCHEMA_VERSION = 1
EXTRACTOR_VERSION = 1
SOURCE_PATH = "zh_CN/gamedata/excel/roguelike_topic_table.json"
REPOSITORY = "Kengxxiao/ArknightsGameData"
DEFAULT_PROXY = "http://127.0.0.1:7897"
ITEM_KINDS = {
    "SCRAP": "scrap",
    "RELIC": "relic",
    "ACTIVE_TOOL": "relic",
    "RECRUIT_TICKET": "recruit_voucher",
    "UPGRADE_TICKET": "upgrade_voucher",
}
NON_PURCHASABLE_TYPES = {
    "HP",
    "HPMAX",
    "GOLD",
    "POPULATION",
    "SQUAD_CAPACITY",
    "EXP",
    "SHIELD",
    "SPECIAL_ZONE_AP",
    "MAX_WEIGHT",
    "STASH_RECRUIT_LIMIT",
    "CHARACTER",
    "FEATURE",
    "BAND",
    "LEGACY",
    "NODE_BUOY",
}
ROLES = {
    "pioneer": "Vanguard",
    "warrior": "Guard",
    "tank": "Defender",
    "sniper": "Sniper",
    "caster": "Caster",
    "medic": "Medic",
    "support": "Supporter",
    "special": "Specialist",
}
RESOURCE_TYPES = {
    "GOLD": "ingots",
    "POPULATION": "hope",
    "HP": "heal_hp",
    "HPMAX": "max_hp",
    "SHIELD": "shield",
    "MAX_WEIGHT": "movement_capacity",
    "SQUAD_CAPACITY": "squad_limit",
    "EXP": "experience",
}
ATTRIBUTE_MUL = {"atk": "attack_pct", "def": "defense_pct", "max_hp": "hp_pct"}
ATTRIBUTE_ADD = {
    "attack_speed": "attack_speed",
    "atk": "attack_flat",
    "def": "defense_flat",
    "max_hp": "hp_flat",
    "magic_resistance": "resistance",
    "hp_recovery_per_sec": "hp_regeneration",
    "cost": "deployment_cost",
    "hp_recovery_per_sec_by_max_hp_ratio": "hp_regeneration_pct",
}


def atomic_json(path, value):
    """Replace only after the complete result has been serialized and flushed."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n"
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=path.parent,
            prefix=path.name + ".",
            suffix=".tmp",
            delete=False,
        ) as stream:
            temporary = Path(stream.name)
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def digest(value):
    return hashlib.sha256(value).hexdigest()


def catalog_digest(value):
    content = {
        "schema_version": value["schema_version"],
        "extractor_version": value["extractor_version"],
        "theme": value["theme"],
        "source_sha256": value["source"]["sha256"],
        "items": value["items"],
    }
    return digest(
        json.dumps(
            content,
            sort_keys=True,
            ensure_ascii=False,
            separators=(",", ":"),
            allow_nan=False,
        ).encode()
    )


def plain(text):
    return re.sub(r"</?color(?:=[^>]*)?>", "", text or "")


def finite_number(value):
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(value)
    ):
        raise ValueError("Game effect has an invalid numeric value")
    return value


def blackboard(buff):
    result = {}
    for entry in buff.get("blackboard", []):
        key = entry["key"]
        if key in result:
            raise ValueError(f"Duplicate blackboard key: {key}")
        result[key] = (
            entry["valueStr"]
            if entry.get("valueStr") is not None
            else finite_number(entry["value"])
        )
    return result


def add(features, key, amount):
    features[key] = features.get(key, 0) + finite_number(amount)


def effect(features, kind, amount, trigger, **params):
    features.setdefault("effects", []).append(
        {
            "kind": kind,
            "amount": finite_number(amount),
            "trigger": trigger,
            "params": params,
        }
    )


def role_scope(value):
    if not value:
        return []
    roles = re.split(r"[,|]", value.lower())
    if any(role not in ROLES for role in roles):
        return None
    return [ROLES[role] for role in roles]


def extract_buff(buff, features, resources):
    """Return False for a whole buff whose conditions are not understood."""
    key = buff["key"]
    board = blackboard(buff)
    reward_triggers = {
        "immediate_reward": "immediate",
        "battle_extra_reward": "battle",
        "zone_into_reward": "zone",
        "node_into_reward": "node",
        "immediate_reward_on_vehicle_broken": "vehicle_broken",
    }
    if key in reward_triggers:
        if set(board) - {"id", "count", "node"}:
            return False
        kind = resources.get(board.get("id"))
        if kind is None or "count" not in board:
            return False
        trigger = reward_triggers[key]
        if trigger == "immediate":
            add(
                features,
                {"ingots": "immediate_ingots", "hope": "immediate_hope"}.get(
                    kind, kind
                ),
                board["count"],
            )
        elif trigger == "battle" and kind == "ingots":
            add(features, "ingots_per_battle", board["count"])
        else:
            effect(
                features,
                kind,
                board["count"],
                trigger,
                **({"node": board["node"]} if "node" in board else {}),
            )
        return True
    if key in {"char_attribute_mul", "char_attribute_add"}:
        mapping = ATTRIBUTE_MUL if key.endswith("mul") else ATTRIBUTE_ADD
        if set(board) - set(mapping) - {"selector.profession"}:
            return False
        roles = role_scope(board.get("selector.profession"))
        if roles is None:
            return False
        values = {
            mapping[name]: amount for name, amount in board.items() if name in mapping
        }
        if not values:
            return False
        if roles:
            for role in roles:
                features.setdefault("role_buffs", []).append(
                    {"role": role, "features": values.copy()}
                )
        else:
            for name, amount in values.items():
                add(features, name, amount)
        return True
    if key in {"level_char_limit_add", "level_init_cost_add"} and set(board) == {
        "value"
    }:
        add(
            features,
            "deployment_limit" if key == "level_char_limit_add" else "starting_dp",
            board["value"],
        )
        return True
    if (
        key in {"recruit_cost", "upgrade_cost"}
        and set(board) <= {"rarity", "profession", "delta"}
        and "delta" in board
    ):
        roles = role_scope(board.get("profession"))
        if roles is None:
            return False
        effect(
            features,
            "hope_discount",
            -board["delta"],
            "recruit" if key == "recruit_cost" else "upgrade",
            roles=roles,
            rarities=board.get("rarity", "ALL").split(","),
        )
        return True
    if (
        key == "char_skill_cost_mul"
        and set(board) <= {"scale", "selector.profession"}
        and "scale" in board
    ):
        roles = role_scope(board.get("selector.profession"))
        if roles is None:
            return False
        effect(
            features, "skill_sp_cost_pct", board["scale"] - 1, "passive", roles=roles
        )
        return True
    return False


def extract_goods(description, features, names):
    """Read effect patterns; names select bundle IDs only, never assigned scores."""
    text = plain(description)
    if text in {"估价较低的自然物", "估价较高的自然物"}:
        return True
    patterns = [
        (r"每次作战后估价\+(\d+)", "sale_value", "battle"),
        (r"每次战斗结束后估价\+(\d+)", "sale_value", "battle"),
        (r"每次揭示节点信息时估价\+(\d+)", "sale_value", "reveal_node"),
        (r"每次招募干员时估价\+(\d+)", "sale_value", "recruit"),
        (r"每次获得零件时估价\+(\d+)", "sale_value", "acquire_scrap"),
        (r"获得零件时估价\+(\d+)", "sale_value", "acquire_scrap"),
        (r"每次移动后估价-(\d+)", "sale_value", "move"),
        (r"进入非战斗节点时估价\+(\d+)", "sale_value", "nonbattle_node"),
    ]
    matched = False
    for pattern, kind, trigger in patterns:
        match = re.search(pattern, text)
        if match:
            amount = int(match[1]) * (-1 if "估价-" in pattern else 1)
            effect(features, kind, amount, trigger)
            text = text[: match.start()] + text[match.end() :]
            matched = True
    perfect = re.search(r"每次完美作战后估价\+(\d+)，非完美作战后自身损坏", text)
    if perfect:
        effect(features, "sale_value", int(perfect[1]), "perfect_battle")
        effect(features, "destroy_self", 1, "imperfect_battle")
        text = text.replace(perfect[0], "")
        matched = True
    bundle = re.search(r"获得时立刻获得(\d+)个<([^>]+)>", text)
    if bundle and len(names.get(bundle[2], [])) == 1:
        features["bundle"] = [{"item_id": names[bundle[2]][0], "count": int(bundle[1])}]
        text = text.replace(bundle[0], "")
        matched = True
    leak = re.search(r"敌人进入目标点后扣除的目标生命\+(\d+)", text)
    if leak:
        features["extra_hp_loss_per_leak"] = int(leak[1])
        text = text.replace(leak[0], "")
        matched = True
    return matched and not text.strip("，；。 ")


def extract_passive(data, features):
    text = plain(data["scrapDesc"])
    uses = re.search(r"（可激活(\d+)次）", text)
    if uses:
        text = text.replace(uses[0], "")
    maximum = int(uses[1]) if uses else None
    vehicle = re.match(r"装载加工品移动至.+?时，", text)
    condition = {
        "trigger": "vehicle_move",
        "node": data["node"],
        "buff_stack": data["buffStack"],
        "max_activations": maximum,
    }
    if vehicle:
        text = text[len(vehicle[0]) :]
    else:
        sold = re.match(r"卖出任意一件价值不低于(\d+)源石锭的自然物时，", text)
        if not sold:
            return False
        condition = {
            "trigger": "sell_natural_scrap",
            "minimum_sale_value": int(sold[1]),
            "max_activations": maximum,
        }
        text = text[len(sold[0]) :]
    features["activation_condition"] = condition
    patterns = [
        (r"本次战斗我方单位攻击速度\+(\d+)", "attack_speed", "next_battle"),
        (r"战斗后获得(\d+)个随机收藏品", "random_relic", "after_next_battle"),
        (r"获得(\d+)个随机收藏品", "random_relic", condition["trigger"]),
        (r"获得(\d+)个珍贵的随机收藏品", "rare_random_relic", condition["trigger"]),
        (r"获得(\d+)点护盾", "shield", condition["trigger"]),
        (r"获得(\d+)个随机加工品", "random_move_scrap", condition["trigger"]),
    ]
    matched = False
    for pattern, kind, trigger in patterns:
        match = re.search(pattern, text)
        if match:
            effect(features, kind, int(match[1]), trigger, activation=condition)
            text = text.replace(match[0], "")
            matched = True
    if "揭露全图视野" in text:
        effect(features, "reveal_map", 1, condition["trigger"], activation=condition)
        text = text.replace("揭露全图视野", "")
        matched = True
    return matched and not text.strip("，；。 ")


def extract_scrap(identifier, item, scrap, names):
    subtype = scrap["scrapItemToType"][identifier]
    table = {
        "MOVE": "moveScrapData",
        "GOODS": "goodsScrapData",
        "PASSIVE": "passiveScrapData",
    }.get(subtype)
    if table is None or identifier not in scrap[table]:
        raise ValueError(f"Scrap definition missing: {identifier}")
    data = scrap[table][identifier]
    features = {"sale_value": finite_number(data["sellPrice"]), "scrap_slots": 1}
    unsupported = []
    if subtype == "MOVE":
        movement = {
            "charges": finite_number(data["count"]),
            "range_type": data["rangeType"],
            "node": data["node"],
            "step": finite_number(data["step"]),
            "random": data["isRandomMove"],
            "node_change_target": data["nodeChangeTargetType"],
        }
        if data.get("range"):
            range_data = scrap["moveScrapRangeData"].get(data["range"])
            if range_data is None:
                raise ValueError(f"Movement range missing: {data['range']}")
            movement["grid_offsets"] = range_data["grids"]
            movement["range_id"] = data["range"]
        features["movement"] = movement
        text = plain(data["scrapDesc"])
        movement["persistent_across_regions"] = "无法被携带至下一区域" not in text
        text = re.sub(r"^装载(?:后能够移动至|并使用后会被随机传送至).+?节点", "", text)
        text = re.sub(r"（可移动\d+次）", "", text)
        text = text.replace("无需行动力", "").replace("无法被携带至下一区域", "")
        for label, kind in [
            ("希望", "hope"),
            ("源石锭", "ingots"),
            ("行动力", "action_points"),
        ]:
            match = re.search(r"且(?:每次)?移动后" + label + r"\+(\d+)", text)
            if match:
                effect(features, kind, int(match[1]), "vehicle_move", node=data["node"])
                text = text.replace(match[0], "")
        if data["nodeChangeTargetType"] != "NONE":
            text = re.sub(r"并将其转换为.+$", "", text)
        if text.strip("，；。 "):
            unsupported.append(
                {"key": "movement_description", "description": data["scrapDesc"]}
            )
    elif subtype == "GOODS":
        if not extract_goods(data["scrapDesc"], features, names):
            unsupported.append(
                {"key": "goods_description", "description": data["scrapDesc"]}
            )
    else:
        if not extract_passive(data, features):
            unsupported.append(
                {"key": "passive_description", "description": data["scrapDesc"]}
            )
    return features, unsupported, subtype


def build_catalog(table, source):
    if not isinstance(table, dict):
        raise ValueError("Expected a complete game topic table")
    try:
        detail = table["details"]["rogue_6"]
        scrap = table["modules"]["rogue_6"]["scrap"]
        raw_items = detail["items"]
        buffs = detail["relics"]
    except (KeyError, TypeError) as error:
        raise ValueError(
            "Source does not contain BlackFlow items, buffs and scrap definitions"
        ) from error
    if not all(isinstance(v, dict) and v for v in [raw_items, buffs, scrap]):
        raise ValueError("BlackFlow source is empty or incomplete")
    names = {}
    for identifier, item in raw_items.items():
        names.setdefault(item["name"], []).append(identifier)
    resources = {
        identifier: RESOURCE_TYPES[item["type"]]
        for identifier, item in raw_items.items()
        if item["type"] in RESOURCE_TYPES
    }
    items = {}
    for identifier, item in raw_items.items():
        if item["type"] in NON_PURCHASABLE_TYPES:
            continue
        kind = ITEM_KINDS.get(item["type"], "unknown")
        if item.get("id") != identifier or not item.get("name"):
            raise ValueError(f"Item identity invalid: {identifier}")
        features, unsupported = {}, []
        record = {
            "id": identifier,
            "name": item["name"],
            "kind": kind,
            "description": plain(item.get("usage")),
            "lore": item.get("description"),
            # Purchase prices are not in this table. The shop's observed price is authoritative.
            "base_price": None,
            "rarity": item.get("rarity"),
            "source": {"item_path": f"details.rogue_6.items.{identifier}"},
        }
        if kind == "unknown":
            record["raw_item"] = item
            unsupported.append({"key": "unknown_item_type", "type": item["type"]})
        elif kind == "scrap":
            features, unsupported, record["scrap_type"] = extract_scrap(
                identifier, item, scrap, names
            )
            record["raw_scrap"] = scrap[
                {
                    "MOVE": "moveScrapData",
                    "GOODS": "goodsScrapData",
                    "PASSIVE": "passiveScrapData",
                }[record["scrap_type"]]
            ][identifier]
        elif kind == "recruit_voucher":
            ticket = detail["recruitTickets"].get(identifier)
            if not ticket:
                unsupported.append({"key": "missing_recruit_ticket_definition"})
            else:
                roles = [
                    ROLES.get(role.lower(), role)
                    for role in ticket.get("professionList", [])
                ]
                features["recruit_roles"] = roles
                features["recruit_role"] = roles[0] if len(roles) == 1 else "ALL"
                features["rarities"] = ticket.get("rarityList", [])
                features["extra_char_ids"] = ticket.get("extraCharIds", [])
                record["raw_ticket"] = ticket
                if ticket.get("extraEliteNum") or ticket.get("extraFreeRarity"):
                    unsupported.append({"key": "ticket_extra_rules", "rules": ticket})
        elif kind == "upgrade_voucher":
            record["raw_ticket"] = detail.get("upgradeTickets", {}).get(identifier)
            unsupported.append({"key": "upgrade_ticket_rules"})
        else:
            raw_buffs = buffs.get(identifier, {}).get("buffs")
            if raw_buffs is None:
                unsupported.append({"key": "missing_buff_definition"})
                raw_buffs = []
            for buff in raw_buffs:
                if not extract_buff(buff, features, resources):
                    unsupported.append(buff)
            record["raw_buffs"] = raw_buffs
        record["features"] = features
        record["unsupported_effects"] = unsupported
        record["extraction_complete"] = not unsupported
        items[identifier] = record
    if set(scrap["scrapItemToType"]) != {
        key for key, item in items.items() if item["kind"] == "scrap"
    }:
        raise ValueError("Scrap metadata and module coverage disagree")
    counts = Counter(item["kind"] for item in items.values())
    if not counts["scrap"] or not counts["relic"]:
        raise ValueError("Valuation catalog has no scraps or relics")
    catalog = {
        "schema_version": SCHEMA_VERSION,
        "extractor_version": EXTRACTOR_VERSION,
        "theme": "BlackFlow",
        "source": source,
        "items": dict(sorted(items.items())),
        "coverage": {
            "by_kind": dict(sorted(counts.items())),
            "items": len(items),
            "extraction_complete": sum(
                not item["unsupported_effects"] for item in items.values()
            ),
            "with_unsupported_effects": sum(
                bool(item["unsupported_effects"]) for item in items.values()
            ),
            "unsupported_keys": dict(
                sorted(
                    Counter(
                        effect["key"]
                        for item in items.values()
                        for effect in item["unsupported_effects"]
                    ).items()
                )
            ),
        },
        "live_integration": False,
    }
    catalog["catalog_content_sha256"] = catalog_digest(catalog)
    validate_catalog(catalog)
    return catalog


def validate_catalog(catalog):
    if (
        catalog.get("schema_version") != SCHEMA_VERSION
        or catalog.get("theme") != "BlackFlow"
    ):
        raise ValueError("Incompatible valuation catalog")
    source = catalog.get("source", {})
    if not re.fullmatch(r"[a-f0-9]{64}", source.get("sha256", "")):
        raise ValueError("Catalog lacks a valid source digest")
    if not isinstance(catalog.get("items"), dict) or not catalog["items"]:
        raise ValueError("Catalog has no items")
    for identifier, item in catalog["items"].items():
        if (
            identifier != item.get("id")
            or not item.get("name")
            or not isinstance(item.get("features"), dict)
            or not isinstance(item.get("unsupported_effects"), list)
            or item.get("kind") not in {*ITEM_KINDS.values(), "unknown"}
        ):
            raise ValueError("Invalid catalog item")
        if item.get("extraction_complete") != (not item.get("unsupported_effects")):
            raise ValueError("Unsupported effects have been hidden")
    coverage = catalog.get("coverage", {})
    if coverage.get("items") != len(catalog["items"]) or coverage.get(
        "by_kind"
    ) != dict(Counter(item["kind"] for item in catalog["items"].values())):
        raise ValueError("Catalog coverage does not match its items")
    complete = sum(
        not item["unsupported_effects"] for item in catalog["items"].values()
    )
    if (
        coverage.get("extraction_complete") != complete
        or coverage.get("with_unsupported_effects") != len(catalog["items"]) - complete
    ):
        raise ValueError("Catalog extraction coverage is inconsistent")
    if catalog.get("catalog_content_sha256") != catalog_digest(catalog):
        raise ValueError("Catalog content fingerprint mismatch")
    json.dumps(catalog, allow_nan=False)


class ClashSource:
    def __init__(self, proxy=DEFAULT_PROXY):
        parsed = urllib.parse.urlsplit(proxy)
        if (
            parsed.scheme != "http"
            or parsed.hostname not in {"127.0.0.1", "localhost", "::1"}
            or not parsed.port
        ):
            raise ValueError("Use an explicit local Clash HTTP/mixed proxy")
        if parsed.username or parsed.password:
            raise ValueError("Proxy credentials are not accepted")
        self.proxy = proxy
        os.environ.update(
            HTTP_PROXY=proxy,
            HTTPS_PROXY=proxy,
            NO_PROXY="",
            http_proxy=proxy,
            https_proxy=proxy,
            no_proxy="",
        )
        self.opener = urllib.request.build_opener(
            urllib.request.ProxyHandler({"http": proxy, "https": proxy})
        )

    def fetch(self, url):
        parsed = urllib.parse.urlsplit(url)
        if parsed.scheme != "https" or parsed.hostname not in {
            "api.github.com",
            "raw.githubusercontent.com",
        }:
            raise ValueError("Unexpected game data source")
        request = urllib.request.Request(
            url,
            headers={
                "User-Agent": "MAA-BlackFlow-Catalog",
                "Accept": "application/vnd.github+json",
            },
        )
        with self.opener.open(request, timeout=30) as response:
            data = response.read(100 * 1024 * 1024 + 1)
            if len(data) > 100 * 1024 * 1024:
                raise ValueError("Game data response exceeds the maximum size")
            return data

    def latest(self):
        url = (
            f"https://api.github.com/repos/{REPOSITORY}/commits?"
            + urllib.parse.urlencode({"path": SOURCE_PATH, "per_page": 1})
        )
        try:
            revisions = json.loads(self.fetch(url))
        except urllib.error.HTTPError as error:
            if error.code not in {403, 429}:
                raise
            # An API quota is not a reason to bypass Clash. The same proxied raw
            # source remains useful with a content version and no invented commit.
            source_url = (
                f"https://raw.githubusercontent.com/{REPOSITORY}/master/{SOURCE_PATH}"
            )
            raw = self.fetch(source_url)
            return raw, {
                "url": source_url,
                "revision": None,
                "source_committed_at": None,
                "content_version": "sha256:" + digest(raw),
                "revision_lookup_status": error.code,
                "retrieved_at": datetime.now(timezone.utc).isoformat(),
                "sha256": digest(raw),
                "transport": {"proxy": self.proxy, "direct_fallback": False},
                "game_version": None,
            }
        if not isinstance(revisions, list) or not revisions:
            raise ValueError("No game source revision was returned")
        revision = revisions[0]
        sha = revision.get("sha", "")
        if not re.fullmatch(r"[a-f0-9]{40}", sha):
            raise ValueError("Invalid game source revision")
        source_url = (
            f"https://raw.githubusercontent.com/{REPOSITORY}/{sha}/{SOURCE_PATH}"
        )
        raw = self.fetch(source_url)
        return raw, {
            "url": source_url,
            "revision": sha,
            "source_committed_at": revision["commit"]["committer"]["date"],
            "content_version": "sha256:" + digest(raw),
            "retrieved_at": datetime.now(timezone.utc).isoformat(),
            "sha256": digest(raw),
            "transport": {"proxy": self.proxy, "direct_fallback": False},
            "game_version": None,
        }


def synchronize(output, client=None):
    """Preserve the last validated catalog on any failed refresh."""
    output = Path(output)
    previous = None
    if output.exists():
        try:
            previous = json.loads(output.read_text(encoding="utf-8"))
            validate_catalog(previous)
        except (OSError, ValueError, KeyError, TypeError):
            # A damaged local file is not a fallback, but a successful source
            # refresh may replace it with a fully validated catalog.
            previous = None
    try:
        raw, source = (client or ClashSource()).latest()
        if digest(raw) != source["sha256"]:
            raise ValueError("Downloaded source digest mismatch")
        catalog = build_catalog(json.loads(raw), source)
        atomic_json(output, catalog)
    except (OSError, ValueError, KeyError, TypeError, urllib.error.URLError) as error:
        if previous is None:
            raise
        return {
            "status": "cached",
            "error": str(error),
            "source": previous["source"],
            "coverage": previous["coverage"],
            "direct_fallback": False,
        }
    return {
        "status": "updated",
        "changed": previous is None or previous["source"]["sha256"] != source["sha256"],
        "source": source,
        "coverage": catalog["coverage"],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--proxy", default=DEFAULT_PROXY)
    parser.add_argument(
        "--input", type=Path, help="Offline fixture; no network requests"
    )
    args = parser.parse_args()
    if args.input:
        raw = args.input.read_bytes()
        source = {
            "url": None,
            "local_path": str(args.input.resolve()),
            "revision": None,
            "source_committed_at": None,
            "retrieved_at": datetime.now(timezone.utc).isoformat(),
            "sha256": digest(raw),
            "transport": {"mode": "offline"},
            "game_version": None,
        }
        catalog = build_catalog(json.loads(raw), source)
        atomic_json(args.output, catalog)
        result = {
            "status": "offline",
            "source": source,
            "coverage": catalog["coverage"],
        }
    else:
        result = synchronize(args.output, ClashSource(args.proxy))
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
