#pragma once
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace asst::spa
{
using BoardCell = std::pair<int, int>; // Column, row; rows increase downwards.

struct DeploymentUnit
{
    std::string id;
    BoardCell cell;
    char facing = 'E';
    int block_count = 0;
    bool frontline = false;
    bool damage_output = false;
    bool healer = false;
    bool range_known = false;
    std::vector<BoardCell> local_range;     // Canonical east-facing game range: x forward, y upwards.
    std::vector<BoardCell> placement_cells; // Verified terrain-compatible cells for this unit.
    // Optional consumption-chain metadata for pacts such as 阿戈尔. A zero rank means unknown.
    int consumption_chain_rank = 0;
    bool consumes_previous_chain_unit = false;
    bool movable = true;
    bool accepts_direct_healing = true;
    bool provides_regeneration = false;
    // Automatic relocation cannot guarantee occupancy of the preparation
    // tile throughout a wave. Keep physical block_count for consumption.
    bool holds_position = true;
    int boss_basic_attack_weight = 1;
    std::vector<BoardCell> boss_skill_attack_range;
    int boss_skill_attack_uptime_percent = 0;
    std::vector<BoardCell> boss_skill_healing_range;
    bool facing_known = true;
};

struct DeploymentLane
{
    std::string id;
    std::vector<BoardCell> path; // Ordered from enemy spawn towards the protected objective.
    int minimum_engagement_column = 0;
    int precontact_cells = 2;
    // Relative coverage priority, not a measured simultaneous enemy count.
    int blocking_weight = 1;
};

struct LaneCoverage
{
    std::string lane;
    std::string blocker;
    std::vector<std::string> healers;
    std::vector<std::string> damage_support;
    int covered_approach_cells = 0;
    int required_approach_cells = 0;
    bool ready = false;
    std::vector<std::string> problems;
    int block_capacity = 0;
    int first_block_capacity = 0;
};

struct DeploymentReview
{
    bool complete = false;
    bool static_constraints_met = false;
    std::vector<LaneCoverage> lanes;
    std::vector<std::string> problems;
    int exposed_support_units = 0;
    int unique_route_block_capacity = 0;
    // Sixty times the minimum shared-capacity/weight ratio over lane subsets.
    // One body shared by two routes contributes its capacity only once.
    int balanced_block_capacity = 0;
};

struct DeploymentProposal
{
    bool input_complete = false;
    bool found = false;
    int evaluations = 0;
    bool budget_exhausted = false;
    bool improved = false;
    int unblocked_lanes_improvement = 0;
    int score_improvement = 0;
    int preference_improvement = 0;
    int blocking_capacity_improvement = 0;
    std::vector<DeploymentUnit> units;
    std::string reason;
    int boss_coverage_improvement = 0;
    int boss_movement_coverage_improvement = 0;
    int boss_uncovered_waypoints_improvement = 0;
    int boss_weakest_waypoint_improvement = 0;
    int preparation_growth_preference_improvement = 0;
    int boss_estimated_attack_improvement = 0;
    int boss_supported_attack_improvement = 0;
    int boss_sustained_attack_improvement = 0;
    int boss_early_contact_floor_improvement = 0;
    int boss_early_contact_sum_improvement = 0;
};

// Distinct friendly patients covered by a different healer's measured range.
std::vector<std::string> uncovered_healable_allies(const std::vector<DeploymentUnit>& units);

// Geometric basic-attack coverage only, not damage or skill uptime. Multiple
// attackable cells from the same boss count at most once for each attacker.
int stationary_boss_attackers(const std::vector<DeploymentUnit>& units, const std::vector<BoardCell>& boss_cells);
int stationary_boss_attack_weight(const std::vector<DeploymentUnit>& units, const std::vector<BoardCell>& boss_cells);
// Sum modeled attack and healing coverage across authored route checkpoints.
// Each point is a hypothetical position with no known travel or dwell time.
int boss_movement_attack_weight(const std::vector<DeploymentUnit>& units, const std::vector<BoardCell>& waypoints);

// Bounded local search; a proposal is not an executable movement sequence or a proof of optimality.
DeploymentProposal propose_deployment(
    const std::vector<DeploymentUnit>& units,
    const std::vector<DeploymentLane>& lanes,
    const std::vector<BoardCell>& deployable_cells,
    int evaluation_budget = 20'000,
    const std::function<int(const std::vector<DeploymentUnit>&)>& preference = {},
    const std::vector<BoardCell>& opening_boss_cells = {},
    const std::vector<BoardCell>& boss_movement_waypoints = {},
    const std::function<int(const std::vector<DeploymentUnit>&)>& preparation_growth_preference = {},
    std::optional<int> available_input_steps = std::nullopt);

enum class DeploymentStepKind
{
    Move,
    Swap,
    Face
};

struct DeploymentStep
{
    DeploymentStepKind kind = DeploymentStepKind::Move;
    std::string unit;
    std::string displaced;
    BoardCell from;
    BoardCell to;
    char facing = 'E';
};

// Review a healer against the present friendly formation, including a new
// position after a swap. Legal cells must come from the registered board.
std::optional<DeploymentStep> repair_healing_coverage(
    const DeploymentUnit& healer,
    const std::vector<DeploymentUnit>& units,
    const std::vector<DeploymentLane>& lanes,
    const std::vector<BoardCell>& legal_cells);

struct DeploymentSequence
{
    bool complete = false;
    bool budget_exhausted = false;
    int explored_states = 0;
    std::vector<DeploymentStep> steps;
    std::string reason;
};

// Preference points are planner units, not measured battle damage or input
// latency. Apply this hysteresis only to the generic coverage planner, never
// to a pact's explicit mechanism sequence such as an Egir consumption chain.
int deployment_preference_input_cost(const DeploymentSequence& sequence);
// A swap can leave the displaced operator's facing unobservable until a repair.
int deployment_input_steps_with_facing_repairs(const DeploymentSequence& sequence);
bool deployment_change_worth_input(const DeploymentProposal& proposal, const DeploymentSequence& sequence);

DeploymentSequence sequence_deployment(
    const std::vector<DeploymentUnit>& current,
    const std::vector<DeploymentUnit>& target,
    int state_budget = 5000);

bool range_covers(const DeploymentUnit& unit, BoardCell target);

inline bool boss_attack_overlaps_cell(const DeploymentUnit& unit, BoardCell cell)
{
    return unit.cell == cell || range_covers(unit, cell);
}

DeploymentReview review_deployment(
    const std::vector<DeploymentUnit>& units,
    const std::vector<DeploymentLane>& lanes,
    const std::vector<BoardCell>& deployable_cells);
}
