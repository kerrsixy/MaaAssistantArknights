#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <meojson/json.hpp>

namespace asst::blackflow
{
struct ModulePanelTarget
{
    std::string module_id;
    std::string display_title;
    int max_level = 3;
};

struct AccountModuleScanTarget
{
    std::string operator_id;
    std::string operator_name;
    ModulePanelTarget panel;
    int minimum_level = 3;
};

// Original-table identity metadata is not account ownership or equipped-module evidence.
class BlackFlowAccountScanCatalog
{
public:
    static std::optional<BlackFlowAccountScanCatalog> parse(const json::value& data, std::string* error = nullptr);
    static std::optional<BlackFlowAccountScanCatalog>
        load(const std::filesystem::path& path, std::string* error = nullptr);

    [[nodiscard]] const std::vector<AccountModuleScanTarget>& targets() const noexcept { return m_targets; }

    [[nodiscard]] const std::string& snapshot_label() const noexcept { return m_snapshot_label; }

    [[nodiscard]] std::optional<std::reference_wrapper<const AccountModuleScanTarget>>
        find_target(const std::string& operator_name, const std::string& module_id, int minimum_level) const;

private:
    std::string m_snapshot_label;
    std::vector<AccountModuleScanTarget> m_targets;
};
}
