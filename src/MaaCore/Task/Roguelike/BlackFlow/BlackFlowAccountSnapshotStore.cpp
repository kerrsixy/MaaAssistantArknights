#include "BlackFlowAccountSnapshotStore.h"

#include <algorithm>
#include <fstream>
#include <set>

#ifdef _WIN32
#include "MaaUtils/SafeWindows.hpp"
#endif

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/BlackFlow/BlackFlowAccountProfile.h"
#include "Utils/Platform.hpp"

namespace asst::blackflow
{
std::optional<json::value> BlackFlowAccountSnapshotStore::merge(
    const json::value& previous,
    const AccountRefreshObservation& observed,
    const std::vector<AccountModuleScanTarget>& targets,
    const std::vector<std::filesystem::path>& images,
    const std::string& stamp,
    std::string* error,
    const std::map<std::string, std::filesystem::path>& strength_images)
{
    const auto fail = [&](const std::string& message) -> std::optional<json::value> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    const std::string package = observed.client == "Official"   ? "com.hypergryph.arknights"
                                : observed.client == "Bilibili" ? "com.hypergryph.arknights.bilibili"
                                                                : "";
    if (package.empty() || observed.uid.empty() || !observed.returned_same_uid || stamp.empty() ||
        images.size() != observed.modules.size() || previous.get("schema", 0) != 1 ||
        previous.get("package", "") != package || previous.get("account", "uid", "") != observed.uid ||
        !previous.find<json::object>("operators")) {
        return fail("Account refresh identity, return receipt or staging data is invalid");
    }
    auto staged = previous;
    for (const auto& [name, oper] : observed.strengths) {
        if (!oper.own || oper.id.empty() || oper.name != name || oper.elite < 0 || oper.elite > 2 || oper.level < 1 ||
            oper.level > 90) {
            continue;
        }
        if (staged["operators"].contains(name)) {
            auto& retained = staged["operators"][name];
            if (!retained.is_object() || retained.get("id", "") != oper.id) {
                const auto old_id = retained.get("id", "");
                const auto canonical = BattleData.find_oper_by_id(oper.id);
                const auto old_form = BattleData.find_oper_by_id(old_id);
                if (retained.is_object() && oper.id.starts_with("char_") && old_id.starts_with("char_") && canonical &&
                    old_form && canonical->name == name && old_form->name == name &&
                    canonical->role != battle::Role::Drone && old_form->role != battle::Role::Drone) {
                    retained["observed_forms"][oper.id] =
                        json::object { { "id", oper.id },
                                       { "own", true },
                                       { "observed_at", stamp },
                                       { "source", "same_uid_roster_and_canonical_character_data" },
                                       { "strength_verified", false } };
                    continue;
                }
                if (retained.is_object() && (old_id.starts_with("trap_") || old_id.starts_with("token_")) &&
                    oper.id.starts_with("char_") && canonical && canonical->name == name &&
                    canonical->role != battle::Role::Drone) {
                    const auto legacy = retained;
                    retained["id"] = oper.id;
                    retained["elite"] = 0;
                    retained["level"] = 0;
                    retained["module_slots"] = json::array {};
                    retained["legacy_identifier_repair"] =
                        json::object { { "previous", legacy },
                                       { "observed_at", stamp },
                                       { "source", "same_uid_roster_and_canonical_character_data" },
                                       { "strength_verified", false } };
                    continue;
                }
                return fail(
                    "Observed operator ID conflicts with same-account cache: " + name + " " + old_id + " -> " +
                    oper.id);
            }
            // This is a required-module refresh. Grid elite defaults are not a
            // reason to downgrade or rewrite the old independently scanned strength.
            continue;
        }
        staged["operators"][name] = json::object { { "id", oper.id } };
        auto& old = staged["operators"][name];
        old["own"] = true;
        old["elite"] = oper.elite;
        old["level"] = oper.level;
        old["potential"] = oper.potential;
        old["rarity"] = oper.rarity;
        old["operator_observed_at"] = stamp;
    }
    if (strength_images.size() != observed.verified_strengths.size()) {
        return fail("Verified detail strength images are incomplete");
    }
    for (const auto& [name, fresh] : observed.verified_strengths) {
        const auto& value = fresh.observation;
        const auto canonical = BattleData.find_oper_by_id(value.operator_id);
        const auto image = strength_images.find(name);
        if (!canonical || canonical->role == battle::Role::Drone || !value.operator_id.starts_with("char_") ||
            canonical->name != name || value.operator_name != name || value.elite < 0 || value.elite > 2 ||
            value.level < 1 || value.maximum_level < value.level || value.maximum_level > 90 ||
            (value.recognized_name != name &&
             (canonical->name_en.empty() || value.recognized_name != canonical->name_en)) ||
            image == strength_images.end() || image->second.empty() || fresh.source_image.empty() ||
            fresh.source_image.cols != 1280 || fresh.source_image.rows != 720 || !staged["operators"].contains(name) ||
            staged["operators"][name].get("id", "") != value.operator_id) {
            return fail("Verified detail strength identity or range is invalid");
        }
        auto& oper = staged["operators"][name];
        const auto prior_elite = oper.get("elite", 0);
        const auto prior_level = oper.get("level", 0);
        oper["own"] = true;
        oper["elite"] = value.elite;
        oper["level"] = value.level;
        oper["operator_observed_at"] = stamp;
        oper["strength_evidence"] =
            json::object { { "source_kind", "exact_same_uid_operator_detail" },
                           { "recognized_name", value.recognized_name },
                           { "observed_at", stamp },
                           { "maximum_level", value.maximum_level },
                           { "previous_elite", prior_elite },
                           { "previous_level", prior_level },
                           { "evidence",
                             json::array { json::object { { "file", utils::path_to_utf8_string(image->second) } } } } };
    }
    std::set<std::string> known;
    for (std::size_t i = 0; i < observed.modules.size(); ++i) {
        const auto& panel = observed.modules[i];
        const auto& evidence = panel.observation.evidence;
        const auto target = std::ranges::find_if(targets, [&](const auto& entry) {
            return entry.operator_name == panel.operator_name && entry.panel.module_id == evidence.id;
        });
        if (target == targets.end() || !evidence.verified || evidence.level < 0 ||
            evidence.level > target->panel.max_level || evidence.unlocked != (evidence.level > 0) ||
            !known.insert(panel.operator_name).second || images[i].empty() ||
            !staged["operators"].contains(panel.operator_name)) {
            return fail("Observed module evidence is incomplete or ambiguous");
        }
        auto& oper = staged["operators"][panel.operator_name];
        if (oper.get("id", "") != target->operator_id) {
            return fail("Module evidence operator does not match the retained roster");
        }
        auto slots = oper.get("module_slots", json::array {});
        std::optional<std::size_t> position;
        for (std::size_t j = 0; j < slots.size(); ++j) {
            if (slots[j].get("module_id", "") == evidence.id) {
                if (position) {
                    return fail("Prior snapshot repeats the exact module slot");
                }
                position = j;
            }
        }
        auto slot = position ? slots[*position] : json::value(json::object {});
        slot["module_id"] = evidence.id;
        slot["level"] = evidence.level;
        slot["unlocked"] = evidence.unlocked;
        slot["verified"] = true;
        slot["observed_at"] = stamp;
        slot["source_kind"] = "exact_account_module_panel";
        slot["state_text"] = panel.observation.state_text;
        slot["evidence"] = json::array { json::object { { "file", utils::path_to_utf8_string(images[i]) } } };
        if (position) {
            slots[*position] = std::move(slot);
        }
        else {
            slots.push_back(std::move(slot));
        }
        oper["module_slots"] = std::move(slots);
    }
    json::array unknown;
    for (const auto& target : targets) {
        if (!known.contains(target.operator_name)) {
            unknown.emplace_back(target.operator_name);
        }
    }
    const bool module_complete = !targets.empty() && known.size() == targets.size();
    if (!staged.contains("modules")) {
        staged["modules"] = json::object {};
    }
    staged["modules"]["last_required_scan"] =
        json::object { { "observed_at", stamp },
                       { "module_refresh_complete", module_complete },
                       { "detail_strength_refresh_count", observed.verified_strengths.size() },
                       { "fresh_roster_complete", false },
                       { "unknown_targets", std::move(unknown) },
                       { "current_battle_equipment_proven", false } };
    if (module_complete) {
        staged["modules"]["required_modules_scanned"] = true;
    }
    // Never promote or erase the old roster/full-module coverage from a partial
    // pass.
    staged["oper_count"] = staged["operators"].as_object().size();
    if (!BlackFlowAccountProfile::parse(staged, observed.client, observed.uid, error)) {
        return std::nullopt;
    }
    return staged;
}

bool BlackFlowAccountSnapshotStore::save_atomic(
    const std::filesystem::path& path,
    const json::value& snapshot,
    const std::string& stamp,
    std::string* error)
{
    const auto fail = [&](const std::string& message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    if (stamp.empty() || !std::ranges::all_of(stamp, [](char c) { return c >= '0' && c <= '9'; })) {
        return fail("Snapshot transaction suffix must be decimal");
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return fail("Could not create account cache directory");
    }
    auto temporary = path;
    temporary += utils::path(".tmp-" + stamp);
    auto backup = path;
    backup += utils::path(".bak-" + stamp);
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return fail("Could not open staged account snapshot");
        }
        stream << snapshot.to_string();
        stream.flush();
        if (!stream) {
            return fail("Could not flush staged account snapshot");
        }
        stream.close();
        if (!stream) {
            return fail("Could not close staged account snapshot");
        }
    }
    const auto reread = json::open(temporary);
    if (!reread || reread->to_string() != snapshot.to_string()) {
        return fail("Staged snapshot reread verification failed");
    }
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) {
        return fail("Could not inspect previous account snapshot");
    }
    if (exists) {
        std::filesystem::copy_file(path, backup, std::filesystem::copy_options::none, ec);
        if (ec) {
            return fail("Could not preserve previous account snapshot");
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return fail("Atomic account snapshot replacement failed");
    }
#else
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        return fail("Atomic account snapshot replacement failed");
    }
#endif
    return true;
}
} // namespace asst::blackflow
