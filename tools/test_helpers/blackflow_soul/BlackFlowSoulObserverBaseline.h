#pragma once
#include "Vision/Roguelike/BlackFlow/BlackFlowSoulObserver.h"

namespace asst::blackflow
{
class BlackFlowSoulObserverBaseline
{
public:
    bool load(const std::filesystem::path& directory, std::string* error = nullptr);
    BossBodyEvidence boss(const cv::Mat& image, bool selection_view = false) const;
    std::vector<Point> souls(const cv::Mat& normal_image, const std::vector<SoulSite>& legal_sites) const;
    bool paused(const cv::Mat& image) const;
    bool retreat_button(const cv::Mat& image, Point expected) const;
    bool normal_running(const cv::Mat& image) const;
    std::optional<std::string> battle_end_scene(const cv::Mat& image) const;

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
} // namespace asst::blackflow
