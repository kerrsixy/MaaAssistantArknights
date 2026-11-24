#pragma once

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <vector>

#include "Config/Roguelike/RoguelikeShoppingConfig.h"

namespace asst::blackflow
{
// This repairs a missing basic duty; it does not estimate healing or combat DPS.
inline bool prioritizes_first_medic(
    const RoguelikeGoods& goods,
    const std::unordered_map<battle::Role, std::size_t>& trained_roles,
    std::optional<int> hope)
{
    if (!goods.is_recruit_ticket || goods.min_hope <= 0 || !hope || *hope < goods.min_hope) {
        return false;
    }
    const auto medic = trained_roles.find(battle::Role::Medic);
    if (medic != trained_roles.end() && medic->second > 0) {
        return false;
    }
    // Explicit mixed-profession tickets take precedence over legacy single-role data.
    return goods.recruit_roles.empty()
               ? goods.recruit_role == battle::Role::Medic
               : std::ranges::find(goods.recruit_roles, battle::Role::Medic) != goods.recruit_roles.end();
}

inline void prioritize_first_medic(
    std::vector<RoguelikeGoods>& goods,
    const std::unordered_map<battle::Role, std::size_t>& trained_roles,
    std::optional<int> hope)
{
    // Keep the existing relative order within each tier. The caller still checks
    // observed cash, reservations, stock, price, and the ordinary demand filters.
    std::stable_partition(goods.begin(), goods.end(), [&](const RoguelikeGoods& item) {
        return prioritizes_first_medic(item, trained_roles, hope);
    });
}
}
