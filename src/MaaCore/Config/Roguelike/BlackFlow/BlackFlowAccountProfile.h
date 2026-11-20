#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <meojson/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>

namespace asst::blackflow
{
struct AccountModuleEvidence
{
    std::string id;
    int level = 0;
    bool unlocked = false;
    bool verified = false;
};

struct AccountOperatorProfile
{
    std::string id;
    int elite = 0;
    int level = 0;
    bool strength_verified = false;
    std::unordered_map<std::string, AccountModuleEvidence> modules;
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

    [[nodiscard]] const std::string& uid() const noexcept { return m_uid; }

    [[nodiscard]] const std::string& client() const noexcept { return m_client; }

    [[nodiscard]] std::size_t operator_count() const noexcept { return m_operators.size(); }

    [[nodiscard]] bool roster_complete() const noexcept { return m_roster_complete; }

private:
    std::string m_uid;
    std::string m_client;
    bool m_roster_complete = false;
    std::unordered_map<std::string, AccountOperatorProfile> m_operators;
};
}
