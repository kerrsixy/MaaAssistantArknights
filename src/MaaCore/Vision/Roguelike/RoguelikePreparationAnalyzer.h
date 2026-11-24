#pragma once

#include <array>
#include <string>
#include <vector>

#include "Config/Roguelike/RoguelikeTrainingConfig.h"
#include "Vision/VisionHelper.h"

namespace asst
{
enum class PreparationPage
{
    Formation,
    Skills,
};

enum class PreparationReadMode
{
    Full,
    SelectionOnly,
};

struct PreparationCardAnchor
{
    std::string name;
    Rect flag;
};

struct PreparationOperatorObservation
{
    std::string name;
    std::string operator_id;
    Rect flag;
    bool anchor_verified = false;
    bool training_observed = true;
    int elite = -1;
    int level = -1;
    double elite_score = -1.0;
    std::string raw_level;
    std::array<SkillAvailability, 3> availability {};
    std::array<int, 3> displayed_skill_levels { -1, -1, -1 };
    std::array<std::string, 3> raw_skill_levels;
    std::array<double, 3> selected_marker_scores {};
    int selected_skill = -1;

    json::value to_json() const;
};

// Receives names/anchors from the existing page recognizer; never clicks or
// invents strength from an account cache. One shared blue-channel frame per page.
class RoguelikePreparationAnalyzer final : public VisionHelper
{
public:
    using VisionHelper::VisionHelper;
    static bool skill_page_visible(const cv::Mat& image);
    std::vector<PreparationOperatorObservation> analyze(
        const std::vector<PreparationCardAnchor>& cards,
        PreparationPage page,
        PreparationReadMode mode = PreparationReadMode::Full) const;
};
}
