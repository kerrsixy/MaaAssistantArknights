#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Common/AsstTypes.h"
#include "MaaUtils/NoWarningCVMat.hpp"

namespace asst::blackflow
{
enum class HunterPresence
{
    Unknown,
    Present,
    Absent,
};

struct HunterSpawnObservation
{
    HunterPresence presence = HunterPresence::Unknown;
    double present_score = -1.0;
    double absent_score = -1.0;
    int distinct_empty_frames = 0;
    Rect roi;
};

struct HunterPreludeFrameContext
{
    std::string battle_key;
    std::uint64_t frame_sequence = 0;
    bool normal_battle_confirmed = false;
    bool stage_geometry_verified = false;
    // Caller evidence: raw stage spawn actions contain no later hunters; bait has retreated,
    // and the entire lower search field is visible without allied sprite or UI occlusion.
    bool spawn_window_closed_verified = false;
    bool lower_visibility_verified = false;
    bool battlefield_visibility_verified = false;
    // A withdrawal probe checks only the calibrated spawn patches. It cannot
    // certify lower-field absence, whole-field coverage or hunter clearance.
    bool spawn_only_probe = false;
    std::uint64_t observed_at_ms = 0;
    // Native pixel rectangles projected from spawn tiles. Empty uses the explicit reference geometry.
    std::vector<Rect> spawn_rois;
    Rect lower_search_roi;
    Rect battlefield_search_roi;
    std::vector<Rect> occlusion_rois;
    std::vector<Rect> sink_head_rois;
    // Allied bait sprites may hide heads locally. Positive evidence elsewhere can permit
    // retreat, but any such mask prevents whole-lower-field absence certification.
    std::vector<Rect> lower_unit_occlusion_rois;
    std::vector<Rect> battlefield_unit_occlusion_rois;
};

struct HunterPreludeObservation
{
    bool frame_observable = false;
    std::array<HunterSpawnObservation, 2> spawns;
    HunterPresence lower_presence = HunterPresence::Unknown;
    HunterPresence battlefield_presence = HunterPresence::Unknown;
    double battlefield_best_candidate_score = -1.0;
    Rect battlefield_best_candidate_rect;
    std::vector<Rect> battlefield_head_candidates;
    double lower_best_candidate_score = -1.0;
    Rect lower_best_candidate_rect;
    int distinct_empty_spawn_frames = 0;
    bool upper_spawns_empty_candidate = false;
    bool bait_retreat_allowed = false;
    std::vector<Rect> lower_head_candidates;
    // Track segments may split after uncertainty; this is never a hunter or kill count.
    int observed_tracks = 0;
    // Legacy result alias is approach evidence only, never a fall or kill count.
    int tracks_last_seen_at_sink = 0;
    int tracks_last_seen_at_approach = 0;
    int distinct_empty_lower_frames = 0;
    // Requires complete upper-plus-lower visibility and timed absence. Approach-zone
    // history is an additional conservative check, not an independent death certificate.
    bool full_search_coverage_verified = false;
    bool all_hunters_cleared = false;
    std::vector<std::string> reasons;
};

// Pure visual evidence for the opening hunters. Does not deploy, pause, navigate or count kills.
class BlackFlowHunterPreludeAnalyzer
{
public:
    bool load(const std::filesystem::path& manifest_path, std::string* error = nullptr);
    [[nodiscard]] HunterPreludeObservation analyze(const cv::Mat& image, const HunterPreludeFrameContext& context);
    void reset() noexcept;

    [[nodiscard]] bool loaded() const noexcept { return m_loaded; }

private:
    struct SpawnModel
    {
        Rect roi;
        std::vector<cv::Mat> present;
        std::vector<cv::Mat> absent;
    };

    struct Anchor
    {
        Rect roi;
        cv::Mat image;
        double min_correlation = 0.9;
        double min_luminance_ratio = 0.72;
    };

    struct Track
    {
        Rect last_head;
        bool last_seen_at_approach = false;
        int missing_frames = 0;
    };

    struct HeadProfile
    {
        std::string id;
        std::string domain;
        cv::Mat image;
        cv::Mat mask;
        double present_threshold = 0.68;
    };

    bool m_loaded = false;
    std::array<SpawnModel, 2> m_spawns;
    std::vector<Anchor> m_anchors;
    std::vector<HeadProfile> m_head_profiles;
    std::vector<Rect> m_approach_rois;
    std::vector<Track> m_tracks;
    double m_head_present_threshold = 0.68;
    double m_head_absent_threshold = 0.50;
    int m_min_clear_frames = 3;
    std::uint64_t m_min_clear_duration_ms = 600;
    int m_empty_lower_frames = 0;
    std::uint64_t m_empty_lower_start_ms = 0;
    std::uint64_t m_last_observed_at_ms = 0;
    Rect m_lower_roi;
    Rect m_battlefield_roi;
    std::string m_battle_key;
    std::uint64_t m_last_sequence = 0;
    bool m_has_sequence = false;
    cv::Mat m_last_observable_frame;
    int m_empty_spawn_frames = 0;
    std::array<int, 2> m_empty_spawn_frames_by_slot {};
};
}
