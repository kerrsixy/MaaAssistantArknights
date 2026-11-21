#pragma once

#include <algorithm>
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
