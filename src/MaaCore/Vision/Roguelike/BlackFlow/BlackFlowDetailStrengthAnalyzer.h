#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Vision/VisionHelper.h"

namespace asst::blackflow
{
struct DetailTrustObservation
{
    int percentage = -1;
    double confidence = 0;
    std::string raw_text;
    Rect label_rect;
    Rect value_rect;
};

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
    std::optional<DetailTrustObservation> trust;
};

// All identity and strength fields must belong to the same verified detail frame.
class BlackFlowDetailStrengthAnalyzer final : public VisionHelper
{
public:
    using VisionHelper::VisionHelper;

    [[nodiscard]] static std::vector<std::string>
        expected_labels(const std::string& operator_id, const std::string& operator_name);

    [[nodiscard]] static std::optional<int> parse_trust_percentage(const std::string& text);

    [[nodiscard]] std::optional<DetailStrengthObservation>
        analyze(const std::string& operator_id, const std::string& operator_name) const;
};
}
