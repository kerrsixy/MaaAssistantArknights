#pragma once

#include "BlackFlowRecruitmentDuty.h"

namespace asst::blackflow
{
struct TerminalDutyContext
{
    std::string strategy;
    std::string stage_name = "永无安宁";
    std::string client;
    std::string uid;
    std::string run_id;
    std::string core;
    int hope = -1;
    bool fixed_opening = false;
    const BlackFlowAccountProfile* account = nullptr;
    const RecruitmentOriginLedger* origins = nullptr;
};

struct TerminalDutyEligible
{
    std::string name;
    int page = 0;
    int existing_score = 0;
};

struct TerminalDutyIntent
{
    RecruitmentDutyOffer chosen;
    RecruitmentDutyOffer legacy;
    std::string client;
    std::string uid;
    std::string run_id;
    std::string stage_name;
    std::string template_terms;
    std::string roster_terms;
};

struct TerminalDutyDecision
{
    std::optional<std::size_t> selected_offer;
    std::optional<TerminalDutyIntent> intent;
    std::string reason;
    json::value evidence = nullptr;
};

// Bounded ending1 roster construction objective, not a DPS dominance claim.
// Actual offers/eligibility keep their original module, origin and price gates.
TerminalDutyDecision choose_terminal_output_duty(
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const std::unordered_map<std::string, RoguelikeOper>& roster,
    const TerminalDutyContext& context,
    const std::vector<RecruitmentDutyOffer>& offers,
    const std::vector<TerminalDutyEligible>& eligible,
    const std::string& legacy_name,
    int legacy_page);

bool terminal_output_refind_matches(
    const TerminalDutyIntent& intended,
    const RecruitmentDutyOffer& actual,
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const std::unordered_map<std::string, RoguelikeOper>& roster,
    const TerminalDutyContext& context,
    std::string* reason);
}
