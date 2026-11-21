#pragma once

#include <optional>
#include <string>

#include "Config/Roguelike/BlackFlow/BlackFlowAccountProfile.h"
#include "Vision/VisionHelper.h"

namespace asst::blackflow
{
struct ModulePanelTarget
{
    std::string module_id;
    std::string display_title;
    int max_level = 3;
};

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
};
}
