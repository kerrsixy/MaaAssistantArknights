#pragma once

#include <array>
#include <optional>
#include <string>

#include "Config/Roguelike/RoguelikeTrainingConfig.h"

namespace asst::blackflow
{
struct AccountTrainingEvidence
{
    std::string operator_id;
    std::string operator_name;
    int elite = -1;
    int level = -1;
    int base_skill_level = -1;
    std::array<int, 3> mastery_levels { -1, -1, -1 };
    std::array<SkillAvailability, 3> availability {
        SkillAvailability::Unknown,
        SkillAvailability::Unknown,
        SkillAvailability::Unknown,
    };
};

// A fresh displayed rank still needs the current phase and slot's game rules.
// This function proves neither account ownership nor ordinary recruitment origin.
[[nodiscard]] std::optional<int> normalize_displayed_skill_level_for_run(
    const std::string& name,
    int skill,
    int current_run_elite,
    int current_run_level,
    int displayed_rank);

// Supply verified current-run phase and level. Account training does not promote an operator.
[[nodiscard]] std::optional<int> account_skill_level_for_run(
    const AccountTrainingEvidence& account,
    int skill,
    int current_run_elite,
    int current_run_level);

[[nodiscard]] json::value account_training_to_json(
    const AccountTrainingEvidence& training,
    const std::string& client,
    const std::string& uid,
    const std::string& observed_at,
    const std::string& evidence_file);

[[nodiscard]] std::optional<AccountTrainingEvidence> account_training_from_json(
    const json::value& value,
    const std::string& client,
    const std::string& uid,
    const std::string& operator_id,
    const std::string& operator_name);
}
