#include "BlackFlowAccountRefreshTask.h"

#include <algorithm>

#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "MaaUtils/ImageIo.h"
#include "Task/AbstractTaskPlugin.h"
#include "Task/Miscellaneous/AccountSnapshotTask.h"
#include "Task/Miscellaneous/OperBoxRecognitionTask.h"
#include "Task/ProcessTask.h"
#include "Utils/Logger.hpp"
#include "Utils/WorkingDir.hpp"
#include "Vision/Roguelike/BlackFlow/BlackFlowAccountTargetLocator.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowAccountTrainingAnalyzer.h"

namespace asst::blackflow
{
class AccountDetailStrengthObserver final : public AbstractTaskPlugin
{
public:
    using AbstractTaskPlugin::AbstractTaskPlugin;

    void configure(std::string id, std::string name)
    {
        m_id = std::move(id);
        m_name = std::move(name);
        m_observation.reset();
        set_retry_times(0);
    }

    bool verify(AsstMsg msg, const json::value& details) const override
    {
        const auto process = dynamic_cast<const ProcessTask*>(m_task_ptr);
        return msg == AsstMsg::SubTaskCompleted && details.get("subtask", "") == "ProcessTask" && process &&
               (process->get_last_task_name() == "BlackFlowAccountScan@Detail" ||
                process->get_last_task_name() == "BlackFlowAccountScan@TrainingDetail");
    }

    const std::optional<FreshAccountDetailStrength>& observation() const noexcept { return m_observation; }

protected:
    bool _run() override
    {
        const auto image = get_hit_image();
        if (image) {
            auto detail = BlackFlowAccountTrainingAnalyzer(*image).analyze_detail(m_id, m_name);
            if (detail) {
                m_observation = FreshAccountDetailStrength { std::move(detail->strength),
                                                             image->clone(),
                                                             std::move(detail->training) };
            }
        }
        return true;
    }

private:
    std::string m_id;
    std::string m_name;
    std::optional<FreshAccountDetailStrength> m_observation;
};

void BlackFlowAccountRefreshTask::configure(
    std::string client,
    std::string uid,
    std::vector<AccountModuleScanTarget> targets,
    std::set<std::string> opening_training_names)
{
    m_targets = std::move(targets);
    m_opening_training_names = std::move(opening_training_names);
    m_attempted.clear();
    m_verified_page.clear();
    m_observation = {};
    m_observation.client = std::move(client);
    m_observation.uid = std::move(uid);
    for (const auto& target : m_targets) {
        m_observation.unknown_targets.insert(target.operator_name);
    }
}

bool BlackFlowAccountRefreshTask::action(const std::string& task, const std::shared_ptr<AbstractTaskPlugin>& observer)
{
    if (need_exit() || !Task.get(task)) {
        return false;
    }
    ProcessTask process(*this, { task });
    if (observer) {
        process.append_plugin(observer);
    }
    // A navigation click can be followed by one delayed emulator frame.
    // Retry the pending recognition with a new frame, not the completed action.
    process.set_retry_times(2);
    process.set_times_limit("OperProgress@ReturnToOperBoxWalk", 4);
    process.set_times_limit("BlackFlowAccountScan@Home@QuickSwitch@ToHome@Open", 3);
    process.set_times_limit("BlackFlowAccountScan@Home@QuickSwitch@ToHome@Entry", 3);
    return process.run();
}

bool BlackFlowAccountRefreshTask::visit_page(const std::vector<OperBoxInfo>& page)
{
    // Scanner pages are multi-frame unions. Only a full verified single frame
    // from this visit may become reusable geometry; no page cache crosses a swipe.
    m_verified_page.clear();
    for (const auto& oper : page) {
        if (m_attempted.contains(oper.name)) {
            continue;
        }
        const auto module = std::ranges::find_if(m_targets, [&](const auto& target) {
            return oper.id == target.operator_id && oper.name == target.operator_name;
        });
        const auto& recruit = RoguelikeRecruit.get_oper_info("BlackFlow", oper.name);
        const bool relevant =
            !RoguelikeRecruit.is_oper_excluded("BlackFlow", oper.name) && !recruit.is_alternate &&
            (recruit.recruit_priority > 0 || recruit.start_priority > 0 || recruit.promote_priority > 0);
        if (module == m_targets.end() && !relevant && !m_opening_training_names.contains(oper.name)) {
            continue;
        }
        if (!visit_target(oper, module == m_targets.end() ? nullptr : &*module)) {
            return false;
        }
    }
    return true;
}

bool BlackFlowAccountRefreshTask::visit_target(const OperBoxInfo& oper, const AccountModuleScanTarget* module)
{
    const auto& oper_id = oper.id;
    const auto& oper_name = oper.name;
    const std::string selection_task = module ? "BlackFlowAccountScan@Select" : "BlackFlowAccountScan@TrainingSelect";
    auto select = Task.get(selection_task);
    auto identity = Task.get<OcrTaskInfo>("BlackFlowAccountScan@DetailName");
    if (!select || !identity) {
        return false;
    }
    const auto previous_rect = select->specific_rect;
    const auto previous_names = identity->text;
    const auto expected_labels = BlackFlowDetailStrengthAnalyzer::expected_labels(oper_id, oper_name);
    if (expected_labels.empty()) {
        return true;
    }
    const auto current_image = ctrler()->get_image();
    const auto located =
        BlackFlowAccountTargetLocator(current_image).locate(oper_id, oper_name, m_verified_page, previous_rect);
    if (!located.list_verified) {
        m_verified_page.clear();
        return false;
    }
    if (!located.target) {
        return true;
    }
    std::set<std::string> before;
    for (const auto& item : located.page) {
        before.insert(item.id);
    }
    const auto directory = UserDir.get() / "BlackFlowEvidence" / "account_refresh" / utils::path(oper_id);
    std::error_code directory_error;
    std::filesystem::create_directories(directory, directory_error);
    const auto persist_frame = [&](const std::string& filename, const cv::Mat& image) {
        if (!directory_error) {
            MAA_NS::imwrite(directory / utils::path(filename), image);
        }
    };
    persist_frame("list-before.png", current_image);
    // The portrait is not a reliable activation surface. Click the exact name
    // recognized in this card's footer, rather than a random portrait offset.
    const auto& label_rect = located.target->name_rect;
    select->specific_rect = { label_rect.x + label_rect.width / 2, label_rect.y + label_rect.height / 2, 1, 1 };
    identity->text = expected_labels;
    m_attempted.insert(oper_name);
    // Select -> detail identity -> exact Manage OCR -> module-page anchor is one
    // resource chain.
    auto strength_observer = std::make_shared<AccountDetailStrengthObserver>(m_callback, m_inst, m_task_chain);
    strength_observer->configure(oper_id, oper_name);
    const bool opened = action(selection_task, strength_observer);
    if (const auto& strength = strength_observer->observation(); strength) {
        m_observation.verified_strengths.emplace(oper_name, *strength);
        persist_frame("detail-strength.png", strength->source_image);
    }
    if (!module && opened) {
        const auto& observed = strength_observer->observation();
        int unknown_mastery = 0;
        int unknown_availability = 0;
        bool base_known = false;
        if (observed && observed->training) {
            base_known = observed->training->base_skill_level > 0;
            for (int slot = 0; slot < 3; ++slot) {
                unknown_availability += observed->training->availability[slot] == SkillAvailability::Unknown ? 1 : 0;
                unknown_mastery += observed->training->availability[slot] == SkillAvailability::Available &&
                                           observed->training->mastery_levels[slot] < 0
                                       ? 1
                                       : 0;
            }
        }
        auto quality = basic_info_with_what("BlackFlowAccountTrainingDataQuality");
        quality["details"] = json::object {
            { "client", m_observation.client },
            { "uid", m_observation.uid },
            { "operator_id", oper_id },
            { "operator_name", oper_name },
            { "detail_page_anchor_verified", true },
            { "exact_detail_strength_verified", observed.has_value() },
            { "same_frame_strength_and_training", observed.has_value() },
            { "base_skill_known", base_known },
            { "available_mastery_unknown_count", observed ? json::value(unknown_mastery) : json::value(nullptr) },
            { "availability_unknown_count", observed ? json::value(unknown_availability) : json::value(nullptr) },
            { "status",
              !observed                                                        ? "identity_or_strength_unknown"
              : !base_known || unknown_mastery > 0 || unknown_availability > 0 ? "training_partial"
                                                                               : "training_observed" },
            { "cached_strength_or_training_substituted", false },
        };
        callback(AsstMsg::SubTaskExtraInfo, quality);
    }
    persist_frame("selection-result.png", ctrler()->get_image());
    select->specific_rect = previous_rect;
    identity->text = previous_names;
    if (module && opened) {
        for (int page_no = 0; page_no < 5 && !need_exit(); ++page_no) {
            auto image = ctrler()->get_image();
            persist_frame("page-" + std::to_string(page_no) + ".png", image);
            const auto result = BlackFlowModulePanelAnalyzer(image).analyze(module->panel);
            if (result) {
                m_observation.modules.push_back({ oper_name, *result, image.clone() });
                m_observation.unknown_targets.erase(oper_name);
                break;
            }
            // Optional, bounded scrolling stays guarded by the module-page anchors.
            if (!Task.get("BlackFlowAccountScan@ModuleNext") || !action("BlackFlowAccountScan@ModuleNext")) {
                LogWarn << __FUNCTION__ << "Required module remains unknown" << oper_name;
                break;
            }
        }
    }
    // An unrecognized detail/manager frame cannot silently resume page scanning.
    if (!action("OperProgress@ReturnToOperBox")) {
        return false;
    }
    OperBoxImageAnalyzer restored(ctrler()->get_image());
    if (!restored.analyze()) {
        return false;
    }
    std::set<std::string> after;
    for (const auto& oper : restored.get_result()) {
        after.insert(oper.id);
    }
    // Preserve the existing strict same-page membership guard. Only its freshly
    // reanalyzed coordinates may be considered by the next target's fresh crop.
    if (before != after) {
        m_verified_page.clear();
        return false;
    }
    m_verified_page = restored.get_result();
    return true;
}

bool BlackFlowAccountRefreshTask::_run()
{
    if (m_observation.uid.empty() || m_targets.empty() || ctrler()->get_client_type() != m_observation.client ||
        !action("BlackFlowAccountScan@Enter")) {
        return false;
    }
    OperBoxRecognitionTask scanner(m_callback, m_inst, m_task_chain);
    scanner.set_retry_times(0);
    scanner.set_page_guard([this] { return action("OperProgress@ReturnToOperBox"); });
    scanner.set_page_visitor([this](const auto& page) { return visit_page(page); });
    // Continue the normal roster pass after the module watchlist: relevant
    // recruitment and configured opening operators also need outside training.
    const bool scanned = scanner.run();
    for (const auto& [name, oper] : scanner.own_operators()) {
        m_observation.strengths.emplace(name, oper);
    }
    m_observation.stopped_at_repeated_page = scanner.stopped_at_repeated_page();
    // Repeated pages can also mean a stalled swipe in the middle. Without a
    // positive boundary/count observation, fresh full-roster coverage is unknown.
    m_observation.roster_complete = false;
    // Always recover to a positive home terminal, never AccountHome ->
    // AccountRead recursion.
    if (need_exit() || !action("BlackFlowAccountScan@ReturnHome")) {
        return false;
    }
    AccountSnapshotTask account(m_callback, m_inst, m_task_chain);
    account.set_retry_times(0);
    m_observation.returned_same_uid =
        account.run() && account.uid() == m_observation.uid && ctrler()->get_client_type() == m_observation.client;
    return scanned && m_observation.returned_same_uid;
}
} // namespace asst::blackflow
