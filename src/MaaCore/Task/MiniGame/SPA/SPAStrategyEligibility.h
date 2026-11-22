#pragma once

#include "SPAStrategyUnlockCatalogue.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace asst::spa
{
struct StrategyCandidate
{
    std::string id;
    std::string name;
    std::vector<std::string> primary_pacts;
    std::vector<std::string> required_pacts;
    std::vector<std::string> forbidden_bans;
    std::vector<std::string> required_initial_terms;
    int observed_support = 0;
    bool badge_verified = false;
    std::vector<std::string> layer_sources;
    int layer_cap_per_round = 0;
    bool layer_sources_verified = false;
    bool requires_core_roles = false;
    // A faction strategy is invalid until at least one primary pact is
    // verified active in the current run. Empty primary_pacts is a generic
    // transition strategy and does not need a faction activation.
    bool requires_active_primary_pact = true;
};

struct StrategyEligibilityContext
{
    std::string initial_brief;
    std::vector<std::string> disabled_pacts;
    std::vector<std::string> disabled_strategy_ids;
    std::vector<std::string> active_pacts;
    int round = 1;
    bool core_layer_operator_verified = false;
    bool damage_carry_verified = false;
    bool opening_selection = false;
};

// A derived formation belongs to one observation. Keep it out of the stored
// caller assertions, so later deployments and withdrawals can change pacts.
StrategyEligibilityContext strategy_context_for_formation(
    const StrategyEligibilityContext& assertions,
    bool formation_complete,
    const std::vector<std::string>& active_pacts);

// Replace an already justified paid refresh, preserving ordinary free charges.
// The native executor must independently verify identity, payment and offers.
bool prefer_higher_tier_strategy_refresh(
    const std::string& strategy_id,
    int shop_level,
    int funds,
    int uses_this_round,
    bool paid_refresh_planned);

struct StrategyEligibility
{
    bool eligible = false;
    bool unclaimed_priority = false;
    int score = 0;
    std::vector<std::string> reasons;
};

struct StrategySelection
{
    bool found = false;
    StrategyCandidate candidate;
    StrategyEligibility eligibility;
};

// Opening grid slots are local to the current page, never persistent strategy identities.
struct OpeningStrategyCard
{
    int index = -1;
    bool available = false;
    bool badge_known = false;
    bool completed = false;
    double support_score = 0;
};

// Opening the right-hand detail panel translates the four-column grid left
// by 264 pixels. The fourth row is only partially visible above the footer.
inline std::array<int, 4> opening_strategy_card_rect(int index, bool details_open)
{
    const int y = 182 + index / 4 * 140;
    return { (details_open ? 391 : 655) + index % 4 * 140, y, 125, std::min(128, 705 - y) };
}

int choose_opening_strategy_card(const std::vector<OpeningStrategyCard>& cards, bool prefer_unclaimed = true);

struct OpeningStrategyContext
{
    bool pact_availability_known = false;
    std::vector<std::string> disabled_pacts;
    std::vector<std::string> occupied_strategy_ids;
    std::string mode;
    std::vector<std::string> observed_available_pacts;
};

std::optional<StrategyUnlockRule> strategy_unlock_rule(std::string_view id);
bool strategy_available_in_mode(std::string_view id, std::string_view mode);

// A scheduled grant is a reason to inspect the inventory, not proof it arrived.
bool strategy_grants_preparation_equipment(std::string_view id, int round);

struct OpeningStrategyAssessment
{
    bool known = false;
    bool eligible = false;
    int score = 0;
    std::string reason;
};

// Heuristic opening value based on actual strategy identity and current bans.
// No faction activation, engine or carry is required at this opening stage.
OpeningStrategyAssessment assess_opening_strategy(const std::string& id, const OpeningStrategyContext& context);

struct SetupSelection

{
    bool valid = false;

    std::string core_pact;

    std::vector<std::string> support_pacts;

    std::vector<std::string> reasons;
};

SetupSelection choose_setup_pacts(
    const std::string& preferred_core,
    const std::vector<std::string>& preferred_support,
    const std::vector<std::string>& available_core,
    const std::vector<std::string>& available_support,
    const std::vector<std::string>& disabled);

StrategyEligibility evaluate_strategy_eligibility(const StrategyCandidate&, const StrategyEligibilityContext&);
StrategySelection
    choose_strategy_with_fallback(const std::vector<StrategyCandidate>&, const StrategyEligibilityContext&);
}
