"""Offline BlackFlow shop valuation; this module never controls or purchases in game.

The unit is heuristic ingot-equivalent marginal utility, not a clear probability.
Observed prices, account identity, hope and capacity are hard gates. Unknown
effects produce a review decision and an unbounded total, rather than a zero
score. No operator or item names participate in the formula.
"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any, Mapping


ROLE_ALIASES = {
    "先锋": "PIONEER",
    "VANGUARD": "PIONEER",
    "近卫": "WARRIOR",
    "GUARD": "WARRIOR",
    "重装": "TANK",
    "DEFENDER": "TANK",
    "狙击": "SNIPER",
    "术师": "CASTER",
    "医疗": "MEDIC",
    "辅助": "SUPPORT",
    "SUPPORTER": "SUPPORT",
    "特种": "SPECIAL",
    "SPECIALIST": "SPECIAL",
    "全职业": "ALL",
}


def role_name(value: str) -> str:
    return ROLE_ALIASES.get(value.upper(), value.upper())


def number(value: Any, label: str) -> float:
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(value)
    ):
        raise ValueError(f"{label} must be a finite number")
    return float(value)


@dataclass(frozen=True)
class LegacyAdvice:
    """Traceable previous-policy advice; kept separate from the new model verdict."""

    decision: str
    source: str
    version: str
    reason: str
    verified: bool = False
    account_uid: str = ""
    client: str = ""

    def __post_init__(self):
        if self.decision not in ("buy", "skip") or not self.source or not self.version:
            raise ValueError("Legacy advice requires a decision, source and version")


@dataclass(frozen=True)
class Weights:
    """Shared coefficients; calibrate with outcome evidence, never item-name overrides.

    cash=1 anchors the unit. The other coefficients express explicit tradeoffs:
    one hope is valued at two ingots, one missing life at two, a 100% team
    attack increase over one fight at eight, and one starting DP at 0.08 per
    fight. These are hypotheses to validate, not measured win-rate estimates.
    """

    cash: float = 1.0
    hope: float = 2.0
    heal_life: float = 2.0
    max_life: float = 0.8
    shield: float = 1.0
    combat_per_fight: float = 8.0
    defense_per_fight: float = 3.0
    hp_per_fight: float = 2.0
    starting_dp_per_fight: float = 0.08
    deployment_per_fight: float = 1.0
    squad_slot_per_fight: float = 0.35
    skill_sp_per_fight: float = 0.12
    skill_recovery_per_fight: float = 2.0
    recruitment_per_fight: float = 10.0
    role_gap: float = 0.5
    missing_tag: float = 0.25
    low_life_urgency: float = 2.0

    def __post_init__(self):
        for key, value in asdict(self).items():
            if number(value, key) < 0:
                raise ValueError(f"Negative valuation coefficient: {key}")


@dataclass(frozen=True)
class Candidate:
    """A same-account recruit/promotion opportunity with an observed hope cost.

    power_after/current_power are caller-supplied normalized strength estimates
    in [0,1], not an operator-name tier list. Required module checks belong to
    account evidence; a missing/unknown required slot makes this candidate unusable.
    """

    id: str
    role: str
    account_uid: str
    client: str
    hope_cost: int | None
    power_after: float
    current_power: float = 0.0
    rarity: int = 6
    trained: bool = False
    owned: bool = False
    evidence_verified: bool = False
    promotion: bool = False
    module_required: bool = False
    module_verified: bool = False
    module_ready: bool = False
    module_applies_to_result: bool = False
    tags: tuple[str, ...] = ()

    def __post_init__(self):
        if not self.id or not 1 <= self.rarity <= 6:
            raise ValueError("Candidate ID and rarity are required")
        for key in ("trained", "owned", "evidence_verified"):
            if not isinstance(getattr(self, key), bool):
                raise ValueError(f"Candidate {key} must be an explicit boolean")
        for key in ("power_after", "current_power"):
            if not 0 <= number(getattr(self, key), key) <= 1:
                raise ValueError(f"Candidate {key} must be in [0,1]")
        if self.hope_cost is not None and (
            isinstance(self.hope_cost, bool)
            or not isinstance(self.hope_cost, int)
            or self.hope_cost < 0
        ):
            raise ValueError("Observed hope cost must be a non-negative integer")


@dataclass(frozen=True)
class Context:
    wallet: int | None
    hope: int | None = None
    reserve: int = 0
    floor: int = 1
    remaining_battles: float | None = None
    remaining_nodes: float | None = None
    remaining_zones: float | None = None
    expected_acquisitions: float | None = None
    expected_reveals: float | None = None
    expected_recruits: float | None = None
    expected_moves: float | None = None
    expected_leaks: float | None = None
    perfect_battle_probability: float | None = None
    conditional_trigger_counts: Mapping[str, float] = field(default_factory=dict)
    action_point_value: float | None = None
    hp: int | None = None
    max_hp: int | None = None
    movement_capacity: int | None = None
    movement_used: int | None = None
    capacity_opportunity_cost: float | None = None
    # Appraisal is not cash without an observed conversion rule.
    sale_value_to_ingots: float | None = None
    liquidation_probability: float | None = None
    # Planner-proven marginal route utility, already accounting for existing moves.
    marginal_route_value: Mapping[str, float] = field(default_factory=dict)
    account_uid: str = ""
    client: str = ""
    candidates: tuple[Candidate, ...] = ()
    candidate_scan_complete: bool = False
    legacy_advice: Mapping[str, LegacyAdvice] = field(default_factory=dict)
    acquired_candidate_ids: frozenset[str] = frozenset()
    team_best_power: Mapping[str, float] = field(default_factory=dict)
    role_needs: Mapping[str, float] = field(default_factory=dict)
    role_weights: Mapping[str, float] = field(default_factory=dict)
    needed_tags: Mapping[str, float] = field(default_factory=dict)
    active_bonuses: Mapping[str, float] = field(default_factory=dict)
    held_counts: Mapping[str, int] = field(default_factory=dict)

    def __post_init__(self):
        integer_fields = {
            "wallet",
            "hope",
            "reserve",
            "floor",
            "hp",
            "max_hp",
            "movement_capacity",
            "movement_used",
        }
        for key in (
            "wallet",
            "hope",
            "reserve",
            "floor",
            "remaining_battles",
            "remaining_nodes",
            "remaining_zones",
            "expected_acquisitions",
            "expected_reveals",
            "expected_recruits",
            "expected_moves",
            "expected_leaks",
            "perfect_battle_probability",
            "action_point_value",
            "hp",
            "max_hp",
            "movement_capacity",
            "movement_used",
            "capacity_opportunity_cost",
            "sale_value_to_ingots",
            "liquidation_probability",
        ):
            value = getattr(self, key)
            if value is not None and number(value, key) < 0:
                raise ValueError(f"Negative context value: {key}")
            if (
                value is not None
                and key in integer_fields
                and not isinstance(value, int)
            ):
                raise ValueError(f"Context {key} must be an integer")
        if self.floor < 1:
            raise ValueError("Floor must be positive")
        if self.hp is not None and self.max_hp is not None and self.hp > self.max_hp:
            raise ValueError("Current life cannot exceed maximum life")
        if (
            self.liquidation_probability is not None
            and self.liquidation_probability > 1
        ):
            raise ValueError("Liquidation probability must be in [0,1]")
        if (
            self.perfect_battle_probability is not None
            and self.perfect_battle_probability > 1
        ):
            raise ValueError("Perfect battle probability must be in [0,1]")
        if (
            self.movement_used is not None
            and self.movement_capacity is not None
            and self.movement_used > self.movement_capacity
        ):
            raise ValueError("Occupied movement slots exceed capacity")
        for key in (
            "marginal_route_value",
            "team_best_power",
            "role_needs",
            "role_weights",
            "needed_tags",
            "active_bonuses",
            "conditional_trigger_counts",
            "held_counts",
        ):
            for name, value in getattr(self, key).items():
                if number(value, f"{key}.{name}") < 0:
                    raise ValueError(f"Negative context quantity: {key}.{name}")
                if (
                    key
                    in ("team_best_power", "role_needs", "role_weights", "needed_tags")
                    and value > 1
                ):
                    raise ValueError(f"Context fraction exceeds one: {key}.{name}")
                if key == "held_counts" and not isinstance(value, int):
                    raise ValueError("Inventory counts must be integers")
        if sum(self.role_weights.values()) > 1 + 1e-9:
            raise ValueError("Team profession contribution shares exceed one")


@dataclass(frozen=True)
class Offer:
    item_id: str
    price: int | None
    price_verified: bool = False

    def __post_init__(self):
        if not self.item_id:
            raise ValueError("Offer item ID is required")
        if self.price is not None and (
            isinstance(self.price, bool)
            or not isinstance(self.price, int)
            or self.price < 0
        ):
            raise ValueError("Shop price must be a non-negative integer")


@dataclass(frozen=True)
class Evaluation:
    item_id: str
    decision: str
    eligible: bool
    denials: tuple[str, ...]
    unknown: tuple[str, ...]
    components: Mapping[str, float]
    known_gross_value: float
    costs: Mapping[str, float]
    known_net_value: float
    value_bounds: tuple[float | None, float | None]
    confidence: str
    selected_candidate_id: str | None = None
    selected_candidate_hope: int = 0
    movement_slots_required: int = 0
    fallback_advice: Mapping[str, Any] | None = None


COMBAT_FEATURES = {
    "attack_pct": ("combat_per_fight", 1.0),
    "defense_pct": ("defense_per_fight", 1.0),
    "hp_pct": ("hp_per_fight", 1.0),
    "attack_speed": ("combat_per_fight", 0.01),
    "starting_dp": ("starting_dp_per_fight", 1.0),
    "deployment_limit": ("deployment_per_fight", 1.0),
    "squad_limit": ("squad_slot_per_fight", 1.0),
    "skill_sp": ("skill_sp_per_fight", 1.0),
    "sp_recovery_pct": ("skill_recovery_per_fight", 1.0),
}


def evaluate(
    item: Mapping[str, Any],
    offer: Offer,
    context: Context,
    weights: Weights = Weights(),
    catalog: Mapping[str, Any] | None = None,
    _parents: tuple[str, ...] = (),
    _check_capacity: bool = True,
) -> Evaluation:
    """Evaluate one observed offer, without issuing actions or mutating the context."""
    if str(item.get("id", "")) != offer.item_id:
        raise ValueError("Offer and catalog item IDs differ")
    if offer.item_id in _parents or len(_parents) >= 8:
        raise ValueError("Cyclic or excessively deep item bundle")
    features = item.get("features", {})
    if not isinstance(features, dict):
        raise ValueError("Catalog features must be an object")
    unknown = [f"unsupported:{value}" for value in item.get("unsupported_effects", [])]
    denials: list[str] = []
    components: dict[str, float] = {}
    costs: dict[str, float] = {}
    chosen: Candidate | None = None
    hope_cost = 0
    sale_growth = 0.0
    survival = 1.0
    liquidation_survival_known = True
    bundle_value = 0.0
    bundle_slots = 0
    bundle_acquisitions = 0
    if not offer.price_verified or offer.price is None:
        unknown.append("shop_price_unverified")
    if context.wallet is None:
        unknown.append("wallet_unverified")
    elif offer.price is not None and offer.price > max(
        0, context.wallet - context.reserve
    ):
        denials.append("insufficient_unreserved_ingots")
    if item.get("stackable") is False and context.held_counts.get(offer.item_id, 0) > 0:
        denials.append("non_stackable_duplicate")
    if offer.price is not None:
        costs["shop_price"] = offer.price * weights.cash

    def add(key: str, value: float):
        components[key] = components.get(key, 0.0) + value

    def horizon(key: str) -> float | None:
        value = getattr(context, key)
        if value is None:
            unknown.append(f"{key}_unverified")
        return value

    def urgency() -> float | None:
        if context.hp is None or context.max_hp is None:
            unknown.append("life_state_unverified")
            return None
        return 1 + weights.low_life_urgency * (1 - context.hp / max(1, context.max_hp))

    def resource(key: str, amount: float, multiplier: float = 1.0):
        if key == "immediate_ingots":
            add(key, amount * multiplier * weights.cash)
        elif key == "immediate_hope":
            add(key, amount * multiplier * weights.hope)
        elif key in ("heal_hp", "max_hp", "shield"):
            factor = urgency()
            if factor is None:
                return
            if key == "heal_hp":
                if amount < 0 and context.hp + amount <= 0:
                    denials.append("lethal_life_cost")
                actual = (
                    min(amount, context.max_hp - context.hp) if amount > 0 else amount
                )
                add(key, actual * multiplier * weights.heal_life * factor)
            elif key == "max_hp":
                if context.max_hp + amount < 1:
                    denials.append("lethal_max_life_cost")
                add(key, amount * multiplier * weights.max_life * factor)
            else:
                add(key, amount * multiplier * weights.shield * factor)

    def combat(key: str, amount: float, share: float = 1.0):
        if share == 0:
            return
        remaining = horizon("remaining_battles")
        if remaining is None:
            return
        coefficient, scale = COMBAT_FEATURES[key]
        baseline = (
            context.active_bonuses.get(key, 0.0) * scale
            if key
            in (
                "attack_pct",
                "defense_pct",
                "hp_pct",
                "attack_speed",
                "sp_recovery_pct",
            )
            else 0.0
        )
        # Relative marginal gain falls as equivalent bonuses are already held.
        add(
            key,
            amount
            * scale
            / (1 + baseline)
            * remaining
            * share
            * getattr(weights, coefficient),
        )

    metadata = {
        "recruit_role",
        "recruit_roles",
        "rarities",
        "extra_char_ids",
        "movement",
        "movement_shapes",
        "movement_charges",
        "slots",
        "scrap_slots",
        "role_buffs",
        "effects",
        "sale_value",
        "growth_per_acquisition",
        "movement_capacity",
        "bundle",
        "extra_hp_loss_per_leak",
    }
    direct = {"immediate_ingots", "immediate_hope", "heal_hp", "max_hp", "shield"}
    periodic = {
        "ingots_per_battle": ("remaining_battles", "immediate_ingots"),
        "hope_per_battle": ("remaining_battles", "immediate_hope"),
        "ingots_per_node": ("remaining_nodes", "immediate_ingots"),
        "ingots_per_zone": ("remaining_zones", "immediate_ingots"),
        "hope_per_zone": ("remaining_zones", "immediate_hope"),
    }
    for key, raw in features.items():
        if key in direct:
            resource(key, number(raw, key))
        elif key in COMBAT_FEATURES:
            combat(key, number(raw, key))
        elif key in periodic:
            attribute, target = periodic[key]
            count = horizon(attribute)
            if count is not None:
                resource(target, number(raw, key), count)
        elif key not in metadata:
            unknown.append(f"feature:{key}")

    for buff in features.get("role_buffs", []):
        role = role_name(str(buff.get("role", "")))
        if role not in context.role_weights:
            if role not in context.team_best_power:
                unknown.append(f"role_population_unverified:{role}")
                continue
            share = 0.0 if context.team_best_power[role] <= 0 else None
            if share is None:
                unknown.append(f"role_weight_unverified:{role}")
                continue
        else:
            share = context.role_weights[role]
        for key, amount in buff.get("features", {}).items():
            if key not in COMBAT_FEATURES:
                unknown.append(f"role_feature:{role}:{key}")
            else:
                combat(key, number(amount, key), share)

    raw_slots = number(
        features.get(
            "scrap_slots",
            features.get("slots", 1 if item.get("kind") == "scrap" else 0),
        ),
        "slots",
    )
    if raw_slots < 0 or not raw_slots.is_integer():
        raise ValueError("Item slot consumption must be a non-negative integer")
    own_slots = int(raw_slots)
    for bundle in features.get("bundle", []):
        count = number(bundle.get("count"), "bundle.count")
        if not count.is_integer() or not 1 <= count <= 1000:
            raise ValueError("Bundle count must be an integer in [1,1000]")
        count = int(count)
        bundle_acquisitions += count
        child_id = str(bundle.get("item_id", ""))
        if catalog is None or child_id not in catalog:
            unknown.append(f"bundle_item_unverified:{child_id}")
            continue
        child = evaluate(
            catalog[child_id],
            Offer(child_id, 0, True),
            context,
            weights,
            catalog,
            (*_parents, offer.item_id),
            False,
        )
        bundle_slots += child.movement_slots_required * count
        bundle_value += child.known_net_value * count
        unknown.extend(f"bundle:{child_id}:{reason}" for reason in child.unknown)
        denials.extend(f"bundle:{child_id}:{reason}" for reason in child.denials)
        if child.selected_candidate_id:
            unknown.append("bundle_recruitment_requires_separate_decision")
        if catalog[child_id].get("stackable") is False and count > 1:
            unknown.append("non_stackable_bundle_multiplicity")

    effects = features.get("effects", [])
    destroys_on_imperfect = any(
        effect.get("kind") == "destroy_self"
        and effect.get("trigger") == "imperfect_battle"
        for effect in effects
    )
    for effect_index, effect in enumerate(effects):
        kind = effect.get("kind")
        trigger = effect.get("trigger")
        params = effect.get("params", {})
        if (
            kind == "destroy_self"
            and trigger == "imperfect_battle"
            and not params
            and effect.get("amount") == 1
        ):
            remaining = horizon("remaining_battles")
            if context.perfect_battle_probability is None:
                unknown.append("perfect_battle_probability_unverified")
                liquidation_survival_known = False
            elif remaining is not None:
                # End-of-horizon liquidation requires surviving every battle.
                survival *= context.perfect_battle_probability**remaining
            else:
                liquidation_survival_known = False
            continue
        if params:
            key = f"{offer.item_id}:{effect_index}"
            count = context.conditional_trigger_counts.get(key)
            if count is None:
                unknown.append(f"conditional_trigger_unverified:{key}")
                continue
        elif trigger == "immediate":
            count = 1.0
        elif trigger in (
            "battle",
            "zone",
            "node",
            "reveal_node",
            "recruit",
            "acquire_scrap",
            "move",
        ):
            count = horizon(
                {
                    "battle": "remaining_battles",
                    "zone": "remaining_zones",
                    "node": "remaining_nodes",
                    "reveal_node": "expected_reveals",
                    "recruit": "expected_recruits",
                    "acquire_scrap": "expected_acquisitions",
                    "move": "expected_moves",
                }[trigger]
            )
            if count is not None and trigger == "acquire_scrap":
                count += bundle_acquisitions
        elif trigger == "perfect_battle":
            count = horizon("remaining_battles")
            if destroys_on_imperfect and context.perfect_battle_probability is None:
                continue
            if not destroys_on_imperfect:
                if context.perfect_battle_probability is None:
                    unknown.append("perfect_battle_probability_unverified")
                    continue
                if count is not None:
                    count *= context.perfect_battle_probability
        else:
            unknown.append(f"conditional_effect:{trigger}:{kind}")
            continue
        if count is not None:
            if kind == "sale_value":
                sale_growth += number(effect.get("amount"), "effect.amount") * count
                continue
            kind = {"hope": "immediate_hope", "ingots": "immediate_ingots"}.get(
                kind, kind
            )
            if kind == "action_points":
                if context.action_point_value is None:
                    unknown.append("action_point_value_unverified")
                else:
                    add(
                        "action_points",
                        number(effect.get("amount"), "effect.amount")
                        * count
                        * context.action_point_value,
                    )
                continue
            if kind not in direct:
                unknown.append(f"conditional_effect:{trigger}:{kind}")
                continue
            # Recurring heals cannot assume damage occurs before every payout.
            if kind in ("heal_hp", "max_hp", "shield") and trigger != "immediate":
                unknown.append(f"conditional_life_effect:{trigger}:{kind}")
            else:
                resource(kind, number(effect.get("amount"), "effect.amount"), count)

    slots = own_slots + bundle_slots
    if slots and _check_capacity:
        if context.movement_capacity is None or context.movement_used is None:
            unknown.append("movement_capacity_unverified")
        elif slots > context.movement_capacity - context.movement_used:
            denials.append("insufficient_movement_capacity")
    if own_slots and context.capacity_opportunity_cost is not None:
        # Child net values already include their own capacity opportunity cost.
        costs["capacity_opportunity"] = own_slots * context.capacity_opportunity_cost
    if "movement_capacity" in features:
        # Capacity's utility depends on the rejected alternatives, not a fixed per-slot tier.
        if context.capacity_opportunity_cost is None:
            unknown.append("capacity_opportunity_unverified")
        else:
            add(
                "movement_capacity",
                number(features["movement_capacity"], "movement_capacity")
                * context.capacity_opportunity_cost,
            )
    if any(
        key in features for key in ("movement", "movement_shapes", "movement_charges")
    ):
        if offer.item_id not in context.marginal_route_value:
            unknown.append("marginal_route_value_unverified")
        else:
            add("movement", context.marginal_route_value[offer.item_id])
    if "sale_value" in features or "growth_per_acquisition" in features:
        if (
            context.sale_value_to_ingots is None
            or context.liquidation_probability is None
        ):
            unknown.append("appraisal_liquidation_unverified")
        elif liquidation_survival_known:
            sale = number(features.get("sale_value", 0), "sale_value") + sale_growth
            if "growth_per_acquisition" in features:
                count = horizon("expected_acquisitions")
                if count is not None:
                    sale += (
                        number(
                            features["growth_per_acquisition"], "growth_per_acquisition"
                        )
                        * count
                    )
            # Selling and consuming the same item are mutually exclusive alternatives.
            liquidation = (
                sale
                * context.sale_value_to_ingots
                * context.liquidation_probability
                * survival
            )
            use_value = sum(components.values())
            add("liquidation_option", max(0.0, liquidation - use_value))
    if bundle_value:
        add("bundle_net_value", bundle_value)
    if "extra_hp_loss_per_leak" in features:
        count = horizon("expected_leaks")
        factor = urgency()
        if count is not None and factor is not None:
            loss = (
                number(features["extra_hp_loss_per_leak"], "extra_hp_loss_per_leak")
                * count
            )
            add("extra_leak_life_loss", -loss * weights.heal_life * factor)
            if loss >= context.hp:
                denials.append("expected_extra_life_loss_exceeds_buffer")

    if (
        item.get("kind") in ("recruit_voucher", "upgrade_voucher")
        or "recruit_role" in features
        or "recruit_roles" in features
    ):
        if not context.account_uid or not context.client:
            unknown.append("account_identity_unverified")
        roles = {role_name(str(role)) for role in features.get("recruit_roles", [])}
        if "recruit_role" in features:
            roles.add(role_name(str(features["recruit_role"])))
        if not roles:
            unknown.append("voucher_profession_unverified")
        rarities = {
            int(str(value).removeprefix("TIER_"))
            for value in features.get("rarities", [1, 2, 3, 4, 5, 6])
        }
        remaining = horizon("remaining_battles")
        eligible_candidates = []
        candidate_issues = set()
        for candidate in context.candidates:
            if item.get("kind") == "upgrade_voucher" and not candidate.promotion:
                continue
            role = role_name(candidate.role)
            if roles and "ALL" not in roles and role not in roles:
                continue
            if (
                candidate.rarity not in rarities
                or candidate.id in context.acquired_candidate_ids
            ):
                continue
            if (
                candidate.account_uid != context.account_uid
                or candidate.client != context.client
            ):
                candidate_issues.add("candidate_identity_mismatch")
                continue
            if (
                not candidate.owned
                or not candidate.trained
                or not candidate.evidence_verified
            ):
                continue
            if candidate.module_required and not (
                candidate.module_verified
                and candidate.module_ready
                and candidate.module_applies_to_result
            ):
                continue
            if candidate.hope_cost is None:
                candidate_issues.add("candidate_hope_cost_unverified")
                continue
            if context.hope is None and candidate.hope_cost:
                candidate_issues.add("current_hope_unverified")
                continue
            if candidate.hope_cost > (context.hope or 0):
                candidate_issues.add("insufficient_hope_for_useful_candidate")
                continue
            if remaining is None:
                continue
            if not candidate.promotion and role not in context.team_best_power:
                candidate_issues.add("team_profession_strength_unverified")
                continue
            baseline = (
                candidate.current_power
                if candidate.promotion
                else context.team_best_power.get(role, 0.0)
            )
            delta = max(0.0, candidate.power_after - baseline)
            needs = context.role_needs.get(role, 0.0)
            tags = sum(context.needed_tags.get(tag, 0.0) for tag in set(candidate.tags))
            gross = (
                (
                    delta
                    + weights.role_gap * needs * candidate.power_after
                    + weights.missing_tag * tags
                )
                * remaining
                * weights.recruitment_per_fight
            )
            hope_price = candidate.hope_cost * weights.hope
            if gross > hope_price:
                eligible_candidates.append(
                    (gross - hope_price, candidate.id, gross, candidate)
                )
        if eligible_candidates:
            _, _, gross, chosen = max(
                eligible_candidates, key=lambda value: (value[0], value[1])
            )
            add("recruitment", gross)
            hope_cost = chosen.hope_cost or 0
            costs["hope_opportunity"] = hope_cost * weights.hope
        else:
            if context.candidate_scan_complete:
                denials.append("no_useful_affordable_recruitment_or_promotion")
            else:
                unknown.append("candidate_opportunities_incomplete")
            unknown.extend(issue for issue in candidate_issues if "unverified" in issue)
            for issue in sorted(candidate_issues):
                if "unverified" in issue:
                    continue
                if (
                    context.candidate_scan_complete
                    or issue == "candidate_identity_mismatch"
                ):
                    denials.append(issue)
                else:
                    unknown.append("candidate_hope_coverage_unverified")

    # An immediate currency loss must also be affordable; utility cannot hide insolvency.
    currency_delta = number(features.get("immediate_ingots", 0), "immediate_ingots")
    if (
        context.wallet is not None
        and offer.price is not None
        and context.wallet - offer.price + currency_delta
        < min(context.wallet, context.reserve)
    ):
        denials.append("post_effect_budget_below_reserve")
    gross = sum(components.values())
    net = gross - sum(costs.values())
    unresolved = tuple(sorted(set(unknown)))
    rejected = tuple(sorted(set(denials)))
    eligible = not unresolved and not rejected
    decision = (
        "skip" if rejected else "review" if unresolved else "buy" if net > 0 else "skip"
    )
    fallback = context.legacy_advice.get(offer.item_id)
    valid_fallback = fallback is not None and fallback.verified
    if item.get("kind") in ("recruit_voucher", "upgrade_voucher") and fallback:
        valid_fallback = (
            valid_fallback
            and fallback.account_uid == context.account_uid
            and fallback.client == context.client
        )
    fallback_output = None
    if decision == "review" and valid_fallback:
        fallback_output = {
            **asdict(fallback),
            "requires_live_recheck": True,
            "new_model_validated": False,
        }
    return Evaluation(
        offer.item_id,
        decision,
        eligible,
        rejected,
        unresolved,
        components,
        gross,
        costs,
        net,
        (None, None) if unresolved else (net, net),
        "partial_unbounded" if unresolved else "modeled_heuristic",
        chosen.id if chosen else None,
        hope_cost,
        slots,
        fallback_output,
    )


def rank_offers(
    catalog: Mapping[str, Any],
    offers: list[Offer],
    context: Context,
    weights: Weights = Weights(),
) -> list[Evaluation]:
    """Rank the next purchase, not a simultaneous basket; re-evaluate after it.

    Unknowns cannot win with a large partial score. This method does not claim
    to solve the whole shop's budgeted portfolio or account for unobserved stock.
    """
    results = [
        evaluate(catalog[offer.item_id], offer, context, weights, catalog)
        for offer in offers
    ]
    order = {"buy": 0, "review": 1, "skip": 2}
    return sorted(
        results,
        key=lambda result: (
            order[result.decision],
            -result.known_net_value,
            result.item_id,
        ),
    )


def context_from_json(data: Mapping[str, Any]) -> Context:
    values = dict(data)
    values["candidates"] = tuple(
        Candidate(**{**candidate, "tags": tuple(candidate.get("tags", []))})
        for candidate in values.get("candidates", [])
    )
    values["acquired_candidate_ids"] = frozenset(
        values.get("acquired_candidate_ids", [])
    )
    values["legacy_advice"] = {
        key: LegacyAdvice(**advice)
        for key, advice in values.get("legacy_advice", {}).items()
    }
    for key in ("team_best_power", "role_needs", "role_weights"):
        if key in values:
            values[key] = {
                role_name(role): value for role, value in values[key].items()
            }
    return Context(**values)


def main() -> int:
    # Only the public file boundary requires a complete, fingerprinted sync
    # catalog. The evaluator above remains independent of the sync tool.
    from blackflow_catalog_sync import validate_catalog

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--context", type=Path, required=True)
    parser.add_argument("--offers", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    catalog = json.loads(args.catalog.read_text(encoding="utf-8"))
    validate_catalog(catalog)
    context = context_from_json(json.loads(args.context.read_text(encoding="utf-8")))
    offers = [
        Offer(**offer) for offer in json.loads(args.offers.read_text(encoding="utf-8"))
    ]
    results = rank_offers(catalog["items"], offers, context)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        json.dumps(
            {
                "mode": "offline_only",
                "not_a_clear_probability": True,
                "weights": asdict(Weights()),
                "results": [asdict(result) for result in results],
            },
            ensure_ascii=False,
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
