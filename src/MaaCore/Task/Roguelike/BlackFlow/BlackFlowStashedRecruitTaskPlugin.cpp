#include "BlackFlowStashedRecruitTaskPlugin.h"

#include <algorithm>

#include "BlackFlowStashedRecruitPolicy.h"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/BlackFlow/BlackFlowAccountProfile.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Config/Roguelike/RoguelikeShoppingConfig.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Task/ProcessTask.h"
#include "Task/Roguelike/RoguelikeRecruitPolicy.h"
#include "Utils/Logger.hpp"
#include "Vision/Matcher.h"
#include "Vision/OCRer.h"
#include "Vision/Roguelike/RoguelikeParameterAnalyzer.h"

namespace asst::blackflow
{
constexpr std::string_view StashedRecruitDecide = "BlackFlow@Roguelike@StashedRecruitDecide";
constexpr std::string_view StashedRecruitEnter = "BlackFlow@Roguelike@StashedRecruitEnter";
constexpr std::string_view StashedRecruitLeave = "BlackFlow@Roguelike@StashedRecruitLeave";

bool BlackFlowStashedRecruitTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    if (m_config->get_theme() != RoguelikeTheme::BlackFlow || msg != AsstMsg::SubTaskStart ||
        details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }
    const std::string task = details.get("details", "task", "");
    if (task == StashedRecruitEnter) {
        m_work = Work::Enter;
    }
    else if (task == StashedRecruitDecide) {
        m_work = Work::Decide;
    }
    else if (task == "BlackFlow@Roguelike@StashedRecruitPortraitObserve") {
        m_work = Work::PortraitObserve;
    }
    else if (task == "BlackFlow@Roguelike@StashedRecruitPortraitAdvance") {
        m_work = Work::PortraitAdvance;
    }
    else {
        return false;
    }
    return true;
}

bool BlackFlowStashedRecruitTaskPlugin::_run()
{
    const bool ok = m_work == Work::Enter             ? guard_entry()
                    : m_work == Work::PortraitObserve ? observe_portrait(false)
                    : m_work == Work::PortraitAdvance ? observe_portrait(true)
                                                      : decide();
    if (!ok && m_task_ptr) {
        // Plugin return values are not propagated by AbstractTask::callback.
        // Disable the parent before ProcessTask can execute the consuming action.
        m_task_ptr->set_enable(false);
    }
    return ok;
}

bool BlackFlowStashedRecruitTaskPlugin::observe_portrait(bool advance)
{
    const auto image = get_hit_image();
    const bool ready = image && stashed_recruit_portrait_page(*image);
    if (advance) {
        return ready;
    }
    auto* process = dynamic_cast<ProcessTask*>(m_task_ptr);
    return process && process->override_next(
                          "BlackFlow@Roguelike@StashedRecruitPortraitObserve",
                          { ready ? "BlackFlow@Roguelike@StashedRecruitPortraitAdvance"
                                  : "BlackFlow@Roguelike@StashedRecruitPostConfirm" });
}

bool stashed_recruit_portrait_page(const cv::Mat& image)
{
    if (image.empty() || image.type() != CV_8UC3 || image.cols != 1280 || image.rows != 720) {
        return false;
    }
    Matcher confirm(image);
    confirm.set_task_info("BlackFlow@Roguelike@ChooseOperFlag");
    if (confirm.analyze()) {
        return false;
    }
    Matcher card_currency(image);
    card_currency.set_task_info("RoguelikeRecruitOcrFlag");
    return !card_currency.analyze();
}

bool BlackFlowStashedRecruitTaskPlugin::decide()
{
    auto* process = dynamic_cast<ProcessTask*>(m_task_ptr);
    if (!process) {
        return false;
    }
    // Every observation starts with a preserving exit, never a stale prior selection.
    process->override_next(std::string(StashedRecruitDecide), { std::string(StashedRecruitLeave) });
    const auto image = ctrler()->get_image();
    const auto hope = RoguelikeParameterAnalyzer::read_number(image, "BlackFlow@Roguelike@CurrentHope");
    const auto& profile = m_config->account_profile();
    const auto budgets = Task.get(StashedRecruitDecide)->special_params;
    const bool supported = budgets.size() == 3 && budgets[0] >= 0 && budgets[1] >= 0 &&
                           m_config->get_difficulty() >= 0 && m_config->get_difficulty() <= budgets[2];
    if (!profile || !supported || !hope || *hope < 0) {
        LogInfo << __FUNCTION__ << "Preserve stored vouchers: account, entry budget or live hope is unknown";
        return true;
    }

    const auto& status = m_config->status();
    const auto& theme = m_config->get_theme();
    const auto goods = RoguelikeShopping.get_shop_goods(theme, status.shopping_buy_table);
    std::vector<std::string> ticket_names;
    for (const auto& item : goods) {
        if (item.is_recruit_ticket && item.recruit_role) {
            ticket_names.emplace_back(item.name);
        }
    }
    OCRer tickets(image);
    tickets.set_task_info("BlackFlow@Roguelike@StashedRecruitVoucher");
    tickets.set_required(ticket_names);
    if (ticket_names.empty() || !tickets.analyze()) {
        LogInfo << __FUNCTION__ << "No recognized stored recruitment voucher";
        return true;
    }

    std::optional<StashedRecruitForecast> best;
    Rect selected;
    std::string selected_ticket;
    for (const auto& ticket : tickets.get_result()) {
        const auto policy = std::ranges::find_if(goods, [&](const auto& item) {
            return item.is_recruit_ticket && item.recruit_role && item.name == ticket.text;
        });
        if (policy == goods.end()) {
            continue;
        }
        const auto role = *policy->recruit_role;
        const auto role_count = std::ranges::count_if(status.opers, [&](const auto& member) {
            return BattleData.get_first_role(member.first) == role;
        });
        std::vector<StashedRecruitForecast> forecasts;
        for (const auto& name : BattleData.get_all_oper_names()) {
            if (BattleData.get_first_role(name) != role || RoguelikeRecruit.is_oper_excluded(theme, name)) {
                continue;
            }
            const auto account_oper = profile->find_operator(name);
            if (!account_oper || !account_oper->get().strength_verified ||
                !is_recruitment_strength_sufficient(account_oper->get().elite, account_oper->get().level)) {
                continue;
            }
            const auto& recruit = RoguelikeRecruit.get_oper_info(theme, name);
            const auto owned = status.opers.find(name);
            const bool promotion = owned != status.opers.end();
            const bool useful =
                promotion ? owned->second.elite >= 0 && owned->second.elite < 2 && account_oper->get().elite == 2
                          : role_count < policy->recruit_role_target;
            int priority = promotion ? recruit.promote_priority : recruit.recruit_priority;
            if (recruit.account_module) {
                const auto& module = *recruit.account_module;
                priority = module.adjust_priority(
                    priority,
                    promotion,
                    profile->module_available(name, module.module_id, module.minimum_level));
            }
            forecasts.push_back({ name, priority, budgets[promotion ? 1 : 0], true, useful });
        }
        const auto target = choose_stashed_recruit_target(forecasts, hope);
        if (target && (!best || target->priority > best->priority)) {
            best = target;
            selected = ticket.rect;
            selected_ticket = ticket.text;
        }
    }
    auto report = basic_info_with_what("BlackFlowStashedRecruitDecision");
    report["details"] = json::object { { "hope", *hope },
                                       { "consume", best.has_value() },
                                       { "ticket", selected_ticket },
                                       { "target_forecast", best ? best->name : "" },
                                       { "entry_budget", best ? best->budget : -1 },
                                       { "actual_card_price_verified", false } };
    callback(AsstMsg::SubTaskExtraInfo, report);
    if (!best) {
        return true;
    }
    if (need_exit() || !ctrler()->click(selected)) {
        return false;
    }
    process->override_next(std::string(StashedRecruitDecide), { std::string(StashedRecruitEnter) });
    return true;
}

bool BlackFlowStashedRecruitTaskPlugin::guard_entry()
{
    const auto image = get_hit_image();
    const auto task = Task.get("BlackFlow@Roguelike@StashedRecruitEnabledButton");
    if (!image || !task || !stashed_recruit_entry_enabled(*image, task->roi)) {
        LogWarn << __FUNCTION__ << "Stored voucher Go button is not verified enabled";
        return false;
    }
    return true;
}

bool stashed_recruit_entry_enabled(const cv::Mat& image, const Rect& roi)
{
    if (image.empty() || image.type() != CV_8UC3 || roi.empty() || roi.x < 0 || roi.y < 0 ||
        roi.x + roi.width > image.cols || roi.y + roi.height > image.rows) {
        return false;
    }
    cv::Mat hsv, enabled;
    cv::cvtColor(image(make_rect<cv::Rect>(roi)), hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(80, 40, 130), cv::Scalar(110, 255, 255), enabled);
    // A gray Go label is not an enabled control. Stop instead of consuming or
    // selecting a second ticket when the first selection was not confirmed.
    const double ratio = static_cast<double>(cv::countNonZero(enabled)) / static_cast<double>(enabled.total());
    return ratio >= 0.025;
}
}
