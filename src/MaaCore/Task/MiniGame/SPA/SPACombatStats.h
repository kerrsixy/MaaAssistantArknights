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

struct PrimaryDamageBonuses
{
    int attack_basis_points = 0;
    int outgoing_damage_basis_points = 0;
};

// Sequence I bond effects. Layer input comes from the current preparation HUD.
// Both Kjerag variants give the same unconditional damage factor.
inline PrimaryDamageBonuses
    season_primary_damage_bonuses(std::string_view pact, int layers, int verified_opening_deployments = 0)
{
    if (layers < 0 || layers > 999) {
        return {};
    }
    if (pact == "乌萨斯") {
        return { .attack_basis_points = 3500 + 75 * layers };
    }
    if (pact == "炎") {
        return { .attack_basis_points = 2300 + 90 * layers };
    }
    if (pact == "莱塔尼亚") {
        return { .attack_basis_points = 3000 + 100 * layers };
    }
    if (pact == "谢拉格") {
        return { .outgoing_damage_basis_points = 2500 };
    }
    if (pact == "卡西米尔" && verified_opening_deployments > 0 && verified_opening_deployments <= 8) {
        // Bound the estimate by the planned opening deployments and the
        // layer cap. Later redeployments and the six-member effect are separate.
        return { .attack_basis_points = std::min(2000 * verified_opening_deployments, 5000 + 100 * layers) };
    }
    return {};
}

// Estimate basic attack output before enemy mitigation or conditional effects.
// This season's garrison base attack speed is 100 in both forms.
inline int season_basic_attack_weight(
    const SeasonCombatStats& stats,
    std::string_view strategy_id,
    bool confirmed_elite,
    PrimaryDamageBonuses primary = {})
{
    if (stats.attack_interval <= 0) {
        return 0;
    }
    const bool weakness = strategy_id == "band_weakness" && confirmed_elite;
    // Each bonus is documented independently. Use the larger known attack
    // bonus until their in-game composition is verified.
    const int attack_bonus = std::max(weakness ? 1600 : 0, primary.attack_basis_points);
    const double speed = weakness ? 1.16 : 1.0;
    const double outgoing_damage = (10'000 + primary.outgoing_damage_basis_points) / 10'000.0;
    return static_cast<int>(
        static_cast<double>(stats.attack) * (10'000 + attack_bonus) * speed * outgoing_damage /
        (10'000 * stats.attack_interval));
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
