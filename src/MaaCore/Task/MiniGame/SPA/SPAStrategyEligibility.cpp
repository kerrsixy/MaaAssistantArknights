#include "SPAStrategyEligibility.h"
#include "SPAGuideRoutes.h"
#include "SPAStrategyCatalogue.h"

#include <algorithm>
#include <cmath>
#include <tuple>

std::optional<asst::spa::StrategyUnlockRule> asst::spa::strategy_unlock_rule(std::string_view id)
{
    const auto rule =
        std::find_if(StrategyUnlockCatalogue.begin(), StrategyUnlockCatalogue.end(), [&](const auto& entry) {
            return entry.id == id;
        });
    return rule != StrategyUnlockCatalogue.end() ? std::optional(*rule) : std::nullopt;
}

bool asst::spa::strategy_available_in_mode(std::string_view id, std::string_view mode)
{
    const auto rule = strategy_unlock_rule(id);
    if (!rule) {
        return false;
    }
    return mode.empty() || (mode == "quick" && rule->quick_available) ||
           (mode == "independent" && rule->independent_available) || (mode == "joint" && rule->joint_available);
}

bool asst::spa::strategy_grants_preparation_equipment(std::string_view id, int round)
{
    if (round < 1 || round > 20) {
        return false;
    }
    if (id == "band_jesica" || id == "band_kjerag2") {
        constexpr std::array rounds { 4, 7, 10, 13 };
        return std::ranges::find(rounds, round) != rounds.end();
    }
    if (id == "band_smith") {
        constexpr std::array rounds { 3, 6, 9, 12, 14 };
        return std::ranges::find(rounds, round) != rounds.end();
    }
    if (id == "band_fang") {
        constexpr std::array rounds { 8, 10, 12, 14 };
        return std::ranges::find(rounds, round) != rounds.end();
    }
    return id == "band_damaztic" && round % 5 == 0;
}

bool asst::spa::prefer_higher_tier_strategy_refresh(
    const std::string& strategy_id,
    int shop_level,
    int funds,
    int uses_this_round,
    bool paid_refresh_planned)
{
    return strategy_id == "band_support" && shop_level >= 1 && shop_level < 6 && funds >= 1 && funds <= 999 &&
           uses_this_round >= 0 && uses_this_round < 3 && paid_refresh_planned;
}

asst::spa::StrategyEligibilityContext asst::spa::strategy_context_for_formation(
    const StrategyEligibilityContext& assertions,
    bool formation_complete,
    const std::vector<std::string>& active_pacts)
{
    auto current = assertions;
    if (current.active_pacts.empty() && formation_complete) {
        current.active_pacts = active_pacts;
    }
    return current;
}

asst::spa::OpeningStrategyAssessment
    asst::spa::assess_opening_strategy(const std::string& id, const OpeningStrategyContext& context)
{
    OpeningStrategyAssessment result;
    const auto fact = std::find_if(StrategyCatalogue.begin(), StrategyCatalogue.end(), [&](const auto& item) {
        return item.id == id;
    });
    if (fact == StrategyCatalogue.end()) {
        result.reason = "Strategy identity is absent from the current-season catalogue";
        return result;
    }
    result.known = true;
    if (!strategy_available_in_mode(id, context.mode)) {
        result.reason = "Strategy is unavailable in this simulation mode";
        return result;
    }
    const auto available = [&](const std::string& pact) {
        return std::ranges::find(context.disabled_pacts, pact) == context.disabled_pacts.end() &&
               (context.pact_availability_known ||
                std::ranges::find(context.observed_available_pacts, pact) != context.observed_available_pacts.end());
    };
    if (!fact->pact.empty() && !available(std::string(fact->pact))) {
        result.reason = "Faction strategy needs a pact verified as available";
        return result;
    }
    result.eligible = true;
    result.score = fact->initial_life * 12 + (fact->passive ? 120 : -120);
    // These are tactical weights, not claimed damage, layer yields or win
    // rates. Active instructions remain candidates, but no unimplemented
    // action is credited with an effect merely because it is written down.
    if (id == "band_weakness") {
        result.score += 250;
    }
    else if (id == "band_amedic") {
        result.score += 200;
    }
    else if (id == "band_takecare") {
        result.score += 160;
    }
    else if (id == "band_amiya") {
        result.score += 170;
    }
    else if (id == "band_ioleta") {
        result.score += 140 + (available("奇迹") ? 60 : 0);
    }
    else if (id == "band_qalaisa" || id == "band_emperor") {
        result.score += available("不屈") ? 100 : 40;
    }
    // Item gifts, forced-sale exchanges and inherited funds require their
    // own inventory/economy executors. Avoid crediting unmodeled loops as
    // guaranteed opening strength; they stay eligible for badge work.
    if (id == "band_jesica" || id == "band_fang" || id == "band_damaztic" || id == "band_random" ||
        id == "band_substitute" || id == "band_cannot" || id == "band_lmlee" || id == "band_pith" ||
        id == "band_mech") {
        result.score -= 120;
    }
    result.reason = fact->passive ? "Compare initial life, automatic combat effects and available pact synergies"
                                  : "Active instruction has no assumed automatic effect; retain as an eligible option";
    if (context.pact_availability_known) {
        const auto routes = guided_primary_choices(id, context.disabled_pacts);
        if (!routes.pacts.empty()) {
            int lowest_contention = 100;
            for (const auto& pact : routes.pacts) {
                lowest_contention = std::min(
                    lowest_contention,
                    inferred_route_contention(pact, context.occupied_strategy_ids, context.disabled_pacts));
            }
            result.score -= lowest_contention * 2;
        }
    }
    return result;
}

int asst::spa::choose_opening_strategy_card(const std::vector<OpeningStrategyCard>& cards, bool prefer_unclaimed)
{
    const OpeningStrategyCard* best = nullptr;
    const auto rank = [&](const OpeningStrategyCard& card) {
        // First pursue orange badges, then complete known orange cards to silver.
        const int badge_rank = prefer_unclaimed ? card.badge_priority() : 0;
        return std::make_tuple(badge_rank, card.support_score, -card.index);
    };
    for (const auto& card : cards) {
        if (!card.available || card.index < 0 || !std::isfinite(card.support_score)) {
            continue;
        }
        if (!best || rank(card) > rank(*best)) {
            best = &card;
        }
    }
    return best ? best->index : -1;
}

namespace
{
bool contains(const std::vector<std::string>& values, const std::string& value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}
}

asst::spa::SetupSelection asst::spa::choose_setup_pacts(
    const std::string& preferred_core,
    const std::vector<std::string>& preferred_support,
    const std::vector<std::string>& available_core,
    const std::vector<std::string>& available_support,
    const std::vector<std::string>& disabled)
{
    SetupSelection result;
    if (preferred_core.empty() || contains(disabled, preferred_core) || !contains(available_core, preferred_core)) {
        result.reasons.emplace_back("Preferred core pact is unavailable or disabled");
        return result;
    }
    result.core_pact = preferred_core;
    for (const auto& pact : preferred_support) {
        if (contains(disabled, pact)) {
            result.reasons.emplace_back("Configured support pact is disabled: " + pact);
            continue;
        }
        if (contains(available_support, pact)) {
            result.support_pacts.emplace_back(pact);
        }
    }
    if (result.support_pacts.empty() && !preferred_support.empty()) {
        result.reasons.emplace_back("No configured support pact is available");
        return result;
    }
    result.valid = true;
    return result;
}

asst::spa::StrategyEligibility asst::spa::evaluate_strategy_eligibility(
    const StrategyCandidate& candidate,
    const StrategyEligibilityContext& context)
{
    StrategyEligibility result;
    if (candidate.id.empty() || candidate.name.empty() || context.round < 1) {
        result.reasons.emplace_back("Incomplete strategy identity or round");
        return result;
    }
    if (candidate.requires_core_roles && (!context.core_layer_operator_verified || !context.damage_carry_verified)) {
        result.reasons.emplace_back("Core layer operator or damage carry is not verified");
        return result;
    }
    bool provisional_opening_candidate = false;
    // This is a hard safety gate. A configured primary faction can never be
    // treated as operational until the game confirms that faction active.
    if (candidate.requires_active_primary_pact && !candidate.primary_pacts.empty()) {
        const bool active =
            std::any_of(candidate.primary_pacts.begin(), candidate.primary_pacts.end(), [&](const auto& pact) {
                return contains(context.active_pacts, pact) && !contains(context.disabled_pacts, pact);
            });
        if (!active && !context.opening_selection) {
            result.reasons.emplace_back("Primary faction pact is not verified active");
            return result;
        }
        if (!active && context.opening_selection) {
            provisional_opening_candidate = true;
        }
    }

    if (contains(context.disabled_strategy_ids, candidate.id)) {
        result.reasons.emplace_back("Strategy is dynamically disabled");
        return result;
    }
    for (const auto& pact : candidate.required_pacts) {
        if (contains(context.disabled_pacts, pact)) {
            result.reasons.emplace_back("Required pact is disabled: " + pact);
        }
    }
    for (const auto& pact : candidate.forbidden_bans) {
        if (contains(context.disabled_pacts, pact)) {
            result.reasons.emplace_back("Forbidden ban is active: " + pact);
        }
    }
    if (!candidate.required_initial_terms.empty()) {
        bool matched = false;
        for (const auto& term : candidate.required_initial_terms) {
            if (term.empty() || context.initial_brief.find(term) != std::string::npos) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            result.reasons.emplace_back("Initial brief does not support the strategy");
        }
    }
    if (!result.reasons.empty()) {
        return result;
    }
    result.eligible = true;
    result.score = candidate.observed_support;
    for (const auto& pact : candidate.primary_pacts) {
        if (contains(context.active_pacts, pact)) {
            result.score += 100;
        }
    }
    result.unclaimed_priority = !candidate.badge_verified;
    if (result.unclaimed_priority) {
        result.score += 10'000;
    }
    result.reasons.emplace_back(
        result.unclaimed_priority ? "Eligible and badge is unclaimed" : "Eligible fallback strategy");
    if (provisional_opening_candidate) {
        result.reasons.emplace_back("Opening candidate is provisional until faction activation is observed");
    }
    return result;
}

asst::spa::StrategySelection asst::spa::choose_strategy_with_fallback(
    const std::vector<StrategyCandidate>& candidates,
    const StrategyEligibilityContext& context)
{
    StrategySelection best;
    for (const auto& candidate : candidates) {
        const auto eligibility = evaluate_strategy_eligibility(candidate, context);
        if (!eligibility.eligible) {
            continue;
        }
        if (!best.found || std::make_tuple(eligibility.unclaimed_priority, eligibility.score) >
                               std::make_tuple(best.eligibility.unclaimed_priority, best.eligibility.score)) {
            best = { true, candidate, eligibility };
        }
    }
    return best;
}
