import json
import unittest
from pathlib import Path

from generate_combat_catalogue import interpolate_stats, cpp_mode_header


class SeasonCombatCatalogueTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[2]
        cls.data = json.loads((root / 'resource/config/MiniGame/SPACombatData.json').read_text(encoding='utf-8'))
        cls.rows = {row['name']: row for row in cls.data['operators']}

    def test_live_istina_panel_matches_separate_potential(self):
        row = self.rows['真理']
        initial = row['initial']
        self.assertEqual(initial['status']['evolvePhase'], 'PHASE_1')
        self.assertEqual(initial['status']['charLevel'], 60)
        # R7 actual panel: HP 991, ATK 436, DEF 84, block 1. Account
        # potential +23 explains attack; it is not folded into the base.
        self.assertEqual(initial['stats_base']['maxHp'], 991)
        self.assertEqual(initial['stats_base']['atk'], 413)
        self.assertEqual(initial['stats_base']['def'], 84)
        self.assertEqual(initial['stats_base']['blockCnt'], 1)
        attack_bonus = sum(mod['value'] for rank in row['potential_ranks']
            if rank.get('buff') for mod in rank['buff']['attributes']['attributeModifiers']
            if mod['attributeType'] in (1, 'ATK'))
        self.assertEqual(initial['stats_base']['atk'] + attack_bonus, 436)

    def test_blocking_changes_with_the_actual_promotion_status(self):
        self.assertEqual(self.rows['拜松']['initial']['stats_base']['blockCnt'], 3)
        self.assertEqual(self.rows['幽灵鲨']['initial']['stats_base']['blockCnt'], 2)
        self.assertEqual(self.rows['幽灵鲨']['elite']['stats_base']['blockCnt'], 3)

    def test_mode_restrictions_preclude_the_wrong_egir_experiment(self):
        for difficulty in range(1, 21):
            row = self.data['modes'][f'mode_single_{difficulty}']
            self.assertEqual('egirShip' in row['inactiveBondIdList'], difficulty <= 4)
            self.assertEqual('egirShip' in row['activeBondIdList'], difficulty >= 5)
        self.assertIn('egirShip', self.data['modes']['mode_single_funny']['inactiveBondIdList'])
        self.assertIn('egirShip', self.data['modes']['mode_multi_funny']['inactiveBondIdList'])

    def test_all_public_mode_pacts_have_unique_known_meanings(self):
        public = {key: row for key, row in self.data['modes'].items()
                  if key.startswith(('mode_single_', 'mode_multi_'))}
        self.assertEqual(len(public), 25)
        for key, row in public.items():
            active, inactive = set(row['activeBondIdList']), set(row['inactiveBondIdList'])
            self.assertFalse(active & inactive, key)
            self.assertEqual(active | inactive, self.data['pacts'].keys(), key)
        header = cpp_mode_header(self.data)
        self.assertIn('SeasonModePactCatalogue', header)
        self.assertNotIn('"mode_training_2"', header)
        self.assertEqual(self.rows['星极']['promotion_copies'], 2)
        self.assertEqual(self.rows['幽灵鲨']['promotion_copies'], 3)

    def test_every_field_operator_has_two_resolved_states(self):
        self.assertEqual(len(self.rows), 143)
        self.assertEqual(sum(not row['hidden_in_shop'] for row in self.rows.values()), 141)
        for row in self.rows.values():
            for name in ('initial', 'elite'):
                form = row[name]
                self.assertGreater(form['stats_base']['maxHp'], 0)
                self.assertEqual(form['stats_base']['attackSpeed'], 100)
                self.assertTrue(form['range_grid'])
                self.assertTrue(any(s['index'] == row['default_skill_index'] for s in form['skills']))
                self.assertTrue(form['garrisons'])
        self.assertTrue(self.rows['盟约·辅助干员']['hidden_in_shop'])
        robot = self.rows['“暴躁铁皮”']
        self.assertTrue(robot['hidden_in_shop'])
        self.assertEqual(robot['initial']['equipment_slots'], 4)
        self.assertEqual(robot['elite']['equipment_slots'], 6)
        self.assertIn('account_cultivation', self.data['baseline_excludes'])

    def test_default_skill_targeting_and_attack_range_are_not_interchangeable(self):
        for form in ['initial', 'elite']:
            for row in self.rows.values():
                default = next(skill for skill in row[form]['skills'] if skill['index'] == row['default_skill_index'])
                self.assertIn('declared_range_grid', default)
                self.assertGreaterEqual(default['data']['spData']['spCost'], 0)
            anchor = next(skill for skill in self.rows['乌尔比安'][form]['skills'] if skill['index'] == 2)
            self.assertTrue(anchor['self_movement'])
            self.assertFalse(anchor['attack_range_replacement'])
            self.assertEqual(max(cell['col'] for cell in anchor['declared_range_grid']), 6)
            clem = next(skill for skill in self.rows['克莱门莎'][form]['skills'] if skill['index'] == 1)
            self.assertTrue(clem['attack_range_replacement'])
            self.assertFalse(clem['self_movement'])
            self.assertEqual({cell['row'] for cell in clem['declared_range_grid']}, {0})
            self.assertEqual(max(cell['col'] for cell in clem['declared_range_grid']), 4)

    def test_invalid_training_is_not_silently_clamped(self):
        character = dict(phases=[dict(maxLevel=10, attributesKeyFrames=[dict(level=1,data={})])])
        with self.assertRaises(ValueError):
            interpolate_stats(character, dict(evolvePhase='PHASE_0', charLevel=11))
        with self.assertRaises(ValueError):
            interpolate_stats(character, dict(evolvePhase='PHASE_2', charLevel=1))

    def test_equipment_pact_hint_is_not_a_granted_operator_pact(self):
        blade = next(e for e in self.data['equipment'] if e['name'] == '阿戈尔重刃' and not e['isGolden'])
        self.assertEqual(blade['giveBondId'], 'egirShip')
        self.assertFalse(blade['canGiveBond'])
        transform = next(e for e in self.data['equipment'] if e['name'] == '变形同构体')
        self.assertTrue(transform['canGiveBond'])
        self.assertIsNone(transform['giveBondId'])

    def test_current_season_effects_and_strategy_requirements_are_linked(self):
        self.assertEqual(len(self.data['strategies']), 61)
        self.assertEqual(len(self.data['pacts']), 26)
        for strategy in self.data['strategies']:
            self.assertIn(strategy['effectId'], self.data['effects'])
            self.assertEqual(strategy['victorCount'], 3)
        weakness = next(s for s in self.data['strategies'] if s['bandId'] == 'band_weakness')
        buffs = self.data['effects'][weakness['effectId']]['buffs']
        weakness_attributes = {b['key']: b['value'] for effect in buffs for b in effect['blackboard']}
        self.assertEqual(weakness_attributes['atk'], .16)
        self.assertEqual(weakness_attributes['attack_speed'], 16)
        self.assertEqual(weakness_attributes['filter_autochess_golden'], 1)
        base = self.data['effects']['bondeffect_egir']
        values = {b['key']: b['value'] for effect in base['buffs'] for b in effect['blackboard']}
        self.assertEqual(values['damage_value'], 5000)
        self.assertEqual(values['power_bond_char_cnt'], 6)
        self.assertEqual(values['max_free_respawn_cnt'], 3)
        switched = self.data['effects']['bondeffect_egir_2']
        self.assertIn('整备区最右侧', switched['metadata']['effectDesc'])


if __name__ == '__main__':
    unittest.main()
