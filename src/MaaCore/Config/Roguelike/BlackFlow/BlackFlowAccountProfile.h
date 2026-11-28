#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <meojson/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>

#include "BlackFlowAccountModifierEvidence.h"
#include "BlackFlowAccountTraining.h"

namespace asst::blackflow
{
struct AccountModuleEvidence
{
    std::string id;
    int level = 0;
    bool unlocked = false;
    bool verified = false;
};

struct AccountModuleDefaultEvidence
{
    std::string operator_id;
    std::string module_id;
    std::string code;
    int level = 0;
    bool verified = false;
};

// This is an independently validated field receipt, not the legacy potential
// number or the displayed panel ATK (which may already include a module).
struct AccountTrustEvidence
{
    std::string client;
    std::string uid;
    std::string operator_id;
    std::string operator_name;
    int percentage = -1;
    double confidence = 0;
    std::string raw_text;
    std::string observed_at;
    std::string evidence_file;
};

[[nodiscard]] json::value account_trust_to_json(const AccountTrustEvidence& value);
[[nodiscard]] std::optional<AccountTrustEvidence> account_trust_from_json(
    const json::value& value,
    const std::string& client,
    const std::string& uid,
    const std::string& operator_id,
    const std::string& operator_name);

struct AccountOperatorProfile
{
    std::string id;
    int elite = 0;
    int level = 0;
    bool strength_verified = false;
    std::unordered_map<std::string, AccountModuleEvidence> modules;
    std::optional<AccountTrainingEvidence> training;
    std::optional<AccountTrustEvidence> trust;
    std::string trust_quality = "missing_optional_evidence";
    std::optional<AccountModuleDefaultEvidence> current_default;
};

// Account strength is separate from operators recruited into the current exploration.
// Only an exact, verified module slot can satisfy a module-dependent recruitment rule.
class BlackFlowAccountProfile
{
public:
    static std::optional<BlackFlowAccountProfile>
        parse(const json::value& snapshot, const std::string& client, const std::string& uid, std::string* error);

    static std::optional<BlackFlowAccountProfile>
        load(const std::filesystem::path& path, const std::string& client, const std::string& uid, std::string* error);

    [[nodiscard]] std::optional<std::reference_wrapper<const AccountOperatorProfile>>
        find_operator(const std::string& name) const;

    [[nodiscard]] std::optional<AccountModuleEvidence>
        module_evidence(const std::string& name, const std::string& module_id) const;

    [[nodiscard]] bool module_available(const std::string& name, const std::string& module_id, int minimum_level) const;
    [[nodiscard]] bool
        module_default_available(const std::string& name, const std::string& module_id, int minimum_level) const;

    [[nodiscard]] std::optional<int>
        skill_level_for_run(const std::string& name, int skill, int current_run_elite, int current_run_level) const;

    [[nodiscard]] std::size_t training_operator_count() const noexcept;
    [[nodiscard]] std::size_t default_operator_count() const noexcept;

    [[nodiscard]] const std::string& uid() const noexcept { return m_uid; }

    [[nodiscard]] const std::string& client() const noexcept { return m_client; }

    [[nodiscard]] std::size_t operator_count() const noexcept { return m_operators.size(); }

    [[nodiscard]] bool roster_complete() const noexcept { return m_roster_complete; }

    [[nodiscard]] const std::optional<LifeModifierReceipt>& life_modifier_evidence() const noexcept
    {
        return m_life_modifiers;
    }

    [[nodiscard]] std::optional<BlackFlowAccountProfile>
        with_life_modifier_evidence(const LifeModifierReceipt& receipt) const;

private:
    std::string m_uid;
    std::string m_client;
    bool m_roster_complete = false;
    std::unordered_map<std::string, AccountOperatorProfile> m_operators;
    std::optional<LifeModifierReceipt> m_life_modifiers;
};
}
