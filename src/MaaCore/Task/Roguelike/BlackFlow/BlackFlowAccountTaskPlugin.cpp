#include "BlackFlowAccountTaskPlugin.h"

#include "Controller/Controller.h"
#include "Task/Miscellaneous/AccountSnapshotTask.h"
#include "Utils/Logger.hpp"

bool asst::blackflow::BlackFlowAccountTaskPlugin::load_params(const json::value& params)
{
    m_refresh_requested = params.get("scan_account_modules", true);
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
    m_verified = true;
    auto info = basic_info_with_what("BlackFlowAccountBound");
    info["details"] = json::object { { "client", client },
                                     { "uid", account.uid() },
                                     { "nick", account.nick() },
                                     { "refresh_requested", m_refresh_requested },
                                     { "module_scan_complete", false } };
    callback(AsstMsg::SubTaskExtraInfo, info);
    return true;
}
