#pragma once

#include "Common/AsstTypes.h"
#include "MaaUtils/NoWarningCVMat.hpp"
#include <cstdint>
#include <meojson/json.hpp>
#include <optional>
#include <string>

namespace asst::blackflow
{
struct MapDifficultyObservation
{
    std::optional<int> level;
    std::optional<int> prior_menu_level;
    std::uint64_t frame_sequence = 0;
    bool current_map_verified = false;
    bool source_disagreement = false;
    double score = 0;
    Rect roi;
    std::string reason;
    [[nodiscard]] json::value to_json() const;
};

// The caller must have positively recognized this exact map frame. Requested
// difficulty is intentionally absent from this interface. Older menu readings
// remain diagnostic and never substitute for a missing current-map badge.
[[nodiscard]] MapDifficultyObservation observe_map_difficulty(
    const cv::Mat& image,
    bool current_map_verified,
    std::uint64_t frame_sequence,
    std::optional<int> prior_menu_level);

// A key is meaningful only inside the owning map observer lifetime. Distinct
// Session revisions prevent a later same-number floor borrowing temporal proof.
[[nodiscard]] std::string weather_floor_key(std::uint64_t run_revision, int floor);
}
