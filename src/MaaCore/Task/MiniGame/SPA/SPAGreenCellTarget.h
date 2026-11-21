#pragma once

#include "SPAProjection.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace asst::spa
{
// A projected actor foot can lie on the lower edge of a raised tile. Search
// only inside its already-registered, vacant, deployable logical cell. Every
// candidate retains the ordinary green-preview threshold; even its complete
// 37x37 sampling patch must stay inside this cell, excluding neighboring green.
inline std::optional<cv::Point> registered_green_cell_target(
    const cv::Mat& held,
    const cv::Mat& grid_to_held,
    cv::Point cell,
    bool registration_verified,
    bool terrain_deployable,
    bool vacancy_verified)
{
    if (!registration_verified || !terrain_deployable || !vacancy_verified || held.size() != cv::Size(1280, 720) ||
        held.type() != CV_8UC3 || cell.x < 0 || cell.x > 10 || cell.y < 0 || cell.y > 4 ||
        grid_to_held.size() != cv::Size(3, 3) || grid_to_held.type() != CV_64FC1 || !cv::checkRange(grid_to_held) ||
        std::abs(cv::determinant(grid_to_held)) < 1e-8) {
        return std::nullopt;
    }
    const cv::Mat held_to_grid = grid_to_held.inv();
    std::vector<cv::Point> offsets;
    for (int y = -4; y <= 4; ++y) {
        for (int x = -4; x <= 4; ++x) {
            offsets.emplace_back(x, y);
        }
    }
    std::stable_sort(offsets.begin(), offsets.end(), [](const auto& left, const auto& right) {
        return left.dot(left) < right.dot(right);
    });
    for (const auto offset : offsets) {
        std::vector<cv::Point2f> pixel;
        cv::perspectiveTransform(
            std::vector<cv::Point2f> { { cell.x + offset.x / 16.0F, cell.y + offset.y / 16.0F } },
            pixel,
            grid_to_held);
        if (pixel.size() != 1 || !std::isfinite(pixel[0].x) || !std::isfinite(pixel[0].y) || pixel[0].x < 180 ||
            pixel[0].x >= 1000 || pixel[0].y < 140 || pixel[0].y >= 620) {
            continue;
        }
        const cv::Point candidate(cvRound(pixel[0].x), cvRound(pixel[0].y));
        std::vector<cv::Point2f> corners;
        cv::perspectiveTransform(
            std::vector<cv::Point2f> { { candidate.x - 18.0F, candidate.y - 18.0F },
                                       { candidate.x + 18.0F, candidate.y - 18.0F },
                                       { candidate.x + 18.0F, candidate.y + 18.0F },
                                       { candidate.x - 18.0F, candidate.y + 18.0F } },
            corners,
            held_to_grid);
        if (corners.size() != 4 || std::any_of(corners.begin(), corners.end(), [&](const auto& point) {
                return !std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x - cell.x) > .49F ||
                       std::abs(point.y - cell.y) > .49F;
            })) {
            continue;
        }
        if (green_deployment_hint(held, candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}
}
