#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace asst::spa
{
enum class ActionKind
{
    Observe,
    BuyOperator,
    BuyEquipment,
    Upgrade,
    Refresh,
    UseStrategy,
    Ready
};

struct Candidate
{
    ActionKind kind = ActionKind::Observe;
    std::string target;
    int cost = 0;
    double survival_gain = 0;
    double permanent_layers = 0;
    double future_income = 0;
    bool legal = false;
    bool effect_verified = false;
    bool preserves_required_units = false;
    // A faction card is not useful merely because its operator name is visible:
    // the pact must be active and the current shop/book level must be able to
    // reach the card's minimum tier.
    bool requires_active_pacts = false;
    bool active_pacts_verified = false;
    int minimum_shop_level = 0;
};

struct RoundState
{
    std::string run_id;
    int round = 0;
    int funds = 0;
    int remaining_rounds = 0;
    int revision = 0;
    bool observation_complete = false;
    bool map_verified = false;
    bool lineup_verified = false;
    bool preparation = false;
    bool survival_at_risk = false;
    bool can_carry_funds = false;
    bool carry_rule_verified = false;
    bool ready_verified = false;
    int remaining_action_budget = 0;
    int shop_level = 0;
};

struct Decision
{
    ActionKind kind = ActionKind::Observe;
    std::string target;
    std::string reason;
    int expected_revision = 0;
    int expected_cost = 0;
    std::string run_id;
    int round = 0;
};

// Ordinary rounds must clear funds before battle. A special strategy/equipment
// spend may retain funds only when it has an explicit review reason.
bool battle_entry_allowed(
    bool round_only,
    bool auto_run,
    int funds,
    bool spending_reviewed,
    const std::string& spending_reason);

// Inputs are verified effects, not keyword matches from the research catalogue.
class Planner
{
public:
    static Decision choose(const RoundState& state, const std::vector<Candidate>& candidates);
    static bool can_execute(const RoundState& state, const Decision& decision);
};

enum class PromotionOrder
{
    BuyThird,
    UpgradeThenBuyThird
};

struct PromotionWindow
{
    int shop_level = 0;
    int funds = -1;
    int third_copy_cost = -1;
    int upgrade_cost = -1;
    bool upgrade_preserves_target_verified = false;
    bool immediate_promotion_required = false;
};

struct PromotionDecision
{
    PromotionOrder order = PromotionOrder::BuyThird;
    int reward_tier = 0;
    int temporary_offer_count = 3;
    int temporary_offer_cost = 0;
};

// The third-copy synthesis page offers a small set of operators one tier
// above the current shop.  OCR/UI code supplies the observed tier and slot;
// this pure selector applies route priorities without pretending that a card
// was clicked or that the promotion was committed.
struct PromotionOption
{
    std::string name;
    int tier = 0;
    int slot = -1;
    bool identity_verified = false;
};

struct PromotionSelection
{
    bool found = false;
    PromotionOption option;
    std::string reason;
};

// Reward tier follows the shop at promotion time, rather than the promoted operator's tier.
std::optional<PromotionDecision> plan_promotion(const PromotionWindow& window);
// Default synthesis threshold.  Special thresholds must come from the
// operator's current-season trait text or an explicit metadata override; the
// function is deliberately kept as a conservative fallback for callers that
// have not loaded the trait catalogue yet.
int promotion_copies_required(const std::string& operator_name);
// Parse a current-season trait description.  This supports any future
// two-copy (or other non-default) synthesis operator without adding a name
// specific branch to the planner.
int promotion_copies_required_from_trait(const std::string& operator_name, const std::string& trait_text);

// PRTS current-season battle limits.  The UI counter includes a small
// transition margin; these are gameplay limits used for planning/diagnostics.
int battle_time_limit_seconds(int round, bool leader_round);
PromotionSelection choose_promotion_option(
    const std::vector<PromotionOption>& options,
    const std::vector<std::string>& preferred_names,
    const std::vector<std::string>& disabled_names,
    int minimum_tier);

struct StrategyProgress
{
    std::string band_id;
    std::optional<int> wins;
    bool unlocked = false;
    bool badge_verified = false;
    bool solo_eligible_verified = false;
    bool silver_badge_verified = false;
};

// An observed result is not a badge; only a subsequent game report confirms completion.
struct Settlement
{
    std::string run_id;
    std::string band_id;
    bool final_result_verified = false;
    bool victory = false;
    bool strategy_credit_verified = false;
};

class ProgressLedger
{
public:
    bool apply(const Settlement& settlement, StrategyProgress& progress);
    static bool complete(const StrategyProgress& progress);
    static bool silver_complete(const StrategyProgress& progress);
    static bool can_schedule_solo(const StrategyProgress& progress, bool require_silver = false);
    static std::optional<std::string>
        next_solo(const std::vector<StrategyProgress>& progress, bool require_silver = false);
    static bool next_report_confirms_win(
        const StrategyProgress& before,
        const StrategyProgress& after,
        std::string_view actual_band_id,
        bool verified_victory);

private:
    std::vector<std::string> m_credited_runs;
};
}
