#include "SPAStrategyReportTaskPlugin.h"

#include "Config/TemplResource.h"
#include "Controller/Controller.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "SPAImageEvidence.h"
#include "SPAObserverTaskPlugin.h"
#include "SPAOpeningControls.h"
#include "SPAPlanner.h"
#include "SPAStrategyEligibility.h"
#include "SPAStrategyReportScroll.h"
#include "Vision/OCRer.h"

#include <array>
#include <cmath>
#include <map>
#include <set>
#include <opencv2/imgproc.hpp>

namespace asst::spa
{
static constexpr std::array<int, 5> StrategyReportColumns { 578, 710, 841, 973, 1104 };
static constexpr int StrategyReportRowTop = 90;
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
        const cv::Rect strip(
            StrategyReportColumns[col] - 3,
            StrategyReportRowTop - 3,
            13,
            StrategyReportRowBottom - StrategyReportRowTop + 3 + edge.rows);
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

static bool same_strategy_report_grid(const cv::Mat& left, const cv::Mat& right)
{
    const cv::Rect catalogue(565, StrategyReportRowTop, 675, 705 - StrategyReportRowTop);
    return left.size() == right.size() && left.type() == right.type() &&
           cv::norm(left(catalogue), right(catalogue), cv::NORM_L1) / (catalogue.area() * 3.0) < 2.0;
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

    const auto scroll_viewport = [&](bool toward_top) -> bool {
        const auto origin = strategy_report_scrollbar(image);
        const auto await_viewport = [&]() -> cv::Mat {
            std::optional<StrategyReportScrollbar> previous_thumb;
            cv::Mat previous;
            for (int sample = 0; sample < 7 && !need_exit(); ++sample) {
                auto current = ctrler()->get_image();
                const auto thumb = strategy_report_scrollbar(current);
                const bool advanced = origin ? thumb && strategy_report_scroll_moved(*origin, *thumb, toward_top)
                                             : !same_strategy_report_grid(image, current);
                const bool settled = origin ? thumb && previous_thumb &&
                                                  same_strategy_report_scrollbar(*previous_thumb, *thumb)
                                            : !previous.empty() && same_strategy_report_grid(previous, current);
                if (advanced && settled && !strategy_report_rows(current, edge).empty() &&
                    strategy_report_tab_visible(current)) {
                    return current;
                }
                previous_thumb = advanced ? thumb : std::nullopt;
                previous = advanced ? std::move(current) : cv::Mat {};
                if (!sleep(240)) {
                    break;
                }
            }
            return {};
        };
        bool input_sent = false;
        if (origin) {
            const int destination = strategy_report_scroll_destination(*origin, toward_top);
            input_sent = ctrler()->swipe(
                Point(StrategyReportScrollX, origin->center()), Point(StrategyReportScrollX, destination), 430);
        }
        cv::Mat next;
        if (input_sent && sleep(420)) {
            next = await_viewport();
        }
        if (next.empty() && !need_exit()) {
            // A drag on the list is a fallback when the scrollbar did not move.
            // Do not send a second scroll through a viewport that is already shifting.
            const auto current = ctrler()->get_image();
            const auto thumb = strategy_report_scrollbar(current);
            const bool unchanged = origin ? thumb && same_strategy_report_scrollbar(*origin, *thumb)
                                          : same_strategy_report_grid(image, current);
            if (unchanged && strategy_report_tab_visible(current) &&
                ctrler()->swipe(Point(900, toward_top ? 320 : 610), Point(900, toward_top ? 610 : 320), 620) &&
                sleep(420)) {
                next = await_viewport();
            }
        }
        const auto after = strategy_report_scrollbar(next);
        auto receipt = basic_info_with_what("SPAStrategyReportViewport");
        receipt["details"] = json::object {
            { "toward_top", toward_top }, { "verified", !next.empty() },
            { "scrollbar_before", origin ? origin->top : -1 }, { "scrollbar_after", after ? after->top : -1 }
        };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        if (next.empty()) {
            save_evidence(ctrler()->get_image(), utils::path("debug/SPA/strategy_report_scroll_unverified"), false);
            return false;
        }
        image = std::move(next);
        return true;
    };

    bool top_verified = false;
    for (int attempt = 0; attempt < 4 && !need_exit(); ++attempt) {
        const auto scrollbar = strategy_report_scrollbar(image);
        if (!scrollbar) {
            break;
        }
        if (scrollbar->at_top()) {
            top_verified = true;
            break;
        }
        if (!scroll_viewport(true)) {
            break;
        }
    }
    ObserverTaskPlugin observer(m_callback, m_inst, m_task_chain);
    observer.set_task_id(m_task_id);
    std::set<std::string> identified;
    std::map<std::string, json::object> locked;
    std::map<std::string, StrategyProgress> progress;
    int visited_viewports = 0;
    bool bottom_verified = false;
    constexpr int max_viewports = 18;
    while (visited_viewports < max_viewports && !need_exit()) {
        auto rows = strategy_report_rows(image, edge);
        if (rows.empty() || !strategy_report_tab_visible(image)) {
            break;
        }
        ++visited_viewports;
        const auto viewport_scrollbar = strategy_report_scrollbar(image);
        const std::span<const int> columns = StrategyReportColumns;
        const auto trailing_columns = columns.first<2>();
        const auto trailing_row = strategy_report_trailing_row(image, rows, trailing_columns, viewport_scrollbar);
        if (trailing_row) {
            rows.push_back(*trailing_row);
        }
        bool viewport_changed = false;
        for (const int row : rows) {
            const auto row_columns = trailing_row == row ? std::span<const int>(trailing_columns) : columns;
            for (const int column : row_columns) {
                if (need_exit()) {
                    return false;
                }
                if (!strategy_report_cell_selected(image, column, row)) {
                    if (!ctrler()->click(Point(column + 58, row + 68)) || !sleep(280)) {
                        return false;
                    }
                    image = ctrler()->get_image();
                    const auto scrolled = strategy_report_scrollbar(image);
                    const auto current_rows =
                        viewport_scrollbar && scrolled ? std::vector<int> {} : strategy_report_rows(image, edge);
                    if (strategy_report_selection_shifted_viewport(viewport_scrollbar, scrolled, rows, current_rows)) {
                        viewport_changed = true;
                        auto receipt = basic_info_with_what("SPAStrategyReportSelectionShift");
                        receipt["details"] = json::object {
                            { "scrollbar_before", viewport_scrollbar ? viewport_scrollbar->top : -1 },
                            { "scrollbar_after", scrolled ? scrolled->top : -1 },
                            { "viewports_scanned", visited_viewports }
                        };
                        callback(AsstMsg::SubTaskExtraInfo, receipt);
                        break;
                    }
                    if (!strategy_report_cell_selected(image, column, row)) {
                        continue;
                    }
                }
                // The outline above confirms the selected tile in this
                // exact frame. Confirm its detail identity before crediting
                // a mark from this one corner, never from a nearby card.
                if (!observer.observe_strategy_report_card(image, Rect(column + 80, row + 5, 42, 40))) {
                    continue;
                }
                const auto& observation = observer.last_observation();
                const auto locked_id = observation.get("locked_strategy_id", std::string());
                if (!locked_id.empty()) {
                    identified.insert(locked_id);
                    if (!progress.contains(locked_id)) {
                        locked.insert_or_assign(
                            locked_id,
                            json::object { { "band_id", locked_id },
                                           { "current", observation.get("locked_strategy_current", -1) },
                                           { "required", observation.get("locked_strategy_required", -1) } });
                    }
                    continue;
                }
                const auto id = observation.get("band_id", std::string());
                const auto rule = strategy_unlock_rule(id);
                if (observation.get("page", std::string()) != "strategy_report" || !rule ||
                    observation.get("contradictory", true) ||
                    !observation.get("strategy_report_identity_verified", false)) {
                    continue;
                }
                identified.insert(id);
                const auto wins = observation.get("strategy_progress_text_verified", false)
                                      ? observation.find<int>("wins")
                                      : std::nullopt;
                // An unlocked detail panel can be verified before its first
                // visible win counter. It does not establish a zero baseline.
                if (!wins && rule->kind != StrategyUnlockKind::Default &&
                    !observation.get("strategy_report_unlock_verified", false)) {
                    continue;
                }
                if (m_portrait_sink) {
                    auto portrait = strategy_portrait(image, cv::Rect(column, row, 120, 120));
                    if (!portrait.empty()) {
                        m_portrait_sink(id, std::move(portrait));
                    }
                }
                locked.erase(id);
                const bool badge_verified = wins && observation.get("badge_verified", false) &&
                                            observation.get("strategy_report_evidence_saved", false);
                const bool silver_verified =
                    badge_verified && *wins >= 6 && observation.get("strategy_badge_tier", std::string()) == "silver";
                StrategyProgress result {
                    id, wins, true, badge_verified, rule->independent_available, silver_verified
                };
                const auto previous = progress.find(id);
                if (previous == progress.end() || silver_verified ||
                    (!previous->second.silver_badge_verified &&
                     (badge_verified || (wins && !previous->second.badge_verified)))) {
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
        if (visited_viewports == max_viewports || progress.size() == StrategyUnlockCatalogue.size() ||
            (!m_target.empty() && progress.contains(m_target))) {
            break;
        }
        if (viewport_changed) {
            // A click during scroll may leave the report on a new grid.
            // Rematch this view before attempting any other report cell.
            continue;
        }
        const auto scrollbar = strategy_report_scrollbar(image);
        if (scrollbar && scrollbar->at_bottom()) {
            bottom_verified = true;
            break;
        }
        if (!scroll_viewport(false)) {
            break;
        }
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
                           { "badge_verified", record.badge_verified },
                           { "silver_badge_verified", record.silver_badge_verified } });
    }
    json::array locked_entries;
    for (const auto& [id, entry] : locked) {
        locked_entries.emplace_back(entry);
    }
    auto info = basic_info_with_what("SPAStrategyReportScan");
    info["details"] =
        json::object { { "target_band_id", m_target },
                       { "target_progress_verified", !m_target.empty() && progress.contains(m_target) },
                       { "strategies", std::move(entries) },
                       { "locked_strategies", std::move(locked_entries) },
                       { "viewports_scanned", visited_viewports },
                       { "scrollbar_top_verified", top_verified },
                       { "scrollbar_bottom_verified", bottom_verified },
                       { "identified_strategies", identified.size() },
                       { "observed_strategy_ids", identified },
                       { "expected_strategies", StrategyUnlockCatalogue.size() },
                       { "all_strategy_identities_observed", identified.size() == StrategyUnlockCatalogue.size() },
                       { "all_strategy_badges_verified",
                         progress.size() == StrategyUnlockCatalogue.size() &&
                             std::all_of(ranked.begin(), ranked.end(), ProgressLedger::complete) },
                       { "all_strategy_silver_badges_verified",
                         progress.size() == StrategyUnlockCatalogue.size() &&
                             std::all_of(ranked.begin(), ranked.end(), ProgressLedger::silver_complete) },
                       { "next_independent_strategy", ProgressLedger::next_solo(ranked).value_or("") },
                       { "next_independent_silver_strategy", ProgressLedger::next_solo(ranked, true).value_or("") } };
    callback(AsstMsg::SubTaskExtraInfo, info);
    return !need_exit();
}
}
