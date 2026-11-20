#include "SPAStrategyReportTaskPlugin.h"

#include "Config/TemplResource.h"
#include "Controller/Controller.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "SPAObserverTaskPlugin.h"
#include "SPAPlanner.h"
#include "SPAStrategyEligibility.h"
#include "Vision/OCRer.h"

#include <array>
#include <cmath>
#include <map>
#include <opencv2/imgproc.hpp>

namespace asst::spa
{
static constexpr std::array<int, 5> StrategyReportColumns { 578, 710, 841, 973, 1104 };
static constexpr int StrategyReportRowTop = 102;
static constexpr int StrategyReportRowBottom = 594;

static bool strategy_report_tab_visible(const cv::Mat& image)
{
    if (image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3) {
        return false;
    }
    OCRer tab(image, Rect(685, 20, 178, 56));
    const auto texts = tab.analyze();
    return texts && std::any_of(texts->begin(), texts->end(), [](const auto& text) {
               return text.score >= .90 && text.text == "策略报告";
           });
}

static std::vector<int> strategy_report_rows(const cv::Mat& image, const cv::Mat& edge)
{
    if (image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3 || edge.size() != cv::Size(6, 23)) {
        return {};
    }
    std::array<cv::Mat, StrategyReportColumns.size()> matches;
    for (size_t col = 0; col < matches.size(); ++col) {
        const cv::Rect strip(StrategyReportColumns[col] - 3, StrategyReportRowTop - 3, 13, 520);
        cv::Mat gray;
        cv::cvtColor(image(strip), gray, cv::COLOR_BGR2GRAY);
        cv::matchTemplate(gray, edge, matches[col], cv::TM_CCOEFF_NORMED);
    }
    std::vector<int> rows;
    double previous_score = 0;
    for (int y = StrategyReportRowTop; y <= StrategyReportRowBottom; ++y) {
        const int offset = y - StrategyReportRowTop + 3;
        int supported = 0;
        double score = 0;
        for (const auto& match : matches) {
            double confidence = 0;
            cv::minMaxLoc(match.row(offset), nullptr, &confidence);
            if (confidence >= .91) {
                ++supported;
                score += confidence;
            }
        }
        if (supported < 2) {
            continue;
        }
        if (rows.empty() || y - rows.back() > 5) {
            rows.push_back(y);
            previous_score = score;
        }
        else if (score > previous_score) {
            rows.back() = y;
            previous_score = score;
        }
    }
    return rows;
}

static bool strategy_report_cell_selected(const cv::Mat& image, int x, int y)
{
    if (image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3) {
        return false;
    }
    const auto pale_border = [&](const cv::Rect& area, double proportion) {
        cv::Mat pale;
        cv::inRange(image(area), cv::Scalar(190, 190, 190), cv::Scalar(255, 255, 255), pale);
        return cv::countNonZero(pale) >= area.area() * proportion;
    };
    return pale_border(cv::Rect(x + 18, y + 4, 82, 10), .45) && pale_border(cv::Rect(x - 3, y + 21, 10, 96), .25);
}

bool StrategyReportTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    return msg == AsstMsg::SubTaskStart && details.get("subtask", std::string()) == "ProcessTask" &&
           details.get("details", "task", std::string()) == "MiniGame@SPA@ScanStrategyReport";
}

bool StrategyReportTaskPlugin::_run()
{
    const auto& edge_template = TemplResource::get_instance().get_templ("MiniGame@SPA@StrategyReportTileEdge.png");
    if (edge_template.size() != cv::Size(6, 23) || edge_template.type() != CV_8UC3) {
        return false;
    }
    cv::Mat edge;
    cv::cvtColor(edge_template, edge, cv::COLOR_BGR2GRAY);
    auto image = ctrler()->get_image();
    if (!strategy_report_tab_visible(image) || strategy_report_rows(image, edge).empty()) {
        return false;
    }

    ObserverTaskPlugin observer(m_callback, m_inst, m_task_chain);
    observer.set_task_id(m_task_id);
    std::map<std::string, StrategyProgress> progress;
    int visited_viewports = 0;
    for (; visited_viewports < 10 && !need_exit(); ++visited_viewports) {
        const auto rows = strategy_report_rows(image, edge);
        if (rows.empty() || !strategy_report_tab_visible(image)) {
            break;
        }
        bool viewport_changed = false;
        for (const int row : rows) {
            for (const int column : StrategyReportColumns) {
                if (need_exit()) {
                    return false;
                }
                if (!strategy_report_cell_selected(image, column, row)) {
                    if (!ctrler()->click(Point(column + 58, row + 68)) || !sleep(280)) {
                        return false;
                    }
                    image = ctrler()->get_image();
                    if (!strategy_report_cell_selected(image, column, row)) {
                        if (strategy_report_rows(image, edge) != rows) {
                            viewport_changed = true;
                            break;
                        }
                        continue;
                    }
                }
                if (!observer.observe_frame(image)) {
                    continue;
                }
                const auto& observation = observer.last_observation();
                const auto id = observation.get("band_id", std::string());
                if (observation.get("page", std::string()) != "strategy_report" || !strategy_unlock_rule(id) ||
                    observation.get("contradictory", true)) {
                    continue;
                }
                if (!observation.get("strategy_progress_text_verified", false)) {
                    // A selected tile plus generic page OCR is insufficient proof of the exact identity.
                    continue;
                }
                const bool badge_verified = observation.get("badge_verified", false) &&
                                            observation.get("strategy_report_evidence_saved", false);
                StrategyProgress result { id,
                                          observation.get("wins", 0),
                                          true,
                                          badge_verified,
                                          strategy_available_in_mode(id, "independent") };
                if (!progress.contains(id) || badge_verified || !progress.at(id).badge_verified) {
                    progress.insert_or_assign(id, std::move(result));
                }
                if (!m_target.empty() && progress.contains(m_target)) {
                    break;
                }
            }
            if (viewport_changed || (!m_target.empty() && progress.contains(m_target))) {
                break;
            }
        }
        if (viewport_changed || progress.size() == StrategyUnlockCatalogue.size() ||
            (!m_target.empty() && progress.contains(m_target))) {
            break;
        }
        if (!ctrler()->swipe(Point(1185, 640), Point(1185, 250), 370) || !sleep(500)) {
            return false;
        }
        const auto next = ctrler()->get_image();
        if (next.size() != image.size() || next.type() != image.type()) {
            break;
        }
        const cv::Rect catalogue(565, 120, 675, 585);
        if (cv::norm(image(catalogue), next(catalogue), cv::NORM_L1) / (catalogue.area() * 3.0) < 2.0) {
            break;
        }
        image = next;
    }

    json::array entries;
    std::vector<StrategyProgress> ranked;
    for (const auto& [id, record] : progress) {
        ranked.push_back(record);
        entries.emplace_back(
            json::object { { "band_id", id },
                           { "wins_known", record.wins.has_value() },
                           { "wins", record.wins.value_or(0) },
                           { "unlocked_verified", record.unlocked },
                           { "badge_verified", record.badge_verified } });
    }
    auto info = basic_info_with_what("SPAStrategyReportScan");
    info["details"] =
        json::object { { "target_band_id", m_target },
                       { "target_progress_verified", !m_target.empty() && progress.contains(m_target) },
                       { "strategies", std::move(entries) },
                       { "identified_strategies", progress.size() },
                       { "expected_strategies", StrategyUnlockCatalogue.size() },
                       { "all_strategy_identities_observed", progress.size() == StrategyUnlockCatalogue.size() },
                       { "all_strategy_badges_verified",
                         progress.size() == StrategyUnlockCatalogue.size() &&
                             std::all_of(ranked.begin(), ranked.end(), ProgressLedger::complete) },
                       { "next_independent_strategy", ProgressLedger::next_solo(ranked).value_or("") } };
    callback(AsstMsg::SubTaskExtraInfo, info);
    return !need_exit();
}
}
