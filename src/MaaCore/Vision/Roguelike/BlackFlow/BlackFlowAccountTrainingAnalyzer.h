#pragma once

#include "Config/Roguelike/BlackFlow/BlackFlowAccountTraining.h"
#include "Vision/VisionHelper.h"

namespace asst::blackflow
{
// Read only the verified operator detail frame. Never enter training or spend resources.
class BlackFlowAccountTrainingAnalyzer final : public VisionHelper
{
public:
    using VisionHelper::VisionHelper;

    [[nodiscard]] std::optional<AccountTrainingEvidence>
        analyze(const std::string& operator_id, const std::string& operator_name) const;
};
}
