#pragma once
#include "Vision/VisionHelper.h"

#include "Common/AsstBattleDef.h"

namespace asst
{
struct RoguelikeRecruitHopeCostEvidence
{
    std::string name;
    Rect currency_rect;
    Rect operation_roi;
    Rect price_roi;
    Rect digits_rect;
    std::string operation;
    std::string raw_digits;
    double operation_score = 0.0;
    double digits_score = 0.0;
    std::optional<int> hope_cost;
    std::string reason;
};

class RoguelikeRecruitImageAnalyzer final : public VisionHelper
{
public:
    using VisionHelper::VisionHelper;
    virtual ~RoguelikeRecruitImageAnalyzer() noexcept override = default;

    bool analyze();

    const auto& get_result() const noexcept { return m_result; }

    const auto& get_hope_cost_evidence() const noexcept { return m_hope_cost_evidence; }

    // The flag must be this card's matched hope-currency template, not its name
    // or a rarity/level-derived price. Unreadable fields remain unknown.
    static RoguelikeRecruitHopeCostEvidence
        analyze_hope_cost(const cv::Mat& image, const Rect& currency_flag, double flag_score);

private:
    int match_elite(const Rect& raw_roi);
    static int match_level(const cv::Mat& image, const Rect& raw_roi);

    std::vector<battle::roguelike::Recruitment> m_result;
    std::vector<RoguelikeRecruitHopeCostEvidence> m_hope_cost_evidence;
};
}
