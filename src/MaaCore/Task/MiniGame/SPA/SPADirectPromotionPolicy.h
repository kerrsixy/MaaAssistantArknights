#pragma once

#include "SPACombatStats.h"
#include "SPAConsumablePolicy.h"
#include <string>

namespace asst::spa
{
// This is one known physical initial body, not the three-copy synthesis
// transaction. A free slot and an observed field identity are required even
// when the advanced item destroys itself immediately.
inline bool direct_promotion_recipient_supported(
    const std::string& name,
    int initial_copies,
    int elite_copies,
    bool deployed,
    bool pose_verified,
    bool slots_known,
    int occupied_slots,
    bool carry,
    int fixed_primary_growth_gain)
{
    const auto initial = season_combat_stats(name, false);
    const auto elite = season_combat_stats(name, true);
    if (initial_copies != 1 || elite_copies != 0 || !deployed || !pose_verified || !slots_known || !initial || !elite ||
        initial->level == elite->level || occupied_slots < 0 || occupied_slots >= initial->equipment_slots) {
        return false;
    }
    // Fixed-phase growth is modeled separately from conditional kill/skill
    // caps. Combat value only applies to an already selected output/core.
    return fixed_primary_growth_gain > 0 ||
           (carry && (elite->attack > initial->attack || elite->max_hp > initial->max_hp ||
                      elite->equipment_slots > initial->equipment_slots));
}

inline bool direct_promotion_form_transition(
    const PromotionConsumableQuote& quote,
    int contact_round,
    int observed_round,
    bool initial_form_observed,
    bool current_field_identity_verified,
    bool current_elite_form_verified,
    int initial_copies,
    int elite_copies)
{
    return quote.effect == PromotionConsumableEffect::DirectPromotion &&
           promotion_consumable_due(quote, contact_round, observed_round) && initial_form_observed &&
           current_field_identity_verified && current_elite_form_verified && initial_copies == 1 && elite_copies == 0;
}
}
