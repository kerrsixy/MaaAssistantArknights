#pragma once

#include "SPADeploymentReview.h"
#include "SPAPlacementPolicy.h"
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <tuple>

namespace asst::spa
{
inline std::optional<int> fear_trap_charge_count(bool button_ready, int maximum, std::optional<int> badge)
{
    if (!button_ready || maximum == 1) {
        return button_ready ? 1 : 0;
    }
    // The drained portrait can still pass the green-pixel check even when
    // the game draws a clear zero. Use its observed count.
    return badge && *badge >= 0 && *badge <= maximum ? badge : std::nullopt;
}

// Prefer a route before first contact, then current fire support, lane
// coverage and proximity to the blocker. These are predicted encounters.
inline std::optional<std::tuple<int, int, int, int>> fear_trap_route_priority(
    BoardCell cell,
    const std::vector<DeploymentLane>& lanes,
    const std::vector<DeploymentUnit>& field,
    const std::set<BoardCell>& existing_traps)
{
    if (existing_traps.contains(cell) ||
        std::ranges::any_of(field, [&](const auto& unit) { return unit.cell == cell; })) {
        return std::nullopt;
    }
    int before_contact = 0, on_route = 0, contact_approach = 0;
    for (const auto& lane : lanes) {
        const auto at = std::ranges::find(lane.path, cell);
        if (at == lane.path.end() || lane.blocking_weight <= 0) {
            continue;
        }
        const int covered = static_cast<int>(std::ranges::count_if(lane.path, [&](const auto& step) {
            return existing_traps.contains(step);
        }));
        const int weight = std::max(1, lane.blocking_weight * 100 / (covered + 1));
        on_route += weight;
        const auto contact = std::ranges::find_if(lane.path, [&](const auto& step) {
            return std::ranges::any_of(field, [&](const auto& unit) {
                return unit.cell == step && unit.frontline && unit.holds_position && !unit.healer &&
                       unit.block_count > 0;
            });
        });
        if (at < contact) {
            before_contact += weight;
            if (contact != lane.path.end()) {
                contact_approach += std::max(0, lane.precontact_cells + 1 - static_cast<int>(contact - at));
            }
        }
    }
    if (on_route == 0) {
        return std::nullopt;
    }
    const int attackers = static_cast<int>(std::ranges::count_if(field, [&](const auto& unit) {
        return unit.damage_output && !unit.healer && range_covers(unit, cell);
    }));
    return std::tuple(before_contact, attackers, on_route, contact_approach);
}

// The first shared non-objective cell of predicted routes is a staging
// anchor, not proof of an individual enemy's path or an executable target.
inline std::optional<BoardCell> engagement_junction(const std::vector<DeploymentLane>& lanes)
{
    std::map<BoardCell, std::pair<int, int>> shared;
    for (const auto& lane : lanes) {
        std::set<BoardCell> seen;
        for (size_t index = 0; index + 1 < lane.path.size(); ++index) {
            const auto cell = lane.path[index];
            if (!seen.insert(cell).second || cell.first < lane.minimum_engagement_column) {
                continue;
            }
            ++shared[cell].first;
            shared[cell].second += static_cast<int>(index);
        }
    }
    std::optional<BoardCell> result;
    int members = 1, progress = std::numeric_limits<int>::max();
    for (const auto& [cell, counts] : shared) {
        if (counts.first > members || (counts.first == members && counts.first >= 2 && counts.second < progress)) {
            result = cell;
            members = counts.first;
            progress = counts.second;
        }
    }
    return result;
}

inline int engagement_position_value(
    PlacementRole role,
    BoardCell cell,
    char facing,
    const std::vector<BoardCell>& attack_range,
    const std::vector<DeploymentLane>& lanes)
{
    const auto junction = engagement_junction(lanes);
    if (!junction) {
        return 0;
    }
    const int distance = std::abs(cell.first - junction->first) + std::abs(cell.second - junction->second);
    if (role == PlacementRole::Frontline) {
        const bool on_route = std::any_of(lanes.begin(), lanes.end(), [&](const auto& lane) {
            return std::find(lane.path.begin(), lane.path.end(), cell) != lane.path.end();
        });
        return on_route ? std::max(0, 6000 - distance * 800) : 0;
    }
    if (role != PlacementRole::Ranged || attack_range.empty() ||
        std::string("ENSW").find(facing) == std::string::npos) {
        return 0;
    }
    DeploymentUnit output;
    output.cell = cell;
    output.facing = facing;
    output.range_known = true;
    output.local_range = attack_range;
    int covered_lanes = 0, covered_cells = 0;
    for (const auto& lane : lanes) {
        const auto merge = std::find(lane.path.begin(), lane.path.end(), *junction);
        if (merge == lane.path.end()) {
            continue;
        }
        const auto end = static_cast<size_t>(merge - lane.path.begin());
        int covered = 0;
        // Include the two approach cells so merely pointing at an empty
        // downstream corridor cannot substitute for two-lane engagement.
        for (size_t index = end > 2 ? end - 2 : 0; index <= end; ++index) {
            covered += range_covers(output, lane.path[index]);
        }
        covered_lanes += covered > 0;
        covered_cells += covered;
    }
    return covered_lanes * 2500 + covered_cells * 200 - (covered_lanes > 0 ? distance * 50 : 0);
}

inline std::vector<PlacementPreference> engagement_placement_preferences(
    PlacementRole role,
    const std::vector<GridPlacementCell>& cells,
    const std::vector<DeploymentLane>& lanes,
    const std::vector<BoardCell>& attack_range)
{
    std::vector<PlacementPreference> result;
    for (const auto& cell : cells) {
        for (char facing : { 'E', 'N', 'S', 'W' }) {
            const int value = engagement_position_value(role, { cell.col, cell.row }, facing, attack_range, lanes);
            if (value > 0) {
                result.push_back({ cell.screen_x, cell.screen_y, facing, value });
            }
        }
    }
    return result;
}

inline int attack_approach_value(
    BoardCell origin,
    char facing,
    const std::vector<GridPlacementMember>& placed,
    const std::vector<DeploymentLane>& lanes,
    const std::vector<BoardCell>& attack_range,
    bool intercepts)
{
    if (attack_range.empty() || std::string_view("ENSW").find(facing) == std::string_view::npos) {
        return 0;
    }
    int score = 0;
    for (const auto& lane : lanes) {
        const auto contact = std::find_if(lane.path.begin(), lane.path.end(), [&](const BoardCell& step) {
            return (intercepts && step == origin) || std::any_of(placed.begin(), placed.end(), [&](const auto& member) {
                       return member.role == PlacementRole::Frontline && step == BoardCell { member.col, member.row };
                   });
        });
        if (contact == lane.path.end()) {
            for (size_t index = 0; index + 1 < lane.path.size(); ++index) {
                const auto& step = lane.path[index];
                if (step != origin &&
                    grid_range_covers(origin.first, origin.second, facing, step.first, step.second, attack_range)) {
                    score += std::max(150, 500 - static_cast<int>(index) * 30);
                }
            }
            continue;
        }
        for (auto step = contact; contact - step <= 3; --step) {
            if (*step != origin &&
                grid_range_covers(origin.first, origin.second, facing, step->first, step->second, attack_range)) {
                score += 900 + (3 - static_cast<int>(contact - step)) * 200;
            }
            if (step == lane.path.begin()) {
                break;
            }
        }
    }
    return score;
}

inline std::vector<PlacementPreference> attack_approach_placement_preferences(
    const std::vector<GridPlacementCell>& legal,
    const std::vector<GridPlacementMember>& placed,
    const std::vector<DeploymentLane>& lanes,
    const std::vector<BoardCell>& attack_range,
    bool intercepts)
{
    std::vector<PlacementPreference> preferences;
    for (const auto& cell : legal) {
        for (const char facing : { 'E', 'N', 'S', 'W' }) {
            const int value =
                attack_approach_value({ cell.col, cell.row }, facing, placed, lanes, attack_range, intercepts);
            if (value > 0) {
                preferences.push_back({ cell.screen_x, cell.screen_y, facing, value });
            }
        }
    }
    return preferences;
}

inline bool uses_attack_approach_geometry(std::string_view name)
{
    return name == "灰喉" || name == "矩" || name == "拉普兰德" || name == "荒芜拉普兰德";
}
}
