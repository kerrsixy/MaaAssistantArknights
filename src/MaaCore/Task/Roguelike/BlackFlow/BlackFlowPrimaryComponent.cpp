#include "BlackFlowPrimaryComponent.h"

#include <algorithm>
#include <cmath>

namespace asst::blackflow::primary_component_detail
{
bool curve_no_less(double a, double ah, double ap, double b, double bh, double bp, bool& strict)
{
    if (a <= 0 || b <= 0 || ah <= 0 || bh <= 0 || ap <= 0 || bp <= 0) {
        return false;
    }
    const std::array<double, 4> points { 0, 0.95 * a, 0.95 * b, std::max(a, b) + 1 };
    strict = true;
    for (double defense : points) {
        const double av = ah * std::max(a - defense, 0.05 * a) / ap;
        const double bv = bh * std::max(b - defense, 0.05 * b) / bp;
        if (av + 1e-9 < bv) {
            return false;
        }
        strict = strict && av > bv + 1e-9;
    }
    return true;
}
}

namespace asst::blackflow
{
std::optional<PrimaryComponentEnvelope> calculate_primary_component(
    const PrimaryComponentVariant& variant,
    const std::optional<PrimaryExternalBounds>& current_run_modifiers)
{
    for (double value : { variant.pre_talent_attack[0],
                          variant.pre_talent_attack[1],
                          variant.own_atk_fraction,
                          variant.own_aspd,
                          variant.base_period,
                          variant.skill_interval_add,
                          variant.skill_scale[0],
                          variant.skill_scale[1],
                          variant.hits,
                          variant.sp_cost,
                          variant.initial_sp,
                          variant.first_deploy_sp_upper }) {
        if (!std::isfinite(value)) {
            return std::nullopt;
        }
    }
    if (variant.pre_talent_attack[0] <= 0 || variant.pre_talent_attack[1] < variant.pre_talent_attack[0] ||
        variant.skill_scale[0] <= 0 || variant.skill_scale[1] < variant.skill_scale[0] || variant.hits <= 0 ||
        variant.sp_cost < 0 || variant.initial_sp < 0 || variant.first_deploy_sp_upper < 0 ||
        variant.base_period <= 0 || variant.base_period + variant.skill_interval_add <= 0) {
        return std::nullopt;
    }
    PrimaryComponentEnvelope result;
    auto& low = result.lower;
    auto& high = result.upper;
    low.base_damage = variant.pre_talent_attack[0] * (1 + variant.own_atk_fraction);
    high.base_damage = variant.pre_talent_attack[1] * (1 + variant.own_atk_fraction);
    double sl = (100 + variant.own_aspd) / 100;
    double sh = sl;
    if (current_run_modifiers) {
        const auto& modifiers = *current_run_modifiers;
        for (const auto& bounds : { modifiers.atk_fraction, modifiers.aspd }) {
            if (!std::isfinite(bounds[0]) || !std::isfinite(bounds[1]) || bounds[0] > bounds[1]) {
                return std::nullopt;
            }
        }
        // Preserve the reviewed full-scope model's arithmetic. This branch is
        // distinct from the intrinsic objective; absent evidence is not zero.
        low.base_damage = variant.pre_talent_attack[0] * (1 + variant.own_atk_fraction + modifiers.atk_fraction[0]);
        high.base_damage = variant.pre_talent_attack[1] * (1 + variant.own_atk_fraction + modifiers.atk_fraction[1]);
        sl = (100 + variant.own_aspd + modifiers.aspd[0]) / 100;
        sh = (100 + variant.own_aspd + modifiers.aspd[1]) / 100;
    }
    if (sl <= 0 || sh <= 0 || low.base_damage <= 0 || high.base_damage < low.base_damage) {
        return std::nullopt;
    }
    low.base_period = variant.base_period / sl;
    high.base_period = variant.base_period / sh;
    low.skill_damage = low.base_damage * variant.skill_scale[0];
    high.skill_damage = high.base_damage * variant.skill_scale[1];
    low.skill_period = (variant.base_period + variant.skill_interval_add) / sl;
    high.skill_period = (variant.base_period + variant.skill_interval_add) / sh;
    low.hits = high.hits = variant.hits;
    low.sp_debt = std::max(0.0, variant.sp_cost - variant.initial_sp);
    high.sp_debt = std::max(0.0, variant.sp_cost - variant.initial_sp - variant.first_deploy_sp_upper);
    for (const auto& profile : { low, high }) {
        if (profile.base_damage <= 0 || profile.skill_damage <= 0 || profile.base_period <= 0 ||
            profile.skill_period <= 0) {
            return std::nullopt;
        }
        for (double value : { profile.base_damage,
                              profile.base_period,
                              profile.skill_damage,
                              profile.skill_period,
                              profile.hits,
                              profile.sp_debt }) {
            if (!std::isfinite(value)) {
                return std::nullopt;
            }
        }
    }
    return result;
}

bool primary_component_curves_strictly_better(
    const std::vector<ProjectedPhysicalProfile>& candidate_lower,
    const std::vector<ProjectedPhysicalProfile>& other_upper,
    bool require_no_more_sp_debt)
{
    if (candidate_lower.empty() || other_upper.empty()) {
        return false;
    }
    for (const auto& low : candidate_lower) {
        for (const auto& high : other_upper) {
            for (const auto& profile : { low, high }) {
                for (double value : { profile.base_damage,
                                      profile.base_period,
                                      profile.skill_damage,
                                      profile.skill_period,
                                      profile.hits,
                                      profile.sp_debt }) {
                    if (!std::isfinite(value) || value < 0) {
                        return false;
                    }
                }
            }
            bool base_strict = false, active_strict = false;
            if ((require_no_more_sp_debt && low.sp_debt > high.sp_debt) ||
                !primary_component_detail::curve_no_less(
                    low.base_damage,
                    1,
                    low.base_period,
                    high.base_damage,
                    1,
                    high.base_period,
                    base_strict) ||
                !primary_component_detail::curve_no_less(
                    low.skill_damage,
                    low.hits,
                    low.skill_period,
                    high.skill_damage,
                    high.hits,
                    high.skill_period,
                    active_strict) ||
                (!base_strict && !active_strict)) {
                return false;
            }
        }
    }
    return true;
}
}
