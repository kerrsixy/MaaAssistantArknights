#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "Config/Roguelike/BlackFlow/BlackFlowAccountProfile.h"
#include "Task/Roguelike/RoguelikeConfig.h"

namespace asst::blackflow
{
// Debug continuation data is separate from account maxima and new-run recruitment.
// A restored exploration is explicitly ineligible for unattended acceptance.
[[nodiscard]] std::optional<std::unordered_map<std::string, RoguelikeOper>> parse_resume_roster(
    const json::value& snapshot,
    const BlackFlowAccountProfile& verified_account,
    std::string* error);
}
