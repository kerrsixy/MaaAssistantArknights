#pragma once
#include "Config/Roguelike/BlackFlow/BlackFlowAccountProfile.h"
#include "Config/Roguelike/BlackFlow/BlackFlowAccountScanCatalog.h"
#include "Vision/VisionHelper.h"

namespace asst::blackflow
{
// Identity, current module code and stage must all belong to this detail frame.
class BlackFlowCurrentModuleAnalyzer final : public VisionHelper
{
public:
    using VisionHelper::VisionHelper;
    [[nodiscard]] std::optional<AccountModuleDefaultEvidence> analyze(const AccountModuleScanTarget& target) const;
};
}
