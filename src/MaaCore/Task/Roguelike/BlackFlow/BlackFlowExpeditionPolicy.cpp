#include "BlackFlowExpeditionPolicy.h"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace asst::blackflow::expedition_detail
{
ExpeditionRunObservation current(const ExpeditionCandidate& candidate)
{
    auto result = candidate.roster.fresh ? candidate.roster : ExpeditionRunObservation {};
    if (!candidate.selection.fresh) {
        return result;
    }
    const auto& selected = candidate.selection;
    result.fresh = true;
    if (selected.recruited.has_value()) {
        result.recruited = selected.recruited;
    }
    if (selected.available.has_value()) {
        result.available = selected.available;
    }
    if (selected.phase.has_value()) {
        result.phase = selected.phase;
    }
    if (selected.temporary.has_value()) {
        result.temporary = selected.temporary;
    }
    return result;
}

bool available(const ExpeditionRunObservation& observation)
{
    return observation.fresh && observation.recruited == true && observation.available == true;
}

ExpeditionRequirements missing(const ExpeditionRequirements& required, const ExpeditionRequirements& remaining)
{
    return { std::max(0, required.output - remaining.output),
             std::max(0, required.healing - remaining.healing),
             std::max(0, required.persistent_blocking - remaining.persistent_blocking) };
}

bool complete(const ExpeditionRequirements& value)
{
    return value.output == 0 && value.healing == 0 && value.persistent_blocking == 0;
}

ExpeditionRequirements remaining_duties(
    const std::vector<ExpeditionCandidate>& candidates,
    const std::vector<ExpeditionRunObservation>& observations,
    const std::string& departing,
    std::vector<std::string>* ids = nullptr)
{
    ExpeditionRequirements result { 0, 0, 0 };
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const auto& candidate = candidates[index];
        if (candidate.id == departing || !available(observations[index])) {
            continue;
        }
        if (ids) {
            ids->emplace_back(candidate.id);
        }
        if (!candidate.duties.fresh) {
            continue;
        }
        result.output += candidate.duties.output == true ? 1 : 0;
        result.healing += candidate.duties.healing == true ? 1 : 0;
        result.persistent_blocking += candidate.duties.persistent_blocking == true ? 1 : 0;
    }
    return result;
}

std::string promotion_denial(
    const ExpeditionContext& context,
    const ExpeditionCandidate& candidate,
    const ExpeditionRunObservation& observed)
{
    if (!available(observed)) {
        return "current_recruitment_or_availability_unverified";
    }
    if (!observed.phase) {
        return "current_phase_unknown";
    }
    if (*observed.phase == 2) {
        return "already_current_e2";
    }
    // This policy has an E1 -> E2 contract only. It does not invent an E0
    // return phase from account ownership or an unspecified event variant.
    if (*observed.phase != 1) {
        return "current_phase_not_supported_for_e2_training";
    }
    if (observed.temporary != false) {
        return "ordinary_current_origin_unverified";
    }
    if (!candidate.account_phase_verified || candidate.account_phase != 2) {
        return "account_e2_unlock_unverified";
    }
    if (candidate.e2_skill_unlock_priority < 0 || (candidate.promote_priority && *candidate.promote_priority < 0)) {
        return "invalid_promotion_benefit";
    }
    if (candidate.e2_skill_unlock_priority == 0 &&
        (!candidate.promote_priority || *candidate.promote_priority < context.minimum_promote_priority)) {
        return "promotion_benefit_below_threshold_or_unknown";
    }
    return {};
}
}

asst::blackflow::ExpeditionDecision asst::blackflow::choose_expedition_candidate(
    const ExpeditionContext& context,
    const std::vector<ExpeditionCandidate>& candidates)
{
    ExpeditionDecision decision;
    const auto& required = context.minimum_remaining;
    if (required.output < 0 || required.healing < 0 || required.persistent_blocking < 0 ||
        context.minimum_promote_priority < 0 || context.final_floor < 2) {
        decision.reason = "invalid_expedition_requirements";
        return decision;
    }
    if (!context.floor || *context.floor < 1) {
        decision.reason = "current_floor_unknown_or_invalid";
        return decision;
    }
    if (*context.floor >= context.final_floor) {
        decision.reason = "no_next_floor_before_final_ending";
        return decision;
    }
    if (!context.promotion_option_verified) {
        decision.reason = "promotion_option_not_verified_never_choose_beacon";
        return decision;
    }
    if (!context.return_next_floor_verified) {
        decision.reason = "next_floor_return_unverified";
        return decision;
    }
    std::set<std::string> unique_ids;
    std::set<std::string> ambiguous_ids;
    std::vector<ExpeditionRunObservation> observations;
    observations.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        if (candidate.id.empty() || !unique_ids.emplace(candidate.id).second) {
            ambiguous_ids.emplace(candidate.id);
        }
    }
    for (const auto& candidate : candidates) {
        observations.emplace_back(
            ambiguous_ids.contains(candidate.id) ? ExpeditionRunObservation {} : expedition_detail::current(candidate));
    }
    decision.remaining =
        expedition_detail::remaining_duties(candidates, observations, {}, &decision.remaining_available_ids);
    decision.missing = expedition_detail::missing(required, decision.remaining);

    std::optional<std::size_t> chosen;
    // A fulfilled-duty proposal ranks ahead of a route-dependent proposal.
    // Within that tier, skill unlock and configured promotion priority are
    // ordinal preferences. Equal values retain the observed candidate order.
    std::tuple<bool, int, int> best_rank;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const auto& candidate = candidates[index];
        ExpeditionCandidateAssessment assessment;
        assessment.id = candidate.id;
        assessment.remaining = expedition_detail::remaining_duties(candidates, observations, candidate.id);
        assessment.missing = expedition_detail::missing(required, assessment.remaining);
        assessment.reason = ambiguous_ids.contains(candidate.id)
                                ? "current_identity_empty_or_ambiguous"
                                : expedition_detail::promotion_denial(context, candidate, observations[index]);
        assessment.eligible = assessment.reason.empty();
        if (assessment.eligible) {
            const bool duties_met = expedition_detail::complete(assessment.missing);
            assessment.requires_battle_avoidance = !duties_met;
            assessment.dispatch_allowed =
                duties_met || context.route == ExpeditionRoute::VerifiedAvoidBattlesUntilReturn;
            assessment.reason = duties_met                    ? "required_remaining_duties_retained"
                                : assessment.dispatch_allowed ? "requires_verified_avoidance_until_return"
                                                              : "required_remaining_duties_missing_route_not_safe";
            const auto rank =
                std::make_tuple(duties_met, candidate.e2_skill_unlock_priority, candidate.promote_priority.value_or(0));
            if (assessment.dispatch_allowed && (!chosen || rank > best_rank)) {
                chosen = index;
                best_rank = rank;
            }
        }
        decision.assessments.emplace_back(std::move(assessment));
    }
    if (!chosen) {
        decision.reason = "no_valuable_dispatchable_promotion_candidate_rest_two_hope";
        return decision;
    }
    const auto& assessment = decision.assessments[*chosen];
    decision.action = ExpeditionAction::TrainForPromotion;
    decision.candidate_id = assessment.id;
    decision.expected_return_phase = 2;
    decision.requires_battle_avoidance = assessment.requires_battle_avoidance;
    decision.remaining = assessment.remaining;
    decision.missing = assessment.missing;
    decision.remaining_available_ids.clear();
    expedition_detail::remaining_duties(candidates, observations, assessment.id, &decision.remaining_available_ids);
    decision.reason = assessment.reason;
    return decision;
}
