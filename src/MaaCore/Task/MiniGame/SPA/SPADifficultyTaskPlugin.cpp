#include "SPADifficultyTaskPlugin.h"

#include "MaaUtils/NoWarningCV.hpp"
#include "SPADifficultyPolicy.h"
#include "SPAObservation.h"
#include <map>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <regex>

#include "Controller/Controller.h"
#include "Task/ProcessTask.h"
#include "Vision/OCRer.h"

bool asst::spa::DifficultyTaskPlugin::set_target(int target, bool start, std::string mode)
{
    if (!valid_difficulty_request(target, mode == "joint" ? 4 : 20)) {
        return false;
    }
    m_target = target;
    m_start = start;
    m_mode = std::move(mode);
    return true;
}

bool asst::spa::DifficultyTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    return msg == AsstMsg::SubTaskStart && details.get("subtask", std::string()) == "ProcessTask" &&
           details.get("details", "task", std::string()) ==
               (m_start ? "MiniGame@SPA@StartIndependent" : "MiniGame@SPA@SelectDifficulty");
}

bool asst::spa::DifficultyTaskPlugin::on_run_fails()
{
    if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
        process->override_next(
            m_start ? "MiniGame@SPA@StartIndependent" : "MiniGame@SPA@SelectDifficulty",
            m_recovery_task.empty() ? std::vector<std::string> {} : std::vector<std::string> { m_recovery_task });
    }
    return false;
}

bool asst::spa::DifficultyTaskPlugin::_run()
{
    if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
        process->set_task_delay(180);
    }
    const int cap = m_mode == "joint" ? 4 : 20;
    const bool joint_mode = m_mode == "joint";
    if (!valid_difficulty_request(m_target, cap)) {
        return false;
    }
    int desired = m_target;
    bool find_max = desired == MaximumDifficulty;
    bool fallback = false;
    std::map<int, bool> seen;
    std::string previous;
    int unchanged = 0;
    DifficultyTargetStability target_stability;
    bool selection_input_issued = false;
    int selection_wait_frames = 0;
    for (int action = 0; action < 28 && !need_exit(); ++action) {
        const auto frame = ctrler()->get_image();
        if (frame.empty() || frame.size() != cv::Size(1280, 720)) {
            return false;
        }
        OCRer ocr(frame);
        const auto recognized = ocr.analyze();
        if (!recognized) {
            if (!sleep(180)) {
                return false;
            }
            continue;
        }
        std::vector<TextObservation> texts;
        bool personal = false, joint = false;
        for (const auto& text : *recognized) {
            texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
            if (text.score >= 0.85 && text.rect.y > 590) {
                personal = personal || text.text == "开始模拟";
                joint = joint || text.text == "开始搜寻";
            }
        }
        const auto page = observe_page(texts, {});
        if (page.page != Page::Difficulty || page.contradictory || !page.selected_difficulty) {
            if (!sleep(180)) {
                return false;
            }
            continue;
        }
        const int current = *page.selected_difficulty;
        const bool training_selected = m_mode == "joint" && current == JointTrainingDifficulty;
        if ((!training_selected && current < 1) || current > cap) {
            return false;
        }
        if (!training_selected) {
            seen[current] = true;
        }
        if (desired == KeepDifficulty) {
            // Training changes the button to "开始训练". A joint search
            // starting from this baseline selects the first matchmaking level.
            desired = training_selected ? 1 : current;
        }
        if (!find_max && current == desired) {
            auto selected = basic_info_with_what("SPADifficultySelected");
            selected["details"] = json::object { { "difficulty", current },
                                                 { "requested", m_target },
                                                 { "fallback_to_max", fallback },
                                                 { "mode", m_mode },
                                                 { "simulation_started", false } };
            callback(AsstMsg::SubTaskExtraInfo, selected);
            if (!m_start) {
                if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr); process && !m_recovery_task.empty()) {
                    // A previous failed scan redirected this step to the dispatcher.
                    // A verified selection can continue to the separate start check.
                    process->override_next("MiniGame@SPA@SelectDifficulty", { "MiniGame@SPA@StartIndependent" });
                }
                return true;
            }
            if ((m_mode == "joint" && (!joint || personal)) || (m_mode != "joint" && (!personal || joint))) {
                return false;
            }
            std::optional<Rect> start;
            for (const auto& text : *recognized) {
                if (text.score >= 0.9 && text.rect.x > 990 && text.rect.y > 600 &&
                    text.text == (m_mode == "joint" ? "开始搜寻" : "开始模拟")) {
                    start = text.rect;
                }
            }
            if (!start || !ctrler()->click(*start)) {
                return false;
            }
            auto issued = basic_info_with_what("SPASimulationStartIssued");
            issued["details"] =
                json::object { { "difficulty", current }, { "mode", m_mode }, { "destination_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, issued);
            return sleep(1400);
        }
        std::map<int, Rect> visible;
        std::string signature;
        const auto add_difficulty_row = [&](const OCRer::Result& text) {
            if (text.score < 0.65 || text.rect.x < 850 || text.rect.y < 65 || text.rect.y > 560) {
                return;
            }
            int value = -1;
            auto normalized = text.text;
            std::erase(normalized, ' ');
            if (joint_mode) {
                value = joint_difficulty_from_label(normalized).value_or(-1);
            }
            else {
                std::smatch match;
                if (std::regex_search(normalized, match, std::regex("规程等级([0-9]{1,2})"))) {
                    const auto digits = match[1].str();
                    value = digits.size() == 1 ? digits[0] - '0' : (digits[0] - '0') * 10 + digits[1] - '0';
                }
            }
            if (value < (joint_mode ? JointTrainingDifficulty : 1) || value > cap || visible.contains(value)) {
                return;
            }
            const cv::Rect area = cv::Rect(text.rect.x, text.rect.y, text.rect.width, text.rect.height) &
                                  cv::Rect(0, 0, frame.cols, frame.rows);
            if (area.empty()) {
                return;
            }
            visible.emplace(value, text.rect);
            if (joint_mode && value == JointTrainingDifficulty) {
                signature += "0;";
                return;
            }
            cv::Mat hsv, mask;
            cv::cvtColor(frame(area), hsv, cv::COLOR_BGR2HSV);
            cv::inRange(hsv, cv::Scalar(0, 80, 80), cv::Scalar(180, 255, 255), mask);
            const bool available = value == current || cv::countNonZero(mask) >= std::max(6, area.area() / 50);
            if (!seen.contains(value) || available) {
                seen[value] = available;
            }
            signature += std::to_string(value) + (available ? "+;" : "-;");
        };
        for (const auto& text : *recognized) {
            add_difficulty_row(text);
        }
        if (!find_max && !selection_input_issued && !visible.contains(desired)) {
            OCRer rows(frame, Rect(850, 65, 430, 505));
            if (const auto recognized_rows = rows.analyze()) {
                for (const auto& text : *recognized_rows) {
                    add_difficulty_row(text);
                }
            }
        }
        unchanged = signature == previous ? unchanged + 1 : 0;
        previous = signature;
        if (selection_input_issued) {
            // One named target input is followed only by observation. The
            // selected-level marker, not text tint, proves the result.
            if (++selection_wait_frames < 2) {
                if (!sleep(350)) {
                    return false;
                }
                continue;
            }
            selection_input_issued = false;
            selection_wait_frames = 0;
            target_stability.clear();
            auto unavailable = basic_info_with_what("SPADifficultyUnavailable");
            unavailable["details"] =
                json::object { { "requested", m_target },
                               { "difficulty", desired },
                               { "mode", m_mode },
                               { "reason", "Requested difficulty was not selected after its verified target click" },
                               { "simulation_started", false } };
            callback(AsstMsg::SubTaskExtraInfo, unavailable);
            return false;
        }
        if (!find_max && visible.contains(desired)) {
            // Unselected higher entries can be gray even while unlocked.
            // Do not infer a lock from that color or click a moving label.
            const auto& target = visible.at(desired);
            if (!target_stability.observe(desired, { target.x, target.y, target.width, target.height })) {
                if (!sleep(350)) {
                    return false;
                }
                continue;
            }
            if (!ctrler()->click(target) || !sleep(500)) {
                return false;
            }
            selection_input_issued = true;
            selection_wait_frames = 0;
            continue;
        }
        if (find_max && (!visible.empty() && visible.rbegin()->first == cap || unchanged >= 2)) {
            std::vector<int> unlocked;
            for (const auto& [level, available] : seen) {
                if (available) {
                    unlocked.push_back(level);
                }
            }
            const auto resolved = resolve_difficulty(MaximumDifficulty, current, unlocked, cap);
            if (!resolved) {
                return false;
            }
            desired = *resolved;
            find_max = false;
            unchanged = 0;
            target_stability.clear();
            continue;
        }
        if (!find_max && unchanged >= 2) {
            // A fixed difficulty request must never become a harder match
            // merely because its label or unlock state could not be verified.
            break;
        }
        const auto scroll =
            joint_mode && !find_max
                ? joint_difficulty_scroll(
                      desired,
                      current,
                      visible.empty() ? std::nullopt : std::optional(visible.begin()->first),
                      visible.empty() ? std::nullopt : std::optional(visible.rbegin()->first))
                : (find_max || (!visible.empty() && desired > visible.rbegin()->first) ? DifficultyScroll::Higher
                                                                                       : DifficultyScroll::Lower);
        target_stability.clear();
        if (scroll == DifficultyScroll::Hold) {
            if (!sleep(350)) {
                return false;
            }
            continue;
        }
        const bool scroll_up = scroll == DifficultyScroll::Higher;
        if (!ctrler()->swipe(Point(1100, scroll_up ? 190 : 510), Point(1100, scroll_up ? 510 : 190), 400) ||
            !sleep(220)) {
            return false;
        }
    }
    auto unavailable = basic_info_with_what("SPADifficultyUnavailable");
    unavailable["details"] = json::object { { "requested", m_target },
                                            { "mode", m_mode },
                                            { "reason", "No verified selectable difficulty after bounded scan" } };
    callback(AsstMsg::SubTaskExtraInfo, unavailable);
    return false;
}
