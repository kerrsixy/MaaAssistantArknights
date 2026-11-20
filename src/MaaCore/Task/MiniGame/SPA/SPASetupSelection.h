#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace asst::spa
{
struct SetupCoreObservation
{
    std::string name;
    std::optional<bool> selected;
};

// Unknown/clipped icons are not evidence of an unselected group. After
// scrolling, allowed_group restricts reconciliation to pre-scan identities.
inline std::optional<std::string> choose_verified_setup_core(
    const std::vector<SetupCoreObservation>& cores,
    const std::string& preferred,
    const std::set<std::string>& allowed_group = {})
{
    std::optional<std::string> fallback;
    for (const auto& core : cores) {
        if (core.name.empty() || core.selected != true ||
            (!allowed_group.empty() && !allowed_group.contains(core.name))) {
            continue;
        }
        if (core.name == preferred) {
            return core.name;
        }
        if (!fallback) {
            fallback = core.name;
        }
    }
    return fallback;
}
}
