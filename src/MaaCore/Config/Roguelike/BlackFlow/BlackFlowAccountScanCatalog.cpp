#include "BlackFlowAccountScanCatalog.h"

#include <algorithm>
#include <set>

#include "Utils/Logger.hpp"

namespace asst::blackflow::account_scan_catalog
{
bool text(const json::value& value)
{
    if (!value.is_string() || value.as_string().empty()) {
        return false;
    }
    const auto& text = value.as_string();
    return text.find_first_of("\r\n\t") == std::string::npos && text.front() != ' ' && text.back() != ' ';
}

bool level(const json::value& value)
{
    return value.is<int>() && value.as_integer() >= 1 && value.as_integer() <= 3;
}
}

std::optional<asst::blackflow::BlackFlowAccountScanCatalog>
    asst::blackflow::BlackFlowAccountScanCatalog::parse(const json::value& data, std::string* error)
{
    const auto fail = [&](const std::string& message) -> std::optional<BlackFlowAccountScanCatalog> {
        if (error) {
            *error = message;
        }
        LogWarn << __FUNCTION__ << message;
        return std::nullopt;
    };
    if (error) {
        error->clear();
    }
    if (!data.is_object() || !data.contains("schema_version") || !data.at("schema_version").is<int>() ||
        data.at("schema_version").as_integer() != 1 || !data.contains("theme") || !data.at("theme").is_string() ||
        data.at("theme").as_string() != "BlackFlow" || !data.contains("snapshot_label") ||
        !account_scan_catalog::text(data.at("snapshot_label"))) {
        return fail("Invalid account scan catalog identity or schema");
    }
    if (!data.contains("label_is_not_game_version_proof") || !data.at("label_is_not_game_version_proof").is_boolean() ||
        !data.at("label_is_not_game_version_proof").as_boolean()) {
        return fail("Account scan catalog must distinguish snapshot labels from verified game versions");
    }
    for (const auto* field : { "account_scan_performed", "current_battle_equipment_proven" }) {
        if (!data.contains(field) || !data.at(field).is_boolean() || data.at(field).as_boolean()) {
            return fail("Account scan catalog must not assert runtime observations");
        }
    }
    const auto targets = data.find<json::array>("targets");
    const auto sources = data.find<json::object>("source_files");
    if (!targets || !sources) {
        return fail("Account scan catalog is missing targets or source provenance");
    }
    for (const auto* source : { "recruitment", "characters", "equipment", "battle_equipment" }) {
        const auto item = data.at("source_files").find(source);
        if (!item || !item->is_object() || !item->contains("name") || !account_scan_catalog::text(item->at("name")) ||
            !item->contains("sha256") || !item->at("sha256").is_string()) {
            return fail("Account scan catalog source record is incomplete");
        }
        const auto& digest = item->at("sha256").as_string();
        if (digest.size() != 64 ||
            !std::ranges::all_of(digest, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) {
            return fail("Account scan catalog source digest is malformed");
        }
    }
    BlackFlowAccountScanCatalog catalog;
    catalog.m_snapshot_label = data.at("snapshot_label").as_string();
    std::set<std::string> names, identifiers, modules, titles;
    for (const auto& target : *targets) {
        if (!target.is_object()) {
            return fail("Account module scan target is not an object");
        }
        for (const auto* field : { "operator_id", "operator_name", "module_id", "display_title" }) {
            if (!target.contains(field) || !account_scan_catalog::text(target.at(field))) {
                return fail("Account module scan target identity is incomplete");
            }
        }
        if (!target.contains("max_level") || !account_scan_catalog::level(target.at("max_level")) ||
            !target.contains("minimum_level") || !account_scan_catalog::level(target.at("minimum_level")) ||
            target.at("minimum_level").as_integer() > target.at("max_level").as_integer()) {
            return fail("Account module scan target levels are unsupported");
        }
        AccountModuleScanTarget parsed {
            .operator_id = target.at("operator_id").as_string(),
            .operator_name = target.at("operator_name").as_string(),
            .panel = { .module_id = target.at("module_id").as_string(),
                       .display_title = target.at("display_title").as_string(),
                       .max_level = target.at("max_level").as_integer() },
            .minimum_level = target.at("minimum_level").as_integer(),
        };
        if (!names.insert(parsed.operator_name).second || !identifiers.insert(parsed.operator_id).second ||
            !modules.insert(parsed.panel.module_id).second || !titles.insert(parsed.panel.display_title).second) {
            return fail("Account module scan target identity is duplicated or ambiguous");
        }
        catalog.m_targets.push_back(std::move(parsed));
    }
    return catalog;
}

std::optional<asst::blackflow::BlackFlowAccountScanCatalog>
    asst::blackflow::BlackFlowAccountScanCatalog::load(const std::filesystem::path& path, std::string* error)
{
    const auto data = json::open(path);
    if (!data) {
        if (error) {
            *error = "Account scan catalog could not be read";
        }
        return std::nullopt;
    }
    return parse(*data, error);
}

std::optional<std::reference_wrapper<const asst::blackflow::AccountModuleScanTarget>>
    asst::blackflow::BlackFlowAccountScanCatalog::find_target(
        const std::string& operator_name,
        const std::string& module_id,
        int minimum_level) const
{
    const auto target = std::ranges::find_if(m_targets, [&](const auto& item) {
        return item.operator_name == operator_name && item.panel.module_id == module_id &&
               item.minimum_level == minimum_level;
    });
    return target == m_targets.end() ? std::nullopt
                                     : std::optional<std::reference_wrapper<const AccountModuleScanTarget>> { *target };
}
