#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <vector>

#include "MaaUtils/NoWarningCV.hpp"

namespace asst::spa
{
inline std::optional<int> opening_strategy_countdown_seconds(std::string_view text)
{
    if (text.empty() || text.size() > 2) {
        return std::nullopt;
    }
    int seconds = 0;
    for (const char digit : text) {
        if (digit < '0' || digit > '9') {
            return std::nullopt;
        }
        seconds = seconds * 10 + (digit - '0');
    }
    return seconds;
}

inline int opening_strategy_inspection_seconds(std::optional<int> countdown)
{
    return std::clamp(countdown.value_or(24) - 8, 0, 38);
}

inline std::optional<int> opening_strategy_display_countdown(const cv::Mat& frame)
{
    if (frame.size() != cv::Size(1280, 720) || frame.type() != CV_8UC3) {
        return std::nullopt;
    }
    // Clockwise from the top bar, with the middle bar last. The left digit's
    // right stroke can be four pixels wide and sit one pixel further left.
    const std::array<cv::Rect, 7> segments { cv::Rect(7, 25, 12, 5), cv::Rect(21, 33, 4, 10), cv::Rect(21, 56, 4, 10),
                                             cv::Rect(7, 68, 12, 4), cv::Rect(0, 56, 5, 10),  cv::Rect(0, 33, 5, 10),
                                             cv::Rect(7, 47, 12, 5) };
    constexpr std::array digit_masks { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
    const auto read = [&](const cv::Scalar& lower, const cv::Scalar& upper) -> std::optional<int> {
        int seconds = 0;
        for (const int x : { 1133, 1165 }) {
            int mask = 0;
            for (std::size_t i = 0; i < segments.size(); ++i) {
                const auto area = segments[i] + cv::Point(x, 0);
                cv::Mat color;
                cv::inRange(frame(area), lower, upper, color);
                const auto lit = cv::countNonZero(color);
                if (lit * 4 >= area.area() * 3) {
                    mask |= 1 << i;
                }
                else if (lit * 5 > area.area()) {
                    return std::nullopt;
                }
            }
            const auto digit = std::find(digit_masks.begin(), digit_masks.end(), mask);
            if (digit == digit_masks.end()) {
                return std::nullopt;
            }
            seconds = seconds * 10 + static_cast<int>(digit - digit_masks.begin());
        }
        return seconds;
    };
    const auto cyan = read(cv::Scalar(105, 155, 0), cv::Scalar(255, 255, 95));
    const auto orange = read(cv::Scalar(0, 45, 175), cv::Scalar(85, 145, 255));
    if (cyan && orange && *cyan != *orange) {
        return std::nullopt;
    }
    return cyan ? cyan : orange;
}

// Catalogue order provides a first-viewport hint. Every slot remains a
// candidate because a client filter can change which cards appear.
inline std::vector<int> opening_strategy_probe_order(int visible_slots, int preferred_slot)
{
    std::vector<int> slots;
    if (preferred_slot >= 0 && preferred_slot < visible_slots) {
        slots.push_back(preferred_slot);
    }
    for (int index = 0; index < visible_slots; ++index) {
        if (slots.empty() || slots.front() != index) {
            slots.push_back(index);
        }
    }
    return slots;
}

inline int opening_strategy_scrollbar_x(bool details_open)
{
    return details_open ? 956 : 1220;
}

inline std::optional<int> opening_strategy_scrollbar_center(const cv::Mat& frame, bool details_open)
{
    if (frame.size() != cv::Size(1280, 720) || frame.type() != CV_8UC3) {
        return std::nullopt;
    }
    const int x = opening_strategy_scrollbar_x(details_open);
    int first = -1;
    std::optional<int> center;
    for (int y = 105; y < 700; ++y) {
        int thumb_pixels = 0;
        for (int dx = -2; dx <= 2; ++dx) {
            const auto pixel = frame.at<cv::Vec3b>(y, x + dx);
            const auto [darkest, brightest] = std::minmax({ pixel[0], pixel[1], pixel[2] });
            thumb_pixels += darkest > 105 && brightest - darkest < 26;
        }
        if (thumb_pixels >= 3) {
            if (first < 0) {
                first = y;
            }
            continue;
        }
        if (first >= 0 && y - first >= 60) {
            if (center || y - first > 240) {
                return std::nullopt;
            }
            center = (first + y - 1) / 2;
        }
        first = -1;
    }
    return first < 0 ? center : std::nullopt;
}

inline bool strategy_unavailable_label(std::string_view compact_text)
{
    return compact_text.find("未解锁") != std::string_view::npos ||
           compact_text.find("不可用") != std::string_view::npos ||
           compact_text.find("禁用") != std::string_view::npos ||
           compact_text.find("已被选择") != std::string_view::npos;
}

// Button color only rejects disabled controls. Identity and confirmation text
// must still be read from this same frame before issuing an input.
inline bool strategy_confirmation_enabled(const cv::Mat& button)
{
    if (button.empty() || button.type() != CV_8UC3 || button.cols != 210 || button.rows != 55) {
        return false;
    }
    int enabled_pixels = 0;
    for (int y = 0; y < button.rows; ++y) {
        for (int x = 0; x < button.cols; ++x) {
            const auto pixel = button.at<cv::Vec3b>(y, x);
            const int blue = pixel[0], green = pixel[1], red = pixel[2];
            enabled_pixels += green >= 100 && blue >= 70 && green - red >= 45 && blue - red >= 25;
        }
    }
    return enabled_pixels > button.rows * button.cols / 4;
}

inline bool strategy_confirmation_enabled_on_frame(const cv::Mat& frame)
{
    return frame.size() == cv::Size(1280, 720) && frame.type() == CV_8UC3 &&
           strategy_confirmation_enabled(frame(cv::Rect(990, 634, 210, 55)));
}

inline bool opening_strategy_tile_highlighted(const cv::Mat& frame, const cv::Rect& card)
{
    if (frame.size() != cv::Size(1280, 720) || frame.type() != CV_8UC3) {
        return false;
    }
    const auto cyan_border = [&](const cv::Rect& area, double proportion) {
        if ((area & cv::Rect(0, 0, frame.cols, frame.rows)) != area) {
            return false;
        }
        cv::Mat cyan;
        cv::inRange(frame(area), cv::Scalar(110, 160, 0), cv::Scalar(255, 255, 105), cyan);
        return cv::countNonZero(cyan) >= area.area() * proportion;
    };
    const bool left = cyan_border({ card.x, card.y + 18, 7, 70 }, .35);
    const bool top_strip = cyan_border({ card.x + 7, card.y, 30, 8 }, .32);
    const bool top_left = cyan_border({ card.x + 1, card.y, 20, 8 }, .32);
    const bool top_right = cyan_border({ card.x + card.width - 19, card.y, 18, 8 }, .32);
    // A medal can cover the top-right corner and the SELECTED label can
    // interrupt the top strip. A complete tile still has its lower-left L.
    const bool bottom_left = card.height >= 125 &&
                             cyan_border({ card.x + 1, card.br().y - 8, 20, 8 }, .32);
    return left && (top_strip || (top_left && (top_right || bottom_left)));
}
}
