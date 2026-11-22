#pragma once

#include <filesystem>
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
// independently verify the selected unit's exact name and fresh Boss evidence.
class BlackFlowSoulObserver
{
public:
    bool load(const std::filesystem::path& directory, std::string* error = nullptr);
    BossBodyEvidence boss(const cv::Mat& image, bool selection_view = false) const;
    std::vector<Point> souls(const cv::Mat& normal_image, const std::vector<SoulSite>& legal_sites) const;
    bool paused(const cv::Mat& image) const;
    bool retreat_button(const cv::Mat& image, Point expected) const;

private:
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
};
}
