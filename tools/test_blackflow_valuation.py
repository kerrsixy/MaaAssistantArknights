"""Offline counterfactuals for BlackFlow valuation; no live actions or network."""

import json
import math
import subprocess
import sys
import tempfile
import unittest
from dataclasses import asdict, replace
from pathlib import Path

from blackflow_valuation import (
    Candidate,
    Context,
    LegacyAdvice,
    Offer,
    Weights,
    context_from_json,
    evaluate,
    rank_offers,
)


CATALOG_PATH = (
    Path(__file__).resolve().parents[1]
    / "resource/roguelike/BlackFlow/valuation_catalog.json"
)


def item(features, *, item_id="test", kind="relic", **extra):
    return {
        "id": item_id,
        "name": "not_used_for_scoring",
        "kind": kind,
        "features": features,
        "unsupported_effects": [],
        **extra,
    }


def context(**updates):
    values = {
        "wallet": 30,
        "hope": 8,
        "reserve": 4,
        "remaining_battles": 6,
        "hp": 3,
        "max_hp": 6,
        "movement_capacity": 10,
        "movement_used": 2,
        "capacity_opportunity_cost": 0,
        "account_uid": "same-uid",
        "client": "Official",
        "team_best_power": {"SNIPER": 0.3, "SPECIAL": 0.5},
        "role_needs": {"SNIPER": 0.8, "SPECIAL": 0.0},
        "candidate_scan_complete": True,
    }
    values.update(updates)
    return Context(**values)


def candidate(**updates):
    values = {
        "id": "trained",
        "role": "SNIPER",
        "account_uid": "same-uid",
        "client": "Official",
        "hope_cost": 6,
        "power_after": 0.95,
        "trained": True,
        "owned": True,
        "evidence_verified": True,
    }
    values.update(updates)
    return Candidate(**values)


class ValuationTests(unittest.TestCase):
    def score(self, data, ctx=None, price=4):
        return evaluate(data, Offer(data["id"], price, True), ctx or context())

    def test_wallet_and_reserve_are_hard_gates_despite_high_value(self):
        data = item({"attack_pct": 100})
        result = self.score(data, context(wallet=8, reserve=5), price=4)
        self.assertEqual(result.decision, "skip")
        self.assertIn("insufficient_unreserved_ingots", result.denials)
        self.assertGreater(result.known_net_value, 100)

    def test_free_positive_reward_can_help_when_wallet_is_below_reserve_target(self):
        result = self.score(
            item({"immediate_ingots": 1}), context(wallet=2, reserve=4), price=0
        )
        self.assertEqual(result.decision, "buy")

    def test_catalog_price_is_not_an_observed_shop_price(self):
        data = item({"immediate_ingots": 100}, base_price=4)
        result = evaluate(data, Offer("test", None, False), context())
        self.assertEqual(result.decision, "review")
        self.assertIn("shop_price_unverified", result.unknown)

    def test_immediate_currency_cost_cannot_consume_reserved_money(self):
        result = self.score(
            item({"immediate_ingots": -10, "attack_pct": 100}),
            context(wallet=15, reserve=4),
        )
        self.assertIn("post_effect_budget_below_reserve", result.denials)
        self.assertEqual(result.decision, "skip")

    def test_healing_has_no_value_at_full_health_and_more_when_hurt(self):
        data = item({"heal_hp": 3})
        full = self.score(data, context(hp=6, max_hp=6), price=1)
        hurt = self.score(data, context(hp=1, max_hp=6), price=1)
        self.assertEqual(full.decision, "skip")
        self.assertEqual(hurt.decision, "buy")
        self.assertGreater(hurt.known_gross_value, full.known_gross_value)

    def test_lethal_cost_cannot_be_hidden_by_a_large_bonus(self):
        result = self.score(
            item({"heal_hp": -3, "immediate_ingots": 1000}), context(hp=3)
        )
        self.assertEqual(result.decision, "skip")
        self.assertIn("lethal_life_cost", result.denials)

    def test_same_combat_bonus_has_less_value_late_or_when_already_buffed(self):
        data = item({"attack_pct": 0.2})
        early = self.score(data, context(remaining_battles=8))
        late = self.score(data, context(remaining_battles=1))
        stacked = self.score(
            data, context(remaining_battles=8, active_bonuses={"attack_pct": 0.5})
        )
        self.assertGreater(early.known_gross_value, late.known_gross_value)
        self.assertGreater(early.known_gross_value, stacked.known_gross_value)

    def test_inapplicable_profession_buff_has_no_invented_team_benefit(self):
        data = item(
            {"role_buffs": [{"role": "Medic", "features": {"attack_pct": 0.5}}]}
        )
        result = self.score(data, context(role_weights={"MEDIC": 0.0}))
        self.assertEqual(result.known_gross_value, 0)
        self.assertEqual(result.decision, "skip")

    def test_profession_population_missing_is_unknown_not_a_global_buff(self):
        data = item(
            {"role_buffs": [{"role": "Medic", "features": {"attack_pct": 0.5}}]}
        )
        result = self.score(data)
        self.assertEqual(result.decision, "review")
        self.assertIn("role_population_unverified:MEDIC", result.unknown)

    def test_movement_requires_capacity_and_planner_proven_marginal_value(self):
        data = item({"movement": {"charges": 2}, "scrap_slots": 1}, kind="scrap")
        unknown = self.score(data)
        self.assertIn("marginal_route_value_unverified", unknown.unknown)
        full = self.score(
            data, context(movement_used=10, marginal_route_value={"test": 1000})
        )
        self.assertIn("insufficient_movement_capacity", full.denials)
        redundant = self.score(data, context(marginal_route_value={"test": 0}))
        useful = self.score(data, context(marginal_route_value={"test": 12}))
        self.assertEqual(redundant.decision, "skip")
        self.assertEqual(useful.decision, "buy")

    def test_appraisal_is_not_spendable_money_without_conversion_evidence(self):
        data = item({"sale_value": 100, "scrap_slots": 1}, kind="scrap")
        result = self.score(data)
        self.assertEqual(result.decision, "review")
        self.assertIn("appraisal_liquidation_unverified", result.unknown)
        self.assertEqual(result.known_gross_value, 0)

    def test_sale_and_using_same_move_are_mutually_exclusive(self):
        data = item({"sale_value": 6, "movement": {}, "scrap_slots": 1}, kind="scrap")
        result = self.score(
            data,
            context(
                sale_value_to_ingots=1,
                liquidation_probability=1,
                marginal_route_value={"test": 10},
            ),
            price=0,
        )
        self.assertEqual(result.known_gross_value, 10)

    def test_recurring_rewards_are_not_immediate_funds(self):
        data = item({"ingots_per_battle": 10})
        result = self.score(
            data, context(wallet=4, reserve=0, remaining_battles=8), price=5
        )
        self.assertEqual(result.decision, "skip")
        self.assertIn("insufficient_unreserved_ingots", result.denials)
        now = self.score(data, context(remaining_battles=0), price=0)
        self.assertEqual(now.known_gross_value, 0)

    def test_voucher_requires_useful_same_account_trained_target(self):
        voucher = item({"recruit_role": "Sniper"}, kind="recruit_voucher")
        good = self.score(voucher, context(candidates=(candidate(),)))
        self.assertEqual(good.decision, "buy")
        for bad in (
            candidate(account_uid="other-uid"),
            candidate(client="Bilibili"),
            candidate(trained=False),
            candidate(owned=False),
            candidate(evidence_verified=False),
        ):
            with self.subTest(candidate=bad):
                result = self.score(voucher, context(candidates=(bad,)))
                self.assertEqual(result.decision, "skip")
                self.assertIsNone(result.selected_candidate_id)

    def test_matching_uid_does_not_supply_missing_asset_evidence(self):
        voucher = item({"recruit_role": "Sniper"}, kind="recruit_voucher")
        required = ("trained", "owned", "evidence_verified")
        for missing in (*required, "all"):
            with self.subTest(missing=missing):
                evidence = {
                    key: True for key in required if key != missing and missing != "all"
                }
                data = {
                    "wallet": 30,
                    "hope": 8,
                    "remaining_battles": 6,
                    "account_uid": "same-uid",
                    "client": "Official",
                    "team_best_power": {"Sniper": 0.3},
                    "role_needs": {"Sniper": 0.8},
                    "candidate_scan_complete": True,
                    "candidates": [
                        {
                            "id": "same-account-with-missing-proof",
                            "role": "Sniper",
                            "account_uid": "same-uid",
                            "client": "Official",
                            "hope_cost": 6,
                            "power_after": 0.95,
                            **evidence,
                        }
                    ],
                }
                result = self.score(voucher, context_from_json(data))
                self.assertNotEqual(result.decision, "buy")
                self.assertIsNone(result.selected_candidate_id)

    def test_no_hope_or_no_money_rejects_voucher(self):
        voucher = item({"recruit_role": "SNIPER"}, kind="recruit_voucher")
        no_hope = self.score(voucher, context(hope=0, candidates=(candidate(),)))
        no_money = self.score(
            voucher, context(wallet=3, reserve=0, candidates=(candidate(),))
        )
        self.assertEqual(no_hope.decision, "skip")
        self.assertIn("insufficient_hope_for_useful_candidate", no_hope.denials)
        self.assertEqual(no_money.decision, "skip")

    def test_already_covered_profession_can_still_have_a_good_upgrade(self):
        voucher = item({"recruit_role": "Sniper"}, kind="recruit_voucher")
        ctx = context(
            team_best_power={"SNIPER": 0.4},
            role_needs={"SNIPER": 0},
            candidates=(candidate(),),
        )
        self.assertEqual(self.score(voucher, ctx).decision, "buy")
        full = replace(ctx, team_best_power={"SNIPER": 0.99})
        self.assertEqual(self.score(voucher, full).decision, "skip")

    def test_key_promotion_is_a_valid_opportunity_when_no_new_operator_is_needed(self):
        voucher = item({"recruit_role": "Specialist"}, kind="recruit_voucher")
        promotion = candidate(
            role="SPECIAL", hope_cost=3, promotion=True, current_power=0.45
        )
        result = self.score(
            voucher, context(candidates=(promotion,), role_needs={"SPECIAL": 0})
        )
        self.assertEqual(result.decision, "buy")
        self.assertEqual(result.selected_candidate_id, promotion.id)
        self.assertEqual(result.selected_candidate_hope, 3)

    def test_required_module_unknown_or_locked_never_counts_as_ready(self):
        voucher = item({"recruit_role": "Sniper"}, kind="recruit_voucher")
        for proof in ((False, False), (True, False)):
            op = candidate(
                module_required=True, module_verified=proof[0], module_ready=proof[1]
            )
            self.assertEqual(
                self.score(voucher, context(candidates=(op,))).decision, "skip"
            )
        available_but_not_active_after_recruit = candidate(
            module_required=True,
            module_verified=True,
            module_ready=True,
            module_applies_to_result=False,
        )
        self.assertEqual(
            self.score(
                voucher, context(candidates=(available_but_not_active_after_recruit,))
            ).decision,
            "skip",
        )
        verified_result = replace(
            available_but_not_active_after_recruit, module_applies_to_result=True
        )
        self.assertEqual(
            self.score(voucher, context(candidates=(verified_result,))).decision, "buy"
        )

    def test_incomplete_candidates_are_review_not_proof_no_good_target_exists(self):
        voucher = item({"recruit_role": "Sniper"}, kind="recruit_voucher")
        result = self.score(
            voucher, context(candidates=(), candidate_scan_complete=False)
        )
        self.assertEqual(result.decision, "review")
        self.assertIn("candidate_opportunities_incomplete", result.unknown)

    def test_partial_expensive_candidates_do_not_prove_entire_profession_unaffordable(
        self,
    ):
        voucher = item({"recruit_role": "Sniper"}, kind="recruit_voucher")
        result = self.score(
            voucher,
            context(hope=2, candidates=(candidate(),), candidate_scan_complete=False),
        )
        self.assertEqual(result.decision, "review")
        self.assertIn("candidate_hope_coverage_unverified", result.unknown)

    def test_unknown_future_effect_keeps_traceable_previous_policy_advice(self):
        advice = LegacyAdvice(
            "buy",
            "resource/roguelike/BlackFlow/shopping.json",
            "prior-commit",
            "trained useful relic",
            True,
        )
        data = item({"future_effect_v2": 100})
        result = self.score(data, context(legacy_advice={"test": advice}))
        self.assertEqual(result.decision, "review")
        self.assertEqual(result.value_bounds, (None, None))
        self.assertEqual(result.fallback_advice["decision"], "buy")
        self.assertFalse(result.fallback_advice["new_model_validated"])
        # The old rule cannot override a verified budget gate.
        broke = self.score(data, context(wallet=0, legacy_advice={"test": advice}))
        self.assertEqual(broke.decision, "skip")
        self.assertIsNone(broke.fallback_advice)

    def test_partial_high_score_cannot_outrank_supported_offer(self):
        partial = item(
            {"immediate_ingots": 1000, "new_negative_or_positive_effect": 1},
            item_id="partial",
        )
        complete = item({"immediate_ingots": 4}, item_id="complete")
        ranking = rank_offers(
            {"partial": partial, "complete": complete},
            [Offer("partial", 1, True), Offer("complete", 1, True)],
            context(),
        )
        self.assertEqual(ranking[0].item_id, "complete")
        self.assertEqual(ranking[1].decision, "review")

    def test_nonstackable_duplicate_rejected(self):
        result = self.score(
            item({"attack_pct": 1}, stackable=False), context(held_counts={"test": 1})
        )
        self.assertIn("non_stackable_duplicate", result.denials)

    def test_price_increases_never_improve_value(self):
        data = item({"attack_pct": 0.5})
        values = [self.score(data, price=price).known_net_value for price in range(12)]
        self.assertEqual(values, sorted(values, reverse=True))

    def test_nonfinite_and_invalid_capacity_are_rejected(self):
        for value in (math.nan, math.inf, -math.inf):
            with self.assertRaises(ValueError):
                self.score(item({"attack_pct": value}))
        with self.assertRaises(ValueError):
            context(movement_capacity=1, movement_used=2)
        with self.assertRaises(ValueError):
            self.score(item({"scrap_slots": 0.5}, kind="scrap"))
        with self.assertRaises(ValueError):
            Weights(cash=math.nan)
        for key in ("trained", "owned", "evidence_verified"):
            with self.subTest(evidence_key=key):
                with self.assertRaises(ValueError):
                    candidate(**{key: "false"})

    def test_context_json_round_trip_preserves_candidates_and_advice(self):
        ctx = context(
            candidates=(candidate(),),
            legacy_advice={
                "test": LegacyAdvice("skip", "prior-policy", "1", "no need", True)
            },
        )
        encoded = json.loads(json.dumps(asdict(ctx), default=list))
        restored = context_from_json(encoded)
        self.assertEqual(restored.candidates[0], ctx.candidates[0])
        self.assertEqual(restored.legacy_advice["test"], ctx.legacy_advice["test"])


class ActualCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))["items"]

    def actual(self, item_id, ctx, price):
        return evaluate(
            self.catalog[item_id],
            Offer(item_id, price, True),
            ctx,
            catalog=self.catalog,
        )

    def test_real_moss_bundle_counts_four_slots_and_future_growth(self):
        item_id = "rogue_6_scrap_G_07"
        base = context(
            sale_value_to_ingots=1, liquidation_probability=1, expected_acquisitions=0
        )
        before = self.actual(item_id, base, 12)
        after = self.actual(item_id, replace(base, expected_acquisitions=6), 12)
        self.assertEqual(after.movement_slots_required, 4)
        self.assertEqual(before.decision, "skip")
        self.assertEqual(after.decision, "buy")
        self.assertGreater(after.known_net_value, before.known_net_value)
        full = self.actual(
            item_id, replace(base, movement_capacity=5, movement_used=2), 12
        )
        self.assertEqual(full.decision, "skip")
        self.assertIn("insufficient_movement_capacity", full.denials)

    def test_real_frost_tree_changes_with_perfect_battle_risk(self):
        item_id = "rogue_6_scrap_G_06"
        base = context(
            sale_value_to_ingots=1, liquidation_probability=1, remaining_battles=2
        )
        unknown = self.actual(item_id, base, 12)
        safe = self.actual(item_id, replace(base, perfect_battle_probability=1), 12)
        risky = self.actual(item_id, replace(base, perfect_battle_probability=0.5), 12)
        self.assertEqual(unknown.decision, "review")
        self.assertEqual(unknown.known_gross_value, 0)
        self.assertEqual(safe.decision, "buy")
        self.assertEqual(risky.decision, "skip")
        self.assertGreater(safe.known_gross_value, risky.known_gross_value)

    def test_real_initial_dp_relic_values_remaining_fights(self):
        item_id = "rogue_6_relic_legacy_33"
        early = self.actual(item_id, context(remaining_battles=10), 5)
        late = self.actual(item_id, context(remaining_battles=1), 5)
        self.assertEqual(early.decision, "buy")
        self.assertEqual(late.decision, "skip")

    def test_real_specialist_buff_does_not_apply_to_absent_profession(self):
        item_id = "rogue_6_relic_book_7"
        absent = self.actual(item_id, context(role_weights={"SPECIAL": 0}), 5)
        present = self.actual(item_id, context(role_weights={"SPECIAL": 0.5}), 5)
        self.assertEqual(absent.decision, "skip")
        self.assertEqual(present.decision, "buy")

    def test_real_sniper_ticket_uses_tier_roles_and_current_hope(self):
        item_id = "rogue_6_recruit_ticket_sniper"
        ctx = context(candidates=(candidate(),))
        usable = self.actual(item_id, ctx, 6)
        unusable = self.actual(item_id, replace(ctx, hope=0), 6)
        self.assertEqual(usable.decision, "buy")
        self.assertEqual(unusable.decision, "skip")

    def test_all_actual_catalog_items_have_finite_partial_values_and_no_actions(self):
        ctx = context(
            sale_value_to_ingots=1,
            liquidation_probability=0.8,
            expected_acquisitions=3,
            expected_reveals=3,
            expected_moves=3,
            expected_recruits=1,
            expected_leaks=0,
            remaining_nodes=5,
            remaining_zones=2,
            perfect_battle_probability=0.8,
            candidates=(candidate(),),
            role_weights={"SPECIAL": 0.5, "SNIPER": 0.5},
        )
        for item_id, data in self.catalog.items():
            with self.subTest(item_id=item_id):
                result = self.actual(item_id, ctx, 6)
                self.assertTrue(math.isfinite(result.known_net_value))
                self.assertIn(result.decision, ("buy", "skip", "review"))
                if data["unsupported_effects"]:
                    self.assertNotEqual(result.decision, "buy")


class PublicCliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
        self.catalog_path = self.root / "catalog.json"
        self.context_path = self.root / "context.json"
        self.offer_path = self.root / "offers.json"
        self.output_path = self.root / "result.json"
        self.context_path.write_text(
            json.dumps(asdict(context()), default=list), encoding="utf-8"
        )
        self.offer_path.write_text(
            json.dumps([asdict(Offer("rogue_6_relic_legacy_33", 1, True))]),
            encoding="utf-8",
        )

    def run_cli(self):
        self.catalog_path.write_text(
            json.dumps(self.catalog, ensure_ascii=False), encoding="utf-8"
        )
        return subprocess.run(
            [
                sys.executable,
                str(Path(__file__).with_name("blackflow_valuation.py")),
                "--catalog",
                str(self.catalog_path),
                "--context",
                str(self.context_path),
                "--offers",
                str(self.offer_path),
                "--out",
                str(self.output_path),
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=30,
            check=False,
        )

    def test_valid_sync_catalog_executes_public_cli(self):
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        output = json.loads(self.output_path.read_text(encoding="utf-8"))
        self.assertEqual(output["mode"], "offline_only")
        self.assertEqual(output["results"][0]["item_id"], "rogue_6_relic_legacy_33")

    def test_cli_matching_uid_without_asset_proof_cannot_recommend_voucher(self):
        data = asdict(context())
        data["candidates"] = [
            {
                "id": "no-asset-proof",
                "role": "SNIPER",
                "account_uid": "same-uid",
                "client": "Official",
                "hope_cost": 6,
                "power_after": 0.95,
            }
        ]
        self.context_path.write_text(json.dumps(data, default=list), encoding="utf-8")
        self.offer_path.write_text(
            json.dumps([asdict(Offer("rogue_6_recruit_ticket_sniper", 6, True))]),
            encoding="utf-8",
        )
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        output = json.loads(self.output_path.read_text(encoding="utf-8"))
        self.assertNotEqual(output["results"][0]["decision"], "buy")
        self.assertIsNone(output["results"][0]["selected_candidate_id"])

    def test_missing_item_is_rejected_before_scoring(self):
        del self.catalog["items"][next(iter(self.catalog["items"]))]
        result = self.run_cli()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Catalog coverage does not match its items", result.stderr)
        self.assertFalse(self.output_path.exists())

    def test_changed_effect_is_rejected_without_overwriting_old_output(self):
        self.catalog["items"]["rogue_6_relic_legacy_33"]["features"]["starting_dp"] = (
            999
        )
        self.output_path.write_text("prior verified output", encoding="utf-8")
        result = self.run_cli()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Catalog content fingerprint mismatch", result.stderr)
        self.assertEqual(
            self.output_path.read_text(encoding="utf-8"), "prior verified output"
        )

    def test_invalid_source_digest_is_rejected(self):
        self.catalog["source"]["sha256"] = "unverified"
        result = self.run_cli()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Catalog lacks a valid source digest", result.stderr)
        self.assertFalse(self.output_path.exists())


if __name__ == "__main__":
    unittest.main()
