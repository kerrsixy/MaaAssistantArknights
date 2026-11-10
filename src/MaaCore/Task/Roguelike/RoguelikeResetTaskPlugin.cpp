#include "RoguelikeResetTaskPlugin.h"

#include <atomic>
#include <string>

#include "Utils/Logger.hpp"

bool asst::RoguelikeResetTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    if (msg != AsstMsg::SubTaskStart || details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }

    if (!RoguelikeConfig::is_valid_theme(m_config->get_theme())) {
        Log.error("Roguelike name doesn't exist!");
        return false;
    }
    const std::string roguelike_name = m_config->get_theme() + "@";
    const std::string& task = details.get("details", "task", "");
    std::string_view task_view = task;
    if (task_view.starts_with(roguelike_name)) {
        task_view.remove_prefix(roguelike_name.length());
    }
    if (task_view == "Roguelike@StartExplore") {
        return true;
    }
    else {
        return false;
    }
}

bool asst::RoguelikeResetTaskPlugin::_run()
{
    for (const auto& plugin : m_task_ptr->get_plugins()) {
        if (auto ptr = std::dynamic_pointer_cast<AbstractRoguelikeTaskPlugin>(plugin)) {
            ptr->reset_in_run_variables();
        }
    }
    m_config->clear();
    // 2026-10-29 阶段C：每次 Roguelike@StartExplore 生成新的 run_id。
    // 这里是全局唯一序号（atomic），故意跨实例共享——它只做「区分」，不参与任何决策；
    // 与 pending_hp 那种会污染逻辑的跨实例状态不同。
    static std::atomic<unsigned long long> s_run_seq { 0 };
    m_config->status().run_id = m_config->get_theme() + "-run" + std::to_string(++s_run_seq);
    LogInfo << __FUNCTION__ << "| new run_id:" << m_config->status().run_id;
    return true;
}
