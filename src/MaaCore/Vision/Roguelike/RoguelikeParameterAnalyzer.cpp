#include "RoguelikeParameterAnalyzer.h"

#include <cmath>

#include "Controller/Controller.h"
#include "MaaUtils/ImageIo.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Status.h"
#include "Task/ProcessTask.h"
#include "Utils/Logger.hpp"
#include "Utils/StringMisc.hpp"
#include "Vision/Matcher.h"
#include "Vision/Miscellaneous/PipelineAnalyzer.h"

/* bool asst::RoguelikeParameterAnalyzer::analyze()
{
    LogTraceFunction;
    return true;
}*/

int asst::RoguelikeParameterAnalyzer::update_chaos(const cv::Mat& image)
{
    return get_number(image, "Sami@Roguelike@SpecialValRecognition");
}

int asst::RoguelikeParameterAnalyzer::update_idea_count(const cv::Mat& image)
{
    return get_number(image, "Sarkaz@Roguelike@SpecialValRecognition");
}

std::optional<int> asst::RoguelikeParameterAnalyzer::read_number(
    const cv::Mat& image,
    const std::string& task_name,
    double minimum_score)
{
    const auto task = Task.get<OcrTaskInfo>(task_name);
    if (image.empty() || task == nullptr || minimum_score < 0 || minimum_score > 1) {
        return std::nullopt;
    }
    PipelineAnalyzer analyzer(image);
    analyzer.set_tasks({ task_name });
    const auto result = analyzer.analyze();
    if (!result.has_value()) {
        return std::nullopt;
    }
    const auto* ocr = std::get_if<OCRer::Result>(&result->result);
    int value = 0;
    if (ocr == nullptr || ocr->score < minimum_score || !utils::chars_to_number(ocr->text, value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<int> asst::RoguelikeParameterAnalyzer::read_blackflow_hope(const cv::Mat& image)
{
    if (image.empty() || image.type() != CV_8UC3 || image.cols != 1280 || image.rows != 720) {
        return std::nullopt;
    }
    Matcher header(image);
    header.set_task_info("BlackFlow@Roguelike@HopeHeader");
    const auto task = Task.get<OcrTaskInfo>("BlackFlow@Roguelike@CurrentHope");
    if (!task || !header.analyze()) {
        return std::nullopt;
    }
    const cv::Rect region(task->roi.x, task->roi.y, task->roi.width, task->roi.height);
    if (region.empty() || (region & cv::Rect(0, 0, image.cols, image.rows)) != region) {
        return std::nullopt;
    }
    cv::Mat hsv, mask, labels, stats, centers;
    cv::cvtColor(image(region), hsv, cv::COLOR_BGR2HSV);
    // The yellow current value moves with the fill bar. Gray maximum digits and
    // the shorter triangular separators must never be concatenated into this
    // budget.
    cv::inRange(hsv, cv::Scalar(15, 120, 160), cv::Scalar(45, 255, 255), mask);
    const int count = cv::connectedComponentsWithStats(mask, labels, stats, centers);
    cv::Mat digits = cv::Mat::zeros(mask.size(), CV_8UC1);
    int glyphs = 0;
    int baseline = -1;
    for (int label = 1; label < count; ++label) {
        const int height = stats.at<int>(label, cv::CC_STAT_HEIGHT);
        if (height < 14 || height > 24) {
            continue;
        }
        const int left = stats.at<int>(label, cv::CC_STAT_LEFT);
        const int width = stats.at<int>(label, cv::CC_STAT_WIDTH);
        const int bottom = stats.at<int>(label, cv::CC_STAT_TOP) + height;
        if (left == 0 || left + width == mask.cols || width > 18 ||
            (baseline >= 0 && std::abs(bottom - baseline) > 2)) {
            return std::nullopt;
        }
        baseline = bottom;
        digits.setTo(255, labels == label);
        ++glyphs;
    }
    if (glyphs == 0 || glyphs > 3) {
        return std::nullopt;
    }
    cv::Mat padded, bgr;
    cv::copyMakeBorder(digits(cv::boundingRect(digits)), padded, 3, 3, 3, 3, cv::BORDER_CONSTANT, cv::Scalar(0));
    cv::cvtColor(padded, bgr, cv::COLOR_GRAY2BGR);
    const auto results = CharOcr::get_instance().recognize(bgr, true);
    int value = 0;
    if (results.size() != 1 || !std::isfinite(results.front().score) || results.front().score < 0.9 ||
        results.front().text.size() != static_cast<std::size_t>(glyphs) ||
        !utils::chars_to_number(results.front().text, value)) {
        return std::nullopt;
    }
    return value;
}

int asst::RoguelikeParameterAnalyzer::get_number(const cv::Mat& image, const std::string& task_name)
{
    int val = 0;
    OCRer analyzer(image);
    analyzer.set_task_info(task_name);
    analyzer.set_replace(Task.get<OcrTaskInfo>("NumberOcrReplace")->replace_map);
    analyzer.set_use_char_model(true);

    if (!analyzer.analyze()) {
        return 0;
    }
    utils::chars_to_number(analyzer.get_result().front().text, val);

    return val;
}

int asst::RoguelikeParameterAnalyzer::update_hp(const cv::Mat& image)
{
    int hp_val;
    asst::OCRer analyzer(image);
    analyzer.set_task_info("Roguelike@HpRecognition");

    auto res_vec_opt = analyzer.analyze();
    if (!res_vec_opt) {
        return -1;
    }
    return utils::chars_to_number(res_vec_opt->front().text, hp_val) ? hp_val : 0;
}
