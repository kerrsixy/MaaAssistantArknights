#pragma once

#include "BlackFlowRecruitmentDuty.h"
#include "BlackFlowRecruitmentQuoteLedger.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowVoucherStash.h"

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

struct TerminalOutputHopeForecast
{
    std::optional<int> expected_hope;
    std::string reason;
    std::string template_terms;
    std::string roster_terms;
    json::value evidence = nullptr;
};

struct TerminalHopeReservationDecision
{
    bool request_stash = false;
    bool needs_stash_evidence = false;
    std::string reason;
    TerminalOutputHopeForecast forecast;
};

[[nodiscard]] TerminalOutputHopeForecast forecast_terminal_output_hope(
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const std::unordered_map<std::string, RoguelikeOper>& roster,
    const TerminalDutyContext& context,
    const RecruitmentQuoteScope& current_price_scope,
    const std::vector<RecruitmentObservedQuote>& observed_quotes);

// A proposed attempt, never a stored-voucher/survival/damage guarantee.
[[nodiscard]] TerminalHopeReservationDecision choose_terminal_hope_reservation(
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const std::unordered_map<std::string, RoguelikeOper>& roster,
    const TerminalDutyContext& context,
    const RecruitmentQuoteScope& current_price_scope,
    const std::vector<RecruitmentObservedQuote>& observed_quotes,
    const std::vector<RecruitmentDutyOffer>& current_offers,
    const std::string& selected_name,
    int selected_page,
    const VoucherStashObservation& current_stash);

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
