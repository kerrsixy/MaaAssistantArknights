"""Export verified level waypoints and spawn associations for battle reviews.

Waypoints constrain an enemy's route; straight lines between them are not an
engine pathfinding simulation. Branch origins and runtime changes need live proof.
"""

import argparse
import hashlib
import json
from pathlib import Path


def export(level_path, tile_path, handbook_path):
    level = json.loads(level_path.read_text(encoding="utf-8"))
    tile = json.loads(tile_path.read_text(encoding="utf-8"))
    handbook = json.loads(handbook_path.read_text(encoding="utf-8"))["enemyData"]
    grid = level["mapData"]["map"]
    height = len(grid)
    width = len(grid[0])
    if len(tile["tiles"]) != height or any(len(row) != width for row in tile["tiles"]):
        raise ValueError("Level and native tile dimensions disagree")
    for y, row in enumerate(grid):
        for x, index in enumerate(row):
            if (
                level["mapData"]["tiles"][index]["tileKey"]
                != tile["tiles"][y][x]["tileKey"]
            ):
                raise ValueError(f"Level and native tile differ at {(x, y)}")

    def point(position):
        return [position["col"], height - 1 - position["row"]]

    def export_routes(source):
        return [
            {
                "index": index,
                "start": point(route["startPosition"]),
                "end": point(route["endPosition"]),
                "raw_start": route["startPosition"],
                "raw_end": route["endPosition"],
                "checkpoints": [
                    {
                        **check,
                        "raw_position": check["position"],
                        "position": point(check["position"]),
                    }
                    for check in route.get("checkpoints") or []
                ],
                "allow_diagonal": route["allowDiagonalMove"],
                "motion_mode": route["motionMode"],
                "spawn_offset": route.get("spawnOffset"),
                "spawn_random_range": route.get("spawnRandomRange"),
                "visit_every_tile_center": route.get("visitEveryTileCenter"),
                "visit_every_checkpoint": route.get("visitEveryCheckPoint"),
            }
            for index, route in enumerate(source)
        ]

    routes = export_routes(level["routes"])
    extra_routes = export_routes(level.get("extraRoutes") or [])
    spawns = []

    def visit(value, context, route_set):
        if isinstance(value, dict):
            if (
                value.get("actionType") in ["SPAWN", 0]
                and "routeIndex" in value
                and value.get("key", "").startswith("enemy_")
            ):
                enemy = value["key"]
                spawns.append(
                    {
                        "context": context,
                        "enemy_id": enemy,
                        "enemy_name": handbook.get(enemy, {}).get("name", enemy),
                        "count": value["count"],
                        "route_index": value["routeIndex"],
                        "route_set": route_set,
                        "pre_delay": value["preDelay"],
                        "interval": value["interval"],
                        "hidden_group": value.get("hiddenGroup"),
                        "random_group": value.get("randomSpawnGroupKey"),
                        "random_weight": value.get("weight"),
                        "timing_origin": "local_action_context",
                    }
                )
            for key, child in value.items():
                visit(child, context + "/" + key, route_set)
        elif isinstance(value, list):
            for index, child in enumerate(value):
                visit(child, context + "/" + str(index), route_set)

    visit(level.get("waves", []), "waves", "routes")
    visit(level.get("branches", {}), "branches", "extra_routes")
    for spawn in spawns:
        referenced = routes if spawn["route_set"] == "routes" else extra_routes
        if not 0 <= spawn["route_index"] < len(referenced):
            raise ValueError(f"Spawn references nonexistent route: {spawn}")

    facility_branches = []
    for section in ("predefines", "hardPredefines"):
        for collection in ("tokenInsts", "characterInsts"):
            for instance in (level.get(section) or {}).get(collection) or []:
                for override in instance.get("overrideSkillBlackboard") or []:
                    if not override["key"].endswith("@branch_id"):
                        continue
                    branch = override.get("valueStr")
                    facility_branches.append(
                        {
                            "context": section + "/" + collection,
                            "character_id": instance["inst"]["characterKey"],
                            "alias": instance.get("alias"),
                            "position": point(instance["position"]),
                            "raw_position": instance["position"],
                            "level": instance["inst"]["level"],
                            "hidden": instance.get("hidden"),
                            "branch_id": branch,
                            "branch_exists": branch in (level.get("branches") or {}),
                            "trigger_condition": "requires mechanism/runtime evidence",
                        }
                    )
    return {
        "stage": tile["name"],
        "level_id": level.get("levelId"),
        "dimensions": [width, height],
        "tile_keys_checked": width * height,
        "coordinate_system": "native top-left x,y; converted from level col,row via y=height-1-row",
        "source_level": str(level_path.resolve()),
        "source_sha256": hashlib.sha256(level_path.read_bytes()).hexdigest(),
        "routes": routes,
        "extra_routes": extra_routes,
        "spawns": spawns,
        "facility_branches": facility_branches,
        "limitations": [
            "Waypoints are not every walked tile",
            "Runtime teleport/forced movement changes observed paths",
            "Branch spawn origins may be relative to a runtime object",
            "Branch actions reference extra_routes, not the main routes array",
            "Raw sentinel coordinates and offsets need runtime interpretation",
            "Facility branch references do not prove activation or repetition",
            "Action delays are not absolute battle timestamps",
            "Hidden/random groups are alternatives, not a combined fixed roster",
        ],
        "live_verified": False,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--level", type=Path, required=True)
    parser.add_argument("--tiles", type=Path, required=True)
    parser.add_argument("--handbook", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = export(args.level, args.tiles, args.handbook)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(
        json.dumps(
            {
                "stage": result["stage"],
                "verified_tiles": result["tile_keys_checked"],
                "routes": len(result["routes"]),
                "extra_routes": len(result["extra_routes"]),
                "spawn_actions": len(result["spawns"]),
            },
            ensure_ascii=False,
        )
    )
