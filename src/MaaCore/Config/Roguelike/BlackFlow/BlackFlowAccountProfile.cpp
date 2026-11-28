#include "BlackFlowAccountProfile.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace asst::blackflow
{
std::optional<BlackFlowAccountProfile>
    BlackFlowAccountProfile::with_life_modifier_evidence(const LifeModifierReceipt& receipt) const
{
    if (receipt.client != m_client || receipt.uid != m_uid || receipt.source.empty() ||
        receipt.source_hash.size() != 64 || receipt.observed_at.empty() || receipt.active_ids.empty()) {
        return std::nullopt;
    }
    std::unordered_set<std::string> states;
    for (const auto& id : receipt.active_ids) {
        if (id.empty() || !states.emplace(id).second) {
            return std::nullopt;
        }
    }
    for (const auto& id : receipt.inactive_ids) {
        if (id.empty() || !states.emplace(id).second) {
            return std::nullopt;
        }
    }
    auto updated = *this;
    updated.m_life_modifiers = receipt;
    return updated;
}

json::value account_trust_to_json(const AccountTrustEvidence& value)
{
    return json::object { { "schema", 1 },
                          { "client", value.client },
                          { "uid", value.uid },
                          { "operator_id", value.operator_id },
                          { "operator_name", value.operator_name },
                          { "percentage", value.percentage },
                          { "confidence", value.confidence },
                          { "raw_text", value.raw_text },
                          { "observed_at", value.observed_at },
                          { "evidence_file", value.evidence_file },
                          { "source_kind", "exact_same_uid_operator_detail" },
                          { "identity_verified", true } };
}

std::optional<AccountTrustEvidence> account_trust_from_json(
    const json::value& value,
    const std::string& client,
    const std::string& uid,
    const std::string& operator_id,
    const std::string& operator_name)
{
    if (!value.is_object()) {
        return std::nullopt;
    }
    // Do not allow coercions/defaults to manufacture a known zero or identity.
    for (const auto& key : { "client",
                             "uid",
                             "operator_id",
                             "operator_name",
                             "raw_text",
                             "observed_at",
                             "evidence_file",
                             "source_kind" }) {
        if (!value.contains(key) || !value.at(key).is_string() || value.at(key).as_string().empty()) {
            return std::nullopt;
        }
    }
    if (!value.contains("schema") || !value.at("schema").is_number() || value.at("schema").as_double() != 1 ||
        !value.contains("identity_verified") || !value.at("identity_verified").is_boolean() ||
        !value.at("identity_verified").as_boolean() || value.at("client").as_string() != client ||
        value.at("uid").as_string() != uid || value.at("operator_id").as_string() != operator_id ||
        value.at("operator_name").as_string() != operator_name ||
        value.at("source_kind").as_string() != "exact_same_uid_operator_detail" || !value.contains("percentage") ||
        !value.at("percentage").is_number() || !value.contains("confidence") || !value.at("confidence").is_number()) {
        return std::nullopt;
    }
    const auto percent = value.at("percentage").as_double();
    const auto confidence = value.at("confidence").as_double();
    if (!std::isfinite(percent) || percent != std::trunc(percent) || percent < 0 || percent > 200 ||
        !std::isfinite(confidence) || confidence < 0.95 || confidence > 1) {
        return std::nullopt;
    }
    const auto& raw = value.at("raw_text").as_string();
    const auto canonical = std::to_string(static_cast<int>(percent));
    if (raw != canonical + "%" && raw != canonical + "％") {
        return std::nullopt;
    }
    return AccountTrustEvidence { .client = client,
                                  .uid = uid,
                                  .operator_id = operator_id,
                                  .operator_name = operator_name,
                                  .percentage = static_cast<int>(percent),
                                  .confidence = confidence,
                                  .raw_text = value.at("raw_text").as_string(),
                                  .observed_at = value.at("observed_at").as_string(),
                                  .evidence_file = value.at("evidence_file").as_string() };
}

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
        if (value.get("default_latest_attempt", "state", "") == "known" && value.contains("current_default_module")) {
            const auto& current = value.at("current_default_module");
            const int level = current.get("level", 0);
            if (current.is_object() && current.get("verified", false) && current.get("client", "") == client &&
                current.get("uid", "") == uid && current.get("operator_id", "") == oper.id &&
                current.get("source_kind", "") == "exact_same_uid_operator_detail_current_module" &&
                !current.get("module_id", "").empty() && !current.get("code", "").empty() &&
                !current.get("evidence_file", "").empty() && !current.get("observed_at", "").empty() &&
                current.get("observed_at", "") == value.get("default_latest_attempt", "observed_at", "") &&
                level >= 1 && level <= 3) {
                oper.current_default = AccountModuleDefaultEvidence { oper.id,
                                                                      current.get("module_id", ""),
                                                                      current.get("code", ""),
                                                                      level,
                                                                      true };
            }
        }
        if (value.contains("attribute_trust")) {
            // An invalid optional attribute is a data-quality unknown. It must
            // not invalidate the otherwise usable UID-bound roster/training.
            oper.trust = account_trust_from_json(value.at("attribute_trust"), client, uid, oper.id, name);
            oper.trust_quality = oper.trust ? "known_cached_observation" : "invalid_optional_evidence";
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

bool BlackFlowAccountProfile::module_default_available(
    const std::string& name,
    const std::string& module_id,
    int minimum_level) const
{
    const auto oper = find_operator(name);
    if (!oper || !oper->get().current_default) {
        return false;
    }
    const auto& current = *oper->get().current_default;
    return minimum_level >= 1 && minimum_level <= 3 && current.verified && current.module_id == module_id &&
           current.operator_id == oper->get().id && current.level >= minimum_level;
}

std::size_t BlackFlowAccountProfile::default_operator_count() const noexcept
{
    return static_cast<std::size_t>(
        std::ranges::count_if(m_operators, [](const auto& entry) { return entry.second.current_default.has_value(); }));
}
}
