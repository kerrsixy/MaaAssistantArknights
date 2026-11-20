#pragma once

#include "SPAModeData.h"
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace asst::spa
{
struct ModePactRules
{
    std::string id;
    std::vector<std::string> active;
    std::vector<std::string> inactive;
};

inline std::optional<ModePactRules> season_mode_pact_rules(const std::string& mode, int difficulty)
{
    std::string id;
    if (mode == "independent" && difficulty >= 1 && difficulty <= 20) {
        id = "mode_single_" + std::to_string(difficulty);
    }
    else if (mode == "quick") {
        id = "mode_single_funny";
    }
    else if (mode == "joint" && difficulty >= 1 && difficulty <= 4) {
        const std::array<std::string_view, 4> joint_ids { "mode_multi_funny",
                                                          "mode_multi_normal",
                                                          "mode_multi_hard",
                                                          "mode_multi_abyss" };
        id = joint_ids[difficulty - 1];
    }
    else {
        return std::nullopt;
    }
    const auto record =
        std::find_if(SeasonModePactCatalogue.begin(), SeasonModePactCatalogue.end(), [&](const auto& row) {
            return row.id == id;
        });
    if (record == SeasonModePactCatalogue.end()) {
        return std::nullopt;
    }
    ModePactRules result;
    result.id = id;
    for (size_t index = 0; index < SeasonModePactNames.size(); ++index) {
        const auto bit = std::uint32_t(1) << index;
        if (record->active_pacts & bit) {
            result.active.emplace_back(SeasonModePactNames[index]);
        }
        if (record->inactive_pacts & bit) {
            result.inactive.emplace_back(SeasonModePactNames[index]);
        }
    }
    return result;
}

// Mode restrictions and observed/account restrictions are independent facts.
// Rebuild from their sources when the actual selected difficulty changes.
inline std::vector<std::string> effective_mode_pact_bans(
    const std::string& mode,
    int difficulty,
    const std::vector<std::string>& declared,
    const std::vector<std::string>& observed)
{
    auto result = declared;
    result.insert(result.end(), observed.begin(), observed.end());
    if (const auto rules = season_mode_pact_rules(mode, difficulty)) {
        result.insert(result.end(), rules->inactive.begin(), rules->inactive.end());
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
}
