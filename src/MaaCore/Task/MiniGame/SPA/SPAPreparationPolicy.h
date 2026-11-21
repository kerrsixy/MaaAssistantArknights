#pragma once
#include "SPAAcquisition.h"
#include <map>
#include <meojson/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace asst::spa
{
struct OwnedOperator
{
    // Total physical copies in the native ledger, including elite bodies.
    int copies = 0;
    bool elite = false;
    bool deployed = false;
    std::optional<OperatorFormCounts> form_counts;
    // Explicit total retained when a legacy row lacks the form split.
    std::optional<int> known_physical_copies;
};

// Legacy elite rows with multiple copies have no trustworthy form split.
// Exact counts may include initial copies awaiting an observed synthesis.
std::optional<OperatorFormCounts> owned_form_counts(const OwnedOperator& owned);
bool set_owned_form_counts(OwnedOperator& owned, OperatorFormCounts counts);
bool add_initial_copies(OwnedOperator& owned, int count = 1);
bool add_purchased_operator(OwnedOperator& owned, bool elite);
bool promote_initial_copies(OwnedOperator& owned, int required);
bool restore_owned_operator(const json::value& row, OwnedOperator& owned);
json::object owned_operator_snapshot(const std::string& name, const OwnedOperator& owned);
// Aggregate quantity from the reconciled roster and a fresh field HUD, not
// an independent visual census or a proof of per-body field identities.
std::optional<int> bench_population_from_roster(const std::map<std::string, OwnedOperator>& owned, int field_count);

struct ShopOffer
{
    std::string name;
    int price = -1;
    int right_slot = -1;
};

bool is_one_coin_bank_offer(const ShopOffer& offer);

struct PreparationPolicy
{
    std::string primary_pact;
    // Explicit GUI choice; a banned choice automatically restores free planning.
    std::string requested_primary_pact;
    std::string selected_strategy_id;
    // Joint-only, exact occupied-card observations. Route pressure is inferred.
    std::vector<std::string> occupied_strategy_ids;
    // When true, primary_pact is a live preference rather than an opening
    // promise.  The preparation controller may pivot to another enabled
    // faction after observing the current shop and roster.
    std::vector<std::string> enabled_core_pacts;
    std::vector<std::string> disabled_pacts;
    std::vector<std::string> carries;
    std::vector<std::string> support_pacts;
    // Per-operator synthesis thresholds obtained from the current-season
    // trait catalogue.  Missing entries use the normal three-copy rule.
    std::map<std::string, int> promotion_copies;
    std::map<std::string, OwnedOperator> owned;
    bool tactical_elite_purchase_pending = false;
    int bench_occupied = 0;
    int minimum_recruit_price = 2;
    bool allow_refresh = true;
    // When enabled, infer one or two secondary pacts from the operators that
    // share members with the live primary pact.  Explicit support_pacts still
    // take precedence; this flag is for the normal auto route.
    bool derive_support_pacts = false;
    // Keep re-ranking enabled after the first provisional faction choice.
    // This prevents an early opening preference from locking later shops.
    // StrongholdProtocol also exposes this as dynamic_primary_pact=true when
    // the caller supplies an explicit preferred faction name.
    bool dynamic_primary_pact = false;
    bool primary_locked_after_activation = false;
    int round = 1;
    int primary_deadline_round = 6;
    int shop_level = 0;
    int free_refreshes = 0;
    bool shop_frozen_this_round = false;
    bool allow_freeze = true;
    // Phase-1 runner: prefer the cheapest verified affordable card and basic
    // upgrades; phase-2 dynamic scoring can opt in later.
    bool basic_mode = false;
    // Keep the guarded phase-one executor while replacing only the
    // preparation decision policy with faction/economy scoring.
    bool dynamic_decision_mode = false;
    bool early_board_ready = false;
    int early_frontline_count = 0;
    int deployed_population = -1;
    int last_round_life_loss = -1;
    int current_life = -1;
    bool boss_preparation = false;
    int shop_discount_wait_level = 0;
    int shop_discount_wait_round = 0;
    // Immediate board value from observed roles; pair bonuses are applied
    // only to cards actually offered, never by paying to force a pair.
    std::map<std::string, int> early_board_scores;
    // At eight deployed units, the native controller evaluates actual
    // replacement targets for visible offers. Missing means not evaluated;
    // zero means no executable positive exchange in the current field model.
    std::map<std::string, int> full_field_offer_improvements;
    // Same-frame HUD readings only. Missing pacts remain unknown, never zero.
    std::map<std::string, int> observed_pact_layers;
    // Set from the same preparation's verified formation. A historical
    // activation lock or a positive layer counter alone is insufficient.
    bool growth_primary_active_verified = false;
    // A planning horizon, not proof of the actual boss round. Zero is unknown.
    int growth_scenario_horizon_round = 0;
    // The native controller opts in only after a complete full-field check,
    // with no pending replacement/identity recovery and ordinary funds expiry.
    // A stored operator is a future sale candidate, never already credited cash.
    bool one_coin_bank_allowed = false;
    // Scenario horizon only: a later preparation is planned, not guaranteed.
    int economy_scenario_horizon_round = 0;
    std::set<std::string> released_transitions;
    // Deterministic phase-one shop upgrade schedule.  Dynamic mode may leave
    // this empty and decide from the live economy instead.
    std::vector<int> basic_upgrade_rounds;
};

std::vector<std::string> planning_support_pacts(
    const PreparationPolicy& policy,
    const std::vector<ShopOffer>& offers,
    const std::map<std::string, std::vector<std::string>>& pacts);

bool ordinary_purchase_completes_promotion(const PreparationPolicy& policy, const std::string& name);

// The original operator becomes elite; the separate free higher-tier reward
// is an initial operator. An unknown source must not erase the current roster.
bool reconcile_promotion_reward(PreparationPolicy& policy, const std::string& source, const std::string& reward);
enum class PreparationAction
{
    Review,
    Buy,
    Refresh,
    Freeze
};

struct PreparationChoice
{
    PreparationAction action = PreparationAction::Review;
    std::string name;
    int price = 0;
    int right_slot = -1;
    std::string reason;
    std::string selected_pact;
    bool free_refresh = false;
    bool is_equipment = false;
    bool banked_value_purchase = false;
    int growth_score = 0;
};

struct DynamicPactSignal
{
    std::string pact;
    int required_members = 0;
    int distinct_members = 0;
    int reachable_members = 0;
    int deployed_members = 0;
    int shop_members = 0;
    int minimum_shop_level = 0;
    int growth_engine_owned = 0;
    int growth_engine_eligible = 0;
    int growth_engine_offers = 0;
    int output_owned = 0;
    int output_offers = 0;
    int score = 0;
    int inferred_contention = 0;
    // Diagnostics used by the controller and live audit.  A route can have
    // a promising role card without being activation-ready yet; that is why
    // these are kept separate instead of collapsing everything into `viable`.
    bool activation_ready = false;
    bool book_window_met = false;
    bool role_signal = false;
    bool viable = false;
    // Three actual Aegir members supply the native consumption growth
    // mechanism. This is availability, not proof of a connected chain.
    bool native_growth_available = false;
};

struct DynamicPactChoice
{
    bool valid = false;
    std::string primary_pact;
    int score = 0;
    std::vector<DynamicPactSignal> ranking;
    std::string reason;
    bool guide_restricted = false;
    bool noncore_guide_pending = false;
};

// Activation count alone is not enough for a battle-ready route. The current
// deployed roster must contain both a layer-growth engine and a terminal/output
// candidate for the selected primary faction.
struct PrimaryRoleAssessment
{
    int growth_engine_owned = 0;
    int output_owned = 0;
    std::vector<std::string> growth_engines;
    std::vector<std::string> outputs;

    bool complete() const { return growth_engine_owned > 0 && output_owned > 0; }
};

bool is_primary_output_candidate(const std::string& name, const std::string& pact);
bool is_terminal_output_candidate(const std::string& name, const std::string& pact);

// Tactical value of a nonmember growth helper with actual eligible recipients.
// This does not establish its position, trigger count or projected layer gain.
int primary_growth_support_value(
    const std::string& name,
    const PreparationPolicy& policy,
    const std::map<std::string, std::vector<std::string>>& pacts);

// An established route admits its own members, useful primary-growth
// helpers and compatible secondary-pact members. Opening role repairs use
// a separate policy before this commitment.
bool belongs_to_planned_lineup(
    const std::string& name,
    const PreparationPolicy& policy,
    const std::map<std::string, std::vector<std::string>>& pacts);

PrimaryRoleAssessment assess_primary_roles(
    const std::string& pact,
    const std::vector<std::string>& deployed_names,
    const std::map<std::string, std::vector<std::string>>& pacts);

// Selects a live primary faction from observed offers and owned operators.
// This is deliberately separate from opening strategy selection: an opening
// strategy may be useful while the corresponding faction never appears.
DynamicPactChoice choose_dynamic_primary_pact(
    const PreparationPolicy& policy,
    const std::vector<ShopOffer>& offers,
    const std::map<std::string, std::vector<std::string>>& pacts,
    int available_funds = -1);

struct ShopUpgradeHold
{
    std::string reason;
    std::string promotion_target;
    int promotion_recruit_budget = 0;
    std::optional<int> next_round_upgrade_price;
};

bool should_upgrade_dynamic_shop(
    const PreparationPolicy& policy,
    int funds,
    int upgrade_price,
    const std::vector<ShopOffer>& offers,
    const std::map<std::string, std::vector<std::string>>& pacts,
    std::optional<ShopUpgradeHold>* hold = nullptr);

struct FixedGrowthPurchaseReservation
{
    std::string name;
    int price = 0;
    int current_layers = 0;
    int scenario_horizon_round = 0;
    int fixed_gain_per_trigger = 0;
    int remaining_phase_triggers = 0;
    int fixed_layer_additions_if_retained = 0;
    bool placement_required = true;
    bool reserve_before_upgrade = false;
};

// Only the actual purchase policy's selected first copy is considered. The
// fixed contribution assumes that the source is placed in time (unless its
// trait works on the bench), retained, and its printed pact remains active.
// Conditional caps, dynamic neighbors and other growth are never added here.
std::optional<FixedGrowthPurchaseReservation> fixed_growth_purchase_reservation(
    const PreparationPolicy& policy,
    int funds,
    int upgrade_price,
    const std::vector<ShopOffer>& offers,
    const std::map<std::string, std::vector<std::string>>& pacts);

int dynamic_target_shop_level(
    const PreparationPolicy& policy,
    const std::map<std::string, std::vector<std::string>>& pacts);

struct PromotionRewardRank
{
    std::string name;
    int score = 0;
};

// Only the observed free choices are ranked. A full bench or zero balance
// cannot turn a synthesis reward into an ordinary-shop refresh decision.
std::vector<PromotionRewardRank> rank_promotion_rewards(
    const PreparationPolicy& policy,
    const std::vector<std::string>& choices,
    const std::map<std::string, std::vector<std::string>>& pacts);

bool promotion_reward_starts_chain(const PreparationPolicy& policy, const std::string& reward);

bool promotion_reward_chain_observed(
    bool expected,
    bool selection_confirmed,
    int free_slots,
    const std::vector<std::string>& previous_choices,
    const std::vector<std::string>& current_choices);

bool bench_operator_can_deploy(const PreparationPolicy& policy, const std::string& name);

std::optional<std::string> choose_pending_deployment(
    const PreparationPolicy& policy,
    int remaining,
    const std::vector<std::string>& target_names);
PreparationChoice choose_preparation(
    const PreparationPolicy& policy,
    int funds,
    const std::vector<ShopOffer>& offers,
    const std::map<std::string, std::vector<std::string>>& pacts);
}
