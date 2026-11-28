#include "BlackFlowModulePanelAnalyzer.h"

#include "Config/TaskData.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Utils/Logger.hpp"
#include "Vision/OCRer.h"
#include <algorithm>

asst::blackflow::BlackFlowModulePanelAnalyzer::ResultOpt
    asst::blackflow::BlackFlowModulePanelAnalyzer::analyze(const ModulePanelTarget& target) const
{
    if (m_image.empty() || target.module_id.empty() || target.display_title.empty() || target.max_level < 1 ||
        target.max_level > 3) {
        return std::nullopt;
    }
    const auto title_task = Task.get<OcrTaskInfo>("BlackFlowAccount@ModulePanelTitle");
    const auto state_task = Task.get<OcrTaskInfo>("BlackFlowAccount@ModulePanelState");
    if (!title_task || !state_task) {
        LogWarn << __FUNCTION__ << "Module panel recognition resources are unavailable";
        return std::nullopt;
    }
    const auto inside_image = [&](const Rect& rect) {
        return rect.x >= 0 && rect.y >= 0 && rect.width > 0 && rect.height > 0 && rect.x + rect.width <= m_image.cols &&
               rect.y + rect.height <= m_image.rows;
    };
    if (!inside_image(title_task->roi)) {
        return std::nullopt;
    }
    OCRer title(m_image);
    title.set_task_info(title_task);
    title.set_required({ target.display_title });
    const auto titles = title.analyze();
    if (!titles || titles->size() != 1 || titles->front().score < 0.95) {
        return std::nullopt;
    }
    const auto& anchor = titles->front();
    const Rect state_roi = anchor.rect.move(state_task->rect_move);
    if (!inside_image(state_roi)) {
        return std::nullopt;
    }
    OCRer state(m_image);
    state.set_task_info(state_task);
    state.set_roi(state_roi);
    const auto states = state.analyze();
    if (!states || states->size() != 1 || states->front().score < 0.95) {
        return std::nullopt;
    }
    // Resource order defines the two unambiguous terminal states: MAX, then locked.
    if (state_task->text.size() != 2) {
        return std::nullopt;
    }
    const auto& status = states->front();
    const bool at_maximum = status.text == state_task->text.front();
    const bool locked = status.text == state_task->text.back();
    if (!at_maximum && !locked) {
        return std::nullopt;
    }
    return ModulePanelObservation {
        .evidence = { .id = target.module_id,
                      .level = at_maximum ? target.max_level : 0,
                      .unlocked = at_maximum,
                      .verified = true },
        .title_rect = anchor.rect,
        .state_rect = status.rect,
        .state_text = status.text,
    };
}

std::optional<asst::Rect>
    asst::blackflow::BlackFlowModulePanelAnalyzer::set_default_button(const ModulePanelTarget& target) const
{
    const auto card = analyze(target);
    if (!card || !card->evidence.unlocked || card->evidence.level != target.max_level) {
        return std::nullopt;
    }
    const Rect region { 1206, std::max(0, card->title_rect.y - 65), 74, 180 };
    if (region.y + region.height > m_image.rows) {
        return std::nullopt;
    }
    OCRer reader(m_image);
    reader.set_task_info("BlackFlowAccountScan@SetDefaultWords");
    // The narrow vertical crop can fuse the two lines into one bad token.
    // Detect on the complete panel, then require both exact words on this card.
    reader.set_roi(Rect { 780, 80, 500, 640 });
    reader.set_required({ "设为", "默认" });
    auto text = reader.analyze();
    if (text) {
        std::erase_if(*text, [&](const auto& word) {
            return word.rect.x < region.x || word.rect.y < region.y ||
                   word.rect.x + word.rect.width > region.x + region.width ||
                   word.rect.y + word.rect.height > region.y + region.height;
        });
    }
    if (!text || text->size() != 2) {
        return std::nullopt;
    }
    std::ranges::sort(*text, {}, [](const auto& value) { return value.rect.y; });
    const auto& upper = text->at(0);
    const auto& lower = text->at(1);
    if (upper.text != "设为" || lower.text != "默认" || upper.score < 0.95 || lower.score < 0.95 ||
        std::abs(upper.rect.x - lower.rect.x) > 8 || lower.rect.y < upper.rect.y + upper.rect.height - 4 ||
        lower.rect.y > upper.rect.y + upper.rect.height + 12) {
        return std::nullopt;
    }
    const int left = std::min(upper.rect.x, lower.rect.x);
    const int right = std::max(upper.rect.x + upper.rect.width, lower.rect.x + lower.rect.width);
    const Rect button { left, upper.rect.y, right - left, lower.rect.y + lower.rect.height - upper.rect.y };
    if (button.x < region.x || button.y < region.y || button.x + button.width > m_image.cols ||
        button.y + button.height > m_image.rows || button.width <= 0 || button.height <= 0) {
        return std::nullopt;
    }
    cv::Mat hsv, blue;
    cv::cvtColor(m_image(cv::Rect(button.x, button.y, button.width, button.height)), hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(85, 100, 100), cv::Scalar(115, 255, 255), blue);
    if (cv::countNonZero(blue) < button.width * button.height / 3) {
        return std::nullopt;
    }
    return button;
}
