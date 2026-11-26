#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "BlackFlowPrimaryComponent.h"
#include "Config/Roguelike/BlackFlow/BlackFlowAccountProfile.h"
#include "Task/Roguelike/RoguelikeConfig.h"
#include "Vision/Roguelike/RoguelikeRecruitImageAnalyzer.h"

namespace asst::blackflow
{
enum class ConditionalPrimaryTarget
{
    Unknown,
    Ground,
    Air,
};

struct PrimarySequenceConditions
{
    ConditionalPrimaryTarget target = ConditionalPrimaryTarget::Unknown;
    std::string source;
    std::optional<bool> isolated_target;
    std::optional<bool> steal_succeeded;
    std::optional<double> stolen_aspd;
    std::optional<int> remaining_ammunition;
};

struct ConditionalPrimaryModeEstimate
{
    std::string key;
    std::vector<PhysicalSequenceEnvelope> variants;
    std::optional<int> simultaneous_target_capacity;
    bool ammunition_is_observed_remaining = false;
    std::array<double, 2> added_aspd { 0, 0 };
    bool ally_opportunity_cost_unresolved = false;
};

struct ConditionalPrimaryEstimate
{
    bool qualified = false;
    std::string id;
    std::string range;
    std::string source;
    std::string reason;
    int effective_rank = -1;
    bool requires_sustain_coverage = false;
    std::array<double, 2> sp_debt { 0, 0 };
    std::vector<ConditionalPrimaryModeEstimate> modes;
    std::vector<std::string> limitations;
};

[[nodiscard]] json::value conditional_primary_to_json(const ConditionalPrimaryEstimate& value);

struct RecruitmentSkillPlan
{
    int preferred = 0;
    int alternate = 0;
    int selected = 0;
    std::array<SkillAvailability, 3> availability { SkillAvailability::Unknown,
                                                    SkillAvailability::Unknown,
                                                    SkillAvailability::Unknown };
    std::string reason;
};

[[nodiscard]] RecruitmentSkillPlan
    resolve_recruitment_skill_plan(int preferred, int alternate, const std::array<SkillAvailability, 3>& availability);

struct DutyBound
{
    int lower = 0;
    int upper = 1;
};

struct ProjectedOutputAttributes
{
    std::optional<int> trust_percent;
    std::optional<int> potential;
    std::string source;
};

struct RecruitmentOutputEvidence
{
    std::string client;
    std::string uid;
    std::string run_id;
    std::string source;
    bool external_modifiers_bounded = false;
    std::array<double, 2> external_atk_fraction { 0, 0 };
    std::array<double, 2> external_aspd { 0, 0 };
    std::unordered_map<std::string, ProjectedOutputAttributes> attributes;
    // Positive, source-bound differential facts; absence is not proof of none.
    std::unordered_map<std::string, std::vector<std::string>> known_operator_risks;
};

enum class PrimaryProjectionObjective
{
    CurrentRunComponents,
    OutsideIntrinsicReference,
};

struct ProjectedOutputEstimate
{
    PrimaryProjectionObjective objective = PrimaryProjectionObjective::CurrentRunComponents;
    std::vector<std::string> limitations;
    bool qualified = false;
    bool marginal_known_positive = false;
    std::string id;
    std::string range;
    std::string reason;
    std::string evidence_source;
    int effective_rank = -1;
    std::vector<ProjectedPhysicalProfile> lower;
    std::vector<ProjectedPhysicalProfile> upper;
};

[[nodiscard]] bool projected_output_strictly_better(const ProjectedOutputEstimate& a, const ProjectedOutputEstimate& b);
[[nodiscard]] json::value projected_output_to_json(const ProjectedOutputEstimate& value);

struct RecruitmentDutyContext
{
    std::string theme = "BlackFlow";
    std::string client;
    std::string uid;
    std::string run_id;
    std::string observed_at;
    int page_index = 0;
    int hope = -1;
    int mode = -1;
    bool initial = false;
    const RecruitmentOutputEvidence* output_evidence = nullptr;
};

struct RecruitmentDutyOffer
{
    std::string client;
    std::string uid;
    std::string run_id;
    std::uint64_t analysis_id = 0;
    std::size_t observation_index = 0;
    std::optional<std::size_t> price_index;
    int page_index = 0;
    std::string name;
    std::string id;
    int elite = -1;
    int level = -1;
    int planned_skill = -1;
    RecruitmentSkillPlan skill_plan;
    int effective_rank = -1;
    SkillAvailability skill_availability = SkillAvailability::Unknown;
    std::optional<int> hope_cost;
    std::string operation;
    std::optional<bool> observed_temporary;
    bool positive_temporary = false;
    std::string inherited_origin_receipt;
    std::string training_binding = "unknown_origin";
    std::string temporary_rule_source;
    bool account_training_used = false;
    bool ordinary_origin = false;
    bool feasible = false;
    std::string observed_at;
    std::array<DutyBound, 2> capability;
    std::array<DutyBound, 2> marginal;
    std::vector<std::string> reasons;
    bool replacement_valid = false;
    json::value skill_facts = nullptr;
    json::value healing_component = nullptr;
    ProjectedOutputEstimate projected_output;
    ProjectedOutputEstimate intrinsic_reference;
    bool intrinsic_reference_used = false;
};

struct RecruitmentOriginReceipt
{
    std::string client;
    std::string uid;
    std::string run_id;
    std::string id;
    std::string name;
    int elite = -1;
    int level = -1;
    std::string receipt_id;
    std::string previous_receipt_id;
    std::uint64_t analysis_id = 0;
    std::size_t observation_index = 0;
    std::size_t price_index = 0;
    int page_index = 0;
    int hope_cost = -1;
    std::string operation;
    std::string observed_at;
    std::string binding = "unknown_origin";
    std::string rule_source;
    std::optional<bool> temporary;
    bool ordinary = false;
};

class RecruitmentOriginLedger
{
public:
    bool bind(const std::string& client, const std::string& uid, const std::string& run_id);
    void clear();

    void forget(const std::string& id) { m_receipts.erase(id); }

    bool confirm(
        const RecruitmentDutyOffer& offer,
        const std::optional<RoguelikeOper>& before,
        bool confirmed,
        int recruit_index);
    [[nodiscard]] std::optional<RecruitmentOriginReceipt> find(
        const std::string& client,
        const std::string& uid,
        const std::string& run_id,
        const std::string& id,
        const RoguelikeOper& before) const;

    [[nodiscard]] std::size_t size() const noexcept { return m_receipts.size(); }

private:
    std::string m_client;
    std::string m_uid;
    std::string m_run_id;
    std::unordered_map<std::string, RecruitmentOriginReceipt> m_receipts;
};

[[nodiscard]] json::value recruitment_origin_to_json(const RecruitmentOriginReceipt& receipt);

class BlackFlowRecruitmentDutyCatalog;
void evaluate_recruitment_duties(
    RecruitmentDutyOffer& offer,
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const std::unordered_map<std::string, RoguelikeOper>& roster);

class BlackFlowRecruitmentDutyCatalog
{
public:
    bool configure(const json::value& facts, const json::value& training, const json::value& battle);
    bool load(
        const std::filesystem::path& facts,
        const std::filesystem::path& training,
        const std::filesystem::path& battle);
    [[nodiscard]] std::array<DutyBound, 2> body(const std::string& name, int elite, int level) const;
    [[nodiscard]] json::value skill(const std::string& id, int slot, int rank) const;
    [[nodiscard]] json::value heal_packet(const std::string& name, int elite, int level, int slot, int rank) const;
    [[nodiscard]] ProjectedOutputEstimate projected_output(
        const std::string& name,
        int elite,
        int level,
        int slot,
        int rank,
        const ProjectedOutputAttributes& attributes,
        const RecruitmentOutputEvidence& modifiers) const;

    [[nodiscard]] ProjectedOutputEstimate intrinsic_primary(
        const std::string& name,
        int elite,
        int level,
        int slot,
        int rank,
        const ProjectedOutputAttributes& attributes,
        const std::string& source) const;

    // Source-conditioned acquired-ground-target components, not an automatic
    // extension of recruitment ranking or a current-run DPS/survival promise.
    [[nodiscard]] ConditionalPrimaryEstimate conditional_primary(
        const std::string& name,
        int elite,
        int level,
        int slot,
        int displayed_or_effective_rank,
        const ProjectedOutputAttributes& attributes,
        const PrimarySequenceConditions& conditions,
        const std::string& source) const;

    [[nodiscard]] bool valid() const noexcept { return m_valid; }

    [[nodiscard]] std::string temporary_rule_source() const;

private:
    [[nodiscard]] ProjectedOutputEstimate primary_components(
        const std::string& name,
        int elite,
        int level,
        int slot,
        int rank,
        const ProjectedOutputAttributes& attributes,
        const RecruitmentOutputEvidence* modifiers) const;
    bool m_valid = false;
    json::value m_operators = json::object {};
    json::value m_temporary_rule = nullptr;
    std::string m_revision;
    std::string m_public_skill_sha;
};

// Current-run instance facts only. This does not resolve an acquired target's
// promotion, ordinary/temporary origin, account training or future lifetime.
[[nodiscard]] std::optional<int>
    current_roster_preparation_rank(const std::string& name, const RoguelikeOper& current, int slot);

void evaluate_recruitment_output(
    RecruitmentDutyOffer& offer,
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const BlackFlowAccountProfile* account,
    const std::unordered_map<std::string, RoguelikeOper>& roster,
    const RecruitmentDutyContext& context,
    const RecruitmentOriginLedger* origins);

[[nodiscard]] std::vector<RecruitmentDutyOffer> observe_recruitment_duties(
    const RoguelikeRecruitImageAnalyzer& analyzer,
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const BlackFlowAccountProfile* account,
    const std::unordered_map<std::string, RoguelikeOper>& roster,
    const RecruitmentDutyContext& context);

[[nodiscard]] std::vector<RecruitmentDutyOffer> observe_recruitment_duties(
    const RoguelikeRecruitImageAnalyzer& analyzer,
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const BlackFlowAccountProfile* account,
    const std::unordered_map<std::string, RoguelikeOper>& roster,
    const RecruitmentDutyContext& context,
    const RecruitmentOriginLedger* origins);

struct RecruitmentDutyDecision
{
    std::optional<std::size_t> selected_offer;
    std::string reason;
    bool adjusted = false;
    bool intrinsic_reference_used = false;
};

// Eligible keys are the unchanged legacy consumer's name/page pairs, not all
// raw observations. This is limited roster-duty dominance, never global DPS.
[[nodiscard]] RecruitmentDutyDecision choose_recruitment_duty(
    const std::vector<RecruitmentDutyOffer>& offers,
    const std::vector<std::pair<std::string, int>>& eligible,
    const std::string& legacy_name,
    int legacy_page,
    bool fixed_opening);

[[nodiscard]] bool
    recruitment_duty_strictly_dominates(const RecruitmentDutyOffer& better, const RecruitmentDutyOffer& worse);
[[nodiscard]] bool
    recruitment_duty_refind_matches(const RecruitmentDutyOffer& intended, const RecruitmentDutyOffer& actual);
[[nodiscard]] json::value recruitment_duty_to_json(const RecruitmentDutyOffer& offer);

struct RecruitmentFinalOffer
{
    std::optional<RecruitmentDutyOffer> observed;
    bool intent_matches = false;
    std::string reason;
};

[[nodiscard]] RecruitmentFinalOffer resolve_recruitment_final_offer(
    const RoguelikeRecruitImageAnalyzer& analyzer,
    const std::vector<RecruitmentDutyOffer>& observed,
    const battle::roguelike::Recruitment& selected,
    const std::optional<RecruitmentDutyOffer>& intent);
}
