#include "BlackFlowAccountProfile.h"

#include <algorithm>

namespace asst::blackflow
{
std::optional<BlackFlowAccountProfile> BlackFlowAccountProfile::parse(
    const json::value& snapshot,
    const std::string& client,
    const std::string& uid,
    std::string* error)
{
    const auto reject = [error](const std::string& reason) -> std::optional<BlackFlowAccountProfile> {
        if (error != nullptr) {
            *error = reason;
        }
        return std::nullopt;
    };
    if (uid.empty() || !std::ranges::all_of(uid, [](char c) { return c >= '0' && c <= '9'; })) {
        return reject("account UID must contain decimal digits");
    }
    const std::string package = client == "Official"   ? "com.hypergryph.arknights"
                                : client == "Bilibili" ? "com.hypergryph.arknights.bilibili"
                                                       : "";
    if (package.empty() || !snapshot.is_object() || snapshot.get("schema", 0) != 1 ||
        snapshot.get("package", "") != package || snapshot.get("account", "uid", "") != uid) {
        return reject("account snapshot identity or schema does not match the current client and UID");
    }
    const auto operators = snapshot.find<json::object>("operators");
    if (!operators || operators->empty()) {
        return reject("account snapshot contains no operator roster");
    }
    BlackFlowAccountProfile profile;
    profile.m_uid = uid;
    profile.m_client = client;
    profile.m_roster_complete = snapshot.get("roster_scan_complete", false);
    for (const auto& [name, value] : *operators) {
        if (!value.is_object() || !value.get("own", true)) {
            continue;
        }
        AccountOperatorProfile oper;
        oper.id = value.get("id", "");
        oper.elite = value.get("elite", -1);
        oper.level = value.get("level", -1);
        if (name.empty() || oper.id.empty() || oper.elite < 0 || oper.elite > 2 || oper.level < 0 || oper.level > 90) {
            return reject("account operator strength is invalid: " + name);
        }
        oper.strength_verified = oper.level > 0;
        if (!oper.strength_verified) {
            profile.m_roster_complete = false;
        }
        // Legacy aggregate module_level/teixian_verified fields cannot distinguish a normal
        // module from the required limited slot. Preserve the roster but ignore those fields.
        for (const auto& slot : value.get("module_slots", json::array {})) {
            if (!slot.is_object() || !slot.get("verified", false)) {
                continue;
            }
            AccountModuleEvidence module;
            module.id = slot.get("module_id", "");
            module.level = slot.get("level", -1);
            module.unlocked = slot.get("unlocked", false);
            module.verified = true;
            if (module.id.empty() || module.level < 0 || module.level > 3 || module.unlocked != (module.level > 0)) {
                return reject("verified module slot is invalid: " + name);
            }
            if (!oper.modules.emplace(module.id, std::move(module)).second) {
                return reject("account snapshot repeats a verified module slot: " + name);
            }
        }
        if (value.contains("skill_training")) {
            oper.training = account_training_from_json(value.at("skill_training"), client, uid, oper.id, name);
            if (!oper.training || oper.training->elite > oper.elite ||
                (oper.training->elite == oper.elite && oper.training->level > oper.level)) {
                return reject("account skill training identity or strength is invalid: " + name);
            }
        }
        profile.m_operators.emplace(name, std::move(oper));
    }
    if (profile.m_operators.empty()) {
        return reject("account snapshot contains no owned operators");
    }
    return profile;
}

std::optional<BlackFlowAccountProfile> BlackFlowAccountProfile::load(
    const std::filesystem::path& path,
    const std::string& client,
    const std::string& uid,
    std::string* error)
{
    const auto snapshot = json::open(path);
    if (!snapshot) {
        if (error != nullptr) {
            *error = "account snapshot could not be read";
        }
        return std::nullopt;
    }
    return parse(*snapshot, client, uid, error);
}

std::optional<std::reference_wrapper<const AccountOperatorProfile>>
    BlackFlowAccountProfile::find_operator(const std::string& name) const
{
    const auto found = m_operators.find(name);
    if (found == m_operators.end()) {
        return std::nullopt;
    }
    return std::cref(found->second);
}

std::optional<AccountModuleEvidence>
    BlackFlowAccountProfile::module_evidence(const std::string& name, const std::string& module_id) const
{
    const auto oper = find_operator(name);
    if (!oper) {
        return std::nullopt;
    }
    const auto found = oper->get().modules.find(module_id);
    if (found == oper->get().modules.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<int> BlackFlowAccountProfile::skill_level_for_run(
    const std::string& name,
    int skill,
    int current_run_elite,
    int current_run_level) const
{
    const auto oper = find_operator(name);
    if (!oper || !oper->get().training) {
        return std::nullopt;
    }
    return account_skill_level_for_run(*oper->get().training, skill, current_run_elite, current_run_level);
}

std::size_t BlackFlowAccountProfile::training_operator_count() const noexcept
{
    return static_cast<std::size_t>(std::ranges::count_if(m_operators, [](const auto& entry) {
        return entry.second.training && entry.second.training->base_skill_level > 0;
    }));
}

bool BlackFlowAccountProfile::module_available(const std::string& name, const std::string& module_id, int minimum_level)
    const
{
    const auto module = module_evidence(name, module_id);
    return minimum_level >= 1 && minimum_level <= 3 && module.has_value() && module->verified && module->unlocked &&
           module->level >= minimum_level;
}
}
