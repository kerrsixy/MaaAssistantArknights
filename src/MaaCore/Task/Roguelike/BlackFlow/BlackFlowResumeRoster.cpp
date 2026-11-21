#include "BlackFlowResumeRoster.h"

namespace asst::blackflow
{
std::optional<std::unordered_map<std::string, RoguelikeOper>> parse_resume_roster(
    const json::value& snapshot,
    const BlackFlowAccountProfile& verified_account,
    std::string* error)
{
    const auto reject =
        [error](const std::string& reason) -> std::optional<std::unordered_map<std::string, RoguelikeOper>> {
        if (error) {
            *error = reason;
        }
        return std::nullopt;
    };
    if (!snapshot.is_object() || snapshot.get("schema", 0) != 1 ||
        snapshot.get("client", "") != verified_account.client() || snapshot.get("uid", "") != verified_account.uid() ||
        snapshot.get("acceptance_eligible", true) ||
        !snapshot.get("requires_fresh_same_uid_verification_before_import", false)) {
        return reject("resume roster identity, schema or assisted-continuation declaration is invalid");
    }
    const auto entries = snapshot.find<json::array>("operators");
    if (!entries || entries->empty() || entries->size() > verified_account.operator_count()) {
        return reject("resume roster has no verified operator entries");
    }
    std::unordered_map<std::string, RoguelikeOper> result;
    for (const auto& entry : *entries) {
        const auto name = entry.find<std::string>("name");
        const auto elite = entry.find<int>("elite");
        const auto level = entry.find<int>("level");
        const auto evidence = entry.find<std::string>("evidence");
        if (!name || name->empty() || !elite || *elite < 0 || *elite > 2 || !level || *level <= 0 || !evidence ||
            evidence->empty()) {
            return reject("resume operator has unknown identity, observed strength or evidence");
        }
        const auto owned = verified_account.find_operator(*name);
        if (!owned || !owned->get().strength_verified || *elite > owned->get().elite || *level > owned->get().level) {
            return reject("resume operator exceeds verified account strength: " + *name);
        }
        if (!result.emplace(*name, RoguelikeOper { .elite = *elite, .level = *level }).second) {
            return reject("resume roster repeats an operator: " + *name);
        }
    }
    return result;
}
}
