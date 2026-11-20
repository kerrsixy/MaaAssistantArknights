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
};

struct BossFamilyMobility
{
    bool known = false;
    bool stationary_route = false;
};

// Family classification only. It establishes neither the current variant,
// difficulty, health nor a screen/grid location.
inline BossFamilyMobility boss_family_mobility(const std::string& family)
{
    BossFamilyMobility result;
    bool all_stationary = true;
    for (const auto& boss : SeasonBossCatalogue) {
        if (boss.family_name == family) {
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
            return cell.first < 0 || cell.first > 10 || cell.second < 0 || cell.second > 4;
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
    for (const auto& boss : SeasonBossCatalogue) {
        if (boss.family_name != family || (hidden_variant && boss.hidden != *hidden_variant) ||
            boss.hp_by_difficulty[difficulty - 1] <= 0) {
            continue;
        }
        result.possible_enemy_ids.emplace_back(boss.enemy_id);
        all_stationary = all_stationary && boss.stationary_route;
        // Both are client coordinates with rows increasing upwards. The
        // registered deployment grid instead increases rows downwards.
        cells.emplace(
            registered_home.first + boss.spawn_col - boss.template_home_col,
            registered_home.second + boss.template_home_row - boss.spawn_row);
        result.maximum_declared_hp = std::max(result.maximum_declared_hp, boss.hp_by_difficulty[difficulty - 1]);
        result.time_limit_seconds = std::max(result.time_limit_seconds, boss.time_limit_seconds);
    }
    result.known = !result.possible_enemy_ids.empty();
    result.stationary_route = result.known && all_stationary;
    result.variant_known = result.possible_enemy_ids.size() == 1;
    result.possible_cells.assign(cells.begin(), cells.end());
    return result;
}

inline bool current_hidden_boss_enemy(const std::string& id)
{
    return std::any_of(SeasonBossCatalogue.begin(), SeasonBossCatalogue.end(), [&](const auto& boss) {
        return boss.hidden && boss.enemy_id == id;
    });
}
}
