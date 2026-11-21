#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace asst::blackflow
{
inline bool is_stashed_recruit_option(std::string_view text)
{
    return text == "尝试发出求援信号" || text == "召集同伴";
}

inline std::vector<std::string> after_stashed_recruit_options(
    const std::vector<std::string>& preferences,
    const std::vector<std::string>& enabled_options)
{
    std::vector<std::string> result;
    for (const auto& name : preferences) {
        if (!is_stashed_recruit_option(name) && std::ranges::find(enabled_options, name) != enabled_options.end()) {
            result.emplace_back(name);
        }
    }
    if (result.empty()) {
        for (const auto& name : enabled_options) {
            if (!is_stashed_recruit_option(name)) {
                result.emplace_back(name);
            }
        }
    }
    return result;
}

struct StashedRecruitForecast
{
    std::string name;
    int priority = 0;
    int budget = 0;
    bool strength_verified = false;
    bool useful = false;
};

// This is an entry budget, never evidence of the price on a candidate card.
// The ordinary recruiter must still read and enforce the actual card price.
inline std::optional<StashedRecruitForecast>
    choose_stashed_recruit_target(const std::vector<StashedRecruitForecast>& forecasts, std::optional<int> hope)
{
    std::optional<StashedRecruitForecast> best;
    if (!hope || *hope < 0) {
        return best;
    }
    for (const auto& candidate : forecasts) {
        if (!candidate.strength_verified || !candidate.useful || candidate.name.empty() || candidate.priority <= 0 ||
            candidate.budget < 0 || candidate.budget > *hope) {
            continue;
        }
        if (!best || candidate.priority > best->priority ||
            (candidate.priority == best->priority && candidate.budget < best->budget) ||
            (candidate.priority == best->priority && candidate.budget == best->budget && candidate.name < best->name)) {
            best = candidate;
        }
    }
    return best;
}
}
