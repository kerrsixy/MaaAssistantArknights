#pragma once

#include "MaaUtils/NoWarningCV.hpp"
#include <opencv2/core.hpp>

namespace asst::spa
{
inline bool is_blank_formation_frame(const cv::Mat& frame)
{
    return frame.size() == cv::Size(1280, 720) && frame.type() == CV_8UC3 && cv::norm(frame, cv::NORM_INF) <= 3;
}
}
