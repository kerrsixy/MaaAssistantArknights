#include "SPADeploymentReview.h"
#include "SPAPlacementPolicy.h"
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <set>
#include <span>
#include <string_view>
#include <tuple>

namespace
{
using namespace asst::spa;

bool covers_for_known_or_all_facings(
    const DeploymentUnit& unit,
    std::span<const BoardCell> targets,
    const std::vector<BoardCell>& range,
    bool own_cell_is_target = false)
{
    const auto covers = [&](char facing) {
        return std::ranges::any_of(targets, [&](const auto target) {
            return (own_cell_is_target && unit.cell == target) ||
                   grid_range_covers(unit.cell.first, unit.cell.second, facing, target.first, target.second, range);
        });
    };
    return unit.facing_known ? covers(unit.facing) : std::ranges::all_of(std::string_view("ESWN"), covers);
}
}

bool asst::spa::range_covers(const DeploymentUnit& unit, BoardCell target)
{
    return unit.range_known && covers_for_known_or_all_facings(unit, std::span(&target, 1), unit.local_range);
}

asst::spa::DeploymentReview asst::spa::review_deployment(
    const std::vector<DeploymentUnit>& units,
    const std::vector<DeploymentLane>& lanes,
    const std::vector<BoardCell>& deployable_cells)
{
    DeploymentReview result;
    std::set<std::string> ids;
    std::set<BoardCell> occupied;
    const std::set<BoardCell> legal(deployable_cells.begin(), deployable_cells.end());
    if (units.empty() || lanes.empty() || lanes.size() > 8 || legal.empty()) {
        result.problems.emplace_back("Missing units, route geometry or deployable-cell evidence");
        return result;
    }
    for (const auto& unit : units) {
        if (unit.id.empty() || !ids.insert(unit.id).second || !occupied.insert(unit.cell).second ||
            !legal.contains(unit.cell) || unit.block_count < 0 ||
            (unit.facing != 'E' && unit.facing != 'S' && unit.facing != 'W' && unit.facing != 'N') ||
            ((unit.healer || unit.damage_output) && (!unit.range_known || unit.local_range.empty()))) {
            result.problems.emplace_back(
                "Invalid placement, duplicate identity, blocked cell or unknown effective range");
            return result;
        }
    }
    std::set<std::string> lane_ids;
    for (const auto& lane : lanes) {
        if (lane.id.empty() || !lane_ids.insert(lane.id).second || lane.path.size() < 2 || lane.precontact_cells < 1 ||
            lane.precontact_cells > 5 || lane.blocking_weight < 1 || lane.blocking_weight > 100) {
            result.problems.emplace_back("Invalid route identity or approach requirement");
            return result;
        }
        std::set<BoardCell> visited;
        for (size_t index = 0; index < lane.path.size(); ++index) {
            if (!visited.insert(lane.path[index]).second ||
                (index > 0 && std::abs(lane.path[index].first - lane.path[index - 1].first) +
                                      std::abs(lane.path[index].second - lane.path[index - 1].second) !=
                                  1)) {
                result.problems.emplace_back("Route cells are contradictory or not contiguous");
                return result;
            }
        }
    }
    result.complete = true;
    result.static_constraints_met = true;
    std::vector<unsigned> route_membership;
    for (const auto& unit : units) {
        unsigned mask = 0;
        if (unit.frontline && unit.holds_position && !unit.healer && unit.block_count > 0) {
            for (size_t index = 0; index < lanes.size(); ++index) {
                if (std::find(lanes[index].path.begin(), lanes[index].path.end(), unit.cell) !=
                    lanes[index].path.end()) {
                    mask |= 1U << index;
                }
            }
        }
        route_membership.push_back(mask);
        if (mask) {
            result.unique_route_block_capacity += unit.block_count;
        }
    }
    result.balanced_block_capacity = std::numeric_limits<int>::max();
    for (unsigned subset = 1; subset < (1U << lanes.size()); ++subset) {
        int weight = 0, capacity = 0;
        for (size_t index = 0; index < lanes.size(); ++index) {
            if (subset & (1U << index)) {
                weight += lanes[index].blocking_weight;
            }
        }
        for (size_t index = 0; index < units.size(); ++index) {
            if (route_membership[index] & subset) {
                capacity += units[index].block_count;
            }
        }
        result.balanced_block_capacity = std::min(result.balanced_block_capacity, 60 * capacity / weight);
    }
    for (const auto& lane : lanes) {
        LaneCoverage report;
        report.lane = lane.id;
        const DeploymentUnit* blocker = nullptr;
        size_t contact = lane.path.size();
        for (const auto& unit : units) {
            // A medic's incidental block count is not accepted as a frontline assignment.
            if (!unit.frontline || !unit.holds_position || unit.healer || unit.block_count <= 0) {
                continue;
            }
            const auto found = std::find(lane.path.begin(), lane.path.end(), unit.cell);
            const auto index = static_cast<size_t>(found - lane.path.begin());
            if (found != lane.path.end()) {
                report.block_capacity += unit.block_count;
            }
            if (index < contact) {
                blocker = &unit;
                contact = index;
            }
        }
        if (!blocker) {
            report.problems.emplace_back("No frontline blocker on the actual enemy route");
        }
        else {
            report.blocker = blocker->id;
            report.first_block_capacity = blocker->block_count;
            for (const auto& unit : units) {
                if (unit.id == blocker->id || unit.block_count <= 0) {
                    continue;
                }
                const auto encountered = std::find(lane.path.begin(), lane.path.end(), unit.cell);
                if (static_cast<size_t>(encountered - lane.path.begin()) < contact) {
                    if (!unit.frontline || unit.healer) {
                        ++result.exposed_support_units;
                    }
                    report.problems.emplace_back(
                        "Another blocking unit intercepts enemies before the assigned frontline");
                }
            }
            if (blocker->cell.first < lane.minimum_engagement_column) {
                report.problems.emplace_back("Engagement line is behind the required forward position");
            }
            for (const auto& unit : units) {
                if (unit.healer && (blocker->accepts_direct_healing || unit.provides_regeneration) &&
                    range_covers(unit, blocker->cell)) {
                    report.healers.emplace_back(unit.id);
                }
            }
            if (report.healers.empty()) {
                report.problems.emplace_back(
                    blocker->accepts_direct_healing ? "Frontline blocker has no verified healing-range coverage"
                                                    : "Unhealable frontline has no verified regeneration coverage");
            }
            report.required_approach_cells = lane.precontact_cells;
            std::set<std::string> support;
            if (contact < static_cast<size_t>(lane.precontact_cells)) {
                report.problems.emplace_back("Insufficient verified route cells before contact");
            }
            else {
                for (int step = 1; step <= lane.precontact_cells; ++step) {
                    bool covered = false;
                    const auto cell = lane.path[contact - step];
                    for (const auto& unit : units) {
                        if (unit.id != blocker->id && unit.damage_output && !unit.healer && range_covers(unit, cell)) {
                            support.insert(unit.id);
                            covered = true;
                        }
                    }
                    report.covered_approach_cells += covered ? 1 : 0;
                }
                if (report.covered_approach_cells != lane.precontact_cells) {
                    report.problems.emplace_back("Damage support starts too late on the enemy approach");
                }
            }
            report.damage_support.assign(support.begin(), support.end());
        }
        report.ready = report.problems.empty();
        result.static_constraints_met = result.static_constraints_met && report.ready;
        result.lanes.emplace_back(std::move(report));
    }
    return result;
}

std::vector<std::string> asst::spa::uncovered_healable_allies(const std::vector<DeploymentUnit>& units)
{
    std::vector<std::string> uncovered;
    for (const auto& ally : units) {
        bool can_receive_healing = !ally.healer && ally.accepts_direct_healing;
        bool covered = false;
        for (const auto& healer : units) {
            if (!healer.healer || healer.id == ally.id ||
                (!ally.accepts_direct_healing && !healer.provides_regeneration)) {
                continue;
            }
            can_receive_healing = true;
            if (range_covers(healer, ally.cell)) {
                covered = true;
                break;
            }
        }
        if (can_receive_healing && !covered) {
            uncovered.push_back(ally.id);
        }
    }
    return uncovered;
}

std::optional<asst::spa::DeploymentStep> asst::spa::repair_healing_coverage(
    const DeploymentUnit& healer,
    const std::vector<DeploymentUnit>& units,
    const std::vector<DeploymentLane>& lanes,
    const std::vector<BoardCell>& legal_cells)
{
    if (!healer.healer || healer.local_range.empty() || lanes.empty() || legal_cells.empty()) {
        return std::nullopt;
    }
    std::vector<std::pair<BoardCell, int>> recipients;
    for (const auto& unit : units) {
        if (unit.id == healer.id || (!unit.accepts_direct_healing && !healer.provides_regeneration)) {
            continue;
        }
        if (std::any_of(units.begin(), units.end(), [&](const auto& other) {
                return other.healer && other.id != healer.id && other.id != unit.id &&
                       (unit.accepts_direct_healing || other.provides_regeneration) && range_covers(other, unit.cell);
            })) {
            continue;
        }
        const bool on_route = unit.frontline && unit.holds_position && unit.block_count > 0 &&
                              std::any_of(lanes.begin(), lanes.end(), [&](const auto& lane) {
                                  return std::find(lane.path.begin(), lane.path.end(), unit.cell) != lane.path.end();
                              });
        recipients.emplace_back(unit.cell, on_route ? 5000 : unit.frontline ? 550 : 450);
    }
    const auto score = [&](const DeploymentUnit& source) {
        int value = 0;
        for (const auto& [cell, weight] : recipients) {
            if (range_covers(source, cell)) {
                value += weight;
            }
        }
        return value;
    };

    const bool exposed = std::any_of(lanes.begin(), lanes.end(), [&](const auto& lane) {
        const auto position = std::find(lane.path.begin(), lane.path.end(), healer.cell);
        return position != lane.path.end() && std::none_of(units.begin(), units.end(), [&](const auto& unit) {
                   return unit.frontline && unit.holds_position && unit.block_count > 0 &&
                          std::find(lane.path.begin(), position, unit.cell) != position;
               });
    });
    int best_score = score(healer) - (exposed ? 6000 : 0);
    if (healer.range_known && best_score >= 5000) {
        // Avoid moving for a tie. An extra patient still pays for a short
        // safe move when the current frontline remains within reach.
        best_score += 80;
    }
    std::optional<DeploymentStep> best;
    auto candidate = healer;
    candidate.range_known = true;
    candidate.facing_known = true;
    for (const auto& cell : legal_cells) {
        const bool moving = cell != healer.cell;
        if (moving && (std::any_of(units.begin(), units.end(), [&](const auto& unit) { return unit.cell == cell; }) ||
                       std::any_of(lanes.begin(), lanes.end(), [&](const auto& lane) {
                           return std::find(lane.path.begin(), lane.path.end(), cell) != lane.path.end();
                       }))) {
            continue;
        }
        candidate.cell = cell;
        for (const char direction : { 'E', 'N', 'S', 'W' }) {
            candidate.facing = direction;
            const int distance = std::abs(cell.first - healer.cell.first) + std::abs(cell.second - healer.cell.second);
            const int coverage = score(candidate);
            const int value = coverage - (moving ? 250 + distance * 25 : exposed ? 6000 : 0);
            if (coverage > 0 && value > best_score) {
                best_score = value;
                best = DeploymentStep { moving ? DeploymentStepKind::Move : DeploymentStepKind::Face,
                                        healer.id,
                                        {},
                                        healer.cell,
                                        cell,
                                        direction };
            }
        }
    }
    return best;
}

static bool can_attack_boss(const asst::spa::DeploymentUnit& unit, std::span<const asst::spa::BoardCell> boss_cells)
{
    return unit.damage_output && !unit.healer && unit.holds_position &&
           (unit.range_known ? covers_for_known_or_all_facings(unit, boss_cells, unit.local_range, true)
                             : std::ranges::find(boss_cells, unit.cell) != boss_cells.end());
}

int asst::spa::stationary_boss_attackers(
    const std::vector<DeploymentUnit>& units,
    const std::vector<BoardCell>& boss_cells)
{
    return static_cast<int>(
        std::count_if(units.begin(), units.end(), [&](const auto& unit) { return can_attack_boss(unit, boss_cells); }));
}

int asst::spa::stationary_boss_attack_weight(
    const std::vector<DeploymentUnit>& units,
    const std::vector<BoardCell>& boss_cells)
{
    int weight = 0;
    for (const auto& unit : units) {
        if (can_attack_boss(unit, boss_cells)) {
            weight += unit.boss_basic_attack_weight;
        }
    }
    return weight;
}

namespace
{
using namespace asst::spa;

int boss_attack_coverage_weight(const DeploymentUnit& unit, std::span<const BoardCell> cells)
{
    if (!unit.damage_output || unit.healer || !unit.holds_position) {
        return 0;
    }
    const bool basic = can_attack_boss(unit, cells);
    const bool skill = !unit.boss_skill_attack_range.empty() &&
                       covers_for_known_or_all_facings(unit, cells, unit.boss_skill_attack_range, true);
    const int uptime = std::clamp(unit.boss_skill_attack_uptime_percent, 0, 100);
    return unit.boss_basic_attack_weight * (basic * (100 - uptime) + skill * uptime);
}

std::pair<int, int> boss_attack_weights(const std::vector<DeploymentUnit>& units, std::span<const BoardCell> boss_cells)
{
    int total_weight = 0;
    int healed_weight = 0;
    if (boss_cells.empty()) {
        return { total_weight, healed_weight };
    }
    for (const auto& attacker : units) {
        const int attack_weight = boss_attack_coverage_weight(attacker, boss_cells);
        if (attack_weight <= 0) {
            continue;
        }
        total_weight += attack_weight;
        const bool covered = std::any_of(units.begin(), units.end(), [&](const auto& support) {
            if (support.id == attacker.id || (!attacker.accepts_direct_healing && !support.provides_regeneration)) {
                return false;
            }
            return (support.healer && range_covers(support, attacker.cell)) ||
                   (support.holds_position && !support.boss_skill_healing_range.empty() &&
                    covers_for_known_or_all_facings(
                        support, std::span(&attacker.cell, 1), support.boss_skill_healing_range));
        });
        if (covered) {
            healed_weight += attack_weight;
        }
    }
    return { total_weight, healed_weight };
}

int sustained_boss_attack_weight(int opening, int supported)
{
    return opening + 4 * supported;
}

struct BossRouteCoverage
{
    int first_weight = 0;
    int weakest_weight = 0;
    int total_weight = 0;
    int uncovered_waypoints = 0;
};

BossRouteCoverage boss_route_coverage(const std::vector<DeploymentUnit>& units, std::span<const BoardCell> waypoints)
{
    BossRouteCoverage result;
    // These are route checkpoints, with no measured travel time between
    // them. Score local attack access and healing at each known checkpoint.
    for (size_t i = 0; i < waypoints.size(); ++i) {
        const auto& waypoint = waypoints[i];
        const auto [attack, supported] = boss_attack_weights(units, std::span(&waypoint, 1));
        const int weight = sustained_boss_attack_weight(attack, supported);
        if (i == 0) {
            result.first_weight = weight;
            result.weakest_weight = weight;
        }
        result.weakest_weight = std::min(result.weakest_weight, weight);
        result.total_weight += weight / 100;
        result.uncovered_waypoints += weight == 0;
    }
    return result;
}
}

int asst::spa::boss_movement_attack_weight(
    const std::vector<DeploymentUnit>& units,
    const std::vector<BoardCell>& waypoints)
{
    return boss_route_coverage(units, waypoints).total_weight;
}

asst::spa::DeploymentProposal asst::spa::propose_deployment(
    const std::vector<DeploymentUnit>& units,
    const std::vector<DeploymentLane>& lanes,
    const std::vector<BoardCell>& deployable_cells,
    int evaluation_budget,
    const std::function<int(const std::vector<DeploymentUnit>&)>& preference,
    const std::vector<BoardCell>& opening_boss_cells,
    const std::vector<BoardCell>& boss_movement_waypoints,
    const std::function<int(const std::vector<DeploymentUnit>&)>& preparation_growth_preference,
    std::optional<int> available_input_steps)
{
    DeploymentProposal result;
    const auto initial = review_deployment(units, lanes, deployable_cells);
    if (!initial.complete || evaluation_budget < 1 || evaluation_budget > 100'000 ||
        (available_input_steps && *available_input_steps < 0)) {
        result.reason = "Incomplete geometry or invalid search budget";
        return result;
    }
    const std::set<BoardCell> legal(deployable_cells.begin(), deployable_cells.end());
    const auto accepts = [&](const DeploymentUnit& unit, BoardCell cell) {
        return legal.contains(cell) &&
               std::find(unit.placement_cells.begin(), unit.placement_cells.end(), cell) != unit.placement_cells.end();
    };
    for (const auto& unit : units) {
        if (unit.placement_cells.empty() || !accepts(unit, unit.cell) ||
            std::any_of(unit.placement_cells.begin(), unit.placement_cells.end(), [&](BoardCell cell) {
                return !legal.contains(cell);
            })) {
            result.reason = "Missing verified unit-specific deployment terrain";
            return result;
        }
    }

    struct CandidateScore
    {
        int unblocked_lanes = 0;
        int exposed_support = 0;
        bool boss_opening_uncovered = false;
        bool boss_first_contact_uncovered = false;
        int boss_uncovered_waypoints = 0;
        int boss_weakest_waypoint_cost = 0;
        int boss_route_only_cost = 0;
        int boss_early_contact_floor_cost = 0;
        int boss_early_contact_sum_cost = 0;
        int boss_sustained_attack_cost = 0;
        int boss_opening_cost = 0;
        int boss_healed_attack_cost = 0;
        int lost_first_block_capacity = 0;
        int lane_problem_count = 0;
        int lost_block_capacity = 0;
        int first_block_capacity_cost = 0;
        int preparation_growth_cost = 0;
        int approach_penalty = 0;
        int block_capacity_cost = 0;
        int boss_movement_cost = 0;
        int placement_preference_cost = 0;

        auto operator<=>(const CandidateScore&) const = default;
    };

    const bool regular_wave = opening_boss_cells.empty() && boss_movement_waypoints.empty();
    const bool has_early_boss_positions = !opening_boss_cells.empty() && !boss_movement_waypoints.empty();
    const bool prioritize_growth = preparation_growth_preference && regular_wave;
    result.input_complete = true;
    result.units = units;
    const auto score = [&](const std::vector<DeploymentUnit>& candidate) {
        ++result.evaluations;
        const auto review = review_deployment(candidate, lanes, deployable_cells);
        if (!review.complete) {
            return CandidateScore { .unblocked_lanes = 1'000'000, .exposed_support = 1'000'000 };
        }
        int unblocked = 0;
        int lane_problems = 0;
        int uncovered_approach = 0;
        int lost_first_block_capacity = 0;
        int first_block_capacity = 0;
        for (size_t index = 0; index < review.lanes.size(); ++index) {
            const auto& lane = review.lanes[index];
            unblocked += lane.blocker.empty() ? 1 : 0;
            first_block_capacity += lanes[index].blocking_weight * lane.first_block_capacity;
            if (regular_wave && review.balanced_block_capacity <= initial.balanced_block_capacity) {
                // Moving capacity rearward must improve coverage between lanes.
                lost_first_block_capacity +=
                    std::max(0, initial.lanes[index].first_block_capacity - lane.first_block_capacity);
            }
            lane_problems += static_cast<int>(lane.problems.size());
            uncovered_approach += lane.required_approach_cells - lane.covered_approach_cells;
        }
        const auto [boss_opening_weight, boss_supported_weight] = boss_attack_weights(candidate, opening_boss_cells);
        // Keep some value for burst without coverage, but favour keeping attackers
        // in reach of healing. This is a search priority, not predicted survival.
        const int boss_sustained_attack_weight =
            sustained_boss_attack_weight(boss_opening_weight, boss_supported_weight);
        const auto route = boss_route_coverage(candidate, boss_movement_waypoints);
        // Preserve firing access at the opening and first contact. Once
        // those are covered, an empty later checkpoint matters more than
        // adding another attacker to the opening cluster.
        const int early_contact_floor =
            has_early_boss_positions ? std::min(boss_sustained_attack_weight, route.first_weight) : 0;
        const int early_contact_sum = has_early_boss_positions ? boss_sustained_attack_weight + route.first_weight : 0;
        // Preparation-only growth can break a tie in survival coverage.
        // Preserve the original coverage ordering when there is no growth
        // objective. A pair cannot trade away a lane, a healing check or
        // existing balanced block capacity.
        return CandidateScore {
            .unblocked_lanes = unblocked,
            .exposed_support = review.exposed_support_units,
            .boss_opening_uncovered = has_early_boss_positions && boss_sustained_attack_weight == 0,
            .boss_first_contact_uncovered = has_early_boss_positions && route.first_weight == 0,
            .boss_uncovered_waypoints = route.uncovered_waypoints,
            .boss_weakest_waypoint_cost = -route.weakest_weight,
            .boss_route_only_cost = has_early_boss_positions ? 0 : -route.total_weight,
            .boss_early_contact_floor_cost = -early_contact_floor,
            .boss_early_contact_sum_cost = -early_contact_sum,
            .boss_sustained_attack_cost = -boss_sustained_attack_weight,
            .boss_opening_cost = -boss_opening_weight,
            .boss_healed_attack_cost = -boss_supported_weight,
            .lost_first_block_capacity = lost_first_block_capacity,
            .lane_problem_count = prioritize_growth ? lane_problems : 0,
            .lost_block_capacity =
                prioritize_growth ? std::max(0, initial.balanced_block_capacity - review.balanced_block_capacity) : 0,
            .first_block_capacity_cost = regular_wave ? -first_block_capacity : 0,
            .preparation_growth_cost = prioritize_growth ? -preparation_growth_preference(candidate) : 0,
            .approach_penalty = lane_problems * 1000 + uncovered_approach * 100,
            .block_capacity_cost = -review.balanced_block_capacity,
            .boss_movement_cost = has_early_boss_positions ? -route.total_weight : 0,
            .placement_preference_cost = preference ? -preference(candidate) : 0
        };
    };
    auto best_score = score(result.units);
    const auto initial_score = best_score;
    while (result.evaluations < evaluation_budget) {
        auto best = result.units;
        auto next_score = best_score;
        for (size_t index = 0; index < result.units.size() && result.evaluations < evaluation_budget; ++index) {
            if (!result.units[index].movable) {
                continue;
            }
            for (const auto cell : result.units[index].placement_cells) {
                if (result.evaluations >= evaluation_budget) {
                    break;
                }
                for (const char direction : std::string("ESWN")) {
                    if (result.evaluations >= evaluation_budget) {
                        break;
                    }
                    if (cell == result.units[index].cell && result.units[index].facing_known &&
                        direction == result.units[index].facing) {
                        continue;
                    }
                    auto candidate = result.units;
                    const auto occupied =
                        std::find_if(candidate.begin(), candidate.end(), [&](const DeploymentUnit& unit) {
                            return unit.cell == cell;
                        });
                    if (occupied != candidate.end() && occupied->id != candidate[index].id) {
                        if (!occupied->movable || !occupied->facing_known ||
                            !accepts(*occupied, candidate[index].cell)) {
                            continue;
                        }
                        occupied->cell = candidate[index].cell;
                    }
                    candidate[index].cell = cell;
                    candidate[index].facing = direction;
                    candidate[index].facing_known = true;
                    const auto value = score(candidate);
                    if (value < next_score) {
                        if (available_input_steps) {
                            int changed = 0;
                            for (size_t member = 0; member < units.size(); ++member) {
                                changed +=
                                    units[member].cell != candidate[member].cell ||
                                    (candidate[member].facing_known &&
                                     (!units[member].facing_known || units[member].facing != candidate[member].facing));
                            }
                            if (changed > *available_input_steps) {
                                continue;
                            }
                            const auto sequence = sequence_deployment(units, candidate, 1500);
                            if (!sequence.complete ||
                                deployment_input_steps_with_facing_repairs(sequence) > *available_input_steps) {
                                continue;
                            }
                        }
                        next_score = value;
                        best = std::move(candidate);
                    }
                }
            }
        }
        if (next_score >= best_score) {
            break;
        }
        best_score = next_score;
        result.units = std::move(best);
    }
    result.found = review_deployment(result.units, lanes, deployable_cells).static_constraints_met;
    result.unblocked_lanes_improvement = initial_score.unblocked_lanes - best_score.unblocked_lanes;
    result.score_improvement = (initial_score.exposed_support - best_score.exposed_support) * 10'000 +
                               initial_score.approach_penalty - best_score.approach_penalty;
    result.blocking_capacity_improvement = initial_score.block_capacity_cost - best_score.block_capacity_cost;
    result.first_block_capacity_improvement =
        initial_score.first_block_capacity_cost - best_score.first_block_capacity_cost;
    result.boss_movement_coverage_improvement = initial_score.boss_route_only_cost + initial_score.boss_movement_cost -
                                                best_score.boss_route_only_cost - best_score.boss_movement_cost;
    result.boss_uncovered_waypoints_improvement =
        initial_score.boss_uncovered_waypoints - best_score.boss_uncovered_waypoints;
    result.boss_weakest_waypoint_improvement =
        initial_score.boss_weakest_waypoint_cost - best_score.boss_weakest_waypoint_cost;
    result.boss_early_contact_floor_improvement =
        initial_score.boss_early_contact_floor_cost - best_score.boss_early_contact_floor_cost;
    result.boss_early_contact_sum_improvement =
        initial_score.boss_early_contact_sum_cost - best_score.boss_early_contact_sum_cost;
    result.boss_coverage_improvement = stationary_boss_attack_weight(result.units, opening_boss_cells) -
                                       stationary_boss_attack_weight(units, opening_boss_cells);
    result.boss_estimated_attack_improvement = initial_score.boss_opening_cost - best_score.boss_opening_cost;
    result.boss_supported_attack_improvement =
        initial_score.boss_healed_attack_cost - best_score.boss_healed_attack_cost;
    result.boss_sustained_attack_improvement =
        initial_score.boss_sustained_attack_cost - best_score.boss_sustained_attack_cost;
    result.preparation_growth_preference_improvement =
        initial_score.preparation_growth_cost - best_score.preparation_growth_cost;
    result.preference_improvement = initial_score.placement_preference_cost - best_score.placement_preference_cost;
    result.improved = best_score < initial_score;
    result.budget_exhausted = result.evaluations >= evaluation_budget;
    result.reason = result.found ? "Static coverage constraints satisfied; movement sequence and live pose unverified"
                                 : "No satisfying layout found by bounded local search; infeasibility is not proven";
    return result;
}

int asst::spa::deployment_preference_input_cost(const DeploymentSequence& sequence)
{
    int cost = 0;
    for (const auto& step : sequence.steps) {
        cost += step.kind == DeploymentStepKind::Face ? 100 : 250;
    }
    return cost;
}

int asst::spa::deployment_input_steps_with_facing_repairs(const DeploymentSequence& sequence)
{
    std::set<std::string> pending_facing;
    for (const auto& step : sequence.steps) {
        // Any later action by the displaced operator sets its own facing.
        pending_facing.erase(step.unit);
        if (step.kind == DeploymentStepKind::Swap) {
            pending_facing.insert(step.displaced);
        }
    }
    return static_cast<int>(sequence.steps.size() + pending_facing.size());
}

bool asst::spa::deployment_change_worth_input(const DeploymentProposal& proposal, const DeploymentSequence& sequence)
{
    if (!proposal.input_complete || !proposal.improved || !sequence.complete || sequence.steps.empty()) {
        return false;
    }
    if (proposal.unblocked_lanes_improvement > 0 || proposal.score_improvement > 0 ||
        proposal.blocking_capacity_improvement > 0 || proposal.first_block_capacity_improvement > 0 ||
        proposal.boss_movement_coverage_improvement > 0 || proposal.boss_uncovered_waypoints_improvement > 0 ||
        proposal.boss_weakest_waypoint_improvement > 0 || proposal.boss_sustained_attack_improvement > 0 ||
        proposal.boss_early_contact_floor_improvement > 0 || proposal.boss_early_contact_sum_improvement > 0) {
        return true;
    }
    // One additional covered approach cell is worth 200 preference points;
    // it cannot alone justify a 250-point relocation. A new lane (2500),
    // actual growth relationship (600/1200), or meaningful frontline advance
    // (800 per tile) can still pay for the required physical changes.
    return std::max(proposal.preference_improvement, proposal.preparation_growth_preference_improvement) >=
           deployment_preference_input_cost(sequence);
}

asst::spa::DeploymentSequence asst::spa::sequence_deployment(
    const std::vector<DeploymentUnit>& current,
    const std::vector<DeploymentUnit>& target,
    int state_budget)
{
    DeploymentSequence result;
    result.reason = "Incomplete movement input or unit-specific terrain evidence";
    if (current.empty() || current.size() > 20 || current.size() != target.size() || state_budget < 1 ||
        state_budget > 10'000) {
        return result;
    }
    std::set<std::string> ids;
    std::set<BoardCell> occupied;
    std::set<BoardCell> goals;
    std::vector<BoardCell> start;
    std::vector<BoardCell> goal;
    std::vector<char> facings;
    std::vector<bool> target_facings_known;
    const auto accepts = [&](size_t index, BoardCell cell) {
        const auto& cells = current[index].placement_cells;
        return std::find(cells.begin(), cells.end(), cell) != cells.end();
    };
    for (size_t index = 0; index < current.size(); ++index) {
        const auto& unit = current[index];
        const auto destination =
            std::find_if(target.begin(), target.end(), [&](const DeploymentUnit& item) { return item.id == unit.id; });
        if (unit.id.empty() || !ids.insert(unit.id).second || !occupied.insert(unit.cell).second ||
            destination == target.end() || !goals.insert(destination->cell).second || !accepts(index, unit.cell) ||
            !accepts(index, destination->cell) ||
            (std::string("ESWN").find(unit.facing) == std::string::npos && (unit.facing != '\0' || !unit.movable)) ||
            std::string("ESWN").find(destination->facing) == std::string::npos ||
            (!destination->facing_known && unit.cell != destination->cell) ||
            (!unit.movable && (unit.cell != destination->cell || unit.facing != destination->facing ||
                               (destination->facing_known && !unit.facing_known)))) {
            return result;
        }
        start.push_back(unit.cell);
        goal.push_back(destination->cell);
        facings.push_back(destination->facing);
        target_facings_known.push_back(destination->facing_known);
    }
    const auto can_move = [&](size_t index) {
        return current[index].movable && target_facings_known[index];
    };

    struct Node
    {
        std::vector<BoardCell> cells;
        size_t parent;
        DeploymentStep step;
    };

    std::vector<Node> nodes { { start, 0, {} } };
    std::set<std::vector<BoardCell>> visited { start };
    size_t solved = std::numeric_limits<size_t>::max();
    for (size_t head = 0; head < nodes.size(); ++head) {
        ++result.explored_states;
        const auto cells = nodes[head].cells;
        if (cells == goal) {
            solved = head;
            break;
        }
        const auto append = [&](size_t index, BoardCell cell) {
            if (!can_move(index)) {
                return;
            }
            auto next = cells;
            const auto found = std::find(next.begin(), next.end(), cell);
            const size_t other = static_cast<size_t>(found - next.begin());
            DeploymentStep step { DeploymentStepKind::Move, current[index].id, {}, cells[index], cell, facings[index] };
            if (other < current.size()) {
                if (other == index || !can_move(other) || !accepts(other, cells[index])) {
                    return;
                }
                next[other] = cells[index];
                step.kind = DeploymentStepKind::Swap;
                step.displaced = current[other].id;
            }
            next[index] = cell;
            if (visited.contains(next)) {
                return;
            }
            if (nodes.size() >= static_cast<size_t>(state_budget)) {
                result.budget_exhausted = true;
                return;
            }
            visited.insert(next);
            nodes.push_back({ std::move(next), head, std::move(step) });
        };
        for (size_t index = 0; index < current.size(); ++index) {
            if (cells[index] == goal[index]) {
                continue;
            }
            append(index, goal[index]);
            const auto occupant = std::find(cells.begin(), cells.end(), goal[index]);
            if (occupant == cells.end()) {
                continue;
            }
            const size_t other = static_cast<size_t>(occupant - cells.begin());
            // If a direct swap violates terrain restrictions, clear its destination through a verified empty cell.
            if (!accepts(other, cells[index])) {
                for (const auto buffer : current[other].placement_cells) {
                    if (std::find(cells.begin(), cells.end(), buffer) == cells.end()) {
                        append(other, buffer);
                    }
                }
            }
        }
    }
    if (solved >= nodes.size() || nodes[solved].cells != goal) {
        result.reason = "No movement sequence found within the bounded search; infeasibility is not proven";
        return result;
    }
    for (size_t node = solved; node != 0; node = nodes[node].parent) {
        result.steps.push_back(nodes[node].step);
    }
    std::reverse(result.steps.begin(), result.steps.end());
    // Each move commits the actor's planned facing. A unit displaced by
    // its last swap can still need a facing action at its new cell.
    for (size_t index = 0; index < current.size(); ++index) {
        const auto last_move =
            std::find_if(result.steps.rbegin(), result.steps.rend(), [&](const DeploymentStep& step) {
                return step.unit == current[index].id || step.displaced == current[index].id;
            });
        const bool needs_facing = target_facings_known[index] &&
                                  (last_move != result.steps.rend()
                                       ? last_move->displaced == current[index].id
                                       : !current[index].facing_known || current[index].facing != facings[index]);
        if (needs_facing) {
            result.steps.push_back(
                { DeploymentStepKind::Face, current[index].id, {}, goal[index], goal[index], facings[index] });
        }
    }
    result.complete = true;
    result.reason = "Terrain-compatible sequence generated; runtime identity and direction verification still required";
    return result;
}
