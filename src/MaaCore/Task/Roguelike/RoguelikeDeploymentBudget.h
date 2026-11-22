#pragma once

#include "Common/AsstBattleDef.h"

namespace asst::battle::roguelike
{
enum class StartingCoreAction
{
    Fallback,
    Deploy,
    Reserve,
};

inline StartingCoreAction starting_core_action(
    const DeploymentOper& core,
    int observed_cost,
    bool initial_blocking,
    bool urgent_home,
    bool legal_coverage)
{
    if (urgent_home || !legal_coverage || core.name.empty() || core.role == Role::Unknown || core.role == Role::Drone ||
        core.cooling) {
        return StartingCoreAction::Fallback;
    }
    if (core.available) {
        return StartingCoreAction::Deploy;
    }
    // Reserve only for an observed DP shortage after initial defence. A dim
    // card with ample/unknown DP may be locked for another reason.
    if (initial_blocking && observed_cost >= 0 && core.cost > observed_cost && core.cost > 0) {
        return StartingCoreAction::Reserve;
    }
    return StartingCoreAction::Fallback;
}

constexpr bool reserve_core_budget(bool established, bool urgent, int current_cost, int core_cost)
{
    // Bound waiting to five DP; initial defence and urgent roles take precedence.
    // 2026-11-02 从十费收紧到五费：预算否决实机里为了等十八费的核心，连续数十秒不下
    // 五费的炮灰，前线直接漏穿。等核心可以，但等不到一个便宜阻挡的时间就不值得。
    return established && !urgent && current_cost >= 0 && core_cost > current_cost && core_cost - current_cost <= 5;
}
}
