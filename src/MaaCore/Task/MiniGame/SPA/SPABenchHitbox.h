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

// Use an exposed head only on the clean preparation frame that already
// established the bench positions. The resulting click still needs a title.
inline std::optional<cv::Point> bench_inventory_inspection_point(const cv::Mat& frame, cv::Point centre)
{
    const auto foot = centre + cv::Point(0, 30);
    if (!overlaps_expanded_shop_controls(foot)) {
        return foot;
    }
    return exposed_bench_head(frame, centre);
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

inline std::optional<cv::Point> exposed_bench_shoulder(const cv::Mat& image, cv::Point source)
{
    // A tall model can have its hair above the button and its left shoulder
    // beside it. Use only the visible part of this badge-backed final slot.
    const cv::Point shoulder(source.x - 16, 446);
    if (!overlaps_expanded_shop_controls(source) || overlaps_expanded_shop_controls(shoulder + cv::Point(2, 0)) ||
        !exposed_bench_head(image, source)) {
        return std::nullopt;
    }
    const cv::Rect patch(shoulder.x - 4, shoulder.y - 5, 7, 15);
    cv::Mat gray, hsv, edges;
    cv::cvtColor(image(patch), gray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(image(patch), hsv, cv::COLOR_BGR2HSV);
    cv::Canny(gray, edges, 60, 120);
    cv::Mat neutral;
    cv::inRange(hsv, cv::Scalar(0, 0, 85), cv::Scalar(180, 60, 255), neutral);
    if (neutral.at<uchar>(5, 4) == 0 || cv::countNonZero(neutral) < patch.area() / 3 ||
        cv::countNonZero(edges) < patch.area() / 4) {
        return std::nullopt;
    }
    return shoulder;
}

inline bool unchanged_blank_bench_inspection(const cv::Mat& before, const cv::Mat& after)
{
    if (!bench_inspection_panel_absent(before) || !bench_inspection_panel_absent(after)) {
        return false;
    }
    // A new camera/page/panel is not a failed hitbox. Require the same blank
    // left margin across the click, while tolerating only capture noise.
    const cv::Rect margin(0, 170, 180, 180);
    cv::Mat difference;
    cv::absdiff(before(margin), after(margin), difference);
    const auto error = cv::mean(difference);
    return std::max({ error[0], error[1], error[2] }) <= 1.0;
}

inline std::optional<cv::Point>
    bench_head_retry_target(const cv::Mat& before, const cv::Mat& after, cv::Point source, bool readable_title_seen)
{
    if (readable_title_seen || !unchanged_blank_bench_inspection(before, after)) {
        return std::nullopt;
    }
    const auto initial = exposed_bench_head(before, source);
    const auto current = exposed_bench_head(after, source);
    if (!initial || current != initial) {
        return std::nullopt;
    }
    return current;
}

inline std::optional<cv::Point>
    bench_shoulder_retry_target(const cv::Mat& before, const cv::Mat& after, cv::Point source, bool readable_title_seen)
{
    if (readable_title_seen || !unchanged_blank_bench_inspection(before, after)) {
        return std::nullopt;
    }
    const auto initial = exposed_bench_shoulder(before, source);
    return initial && initial == exposed_bench_shoulder(after, source) ? initial : std::nullopt;
}

inline std::optional<cv::Point> verified_bench_grab_point(
    const cv::Mat& image,
    cv::Point live_slot,
    cv::Point inspected_slot,
    std::optional<cv::Point> verified_contact)
{
    const cv::Point foot(live_slot.x, std::min(live_slot.y + 30, 495));
    if (!overlaps_expanded_shop_controls(foot)) {
        return foot;
    }
    // Opening a panel changes the camera. Recheck the same exposed part on
    // the reacquired slot before starting a drag from the verified hitbox.
    if (!verified_contact) {
        return std::nullopt;
    }
    const auto contact = live_slot + (*verified_contact - inspected_slot);
    if (overlaps_expanded_shop_controls(contact)) {
        return std::nullopt;
    }
    if (contact == exposed_bench_head(image, live_slot) || contact == exposed_bench_shoulder(image, live_slot)) {
        return contact;
    }
    return std::nullopt;
}
}
