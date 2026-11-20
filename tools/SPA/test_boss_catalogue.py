import copy
import json
from pathlib import Path
import unittest

from generate_boss_catalogue import movement_waypoints, route_record


class BossCatalogueTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(__file__).resolve().parents[2] / 'resource/config/MiniGame/SPABossData.json'
        cls.data = json.loads(path.read_text(encoding='utf-8'))
        cls.bosses = {row['boss_id']: row for row in cls.data['bosses']}

    def test_every_available_solo_boss_has_anchored_route_and_real_hp(self):
        self.assertEqual(len(self.bosses), 16)
        self.assertEqual(sum(row['hidden'] for row in self.bosses.values()), 6)
        for row in self.bosses.values():
            self.assertEqual(row['template_home'], {'col': 0, 'row': 1})
            self.assertGreater(row['hp_by_mode']['mode_single_14'], 0)
            self.assertTrue(row['abilities'])
            self.assertEqual(row['rounds_by_mode']['mode_single_14'], [15] if row['hidden'] else [14])

    def test_normal_and_hidden_pipe_are_different_target_cells(self):
        normal, hidden = self.bosses['boss_3'], self.bosses['boss_10']
        self.assertEqual(normal['family_name'], hidden['family_name'])
        self.assertEqual(normal['route']['startPosition'], {'col': 8, 'row': 2})
        self.assertEqual(hidden['route']['startPosition'], {'col': 7, 'row': 2})
        self.assertEqual(normal['hp_by_mode']['mode_single_14'], 2520000)
        self.assertTrue(normal['stationary_route'])
        self.assertFalse(self.bosses['boss_13']['stationary_route'])

    def test_short_wait_or_moving_checkpoint_does_not_prove_stationary_route(self):
        level = {'mapData': {'map': [[0]], 'tiles': [{'tileKey': 'tile_end'}]},
                 'waves': [{'fragments': [{'actions': [{'actionType': 'SPAWN', 'key': 'boss',
                                                       'routeIndex': 0, 'count': 1}]}]}],
                 'routes': [{'startPosition': {'col': 8, 'row': 2}, 'endPosition': {'col': 8, 'row': 2},
                             'checkpoints': [{'type': 'WAIT_FOR_SECONDS', 'time': 999}],
                             'spawnRandomRange': {'x': 0, 'y': 0}}],
                 'options': {'maxPlayTime': 120}}
        self.assertTrue(route_record(level, 'boss')['stationary_route'])
        short = copy.deepcopy(level)
        short['routes'][0]['checkpoints'][0]['time'] = 10
        self.assertFalse(route_record(short, 'boss')['stationary_route'])
        short['routes'][0]['checkpoints'][0]['type'] = 'PATROL_MOVE'
        self.assertFalse(route_record(short, 'boss')['stationary_route'])
        short['routes'][0]['spawnRandomRange']['x'] = 1
        with self.assertRaises(ValueError):
            route_record(short, 'boss')

    def test_patrol_uses_ordered_checkpoints_excluding_the_default_home_endpoint(self):
        mobile = self.bosses['boss_13']
        self.assertEqual(mobile['route']['endPosition'], mobile['template_home'])
        self.assertEqual(movement_waypoints(mobile), [
            {'col': 7, 'row': 4}, {'col': 1, 'row': 1}, {'col': 7, 'row': 2},
        ])
        self.assertEqual(movement_waypoints(self.bosses['boss_3']), [])


if __name__ == '__main__':
    unittest.main()
