#include "BlackFlowMovePreviewAnalyzer.h"

#include "Config/Roguelike/BlackFlow/BlackFlowNodeExecutionConfig.h"
#include "Config/TaskData.h"
#include "Vision/OCRer.h"
#include <algorithm>
#include <tuple>

namespace asst::blackflow
{
bool recognize_move_preview_identity(const cv::Mat& image, MovePreview& preview, std::string* error)
{
    const auto task = Task.get<OcrTaskInfo>("BlackFlow@Roguelike@MovePreviewDisplayedName");
    if (!task || image.empty()) {
        if (error) {
            *error = "preview category frame/task missing";
        }
        return false;
    }
    OCRer names(image);
    names.set_task_info(task);
    const auto rows = names.analyze();
    if (!rows) {
        if (error) {
            *error = "preview category OCR missing";
        }
        return false;
    }
    std::optional<NodeType> kind;
    int category_y = -1;
    std::string category;
    for (const auto& row : *rows) {
        const auto type = BlackFlowNodeExecution.preview_node_type(row.text);
        if (type && row.score >= 0.95 && (category_y < 0 || row.rect.y < category_y)) {
            kind = type;
            category = row.text;
            category_y = row.rect.y;
        }
    }
    for (const auto& row : *rows) {
        const auto type = BlackFlowNodeExecution.preview_node_type(row.text);
        if (kind && type && row.score >= 0.95 && std::abs(row.rect.y - category_y) < 8 && *type != *kind) {
            if (error) {
                *error = "ambiguous same-line preview category";
            }
            return false;
        }
    }
    if (!kind) {
        if (error) {
            *error = "preview category has no verified mapping";
        }
        return false;
    }
    preview.displayed_type = *kind;
    preview.displayed_name = category;
    preview.stage_title.clear();
    preview.occupation.reset();
    preview.identity_revealed = *kind != NodeType::HideInvisible && *kind != NodeType::HideBattle;
    if (*kind == NodeType::BattleNormal || *kind == NodeType::BattleElite || *kind == NodeType::BattleSavage) {
        for (const auto& row : *rows) {
            if (row.score >= 0.95 && row.rect.y >= category_y + 12) {
                preview.stage_title = row.text;
                break;
            }
        }
    }
    if (*kind == NodeType::BattleNormal && !preview.stage_title.empty()) {
        const auto body_task = Task.get<OcrTaskInfo>("BlackFlow@Roguelike@MovePreviewOccupationDescription");
        if (body_task) {
            OCRer body(image);
            body.set_task_info(body_task);
            auto description = body.analyze();
            if (description) {
                std::ranges::sort(*description, [](const auto& a, const auto& b) {
                    return std::tie(a.rect.y, a.rect.x) < std::tie(b.rect.y, b.rect.x);
                });
                std::string text;
                bool confident = !description->empty();
                for (const auto& row : *description) {
                    text += row.text;
                    confident &= row.score >= 0.95;
                }
                if (confident) {
                    preview.occupation = parse_node_occupation_description(text, preview.stage_title);
                }
            }
        }
    }
    return true;
}
}
