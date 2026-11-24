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
    std::uint64_t analysis_id = 0;
    std::size_t observation_index = 0;
    int page_index = 0;
    std::string name;
    std::string id;
    int elite = -1;
    int level = -1;
    int planned_skill = -1;
    int effective_rank = -1;
    SkillAvailability skill_availability = SkillAvailability::Unknown;
    std::optional<int> hope_cost;
    std::string operation;
    std::optional<bool> observed_temporary;
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
}
