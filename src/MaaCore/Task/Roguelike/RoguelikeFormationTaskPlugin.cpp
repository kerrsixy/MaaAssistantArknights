#include "RoguelikeFormationTaskPlugin.h"
#include "RoguelikeBattleTaskPlugin.h"
#include "RoguelikePreparationTask.h"
#include "Utils/WorkingDir.hpp"

#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "Task/ProcessTask.h"
#include "Utils/Logger.hpp"
#include <ranges>

bool asst::RoguelikeFormationTaskPlugin::load_params([[maybe_unused]] const json::value& params)
{
    set_retry_times(m_config->get_theme() == RoguelikeTheme::BlackFlow ? 2 : RetryTimesDefault);
    return true;
}

bool asst::RoguelikeFormationTaskPlugin::on_run_fails()
{
    if (m_config->get_theme() != RoguelikeTheme::BlackFlow || m_cur_retry < m_retry_times) {
        return true;
    }

    // A failed plugin otherwise lets ProcessTask confirm an unchanged formation and start the battle.
    Log.error(__FUNCTION__, "BlackFlow quick formation failed; stop before battle");
    save_img(utils::path("debug/roguelike"), false);
    auto info = basic_info_with_what("BlackFlowFormationFailed");
    info["details"] = json::object { { "reason", "Quick formation could not be verified" } };
    callback(AsstMsg::SubTaskExtraInfo, info);
    m_task_ptr->set_enable(false);
    return false;
}

bool asst::RoguelikeFormationTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    if (msg != AsstMsg::SubTaskCompleted || details.get("subtask", std::string()) != "ProcessTask") {
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
    if (task_view == "Roguelike@QuickFormation" ||
        (m_config->get_theme() == RoguelikeTheme::BlackFlow && task_view == "Roguelike@DuelQuickFormation")) {
        return true;
    }
    else {
        return false;
    }
}

bool asst::RoguelikeFormationTaskPlugin::_run()
{
    LogTraceFunction;

    if (m_config->get_theme() == RoguelikeTheme::BlackFlow && m_task_ptr) {
        if (const auto battle = m_task_ptr->find_plugin<RoguelikeBattleTaskPlugin>()) {
            battle->invalidate_prepared_primary();
        }
    }
    const auto frame = ctrler()->get_image();
    RoguelikeFormationImageAnalyzer formation_analyzer(frame);
    if (!formation_analyzer.analyze()) {
        return false;
    }

    observe_preparation(frame, formation_analyzer.get_result());

    size_t pre_selected = 0;
    size_t select_count = 0;
    for (const auto& oper : formation_analyzer.get_result()) {
        if (RoguelikeRecruit.is_oper_excluded(m_config->get_theme(), oper.name)) {
            if (oper.selected) {
                ctrler()->click(oper.rect);
            }
            continue;
        }
        if (oper.selected) {
            ++pre_selected;
            continue;
        }
        ctrler()->click(oper.rect);
        ++select_count;
    }
    Log.info(__FUNCTION__, "pre_selected: ", pre_selected, " select: ", select_count);

    // 以下情况清空重选（游戏会自动排序）
    // 1. 这一页选满 8 个了
    // 2. select 有值，但是重新识别后发现总的选择数量并没有增加（说明上面的click都没生效）
    bool reselect = false;

    if (pre_selected == MaxNumOfOperPerPage) {
        reselect = true;
    }
    if (!reselect && select_count != 0) {
        sleep(Task.get("RoguelikeQuickFormationDelay")->post_delay);
        formation_analyzer.set_image(ctrler()->get_image());

        if (!formation_analyzer.analyze()) {
            Log.warn("RoguelikeFormationImageAnalyzer re analyze failed");
            return true;
        }

        auto& new_result = formation_analyzer.get_result();
        size_t new_selected_count = std::ranges::count_if(new_result, [](const auto& oper) { return oper.selected; });
        // 说明 select_count 计数没生效，即都没点上
        if (new_selected_count == pre_selected) {
            reselect = true;
        }
    }

    if (reselect) {
        return clear_and_reselect();
    }

    return true;
}

bool asst::RoguelikeFormationTaskPlugin::clear_and_reselect()
{
    LogTraceFunction;

    // 清空并退出游戏会自动按等级重新排序
    if (!ProcessTask(*this, { "RoguelikeQuickFormationClearAndReselect" }).run() &&
        m_config->get_theme() == RoguelikeTheme::BlackFlow) {
        return false;
    }

    if (!scan_oper() && m_config->get_theme() == RoguelikeTheme::BlackFlow) {
        return false;
    }
    // 如果新识别出的干员页数没有之前的多，说明没在最左侧开始识别
    if (max_page > cur_page) {
        cur_page = max_page;
        swipe_to_first_page();
        if (!scan_oper() && m_config->get_theme() == RoguelikeTheme::BlackFlow) {
            return false;
        }
    }
    max_page = cur_page;
    std::erase_if(oper_list, [&](const auto& oper) {
        return RoguelikeRecruit.is_oper_excluded(m_config->get_theme(), oper.name);
    });

    const auto all_operators = oper_list;
    // 将oper_list的每个干员重置为未选择
    for (auto& oper : oper_list) {
        oper.selected = false;
    }

    Log.info(__FUNCTION__, "max_page: ", max_page, " oper_count: ", oper_list.size());

    std::vector<asst::RoguelikeFormationImageAnalyzer::FormationOper> sorted_oper_list;
    std::unordered_set<std::string> oper_to_select; // 和上面的 vector 一致，用于快速查重

    const auto& team_complete_condition = RoguelikeRecruit.get_team_complete_info(m_config->get_theme());
    const auto& group_list = RoguelikeRecruit.get_group_names(m_config->get_theme());

    for (const auto& condition : team_complete_condition) { // 优先选择阵容核心干员
        int count = 0;
        const int require = condition.threshold;
        for (const std::string& group_name : condition.groups) {
            auto group_filter = std::views::filter([&](const auto& oper) {
                const auto& group_ids = RoguelikeRecruit.get_group_ids_of_oper(m_config->get_theme(), oper.name);
                return std::ranges::any_of(group_ids, [&](int id) { return group_list[id] == group_name; });
            });
            for (const auto& oper : oper_list | group_filter | std::views::take(require - count)) {
                if (!oper_to_select.contains(oper.name)) {
                    sorted_oper_list.emplace_back(oper);
                    oper_to_select.emplace(oper.name);
                }
                count++;
            }
            if (count == require) {
                break;
            }
        }
    }

    std::erase_if(oper_list, [&](const auto& oper) { return oper_to_select.contains(oper.name); });

    // 剩下的位置按招募优先级从高到低补，而不是按游戏列表顺序补
    // （游戏列表按等级/稀有度排，会让低伤害干员抢到位置）
    const auto is_reserve_oper = [](const std::string& name) {
        return name.starts_with("预备干员") || name.starts_with("罗德岛临时雇员");
    };
    const auto priority_of = [&](const std::string& name) {
        const auto& info = RoguelikeRecruit.get_oper_info(m_config->get_theme(), name);
        return info.recruit_priority_when_team_full > 0 ? info.recruit_priority_when_team_full : info.recruit_priority;
    };
    std::ranges::stable_sort(oper_list, [&](const auto& lhs, const auto& rhs) {
        const bool lhs_reserve = is_reserve_oper(lhs.name);
        const bool rhs_reserve = is_reserve_oper(rhs.name);
        if (lhs_reserve != rhs_reserve) {
            return !lhs_reserve; // 预备干员永远排最后
        }
        return priority_of(lhs.name) > priority_of(rhs.name);
    });

    std::ranges::move(oper_list | std::views::take(13 - sorted_oper_list.size()), std::back_inserter(sorted_oper_list));

    std::vector<std::string> legacy_names;
    for (const auto& card : sorted_oper_list) {
        legacy_names.push_back(card.name);
    }
    std::optional<blackflow::FormationRoleProposal> role;
    std::vector<std::string> all;
    for (const auto& card : all_operators) {
        all.push_back(card.name);
    }
    role = plan_primary_members(all, legacy_names);
    if (role) {
        std::vector<RoguelikeFormationImageAnalyzer::FormationOper> proposed;
        for (const auto& name : role->requested_members) {
            const auto found = std::ranges::find_if(all_operators, [&](const auto& x) { return x.name == name; });
            if (found == all_operators.end()) {
                return false;
            }
            proposed.push_back(*found);
        }
        sorted_oper_list = std::move(proposed);
    }
    m_selection_input_issued = false;
    for (const auto& oper : sorted_oper_list) {
        if (!select(oper)) {
            return false;
        }
    }
    if (m_config->get_theme() == RoguelikeTheme::BlackFlow) {
        blackflow::FormationRoleProposal requested;
        for (const auto& oper : sorted_oper_list) {
            requested.requested_members.push_back(oper.name);
        }
        if (role) {
            requested = *role;
        }
        // The current page can have moved after selecting an earlier member.
        // Re-read names/selected flags, not all training, once for UI closure.
        std::vector<std::string> actual;
        if (m_selection_input_issued) {
            swipe_to_first_page();
            if (!scan_oper(false)) {
                return false;
            }
            for (const auto& oper : oper_list) {
                if (oper.selected) {
                    actual.push_back(oper.name);
                }
            }
        }
        else {
            // Full page collection is already complete and no input changed it.
            for (const auto& oper : all_operators) {
                if (oper.selected) {
                    actual.push_back(oper.name);
                }
            }
        }
        const bool verified = role ? confirm_primary_members(requested, actual)
                                   : blackflow::verify_formation_selected_set(requested, actual);
        json::array planned;
        if (role) {
            for (const auto& entry : requested.full_observations) {
                planned.emplace_back(
                    json::object { { "name", entry.name },
                                   { "elite", entry.full_observed.elite },
                                   { "level", entry.full_observed.level },
                                   { "planned_skill", entry.planned_skill },
                                   { "training_sequence", entry.full_observed.preparation.training_sequence },
                                   { "observed_selected_skill", entry.full_observed.preparation.selected_skill },
                                   { "component", blackflow::conditional_primary_to_json(entry.component) } });
            }
        }
        auto receipt = basic_info_with_what("BlackFlowFormationPrimaryDecision");
        receipt["details"] =
            json::object { { "reason", role ? requested.reason : "stage_context_unknown_preserve_legacy_members" },
                           { "requested_members", requested.requested_members },
                           { "observed_selected_members", actual },
                           { "selected_set_verified", verified },
                           { "primary_candidates", requested.primary_candidates },
                           { "Full_planned_components", planned },
                           { "stage_context",
                             role ? blackflow::preparation_stage_to_json(requested.stage) : json::value(nullptr) },
                           { "skill_selection_still_requires_actual_Skills_page", true },
                           { "member_swap_applied", requested.requested_members != legacy_names },
                           { "selection_input_issued", m_selection_input_issued },
                           { "duplicate_Full_scan", false } };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        if (!verified) {
            return false;
        }
    }
    return true;
}

std::optional<asst::blackflow::FormationRoleProposal> asst::RoguelikeFormationTaskPlugin::plan_primary_members(
    const std::vector<std::string>& all,
    const std::vector<std::string>& legacy) const
{
    if (m_config->get_theme() != RoguelikeTheme::BlackFlow || !m_task_ptr) {
        return std::nullopt;
    }
    const auto battle = m_task_ptr->find_plugin<RoguelikeBattleTaskPlugin>();
    if (!battle) {
        return std::nullopt;
    }
    const auto stage = battle->preparation_stage_context();
    if (!stage) {
        return std::nullopt;
    }
    blackflow::BlackFlowRecruitmentDutyCatalog catalog;
    catalog.load(
        ResDir.get() / utils::path("roguelike/BlackFlow/recruitment_duties.json"),
        ResDir.get() / utils::path("roguelike/BlackFlow/operator_training.json"),
        ResDir.get() / utils::path("battle_data.json"));
    return blackflow::choose_formation_primary(
        catalog,
        m_config->status(),
        *stage,
        all,
        legacy,
        m_config->get_core_char());
}

bool asst::RoguelikeFormationTaskPlugin::confirm_primary_members(
    blackflow::FormationRoleProposal& proposal,
    const std::vector<std::string>& actual) const
{
    if (!blackflow::verify_formation_selected_set(proposal, actual) || !m_task_ptr) {
        return false;
    }
    const auto battle = m_task_ptr->find_plugin<RoguelikeBattleTaskPlugin>();
    const auto current = battle ? battle->preparation_stage_context() : std::nullopt;
    if (!current || *current != proposal.stage) {
        return false;
    }
    battle->publish_formation_role(proposal);
    return true;
}

bool asst::RoguelikeFormationTaskPlugin::analyze(bool read_training)
{
    LogTraceFunction;

    const auto frame = ctrler()->get_image();
    RoguelikeFormationImageAnalyzer formation_analyzer(frame);
    if (!formation_analyzer.analyze()) {
        m_scan_failed = true;
        return false;
    }

    // 比较在新一页识别到的干员名 （detected_oper_names） 与 上一页识别到的干员名 (m_last_detected_oper_names)
    // 若完全相同则认为已到达尾页
    auto formation_analyze_result = formation_analyzer.get_result();
    auto oper_name_view = formation_analyze_result | std::views::transform([&](auto oper) { return oper.name; });
    std::vector<std::string> detected_oper_names(oper_name_view.begin(), oper_name_view.end());
    const bool reach_last_column = (detected_oper_names == m_last_detected_oper_names);
    m_last_detected_oper_names = std::move(detected_oper_names);
    if (!reach_last_column && read_training) {
        observe_preparation(frame, formation_analyze_result);
    }

    auto unique_filter = std::views::filter([&](const auto& oper) {
        // TODO: 这里没考虑多个相同预备干员的情况，不过影响应该不大
        return !std::ranges::any_of(oper_list, [&](const auto& existing_oper) {
            return oper.name == existing_oper.name;
        });
    });
    auto append_page_proj = std::views::transform([&](auto oper) {
        Log.info(__FUNCTION__, "oper: ", oper.name, " page: ", cur_page);
        oper.page = cur_page;
        return oper;
    });
    auto result_oper_list = formation_analyze_result | unique_filter | append_page_proj;
    std::ranges::move(result_oper_list, std::back_inserter(oper_list));
    return !reach_last_column;
}

bool asst::RoguelikeFormationTaskPlugin::select(RoguelikeFormationImageAnalyzer::FormationOper oper)
{
    LogTraceFunction;

    if (cur_page != oper.page) {
        Log.info(__FUNCTION__, "swipe from page", cur_page, "to page", oper.page);
        // 在最大页码时（当总页数>=2），从右往左划可能会有对不齐的问题，直接划动到底
        if ((cur_page > oper.page && max_page >= 2 && cur_page == max_page) || max_page == 1) {
            swipe_to_first_page();
        }
        // 中间页面划动姑且当成是相对准确的
        while (cur_page > oper.page) {
            ProcessTask(*this, { "RoguelikeRecruitOperListSlowlySwipeToTheLeft" }).run();
            cur_page--;
        }
        while (cur_page < oper.page) {
            ProcessTask(*this, { "RoguelikeRecruitOperListSlowlySwipeToTheRight" }).run();
            cur_page++;
        }
    }
    Log.info(__FUNCTION__, "select oper", oper.name, "on page", cur_page);
    if (m_config->get_theme() == RoguelikeTheme::BlackFlow) {
        RoguelikeFormationImageAnalyzer current(ctrler()->get_image());
        if (!current.analyze()) {
            return false;
        }
        const auto& cards = current.get_result();
        if (std::ranges::count_if(cards, [&](const auto& x) { return x.name == oper.name; }) != 1) {
            return false;
        }
        const auto target = std::ranges::find_if(cards, [&](const auto& x) { return x.name == oper.name; });
        if (!target->selected) {
            ctrler()->click(target->rect);
            m_selection_input_issued = true;
        }
        return true;
    }
    ctrler()->click(oper.rect);
    return true;
}

void asst::RoguelikeFormationTaskPlugin::swipe_to_first_page()
{
    for (; cur_page > 0; --cur_page) { // 多划一次到不存在的第0页
        ProcessTask(*this, { "RoguelikeRecruitOperListSwipeToTheLeft" }).run();
    }
    ProcessTask(*this, { "SleepAfterOperListQuickSwipe" }).run();
    cur_page = 1;
}

bool asst::RoguelikeFormationTaskPlugin::scan_oper(bool read_training)
{
    m_last_detected_oper_names.clear();
    m_scan_failed = false;
    oper_list.clear();
    cur_page = 1;

    while (analyze(read_training)) { // 返回true说明新增了干员，可能还有下一页
        ProcessTask(*this, { "RoguelikeRecruitOperListSlowlySwipeToTheRight" }).run();
        cur_page++;
    }

    cur_page--; // 最后多划了一下，退回最后一页的页码
    return !m_scan_failed;
}

void asst::RoguelikeFormationTaskPlugin::observe_preparation(
    const cv::Mat& frame,
    const std::vector<RoguelikeFormationImageAnalyzer::FormationOper>& operators)
{
    if (m_config->get_theme() != RoguelikeTheme::BlackFlow) {
        return;
    }
    const auto relative = Task.get("BlackFlowPreparationFormationAnchor")->rect_move;
    std::vector<PreparationCardAnchor> cards;
    for (const auto& oper : operators) {
        cards.push_back({ oper.name, oper.rect.move(relative) });
    }
    RoguelikePreparationAnalyzer analyzer(frame);
    const auto observations = analyzer.analyze(cards, PreparationPage::Formation);
    auto receipt = basic_info_with_what("BlackFlowPreparationObserved");
    receipt["details"] =
        commit_preparation_observations(m_config->status(), frame, observations, PreparationPage::Formation);
    callback(AsstMsg::SubTaskExtraInfo, receipt);
}
