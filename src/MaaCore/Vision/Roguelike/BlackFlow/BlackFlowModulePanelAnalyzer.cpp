#include "BlackFlowModulePanelAnalyzer.h"

#include "Config/TaskData.h"
#include "Utils/Logger.hpp"
#include "Vision/OCRer.h"

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
