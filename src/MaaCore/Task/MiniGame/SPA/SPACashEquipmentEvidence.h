#pragma once

#include "SPAProjection.h"
#include <meojson/json.hpp>
#include <optional>
#include <string_view>
#include <vector>

namespace asst::spa
{
// A global inventory index is not an identity: unrelated animated operator
// badges can appear or disappear while the same equipment stays in place.
inline std::optional<size_t> stable_cash_source(cv::Point verified_source, const std::vector<cv::Point>& candidates)
{
    if (!equipment_inventory_probe_safe(verified_source)) {
        return std::nullopt;
    }
    std::optional<size_t> match;
    for (size_t index = 0; index < candidates.size(); ++index) {
        const auto point = candidates[index];
        if (!equipment_inventory_probe_safe(point)) {
            continue;
        }
        const auto delta = point - verified_source;
        if (delta.x * delta.x + delta.y * delta.y >= 15 * 15) {
            continue;
        }
        if (match) {
            return std::nullopt;
        }
        match = index;
    }
    return match;
}

inline bool cash_source_retry_allowed(int attempts, bool contact_attempted, bool committed, std::string_view stage)
{
    return attempts == 1 && !contact_attempted && !committed &&
           (stage == "source_identity" || stage == "cash_source_reacquisition" || stage == "cash_source_title_recheck");
}

inline bool cash_effect_verified(
    bool contact_attempted,
    bool source_and_target_verified,
    int source_round,
    int observed_round,
    int funds_before,
    int credit,
    int funds_after)
{
    return contact_attempted && source_and_target_verified && source_round >= 1 && source_round <= 20 &&
           observed_round == source_round && funds_before >= 0 && funds_before <= 999 && (credit == 1 || credit == 2) &&
           funds_after <= 999 && funds_after == funds_before + credit;
}

enum class CashReconciliation
{
    Verified,
    ObserveAgain,
    CloseUnknown,
    AlreadyClosed,
};

inline json::object restored_cash_pending(json::object pending)
{
    // A checkpoint retains replay protection, not uninterrupted observation
    // of the old wallet. Recovery/manual transactions may have intervened.
    pending["observation_checked"] = true;
    pending["attribution_closed_reason"] = "restored_contact_unknown";
    return pending;
}

inline bool cash_new_source_allowed(
    const json::object& pending,
    int round,
    int freshly_inspected_round,
    bool attempted_this_round)
{
    const int source_round = pending.find<int>("source_round").value_or(-1);
    const int previous_round = source_round >= 1 && source_round <= 20
                                   ? source_round
                                   : pending.find<int>("closed_observation_round").value_or(-1);
    return pending.find<bool>("observation_checked").value_or(false) && previous_round >= 1 && round > previous_round &&
           round <= 20 && freshly_inspected_round == round && !attempted_this_round;
}

// Only the uninterrupted wallet observations immediately after the contact
// can attribute credit. Once closed, a later sale or round income cannot do so.
inline CashReconciliation pending_cash_observation(const json::object& pending, const json::value& state, bool stopping)
{
    if (pending.find<bool>("observation_checked").value_or(false)) {
        return CashReconciliation::AlreadyClosed;
    }
    const bool contact = pending.find<bool>("contact_release_attempted").value_or(false);
    const bool identities = pending.find<bool>("source_and_target_verified").value_or(false);
    const int source_round = pending.find<int>("source_round").value_or(-1);
    const int round = state.get("round", -1);
    const bool known_round =
        state.get("round_known", false) && source_round >= 1 && source_round <= 20 && source_round == round;
    const bool known_page = state.get("page", std::string()) == "preparation" && !state.get("contradictory", true);
    if (known_page && known_round && state.get("funds_known", false) &&
        pending.get("equipment", std::string()) == "盟约之币" &&
        cash_effect_verified(
            contact,
            identities,
            source_round,
            round,
            pending.find<int>("funds_before").value_or(-1),
            pending.find<int>("cash_gain_quote").value_or(0),
            state.get("funds", -1))) {
        return CashReconciliation::Verified;
    }
    return known_page && known_round && contact && identities && !stopping &&
                   pending.find<int>("wallet_observations").value_or(0) == 0
               ? CashReconciliation::ObserveAgain
               : CashReconciliation::CloseUnknown;
}
}
