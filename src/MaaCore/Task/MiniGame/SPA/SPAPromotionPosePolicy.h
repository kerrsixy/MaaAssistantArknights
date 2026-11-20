#pragma once
#include "SPAAcquisition.h"
#include <algorithm>
#include <set>
#include <utility>
#include <vector>

namespace asst::spa
{
// Each observed point must already have passed a fresh same-name, elite-level,
// field-control probe and returned to the same collapsed preparation camera.
// Retaining a verified subset is not a complete census of either form.
inline std::vector<std::pair<int, int>> retain_promoted_elite_poses(
    const OperatorFormCounts& counts,
    const std::vector<std::pair<int, int>>& prior,
    const std::vector<std::pair<int, int>>& observed)
{
    if (counts.initial < 0 || counts.initial > 18 || counts.elite < 1 || counts.elite > 18 ||
        counts.initial + counts.elite > 18 || prior.size() > 8 || observed.size() > 8 ||
        observed.size() > static_cast<std::size_t>(counts.elite)) {
        return {};
    }
    const std::set<std::pair<int, int>> prior_points(prior.begin(), prior.end());
    const std::set<std::pair<int, int>> observed_points(observed.begin(), observed.end());
    if (prior_points.size() != prior.size() || observed_points.size() != observed.size() ||
        !std::includes(prior_points.begin(), prior_points.end(), observed_points.begin(), observed_points.end())) {
        return {};
    }
    // A new elite can coexist with an older elite. Multiple positive probes
    // are contradictory only when they exceed the physical elite ledger.
    return observed;
}
}
