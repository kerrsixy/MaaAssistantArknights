"""Regress branch-route ownership without simulating battle movement."""

import copy
import json
import tempfile
import unittest
from pathlib import Path

from blackflow_stage_routes import export


class BranchRouteTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.route = {
            "startPosition": {"col": 0, "row": 0},
            "endPosition": {"col": 1, "row": 1},
            "allowDiagonalMove": True,
            "motionMode": "WALK",
            "checkpoints": [],
        }
        self.spawn = {
            "actionType": "SPAWN",
            "key": "enemy_test",
            "count": 1,
            "routeIndex": 0,
            "preDelay": 0,
            "interval": 1,
        }
        extra = copy.deepcopy(self.route)
        extra["startPosition"] = {"col": 1, "row": 0}
        self.level = {
            "mapData": {"map": [[0, 0], [0, 0]], "tiles": [{"tileKey": "tile_road"}]},
            "routes": [self.route],
            "extraRoutes": [extra],
            "waves": [{"actions": [self.spawn]}],
            "branches": {"transport": {"actions": [copy.deepcopy(self.spawn)]}},
            "predefines": {
                "tokenInsts": [
                    {
                        "position": {"col": 0, "row": 0},
                        "inst": {"characterKey": "trap_test", "level": 2},
                        "overrideSkillBlackboard": [
                            {
                                "key": "talent@branch_id",
                                "valueStr": "transport",
                            }
                        ],
                    }
                ],
            },
        }
        self.tiles = {"name": "fixture", "tiles": [[{"tileKey": "tile_road"}] * 2] * 2}

    def run_export(self):
        paths = [
            self.root / name for name in ("level.json", "tiles.json", "handbook.json")
        ]
        for path, value in zip(paths, (self.level, self.tiles, {"enemyData": {}})):
            path.write_text(json.dumps(value), encoding="utf-8")
        return export(*paths)

    def test_same_index_resolves_different_route_arrays(self):
        result = self.run_export()
        wave, branch = result["spawns"]
        self.assertEqual(wave["route_set"], "routes")
        self.assertEqual(branch["route_set"], "extra_routes")
        self.assertEqual(result[wave["route_set"]][0]["start"], [0, 1])
        self.assertEqual(result[branch["route_set"]][0]["start"], [1, 1])
        facility = result["facility_branches"][0]
        self.assertEqual(facility["branch_id"], "transport")
        self.assertTrue(facility["branch_exists"])
        self.assertEqual(facility["position"], [0, 1])
        self.assertFalse(result["live_verified"])

    def test_branch_index_valid_only_in_extra_routes(self):
        self.level["routes"] = []
        self.level["waves"] = []
        self.assertEqual(len(self.run_export()["spawns"]), 1)

    def test_main_route_does_not_mask_missing_extra_route(self):
        self.level["extraRoutes"] = []
        with self.assertRaisesRegex(ValueError, "nonexistent route"):
            self.run_export()

    def test_dynamic_origin_preserves_raw_coordinates_and_offsets(self):
        raw = {"col": -1, "row": -1}
        self.level["extraRoutes"][0]["startPosition"] = raw
        self.level["extraRoutes"][0]["spawnOffset"] = {"x": 0.5, "y": -0.25}
        result = self.run_export()["extra_routes"][0]
        self.assertEqual(result["raw_start"], raw)
        self.assertEqual(result["spawn_offset"], {"x": 0.5, "y": -0.25})


if __name__ == "__main__":
    unittest.main()
