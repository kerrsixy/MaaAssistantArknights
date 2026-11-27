#pragma once

#include "BlackFlowCombatReadinessTypes.h"
#include "BlackFlowRecruitmentDuty.h"

namespace asst::blackflow
{
[[nodiscard]] std::optional<std::vector<CombatReadinessProfile>>
    parse_combat_readiness_profiles(const json::value& input, std::string* error = nullptr);

[[nodiscard]] std::string combat_readiness_profile_terms(const CombatReadinessProfile& profile);

[[nodiscard]] std::string combat_roster_scope(
    const RoguelikeStatus& status,
    const std::string& client,
    const std::string& uid,
    const std::string& core);

[[nodiscard]] CombatReadinessEvidence evaluate_combat_readiness(
    const CombatReadinessProfile& profile,
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const RoguelikeStatus& status,
    const std::string& client,
    const std::string& uid,
    const std::string& core,
    bool roster_complete = false);

[[nodiscard]] json::value combat_readiness_to_json(const CombatReadinessEvidence& evidence);
}
