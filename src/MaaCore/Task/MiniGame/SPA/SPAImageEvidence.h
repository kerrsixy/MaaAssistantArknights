#pragma once
#include "Utils/DebugImageHelper.hpp"

namespace asst::spa
{
inline bool save_evidence(const cv::Mat& image, const std::filesystem::path& relative_dir, bool auto_clean = false)
{
    // save_debug_image only creates directories through its optional automatic cleanup path.
    const auto directory = relative_dir.is_absolute() ? relative_dir : UserDir.get() / relative_dir;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        LogError << "SPA evidence directory could not be created:" << error.message();
        return false;
    }
    const bool saved = utils::save_debug_image(image, directory, auto_clean);
    if (!saved) {
        LogError << "SPA evidence image could not be saved";
    }
    return saved;
}
}
