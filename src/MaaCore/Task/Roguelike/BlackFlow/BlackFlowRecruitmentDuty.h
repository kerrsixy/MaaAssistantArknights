#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "Config/Roguelike/BlackFlow/BlackFlowAccountProfile.h"
#include "Task/Roguelike/RoguelikeConfig.h"
#include "Vision/Roguelike/RoguelikeRecruitImageAnalyzer.h"

namespace asst::blackflow
{
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

    [[nodiscard]] bool valid() const noexcept { return m_valid; }

    [[nodiscard]] std::string temporary_rule_source() const;

private:
    bool m_valid = false;
    json::value m_operators = json::object {};
    json::value m_temporary_rule = nullptr;
    std::string m_revision;
};

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
