#pragma once

#include "Common/AsstBattleDef.h"
#include "SPAPlacementPolicy.h"
#include <string>

namespace asst::spa
{
inline battle::LocationType season_location_type(const std::string& name, battle::LocationType ordinary_location)
{
    return name == "“暴躁铁皮”" ? battle::LocationType::Melee : ordinary_location;
}

// Baseline attacks only. Skill-enabled attacks do not establish coverage
// during the skill cooldown.
inline bool has_basic_attack(battle::Role role, battle::SubRole branch)
{
    return role != battle::Role::Unknown && role != battle::Role::Medic && branch != battle::SubRole::Support_Bard &&
           branch != battle::SubRole::Caster_Phalanx && branch != battle::SubRole::Warrior_Liberator;
}

inline bool prioritizes_airborne_targets(battle::SubRole branch)
{
    return branch == battle::SubRole::Sniper_FastShot;
}

inline bool can_attack_air_with_basic_attack(battle::Role role, battle::SubRole branch)
{
    if (branch == battle::SubRole::Unknown || !has_basic_attack(role, branch)) {
        return false;
    }
    return branch == battle::SubRole::Warrior_Lord || role == battle::Role::Caster ||
           (role == battle::Role::Sniper && branch != battle::SubRole::Sniper_Bombarder);
}

inline bool accepts_direct_healing(battle::SubRole branch)
{
    return branch != battle::SubRole::Warrior_Musha && branch != battle::SubRole::Warrior_Reaper &&
           branch != battle::SubRole::Tank_Unyield;
}

// Normal body role only: skills, equipment and summons can change blocking.
// In particular, a liberator or tactician body cannot secure a ground lane
// merely because its profession is guard or vanguard.
inline PlacementRole combat_placement_role(battle::Role role, battle::SubRole branch)
{
    if (branch == battle::SubRole::Warrior_Liberator || branch == battle::SubRole::Pioneer_Tactician) {
        return PlacementRole::Ranged;
    }
    if (role == battle::Role::Medic || branch == battle::SubRole::Support_Bard) {
        return PlacementRole::Healer;
    }
    if (role == battle::Role::Tank || role == battle::Role::Warrior || role == battle::Role::Pioneer ||
        branch == battle::SubRole::Support_Craftsman || branch == battle::SubRole::Special_Executor ||
        branch == battle::SubRole::Special_Pusher || branch == battle::SubRole::Special_HookMaster ||
        branch == battle::SubRole::Special_Merchant || branch == battle::SubRole::Special_DollKeeper) {
        return PlacementRole::Frontline;
    }
    if (role == battle::Role::Sniper || role == battle::Role::Caster || role == battle::Role::Support ||
        branch == battle::SubRole::Special_Stalker || branch == battle::SubRole::Special_Geek ||
        branch == battle::SubRole::Special_Traper || branch == battle::SubRole::Special_Alchemist) {
        return PlacementRole::Ranged;
    }
    return PlacementRole::Unknown;
}
}
