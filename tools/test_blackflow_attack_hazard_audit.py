import copy
import unittest

from blackflow_attack_hazard_audit import analyze


def fixture():
    return {
        "battle_key": "fixture-current",
        "attack": {
            "range_offsets": [[0, 0]],
            "deployment": [0, 0],
            "direction": "RIGHT",
            "area_effects": [{"radius": 0.8}],
        },
        "segments": [
            {
                "id": "edge-traverse",
                "kind": "confirmed_linear",
                "start": [-2, 0.2],
                "end": [2, 0.2],
                "source": {
                    "kind": "observed",
                    "uri": "fixture://clip",
                    "frame_span": [1, 2],
                },
            }
        ],
        "hazards": [
            {
                "id": "hazard",
                "battle_key": "fixture-current",
                "position": [1.2, 0.2],
                "classification": "reflective",
                "classification_status": "confirmed",
                "position_status": "confirmed_current",
                "source": {"uri": "fixture://label"},
            }
        ],
    }


class SafetyContracts(unittest.TestCase):
    def test_missing_source_cannot_claim_confirmed_reflective_contact(self):
        data = fixture()
        del data["hazards"][0]["source"]
        result = analyze(data)
        self.assertTrue(result["hazards"][0]["potential_geometric_contact"])
        self.assertFalse(
            result["hazards"][0]["confirmed_reflective_object_contact_in_model"]
        )
        self.assertTrue(
            any(
                x.get("reason") == "missing_hazard_provenance"
                for x in result["unknown_boundaries"]
            )
        )

    def test_unconfirmed_position_cannot_become_a_current_hazard(self):
        data = fixture()
        data["hazards"][0]["position_status"] = "last_seen"
        self.assertFalse(
            analyze(data)["hazards"][0]["confirmed_reflective_object_contact_in_model"]
        )

    def test_splash_outside_direct_range(self):
        result = analyze(fixture())["hazards"][0]
        self.assertFalse(result["directly_in_attack_cell_union"])
        self.assertTrue(result["effects"][0]["can_geometrically_intersect"])

    def test_continuous_boundary_not_only_cell_center(self):
        effect = analyze(fixture())["hazards"][0]["effects"][0]
        self.assertAlmostEqual(effect["minimum_distance"], 0.7)
        self.assertEqual(effect["witness"]["target_point"], [0.5, 0.2])

    def test_no_cross_battle_label_reuse_at_identical_coordinate(self):
        data = fixture()
        data["battle_key"] = "another-battle"
        result = analyze(data)
        self.assertEqual(result["hazards"], [])
        self.assertTrue(
            any(
                x.get("reason") == "classification_not_inherited_across_battles"
                for x in result["unknown_boundaries"]
            )
        )

    def test_current_unknown_is_not_confirmed_reflective(self):
        data = fixture()
        data["hazards"][0]["classification_status"] = "unknown"
        result = analyze(data)["hazards"][0]
        self.assertTrue(result["potential_geometric_contact"])
        self.assertFalse(result["confirmed_reflective_object_contact_in_model"])

    def test_disappear_does_not_create_shootable_line(self):
        data = fixture()
        data["segments"][0]["kind"] = "disappear"
        result = analyze(data)
        self.assertEqual(result["segments"], [])
        self.assertIsNone(result["hazards"][0]["effects"][0]["minimum_distance"])

    def test_unknown_curve_is_not_endpoint_chord(self):
        data = fixture()
        data["segments"][0]["kind"] = "unknown_curve"
        result = analyze(data)
        self.assertEqual(result["segments"], [])
        self.assertTrue(
            any(
                x.get("reason") == "unmodeled_path:unknown_curve"
                for x in result["unknown_boundaries"]
            )
        )

    def test_script_chord_preserves_provisional_and_source(self):
        data = fixture()
        data["segments"][0]["kind"] = "provisional_chord"
        data["segments"][0]["source"] = {
            "uri": "fixture://raw-level",
            "sha256": "fixture-digest",
            "json_pointer": "/extraRoutes/3/checkpoints/2",
        }
        result = analyze(data)
        self.assertEqual(result["segments"][0]["source"], data["segments"][0]["source"])
        self.assertEqual(
            result["hazards"][0]["effects"][0]["witness"]["segment_kind"],
            "provisional_chord",
        )
        self.assertTrue(
            any(
                x.get("reason") == "script_chord_is_not_engine_path"
                for x in result["unknown_boundaries"]
            )
        )

    def test_overlapping_cells_do_not_double_count_coverage(self):
        data = fixture()
        data["attack"]["range_offsets"] = [[0, 0], [0, 0], [1, 0]]
        result = analyze(data)
        self.assertEqual(len(result["coverage_cells"]), 2)
        self.assertAlmostEqual(result["segments"][0]["covered_length"], 2.0)

    def test_attacker_centered_effect_requires_trigger_evidence(self):
        data = fixture()
        data["attack"]["area_effects"] = [{"origin": "deployment", "radius": 1.5}]
        data["segments"] = []
        effect = analyze(data)["hazards"][0]["effects"][0]
        self.assertTrue(effect["can_geometrically_intersect"])
        self.assertFalse(effect["witness"]["activation_confirmed"])

    def test_invalid_numeric_input_is_rejected(self):
        data = fixture()
        data["attack"]["area_effects"][0]["radius"] = float("nan")
        with self.assertRaises(ValueError):
            analyze(data)

    def test_no_device_or_pass_claim(self):
        result = analyze(copy.deepcopy(fixture()))
        self.assertTrue(result["offline_metadata_only"])
        self.assertFalse(result["live_observation"])
        self.assertFalse(result["livepass"])


if __name__ == "__main__":
    unittest.main()
