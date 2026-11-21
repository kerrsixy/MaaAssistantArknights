#pragma once

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "SPACombatRole.h"

namespace asst::spa
{
inline PlacementRole operator_placement_role(const std::string& name)
{
    // This season-only strategy gift is absent from the ordinary operator
    // roster. Its training data and live panel both identify a block-3 body.
    if (name == "“暴躁铁皮”") {
        return PlacementRole::Frontline;
    }
    if (name == "Miss.Christine") {
        return PlacementRole::Ranged;
    }
    const auto props = BattleData.find_first_oper(battle::Role::Unknown, name, true);
    return props ? combat_placement_role(props->role, props->sub_role) : PlacementRole::Unknown;
}
}
