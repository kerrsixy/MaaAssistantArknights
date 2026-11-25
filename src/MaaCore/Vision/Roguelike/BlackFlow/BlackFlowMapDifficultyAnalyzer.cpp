#include "BlackFlowMapDifficultyAnalyzer.h"

#include "Config/TaskData.h"
#include "Utils/StringMisc.hpp"
#include "Vision/OCRer.h"
#include <algorithm>
#include <cmath>

namespace asst::blackflow
{
MapDifficultyObservation observe_map_difficulty(
    const cv::Mat& image,
    bool current_map_verified,
    std::uint64_t frame_sequence,
    std::optional<int> prior_menu_level)
{
    MapDifficultyObservation result;
    result.prior_menu_level = prior_menu_level;
    result.frame_sequence = frame_sequence;
    result.current_map_verified = current_map_verified;
    const auto task = Task.get<OcrTaskInfo>("BlackFlow@Roguelike@MapDifficultyBadge");
    if (!current_map_verified || frame_sequence == 0 || image.empty() || image.type() != CV_8UC3 ||
        image.cols != 1280 || image.rows != 720 || !task || task->roi.x < 0 || task->roi.y < 0 ||
        task->roi.width <= 0 || task->roi.height <= 0 || task->roi.x + task->roi.width > image.cols ||
        task->roi.y + task->roi.height > image.rows) {
        result.reason = "current_map_frame_or_badge_resource_unknown";
        return result;
    }
    result.roi = task->roi;
    OCRer analyzer(image);
    analyzer.set_task_info(task);
    analyzer.set_use_char_model(true);
    const auto read = analyzer.analyze();
    int value = 0;
    if (!read || read->size() != 1 || !std::isfinite(read->front().score) || read->front().score < 0.9 ||
        read->front().text.empty() || read->front().text.size() > 2 ||
        !std::ranges::all_of(read->front().text, [](unsigned char c) { return c >= '0' && c <= '9'; }) ||
        !utils::chars_to_number(read->front().text, value) || value < 0 || value > 16) {
        result.reason = "current_map_badge_ocr_unknown";
        return result;
    }
    result.level = value;
    result.score = read->front().score;
    result.source_disagreement = prior_menu_level && *prior_menu_level != value;
    result.reason = result.source_disagreement ? "current_map_badge_disagrees_with_prior_menu"
                                               : "current_map_badge_positively_read";
    return result;
}

json::value MapDifficultyObservation::to_json() const
{
    return json::object {
        { "source", "current_normal_map_difficulty_badge" },
        { "frame_sequence", frame_sequence },
        { "current_map_verified", current_map_verified },
        { "level", level ? json::value(*level) : json::value(nullptr) },
        { "prior_menu_level", prior_menu_level ? json::value(*prior_menu_level) : json::value(nullptr) },
        { "source_disagreement", source_disagreement },
        { "score", score },
        { "roi", json::array { roi.x, roi.y, roi.width, roi.height } },
        { "reason", reason },
    };
}

std::string weather_floor_key(std::uint64_t run_revision, int floor)
{
    if (run_revision == 0 || floor < 1 || floor > 5) {
        return {};
    }
    return "BF-R" + std::to_string(run_revision) + "-F" + std::to_string(floor);
}
}
