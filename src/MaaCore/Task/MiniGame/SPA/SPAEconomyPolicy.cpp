#include "SPAEconomyPolicy.h"
#include "SPAStrategyCatalogue.h"
#include <algorithm>

std::optional<asst::spa::RoundBalanceRetention> asst::spa::round_balance_retention(std::string_view strategy, int funds)
{
    if (funds < 0 || strategy == "band_random" ||
        std::none_of(
            StrategyCatalogue.begin(),
            StrategyCatalogue.end(),
            [&](const auto& item) { return item.id == strategy; })) {
        return std::nullopt;
    }
    if (strategy == "band_cannot") {
        return RoundBalanceRetention { true, funds, funds >= 5 ? 1 : 0 };
    }
    return RoundBalanceRetention { false, 0, 0 };
}

std::string asst::spa::round_balance_review_reason(std::string_view strategy, int funds)
{
    const auto rule = round_balance_retention(strategy, funds);
    if (!rule) {
        return "No verified affordable purchase; balance retention is unverified";
    }
    if (!rule->carries) {
        return "No verified affordable purchase; the ordinary balance clears at battle start";
    }
    return "利滚利 retains " + std::to_string(rule->retained) + " funds and grants " + std::to_string(rule->interest) +
           " extra next-round gold if play continues";
}

asst::spa::EconomyDecision asst::spa::choose_economy_action(const EconomyContext& c)
{
    if (c.round < 1 || c.funds < 0 || c.shop_level < 1 || c.refresh_cost < 1 || c.purchase_deadline_ms < 0 ||
        c.free_refreshes < 0) {
        return { EconomyAction::Block, 0, "Incomplete economy state" };
    }
    if (c.funds == 0) {
        if (c.free_refreshes > 0 && c.allow_refresh && c.purchase_deadline_ms >= (c.multiplayer ? 1800 : 900)) {
            return { EconomyAction::Refresh, 0, "Use a verified free refresh; preserve the round budget" };
        }
        return { EconomyAction::StartRound, 0, "No preparation funds remain" };
    }
    // A true preserve pact takes precedence over the ordinary end-of-round wipe.
    if (c.special_preserve_pact) {
        return { EconomyAction::PreserveByPact, 0, "Preserve funds only with an explicitly verified pact" };
    }
    // Card tiers are gated by the observed shop level, never by the round
    // number.  A caller that needs a higher tier supplies the upgrade reserve;
    // the round only affects the opportunity cost of waiting for the price to
    // fall and the remaining combat horizon.
    if (c.upgrade_cost > 0 && c.funds >= c.upgrade_cost && c.reserve_for_upgrade > 0) {
        return { EconomyAction::UpgradeShop,
                 c.upgrade_cost,
                 "Upgrade because the observed shop level is below the required card tier" };
    }
    if (c.core_offer_present) {
        return { EconomyAction::BuyCore, 1, "Buy a verified primary-pact core before refreshing" };
    }
    if (c.growth_offer_present) {
        return { EconomyAction::BuyGrowth, 1, "Buy a verified stacking source" };
    }
    const int refresh_spend = c.free_refreshes > 0 ? 0 : c.refresh_cost;
    const int after_refresh = c.funds - refresh_spend;
    const bool enough_for_next_upgrade = c.reserve_for_upgrade <= 0 || after_refresh >= c.reserve_for_upgrade;
    // In multiplayer, the timer is a hard constraint: do not spend a refresh
    // cycle if the remaining purchase window is too short to verify the result.
    if (c.allow_refresh && c.purchase_deadline_ms >= (c.multiplayer ? 1800 : 900) && enough_for_next_upgrade &&
        after_refresh >= 0) {
        return { EconomyAction::Refresh,
                 refresh_spend,
                 c.free_refreshes > 0
                     ? "Use a verified free refresh before spending round funds"
                     : "Refresh only inside the formation window and after reserving the next upgrade" };
    }
    // The ordinary wipe can be converted into a bank only by a verified one-cost
    // buy/sell cycle. This is deliberately conservative and never assumes that
    // an arbitrary unit refunds its full price.
    if (c.one_cost_trade_available && c.funds >= 1 && c.purchase_deadline_ms >= (c.multiplayer ? 2200 : 1200)) {
        return { EconomyAction::BankViaOneCostTrade, 1, "Use a verified one-cost buy/sell bank before the wipe" };
    }
    return { EconomyAction::ClearRemainder, c.funds, "Ordinary remainder is cleared at battle confirmation" };
}
