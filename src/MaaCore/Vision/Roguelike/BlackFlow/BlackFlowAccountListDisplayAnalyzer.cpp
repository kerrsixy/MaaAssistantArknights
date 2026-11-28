#include "BlackFlowAccountListDisplayAnalyzer.h"

#include "MaaUtils/NoWarningCV.hpp"
#include "Vision/OCRer.h"

std::optional<asst::blackflow::AccountListDisplayObservation>
    asst::blackflow::BlackFlowAccountListDisplayAnalyzer::analyze() const
{
    if (m_image.empty() || m_image.type() != CV_8UC3 || m_image.cols != 1280 || m_image.rows != 720) {
        return std::nullopt;
    }
    OCRer label(m_image);
    label.set_task_info("BlackFlowAccountScan@BatchEditLabel");
    const auto text = label.analyze();
    const bool batch_label =
        text && text->size() == 1 && text->front().score >= 0.9 && text->front().text == "批量编辑";
    // Same-page native on/off fixtures anchor this unique rook glyph band.
    const cv::Rect band(744, 18, 50, 43);
    cv::Mat hsv, white, yellow;
    cv::cvtColor(m_image(band), hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(0, 0, 180), cv::Scalar(180, 60, 255), white);
    cv::inRange(hsv, cv::Scalar(18, 100, 120), cv::Scalar(45, 255, 255), yellow);
    const auto white_count = cv::countNonZero(white);
    const auto yellow_count = cv::countNonZero(yellow);
    const bool disabled = white_count >= 140 && white_count <= 260 && yellow_count <= 10 && !batch_label;
    const bool enabled = yellow_count >= 180 && yellow_count <= 320 && white_count <= 10 && batch_label;
    if (!disabled && !enabled) {
        return std::nullopt;
    }
    std::vector<cv::Point> glyph;
    cv::findNonZero(disabled ? white : yellow, glyph);
    const auto bounds = cv::boundingRect(glyph);
    if (bounds.width < 12 || bounds.width > 36 || bounds.height < 20 || bounds.height > 28) {
        return std::nullopt;
    }
    return AccountListDisplayObservation {
        disabled ? AccountListBatchEdit::Disabled : AccountListBatchEdit::Enabled,
        { band.x + bounds.x, band.y + bounds.y, bounds.width, bounds.height },
    };
}
