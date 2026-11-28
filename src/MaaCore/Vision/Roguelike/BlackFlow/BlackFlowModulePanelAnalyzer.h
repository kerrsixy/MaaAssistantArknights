#pragma once

#include <optional>
#include <string>

#include "Config/Roguelike/BlackFlow/BlackFlowAccountProfile.h"
#include "Config/Roguelike/BlackFlow/BlackFlowAccountScanCatalog.h"
#include "Vision/VisionHelper.h"

namespace asst::blackflow
{
struct ModulePanelObservation
{
    AccountModuleEvidence evidence;
    Rect title_rect;
    Rect state_rect;
    std::string state_text;
};

// This account-panel observation proves neither the equipped module nor current-battle availability.
// Intermediate levels without an unambiguous state remain unknown.
class BlackFlowModulePanelAnalyzer final : public VisionHelper
{
public:
    using VisionHelper::VisionHelper;
    using Result = ModulePanelObservation;
    using ResultOpt = std::optional<Result>;

    [[nodiscard]] ResultOpt analyze(const ModulePanelTarget& target) const;
    // A positive full two-line control on this exact MAX card proves selection.
    [[nodiscard]] std::optional<Rect> set_default_button(const ModulePanelTarget& target) const;
};
}
