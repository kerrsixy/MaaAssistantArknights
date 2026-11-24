#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Vision/VisionHelper.h"

namespace asst::blackflow
{
struct DetailStrengthObservation
{
    std::string operator_id;
    std::string operator_name;
    std::string recognized_name;
    int elite = 0;
    int level = 0;
    int maximum_level = 0;
    Rect name_rect;
    Rect level_rect;
    Rect maximum_level_rect;
};

// All identity and strength fields must belong to the same verified detail frame.
class BlackFlowDetailStrengthAnalyzer final : public VisionHelper
{
public:
    using VisionHelper::VisionHelper;

    [[nodiscard]] static std::vector<std::string>
        expected_labels(const std::string& operator_id, const std::string& operator_name);

    [[nodiscard]] std::optional<DetailStrengthObservation>
        analyze(const std::string& operator_id, const std::string& operator_name) const;
};
}
