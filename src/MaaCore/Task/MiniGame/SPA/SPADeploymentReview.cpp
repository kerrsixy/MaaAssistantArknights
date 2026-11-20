#include "SPADeploymentReview.h"
#include "SPAPlacementPolicy.h"
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <set>
#include <tuple>

bool asst::spa::range_covers(const DeploymentUnit& unit, BoardCell target)
{
    return unit.range_known && grid_range_covers(
                                   unit.cell.first,
                                   unit.cell.second,
                                   unit.facing,
                                   target.first,
                                   target.second,
                                   unit.local_range);
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

static bool can_attack_boss(const asst::spa::DeploymentUnit& unit, const std::vector<asst::spa::BoardCell>& boss_cells)
{
    return unit.damage_output && !unit.healer && unit.holds_position &&
           std::any_of(boss_cells.begin(), boss_cells.end(), [&](const auto cell) {
               return asst::spa::range_covers(unit, cell);
           });
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

asst::spa::DeploymentProposal asst::spa::propose_deployment(
    const std::vector<DeploymentUnit>& units,
    const std::vector<DeploymentLane>& lanes,
    const std::vector<BoardCell>& deployable_cells,
    int evaluation_budget,
    const std::function<int(const std::vector<DeploymentUnit>&)>& preference,
    const std::vector<BoardCell>& stationary_boss_cells)
{
    DeploymentProposal result;
    const auto initial = review_deployment(units, lanes, deployable_cells);
    if (!initial.complete || evaluation_budget < 1 || evaluation_budget > 100'000) {
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
    result.input_complete = true;
    result.units = units;
    const auto score = [&](const std::vector<DeploymentUnit>& candidate) {
        ++result.evaluations;
        const auto review = review_deployment(candidate, lanes, deployable_cells);
        if (!review.complete) {
            return std::tuple { 1'000'000, 1'000'000, 1'000'000, 0, 0, 0 };
        }
        int unblocked = 0;
        int penalty = 0;
        for (const auto& lane : review.lanes) {
            unblocked += lane.blocker.empty() ? 1 : 0;
            penalty += static_cast<int>(lane.problems.size()) * 1000;
            penalty += (lane.required_approach_cells - lane.covered_approach_cells) * 100;
        }
        // Missing a blocker must not look cheaper than a blocked lane that
        // still needs healing and damage support during early development.
        return std::tuple { unblocked,
                            review.exposed_support_units,
                            -stationary_boss_attack_weight(candidate, stationary_boss_cells),
                            penalty,
                            -review.balanced_block_capacity,
                            preference ? -preference(candidate) : 0 };
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
                    if (cell == result.units[index].cell && direction == result.units[index].facing) {
                        continue;
                    }
                    auto candidate = result.units;
                    const auto occupied =
                        std::find_if(candidate.begin(), candidate.end(), [&](const DeploymentUnit& unit) {
                            return unit.cell == cell;
                        });
                    if (occupied != candidate.end() && occupied->id != candidate[index].id) {
                        if (!occupied->movable || !accepts(*occupied, candidate[index].cell)) {
                            continue;
                        }
                        occupied->cell = candidate[index].cell;
                    }
                    candidate[index].cell = cell;
                    candidate[index].facing = direction;
                    const auto value = score(candidate);
                    if (value < next_score) {
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
    result.unblocked_lanes_improvement = std::get<0>(initial_score) - std::get<0>(best_score);
    result.score_improvement = (std::get<1>(initial_score) - std::get<1>(best_score)) * 10'000 +
                               std::get<3>(initial_score) - std::get<3>(best_score);
    result.blocking_capacity_improvement = std::get<4>(initial_score) - std::get<4>(best_score);
    result.boss_coverage_improvement = std::get<2>(initial_score) - std::get<2>(best_score);
    result.preference_improvement = std::get<5>(initial_score) - std::get<5>(best_score);
    result.improved = best_score < initial_score;
    result.budget_exhausted = result.evaluations >= evaluation_budget;
    result.reason = result.found ? "Static coverage constraints satisfied; movement sequence and live pose unverified"
                                 : "No satisfying layout found by bounded local search; infeasibility is not proven";
    return result;
}

int asst::spa::deployment_preference_input_cost(const DeploymentSequence& sequence)
{
    std::set<std::string> moved;
    int cost = 0;
    for (const auto& step : sequence.steps) {
        if (step.kind != DeploymentStepKind::Face) {
            cost += 250;
            moved.insert(step.unit);
        }
    }
    for (const auto& step : sequence.steps) {
        // The movement executor already commits the incoming unit's facing.
        // Its synthetic final Face step is removed by fresh sequencing after
        // that move, whereas a displaced unit can still need reorientation.
        if (step.kind == DeploymentStepKind::Face && !moved.contains(step.unit)) {
            cost += 100;
        }
    }
    return cost;
}

bool asst::spa::deployment_change_worth_input(const DeploymentProposal& proposal, const DeploymentSequence& sequence)
{
    if (!proposal.input_complete || !proposal.improved || !sequence.complete || sequence.steps.empty()) {
        return false;
    }
    if (proposal.unblocked_lanes_improvement > 0 || proposal.score_improvement > 0 ||
        proposal.blocking_capacity_improvement > 0 || proposal.boss_coverage_improvement > 0) {
        return true;
    }
    // One additional covered approach cell is worth 200 preference points;
    // it cannot alone justify a 250-point relocation. A new lane (2500),
    // actual growth relationship (600/1200), or meaningful frontline advance
    // (800 per tile) can still pay for the required physical changes.
    return proposal.preference_improvement >= deployment_preference_input_cost(sequence);
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
            !accepts(index, destination->cell) || std::string("ESWN").find(unit.facing) == std::string::npos ||
            std::string("ESWN").find(destination->facing) == std::string::npos ||
            (!unit.movable && (unit.cell != destination->cell || unit.facing != destination->facing))) {
            return result;
        }
        start.push_back(unit.cell);
        goal.push_back(destination->cell);
        facings.push_back(destination->facing);
    }

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
            if (!current[index].movable) {
                return;
            }
            auto next = cells;
            const auto found = std::find(next.begin(), next.end(), cell);
            const size_t other = static_cast<size_t>(found - next.begin());
            DeploymentStep step { DeploymentStepKind::Move, current[index].id, {}, cells[index], cell, facings[index] };
            if (other < current.size()) {
                if (other == index || !current[other].movable || !accepts(other, cells[index])) {
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
    // Movement can change either participant's facing. Explicitly restore every moved unit's final facing.
    for (size_t index = 0; index < current.size(); ++index) {
        const bool moved = std::any_of(result.steps.begin(), result.steps.end(), [&](const DeploymentStep& step) {
            return step.unit == current[index].id || step.displaced == current[index].id;
        });
        if (moved || current[index].facing != facings[index]) {
            result.steps.push_back(
                { DeploymentStepKind::Face, current[index].id, {}, goal[index], goal[index], facings[index] });
        }
    }
    result.complete = true;
    result.reason = "Terrain-compatible sequence generated; runtime identity and direction verification still required";
    return result;
}
