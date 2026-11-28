#include "BlackFlowAccountListOrderAnalyzer.h"

#include "MaaUtils/NoWarningCV.hpp"
#include "Vision/OCRer.h"

std::optional<asst::blackflow::AccountListOrder> asst::blackflow::BlackFlowAccountListOrderAnalyzer::analyze() const
{
    if (m_image.empty() || m_image.type() != CV_8UC3 || m_image.cols != 1280 || m_image.rows != 720) {
        return std::nullopt;
    }
    OCRer label(m_image);
    label.set_task_info("BlackFlowAccountScan@SortLevel");
    const auto text = label.analyze();
    if (!text || text->size() != 1 || text->front().score < 0.9 || text->front().text != "等级") {
        return std::nullopt;
    }
    const auto& rect = text->front().rect;
    // The selected blue chevron sits directly below the selected sort label.
    // OCR may include part of the chevron in its text box. Anchor its horizontal
    // center to the recognized label, but keep the header's fixed arrow band.
    const cv::Rect roi(rect.x + rect.width / 2 - 20, 43, 40, 18);
    if ((roi & cv::Rect(0, 0, m_image.cols, m_image.rows)) != roi) {
        return std::nullopt;
    }
    cv::Mat hsv, mask;
    cv::cvtColor(m_image(roi), hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(85, 100, 100), cv::Scalar(115, 255, 255), mask);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    std::optional<AccountListOrder> result;
    for (const auto& contour : contours) {
        const auto bounds = cv::boundingRect(contour);
        if (bounds.width < 12 || bounds.width > 28 || bounds.height < 5 || bounds.height > 14) {
            continue;
        }
        if (result) {
            return std::nullopt;
        }
        const auto glyph = mask(bounds);
        const int half = glyph.rows / 2;
        std::vector<cv::Point> upper, lower;
        cv::findNonZero(glyph.rowRange(0, half), upper);
        cv::findNonZero(glyph.rowRange(glyph.rows - half, glyph.rows), lower);
        if (upper.empty() || lower.empty()) {
            return std::nullopt;
        }
        const int difference = cv::boundingRect(upper).width - cv::boundingRect(lower).width;
        if (std::abs(difference) < 4) {
            return std::nullopt;
        }
        result = difference > 0 ? AccountListOrder::Descending : AccountListOrder::Ascending;
    }
    return result;
}
