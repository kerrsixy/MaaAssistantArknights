import copy
from decimal import Decimal
import unittest

from audit_pact_scaling import blackboard_values, evaluate_contribution, extract_model, linear_contribution


def sample_model(pact):
    formulas = {
        "egirShip": [("max_hp_bonus_fraction", .35, .01)],
        "preciShip": [("attack_bonus_fraction", .1, .012)],
        "steadShip": [("max_hp_bonus_fraction", .25, .012), ("retaliation_arts_pre_mitigation", 850, 10)],
    }
    return {
        "pact_id": pact, "activation_distinct_members": 3 if pact == "egirShip" else 2,
        "milestone_distinct_members": 6 if pact == "egirShip" else 3,
        "formulas": [{"attribute": attr, "base": base, "per_layer": layer} for attr, base, layer in formulas[pact]],
        "blackboard": {"power_def_penetrate": .3, "power_magic_resist_penetrate": .3,
                       "damage_resistance": .4, "cd_duration": .2, "damage_scale": 1.4, "weak[limit]": 5,
                       "damage_value": 5000, "bond_add_type": "by_charlevel", "max_free_respawn_cnt": 3},
    }


def evaluate(pact, layers=300, **kwargs):
    args = {"active": True, "distinct_members": 6 if pact == "egirShip" else 3,
            "target_member": True, "target_ranged": False}
    args.update(kwargs)
    return evaluate_contribution(sample_model(pact), layers=layers, **args)


class PactScalingTests(unittest.TestCase):
    def test_exact_decimal_coefficients(self):
        self.assertEqual(linear_contribution(.35, .01, 0), Decimal("0.35"))
        self.assertEqual(linear_contribution(.35, .01, 183), Decimal("2.18"))
        self.assertEqual(linear_contribution(.35, .01, 300), Decimal("3.35"))
        self.assertEqual(linear_contribution(.1, .012, 300), Decimal("3.7"))
        self.assertEqual(linear_contribution(.25, .012, 300), Decimal("3.85"))
        self.assertEqual(linear_contribution(850, 10, 300), Decimal("3850"))

    def test_unknown_and_invalid_layers_never_become_zero(self):
        self.assertEqual(evaluate("egirShip", layers=None)["status"], "unknown")
        for layers in (-1, 1.0, True, "300"):
            with self.assertRaises(ValueError):
                evaluate("egirShip", layers=layers)
        for coefficient in ("NaN", "Infinity", -1):
            with self.assertRaises(ValueError):
                linear_contribution(coefficient, .01, 300)

    def test_layers_do_not_prove_activation(self):
        self.assertEqual(evaluate("egirShip", active=None)["status"], "unknown")
        self.assertEqual(evaluate("egirShip", active=False)["status"], "inactive")
        self.assertEqual(evaluate("egirShip", distinct_members=None)["status"], "unknown")
        self.assertEqual(evaluate("egirShip", distinct_members=2)["status"], "contradictory")

    def test_egir_targets_members_only_and_physical_damage_is_not_a_kill(self):
        self.assertEqual(evaluate("egirShip", target_member=False)["status"], "not_recipient")
        self.assertEqual(evaluate("egirShip", target_member=None)["status"], "unknown")
        value = evaluate("egirShip")
        self.assertEqual(value["contributions"], {"max_hp_bonus_fraction": "3.35"})
        self.assertFalse(value["consumption_mechanics"]["successful_kill_guaranteed"])
        self.assertEqual(value["consumption_mechanics"]["declared_physical_damage"], 5000)
        self.assertEqual(value["consumption_mechanics"]["default_free_respawn_limit_if_milestone"], 3)
        self.assertEqual(evaluate("egirShip", distinct_members=3)["consumption_mechanics"][
            "default_free_respawn_limit_if_milestone"], 0)

    def test_precision_three_members_extends_to_ranged(self):
        self.assertEqual(evaluate("preciShip", distinct_members=2, target_member=False, target_ranged=True)[
            "status"], "not_recipient")
        value = evaluate("preciShip", target_member=False, target_ranged=True)
        self.assertEqual(value["contributions"], {"attack_bonus_fraction": "3.7"})
        self.assertEqual(value["declared_penetration"]["magic_resistance_fraction"], .3)
        self.assertEqual(evaluate("preciShip", target_member=False, target_ranged=False)["status"], "not_recipient")
        self.assertEqual(evaluate("preciShip", target_member=False, target_ranged=None)["status"], "unknown")
        self.assertNotIn("declared_penetration", evaluate("preciShip", distinct_members=2))

    def test_steadfast_hp_is_all_operators_but_retaliation_is_members(self):
        value = evaluate("steadShip", target_member=False, target_ranged=False)
        self.assertEqual(value["contributions"], {"max_hp_bonus_fraction": "3.85"})
        self.assertEqual(evaluate("steadShip")["contributions"]["retaliation_arts_pre_mitigation"], "3850")
        self.assertNotIn("retaliation_arts_pre_mitigation", evaluate("steadShip", distinct_members=2)["contributions"])
        self.assertNotIn("retaliation_arts_pre_mitigation", evaluate("steadShip", target_member=None)["contributions"])

    def test_no_combined_stats_damage_or_win_claim(self):
        for pact in ("egirShip", "preciShip", "steadShip"):
            value = evaluate(pact)
            self.assertIsNone(value["combined_panel_stats"])
            self.assertEqual(value["attribute_bucket_stacking"], "unknown")
            self.assertEqual(value["combat_verdict"], "unknown")
            self.assertFalse(value["runtime_verified"])
            self.assertEqual(value["observed_layers_used_once"], 300)

    def test_duplicate_and_multiple_handlers_rejected(self):
        one = {"key": "env_gbuff_new", "blackboard": [{"key": "atk", "value": .1, "valueStr": None}]}
        self.assertEqual(blackboard_values([one]), {"atk": .1})
        with self.assertRaises(ValueError):
            blackboard_values([one, one])
        duplicate = copy.deepcopy(one)
        duplicate["blackboard"].append(duplicate["blackboard"][0])
        with self.assertRaises(ValueError):
            blackboard_values([duplicate])

    def test_catalogue_text_and_parameter_binding_crosscheck(self):
        effect = "bondeffect_egir"
        pact = {"name": "阿戈尔", "effectId": effect, "activeCount": 3}
        metadata = {"effectDesc": "【阿戈尔】干员生命值+{0:0%}"}
        parameters = {"descParamInfos": [{"descParamBase": "base_max_hp", "descParamPerStack": "max_hp_per_stack",
                                           "descParamMax": 0}], "phaseType": "PEOPLE_COUNT"}
        board = {"key": "act1autochess_bond_eff_egir", "base_max_hp": .35, "max_hp_per_stack": .01,
                 "power_bond_char_cnt": 6}
        buffs = [{"key": "env_gbuff_new", "blackboard": [
            {"key": key, "value": value if not isinstance(value, str) else 0,
             "valueStr": value if isinstance(value, str) else None} for key, value in board.items()]}]
        season = {"bondInfoDict": {"egirShip": pact}, "effectInfoDataDict": {effect: metadata},
                  "effectDescParamDataDict": {effect: parameters}, "effectBuffInfoDataDict": {effect: buffs}}
        catalogue = {"pacts": {"egirShip": copy.deepcopy(pact)}, "effects": {
            effect: {"metadata": copy.deepcopy(metadata), "parameters": copy.deepcopy(parameters),
                     "buffs": copy.deepcopy(buffs)}}}
        model = extract_model(season, catalogue, "egirShip")
        self.assertTrue(all(model["catalogue_crosscheck"].values()))
        self.assertEqual(model["formulas"][0]["raw_desc_param_max"], 0)
        self.assertIn("unknown", model["formulas"][0]["cap_semantics"])
        broken = copy.deepcopy(catalogue)
        broken["effects"][effect]["metadata"]["effectDesc"] = "wrong"
        with self.assertRaises(ValueError):
            extract_model(season, broken, "egirShip")
        season["effectDescParamDataDict"][effect]["descParamInfos"][0]["descParamPerStack"] = "wrong"
        catalogue["effects"][effect]["parameters"] = copy.deepcopy(season["effectDescParamDataDict"][effect])
        with self.assertRaises(ValueError):
            extract_model(season, catalogue, "egirShip")


if __name__ == "__main__":
    unittest.main()
