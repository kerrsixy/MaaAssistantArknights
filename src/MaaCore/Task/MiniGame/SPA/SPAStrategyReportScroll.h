#pragma once

#include "MaaUtils/NoWarningCV.hpp"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <span>
#include <opencv2/imgproc.hpp>

namespace asst::spa
{
inline constexpr int StrategyReportScrollX = 1261;
inline constexpr int StrategyReportScrollTop = 109;
inline constexpr int StrategyReportScrollBottom = 688;

struct StrategyReportScrollbar
{
    int top;
    int bottom;

    int height() const { return bottom - top + 1; }
    int center() const { return (top + bottom) / 2; }
    bool at_top() const { return top <= StrategyReportScrollTop + 4; }
    bool at_bottom() const { return bottom >= StrategyReportScrollBottom - 4; }
};

inline std::optional<StrategyReportScrollbar> strategy_report_scrollbar(const cv::Mat& image)
{
    if (image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3) {
        return std::nullopt;
    }
    std::optional<StrategyReportScrollbar> thumb;
    int first = -1;
    for (int y = StrategyReportScrollTop - 5; y <= StrategyReportScrollBottom + 6; ++y) {
        const auto* row = image.ptr<cv::Vec3b>(y);
        int matching_pixels = 0;
        for (int x = StrategyReportScrollX - 2; x <= StrategyReportScrollX + 2; ++x) {
            const auto pixel = row[x];
            matching_pixels += pixel[2] <= 92 && pixel[1] <= 106 && pixel[0] <= 114 &&
                               row[StrategyReportScrollX - 6][1] >= pixel[1] + 43 &&
                               row[StrategyReportScrollX + 6][1] >= pixel[1] + 43;
        }
        if (matching_pixels >= 3) {
            if (first < 0) {
                first = y;
            }
        }
        else if (first >= 0) {
            const int height = y - first;
            if (height >= 60) {
                if (thumb || height > 420) {
                    return std::nullopt;
                }
                thumb = StrategyReportScrollbar { first, y - 1 };
            }
            first = -1;
        }
    }
    return first < 0 ? thumb : std::nullopt;
}

inline std::optional<int> strategy_report_trailing_row(
    const cv::Mat& image,
    std::span<const int> rows,
    std::span<const int, 2> leading_columns,
    const std::optional<StrategyReportScrollbar>& scrollbar)
{
    if (!scrollbar || !scrollbar->at_bottom() || rows.size() < 3 || image.size() != cv::Size(1280, 720) ||
        image.type() != CV_8UC3) {
        return std::nullopt;
    }
    const int last_spacing = rows.back() - rows[rows.size() - 2];
    const int previous_spacing = rows[rows.size() - 2] - rows[rows.size() - 3];
    if (std::abs(last_spacing - 137) > 2 || std::abs(previous_spacing - 137) > 2) {
        return std::nullopt;
    }
    const int next_row = rows.back() + (last_spacing + previous_spacing + 1) / 2;
    if (next_row < 550 || next_row > 594) {
        return std::nullopt;
    }
    // In the last viewport, the page gradient can erase the dark tile edges.
    // Require both portraits at the predicted row before trying either card.
    const bool portraits_present = std::ranges::all_of(leading_columns, [&](int column) {
        const cv::Rect portrait(column + 18, next_row + 24, 84, 90);
        if ((portrait & cv::Rect(0, 0, image.cols, image.rows)) != portrait) {
            return false;
        }
        int colored_pixels = 0;
        for (const auto& pixel : cv::Mat_<cv::Vec3b>(image(portrait))) {
            const int red = pixel[2];
            colored_pixels += pixel[1] >= 80 && pixel[1] - red > 28 && pixel[0] - red > 20;
        }
        return colored_pixels >= portrait.area() / 50;
    });
    return portraits_present ? std::optional(next_row) : std::nullopt;
}

inline bool strategy_report_cell_selected(const cv::Mat& image, int x, int y)
{
    const cv::Rect top(x + 18, y, 82, 17);
    const cv::Rect side(x - 3, y + 21, 10, 96);
    const cv::Rect frame(0, 0, image.cols, image.rows);
    if (image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3 || (top & frame) != top ||
        (side & frame) != side) {
        return false;
    }
    const cv::Scalar white_lower(190, 190, 190), white_upper(255, 255, 255);
    cv::Mat side_mask;
    cv::inRange(image(side), white_lower, white_upper, side_mask);
    if (cv::countNonZero(side_mask) < side.area() * .25) {
        return false;
    }
    cv::Mat top_mask;
    cv::inRange(image(top), white_lower, white_upper, top_mask);
    const auto standard = top_mask.rowRange(4, 14);
    if (cv::countNonZero(standard) >= standard.total() * .45) {
        return true;
    }
    // The bottom gradient leaves a thin but continuous selected outline.
    // It is still anchored to this precise tile by the side evidence.
    int adjacent_bright_lines = 0;
    for (int row = 0; row < top_mask.rows; ++row) {
        if (cv::countNonZero(top_mask.row(row)) < top_mask.cols * .85) {
            adjacent_bright_lines = 0;
        }
        else if (++adjacent_bright_lines == 2) {
            return true;
        }
    }
    return false;
}

inline bool same_strategy_report_scrollbar(const StrategyReportScrollbar& left, const StrategyReportScrollbar& right)
{
    return std::abs(left.top - right.top) <= 2 && std::abs(left.bottom - right.bottom) <= 2;
}

inline bool strategy_report_scroll_moved(
    const StrategyReportScrollbar& before,
    const StrategyReportScrollbar& after,
    bool toward_top)
{
    const int direction = toward_top ? -1 : 1;
    return std::abs(before.height() - after.height()) <= 5 &&
           direction * (after.top - before.top) >= 4 && direction * (after.bottom - before.bottom) >= 4;
}

inline bool strategy_report_selection_shifted_viewport(
    const std::optional<StrategyReportScrollbar>& before,
    const std::optional<StrategyReportScrollbar>& after,
    std::span<const int> before_rows,
    std::span<const int> after_rows)
{
    if (before && after) {
        return strategy_report_scroll_moved(*before, *after, true) ||
               strategy_report_scroll_moved(*before, *after, false);
    }
    // Selecting a card can change the template score for its entire row.
    // Without a scrollbar pair, require two rows on both sides and a
    // distinct set of row positions before discarding the current scan.
    return before_rows.size() >= 2 && after_rows.size() >= 2 && std::ranges::none_of(before_rows, [&](int row) {
               return std::ranges::any_of(after_rows, [&](int current) { return std::abs(row - current) <= 4; });
           });
}

inline bool strategy_report_card_badge_matches(
    const cv::Mat& card_corner,
    const cv::Mat& expected_mark,
    const cv::Mat& other_mark)
{
    if (card_corner.type() != CV_8UC3 || expected_mark.empty() || other_mark.empty()) {
        return false;
    }
    const auto confidence = [&](const cv::Mat& mark) {
        if (mark.type() != card_corner.type() || mark.rows > card_corner.rows || mark.cols > card_corner.cols) {
            return -1.0;
        }
        cv::Mat scores;
        cv::matchTemplate(card_corner, mark, scores, cv::TM_CCOEFF_NORMED);
        double best = 0;
        cv::minMaxLoc(scores, nullptr, &best);
        return best;
    };
    const double expected = confidence(expected_mark);
    const double other = confidence(other_mark);
    // The tile portrait and selection border change the background. Require
    // both a strong match and a separation from the other badge colour.
    return expected >= .80 && other >= 0 && expected - other >= .18;
}

inline int strategy_report_scroll_destination(const StrategyReportScrollbar& thumb, bool toward_top)
{
    if (toward_top) {
        return thumb.center() - (thumb.top - StrategyReportScrollTop);
    }
    // One drag exposes less than half a viewport, retaining complete rows across the boundary.
    return thumb.center() + std::min(StrategyReportScrollBottom - thumb.bottom, thumb.height() * 43 / 100);
}
}
