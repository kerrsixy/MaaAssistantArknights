#include "BlackFlowAccountTraining.h"

#include <algorithm>
#include <cmath>

namespace asst::blackflow::account_training_detail
{
std::optional<int> whole_number(const json::value& value, int minimum, int maximum)
{
    if (!value.is_number()) {
        return std::nullopt;
    }
    const double number = value.as_double();
    if (!std::isfinite(number) || number < minimum || number > maximum || number != std::trunc(number)) {
        return std::nullopt;
    }
    return static_cast<int>(number);
}
}

namespace asst::blackflow
{
json::value account_training_to_json(
    const AccountTrainingEvidence& training,
    const std::string& client,
    const std::string& uid,
    const std::string& observed_at,
    const std::string& evidence_file)
{
    json::array mastery, availability;
    for (int slot = 0; slot < 3; ++slot) {
        mastery.emplace_back(training.mastery_levels[slot]);
        availability.emplace_back(static_cast<int>(training.availability[slot]));
    }
    return json::object { { "schema", 1 },
                          { "client", client },
                          { "uid", uid },
                          { "operator_id", training.operator_id },
                          { "operator_name", training.operator_name },
                          { "elite", training.elite },
                          { "level", training.level },
                          { "base_skill_level", training.base_skill_level },
                          { "mastery_levels", std::move(mastery) },
                          { "availability", std::move(availability) },
                          { "identity_verified", true },
                          { "source_kind", "exact_same_uid_operator_detail" },
                          { "observed_at", observed_at },
                          { "evidence_file", evidence_file } };
}

std::optional<AccountTrainingEvidence> account_training_from_json(
    const json::value& value,
    const std::string& client,
    const std::string& uid,
    const std::string& operator_id,
    const std::string& operator_name)
{
    if (!value.is_object()) {
        return std::nullopt;
    }
    const auto field = [&](const std::string& key, int minimum, int maximum) -> std::optional<int> {
        return value.contains(key) ? account_training_detail::whole_number(value.at(key), minimum, maximum)
                                   : std::nullopt;
    };
    if (field("schema", 1, 1) != 1 || value.get("client", "") != client || value.get("uid", "") != uid ||
        value.get("operator_id", "") != operator_id || value.get("operator_name", "") != operator_name ||
        !value.get("identity_verified", false) || value.get("source_kind", "") != "exact_same_uid_operator_detail" ||
        value.get("observed_at", "").empty() || value.get("evidence_file", "").empty()) {
        return std::nullopt;
    }
    const auto elite = field("elite", 0, 2), level = field("level", 1, 90), base = field("base_skill_level", -1, 7);
    const auto mastery = value.find<json::array>("mastery_levels"), available = value.find<json::array>("availability");
    if (!elite || !level || !base || *elite < 0 || *elite > 2 || *level < 1 || *level > 90 || *base < -1 ||
        *base == 0 || *base > 7 || (*elite == 0 && *base > 4) || !mastery || !available || mastery->size() != 3 ||
        available->size() != 3) {
        return std::nullopt;
    }
    AccountTrainingEvidence result { .operator_id = operator_id,
                                     .operator_name = operator_name,
                                     .elite = *elite,
                                     .level = *level,
                                     .base_skill_level = *base };
    for (int slot = 0; slot < 3; ++slot) {
        const auto rank = account_training_detail::whole_number((*mastery)[slot], -1, 3);
        const auto state =
            account_training_detail::whole_number((*available)[slot], 0, static_cast<int>(SkillAvailability::Absent));
        if (!rank || !state || (*rank > 0 && (*elite < 2 || *base != 7)) ||
            (*state != static_cast<int>(SkillAvailability::Available) && *rank != -1)) {
            return std::nullopt;
        }
        result.mastery_levels[slot] = *rank;
        result.availability[slot] = static_cast<SkillAvailability>(*state);
    }
    return result;
}

std::optional<int> normalize_displayed_skill_level_for_run(
    const std::string& name,
    int skill,
    int current_run_elite,
    int current_run_level,
    int displayed_rank)
{
    const auto level_cap = RoguelikeTraining.maximum_level(name, current_run_elite);
    const auto base_cap = RoguelikeTraining.maximum_base_skill(name);
    const auto rank_cap = RoguelikeTraining.maximum_training(name, skill);
    if (RoguelikeTraining.unique_identity(name).empty() || !level_cap || !base_cap || !rank_cap ||
        current_run_elite < 0 || current_run_elite > 2 || current_run_level <= 0 || current_run_level > *level_cap ||
        displayed_rank < 1 || displayed_rank > *rank_cap ||
        RoguelikeTraining.skill_availability(name, skill, current_run_elite, current_run_level) !=
            SkillAvailability::Available) {
        return std::nullopt;
    }
    // These are the existing account-skill phase rules, now shared by both
    // evidence adapters. The base maximum remains operator-source driven.
    if (current_run_elite < 2) {
        return std::min(displayed_rank, current_run_elite == 0 ? std::min(*base_cap, 4) : *base_cap);
    }
    return displayed_rank;
}

std::optional<int> account_skill_level_for_run(
    const AccountTrainingEvidence& account,
    int skill,
    int current_run_elite,
    int current_run_level)
{
    const auto account_cap = RoguelikeTraining.maximum_level(account.operator_name, account.elite);
    const auto run_cap = RoguelikeTraining.maximum_level(account.operator_name, current_run_elite);
    const auto base_cap = RoguelikeTraining.maximum_base_skill(account.operator_name);
    if (account.operator_id.empty() ||
        RoguelikeTraining.unique_identity(account.operator_name) != account.operator_id || !account_cap || !run_cap ||
        !base_cap || account.level <= 0 || account.level > *account_cap || current_run_elite < 0 ||
        current_run_elite > account.elite || current_run_level <= 0 || current_run_level > *run_cap ||
        (current_run_elite == account.elite && current_run_level > account.level) || skill < 1 || skill > 3 ||
        account.base_skill_level <= 0 || account.base_skill_level > *base_cap ||
        (account.elite == 0 && account.base_skill_level > 4)) {
        return std::nullopt;
    }
    if (account.availability[skill - 1] != SkillAvailability::Available ||
        RoguelikeTraining.skill_availability(account.operator_name, skill, current_run_elite, current_run_level) !=
            SkillAvailability::Available) {
        return std::nullopt;
    }
    const int mastery = account.mastery_levels[skill - 1];
    if (mastery < -1 || mastery > 3 || (mastery > 0 && (account.elite < 2 || account.base_skill_level != 7))) {
        return std::nullopt;
    }
    if (current_run_elite < 2 || account.base_skill_level < 7) {
        return normalize_displayed_skill_level_for_run(
            account.operator_name,
            skill,
            current_run_elite,
            current_run_level,
            account.base_skill_level);
    }
    return mastery >= 0 ? normalize_displayed_skill_level_for_run(
                              account.operator_name,
                              skill,
                              current_run_elite,
                              current_run_level,
                              7 + mastery)
                        : std::nullopt;
}
}
