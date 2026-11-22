#pragma once

#include "MaaUtils/NoWarningCV.hpp"

#include <algorithm>
#include <cstdlib>
#include <optional>

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

inline int strategy_report_scroll_destination(const StrategyReportScrollbar& thumb, bool toward_top)
{
    if (toward_top) {
        return thumb.center() - (thumb.top - StrategyReportScrollTop);
    }
    // One drag exposes less than half a viewport, retaining complete rows across the boundary.
    return thumb.center() + std::min(StrategyReportScrollBottom - thumb.bottom, thumb.height() * 43 / 100);
}
}
