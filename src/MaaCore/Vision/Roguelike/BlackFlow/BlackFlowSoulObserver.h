#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Common/AsstTypes.h"
#include "MaaUtils/NoWarningCVMat.hpp"

namespace asst::blackflow
{
struct SoulSite
{
    Point tile;
    Point pixel;
};

struct BossBodyEvidence
{
    double score = -1.0;
    bool positive_hp = false;
    Rect body;
    Rect hp_fill;

    bool alive() const noexcept { return positive_hp; }
};

// Appearance proposes a selection, never a retreat. The battle actor must
// independently verify the selected exact identity. Boss protection
// additionally requires fresh Boss evidence; deployment-site inspection has its
// own intent.
struct GoalLifeObservation
{
    std::optional<int> life;
    bool hud_verified = false;
    bool cache_hit = false;
    bool ocr_performed = false;
    std::uint64_t ocr_attempt = 0;
    std::string anchor_task;
    Rect number_roi;
    Rect digits_rect;
    std::string raw_digits;
    double score = 0.0;
    std::string reason;
};

class BlackFlowSoulObserver
{
public:
    bool load(const std::filesystem::path& directory, std::string* error = nullptr);
    BossBodyEvidence boss(const cv::Mat& image, bool selection_view = false) const;
    // A previous body rectangle proposes a search area only. This method still
    // requires the current frame's complete body template and associated HP.
    BossBodyEvidence boss_in_roi(const cv::Mat& image, Rect proposal) const;
    std::vector<Point> souls(const cv::Mat& normal_image, const std::vector<SoulSite>& legal_sites) const;
    bool paused(const cv::Mat& image) const;
    bool retreat_button(const cv::Mat& image, Point expected) const;
    bool normal_running(const cv::Mat& image) const;
    std::optional<std::string> battle_end_scene(const cv::Mat& image) const;
    // Existing frame only. ROI reuse is not a new capture or OCR observation.
    GoalLifeObservation goal_life(const cv::Mat& image);

private:
    BossBodyEvidence boss_impl(const cv::Mat& image, Rect search, bool selection_view) const;

    struct Profile
    {
        cv::Mat image;
        cv::Mat mask;
        cv::Mat coarse_image;
        cv::Mat coarse_mask;
        cv::Point2d anchor;
    };

    std::vector<Profile> m_boss;
    std::vector<Profile> m_selected_boss;
    std::vector<Profile> m_soul;
    double m_body_threshold = 0.45;
    double m_soul_threshold = 0.65;
    bool m_ready = false;
    cv::Mat m_pause_mask;
    cv::Mat m_retreat_mask;
    cv::Mat m_goal_life_roi;
    GoalLifeObservation m_goal_life_cached;
    std::uint64_t m_goal_life_ocr_attempts = 0;
};
} // namespace asst::blackflow
