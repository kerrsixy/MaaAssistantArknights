#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace asst::spa
{
enum class EconomyAction
{
    StartRound,
    UpgradeShop,
    BuyCore,
    BuyGrowth,
    Refresh,
    BankViaOneCostTrade,
    ClearRemainder,
    PreserveByPact,
    Block
};

struct EconomyContext
{
    int round = 1;
    int funds = 0;
    int shop_level = 1;
    int upgrade_cost = 0;
    int refresh_cost = 1;
    int reserve_for_upgrade = 0;
    int purchase_deadline_ms = 0;
    bool special_preserve_pact = false;
    bool core_offer_present = false;
    bool growth_offer_present = false;
    bool one_cost_trade_available = false;
    bool allow_refresh = false;
    bool multiplayer = false;
    // Optional finite-horizon facts. When present, they replace the old
    // fixed-round upgrade heuristic; a round's budget is not a persistent
    // bank unless a verified pact explicitly preserves it.
    int total_rounds = 0;
    int terminal_round = 0;
    // Verified free-refresh charges granted by a pact, operator trait or item.
    // A free refresh spends no round funds, but still consumes one charge.
    int free_refreshes = 0;
};

struct EconomyDecision
{
    EconomyAction action = EconomyAction::Block;
    int spend = 0;
    std::string reason;
};

struct RoundBalanceRetention
{
    bool carries;
    int retained;
    int interest;
};

std::optional<RoundBalanceRetention> round_balance_retention(std::string_view strategy, int funds);
std::string round_balance_review_reason(std::string_view strategy, int funds);
EconomyDecision choose_economy_action(const EconomyContext& context);
}
