#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "BlackFlowTaskPluginBase.h"
#include "Common/AsstTypes.h"

namespace asst::blackflow
{
class BlackFlowMovementTaskPlugin final : public BlackFlowTaskPluginBase
{
public:
    using BlackFlowTaskPluginBase::BlackFlowTaskPluginBase;

    virtual bool verify(AsstMsg msg, const json::value& details) const override;
    virtual void reset_in_run_variables() override;

protected:
    virtual bool _run() override;

private:
    enum class PendingWork
    {
        None,
        SelectMovement,
        ObserveInventory,
    };

    enum class SelectionOutcome
    {
        Selected,
        Unavailable,
        Failed,
    };

    enum class InventoryAnalysisOutcome
    {
        Recognized,
        NoCandidate,
        Failed,
    };

    struct PanelItem
    {
        MovementKind movement = MovementKind::Walk;
        Rect name_rect;
        double name_score = 0.0;
        std::optional<int> remaining_uses;
        bool loaded = false;
        int name_match_count = 1;
        int remaining_match_count = 0;
    };

    struct PanelFrame
    {
        std::vector<PanelItem> items;
        std::optional<MovementKind> loaded_movement;
    };

    struct InventoryItem
    {
        MovementKind movement = MovementKind::Walk;
        Rect name_rect;
    };

    struct InventoryFrame
    {
        std::unordered_map<MovementKind, int> movements;
        // 保留本帧卡片位置，用于跨水平分页去重。零件箱可以出现同名加工品，
        // 不能只用名称集合去重，否则会把合法的第二件丢掉。
        std::vector<InventoryItem> items;
        std::optional<MovementKind> loaded_movement;
        // 只有至少成功识别到一页完整卡片后，才允许用“缺失”清理本层过期零件。
        bool complete = false;
    };

    bool observe_inventory();
    bool scan_inventory_frame(InventoryFrame& frame, std::string* error);
    InventoryAnalysisOutcome
        analyze_inventory_frame(const cv::Mat& image, InventoryFrame& frame, std::string* error) const;

    SelectionOutcome select_movement(MovementKind target, std::string* error);
    bool rewind_panel(PanelFrame& frame, std::string* error);
    bool ensure_panel_open(std::string* error);
    bool close_panel(std::string* error);
    bool title_visible(const cv::Mat& image) const;
    bool scan_current_frame(PanelFrame& frame, std::string* error) const;
    bool analyze_frame(const cv::Mat& image, PanelFrame& frame, std::string* error) const;
    bool run_fixed_task(std::string_view task);
    bool report_target_observation(
        MovementKind target,
        const PanelItem& item,
        int completed_swipes,
        std::optional<MovementKind> active_movement,
        std::string* error);

    static const MovementSpec* movement_from_name(std::string_view name) noexcept;
    static std::optional<int> remaining_uses_from_text(std::string_view text) noexcept;
    static std::vector<std::string> ocr_candidates();
    static std::vector<std::string> inventory_ocr_candidates();

    mutable PendingWork m_pending = PendingWork::None;
};
} // namespace asst::blackflow
