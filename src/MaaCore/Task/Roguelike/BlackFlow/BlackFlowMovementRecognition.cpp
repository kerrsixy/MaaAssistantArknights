#include "BlackFlowMovementRecognition.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <string_view>

#include "Config/Roguelike/BlackFlow/BlackFlowScrapMarketConfig.h"
#include "Config/TaskData.h"
#include "Utils/Logger.hpp"
#include "Vision/Matcher.h"
#include "Vision/OCRer.h"

namespace asst::blackflow
{
std::optional<MovementKind> recognize_loaded_movement(const cv::Mat& image)
{
    struct LoadedTask
    {
        MovementKind movement;
        std::string_view task;
    };

    static constexpr std::array LoadedTasks = {
        LoadedTask { MovementKind::Walk, "BlackFlow@Roguelike@MovementLoaded-Walk" },
        LoadedTask { MovementKind::M01, "BlackFlow@Roguelike@MovementLoaded-M01" },
        LoadedTask { MovementKind::M02, "BlackFlow@Roguelike@MovementLoaded-M02" },
        LoadedTask { MovementKind::M03, "BlackFlow@Roguelike@MovementLoaded-M03" },
        // M04（重弹簧）、M12（简易遥控器）缺少 Movement 模板与 MovementLoaded 任务
        // LoadedTask { MovementKind::M04, "BlackFlow@Roguelike@MovementLoaded-M04" },
        LoadedTask { MovementKind::M05, "BlackFlow@Roguelike@MovementLoaded-M05" },
        LoadedTask { MovementKind::M06, "BlackFlow@Roguelike@MovementLoaded-M06" },
        LoadedTask { MovementKind::M07, "BlackFlow@Roguelike@MovementLoaded-M07" },
        LoadedTask { MovementKind::M08, "BlackFlow@Roguelike@MovementLoaded-M08" },
        LoadedTask { MovementKind::M09, "BlackFlow@Roguelike@MovementLoaded-M09" },
        LoadedTask { MovementKind::M10, "BlackFlow@Roguelike@MovementLoaded-M10" },
        LoadedTask { MovementKind::M11, "BlackFlow@Roguelike@MovementLoaded-M11" },
        // LoadedTask { MovementKind::M12, "BlackFlow@Roguelike@MovementLoaded-M12" },
        LoadedTask { MovementKind::M13, "BlackFlow@Roguelike@MovementLoaded-M13" },
        LoadedTask { MovementKind::M14, "BlackFlow@Roguelike@MovementLoaded-M14" },
        LoadedTask { MovementKind::M15, "BlackFlow@Roguelike@MovementLoaded-M15" },
        LoadedTask { MovementKind::M16, "BlackFlow@Roguelike@MovementLoaded-M16" },
    };

    std::optional<MovementKind> best;
    double best_score = 0.0;
    for (const LoadedTask& loaded : LoadedTasks) {
        Matcher matcher(image);
        matcher.set_task_info(std::string(loaded.task));
        const auto result = matcher.analyze();
        if (result.has_value() && result->score > best_score) {
            best = loaded.movement;
            best_score = result->score;
        }
    }
    return best;
}

const MovementSpec* movement_from_name(std::string_view name) noexcept
{
    const auto found = std::ranges::find(movement_specs(), name, &MovementSpec::name);
    return found == movement_specs().end() ? nullptr : &*found;
}

std::vector<std::string> movement_inventory_ocr_candidates()
{
    std::vector<std::string> candidates { "装载中", "待收集零件" };
    for (const auto& item : BlackFlowScrapMarket.items()) {
        candidates.emplace_back(item.name);
    }
    for (const auto& spec : movement_specs()) {
        if (spec.kind != MovementKind::Walk && std::ranges::find(candidates, spec.name) == candidates.end()) {
            candidates.emplace_back(spec.name);
        }
    }
    return candidates;
}

MovementInventoryFrameStatus
    analyze_movement_inventory_frame(const cv::Mat& image, MovementInventoryFrame& frame, std::string* error)
{
    frame = MovementInventoryFrame {};
    const auto task = Task.get<OcrTaskInfo>("BlackFlow@Roguelike@MovementInventoryItems");
    if (image.empty() || task == nullptr) {
        if (error != nullptr) {
            *error =
                image.empty() ? "movement inventory screenshot is empty" : "movement inventory OCR task is missing";
        }
        return MovementInventoryFrameStatus::Failed;
    }
    OCRer analyzer(image);
    analyzer.set_task_info(task);
    analyzer.set_required(movement_inventory_ocr_candidates());
    const auto results = analyzer.analyze();
    if (!results) {
        return MovementInventoryFrameStatus::NoCandidate;
    }
    std::vector<Rect> loaded_markers;
    for (const auto& result : *results) {
        if (result.text == "装载中") {
            loaded_markers.emplace_back(result.rect);
            continue;
        }
        frame.labels.emplace_back(MovementInventoryLabel { result.text, result.rect });
        if (const auto* movement = movement_from_name(result.text);
            movement != nullptr && movement->kind != MovementKind::Walk) {
            ++frame.movements[movement->kind];
            frame.items.emplace_back(MovementInventoryItem { movement->kind, result.rect });
        }
    }
    int best_gap = std::numeric_limits<int>::max();
    for (const auto& marker : loaded_markers) {
        for (const auto& item : frame.items) {
            if (std::abs(item.name_rect.y + item.name_rect.height / 2 - marker.y - marker.height / 2) > 32) {
                continue;
            }
            const int gap = marker.x - item.name_rect.x - item.name_rect.width;
            if (gap >= 0 && gap < best_gap) {
                frame.loaded_movement = item.movement;
                best_gap = gap;
            }
        }
    }
    if (!loaded_markers.empty() && !frame.loaded_movement) {
        frame.has_unmatched_loaded_marker = true;
        LogDebug << "BlackFlow inventory loaded marker is clipped; retaining unknown movement identity";
    }
    return frame.labels.empty() ? MovementInventoryFrameStatus::NoCandidate : MovementInventoryFrameStatus::Recognized;
}

bool same_movement_inventory_page(
    const MovementInventoryFrame& first,
    const MovementInventoryFrame& second,
    int tolerance)
{
    if (first.labels.empty() || first.labels.size() != second.labels.size()) {
        return false;
    }
    std::vector<bool> used(second.labels.size());
    for (const auto& label : first.labels) {
        bool matched = false;
        for (std::size_t i = 0; i < second.labels.size(); ++i) {
            const auto& other = second.labels[i];
            if (!used[i] && label.name == other.name && std::abs(label.rect.x - other.rect.x) <= tolerance &&
                std::abs(label.rect.y - other.rect.y) <= tolerance) {
                used[i] = true;
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
    }
    return true;
}
} // namespace asst::blackflow
