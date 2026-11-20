#include "BlackFlowAccountTaskPlugin.h"

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
    auto profile = BlackFlowAccountProfile::load(cache_path, client, account.uid(), &cache_error);
    const bool cache_loaded = profile.has_value();
    size_t cached_operator_count = 0;
    bool cached_roster_complete = false;
    if (profile) {
        cached_operator_count = profile->operator_count();
        cached_roster_complete = profile->roster_complete();
        if (!m_config->bind_account_profile(std::make_shared<BlackFlowAccountProfile>(std::move(*profile)))) {
            return false;
        }
    }
    else {
        LogWarn << __FUNCTION__ << "Verified account cache is unavailable" << cache_path << cache_error;
    }
    m_verified = true;
    auto info = basic_info_with_what("BlackFlowAccountBound");
    info["details"] = json::object { { "client", client },
                                     { "uid", account.uid() },
                                     { "nick", account.nick() },
                                     { "refresh_requested", m_refresh_requested },
                                     { "cache_loaded", cache_loaded },
                                     { "cache_file", utils::path_to_utf8_string(cache_path) },
                                     { "cached_operator_count", cached_operator_count },
                                     { "cached_roster_complete", cached_roster_complete },
                                     { "fresh_scan_complete", false },
                                     { "module_scan_complete", false } };
    callback(AsstMsg::SubTaskExtraInfo, info);
    return true;
}
