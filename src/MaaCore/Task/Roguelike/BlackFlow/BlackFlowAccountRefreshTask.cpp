#include "BlackFlowAccountRefreshTask.h"
#include "BlackFlowRequiredModuleRepair.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <string_view>
#include <utility>

#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "MaaUtils/ImageIo.h"
#include "Task/AbstractTaskPlugin.h"
#include "Task/Miscellaneous/AccountSnapshotTask.h"
#include "Task/Miscellaneous/OperBoxRecognitionTask.h"
#include "Task/ProcessTask.h"
#include "Task/Roguelike/RoguelikeRecruitPolicy.h"
#include "Utils/Logger.hpp"
#include "Utils/WorkingDir.hpp"
#include "Vision/Roguelike/BlackFlow/BlackFlowAccountListDisplayAnalyzer.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowAccountListOrderAnalyzer.h"
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
    m_priority_remaining = m_opening_training_names;
    m_attempted.clear();
    m_verified_page.clear();
    m_observation = {};
    m_observation.client = std::move(client);
    m_observation.uid = std::move(uid);
    for (const auto& target : m_targets) {
        m_observation.unknown_targets.insert(target.operator_name);
        m_priority_remaining.insert(target.operator_name);
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
        if (need_exit()) {
            return false;
        }
        if (std::chrono::steady_clock::now() >= m_scan_deadline) {
            m_scan_stop_reason = "deadline_at_verified_list_checkpoint";
            break;
        }
        if (m_attempted.contains(oper.name)) {
            continue;
        }
        const auto module = std::ranges::find_if(m_targets, [&](const auto& target) {
            return oper.id == target.operator_id && oper.name == target.operator_name;
        });
        // Exact required-module cards must reach detail identity verification
        // even when the list strength crop is unreadable or below the gate.
        if (!account_scan_should_open_detail(oper, module != m_targets.end())) {
            if (module == m_targets.end() && oper.own && oper.level > 0) {
                m_priority_remaining.erase(oper.name);
            }
            LogTrace << __FUNCTION__ << "Skip detail visit below observed recruitment strength" << oper.name << "owned"
                     << oper.own << "elite" << oper.elite << "level" << oper.level;
            continue;
        }
        const bool priority = module != m_targets.end() || m_opening_training_names.contains(oper.name);
        if (m_priority_only && !priority) {
            continue;
        }
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
        if (m_attempted.contains(oper.name)) {
            m_priority_remaining.erase(oper.name);
        }
    }
    return true;
}

bool BlackFlowAccountRefreshTask::ensure_descending_order()
{
    for (int attempt = 0; attempt < 2 && !need_exit(); ++attempt) {
        const auto order = BlackFlowAccountListOrderAnalyzer(ctrler()->get_image()).analyze();
        if (order == AccountListOrder::Descending) {
            return true;
        }
        if (order != AccountListOrder::Ascending || attempt != 0 || !action("BlackFlowAccountScan@SortLevel")) {
            break;
        }
    }
    LogWarn << __FUNCTION__ << "Descending level order could not be positively verified";
    return false;
}

bool BlackFlowAccountRefreshTask::ensure_batch_edit_disabled()
{
    for (int attempt = 0; attempt < 2 && !need_exit(); ++attempt) {
        const auto observed = BlackFlowAccountListDisplayAnalyzer(ctrler()->get_image()).analyze();
        if (!observed) {
            break;
        }
        if (observed->batch_edit == AccountListBatchEdit::Disabled) {
            return true;
        }
        auto toggle = Task.get("BlackFlowAccountScan@CloseBatchEdit");
        if (!toggle || attempt != 0) {
            break;
        }
        const auto previous = toggle->specific_rect;
        const auto& glyph = observed->toggle_rect;
        toggle->specific_rect = { glyph.x + glyph.width / 2, glyph.y + glyph.height / 2, 1, 1 };
        const bool clicked = action("BlackFlowAccountScan@CloseBatchEdit");
        toggle->specific_rect = previous;
        if (!clicked) {
            break;
        }
    }
    LogWarn << __FUNCTION__ << "Disabled operator-list batch display could not be positively verified";
    return false;
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
        // A strict same-frame detail supersedes an unreadable list crop and
        // seeds the roster row required by the cache's typed merge validation.
        auto verified_oper = oper;
        verified_oper.elite = strength->observation.elite;
        verified_oper.level = strength->observation.level;
        m_observation.strengths.insert_or_assign(oper_name, std::move(verified_oper));
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
    for (const auto& restored_oper : restored.get_result()) {
        after.insert(restored_oper.id);
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
    const auto scan_started = std::chrono::steady_clock::now();
    m_scan_deadline = scan_started + std::chrono::minutes(5);
    m_scan_stop_reason.clear();
    const auto scan_started_us =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    if (m_observation.uid.empty() || m_targets.empty() || ctrler()->get_client_type() != m_observation.client ||
        !action("BlackFlowAccountScan@Enter") || !ensure_batch_edit_disabled() || !ensure_descending_order()) {
        return false;
    }
    const auto configure_training_scan = [this](OperBoxRecognitionTask& task, std::size_t analyses) {
        task.set_retry_times(0);
        task.set_page_guard([this] { return action("OperProgress@ReturnToOperBox"); });
        OperBoxRecognitionTask::TraversalProfile traversal;
        traversal.forward_task = "OperBoxSlowlySwipeToTheRight";
        traversal.backtrack_task = "OperBoxSlowlySwipeToTheLeft";
        traversal.maximum_page_analyses = analyses;
        traversal.maximum_backtracks = 0;
        traversal.emit_roster_callbacks = false;
        task.set_traversal_profile(std::move(traversal));
    };
    m_priority_only = true;
    OperBoxRecognitionTask scanner(m_callback, m_inst, m_task_chain);
    configure_training_scan(scanner, 64);
    std::set<std::string> primary_seen_required_ids;
    std::set<std::string> scanned_ids;
    int pages_without_progress = 0;
    scanner.set_page_visitor(
        [this, &primary_seen_required_ids, &scanned_ids, &pages_without_progress](const auto& page) {
            const auto previous_count = scanned_ids.size();
            for (const auto& oper : page) {
                if (!oper.id.empty()) {
                    scanned_ids.insert(oper.id);
                }
                for (const auto& target : m_targets) {
                    if (oper.id == target.operator_id && oper.name == target.operator_name) {
                        primary_seen_required_ids.insert(oper.id);
                    }
                }
            }
            pages_without_progress = scanned_ids.size() == previous_count ? pages_without_progress + 1 : 0;
            if (pages_without_progress >= 3) {
                m_scan_stop_reason = "no_new_canonical_ids";
            }
            if (!m_scan_stop_reason.empty()) {
                return true;
            }
            return visit_page(page);
        });
    scanner.set_stop_after_page([this] { return !m_scan_stop_reason.empty() || m_priority_remaining.empty(); });
    // List observation remains broad, but expensive detail visits first serve
    // the configured opening and required modules rather than early sort slots.
    const bool scanned = scanner.run();
    m_priority_only = false;
    for (const auto& [name, oper] : scanner.own_operators()) {
        if (oper.level > 0) {
            m_observation.strengths.emplace(name, oper);
        }
    }
    m_observation.stopped_at_repeated_page = scanner.stopped_at_repeated_page();
    // Repeated pages can also mean a stalled swipe in the middle. Without a
    // positive boundary/count observation, fresh full-roster coverage is unknown.
    m_observation.roster_complete = false;

    std::vector<RequiredRepairTarget> required;
    for (const auto& target : m_targets) {
        required.push_back({ target.operator_id, target.operator_name });
    }
    const auto missing =
        required_module_repair_targets(required, m_observation.unknown_targets, primary_seen_required_ids, m_attempted);
    const auto repair_scanner_budget = required_repair_scanner_budget(missing.size());
    if (scanned && m_scan_stop_reason.empty() && !missing.empty() && repair_scanner_budget > 0 && !need_exit() &&
        std::chrono::steady_clock::now() < m_scan_deadline) {
        // A second pass locates only required targets never admitted by the first
        // list pass. It does not repeat relevant/opening training visits.
        std::set<std::string> encountered;
        OperBoxRecognitionTask locating(m_callback, m_inst, m_task_chain);
        locating.set_retry_times(0);
        locating.set_page_guard([this] { return action("OperProgress@ReturnToOperBox"); });
        locating.set_page_visitor([this, &missing, &encountered](const auto& page) {
            std::vector<OperBoxInfo> selected;
            for (const auto& oper : page) {
                for (const auto& target : missing) {
                    if (oper.id == target.id && oper.name == target.name && encountered.insert(oper.id).second) {
                        selected.push_back(oper);
                    }
                }
            }
            return visit_page(selected);
        });
        locating.set_stop_after_page(
            [&] { return encountered.size() == missing.size() || !m_scan_stop_reason.empty(); });
        const auto evidence_root = UserDir.get() / utils::path("BlackFlowEvidence/account_required_repair");
        std::error_code directory_error;
        std::filesystem::create_directories(evidence_root, directory_error);
        std::filesystem::path directory;
        bool directory_created = false;
        for (int sequence = 0; !directory_error && sequence < 16; ++sequence) {
            directory =
                evidence_root /
                utils::path(m_observation.uid + "_" + std::to_string(scan_started_us) + "_" + std::to_string(sequence));
            directory_created = std::filesystem::create_directory(directory, directory_error);
            if (directory_created) {
                break;
            }
        }
        if (!directory_created && !directory_error) {
            directory_error = std::make_error_code(std::errc::file_exists);
        }
        if (directory_error) {
            LogWarn << __FUNCTION__ << "Required repair evidence directory unavailable" << directory_error.message();
        }
        std::size_t diagnostic = 0;
        OperBoxRecognitionTask::TraversalProfile traversal;
        traversal.forward_task = "BlackFlowAccountScan@RequiredRepairForward";
        traversal.backtrack_task = "BlackFlowAccountScan@RequiredRepairBacktrack";
        traversal.maximum_page_analyses = repair_scanner_budget;
        traversal.maximum_backtracks = 1;
        traversal.emit_roster_callbacks = false;
        traversal.evidence = [&](const cv::Mat& frame,
                                 const auto& frame_opers,
                                 const auto& page_union,
                                 const auto& previous_ids,
                                 const char* reason) {
            if (std::string_view(reason) == "page_observed" ||
                (std::string_view(reason) == "scan_finished" && encountered.size() == missing.size())) {
                return;
            }
            json::array frame_ids, union_ids, eligible_ids, encountered_ids, previous;
            for (const auto& oper : frame_opers) {
                frame_ids.emplace_back(oper.id);
            }
            for (const auto& oper : page_union) {
                union_ids.emplace_back(oper.id);
            }
            for (const auto& target : missing) {
                eligible_ids.emplace_back(target.id);
            }
            for (const auto& id : encountered) {
                encountered_ids.emplace_back(id);
            }
            for (const auto& id : previous_ids) {
                previous.emplace_back(id);
            }
            const auto stem = std::to_string(++diagnostic);
            const auto image_path = directory / utils::path(stem + ".png");
            bool saved = false;
            try {
                saved = !directory_error && !frame.empty() && MAA_NS::imwrite(image_path, frame);
            }
            catch (const std::exception& error) {
                LogWarn << __FUNCTION__ << "Required repair image evidence unavailable" << error.what();
            }
            const json::value evidence =
                json::object { { "reason", reason },
                               { "client", m_observation.client },
                               { "uid", m_observation.uid },
                               { "scan_started_us", scan_started_us },
                               { "source_kind", "required_repair_single_list_frame" },
                               { "frame_ids", std::move(frame_ids) },
                               { "page_union_ids", std::move(union_ids) },
                               { "previous_union_ids", std::move(previous) },
                               { "eligible_ids", std::move(eligible_ids) },
                               { "encountered_ids", std::move(encountered_ids) },
                               { "coverage_complete", false },
                               { "unseen_target_success_inferred", false },
                               { "image_saved", saved },
                               { "image", saved ? utils::path_to_utf8_string(image_path) : "" } };
            if (!directory_error) {
                std::ofstream stream(directory / utils::path(stem + ".json"));
                stream << evidence.to_string();
                if (!stream || !saved) {
                    LogWarn << __FUNCTION__ << "Required repair evidence incomplete" << reason;
                }
            }
        };
        locating.set_traversal_profile(std::move(traversal));
        const bool located = locating.run();
        if (!located) {
            LogWarn << __FUNCTION__ << "Required locating did not restore a safe list; discard refresh";
            if (!need_exit()) {
                action("BlackFlowAccountScan@ReturnHome");
            }
            return false;
        }
        for (const auto& [name, oper] : locating.own_operators()) {
            if (oper.level > 0 && std::ranges::any_of(missing, [&](const auto& target) {
                    return oper.id == target.id && name == target.name;
                })) {
                m_observation.strengths.emplace(name, oper);
            }
        }
        LogInfo << __FUNCTION__ << "Required locating pass finished" << located << "eligible" << missing.size()
                << "encountered" << encountered.size() << "unknown" << m_observation.unknown_targets.size()
                << "total list frame cap" << 128 << "scanner frame cap" << repair_scanner_budget;
    }
    // Ordinary trained operators retain their observation path, but can only
    // spend the time left after the opening/module pass and required repair.
    if (scanned && !need_exit() && std::chrono::steady_clock::now() < m_scan_deadline) {
        const auto primary_reason = std::exchange(m_scan_stop_reason, std::string {});
        OperBoxRecognitionTask training(m_callback, m_inst, m_task_chain);
        configure_training_scan(training, 32);
        training.set_page_visitor([this](const auto& page) { return visit_page(page); });
        training.set_stop_after_page([this] { return !m_scan_stop_reason.empty(); });
        if (!training.run()) {
            LogWarn << __FUNCTION__ << "Training pass did not restore a safe list; discard refresh";
            if (!need_exit()) {
                action("BlackFlowAccountScan@ReturnHome");
            }
            return false;
        }
        for (const auto& [name, oper] : training.own_operators()) {
            if (oper.level > 0) {
                m_observation.strengths.emplace(name, oper);
            }
        }
        if (m_scan_stop_reason.empty()) {
            m_scan_stop_reason = primary_reason;
        }
    }
    // Always recover to a positive home terminal, never AccountHome ->
    // AccountRead recursion.
    if (need_exit() || !action("BlackFlowAccountScan@ReturnHome")) {
        return false;
    }
    AccountSnapshotTask account(m_callback, m_inst, m_task_chain);
    account.set_retry_times(0);
    m_observation.returned_same_uid =
        account.run() && account.uid() == m_observation.uid && ctrler()->get_client_type() == m_observation.client;
    LogInfo << __FUNCTION__ << "Account training partial scan finished" << "reason"
            << (m_scan_stop_reason.empty() ? "bounded_scanner_finished" : m_scan_stop_reason) << "elapsed seconds"
            << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - scan_started).count()
            << "details attempted" << m_attempted.size() << "details verified"
            << m_observation.verified_strengths.size() << "priority targets remaining" << m_priority_remaining.size()
            << "returned same account" << m_observation.returned_same_uid;
    return scanned && m_observation.returned_same_uid;
}
} // namespace asst::blackflow
