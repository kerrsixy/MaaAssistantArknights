#pragma once

#include "MaaUtils/NoWarningCV.hpp"
#include <opencv2/imgproc.hpp>

#include <cstdlib>
#include <optional>

namespace asst::spa
{
inline std::optional<int> joint_emote_page(const cv::Mat& before, const cv::Mat& panel)
{
    const cv::Rect menu(16, 438, 292, 195);
    if (before.size() != cv::Size(1280, 720) || panel.size() != before.size() || before.type() != CV_8UC3 ||
        panel.type() != CV_8UC3) {
        return std::nullopt;
    }
    cv::Mat changed, gray, mask;
    cv::absdiff(before(menu), panel(menu), changed);
    cv::cvtColor(panel(menu), gray, cv::COLOR_BGR2GRAY);
    if (cv::mean(changed)[0] < 15 || cv::mean(gray)[0] > 100) {
        return std::nullopt;
    }

    // The carousel retains its previously selected page. All six indicators
    // share one baseline, and the active white bar is wider than the dots.
    const cv::Mat indicators = gray(cv::Rect(117, 183, 55, 11));
    cv::inRange(indicators, cv::Scalar(70), cv::Scalar(255), mask);
    cv::Mat labels, stats, centroids;
    if (cv::connectedComponentsWithStats(mask, labels, stats, centroids) != 7) {
        return std::nullopt;
    }
    std::optional<int> active_x;
    int baseline = -1;
    for (int i = 1; i < 7; ++i) {
        const auto* item = stats.ptr<int>(i);
        const int x = item[cv::CC_STAT_LEFT], y = item[cv::CC_STAT_TOP];
        const int width = item[cv::CC_STAT_WIDTH], height = item[cv::CC_STAT_HEIGHT];
        if ((baseline >= 0 && std::abs(y - baseline) > 1) || height < 3 || height > 6 || width < 3 || width > 12) {
            return std::nullopt;
        }
        baseline = y;
        if (width >= 8) {
            cv::Mat bright;
            cv::inRange(indicators(cv::Rect(x, y, width, height)), cv::Scalar(205), cv::Scalar(255), bright);
            if (active_x || cv::countNonZero(bright) < width * 2) {
                return std::nullopt;
            }
            active_x = x;
        }
    }
    if (!active_x) {
        return std::nullopt;
    }
    int page = 1;
    for (int i = 1; i < 7; ++i) {
        page += stats.at<int>(i, cv::CC_STAT_LEFT) < *active_x;
    }
    return page;
}

inline int joint_emote_column(int page, std::optional<int> local_leaks)
{
    if (page == 2 && local_leaks && *local_leaks > 0) {
        return *local_leaks > 5 ? 1 : 2;
    }
    // The first two buttons on page three are reserved for praise and aid.
    return page == 3 ? 2 : 0;
}
}

