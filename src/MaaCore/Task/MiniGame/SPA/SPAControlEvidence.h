#pragma once

#include "MaaUtils/NoWarningCV.hpp"
#include <optional>

namespace asst::spa
{
// Reuse only the exact, unchanged shop-control pixels already read by OCR.
// This proves neither the preparation page nor any operator/transaction.
class ShopToggleEvidence
{
public:
    void clear()
    {
        m_expanded.reset();
        m_control.release();
    }

    std::optional<bool> observe_unchanged(const cv::Mat& image) const
    {
        if (!m_expanded || !valid_image(image) || m_control.empty() ||
            cv::norm(m_control, image(control_region()), cv::NORM_INF) != 0) {
            return std::nullopt;
        }
        return m_expanded;
    }

    void remember(const cv::Mat& image, bool expanded)
    {
        clear();
        if (valid_image(image)) {
            m_control = image(control_region()).clone();
            m_expanded = expanded;
        }
    }

private:
    static cv::Rect control_region() { return { 1080, 620, 200, 100 }; }

    static bool valid_image(const cv::Mat& image)
    {
        return image.size() == cv::Size(1280, 720) && image.type() == CV_8UC3;
    }

    std::optional<bool> m_expanded;
    cv::Mat m_control;
};
}
