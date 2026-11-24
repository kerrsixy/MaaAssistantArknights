#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace asst::blackflow
{
// Each newly encountered target uses at most one full locator analysis and
// one restored-list analysis. Reserve those inside the total list-frame cap.
inline std::size_t required_repair_scanner_budget(std::size_t targets) noexcept
{
    return targets < 64 ? 128 - 2 * targets : 0;
}

struct RequiredRepairTarget
{
    std::string id;
    std::string name;
};

// Old account snapshots are deliberately absent from this API: they cannot
// turn an unseen current-scan target into a fresh observation.
inline std::vector<RequiredRepairTarget> required_module_repair_targets(
    const std::vector<RequiredRepairTarget>& required,
    const std::set<std::string>& unknown_names,
    const std::set<std::string>& primary_seen_ids,
    const std::set<std::string>& attempted_names)
{
    std::vector<RequiredRepairTarget> missing;
    std::set<std::string> ids, names;
    for (const auto& target : required) {
        if (target.id.empty() || target.name.empty() || !ids.insert(target.id).second ||
            !names.insert(target.name).second) {
            return {};
        }
        if (unknown_names.contains(target.name) && !primary_seen_ids.contains(target.id) &&
            !attempted_names.contains(target.name)) {
            missing.push_back(target);
        }
    }
    return missing;
}
}
