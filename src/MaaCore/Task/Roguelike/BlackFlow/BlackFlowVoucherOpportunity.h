#pragma once

#include "Task/Roguelike/RoguelikeRecruitPolicy.h"
#include <optional>
#include <string>
#include <vector>

namespace asst::blackflow
{
// A safety ceiling, not a claim that all physical pages have been read.
inline int recruitment_scan_observation_limit(int legacy, int configured)
{
    return configured > 0 && configured <= 64 ? configured : legacy;
}

struct VoucherOptionPreference
{
    std::string name;
    RoguelikeRecruitTier tier = RoguelikeRecruitTier::Skip;
    int score = 0;
    std::optional<int> hope_cost;
};

struct VoucherOpportunityContext
{
    int hope = -1;
    bool selected_offer_verified = false;
    bool fixed_opening = false;
    bool baseline_roles_present = false;
    bool selected_fills_immediate_need = false;
    bool selected_is_promotion = false;
    bool selected_is_temporary = false;
    bool storage_observed = false;
    bool can_store = false;
};

struct VoucherOpportunityDecision
{
    bool needs_storage_observation = false;
    bool request_stash = false;
    std::string reason = "current_offer_or_budget_unknown";
    std::string deferred_name;
};

// Configured preferences express an acquisition plan, not a damage guarantee.
// Both free and paid recruitment consume the same non-renewable voucher.
inline VoucherOpportunityDecision choose_voucher_opportunity(
    const VoucherOpportunityContext& context,
    const VoucherOptionPreference& selected,
    const std::vector<VoucherOptionPreference>& qualified_current_unaffordable)
{
    VoucherOpportunityDecision out;
    if (!context.selected_offer_verified || context.hope < 0 || !selected.hope_cost || *selected.hope_cost < 0 ||
        *selected.hope_cost > context.hope || selected.tier == RoguelikeRecruitTier::Skip) {
        return out;
    }
    if (context.fixed_opening || !context.baseline_roles_present || context.selected_fills_immediate_need ||
        context.selected_is_promotion || context.selected_is_temporary ||
        selected.tier == RoguelikeRecruitTier::EliteTwo) {
        out.reason = "opening_or_immediate_roster_gain_precedes_retention";
        return out;
    }
    const auto better = [](const auto& left, const auto& right) {
        return left.tier < right.tier || (left.tier == right.tier && left.score > right.score);
    };
    const VoucherOptionPreference* best = nullptr;
    for (const auto& future : qualified_current_unaffordable) {
        if (future.name.empty() || future.name == selected.name || !future.hope_cost ||
            *future.hope_cost <= context.hope || future.tier == RoguelikeRecruitTier::Skip ||
            !better(future, selected)) {
            continue;
        }
        if (!best || better(future, *best)) {
            best = &future;
        }
    }
    if (!best) {
        out.reason = "no_observed_qualified_preferred_unaffordable_option";
        return out;
    }
    out.deferred_name = best->name;
    if (!context.storage_observed) {
        out.needs_storage_observation = true;
        out.reason = "preferred_deferred_option_requires_fresh_storage_evidence";
    }
    else if (!context.can_store) {
        out.reason = "no_verified_storage_capacity_or_enabled_control";
    }
    else {
        out.request_stash = true;
        out.reason = "preserve_voucher_for_observed_preferred_option";
    }
    return out;
}
}
