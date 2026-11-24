#include "BlackFlowAccountTargetLocator.h"

#include <algorithm>

#include "Config/Roguelike/RoguelikeTrainingConfig.h"
#include "Config/TaskData.h"
#include "Vision/Miscellaneous/PipelineAnalyzer.h"
#include "Vision/OCRer.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowDetailStrengthAnalyzer.h"

namespace asst::blackflow
{
AccountListTargetLocation BlackFlowAccountTargetLocator::locate(
    const std::string& operator_id,
    const std::string& operator_name,
    const std::vector<OperBoxInfo>& verified_page,
    const Rect& footer_offset) const
{
    AccountListTargetLocation result;
    if (m_image.empty() || BlackFlowDetailStrengthAnalyzer::expected_labels(operator_id, operator_name).empty()) {
        result.reason = "target_identity_or_frame_unverified";
        return result;
    }
    const auto identity_task = Task.get<OcrTaskInfo>("BlackFlowAccountScan@DetailName");
    if (!identity_task) {
        result.reason = "target_crop_task_missing";
        return result;
    }
    const auto exact_target = [&](const auto& value) {
        return value.id == operator_id && value.name == operator_name && !value.rect.empty();
    };
    const auto read_target = [&](const OperBoxInfo& oper) -> std::optional<VerifiedAccountListTarget> {
        auto roi = oper.rect.move(footer_offset);
        if (roi.empty() || roi.x < 0 || roi.y < 0 || roi.x >= m_image.cols || roi.y >= m_image.rows) {
            return std::nullopt;
        }
        roi.width = std::min(roi.width, m_image.cols - roi.x);
        roi.height = std::min(roi.height, m_image.rows - roi.y);
        OCRer name(m_image);
        name.set_task_info(identity_task);
        name.set_roi(roi);
        name.set_required({ operator_name });
        const auto labels = name.analyze();
        if (!labels || labels->size() != 1 || labels->front().score < 0.95 || labels->front().text != operator_name) {
            return std::nullopt;
        }
        return VerifiedAccountListTarget { oper, labels->front().rect, labels->front().score, labels->front().text };
    };

    // Only a previously verified single page can be reused, never a multi-frame
    // union or an account cache. Ambiguous identities use the existing full scan.
    if (!verified_page.empty() && RoguelikeTraining.unique_identity(operator_name) == operator_id &&
        std::ranges::count_if(verified_page, exact_target) == 1) {
        PipelineAnalyzer page(m_image);
        page.set_tasks(
            std::vector<std::string> {
                "OperProgress@OperBoxRoleNoFilter",
                "OperProgress@OperBoxRoleFiltered",
                "OperProgress@OperBoxRoleBarOpened",
                "OperProgress@OperBoxSortTabs",
            });
        if (const auto gate = page.analyze()) {
            result.page_anchor_verified = true;
            result.page_anchor = gate->task_ptr->name;
            const auto found = std::ranges::find_if(verified_page, exact_target);
            if (auto target = read_target(*found)) {
                result.list_verified = true;
                result.used_verified_page_geometry = true;
                result.page = verified_page;
                result.target = std::move(target);
                result.reason = "fresh_page_anchor_and_exact_target_crop";
                return result;
            }
        }
    }

    // Uncertain geometry/page/identity never creates a best-effort click. Read
    // the whole current frame and preserve the original unique target crop gate.
    result.full_page_scanned = true;
    OperBoxImageAnalyzer fresh(m_image);
    if (!fresh.analyze()) {
        result.reason = "fresh_full_list_unverified";
        return result;
    }
    result.list_verified = true;
    result.page = fresh.get_result();
    if (std::ranges::count_if(result.page, exact_target) != 1) {
        result.reason = "target_not_unique_on_fresh_full_page";
        return result;
    }
    const auto found = std::ranges::find_if(result.page, exact_target);
    result.target = read_target(*found);
    result.reason = result.target ? "fresh_full_page_and_exact_target_crop" : "fresh_target_crop_unverified";
    return result;
}
}
