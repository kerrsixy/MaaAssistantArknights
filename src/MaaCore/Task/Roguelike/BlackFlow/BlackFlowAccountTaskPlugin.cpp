#include "BlackFlowAccountTaskPlugin.h"

#include <chrono>

#include "BlackFlowAccountRefreshTask.h"
#include "BlackFlowAccountSnapshotStore.h"
#include "BlackFlowResumeRoster.h"
#include "Config/Roguelike/BlackFlow/BlackFlowAccountScanCatalog.h"
#include "MaaUtils/ImageIo.h"

#include "Config/Roguelike/BlackFlow/BlackFlowAccountProfile.h"
#include "Controller/Controller.h"
#include "Task/Miscellaneous/AccountSnapshotTask.h"
#include "Utils/Logger.hpp"
#include "Utils/WorkingDir.hpp"

bool asst::blackflow::BlackFlowAccountTaskPlugin::load_params(const json::value& params)
{
    m_refresh_requested = params.get("scan_account_modules", true);
    m_cache_file = params.get("account_cache_file", std::string {});
    m_verified = false;
    m_refresh_attempted = false;
    m_module_refresh_complete = false;
    set_retry_times(0);
#ifdef ASST_DEBUG
    m_resume_roster_file = params.get("blackflow_resume_roster_file", std::string {});
#endif
    return m_config->get_theme() == RoguelikeTheme::BlackFlow;
}

bool asst::blackflow::BlackFlowAccountTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    return m_config->get_theme() == RoguelikeTheme::BlackFlow && msg == AsstMsg::SubTaskStart &&
           details.get("subtask", std::string()) == "ProcessTask" &&
           details.get("details", "task", "") == "BlackFlow@Roguelike@AccountRead";
}

bool asst::blackflow::BlackFlowAccountTaskPlugin::_run()
{
    if (m_verified) {
        return true;
    }
    AccountSnapshotTask account(m_callback, m_inst, m_task_chain);
    account.set_retry_times(1);
    const std::string client = ctrler()->get_client_type();
    if (client.empty() || !account.run() || account.uid().empty()) {
        LogError << "BlackFlow account identity could not be verified";
        m_task_ptr->set_enable(false);
        return false;
    }
    m_config->bind_account(client, account.uid());
    const auto cache_path = m_cache_file.empty() ? UserDir.get() / "cache" / "BlackFlowAccounts" /
                                                       utils::path(client + "_" + account.uid() + ".json")
                                                 : utils::path(m_cache_file);
    std::string cache_error;
    json::array refresh_unknown_targets;
    std::size_t fresh_strength_count = 0;
    if (m_refresh_requested && !m_refresh_attempted) {
        // Set before any navigation: this startup task must never repeat a partial refresh.
        m_refresh_attempted = true;
        const auto catalog = BlackFlowAccountScanCatalog::load(
            ResDir.get() / "roguelike" / "BlackFlow" / "account_scan_catalog.json",
            &cache_error);
        auto previous = json::open(cache_path);
        std::error_code cache_ec;
        const bool cache_exists = std::filesystem::exists(cache_path, cache_ec);
        if (!catalog || cache_ec ||
            (cache_exists &&
             (!previous || !BlackFlowAccountProfile::parse(*previous, client, account.uid(), &cache_error)))) {
            LogError << __FUNCTION__ << "Account refresh prerequisites failed" << cache_error;
            m_task_ptr->set_enable(false);
            return false;
        }
        if (!previous) {
            const std::string package = client == "Official"   ? "com.hypergryph.arknights"
                                        : client == "Bilibili" ? "com.hypergryph.arknights.bilibili"
                                                               : "";
            previous = json::object { { "schema", 1 },
                                      { "package", package },
                                      { "account", json::object { { "uid", account.uid() } } },
                                      { "operators", json::object {} },
                                      { "roster_scan_complete", false },
                                      { "modules", json::object { { "scanned", false } } } };
        }
        BlackFlowAccountRefreshTask refresh(m_callback, m_inst, m_task_chain);
        refresh.set_retry_times(0);
        std::set<std::string> opening_training_names;
        for (const auto& oper : m_config->get_start_opers()) {
            if (!oper.use_support) {
                opening_training_names.insert(oper.name);
            }
        }
        refresh.configure(client, account.uid(), catalog->targets(), std::move(opening_training_names));
        if (!refresh.run()) {
            LogError << __FUNCTION__ << "Account refresh could not return to the same UID home screen";
            m_task_ptr->set_enable(false);
            return false;
        }
        const auto& observed = refresh.observation();
        const auto stamp = std::to_string(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
        std::vector<std::filesystem::path> evidence_images;
        for (std::size_t index = 0; index < observed.modules.size(); ++index) {
            const auto path = cache_path.parent_path() / "evidence" / utils::path(client + "_" + account.uid()) /
                              utils::path(stamp) / utils::path(std::to_string(index) + ".png");
            if (!MAA_NS::imwrite(path, observed.modules[index].source_image)) {
                LogError << __FUNCTION__ << "Could not persist exact module observation";
                m_task_ptr->set_enable(false);
                return false;
            }
            evidence_images.push_back(path);
        }
        std::map<std::string, std::filesystem::path> strength_images;
        for (const auto& [name, fresh] : observed.verified_strengths) {
            const auto path = cache_path.parent_path() / "evidence" / utils::path(client + "_" + account.uid()) /
                              utils::path(stamp) / utils::path("strength-" + fresh.observation.operator_id + ".png");
            if (!MAA_NS::imwrite(path, fresh.source_image)) {
                LogError << __FUNCTION__ << "Could not persist exact detail strength observation";
                m_task_ptr->set_enable(false);
                return false;
            }
            strength_images.emplace(name, path);
        }
        const auto staged = BlackFlowAccountSnapshotStore::merge(
            *previous,
            observed,
            catalog->targets(),
            evidence_images,
            stamp,
            &cache_error,
            strength_images);
        const auto current = json::open(cache_path);
        if ((cache_exists && (!current || current->to_string() != previous->to_string())) ||
            (!cache_exists && std::filesystem::exists(cache_path, cache_ec)) || cache_ec) {
            LogError << __FUNCTION__ << "Account cache changed during the UI refresh; retained the newer file";
            m_task_ptr->set_enable(false);
            return false;
        }
        if (!staged || !BlackFlowAccountSnapshotStore::save_atomic(cache_path, *staged, stamp, &cache_error)) {
            LogError << __FUNCTION__ << "Account refresh cache transaction failed" << cache_error;
            m_task_ptr->set_enable(false);
            return false;
        }
        m_module_refresh_complete = observed.required_modules_complete();
        fresh_strength_count = observed.verified_strengths.size();
        for (const auto& name : observed.unknown_targets) {
            refresh_unknown_targets.emplace_back(name);
        }
    }
    auto profile = BlackFlowAccountProfile::load(cache_path, client, account.uid(), &cache_error);
    if (m_refresh_attempted && !profile) {
        LogError << __FUNCTION__ << "Committed account refresh could not be reloaded" << cache_error;
        m_task_ptr->set_enable(false);
        return false;
    }
    const bool cache_loaded = profile.has_value();
    size_t cached_operator_count = 0;
    size_t cached_training_count = 0;
    bool cached_roster_complete = false;
    if (profile) {
        cached_operator_count = profile->operator_count();
        cached_training_count = profile->training_operator_count();
        cached_roster_complete = profile->roster_complete();
        if (!m_config->bind_account_profile(std::make_shared<BlackFlowAccountProfile>(std::move(*profile)))) {
            return false;
        }
    }
    else {
        LogWarn << __FUNCTION__ << "Verified account cache is unavailable" << cache_path << cache_error;
    }
#ifdef ASST_DEBUG
    if (!m_resume_roster_file.empty()) {
        const auto snapshot = json::open(utils::path(m_resume_roster_file));
        const auto& bound_profile = m_config->account_profile();
        const auto restored =
            snapshot && bound_profile ? parse_resume_roster(*snapshot, *bound_profile, &cache_error) : std::nullopt;
        if (!restored) {
            LogError << __FUNCTION__ << "Refused assisted roster restoration" << m_resume_roster_file << cache_error;
            m_task_ptr->set_enable(false);
            return false;
        }
        m_config->status().opers = *restored;
        auto restored_info = basic_info_with_what("BlackFlowRosterRestored");
        json::array operators;
        for (const auto& [name, strength] : *restored) {
            operators.emplace_back(
                json::object { { "name", name }, { "elite", strength.elite }, { "level", strength.level } });
        }
        restored_info["details"] = json::object { { "uid", account.uid() },
                                                  { "client", client },
                                                  { "source", m_resume_roster_file },
                                                  { "operators", std::move(operators) },
                                                  { "unattended_acceptance_eligible", false } };
        callback(AsstMsg::SubTaskExtraInfo, restored_info);
    }
#endif
    m_verified = true;
    auto info = basic_info_with_what("BlackFlowAccountBound");
    info["details"] = json::object { { "client", client },
                                     { "uid", account.uid() },
                                     { "nick", account.nick() },
                                     { "refresh_requested", m_refresh_requested },
                                     { "cache_loaded", cache_loaded },
                                     { "cache_file", utils::path_to_utf8_string(cache_path) },
                                     { "cached_operator_count", cached_operator_count },
                                     { "cached_training_operator_count", cached_training_count },
                                     { "full_skill_training_scan_complete", false },
                                     { "cached_roster_complete", cached_roster_complete },
                                     { "fresh_scan_complete", false },
                                     { "fresh_roster_complete", false },
                                     { "detail_strength_refresh_count", fresh_strength_count },
                                     { "module_scan_complete", m_module_refresh_complete },
                                     { "module_refresh_complete", m_module_refresh_complete },
                                     { "refresh_unknown_targets", std::move(refresh_unknown_targets) },
                                     { "refresh_attempted", m_refresh_attempted } };
    callback(AsstMsg::SubTaskExtraInfo, info);
    return true;
}
