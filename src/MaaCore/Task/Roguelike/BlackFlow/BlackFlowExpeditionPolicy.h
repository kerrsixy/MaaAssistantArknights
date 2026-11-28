#pragma once

#include <optional>
#include <string>
#include <vector>

namespace asst::blackflow
{
// The caller binds these observations to the same current exploration and
// encounter. Fresh selection-page fields override known roster fields; absent
// fields retain the roster evidence. Account ownership never implies recruitment.
struct ExpeditionRunObservation
{
    bool fresh = false;
    std::optional<bool> recruited;
    std::optional<bool> available;
    std::optional<int> phase;
    std::optional<bool> temporary;
};

struct ExpeditionDuties
{
    bool fresh = false;
    std::optional<bool> output;
    std::optional<bool> healing;
    std::optional<bool> persistent_blocking;
};

struct ExpeditionCandidate
{
    std::string id;
    ExpeditionRunObservation roster;
    ExpeditionRunObservation selection;
    std::optional<int> account_phase;
    bool account_phase_verified = false;
    ExpeditionDuties duties;
    // Ordinal policy inputs, not DPS or a predicted victory probability.
    std::optional<int> promote_priority;
    int e2_skill_unlock_priority = 0;
};

struct ExpeditionRequirements
{
    int output = 1;
    int healing = 0;
    int persistent_blocking = 1;
};

enum class ExpeditionRoute
{
    Unknown,
    BattlesRequired,
    VerifiedAvoidBattlesUntilReturn,
};

struct ExpeditionContext
{
    std::optional<int> floor;
    int final_floor = 5;
    bool promotion_option_verified = false;
    bool return_next_floor_verified = false;
    ExpeditionRoute route = ExpeditionRoute::Unknown;
    ExpeditionRequirements minimum_remaining;
    int minimum_promote_priority = 1;
};

enum class ExpeditionAction
{
    RestTwoHope,
    TrainForPromotion,
};

struct ExpeditionCandidateAssessment
{
    std::string id;
    bool eligible = false;
    bool dispatch_allowed = false;
    bool requires_battle_avoidance = false;
    ExpeditionRequirements remaining { 0, 0, 0 };
    ExpeditionRequirements missing { 0, 0, 0 };
    std::string reason;
};

struct ExpeditionDecision
{
    ExpeditionAction action = ExpeditionAction::RestTwoHope;
    std::optional<std::string> candidate_id;
    std::optional<int> expected_return_phase;
    bool requires_battle_avoidance = false;
    ExpeditionRequirements remaining { 0, 0, 0 };
    ExpeditionRequirements missing { 0, 0, 0 };
    std::vector<std::string> remaining_available_ids;
    std::vector<ExpeditionCandidateAssessment> assessments;
    std::string reason;
};

// E1 -> account-unlocked E2 proposal only. UI identity/selection, removal and
// next-floor promotion receipts remain the caller's responsibility. E0 return
// phases and ending-three beacon actions are outside this contract.
[[nodiscard]] ExpeditionDecision
    choose_expedition_candidate(const ExpeditionContext& context, const std::vector<ExpeditionCandidate>& candidates);
}
