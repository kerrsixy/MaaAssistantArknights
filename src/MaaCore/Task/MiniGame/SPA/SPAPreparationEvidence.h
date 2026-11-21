#pragma once

#include "MaaUtils/NoWarningCV.hpp"
#include <array>

namespace asst::spa
{
// Remember verified round/counter/Ready facts. Budget reuse additionally
// requires a recognized digit; the legacy zero-budget fallback is not proof.
// This does not establish identity, terrain, facing or transaction success.
class PreparationHudEvidence
{
public:
    void clear()
    {
        m_valid = false;
        m_counter_label_y = -1;
        m_funds_verified = false;
        for (auto& image : m_regions) {
            image.release();
        }
    }

    bool matches(const cv::Mat& image, int round, int funds, int remaining) const
    {
        for (size_t index = 0; index < m_regions.size(); ++index) {
            if (!matches_region(index, image, round, funds, remaining)) {
                return false;
            }
        }
        return true;
    }

    // A changed/animated control must not invalidate unrelated unchanged
    // digits. Every reused region still matches the current frame exactly.
    bool matches_region(size_t index, const cv::Mat& image, int round, int funds, int remaining) const
    {
        if (!m_valid || index >= m_regions.size() || m_round != round || m_funds != funds || m_remaining != remaining ||
            !valid_image(image) || (index == 1 && !m_funds_verified)) {
            return false;
        }
        return cv::norm(m_regions[index], image(regions()[index]), cv::NORM_INF) == 0;
    }

    bool remember_verified(
        const cv::Mat& image,
        int round,
        int funds,
        int remaining,
        int counter_label_y = -1,
        bool funds_verified = true)
    {
        const int label_y = counter_label_y >= 0 ? counter_label_y
                            : m_valid && m_round == round && m_funds == funds && m_remaining == remaining
                                ? m_counter_label_y
                                : -1;
        clear();
        if (!valid_image(image) || round < 1 || round > 20 || funds < 0 || funds > 999 || remaining < 0 ||
            remaining > 8 || (label_y != -1 && (label_y < 350 || label_y >= 510))) {
            return false;
        }
        m_counter_label_y = label_y;
        m_funds_verified = funds_verified;
        const auto bounds = regions();
        for (size_t index = 0; index < bounds.size(); ++index) {
            m_regions[index] = image(bounds[index]).clone();
        }
        m_round = round;
        m_funds = funds;
        m_remaining = remaining;
        m_valid = true;
        return true;
    }

private:
    static bool valid_image(const cv::Mat& image)
    {
        return image.size() == cv::Size(1280, 720) && image.type() == CV_8UC3;
    }

    std::array<cv::Rect, 4> regions() const
    {
        return { cv::Rect(480, 8, 50, 40),
                 cv::Rect(1180, 510, 45, 40),
                 cv::Rect(1100, 70, 180, 85),
                 m_counter_label_y >= 350 ? cv::Rect(1090, m_counter_label_y - 2, 190, 34)
                                          : cv::Rect(1090, 350, 190, 165) };
    }

    bool m_valid = false;
    bool m_funds_verified = false;
    int m_round = -1;
    int m_funds = -1;
    int m_remaining = -1;
    int m_counter_label_y = -1;
    std::array<cv::Mat, 4> m_regions;
};
}
