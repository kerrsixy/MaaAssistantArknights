#pragma once

#include "SPADeploymentReview.h"
#include "SPAOperatorCombatData.h"
#include "SPAOperatorSkillData.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace asst::spa
{
inline std::optional<SeasonCombatStats> season_combat_stats(std::string_view name, bool elite)
{
    const auto found =
        std::find_if(SeasonOperatorCombatCatalogue.begin(), SeasonOperatorCombatCatalogue.end(), [&](const auto& row) {
            return row.name == name;
        });
    if (found == SeasonOperatorCombatCatalogue.end()) {
        return std::nullopt;
    }
    return elite ? found->elite : found->initial;
}

// Estimate basic attack output before enemy mitigation or conditional effects.
// This season's garrison base attack speed is 100 in both forms.
inline int
    season_basic_attack_weight(const SeasonCombatStats& stats, std::string_view strategy_id, bool confirmed_elite)
{
    if (stats.attack_interval <= 0) {
        return 0;
    }
    constexpr double weakness_elite_attack_multiplier = 1.16;
    constexpr double weakness_elite_speed_multiplier = 116.0 / 100.0;
    const double modifier = strategy_id == "band_weakness" && confirmed_elite
                                ? weakness_elite_attack_multiplier * weakness_elite_speed_multiplier
                                : 1.0;
    return static_cast<int>(stats.attack * modifier / stats.attack_interval);
}

// The left solo field in all 30 Sequence I subboards places both
// aceffect_band_61 forms at (2,3), facing east. Both use basic range 3-3.
inline std::optional<DeploymentUnit>
    solo_field_medical_support(std::string_view strategy_id, int confirmed_elites, int unknown_forms)
{
    if (strategy_id != "band_amedic") {
        return std::nullopt;
    }
    constexpr int touch_elite_threshold = 2;
    DeploymentUnit healer;
    healer.id = confirmed_elites >= touch_elite_threshold                  ? "Touch"
                : confirmed_elites + unknown_forms < touch_elite_threshold ? "预备干员-医疗"
                                                                           : "外勤医疗";
    healer.cell = { 2, 3 };
    healer.healer = true;
    healer.range_known = true;
    healer.movable = false;
    healer.local_range = { { 0, 1 }, { 1, 1 }, { 2, 1 },  { 3, 1 },  { 0, 0 },  { 1, 0 },
                           { 2, 0 }, { 3, 0 }, { 0, -1 }, { 1, -1 }, { 2, -1 }, { 3, -1 } };
    return healer;
}

inline std::optional<SeasonDefaultSkill> season_default_skill(std::string_view name, bool elite)
{
    const auto found = std::find_if(SeasonDefaultSkills.begin(), SeasonDefaultSkills.end(), [&](const auto& row) {
        return row.operator_name == name && row.elite == elite;
    });
    return found == SeasonDefaultSkills.end() ? std::nullopt : std::optional(*found);
}

struct ConditionalSkillAttackWindow
{
    bool possible = false;
    double first_activation_seconds = 0;
    double active_seconds = 0;
    std::span<const std::pair<int, int>> attack_range;
};

// This is the season default, not proof of a player-selected alternate skill.
// Until a live override is verified, moving defaults cannot secure a fixed lane.
inline bool default_skill_holds_position(std::string_view name, bool elite)
{
    const auto skill = season_default_skill(name, elite);
    return skill && !skill->self_movement;
}

// A baseline opportunity, not observed activation or guaranteed uptime.
// Attack/defense charge, movement targeting and unknown semantics remain
// outside this estimate; a declared targeting range is not a basic range.
inline ConditionalSkillAttackWindow
    conditional_skill_attack_window(std::string_view name, bool elite, double fight_seconds)
{
    ConditionalSkillAttackWindow result;
    const auto skill = season_default_skill(name, elite);
    if (!skill || !std::isfinite(fight_seconds) || fight_seconds <= 0 || fight_seconds > 3600 ||
        !skill->attack_range_replacement || skill->self_movement || skill->declared_range.empty() ||
        skill->charge != SeasonSkillCharge::Time || skill->recovery_per_second <= 0 ||
        (skill->duration_seconds <= 0 && skill->duration_seconds != -1)) {
        return result;
    }
    const double recharge = skill->sp_cost / skill->recovery_per_second;
    result.first_activation_seconds = std::max(0.0, (skill->sp_cost - skill->initial_sp) / skill->recovery_per_second);
    if (result.first_activation_seconds >= fight_seconds) {
        return result;
    }
    result.attack_range = skill->declared_range;
    if (skill->duration_seconds == -1) {
        result.active_seconds = fight_seconds - result.first_activation_seconds;
    }
    else {
        const double cycle = recharge + skill->duration_seconds;
        for (double start = result.first_activation_seconds; start < fight_seconds; start += cycle) {
            result.active_seconds += std::min(skill->duration_seconds, fight_seconds - start);
        }
    }
    result.possible = result.active_seconds > 0;
    return result;
}
}
