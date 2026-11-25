#include "RoguelikeSkillSelectionTaskPlugin.h"
#include "RoguelikeBattleTaskPlugin.h"
#include "RoguelikePreparationTask.h"

#include "Config/GeneralConfig.h"
#include <chrono>
#include <ranges>

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
    if (task_view == "Roguelike@StartAction" ||
        (m_config->get_theme() == RoguelikeTheme::BlackFlow && task_view == "Roguelike@DuelStartAction")) {
        return true;
    }
    else {
        return false;
    }
}

bool asst::RoguelikeSkillSelectionTaskPlugin::_run()
{
    LogTraceFunction;

    if (m_config->get_theme() == RoguelikeTheme::BlackFlow) {
        return run_blackflow_preparation();
    }

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
        // Legacy themes keep their existing reader; BlackFlow refreshes the actual
        // preparation card evidence in run_blackflow_preparation().
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

bool asst::RoguelikeSkillSelectionTaskPlugin::run_blackflow_preparation()
{
    const auto formation_role = take_prepared_formation_role();
    begin_prepared_primary_observation();
    auto image = ctrler()->get_image();
    const auto started = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(Config.get_options().battle_start_timeout_seconds);
    const int delay = Task.get("RoguelikeSkillSelectionMove1")->post_delay;
    while (!RoguelikePreparationAnalyzer::skill_page_visible(image)) {
        if (need_exit()) {
            return false;
        }
        if (std::chrono::steady_clock::now() - started >= timeout) {
            return interrupt_before_battle("skill_page_not_ready");
        }
        if (!sleep(std::max(delay, Task.get("RoguelikeQuickFormationDelay")->post_delay))) {
            return false;
        }
        image = ctrler()->get_image();
    }
    RoguelikeSkillSelectionImageAnalyzer names(image);
    if (!names.analyze()) {
        return interrupt_before_battle("preparation_names_unreadable");
    }
    const auto move = Task.get("RoguelikeSkillSelectionMove1")->rect_move;
    const auto size = Task.get("BlackFlowPreparationSkillAnchor")->rect_move;
    std::vector<PreparationCardAnchor> cards;
    auto skill_rects = names.get_result();
    bool team_full = names.get_team_full();
    const auto update_cards = [&](const auto& results) {
        cards.clear();
        for (const auto& [name, skills] : results) {
            if (!skills.empty()) {
                cards.push_back({ name, skills.front().move({ -move.x, -move.y, size.width, size.height }) });
            }
        }
    };
    update_cards(skill_rects);
    const auto observe = [&](const cv::Mat& frame, PreparationReadMode mode = PreparationReadMode::Full) {
        RoguelikePreparationAnalyzer analyzer(frame);
        auto observations = analyzer.analyze(cards, PreparationPage::Skills, mode);
        auto receipt = basic_info_with_what("BlackFlowPreparationObserved");
        receipt["details"] =
            commit_preparation_observations(m_config->status(), frame, observations, PreparationPage::Skills);
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        return observations;
    };
    auto observations = observe(image);
    const auto has_selection_phase_conflict = [&](const auto& observed) {
        const auto current = m_config->status().opers.find(observed.name);
        return observed.selected_skill > 0 && !observed.operator_id.empty() &&
               current != m_config->status().opers.end() &&
               current->second.preparation.operator_id == observed.operator_id &&
               current->second.preparation.selected_skill != observed.selected_skill;
    };
    std::vector<std::string> conflicting_names;
    for (const auto& observed : observations) {
        if (has_selection_phase_conflict(observed)) {
            conflicting_names.push_back(observed.name);
        }
    }
    if (!conflicting_names.empty()) {
        const auto fresh = ctrler()->get_image();
        RoguelikeSkillSelectionImageAnalyzer fresh_names(fresh);
        bool resolved = RoguelikePreparationAnalyzer::skill_page_visible(fresh) && fresh_names.analyze();
        if (resolved) {
            skill_rects = fresh_names.get_result();
            team_full = fresh_names.get_team_full();
            update_cards(skill_rects);
            observations = observe(fresh);
            resolved = std::ranges::all_of(conflicting_names, [&](const auto& name) {
                const auto found = std::ranges::find(observations, name, &PreparationOperatorObservation::name);
                return found != observations.end() && found->anchor_verified && found->elite >= 0 &&
                       !has_selection_phase_conflict(*found);
            });
        }
        if (!resolved) {
            return interrupt_before_battle("phase_vs_selected_skill_conflict");
        }
    }
    if (std::ranges::none_of(observations, [](const auto& observed) {
            return observed.anchor_verified && !observed.operator_id.empty();
        })) {
        return interrupt_before_battle("preparation_identity_unavailable");
    }

    struct Choice
    {
        std::string name;
        int preferred = 0;
        int alternate = 0;
        int requested = 0;
        bool already_selected = false;
        bool initial_input_attempted = false;
        bool input_accepted = false;
        bool retried = false;
        bool retry_input_attempted = false;
        bool retry_input_accepted = false;
        std::string retry_failure;
    };

    std::vector<Choice> choices;
    bool attempted_input = false;
    bool has_rookie = false;
    for (const auto& observed : observations) {
        if (need_exit()) {
            return false;
        }
        const auto& configured = RoguelikeRecruit.get_oper_info(m_config->get_theme(), observed.name);
        if (configured.name.empty()) {
            continue;
        }
        has_rookie = has_rookie || configured.promote_priority < 200;
        Choice choice;
        choice.name = observed.name;
        choice.preferred = configured.skill;
        choice.alternate = configured.alternate_skill;
        const auto current = m_config->status().opers.find(observed.name);
        choice.requested = choose_preparation_skill(
            configured.skill,
            configured.alternate_skill,
            observed,
            current == m_config->status().opers.end() ? nullptr : &current->second);
        choice.already_selected = choice.requested > 0 && observed.selected_skill == choice.requested;
        if (choice.requested > 0 && !choice.already_selected) {
            attempted_input = true;
            choice.initial_input_attempted = true;
            choice.input_accepted = ctrler()->click(skill_rects.at(choice.name).at(choice.requested - 1));
            if (!sleep(delay)) {
                return false;
            }
        }
        choices.push_back(std::move(choice));
    }
    if (attempted_input) {
        observations = observe(ctrler()->get_image(), PreparationReadMode::SelectionOnly);
    }
    const auto selected = [&](const std::string& name) {
        const auto found = std::ranges::find(observations, name, &PreparationOperatorObservation::name);
        if (found == observations.end()) {
            return -1;
        }
        const auto current = m_config->status().opers.find(name);
        if (!found->operator_id.empty() && current != m_config->status().opers.end() &&
            current->second.preparation.operator_id == found->operator_id) {
            return current->second.preparation.selected_skill;
        }
        return found->selected_skill;
    };
    bool retried = false;
    for (auto& choice : choices) {
        if (choice.requested > 0 && selected(choice.name) != choice.requested) {
            if (need_exit()) {
                return false;
            }
            choice.retried = true;
            const auto retry_frame = ctrler()->get_image();
            if (!RoguelikePreparationAnalyzer::skill_page_visible(retry_frame)) {
                choice.retry_failure = "skill_page_changed";
                continue;
            }
            RoguelikeSkillSelectionImageAnalyzer retry_names(retry_frame);
            if (!retry_names.analyze()) {
                choice.retry_failure = "card_identities_unreadable";
                continue;
            }
            const auto found = retry_names.get_result().find(choice.name);
            if (found == retry_names.get_result().end() || choice.requested > static_cast<int>(found->second.size())) {
                choice.retry_failure = "requested_operator_not_on_page";
                continue;
            }
            update_cards(retry_names.get_result());
            observations = observe(retry_frame, PreparationReadMode::SelectionOnly);
            if (selected(choice.name) == choice.requested) {
                continue;
            }
            retried = true;
            choice.retry_input_attempted = true;
            choice.retry_input_accepted = ctrler()->click(found->second.at(choice.requested - 1));
            if (!sleep(delay)) {
                return false;
            }
        }
    }
    if (retried) {
        observations = observe(ctrler()->get_image(), PreparationReadMode::SelectionOnly);
    }
    bool complete = true;
    json::array decisions;
    for (const auto& choice : choices) {
        const int actual = selected(choice.name);
        const bool no_requirement = choice.preferred <= 0 && choice.alternate <= 0;
        const bool verified = no_requirement || (choice.requested > 0 && actual == choice.requested);
        complete = complete && verified;
        decisions.emplace_back(
            json::object {
                { "name", choice.name },
                { "configured_preferred_skill", choice.preferred },
                { "configured_alternate_skill", choice.alternate },
                { "requested_skill", choice.requested },
                { "observed_selected_skill", actual },
                { "already_selected", choice.already_selected },
                { "initial_input_attempted", choice.initial_input_attempted },
                { "input_accepted", choice.input_accepted },
                { "retried", choice.retried },
                { "retry_input_attempted", choice.retry_input_attempted },
                { "retry_input_accepted", choice.retry_input_accepted },
                { "retry_failure", choice.retry_failure },
                { "selection_failure",
                  verified                ? ""
                  : choice.requested == 0 ? "no_configured_skill_available"
                  : actual < 1            ? "selected_skill_unreadable"
                                          : "observed_different_skill" },
                { "selection_requirement_met", verified },
            });
    }
    auto receipt = basic_info_with_what("BlackFlowPreparationSkillDecision");
    receipt["details"] = json::object { { "operators", std::move(decisions) }, { "complete", complete } };
    callback(AsstMsg::SubTaskExtraInfo, receipt);
    if (!complete) {
        return interrupt_before_battle("selected_skill_not_confirmed");
    }
    std::vector<std::string> selected_names;
    for (const auto& choice : choices) {
        selected_names.push_back(choice.name);
    }
    publish_prepared_primary_observation(selected_names, formation_role);
    m_config->status().team_full_without_rookie = team_full && !has_rookie;
    return true;
}

bool asst::RoguelikeSkillSelectionTaskPlugin::interrupt_before_battle(std::string_view reason)
{
    auto incomplete = basic_info_with_what("BlackFlowPreparationIncomplete");
    incomplete["details"] = json::object { { "reason", std::string(reason) }, { "battle_started", false } };
    callback(AsstMsg::SubTaskExtraInfo, incomplete);
    m_task_ptr->set_enable(false);
    return true;
}

std::optional<asst::blackflow::FormationRoleProposal>
    asst::RoguelikeSkillSelectionTaskPlugin::take_prepared_formation_role()
{
    if (m_task_ptr) {
        if (const auto battle = m_task_ptr->find_plugin<RoguelikeBattleTaskPlugin>()) {
            return battle->take_formation_role();
        }
    }
    return std::nullopt;
}

void asst::RoguelikeSkillSelectionTaskPlugin::begin_prepared_primary_observation()
{
    if (m_task_ptr) {
        if (const auto battle = m_task_ptr->find_plugin<RoguelikeBattleTaskPlugin>()) {
            battle->invalidate_prepared_primary();
        }
    }
    for (auto& [name, oper] : m_config->status().opers) {
        static_cast<void>(name);
        oper.preparation = {};
    }
}

void asst::RoguelikeSkillSelectionTaskPlugin::publish_prepared_primary_observation(
    const std::vector<std::string>& names,
    const std::optional<blackflow::FormationRoleProposal>& proposal)
{
    if (m_task_ptr) {
        if (const auto battle = m_task_ptr->find_plugin<RoguelikeBattleTaskPlugin>()) {
            battle->prepare_primary_from_selection(names, proposal);
        }
    }
}
