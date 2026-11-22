"""Pure offline geometry. Takes explicit segments, never infers paths from waypoints.

CLI: python blackflow_attack_hazard_audit.py input.json > result.json
The module itself writes no files and performs no network/device access.
"""

import json
import math
import sys
from typing import Any

EPSILON = 1e-9


def point(value: Any) -> tuple[float, float]:
    if not isinstance(value, (list, tuple)) or len(value) != 2:
        raise ValueError("A point must contain exactly two finite numbers")
    if any(
        isinstance(x, bool) or not isinstance(x, (int, float)) or not math.isfinite(x)
        for x in value
    ):
        raise ValueError("A point must contain exactly two finite numbers")
    return float(value[0]), float(value[1])


def nonnegative(value: Any, label: str) -> float:
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(value)
        or value < 0
    ):
        raise ValueError(f"{label} must be finite and nonnegative")
    return float(value)


def transformed_cells(offsets, deployment, direction):
    direction = direction.upper()
    if direction not in {"RIGHT", "LEFT", "UP", "DOWN"}:
        raise ValueError("direction must be RIGHT, LEFT, UP or DOWN")
    anchor = point(deployment)
    cells = set()
    for offset in offsets:
        x, y = point(offset)
        dx, dy = {"RIGHT": (x, y), "LEFT": (-x, -y), "UP": (y, -x), "DOWN": (-y, x)}[
            direction
        ]
        cells.add((anchor[0] + dx, anchor[1] + dy))
    if not cells:
        raise ValueError("range_offsets must not be empty")
    return sorted(cells)


def clipped_interval(start, end, cell, half_extent):
    lower, upper = 0.0, 1.0
    for axis in (0, 1):
        step = end[axis] - start[axis]
        cell_min = cell[axis] - half_extent
        cell_max = cell[axis] + half_extent
        if abs(step) <= EPSILON:
            if start[axis] < cell_min - EPSILON or start[axis] > cell_max + EPSILON:
                return None
            continue
        enter, leave = sorted(
            ((cell_min - start[axis]) / step, (cell_max - start[axis]) / step)
        )
        lower, upper = max(lower, enter), min(upper, leave)
        if lower > upper + EPSILON:
            return None
    return max(0.0, lower), min(1.0, upper)


def merged_intervals(intervals):
    result = []
    for lower, upper in sorted(intervals):
        if result and lower <= result[-1][1] + EPSILON:
            result[-1][1] = max(result[-1][1], upper)
        else:
            result.append([lower, upper])
    return result


def location_at(start, end, t):
    return [start[0] + (end[0] - start[0]) * t, start[1] + (end[1] - start[1]) * t]


def nearest_point(start, end, interval, target):
    dx, dy = end[0] - start[0], end[1] - start[1]
    length_squared = dx * dx + dy * dy
    t = (
        interval[0]
        if length_squared <= EPSILON
        else (
            ((target[0] - start[0]) * dx + (target[1] - start[1]) * dy) / length_squared
        )
    )
    t = max(interval[0], min(interval[1], t))
    nearest = location_at(start, end, t)
    return math.dist(nearest, target), nearest, t


def analyze(payload):
    battle_key = payload.get("battle_key")
    if not isinstance(battle_key, str) or not battle_key:
        raise ValueError(
            "battle_key is required; coordinates cannot establish battle identity"
        )
    attack = payload["attack"]
    deployment = point(attack["deployment"])
    cells = transformed_cells(attack["range_offsets"], deployment, attack["direction"])
    extent = nonnegative(attack.get("cell_half_extent", 0.5), "cell_half_extent")
    effects = attack.get("area_effects", [])
    parsed_effects = []
    for index, effect in enumerate(effects):
        origin = effect.get("origin", "attack_target")
        if origin not in {"attack_target", "deployment"}:
            raise ValueError("area_effect origin must be attack_target or deployment")
        parsed_effects.append(
            {
                **effect,
                "id": effect.get("id", str(index)),
                "origin": origin,
                "radius": nonnegative(effect["radius"], "effect radius"),
            }
        )
    unknown = []
    modeled_segments = []
    omitted_segments = []
    for index, segment in enumerate(payload.get("segments", [])):
        identity = segment.get("id", str(index))
        kind = segment.get("kind", "unknown")
        source = segment.get("source")
        if not source:
            unknown.append(
                {"segment_id": identity, "reason": "missing_segment_provenance"}
            )
        if segment.get("battle_key", battle_key) != battle_key:
            omitted_segments.append(
                {"id": identity, "reason": "different_battle", "source": source}
            )
            unknown.append({"segment_id": identity, "reason": "different_battle"})
            continue
        if kind not in {"confirmed_linear", "provisional_chord", "confirmed_hold"}:
            omitted_segments.append(
                {"id": identity, "reason": f"not_interpolated:{kind}", "source": source}
            )
            if kind != "disappear":
                unknown.append(
                    {"segment_id": identity, "reason": f"unmodeled_path:{kind}"}
                )
            continue
        if kind == "provisional_chord":
            unknown.append(
                {"segment_id": identity, "reason": "script_chord_is_not_engine_path"}
            )
        start, end = (
            point(segment["start"]),
            point(segment.get("end", segment["start"])),
        )
        if kind == "confirmed_hold" and start != end:
            raise ValueError("confirmed_hold must have identical start/end")
        intervals = merged_intervals(
            [
                interval
                for cell in cells
                if (interval := clipped_interval(start, end, cell, extent)) is not None
            ]
        )
        segment_length = math.dist(start, end)
        modeled_segments.append(
            {
                "id": identity,
                "kind": kind,
                "start": start,
                "end": end,
                "source": source,
                "covered_t_intervals": intervals,
                "covered_subsegments": [
                    [location_at(start, end, a), location_at(start, end, b)]
                    for a, b in intervals
                ],
                "covered_length": sum((b - a) * segment_length for a, b in intervals),
                "point_covered": bool(intervals) if segment_length <= EPSILON else None,
            }
        )
    if not modeled_segments:
        unknown.append({"reason": "no_modeled_continuous_segments"})
    hazards = []
    excluded_observations = []
    for index, hazard in enumerate(payload.get("hazards", [])):
        identity = hazard.get("id", str(index))
        if hazard.get("battle_key") != battle_key:
            excluded_observations.append(
                {
                    "id": identity,
                    "reason": "different_or_missing_battle_key",
                    "original_battle_key": hazard.get("battle_key"),
                    "source": hazard.get("source"),
                }
            )
            unknown.append(
                {
                    "hazard_id": identity,
                    "reason": "classification_not_inherited_across_battles",
                }
            )
            continue
        position = point(hazard["position"])
        classification = hazard.get("classification", "unknown")
        known = hazard.get(
            "classification_status"
        ) == "confirmed" and classification in {"reflective", "non_reflective"}
        current = hazard.get("position_status") == "confirmed_current"
        if not known or not current:
            unknown.append(
                {
                    "hazard_id": identity,
                    "reason": "classification_or_current_position_unconfirmed",
                }
            )
        if not hazard.get("source"):
            unknown.append(
                {"hazard_id": identity, "reason": "missing_hazard_provenance"}
            )
        directly_selected = any(
            abs(position[0] - c[0]) <= extent + EPSILON
            and abs(position[1] - c[1]) <= extent + EPSILON
            for c in cells
        )
        effect_results = []
        for effect in parsed_effects:
            witness = None
            minimum = None
            if effect["origin"] == "deployment":
                minimum = math.dist(deployment, position)
                witness = {
                    "origin": deployment,
                    "activation_confirmed": False,
                    "note": "geometric potential only; caller must prove trigger/lifecycle",
                }
            else:
                for segment in modeled_segments:
                    for interval in segment["covered_t_intervals"]:
                        distance, nearest, t = nearest_point(
                            segment["start"], segment["end"], interval, position
                        )
                        if minimum is None or distance < minimum:
                            minimum = distance
                            witness = {
                                "segment_id": segment["id"],
                                "segment_kind": segment["kind"],
                                "target_point": nearest,
                                "t": t,
                                "source": segment["source"],
                            }
            effect_results.append(
                {
                    "id": effect["id"],
                    "origin": effect["origin"],
                    "radius": effect["radius"],
                    "minimum_distance": minimum,
                    "can_geometrically_intersect": minimum is not None
                    and minimum <= effect["radius"] + EPSILON,
                    "margin": None if minimum is None else minimum - effect["radius"],
                    "witness": witness,
                    "source": effect.get("source"),
                }
            )
        potential_contact = directly_selected or any(
            e["can_geometrically_intersect"] for e in effect_results
        )
        hazards.append(
            {
                "id": identity,
                "battle_key": battle_key,
                "position": position,
                "classification": classification,
                "classification_confirmed": known,
                "position_current_confirmed": current,
                "directly_in_attack_cell_union": directly_selected,
                "effects": effect_results,
                "potential_geometric_contact": potential_contact,
                "confirmed_reflective_object_contact_in_model": potential_contact
                and known
                and current
                and bool(hazard.get("source"))
                and classification == "reflective",
                "source": hazard.get("source"),
            }
        )
    if not hazards:
        unknown.append({"reason": "no_current_battle_hazard_observations"})
    return {
        "battle_key": battle_key,
        "offline_metadata_only": True,
        "live_observation": False,
        "livepass": False,
        "model": {
            "range": "union_of_axis_aligned_attack_cells",
            "cell_half_extent": extent,
            "effect_test": "distance_to_continuously_covered_target_centers; closed boundaries",
            "path_policy": "explicit segments only; never auto-connect raw waypoints or disappear endpoints",
        },
        "attack_source": attack.get("source"),
        "coverage_cells": cells,
        "segments": modeled_segments,
        "omitted_segments": omitted_segments,
        "hazards": hazards,
        "excluded_observations": excluded_observations,
        "unknown_boundaries": unknown,
        "global_limitations": [
            "No engine collision-body or pathfinding parity",
            "No target priority, HP, DPS or survival proof",
            "No chained effects unless separately supplied as an explicit model",
            "Current positions/classifications and effect triggers are caller-supplied evidence, not recognition",
            "No conclusion that all hazards were observed or all routes supplied",
        ],
    }


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python blackflow_attack_hazard_audit.py input.json")
    with open(sys.argv[1], encoding="utf-8-sig") as handle:
        payload = json.load(handle)
    print(json.dumps(analyze(payload), ensure_ascii=False, indent=2))
