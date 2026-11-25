#pragma once

#include "Common/BlackFlowPauseTransition.h"
#include "MaaUtils/NoWarningCVMat.hpp"

namespace asst::blackflow
{
PauseTransitionFrame analyze_pause_transition_frame(
    const cv::Mat& image,
    std::int64_t captured_ms,
    bool paused,
    bool official_running,
    bool normal_running);
}
