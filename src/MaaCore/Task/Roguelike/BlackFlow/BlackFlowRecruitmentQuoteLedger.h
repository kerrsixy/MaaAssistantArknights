#pragma once

#include "BlackFlowRecruitmentDuty.h"

namespace asst::blackflow
{
struct RecruitmentQuoteScope
{
    std::string client;
    std::string uid;
    std::string package;
    std::string squad;
    std::string squad_source;
    std::string source_cohort;
    std::string price_context;
    bool price_context_complete = false;

    [[nodiscard]] bool valid() const;
};

struct RecruitmentObservedQuote
{
    RecruitmentQuoteScope scope;
    RecruitmentDutyOffer offer;
    std::string frame_path;
    std::string observation_id;
    bool original_scan_eligible = false;
    std::optional<bool> non_economic_eligible;
    std::optional<bool> affordable_at_scan_balance;
    std::optional<int> scan_hope_balance;
};

struct RecruitmentQuoteEligibility
{
    std::uint64_t analysis_id = 0;
    std::size_t observation_index = 0;
    std::string name;
    int page_index = 0;
    std::optional<bool> non_economic_eligible;
    bool affordable = false;
    bool selection_eligible = false;
};

// A native same-analysis observation archive, never an offer for the current UI.
// Loading legacy callbacks/Python projections is deliberately unsupported.
class RecruitmentQuoteLedger
{
public:
    static constexpr std::size_t MaximumEntries = 2048;

    [[nodiscard]] static std::string source_cohort(const std::filesystem::path& resource);
    [[nodiscard]] static std::vector<RecruitmentObservedQuote> capture(
        const RoguelikeRecruitImageAnalyzer& analyzer,
        const std::vector<RecruitmentDutyOffer>& offers,
        const json::value& native_page_receipt,
        const RecruitmentQuoteScope& scope);

    bool bind(const RecruitmentQuoteScope& scope);
    void append(
        std::vector<RecruitmentObservedQuote> observations,
        const std::vector<std::pair<std::string, int>>& eligible);
    void append_with_non_economic_eligibility(
        std::vector<RecruitmentObservedQuote> observations,
        const std::vector<RecruitmentQuoteEligibility>& eligibility);
    bool load(const std::filesystem::path& path, std::string* diagnostic);
    bool save(const std::filesystem::path& path, std::string* diagnostic) const;

    [[nodiscard]] const std::vector<RecruitmentObservedQuote>& entries() const noexcept { return m_entries; }

    [[nodiscard]] json::value to_json() const;

private:
    RecruitmentQuoteScope m_scope;
    std::vector<RecruitmentObservedQuote> m_entries;
};

[[nodiscard]] json::value recruitment_quote_to_json(const RecruitmentObservedQuote& value);
}
