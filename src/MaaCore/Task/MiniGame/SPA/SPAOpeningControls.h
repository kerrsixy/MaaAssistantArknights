#pragma once

#include <algorithm>
#include <optional>
#include <string_view>

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
}
