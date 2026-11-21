#pragma once

#include "SPAProjection.h"
#include <algorithm>
#include <optional>

namespace asst::spa
{
// A retry is an inspection, never identity or sale evidence. This deliberately
// supports only the measured rightmost bench slot whose lower body overlaps
// the expanded shop's freeze control. Other layouts keep the normal failure.
inline std::optional<cv::Point> exposed_bench_head(const cv::Mat& image, cv::Point source)
{
    if (image.type() != CV_8UC3 || image.size() != cv::Size(1280, 720) || source.y != 450 || source.x < 940 ||
        source.x > 975) {
        return std::nullopt;
    }
    const auto centres = bench_source_centres(image);
    // The final detected slot is directly badge-supported: gap recovery in
    // bench_source_centres can only insert internal slots, never the last one.
    if (centres.empty() || centres.back() != source) {
        return std::nullopt;
    }
    // The inspected patch lies below the tier badge and entirely above the
    // freeze button (y >= 430). No horizontal search, neighbouring slot, or
    // equipment-row probe is allowed. Require an actually visible textured
    // head, not merely the badge or a fixed upward offset.
    const cv::Rect head(source.x - 10, 412, 21, 15);
    cv::Mat gray, hsv, edges;
    cv::cvtColor(image(head), gray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(image(head), hsv, cv::COLOR_BGR2HSV);
    cv::Canny(gray, edges, 60, 120);
    cv::Scalar mean, stddev;
    cv::meanStdDev(gray, mean, stddev);
    cv::Mat blue;
    cv::inRange(hsv, cv::Scalar(90, 100, 50), cv::Scalar(140, 255, 255), blue);
    const double area = static_cast<double>(head.area());
    if (stddev[0] < 35 || cv::countNonZero(edges) / area < .20 || cv::countNonZero(gray < 80) / area < .03 ||
        cv::countNonZero(gray > 180) / area < .18 || cv::countNonZero(blue) / area > .03 ||
        cv::mean(gray(cv::Rect(9, 7, 3, 3)))[0] < 80) {
        return std::nullopt;
    }
    return cv::Point(source.x, 420);
}

inline bool bench_inspection_panel_absent(const cv::Mat& image)
{
    if (image.type() != CV_8UC3 || image.size() != cv::Size(1280, 720)) {
        return false;
    }
    // Positive evidence for this narrow layout: the entire title/stat area is
    // the neutral, dark map-edge vignette. A readable or unreadable operator
    // or equipment panel introduces text/portrait pixels here. Maps without
    // this blank margin are intentionally unsupported, not assumed clear.
    const auto panel = image(cv::Rect(0, 170, 180, 180));
    cv::Mat gray, hsv, edges;
    cv::cvtColor(panel, gray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(panel, hsv, cv::COLOR_BGR2HSV);
    cv::Canny(gray, edges, 30, 60);
    cv::Mat coloured;
    cv::inRange(hsv, cv::Scalar(0, 4, 0), cv::Scalar(180, 255, 255), coloured);
    const double area = static_cast<double>(panel.total());
    return cv::countNonZero(gray > 110) == 0 && cv::countNonZero(coloured) / area <= .001 &&
           cv::countNonZero(edges) / area <= .001;
}

inline std::optional<cv::Point>
    bench_head_retry_target(const cv::Mat& before, const cv::Mat& after, cv::Point source, bool readable_title_seen)
{
    if (readable_title_seen || !bench_inspection_panel_absent(before) || !bench_inspection_panel_absent(after)) {
        return std::nullopt;
    }
    const auto initial = exposed_bench_head(before, source);
    const auto current = exposed_bench_head(after, source);
    if (!initial || current != initial) {
        return std::nullopt;
    }
    // A new camera/page/panel is not a failed hitbox. Require the same blank
    // left margin across the click, while tolerating only capture noise.
    cv::Mat difference;
    cv::absdiff(before(cv::Rect(0, 170, 180, 180)), after(cv::Rect(0, 170, 180, 180)), difference);
    const auto error = cv::mean(difference);
    if (std::max({ error[0], error[1], error[2] }) > 1.0) {
        return std::nullopt;
    }
    return current;
}
}
