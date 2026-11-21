#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <meojson/json.hpp>

#include "Common/AsstBattleDef.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowHunterPreludeAnalyzer.h"

namespace Map
{
class Level;
}

namespace asst::blackflow
{
struct HunterPreludeDecision
{
    // A request, not confirmation that a retreat input succeeded.
    std::optional<Point> retreat_bait;
    bool main_allowed = false;
    json::object diagnostics;
};

// Owns only evidence and decisions. The battle actor owns all inputs and must
// report successful deployment, rather than an attempted drag, via deployed().
class BlackFlowHunterPreludeController
{
public:
    bool prepare(
        const std::filesystem::path& template_manifest,
        const std::filesystem::path& raw_level_path,
        const Map::Level& current_level,
        int bait_plan_index,
        std::string battle_key,
        std::string* error = nullptr);

    bool prepare(
        const std::filesystem::path& template_manifest,
        const std::filesystem::path& raw_level_path,
        const Map::Level& current_level,
        int bait_plan_index,
        std::string battle_key,
        int bait_attempt_limit,
        std::string* error = nullptr);

    [[nodiscard]] bool allows_plan(int plan_index) const noexcept;
    void deployed(int plan_index, const battle::OperNameTag& oper, Point tile);
    [[nodiscard]] HunterPreludeDecision observe(
        const cv::Mat& image,
        bool normal_battle,
        const std::map<battle::OperNameTag, Point>& battlefield,
        const std::vector<battle::DeploymentOper>& cards,
        std::uint64_t observed_ms);

    [[nodiscard]] bool prepared() const noexcept { return m_prepared; }

    [[nodiscard]] bool main_released() const noexcept { return m_main_released; }

private:
    BlackFlowHunterPreludeAnalyzer m_analyzer;
    bool m_prepared = false;
    bool m_spawn_window_verified = false;
    bool m_frame_verified = false;
    bool m_hunter_positive_verified = false;
    bool m_bait_consumed = false;
    bool m_bait_removal_verified = false;
    bool m_replacement_bait_allowed = false;
    bool m_retreat_requested = false;
    bool m_main_released = false;
    bool m_contract_breached = false;
    int m_bait_plan_index = -1;
    int m_bait_attempts = 0;
    int m_bait_attempt_limit = 1;
    std::string m_battle_key;
    std::optional<battle::OperNameTag> m_bait;
    std::string m_failure_reason;
    std::uint64_t m_sequence = 0;
    std::optional<std::uint64_t> m_last_observed_ms;
    std::vector<Point> m_tile_pixels;
    std::array<Rect, 2> m_spawn_rois;
    std::vector<Rect> m_approach_rois;
    Rect m_lower_roi;
    Rect m_battlefield_roi;
    json::object m_preparation_evidence;
};
}
