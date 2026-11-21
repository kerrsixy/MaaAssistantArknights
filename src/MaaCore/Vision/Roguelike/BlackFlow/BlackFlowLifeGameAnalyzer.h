#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Task/Roguelike/BlackFlow/BlackFlowLifeGamePlanner.h"
#include "Vision/VisionHelper.h"

namespace asst::blackflow
{
enum class LifeGameSelectedState
{
    Unknown,
    Active,
    Unpurchased,
};

struct LifeGameScreenObservation
{
    bool page_verified = false;
    bool collection_complete = false;
    std::optional<int> points;
    std::optional<int> collected;
    std::optional<int> total;
    std::string selected_name;
    std::string selected_effect;
    LifeGameSelectedState selected_state = LifeGameSelectedState::Unknown;
    std::optional<int> selected_cost;
    std::optional<Rect> selected_rect;
    // Geometric selection candidates only. Brightness does not prove purchased state or identity.
    std::vector<Rect> selection_candidates;
};

class BlackFlowLifeGameAnalyzer final : public VisionHelper
{
public:
    using VisionHelper::VisionHelper;
    [[nodiscard]] LifeGameScreenObservation analyze(const std::vector<LifeGameNode>& nodes, bool details = true) const;

    [[nodiscard]] static std::optional<int> parse_number(std::string_view text);
    [[nodiscard]] static std::optional<std::pair<int, int>> parse_collected(std::string_view text);
    [[nodiscard]] static std::vector<Rect> selection_candidates(const cv::Mat& image, const Rect& area);
    [[nodiscard]] static std::optional<Rect>
        selected_candidate(const cv::Mat& image, const std::vector<Rect>& candidates);
    [[nodiscard]] static std::vector<std::string>
        selected_node_ids(const LifeGameScreenObservation& screen, const std::vector<LifeGameNode>& nodes);
    [[nodiscard]] static bool verifies_upgrade(
        const LifeGameScreenObservation& before,
        const LifeGameScreenObservation& after,
        const LifeGameNode& node);
    [[nodiscard]] static bool verifies_terminal_upgrade(
        const LifeGameScreenObservation& before,
        const LifeGameScreenObservation& after,
        const LifeGameNode& node,
        int expected_total);
};
}
