#pragma once
#include "Vision/VisionHelper.h"
#include <cstdint>
#include <functional>

#include "Common/AsstBattleDef.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowRecruitmentHope.h"

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

// One recognized name/flag observation from one analyze() call. A missing
// level can exclude it from get_result() while this raw sidecar still survives.
struct RoguelikeRecruitCandidateEvidence
{
    std::uint64_t analysis_id = 0;
    std::size_t observation_index = 0;
    std::optional<std::size_t> usable_result_index;
    std::string name;
    Rect name_rect;
    double name_score = 0.0;
    Rect currency_flag_rect;
    double currency_flag_score = 0.0;
    std::optional<std::size_t> hope_cost_evidence_index;
    int elite = -1;
    int level = -1;
    // true = positively temporary; false requires a proven non-temporary rule.
    // The present detector emits true/unknown, never false from a missing badge.
    std::optional<bool> temporary;
    std::optional<bool> legacy_temporary_flag;
    Rect temporary_search_roi;
    std::optional<Rect> temporary_badge_rect;
    std::optional<double> temporary_badge_score;
    std::string temporary_reason;
    bool exact_price_link = false;
    std::string price_link_reason;
    std::string usability_reason;
};

// Geometry preserves named cards excluded from usable results and unmatched
// currency anchors. It does not prove a physical endpoint or full-list coverage.
struct RoguelikeRecruitPageGeometry
{
    std::vector<std::pair<std::string, Rect>> named_cards;
    std::vector<Rect> currency_anchors;
};

class RoguelikeRecruitImageAnalyzer final : public VisionHelper
{
public:
    using VisionHelper::VisionHelper;
    virtual ~RoguelikeRecruitImageAnalyzer() noexcept override = default;

    bool analyze();

    // Resolve a real pre-payment balance from one frame, never a gray cap or
    // another candidate's price. Unverified selection/price remains unknown.
    static blackflow::RecruitmentHopeReading read_blackflow_hope_balance(const cv::Mat& image);

    const auto& get_result() const noexcept { return m_result; }

    const auto& get_hope_cost_evidence() const noexcept { return m_hope_cost_evidence; }

    const auto& get_candidate_evidence() const noexcept { return m_candidate_evidence; }

    std::uint64_t get_analysis_id() const noexcept { return m_analysis_id; }

    RoguelikeRecruitPageGeometry paging_geometry() const;

    // Empty means another bounded observation may be attempted. An unchanged
    // signature may also be a failed swipe or stale image, never endpoint proof.
    static std::string paging_stop_reason(
        const RoguelikeRecruitPageGeometry& current,
        const std::optional<RoguelikeRecruitPageGeometry>& previous,
        int observation_step,
        int max_observations);

    // Returns a reference only for an unambiguous same-analysis flag/index link.
    // Copies with a stale analysis ID or a different card/price index are rejected.
    std::optional<std::reference_wrapper<const RoguelikeRecruitHopeCostEvidence>>
        resolve_candidate_hope_cost(const RoguelikeRecruitCandidateEvidence& candidate) const noexcept;

    // The flag must be this card's matched hope-currency template, not its name
    // or a rarity/level-derived price. Unreadable fields remain unknown.
    static RoguelikeRecruitHopeCostEvidence
        analyze_hope_cost(const cv::Mat& image, const Rect& currency_flag, double flag_score);

private:
    int match_elite(const Rect& raw_roi);
    static int match_level(const cv::Mat& image, const Rect& raw_roi);

    std::vector<battle::roguelike::Recruitment> m_result;
    std::vector<RoguelikeRecruitHopeCostEvidence> m_hope_cost_evidence;
    std::vector<RoguelikeRecruitCandidateEvidence> m_candidate_evidence;
    std::uint64_t m_analysis_id = 0;
};
}
