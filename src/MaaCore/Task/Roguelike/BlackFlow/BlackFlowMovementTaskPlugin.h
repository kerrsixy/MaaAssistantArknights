#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "BlackFlowMovementRecognition.h"
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

    bool observe_inventory();
    bool rewind_inventory(
        bool& head_verified,
        std::optional<MovementInventoryFrame>& verified_head_frame,
        std::string* error);
    bool scan_inventory_frame(MovementInventoryFrame& frame, std::string* error);

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

    static std::optional<int> remaining_uses_from_text(std::string_view text) noexcept;
    static std::vector<std::string> ocr_candidates();

    mutable PendingWork m_pending = PendingWork::None;
};
} // namespace asst::blackflow
