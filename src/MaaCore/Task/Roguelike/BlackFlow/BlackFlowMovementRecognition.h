#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "BlackFlowModel.h"
#include "Common/AsstTypes.h"

namespace cv
{
class Mat;
}

namespace asst::blackflow
{
[[nodiscard]] std::optional<MovementKind> recognize_loaded_movement(const cv::Mat& image);

struct MovementInventoryItem
{
    MovementKind movement = MovementKind::Walk;
    Rect name_rect;
};

struct MovementInventoryLabel
{
    std::string name;
    Rect rect;
};

struct MovementInventoryFrame
{
    std::unordered_map<MovementKind, int> movements;
    std::vector<MovementInventoryItem> items;
    // 非代步零件与空槽也提供有效页面证据，并用于核验横向滚动的边界。
    std::vector<MovementInventoryLabel> labels;
    std::optional<MovementKind> loaded_movement;
    bool has_unmatched_loaded_marker = false;
    bool complete = false;
};

enum class MovementInventoryFrameStatus
{
    Recognized,
    NoCandidate,
    Failed,
};

[[nodiscard]] const MovementSpec* movement_from_name(std::string_view name) noexcept;
[[nodiscard]] std::vector<std::string> movement_inventory_ocr_candidates();
[[nodiscard]] MovementInventoryFrameStatus
    analyze_movement_inventory_frame(const cv::Mat& image, MovementInventoryFrame& frame, std::string* error);
[[nodiscard]] bool same_movement_inventory_page(
    const MovementInventoryFrame& first,
    const MovementInventoryFrame& second,
    int tolerance = 20);
}
