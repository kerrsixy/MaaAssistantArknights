#pragma once

#include "MaaUtils/NoWarningCV.hpp"

namespace asst::spa
{
// A recognized title proves only that a selection panel is present. Reuse it
// once, and only when a fresh frame has exactly the same title-region pixels.
// This never proves identity, placement, facing, or a transaction after input.
class SelectedPanelEvidence
{
public:
    void clear() { m_title.release(); }

    void remember_recognized_title(const cv::Mat& image)
    {
        clear();
        if (valid_image(image)) {
            m_title = image(title_region()).clone();
        }
    }

    bool consume_unchanged(const cv::Mat& image)
    {
        const bool matched =
            !m_title.empty() && valid_image(image) && cv::norm(m_title, image(title_region()), cv::NORM_INF) == 0;
        // A mismatch also spends the evidence. A later frame returning to the
        // old pixels must not revive an observation from a prior selection.
        clear();
        return matched;
    }

private:
    static cv::Rect title_region() { return { 0, 165, 350, 75 }; }

    static bool valid_image(const cv::Mat& image)
    {
        return image.size() == cv::Size(1280, 720) && image.type() == CV_8UC3;
    }

    cv::Mat m_title;
};
}
