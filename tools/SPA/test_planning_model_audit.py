import copy
import unittest

from audit_planning_model import build_facts


def fixture():
    season = {
        "modeDataDict": {"mode_single_7": {"difficultyDesc": "+6", "factor": 1.3}},
        "battleDataDict": {"mode_single_7": {"14": [{"bossId": "boss", "levelId": "path/template"}]}},
        "bossInfoDict": {"boss": {"bloodDataDict": {"mode_single_7": 851040}, "isHidingBoss": False}},
        "charShopChessDatas": {}, "charChessDataDict": {}, "trapShopChessDatas": {},
        "shopLevelDataDict": {"mode_single_7": {"2": {"initialUpgradePrice": 8}}},
        "shopCharChessInfoData": {}, "bondInfoDict": {}, "effectBuffInfoDataDict": {},
        "modeFactorInfo": {}, "difficultyFactorInfo": {}, "stageDatasDict": {},
        "specialEnemyRandomTypeDict": {}, "specialEnemyInfoDict": {},
        "bandDataListDict": {"band_mech": {"inactiveModeIdList": ["mode_single_funny"]}},
        "constData": {"shopRefreshPrice": 1, "maxDeckChessCnt": 10, "maxDeckEquipmentChessCnt": 5,
                      "maxBattleChessCnt": 8, "diyCharLimitForLevels": {"5": 2, "6": 2}},
    }
    activity = {"activity": {"AUTOCHESS": {"act3autochess": {"V3_1": season}}},
                "autoChessData": {"randomEnemyAttributeDict": {}, "constData": {
                    "enemyMaxHpFactor": 1, "enemyAtkFactor": 5, "enemyDefFactor": 3,
                    "enemyMagicResistanceFactor": 3}}}
    bosses = {"bosses": [{"boss_id": "boss", "family_name": "Example", "hp_by_mode": {"mode_single_7": 851040},
                          "hidden": False, "enemy_id": "enemy", "level_id": "path/template",
                          "rounds_by_mode": {"mode_single_7": [14]}, "template_home": {"col": 0, "row": 1},
                          "route": {}, "stationary_route": True, "time_limit_seconds": 120,
                          "enemy_database_level": 0, "enemy_base": {"attributes": {
                              "atk": {"m_defined": False, "m_value": 0}}}}]}
    return activity, bosses


class PlanningModelAuditTests(unittest.TestCase):
    def test_display_difficulty_and_factor_are_not_fabricated_stat_scaling(self):
        result = build_facts(*fixture(), {}, {})
        self.assertEqual(result["difficulty"]["modes"][0]["value"], {"difficultyDesc": "+6", "factor": 1.3})
        self.assertEqual(result["difficulty"]["runtime_multiplier_formula"]["status"], "unknown")
        self.assertFalse(result["runtime_validation_performed"])

    def test_explicit_boss_hp_mismatch_is_rejected(self):
        activity, bosses = fixture()
        bosses["bosses"][0]["hp_by_mode"]["mode_single_7"] = 600000
        with self.assertRaises(ValueError):
            build_facts(activity, bosses, {}, {})

    def test_multiplayer_template_is_not_claimed_as_validated_solo_route(self):
        activity, bosses = fixture()
        season = activity["activity"]["AUTOCHESS"]["act3autochess"]["V3_1"]
        season["battleDataDict"]["mode_multi_normal"] = copy.deepcopy(season["battleDataDict"]["mode_single_7"])
        with self.assertRaises(ValueError):
            build_facts(activity, bosses, {}, {})

    def test_shop_probabilities_and_map_selection_remain_unknown(self):
        result = build_facts(*fixture(), {}, {})
        self.assertEqual(result["shop"]["tier_and_card_draw_probabilities"]["status"], "unknown")
        self.assertEqual(result["maps_and_waves"]["fixed_map_does_not_prove_fixed_enemies"]["status"], "unknown")
        self.assertEqual(result["bosses"][0]["base_attributes"]["value"]["atk"], {"m_defined": False, "m_value": 0})

    def test_every_fact_pointer_resolves_to_the_unmodified_source_value(self):
        activity, bosses = fixture()
        sources = {"activity": activity, "boss_catalogue": bosses}
        result = build_facts(activity, bosses, {}, {})

        def inspect(value):
            if isinstance(value, dict):
                if {"source", "json_pointer", "value"}.issubset(value):
                    original = sources[value["source"]]
                    for key in value["json_pointer"].split("/")[1:]:
                        original = original[int(key)] if isinstance(original, list) else original[key]
                    self.assertEqual(value["value"], original, value["json_pointer"])
                for item in value.values():
                    inspect(item)
            elif isinstance(value, list):
                for item in value:
                    inspect(item)
        inspect(result)


if __name__ == "__main__":
    unittest.main()
