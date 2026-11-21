"""Synthetic offline observation replays; these do not validate a game clear."""

from dataclasses import replace
import unittest

from blackflow_wang_piece_planner import (
    CellObservation,
    Effect,
    Enemy,
    PieceLedger,
    Placement,
    RunState,
    coverage,
    review_placement,
    s2_effect,
)


def effect(origin, skill=2, triggers=None, damage=None):
    return Effect(
        origin,
        skill,
        frozenset(triggers or {origin}),
        frozenset(damage or {origin}),
        "synthetic observed footprint",
    )


class PieceLedgerReplayTests(unittest.TestCase):
    def test_simultaneous_same_name_pieces_keep_separate_instances(self):
        ledger = PieceLedger()
        ledger.observe(
            1,
            (
                CellObservation((1, 1), "piece"),
                CellObservation((2, 1), "piece"),
            ),
        )
        self.assertEqual(len(ledger.cells), 2)
        self.assertNotEqual(
            ledger.cells[(1, 1)].piece_instance, ledger.cells[(2, 1)].piece_instance
        )

    def test_partial_frame_does_not_delete_unscanned_piece(self):
        ledger = PieceLedger()
        ledger.observe(1, (CellObservation((1, 1), "piece"),))
        identity = ledger.cells[(1, 1)].piece_instance
        ledger.observe(2, (CellObservation((8, 1), "empty"),))
        self.assertEqual(ledger.cells[(1, 1)].piece_instance, identity)
        self.assertEqual(ledger.cells[(1, 1)].observed_at, 1)

    def test_occlusion_keeps_identity_but_does_not_keep_active_coverage(self):
        ledger = PieceLedger()
        ledger.observe(1, (CellObservation((1, 1), "piece", True, effect((1, 1))),))
        identity = ledger.cells[(1, 1)].piece_instance
        ledger.observe(2, (CellObservation((1, 1), "unknown"),))
        state = ledger.cells[(1, 1)]
        self.assertEqual(state.piece_instance, identity)
        self.assertIsNone(state.observation.effect)
        self.assertIsNone(state.observation.active)
        ledger.observe(3, (CellObservation((1, 1), "piece"),))
        self.assertEqual(ledger.cells[(1, 1)].piece_instance, identity)

    def test_explicit_empty_then_reappearance_starts_new_instance(self):
        ledger = PieceLedger()
        ledger.observe(1, (CellObservation((1, 1), "piece"),))
        identity = ledger.cells[(1, 1)].piece_instance
        ledger.observe(2, (CellObservation((1, 1), "empty"),))
        self.assertIsNone(ledger.cells[(1, 1)].piece_instance)
        ledger.observe(3, (CellObservation((1, 1), "piece"),))
        self.assertNotEqual(ledger.cells[(1, 1)].piece_instance, identity)

    def test_out_of_order_and_conflicting_frames_do_not_mutate_ledger(self):
        ledger = PieceLedger()
        ledger.observe(2, (CellObservation((1, 1), "piece"),))
        previous = dict(ledger.cells)
        for at, observations in (
            (1, (CellObservation((1, 1), "empty"),)),
            (
                3,
                (
                    CellObservation((1, 1), "piece"),
                    CellObservation((1, 1), "empty"),
                ),
            ),
        ):
            with self.subTest(at=at), self.assertRaises(ValueError):
                ledger.observe(at, observations)
            self.assertEqual(ledger.cells, previous)
            self.assertEqual(ledger.observed_at, 2)

    def test_elapsed_time_does_not_invent_piece_expiration(self):
        ledger = PieceLedger()
        ledger.observe(1, (CellObservation((1, 1), "piece"),))
        ledger.observe(1000, ())
        self.assertEqual(ledger.cells[(1, 1)].observation.occupancy, "piece")


class PlacementReplayTests(unittest.TestCase):
    def setUp(self):
        self.ledger = PieceLedger()
        self.anchor = (1, 1)
        self.destination = (2, 1)
        self.link = s2_effect(self.anchor, frozenset({"horizontal"}), "test S2 axis")
        self.ledger.observe(
            10,
            (
                CellObservation(self.anchor, "piece", False, self.link),
                CellObservation(self.destination, "empty"),
            ),
        )
        self.run = RunState(10, 1, 2, True, 4, 3, 12, True)
        self.enemies = (
            Enemy("small_trigger", self.anchor, 1, 10),
            Enemy("large_target", (4, 1), 8, 10),
        )
        # Proposed main piece connects to the known dormant anchor. The
        # explicit candidate effects describe that anchor becoming active.
        self.option = Placement(
            self.destination, (self.link,), 10, True, True, "test observed topology"
        )

    def review(self, **changes):
        values = dict(
            ledger=self.ledger,
            run=self.run,
            enemies=self.enemies,
            options=(self.option,),
            now=10,
            max_age=1,
        )
        values.update(changes)
        return review_placement(**values)

    def test_small_enemy_can_trigger_damage_to_separate_priority_target(self):
        result = self.review()
        self.assertEqual(result.outcome, "placement_candidate")
        self.assertEqual(result.trigger_enemies, {"small_trigger"})
        self.assertIn("large_target", result.marginal_targets)
        self.assertEqual(result.score, 9)

    def test_damage_area_without_trigger_gets_no_damage_credit(self):
        result = self.review(enemies=self.enemies[1:])
        self.assertEqual(result.outcome, "observe")
        self.assertFalse(result.marginal_targets)

    def test_existing_active_coverage_is_not_double_counted(self):
        self.ledger.observe(
            10, (CellObservation(self.anchor, "piece", True, self.link),)
        )
        result = self.review()
        self.assertEqual(result.outcome, "observe")
        self.assertEqual(result.covered_targets, {"small_trigger", "large_target"})

    def test_explicit_disappearance_changes_coverage_and_allows_refill_review(self):
        self.ledger.observe(
            10,
            (CellObservation(self.destination, "piece", True, self.link_for_new()),),
        )
        self.assertEqual(self.review().outcome, "observe")
        self.ledger.observe(10.2, (CellObservation(self.destination, "empty"),))
        self.assertEqual(self.review(now=10.2).outcome, "placement_candidate")

    def link_for_new(self):
        return s2_effect(self.destination, frozenset({"horizontal"}), "test new piece")

    def test_occluded_or_other_occupant_is_not_a_free_deployment_tile(self):
        for occupancy in ("unknown", "other", "piece"):
            with self.subTest(occupancy=occupancy):
                self.ledger.observe(10, (CellObservation(self.destination, occupancy),))
                self.assertEqual(self.review().outcome, "observe")

    def test_unseen_extra_piece_cannot_supply_promised_damage(self):
        bonus = effect((9, 1), triggers={self.anchor}, damage={(4, 1)})
        option = replace(self.option, effects=(bonus,))
        self.assertEqual(self.review(options=(option,)).outcome, "observe")

    def test_stale_individual_cells_are_not_refreshed_by_unrelated_scan(self):
        self.ledger.observe(12, (CellObservation((9, 9), "empty"),))
        result = self.review(
            now=12,
            run=replace(self.run, observed_at=12),
            enemies=tuple(replace(enemy, observed_at=12) for enemy in self.enemies),
            options=(replace(self.option, observed_at=12),),
        )
        self.assertEqual(result.outcome, "observe")

    def test_stale_or_future_enemy_evidence_requests_observation(self):
        for at in (8, 11):
            with self.subTest(at=at):
                enemies = (replace(self.enemies[0], observed_at=at), self.enemies[1])
                self.assertEqual(
                    self.review(enemies=enemies).reason, "stale_enemy_evidence"
                )

    def test_unknown_target_state_is_not_treated_as_hittable(self):
        enemies = (self.enemies[0], replace(self.enemies[1], targetable=None))
        self.assertEqual(self.review(enemies=enemies).reason, "enemy_state_unknown")

    def test_known_untargetable_enemy_gets_no_target_credit(self):
        enemies = (self.enemies[0], replace(self.enemies[1], targetable=False))
        self.assertNotIn("large_target", self.review(enemies=enemies).marginal_targets)

    def test_actual_e1_cannot_use_account_maximum_e2_skill3(self):
        result = self.review(run=replace(self.run, phase=1, skill=3))
        self.assertEqual(result.reason, "selected_skill_unavailable_in_this_run")

    def test_unknown_run_phase_owner_or_skill_abstains(self):
        for changes in ({"phase": None}, {"skill": None}, {"owner_present": None}):
            with self.subTest(changes=changes):
                self.assertEqual(
                    self.review(run=replace(self.run, **changes)).reason,
                    "run_form_or_owner_unknown",
                )

    def test_s2_cannot_reuse_s3_footprint(self):
        option = replace(self.option, effects=(replace(self.link, skill=3),))
        self.assertEqual(self.review(options=(option,)).outcome, "observe")

    def test_s3_uses_supplied_piece_geometry_without_owner_base_range(self):
        footprint = effect((20, 20), 3, {(20, 21)}, {(20, 21), (22, 20)})
        self.ledger.observe(10, (CellObservation((20, 20), "empty"),))
        option = Placement((20, 20), (footprint,), 10, True, True, "test S3 geometry")
        enemies = (Enemy("trigger", (20, 21), 1, 10), Enemy("target", (22, 20), 9, 10))
        result = self.review(
            run=replace(self.run, phase=2, skill=3),
            enemies=enemies,
            options=(option,),
        )
        self.assertEqual(result.tile, (20, 20))
        self.assertEqual(result.marginal_targets, {"trigger", "target"})

    def test_resource_and_ready_state_are_observed_hard_gates(self):
        for changes, reason in (
            ({"stock": None}, "piece_resources_unknown"),
            ({"available_cost": None}, "piece_resources_unknown"),
            ({"deployment_cost": None}, "piece_resources_unknown"),
            ({"card_ready": None}, "piece_card_not_verified_ready"),
            ({"card_ready": False}, "piece_card_not_verified_ready"),
            ({"deployment_cost": 15}, "insufficient_observed_cost"),
        ):
            with self.subTest(changes=changes):
                self.assertEqual(
                    self.review(run=replace(self.run, **changes)).reason, reason
                )

    def test_s2_empty_stock_waits_for_automatic_replenishment(self):
        result = self.review(run=replace(self.run, stock=0, skill_ready=True))
        self.assertEqual(result.outcome, "wait")
        self.assertEqual(result.skill_advice, "await_automatic_s2_replenishment")

    def test_s3_activation_advice_does_not_invent_stock_or_spawned_pieces(self):
        run = replace(self.run, phase=2, skill=3, stock=0, skill_ready=True)
        previous = dict(self.ledger.cells)
        result = self.review(run=run)
        self.assertEqual(result.skill_advice, "review_s3_then_observe_spawned_pieces")
        self.assertEqual(run.stock, 0)
        self.assertEqual(self.ledger.cells, previous)

    def test_changed_enemy_positions_change_candidate_without_schedule(self):
        right = (10, 1)
        self.ledger.observe(10, (CellObservation(right, "empty"),))
        far_effect = effect(right, 2, {(10, 2)}, {(10, 2), (11, 2)})
        far_option = Placement(
            right, (far_effect,), 10, True, True, "test far geometry"
        )
        options = (self.option, far_option)
        self.assertEqual(self.review(options=options).tile, self.destination)
        shifted = (
            replace(self.enemies[0], tile=(10, 2)),
            replace(self.enemies[1], tile=(11, 2)),
        )
        self.assertEqual(self.review(options=options, enemies=shifted).tile, right)

    def test_unknown_legality_and_link_activation_are_not_assumed(self):
        for changes in ({"legal": None}, {"activation_supported": None}):
            with self.subTest(changes=changes):
                option = replace(self.option, **changes)
                self.assertEqual(self.review(options=(option,)).outcome, "observe")

    def test_review_does_not_commit_click_as_piece_presence(self):
        self.review()
        self.assertEqual(
            self.ledger.cells[self.destination].observation.occupancy, "empty"
        )

    def test_negative_nan_and_boolean_resources_are_rejected(self):
        for value in (-1, float("nan"), True):
            with self.subTest(value=value), self.assertRaises(ValueError):
                replace(self.run, stock=value)

    def test_s2_axes_do_not_turn_into_diagonal_or_owner_facing(self):
        line = s2_effect((5, 5), frozenset({"vertical"}), "test vertical chain")
        self.assertIn((5, 2), line.damage_cells)
        self.assertNotIn((2, 5), line.damage_cells)
        self.assertNotIn((4, 4), line.damage_cells)
        with self.assertRaises(ValueError):
            s2_effect((5, 5), frozenset({"left"}), "not a link axis")

    def test_coverage_does_not_report_kill_or_guaranteed_hit(self):
        observed = coverage(self.link, self.enemies)
        self.assertEqual(observed.trigger_enemies, {"small_trigger"})
        self.assertEqual(observed.targets, {"small_trigger", "large_target"})


if __name__ == "__main__":
    unittest.main()
