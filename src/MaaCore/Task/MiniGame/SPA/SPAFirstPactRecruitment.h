#pragma once

#include "SPAAcquisition.h"
#include <cstdint>

namespace asst::spa
{
struct RecruitmentEvidenceScope
{
    std::string execution_id;
    std::string transaction_id;
    int round = 0;

    bool operator==(const RecruitmentEvidenceScope&) const = default;
};

struct RecruitmentPhysicalSnapshot
{
    RecruitmentEvidenceScope scope;
    // Monotonically increasing observations within this execution, not wall time.
    std::uint64_t observation_sequence = 0;
    ObservedOperatorInventory inventory;
};

struct ConfirmedRecruitmentPromotionReward
{
    RecruitmentEvidenceScope scope;
    std::string receipt_id;
    std::string initial_operator_name;
    std::uint64_t selection_sequence = 0;
    std::uint64_t confirmation_sequence = 0;
    bool initial_form_verified = false;
    bool selection_confirmed = false;
    // The selected reward was confirmed and its page disappeared or advanced
    // to a distinct, verified next reward page. A selected card alone is not a receipt.
    bool page_advanced = false;
};

struct FirstPactRecruitmentTransaction
{
    RecruitmentEvidenceScope scope;
    std::string recipient_name;
    std::string recipient_first_pact;
    int initial_grants_quote = 0;
    std::uint64_t contact_sequence = 0;
    bool first_pact_verified = false;
    bool quote_verified = false;
    bool item_consumption_verified = false;
    bool isolated_roster_changes_verified = false;
    bool promotion_rewards_resolved = false;
    bool already_committed = false;
};

struct FirstPactRecruitmentReconciliation
{
    bool verified = false;
    RecruitmentEvidenceScope scope;
    std::vector<std::string> random_initial_grants;
    std::vector<std::string> confirmed_initial_rewards;
    std::vector<std::string> promoted_operators;
    int initial_value_before = 0;
    int initial_value_after = 0;
    // Replace the roster once after a successful compare-and-commit against
    // the pending transaction. Do not replay three-copy deductions or rewards.
    ObservedOperatorInventory final_inventory;
    std::string reason;
};

// Pure reconciliation of an isolated, already consumed phone. Complete means
// every field and bench body has verified identity AND form, with no duplicate
// physical slot, and no unresolved promotion; name hints cannot set it true.
// Rewards are supplied in observed order and must each consume a promotion
// opportunity caused by this transaction, including any real reward chain.
// The catalogue constrains pact membership only: it does not prove the server
// draw pool, tier limits or the probability of obtaining the strategy core.
// This currently supports a verified native first pact, not an inferred
// conversion/reordering of the recipient's pacts. Unknowns fail closed.
FirstPactRecruitmentReconciliation reconcile_first_pact_recruitment(
    const FirstPactRecruitmentTransaction& transaction,
    const RecruitmentPhysicalSnapshot& before,
    const RecruitmentPhysicalSnapshot& after,
    const std::vector<ConfirmedRecruitmentPromotionReward>& rewards);
}
