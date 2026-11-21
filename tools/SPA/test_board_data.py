import copy
import json
from pathlib import Path
import tempfile
import unittest

from generate_board_data import board_record


class BoardDeviceTests(unittest.TestCase):
    def record(self, level):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "level_act3autochess_m04"
            root.mkdir()
            path = root / "level_act3autochess_m04_sub_01.json"
            path.write_text(json.dumps(level), encoding="utf-8")
            return board_record(path)

    def sample(self):
        return json.loads((Path(__file__).parent / "fixtures/board_devices_m04.json").read_text(encoding="utf-8"))

    def test_visible_crates_override_buildable_road_and_fence(self):
        result = self.record(self.sample())
        self.assertEqual(result["rows"][1][3:5], "XX")
        self.assertEqual(result["rows"][1][8], "X")
        self.assertEqual(len(result["devices"]), 3)

    def test_hidden_crate_does_not_block_road(self):
        level = self.sample()
        level["predefines"]["tokenInsts"][2]["hidden"] = True
        self.assertEqual(self.record(level)["rows"][1][8], "G")

    def test_shifted_home_anchor_preserves_player_grid_and_devices(self):
        level = self.sample()
        before = self.record(level)
        level["mapData"]["map"] = [copy.deepcopy(level["mapData"]["map"][0])] * 6 + level["mapData"]["map"]
        after = self.record(level)
        self.assertEqual(after["source_row_origin"], before["source_row_origin"] + 6)
        self.assertEqual(after["rows"], before["rows"])
        self.assertEqual(after["devices"], before["devices"])

    def test_platform_is_ranged_only_and_off_board_objects_do_not_enter_grid(self):
        level = self.sample()
        platform = copy.deepcopy(level["predefines"]["tokenInsts"][2])
        platform["inst"]["characterKey"] = "trap_1106_achplat"
        level["predefines"]["tokenInsts"][2] = platform
        outside = copy.deepcopy(platform)
        outside["position"]["col"] = 12
        level["hardPredefines"] = {"tokenInsts": [outside]}
        result = self.record(level)
        self.assertEqual(result["rows"][1][8], "R")
        self.assertEqual(len(result["devices"]), 3)


if __name__ == "__main__":
    unittest.main()
