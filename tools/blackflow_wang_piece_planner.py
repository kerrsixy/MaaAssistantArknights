"""Offline Wang piece observation ledger and coverage review.

This module has no detector, controller, timer-based deployment script or live
integration. Callers must supply current, map-calibrated evidence. An omitted
tile is not an empty tile. Piece disappearance, bonus-piece locations, movement,
facing and lifetime are never inferred from a click or elapsed time.

S2 damage geometry follows the in-game 连星 description: three tiles on either
side along the established link axis. S3 footprints must be supplied from
verified skill-specific evidence; the owner's basic range is not substituted.
The user reference BV16HKn6gEN1 p12 at 103s demonstrates that a small enemy can
trigger pieces whose damage hits a different target. p15/p16 also require a
separate bait/kill-window protocol that this module does not execute or certify.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Literal

Tile = tuple[int, int]
Occupancy = Literal["piece", "empty", "other", "unknown"]


def _time(value: float) -> None:
    if isinstance(value, bool) or not math.isfinite(value) or value < 0:
        raise ValueError("Observation time must be finite and nonnegative")


@dataclass(frozen=True)
class Effect:
    """One piece's verified trigger and damage cells for the selected skill.

    A placement option may also activate an already observed adjacent piece.
    Those effects are explicit; unknown automatic extra pieces are not added.
    Cells are native map coordinates, not screen pixels or owner attack range.
    """

    origin: Tile
    skill: int
    trigger_cells: frozenset[Tile]
    damage_cells: frozenset[Tile]
    source: str

    def __post_init__(self) -> None:
        if self.skill not in (2, 3) or not self.source.strip():
            raise ValueError("A supported skill and geometry source are required")
        if not self.trigger_cells or not self.damage_cells:
            raise ValueError("Trigger and damage evidence must both be supplied")


def s2_effect(origin: Tile, axes: frozenset[str], source: str) -> Effect:
    """Model confirmed link axes; this does not detect links or choose facing."""
    if not axes or not axes <= {"horizontal", "vertical"}:
        raise ValueError("S2 requires explicitly established link axes")
    x, y = origin
    damage = {origin}
    for distance in range(-3, 4):
        if "horizontal" in axes:
            damage.add((x + distance, y))
        if "vertical" in axes:
            damage.add((x, y + distance))
    return Effect(origin, 2, frozenset({origin}), frozenset(damage), source)


@dataclass(frozen=True)
class CellObservation:
    tile: Tile
    occupancy: Occupancy
    active: bool | None = None
    effect: Effect | None = None

    def __post_init__(self) -> None:
        if self.occupancy not in {"piece", "empty", "other", "unknown"}:
            raise ValueError("Unknown occupancy category")
        if self.occupancy != "piece" and (
            self.active is not None or self.effect is not None
        ):
            raise ValueError("Non-piece observations cannot carry piece geometry")
        if self.effect is not None and self.effect.origin != self.tile:
            raise ValueError("Geometry origin differs from observed piece tile")


@dataclass(frozen=True)
class CellState:
    observation: CellObservation
    observed_at: float
    piece_instance: int | None


@dataclass
class PieceLedger:
    """Per-tile identities for simultaneous same-name pieces.

    Identity lasts until explicit absence/replacement evidence. An occluded
    cell retains its last identity but contributes no current coverage. A new
    piece after a confirmed empty cell receives a new identity.
    """

    cells: dict[Tile, CellState] = field(default_factory=dict)
    observed_at: float = 0
    _next_instance: int = 1

    def observe(self, at: float, observations: tuple[CellObservation, ...]) -> None:
        _time(at)
        if at < self.observed_at:
            raise ValueError("Out-of-order observation frame")
        tiles = [item.tile for item in observations]
        if len(tiles) != len(set(tiles)):
            raise ValueError("Conflicting observations of one tile in a frame")
        for item in observations:
            previous = self.cells.get(item.tile)
            identity = previous.piece_instance if previous else None
            if item.occupancy == "piece" and identity is None:
                identity = self._next_instance
                self._next_instance += 1
            elif item.occupancy not in {"piece", "unknown"}:
                identity = None
            self.cells[item.tile] = CellState(item, at, identity)
        self.observed_at = at


@dataclass(frozen=True)
class Enemy:
    identity: str
    tile: Tile
    priority: float
    observed_at: float
    targetable: bool | None = True

    def __post_init__(self) -> None:
        _time(self.observed_at)
        if not self.identity or not math.isfinite(self.priority) or self.priority <= 0:
            raise ValueError("Enemy identity and positive finite priority required")


@dataclass(frozen=True)
class RunState:
    """This run's observed form/resources; account maximum rank is not an input."""

    observed_at: float
    phase: int | None
    skill: int | None
    owner_present: bool | None
    stock: int | None
    deployment_cost: int | None
    available_cost: int | None
    card_ready: bool | None
    skill_ready: bool | None = None

    def __post_init__(self) -> None:
        _time(self.observed_at)
        for value in (self.stock, self.deployment_cost, self.available_cost):
            if value is not None and (type(value) is not int or value < 0):
                raise ValueError("Observed resources must be nonnegative integers")
        if self.phase is not None and (
            type(self.phase) is not int or self.phase not in (0, 1, 2)
        ):
            raise ValueError("Phase must be this run's E0/E1/E2 or unknown")


@dataclass(frozen=True)
class Placement:
    """A conditional option, using the existing executor's legality checks.

    legal is supplied by the owning map/deployment validation, not recomputed
    here. effects must describe the proposed main piece and/or existing pieces
    activated by it. They cannot invent unseen talent-generated pieces.
    """

    tile: Tile
    effects: tuple[Effect, ...]
    observed_at: float
    legal: bool | None
    activation_supported: bool | None
    source: str

    def __post_init__(self) -> None:
        _time(self.observed_at)
        if not self.effects or not self.source.strip():
            raise ValueError("Placement requires explicit effect and evidence")
        origins = [effect.origin for effect in self.effects]
        if len(origins) != len(set(origins)):
            raise ValueError("One effect per piece origin is required")


@dataclass(frozen=True)
class Coverage:
    trigger_enemies: frozenset[str]
    targets: frozenset[str]


def coverage(effect: Effect, enemies: tuple[Enemy, ...]) -> Coverage:
    """Report geometric eligibility, not hit/kill confirmation or HP prediction."""
    usable = tuple(enemy for enemy in enemies if enemy.targetable is True)
    triggers = frozenset(
        enemy.identity for enemy in usable if enemy.tile in effect.trigger_cells
    )
    targets = frozenset(
        enemy.identity for enemy in usable if enemy.tile in effect.damage_cells
    )
    return Coverage(triggers, targets if triggers else frozenset())


@dataclass(frozen=True)
class Review:
    outcome: Literal["observe", "wait", "placement_candidate"]
    reason: str
    tile: Tile | None = None
    marginal_targets: frozenset[str] = frozenset()
    trigger_enemies: frozenset[str] = frozenset()
    covered_targets: frozenset[str] = frozenset()
    score: float = 0
    skill_advice: str = "none"
    # A coverage candidate is never an instruction to the live input controller.
    requires_deployment_verification: bool = True


def review_placement(
    ledger: PieceLedger,
    run: RunState,
    enemies: tuple[Enemy, ...],
    options: tuple[Placement, ...],
    *,
    now: float,
    max_age: float,
) -> Review:
    """Re-rank current threats after each observed piece appearance/disappearance.

    No path is forecast from a title, video timestamp or random enemy motion.
    No trigger means no current damage credit, even if the damage area contains
    an urgent enemy. All inputs must describe one calibrated map/skill state.
    """
    _time(now)
    if isinstance(max_age, bool) or not math.isfinite(max_age) or max_age <= 0:
        raise ValueError("max_age must be positive and finite")

    def fresh(at: float) -> bool:
        return 0 <= now - at <= max_age

    if len({enemy.identity for enemy in enemies}) != len(enemies):
        raise ValueError("Enemy identities must be unique")
    if not fresh(run.observed_at) or not fresh(ledger.observed_at):
        return Review("observe", "stale_run_or_scene")
    if run.owner_present is not True or run.phase is None or run.skill is None:
        return Review("observe", "run_form_or_owner_unknown")
    if run.skill not in (2, 3) or run.phase < (1 if run.skill == 2 else 2):
        return Review("observe", "selected_skill_unavailable_in_this_run")
    if any(not fresh(enemy.observed_at) for enemy in enemies):
        return Review("observe", "stale_enemy_evidence")
    if any(enemy.targetable is None for enemy in enemies):
        return Review("observe", "enemy_state_unknown")
    if not enemies:
        return Review("wait", "no_observed_enemies")
    if run.stock is None or run.deployment_cost is None or run.available_cost is None:
        return Review("observe", "piece_resources_unknown")
    if run.stock == 0:
        advice = "await_automatic_s2_replenishment"
        if run.skill == 3:
            advice = (
                "review_s3_then_observe_spawned_pieces"
                if run.skill_ready is True
                else "observe_s3_readiness"
            )
        return Review("wait", "no_piece_stock", skill_advice=advice)
    if run.card_ready is not True:
        return Review("observe", "piece_card_not_verified_ready")
    if run.available_cost < run.deployment_cost:
        return Review("wait", "insufficient_observed_cost")

    present = {
        tile: state
        for tile, state in ledger.cells.items()
        if state.observation.occupancy == "piece" and fresh(state.observed_at)
    }
    already_covered: set[str] = set()
    for state in present.values():
        item = state.observation
        if item.active is True and item.effect and item.effect.skill == run.skill:
            already_covered.update(coverage(item.effect, enemies).targets)

    weights = {enemy.identity: enemy.priority for enemy in enemies}
    ranked: list[Review] = []
    for option in options:
        cell = ledger.cells.get(option.tile)
        if (
            not fresh(option.observed_at)
            or option.legal is not True
            or option.activation_supported is not True
            or cell is None
            or not fresh(cell.observed_at)
            or cell.observation.occupancy != "empty"
        ):
            continue
        if any(effect.skill != run.skill for effect in option.effects):
            continue
        if any(
            effect.origin != option.tile and effect.origin not in present
            for effect in option.effects
        ):
            # An unobserved extra piece is not a promised damage entity.
            continue
        targets: set[str] = set()
        triggers: set[str] = set()
        for effect in option.effects:
            result = coverage(effect, enemies)
            targets.update(result.targets)
            triggers.update(result.trigger_enemies)
        marginal = targets - already_covered
        if marginal:
            ranked.append(
                Review(
                    "placement_candidate",
                    "current_trigger_and_marginal_coverage",
                    option.tile,
                    frozenset(marginal),
                    frozenset(triggers),
                    frozenset(already_covered),
                    sum(weights[identity] for identity in marginal),
                )
            )
    if not ranked:
        return Review(
            "observe",
            "no_verified_incremental_triggered_placement",
            covered_targets=frozenset(already_covered),
        )
    return min(ranked, key=lambda result: (-result.score, result.tile))
