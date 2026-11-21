import unittest
from spa_relocation import relocation_options, relocation_verified


class RelocationTests(unittest.TestCase):
    def test_bottom_field_uses_verified_grid_relocation(self):
        data = {"map_id": "m10", "origin": [4, 4], "destination": [3, 4],
                "placement_units": [{"name": "烈夏", "x": 552, "y": 440,
                                     "field_pose_verified": True, "coordinate_frame": "collapsed"}]}
        options = relocation_options(data, "烈夏", [552, 440])
        self.assertTrue(options["relocate_field"])
        self.assertEqual(options["reorient_col"], 3)
        self.assertEqual(options["origin_row"], 4)
        data["placement_units"][0]["coordinate_frame"] = "expanded"
        with self.assertRaises(ValueError):
            relocation_options(data, "烈夏", [552, 440])

    def test_source_must_match_unique_verified_entity(self):
        data = {"map_id": "m10", "origin": [4, 4], "destination": [3, 4],
                "placement_units": [{"name": "烈夏", "x": 552, "y": 440,
                                     "field_pose_verified": True, "coordinate_frame": "collapsed"}]}
        with self.assertRaises(ValueError):
            relocation_options(data, "烈夏", [552, 371])
        data["placement_units"] *= 2
        with self.assertRaises(ValueError):
            relocation_options(data, "烈夏", [552, 440])

    def test_cancelled_move_is_not_success(self):
        receipt = {"relocation_applied": True, "direction_input_committed": True,
                   "population_preserved": True, "funds_preserved": True,
                   "source_identity_and_field_control_verified": True, "field_coordinates_registered": 4}
        self.assertTrue(relocation_verified("SPAFieldRelocationVerified", receipt))
        receipt["relocation_applied"] = False
        self.assertFalse(relocation_verified("SPAFieldPoseRestored", receipt))


if __name__ == "__main__":
    unittest.main()
