#include "RoguelikeSkillSelectionTaskPlugin.h"

#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "Utils/Logger.hpp"
#include "Vision/Roguelike/RoguelikeSkillSelectionImageAnalyzer.h"

bool asst::RoguelikeSkillSelectionTaskPlugin::verify(AsstMsg msg, const json::value& details) const
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
    if (task_view == "Roguelike@StartAction") {
        return true;
    }
    else {
        return false;
    }
}

bool asst::RoguelikeSkillSelectionTaskPlugin::_run()
{
    LogTraceFunction;

    auto image = ctrler()->get_image();
    RoguelikeSkillSelectionImageAnalyzer analyzer(image);

    if (!analyzer.analyze()) {
        return false;
    }

    int delay = Task.get("RoguelikeSkillSelectionMove1")->post_delay;
    bool has_rookie = false;
    for (const auto& [name, skill_vec] : analyzer.get_result()) {
        const auto& oper_info = RoguelikeRecruit.get_oper_info(m_config->get_theme(), name);
        if (oper_info.name.empty()) {
            Log.warn("Unknown oper", name);
            continue;
        }

        if (oper_info.alternate_skill > 0) {
            Log.info(__FUNCTION__, name, " select alternate skill:", oper_info.alternate_skill);
            ctrler()->click(skill_vec.at(oper_info.alternate_skill - 1));
            sleep(delay);
        }
        if (oper_info.skill > 0) {
            Log.info(__FUNCTION__, name, " select main skill:", oper_info.skill);
            ctrler()->click(skill_vec.at(oper_info.skill - 1));
            sleep(delay);
        }
        constexpr int RookieStd = 200;
        if (oper_info.promote_priority < RookieStd) {
            has_rookie = true;
        }
    }

    if (m_config->status().opers.empty()) {
        // 技能选择页只有干员姓名和技能按钮，没有可靠的精英化/等级信息。
        // 不再用伪造的 E1/LV80 填充状态；后续编队或狭路相逢详情页会读取
        // 当前卡片/详情面板中的真实练度，再写入 status.opers。
        Log.info(__FUNCTION__, "| operator levels unavailable on skill selection page; defer to detail recognition");
    }

    if (analyzer.get_team_full() && !has_rookie) {
        Log.info("Team full and no rookie");
        m_config->status().team_full_without_rookie = true;
    }
    else {
        Log.info("Team not full or has rookie");
        m_config->status().team_full_without_rookie = false;
    }
    return true;
}
