#pragma once

#include "BlackFlowModel.h"
#include <opencv2/core/mat.hpp>

namespace asst::blackflow
{
// Reads category, stage title and explicit occupation from one current panel frame.
bool recognize_move_preview_identity(const cv::Mat& image, MovePreview& preview, std::string* error = nullptr);
}
