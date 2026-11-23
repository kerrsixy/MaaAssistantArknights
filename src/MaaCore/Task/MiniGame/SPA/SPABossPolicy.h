#pragma once

#include "SPABossData.h"
#include "SPADeploymentReview.h"
#include "SPAObservation.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <set>

namespace asst::spa
{
inline std::string normalize_boss_family(std::string value)
{
    for (const auto& token : { " ", "\t", "\r", "\n", "“", "”", "｢", "｣", "，", "," }) {
        std::size_t at = 0;
        while ((at = value.find(token, at)) != std::string::npos) {
            value.erase(at, std::string_view(token).size());
        }
    }
    for (std::size_t at = 0; (at = value.find("：", at)) != std::string::npos; ++at) {
        value.replace(at, std::string_view("：").size(), ":");
    }
    return value;
}

inline std::optional<std::string> advertised_boss_family(const std::vector<TextObservation>& texts)
{
    std::vector<TextObservation> names;
    for (const auto& text : texts) {
        if (std::isfinite(text.score) && text.score >= .90 && text.x >= 900 && text.x < 1280 && text.y >= 310 &&
            text.y < 430) {
            names.push_back(text);
        }
    }
    std::stable_sort(names.begin(), names.end(), [](const auto& left, const auto& right) {
        return std::pair(left.y, left.x) < std::pair(right.y, right.x);
    });
    std::string joined;
    for (const auto& name : names) {
        joined += normalize_boss_family(name.text);
    }
    std::set<std::string> found;
    for (const auto& boss : SeasonBossCatalogue) {
        if (joined.find(normalize_boss_family(std::string(boss.family_name))) != std::string::npos) {
            found.emplace(boss.family_name);
        }
    }
    return found.size() == 1 ? std::optional(*found.begin()) : std::nullopt;
}

struct BossObjective
{
    bool known = false;
    bool stationary_route = false;
    bool variant_known = false;
    std::vector<BoardCell> possible_cells;
    std::vector<std::string> possible_enemy_ids;
    int maximum_declared_hp = 0;
    double time_limit_seconds = 0;
    std::vector<BoardCell> movement_waypoints;
};

struct BossFamilyMobility
{
    bool known = false;
    bool stationary_route = false;
};

// Family classification only. It establishes neither the current variant,
// difficulty, health nor a screen/grid location.
inline BossFamilyMobility boss_family_mobility(
    const std::string& family,
    std::optional<bool> hidden_variant = std::nullopt)
{
    BossFamilyMobility result;
    bool all_stationary = true;
    for (const auto& boss : SeasonBossCatalogue) {
        if (boss.family_name == family && (!hidden_variant || boss.hidden == *hidden_variant)) {
            result.known = true;
            all_stationary = all_stationary && boss.stationary_route;
        }
    }
    result.stationary_route = result.known && all_stationary;
    return result;
}

// Measured preparation tiles describe the opening target even when the
// boss later moves. They do not establish its route or sustained uptime.
inline std::vector<BoardCell> verified_boss_opening_region(
    bool current_boss_phase,
    const std::string& map_id,
    int round,
    const std::string& observed_map_id,
    int observed_round,
    const std::vector<BoardCell>& observed_cells)
{
    if (!current_boss_phase || map_id.empty() || map_id != observed_map_id || round < 1 || round > 20 ||
        round != observed_round || observed_cells.empty() || observed_cells.size() > 55 ||
        std::any_of(observed_cells.begin(), observed_cells.end(), [](BoardCell cell) {
            return cell.first < 0 || cell.first > 12 || cell.second < 0 || cell.second > 4;
        })) {
        return {};
    }
    auto cells = observed_cells;
    std::sort(cells.begin(), cells.end());
    cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
    return cells;
}

inline std::vector<BoardCell> verified_stationary_boss_region(
    const BossFamilyMobility& mobility,
    bool current_boss_phase,
    const std::string& map_id,
    int round,
    const std::string& observed_map_id,
    int observed_round,
    const std::vector<BoardCell>& observed_cells)
{
    return mobility.known && mobility.stationary_route ? verified_boss_opening_region(
                                                             current_boss_phase,
                                                             map_id,
                                                             round,
                                                             observed_map_id,
                                                             observed_round,
                                                             observed_cells)
                                                       : std::vector<BoardCell> {};
}

inline bool known_boss_family(const std::string& family)
{
    return std::any_of(SeasonBossCatalogue.begin(), SeasonBossCatalogue.end(), [&](const auto& boss) {
        return boss.family_name == family;
    });
}

inline bool scheduled_independent_boss_preparation(
    std::string_view mode,
    int observed_round,
    int scheduled_round,
    int difficulty,
    std::string_view verified_family)
{
    return mode == "independent" && observed_round >= 2 && observed_round <= 20 &&
           observed_round == scheduled_round && difficulty >= 1 && difficulty <= 20 &&
           std::any_of(SeasonBossCatalogue.begin(), SeasonBossCatalogue.end(), [&](const auto& boss) {
               return boss.family_name == verified_family && boss.hp_by_difficulty[difficulty - 1] > 0;
           });
}

// Bind a catalogue variant to a recognized solo phase, never to a numeric
// late round alone. The observed region remains the opening-position proof.
inline std::optional<bool> independent_boss_variant_for_preparation(
    std::string_view mode,
    int round,
    int scheduled_boss_round,
    int difficulty,
    bool bonus_round,
    std::string_view round_source)
{
    if (mode != "independent" || difficulty < 1 || difficulty > 20) {
        return std::nullopt;
    }
    if (round == HiddenCoreRound && bonus_round && difficulty >= 5 &&
        round_source == "independent_hidden_core_phase") {
        return true;
    }
    if (!bonus_round && round >= 2 && round < HiddenCoreRound && round == scheduled_boss_round) {
        return false;
    }
    return std::nullopt;
}

inline BossObjective boss_objective(
    const std::string& family,
    BoardCell registered_home,
    int difficulty,
    std::optional<bool> hidden_variant = std::nullopt)
{
    BossObjective result;
    if (family.empty() || difficulty < 1 || difficulty > 20) {
        return result;
    }
    std::set<BoardCell> cells;
    bool all_stationary = true;
    std::optional<std::vector<BoardCell>> shared_waypoints;
    for (const auto& boss : SeasonBossCatalogue) {
        if (boss.family_name != family || (hidden_variant && boss.hidden != *hidden_variant) ||
            boss.hp_by_difficulty[difficulty - 1] <= 0) {
            continue;
        }
        result.possible_enemy_ids.emplace_back(boss.enemy_id);
        all_stationary = all_stationary && boss.stationary_route;
        // Both are client coordinates with rows increasing upwards. The
        // registered deployment grid instead increases rows downwards.
        const auto translate = [&](int col, int row) {
            return BoardCell { registered_home.first + col - boss.template_home_col,
                               registered_home.second + boss.template_home_row - row };
        };
        cells.insert(translate(boss.spawn_col, boss.spawn_row));
        std::vector<BoardCell> waypoints;
        for (int index = 0; index < boss.waypoint_count; ++index) {
            const auto& point = boss.movement_waypoints[index];
            const auto cell = translate(point.col, point.row);
            if (cell != registered_home && cell.first >= 0 && cell.first <= 12 && cell.second >= 0 &&
                cell.second <= 4 && std::find(waypoints.begin(), waypoints.end(), cell) == waypoints.end()) {
                waypoints.push_back(cell);
            }
        }
        if (!shared_waypoints) {
            shared_waypoints = std::move(waypoints);
        }
        else if (*shared_waypoints != waypoints) {
            shared_waypoints->clear();
        }
        result.maximum_declared_hp = std::max(result.maximum_declared_hp, boss.hp_by_difficulty[difficulty - 1]);
        result.time_limit_seconds = std::max(result.time_limit_seconds, boss.time_limit_seconds);
    }
    result.known = !result.possible_enemy_ids.empty();
    result.stationary_route = result.known && all_stationary;
    result.variant_known = result.possible_enemy_ids.size() == 1;
    result.possible_cells.assign(cells.begin(), cells.end());
    if (!result.stationary_route && shared_waypoints) {
        result.movement_waypoints = std::move(*shared_waypoints);
    }
    return result;
}

inline std::vector<BoardCell> estimate_five_by_three_boss_footprint(BoardCell center)
{
    const auto [center_col, center_row] = center;
    if (center_col < 2 || center_col > 10 || center_row < 1 || center_row > 3) {
        return {};
    }
    std::vector<BoardCell> cells;
    for (int row = center_row - 1; row <= center_row + 1; ++row) {
        for (int col = center_col - 2; col <= center_col + 2; ++col) {
            cells.emplace_back(col, row);
        }
    }
    return cells;
}

inline BossInterception boss_interception_for_solo(
    const BossObjective& boss,
    std::string_view map_id,
    const std::vector<BoardCell>& deployable_ground_route)
{
    if (boss.possible_enemy_ids.size() != 1 || boss.possible_enemy_ids.front() != "enemy_2124_dyjsfg" ||
        boss.stationary_route) {
        return {};
    }
    // 矩兽's resistance drops when stopped. Plan for two blocks so a
    // baseline single-block actor does not get credit through Overload.
    BossInterception result { boss.movement_waypoints, 2 };
    if (map_id == "m03" && std::ranges::find(result.cells, BoardCell { 3, 4 }) != result.cells.end()) {
        // The D7 battle showed the west approach to this checkpoint at (4,4).
        result.cells.emplace_back(4, 4);
    }
    std::erase_if(result.cells, [&](BoardCell cell) {
        return std::ranges::find(deployable_ground_route, cell) == deployable_ground_route.end();
    });
    return result;
}

// Use a shared spawn only for a registered solo board.
inline std::vector<BoardCell> estimate_solo_stationary_boss_footprint(const BossObjective& objective)
{
    return objective.stationary_route && objective.possible_cells.size() == 1
               ? estimate_five_by_three_boss_footprint(objective.possible_cells.front())
               : std::vector<BoardCell> {};
}

// A moving boss can leave its opening footprint immediately. It still has
// separate route checkpoints, and widening needs current preview geometry.
inline std::vector<BoardCell>
    estimate_solo_boss_opening_cells(const BossObjective& objective, bool moving_opening_corroborated = false)
{
    if (!objective.known || objective.possible_cells.size() != 1) {
        return {};
    }
    if (objective.stationary_route) {
        return estimate_solo_stationary_boss_footprint(objective);
    }
    if (moving_opening_corroborated) {
        auto footprint = estimate_five_by_three_boss_footprint(objective.possible_cells.front());
        if (!footprint.empty()) {
            return footprint;
        }
    }
    const auto [col, row] = objective.possible_cells.front();
    return col >= 0 && col <= 12 && row >= 0 && row <= 4 ? objective.possible_cells : std::vector<BoardCell> {};
}

inline bool current_hidden_boss_enemy(const std::string& id)
{
    return std::any_of(SeasonBossCatalogue.begin(), SeasonBossCatalogue.end(), [&](const auto& boss) {
        return boss.hidden && boss.enemy_id == id;
    });
}
}
