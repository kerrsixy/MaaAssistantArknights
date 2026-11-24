#pragma once

#include "Config/Roguelike/BlackFlow/BlackFlowAccountTraining.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowDetailStrengthAnalyzer.h"
#include "Vision/VisionHelper.h"

namespace asst::blackflow
{
struct AccountDetailTrainingObservation
{
    DetailStrengthObservation strength;
    AccountTrainingEvidence training;
};

// Read only the verified operator detail frame. Never enter training or spend resources.
class BlackFlowAccountTrainingAnalyzer final : public VisionHelper
{
public:
    using VisionHelper::VisionHelper;

    // One strict detail read supplies both fields from exactly this same frame.
    [[nodiscard]] std::optional<AccountDetailTrainingObservation>
        analyze_detail(const std::string& operator_id, const std::string& operator_name) const;

    [[nodiscard]] std::optional<AccountTrainingEvidence>
        analyze(const std::string& operator_id, const std::string& operator_name) const;
};
}
