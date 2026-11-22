#include "SPAPlanner.h"
#include "SPAStrategyUnlockCatalogue.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

bool asst::spa::battle_entry_allowed(
    bool round_only,
    bool auto_run,
    int funds,
    bool spending_reviewed,
    const std::string& spending_reason)
{
    if (funds < 0 || spending_reason.size() > 512) {
        return false;
    }
    return round_only || (auto_run && (funds == 0 || (spending_reviewed && !spending_reason.empty())));
}

asst::spa::Decision asst::spa::Planner::choose(const RoundState& state, const std::vector<Candidate>& candidates)
{
    Decision result { ActionKind::Observe, {},         "Incomplete preparation observation", state.revision, 0,
                      state.run_id,        state.round };
    if (!state.observation_complete || !state.preparation || !state.map_verified || !state.lineup_verified ||
        state.funds < 0 || state.remaining_rounds < 0 || state.run_id.empty()) {
        return result;
    }
    if (state.remaining_action_budget <= 0) {
        result.reason = "Action budget exhausted; reconcile before continuing";
        return result;
    }

    // Survival takes precedence over stacking; money expires unless a verified rule preserves it.
    // No interest or next-round cash is inferred from an ordinary unspent balance.
    const bool carry = state.can_carry_funds && state.carry_rule_verified;
    const Candidate* best = nullptr;
    std::tuple<double, double, double, double, int> best_score { 0, 0, 0, 0, 0 };
    for (const auto& candidate : candidates) {
        if (!candidate.legal || !candidate.effect_verified || !candidate.preserves_required_units ||
            candidate.cost < 0 || candidate.cost > state.funds || candidate.target.empty() ||
            !std::isfinite(candidate.survival_gain) || !std::isfinite(candidate.permanent_layers) ||
            !std::isfinite(candidate.future_income) || candidate.kind == ActionKind::Observe ||
            candidate.kind == ActionKind::Ready ||
            (candidate.requires_active_pacts && !candidate.active_pacts_verified) ||
            (candidate.minimum_shop_level > 0 && state.shop_level < candidate.minimum_shop_level)) {
            continue;
        }
        if (state.survival_at_risk && candidate.survival_gain <= 0) {
            continue;
        }
        const double income = state.remaining_rounds > 0 ? candidate.future_income : 0;
        // Conserving money has value only when the active strategy explicitly carries it.
        const double net_income = income - (carry ? candidate.cost : 0);
        const auto score = std::make_tuple(
            state.survival_at_risk ? candidate.survival_gain : 0,
            candidate.permanent_layers,
            candidate.survival_gain,
            net_income,
            -candidate.cost);
        if (score > best_score) {
            best = &candidate;
            best_score = score;
        }
    }
    if (best) {
        return { best->kind,   best->target, "Verified affordable improvement", state.revision, best->cost,
                 state.run_id, state.round };
    }
    if (state.survival_at_risk) {
        result.reason = "No verified survival improvement; inspect alternatives";
        return result;
    }
    if (state.ready_verified) {
        return { ActionKind::Ready, {},         "No verified beneficial affordable action", state.revision, 0,
                 state.run_id,      state.round };
    }
    result.reason = "Ready control not verified";
    return result;
}

bool asst::spa::Planner::can_execute(const RoundState& state, const Decision& decision)
{
    return decision.kind != ActionKind::Observe && state.observation_complete && state.preparation &&
           state.map_verified && state.lineup_verified && !state.run_id.empty() && state.remaining_action_budget > 0 &&
           state.revision == decision.expected_revision && state.run_id == decision.run_id &&
           state.round == decision.round && decision.expected_cost >= 0 && state.funds >= decision.expected_cost &&
           (decision.kind != ActionKind::Ready || (state.ready_verified && !state.survival_at_risk));
}

bool asst::spa::ProgressLedger::apply(const Settlement& settlement, StrategyProgress& progress)
{
    if (!settlement.final_result_verified || !settlement.victory || !settlement.strategy_credit_verified ||
        settlement.run_id.empty() || settlement.band_id.empty() || settlement.band_id != progress.band_id ||
        !progress.wins || *progress.wins < 0 || *progress.wins == std::numeric_limits<int>::max() ||
        std::find(m_credited_runs.begin(), m_credited_runs.end(), settlement.run_id) != m_credited_runs.end()) {
        return false;
    }
    m_credited_runs.emplace_back(settlement.run_id);
    ++*progress.wins;
    return true;
}

bool asst::spa::ProgressLedger::complete(const StrategyProgress& progress)
{
    return progress.unlocked && progress.wins && *progress.wins >= 3 && progress.badge_verified;
}

bool asst::spa::ProgressLedger::next_report_confirms_win(
    const StrategyProgress& before,
    const StrategyProgress& after,
    std::string_view actual_band_id,
    bool verified_victory)
{
    return verified_victory && !actual_band_id.empty() && before.band_id == actual_band_id &&
           after.band_id == actual_band_id && before.unlocked && after.unlocked && before.wins && after.wins &&
           *before.wins >= 0 && *before.wins < std::numeric_limits<int>::max() && *after.wins == *before.wins + 1;
}

bool asst::spa::ProgressLedger::can_schedule_solo(const StrategyProgress& progress)
{
    if (progress.band_id.empty() || !progress.unlocked || !progress.solo_eligible_verified || complete(progress)) {
        return false;
    }
    if (progress.wins) {
        // The orange mark can verify completion from three wins. An
        // unverified mark can still be followed up with the six-win mark.
        return *progress.wins >= 0 && *progress.wins < 6;
    }
    return !progress.badge_verified &&
           std::any_of(StrategyUnlockCatalogue.begin(), StrategyUnlockCatalogue.end(), [&](const auto& rule) {
               return rule.id == progress.band_id && rule.independent_available;
           });
}

std::optional<std::string> asst::spa::ProgressLedger::next_solo(const std::vector<StrategyProgress>& progress)
{
    // Finish nearby marks first. An unverified three-win mark stays
    // eligible for the six-win threshold and is three wins away again.
    const auto priority = [](const StrategyProgress& entry) {
        const int wins = entry.wins.value_or(-1);
        return std::tuple(!entry.badge_verified, wins % 3, wins);
    };
    const StrategyProgress* best = nullptr;
    for (const auto& item : progress) {
        if (!can_schedule_solo(item)) {
            continue;
        }
        if (!best || priority(item) > priority(*best)) {
            best = &item;
        }
    }
    return best ? std::optional(best->band_id) : std::nullopt;
}

std::optional<asst::spa::PromotionDecision> asst::spa::plan_promotion(const PromotionWindow& window)
{
    if (window.shop_level < 1 || window.shop_level > 6 || window.funds < 0 || window.third_copy_cost < 0 ||
        window.upgrade_cost < 0 || window.third_copy_cost > window.funds) {
        return std::nullopt;
    }
    const int reward = std::min(6, window.shop_level + 1);
    if (reward < 6 && window.upgrade_preserves_target_verified && !window.immediate_promotion_required &&
        window.upgrade_cost <= window.funds - window.third_copy_cost) {
        return PromotionDecision { PromotionOrder::UpgradeThenBuyThird, reward + 1 };
    }
    return PromotionDecision { PromotionOrder::BuyThird, reward };
}

int asst::spa::promotion_copies_required(const std::string& operator_name)
{
    // 星极 is the currently catalogued special case.  Keep this function
    // conservative: new exceptions are added to the trait catalogue or an
    // explicit policy override, never inferred from a name in live logic.
    return operator_name == "星极" ? 2 : 3;
}

int asst::spa::promotion_copies_required_from_trait(const std::string& operator_name, const std::string& trait_text)
{
    // Trait text is authoritative for special synthesis.  Keep matching
    // deliberately broad because the game/localization wording has used
    // both “名” and “张”, and may add another numeral in a later sequence.
    if (trait_text.find("两名") != std::string::npos || trait_text.find("2名") != std::string::npos ||
        trait_text.find("两张") != std::string::npos || trait_text.find("2张") != std::string::npos ||
        trait_text.find("两只") != std::string::npos || trait_text.find("2只") != std::string::npos) {
        return 2;
    }
    if (trait_text.find("四名") != std::string::npos || trait_text.find("4名") != std::string::npos ||
        trait_text.find("四张") != std::string::npos || trait_text.find("4张") != std::string::npos) {
        return 4;
    }
    return promotion_copies_required(operator_name);
}

int asst::spa::battle_time_limit_seconds(int round, bool leader_round)
{
    if (round < 1 || round > 20) {
        return 0;
    }
    if (leader_round) {
        return 120;
    }
    if (round <= 2) {
        return 45;
    }
    if (round <= 5) {
        return 55;
    }
    if (round == 6) {
        return 70;
    }
    if (round <= 8) {
        return 85;
    }
    if (round <= 10) {
        return 95;
    }
    return 115;
}

asst::spa::PromotionSelection asst::spa::choose_promotion_option(
    const std::vector<PromotionOption>& options,
    const std::vector<std::string>& preferred_names,
    const std::vector<std::string>& disabled_names,
    int minimum_tier)
{
    PromotionSelection result;
    if (minimum_tier < 1 || minimum_tier > 6) {
        result.reason = "Promotion reward tier is outside the shop range";
        return result;
    }
    const auto disabled = [&](const std::string& name) {
        return std::find(disabled_names.begin(), disabled_names.end(), name) != disabled_names.end();
    };
    const auto preferred = [&](const std::string& name) {
        const auto it = std::find(preferred_names.begin(), preferred_names.end(), name);
        return it == preferred_names.end() ? 0
                                           : static_cast<int>(preferred_names.size() - (it - preferred_names.begin()));
    };
    const PromotionOption* best = nullptr;
    std::tuple<int, int, int, std::string> best_score;
    for (const auto& option : options) {
        if (!option.identity_verified || option.name.empty() || option.slot < 0 || option.tier < minimum_tier ||
            option.tier > 6 || disabled(option.name)) {
            continue;
        }
        const auto score =
            std::make_tuple(preferred(option.name), option.tier == minimum_tier ? 2 : 1, option.tier, option.name);
        if (!best || score > best_score) {
            best = &option;
            best_score = score;
        }
    }
    if (!best) {
        result.reason = "No verified non-disabled promotion option meets the required tier";
        return result;
    }
    result.found = true;
    result.option = *best;
    result.reason = preferred(best->name) > 0 ? "Choose the highest-priority verified carry or layer engine"
                                              : "Choose the first verified option meeting the promotion tier";
    return result;
}
