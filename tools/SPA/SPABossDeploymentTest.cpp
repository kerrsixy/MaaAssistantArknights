#include "Task/MiniGame/SPA/SPABossPolicy.h"
#include "Task/MiniGame/SPA/SPACombatStats.h"
#include "Task/MiniGame/SPA/SPADeploymentPlanCache.h"
#include "Task/MiniGame/SPA/SPAEngagementPolicy.h"
#include "Task/MiniGame/SPA/SPATerrainModel.h"
#include <cassert>
#include <iostream>
#include <map>

using namespace asst::spa;

namespace spa_boss_deployment_test
{
DeploymentUnit member(
    const std::string& name,
    BoardCell cell,
    char facing,
    bool elite,
    PlacementRole role,
    const TerrainLayoutModel& terrain)
{
    const auto stats = season_combat_stats(name, elite);
    assert(stats);
    DeploymentUnit unit;
    unit.id = name;
    unit.cell = cell;
    unit.facing = facing;
    unit.block_count = stats->block_count;
    unit.frontline = role == PlacementRole::Frontline;
    unit.healer = role == PlacementRole::Healer;
    unit.damage_output = !unit.healer;
    unit.range_known = true;
    unit.holds_position = default_skill_holds_position(name, elite);
    unit.local_range.assign(stats->basic_range.begin(), stats->basic_range.end());
    unit.placement_cells = unit.frontline ? terrain.ground_route : terrain.buildable;
    unit.accepts_direct_healing = name != "折桠";
    unit.provides_regeneration = name == "魔王";
    return unit;
}

void dump(const char* label, const DeploymentProposal& plan, const std::vector<BoardCell>& cells)
{
    std::cout << label << " complete=" << plan.input_complete << " improved=" << plan.improved
              << " attackers=" << stationary_boss_attackers(plan.units, cells) << " score=" << plan.score_improvement
              << " preference=" << plan.preference_improvement << '\n';
    for (const auto& unit : plan.units) {
        std::cout << unit.id << ' ' << unit.cell.first << ',' << unit.cell.second << ' ' << unit.facing << '\n';
    }
}
}

int main()
{
    using namespace spa_boss_deployment_test;
    const auto fixed = boss_family_mobility("“萨米的意志”");
    const auto mobile = boss_family_mobility("假想敌：鳍");
    assert(fixed.known && fixed.stationary_route);
    assert(mobile.known && !mobile.stationary_route);
    assert(!boss_family_mobility("unknown").known);
    // Live D4 m01 advertised 矩兽. Round 14 had its preview but no readable HP bar.
    assert(scheduled_independent_boss_preparation("independent", 14, 14, 4, "矩兽"));
    assert(!scheduled_independent_boss_preparation("independent", 13, 14, 4, "矩兽"));
    assert(!scheduled_independent_boss_preparation("independent", 15, 14, 4, "矩兽"));
    assert(!scheduled_independent_boss_preparation("independent", 14, 14, 4, ""));
    assert(!scheduled_independent_boss_preparation("independent", 14, 14, 0, "矩兽"));
    assert(!scheduled_independent_boss_preparation("joint", 14, 14, 4, "矩兽"));
    const auto solo_normal = independent_boss_variant_for_preparation("independent", 14, 14, 14, false, "numeric_badge");
    const auto solo_hidden =
        independent_boss_variant_for_preparation("independent", 15, 14, 14, true, "independent_hidden_core_phase");
    assert(solo_normal == false);
    assert(solo_hidden == true);
    assert(independent_boss_variant_for_preparation(
               "independent", 15, 14, 5, true, "independent_hidden_core_phase") == true);
    assert(!independent_boss_variant_for_preparation("independent", 15, 14, 4, true, "independent_hidden_core_phase"));
    assert(!independent_boss_variant_for_preparation("independent", 15, 14, 14, false, "numeric_badge"));
    assert(!independent_boss_variant_for_preparation("independent", 15, 14, 14, true, "joint_risk_bonus_phase"));
    assert(!independent_boss_variant_for_preparation("joint", 15, 14, 14, true, "independent_hidden_core_phase"));
    assert(!independent_boss_variant_for_preparation("independent", 13, 14, 14, false, "numeric_badge"));
    const auto normal_pipe = boss_objective("假想敌：管", { 2, 4 }, 14, solo_normal);
    const auto hidden_pipe = boss_objective("假想敌：管", { 2, 4 }, 14, solo_hidden);
    assert(normal_pipe.variant_known && hidden_pipe.variant_known);
    assert((normal_pipe.possible_cells == std::vector<BoardCell> { { 10, 3 } }));
    assert((hidden_pipe.possible_cells == std::vector<BoardCell> { { 9, 3 } }));
    assert(estimate_solo_stationary_boss_footprint(normal_pipe).size() == 15);
    assert(estimate_solo_stationary_boss_footprint(hidden_pipe).size() == 15);
    assert(!boss_objective("“萨米的意志”", { 2, 4 }, 14, solo_hidden).known);
    assert(!boss_family_mobility("“萨米的意志”", solo_hidden).known);
    const auto d4_m01_boss = boss_objective("矩兽", { 2, 4 }, 4);
    assert((estimate_solo_boss_opening_cells(d4_m01_boss) == std::vector<BoardCell> { { 10, 2 } }));
    assert(!d4_m01_boss.movement_waypoints.empty());
    const auto moving_objective = boss_objective("假想敌：鳍", { 2, 4 }, 14);
    assert(moving_objective.known && !moving_objective.stationary_route && !moving_objective.variant_known);
    assert((moving_objective.movement_waypoints == std::vector<BoardCell> { { 9, 1 }, { 3, 4 }, { 9, 3 } }));
    assert(boss_objective("假想敌：管", { 2, 4 }, 14).movement_waypoints.empty());
    const std::vector<BoardCell> cells { { 10, 3 } };
    const auto region = [&](const BossFamilyMobility& mobility,
                            bool phase,
                            const std::string& map,
                            int round,
                            const std::vector<BoardCell>& observed) {
        return verified_stationary_boss_region(mobility, phase, "m01", 9, map, round, observed);
    };
    assert(region(fixed, true, "m01", 9, cells) == cells);
    assert(region(fixed, true, "m01", 9, { { 10, 3 }, { 10, 3 } }) == cells);
    assert(region(mobile, true, "m01", 9, cells).empty());
    assert(verified_boss_opening_region(true, "m01", 9, "m01", 9, cells) == cells);
    assert(verified_boss_opening_region(true, "m01", 9, "m01", 8, cells).empty());
    assert((
        verified_boss_opening_region(true, "m01", 9, "m01", 9, { { 12, 3 } }) == std::vector<BoardCell> { { 12, 3 } }));
    assert(region({}, true, "m01", 9, cells).empty());
    assert(region(fixed, false, "m01", 9, cells).empty());
    assert(region(fixed, true, "m02", 9, cells).empty());
    assert(region(fixed, true, "m01", 8, cells).empty());
    assert(region(fixed, true, "m01", 10, cells).empty());
    assert(region(fixed, true, "m01", 9, {}).empty());
    assert(region(fixed, true, "m01", 9, { { 13, 3 } }).empty());
    assert(region(fixed, true, "m01", 9, { { 10, -1 } }).empty());
    assert(region(fixed, true, "m01", 9, { { 10, 3 }, { 13, 3 } }).empty());
    const auto terrain = terrain_layout_model("m01", true);
    assert(terrain);
    // The regulation 4 round-14 Cocoon was advertised without a variant.
    // Both client variants translate to the same cell on the registered map.
    const auto cocoon = boss_objective("假想敌：茧", terrain->lanes.front().path.back(), 4);
    assert(cocoon.known && cocoon.stationary_route && !cocoon.variant_known);
    assert(cocoon.possible_cells == cells);
    const auto cocoon_footprint = estimate_solo_stationary_boss_footprint(cocoon);
    assert(cocoon_footprint.size() == 15);
    assert(cocoon_footprint.front() == BoardCell(8, 2) && cocoon_footprint.back() == BoardCell(12, 4));
    assert(estimate_solo_stationary_boss_footprint(moving_objective).empty());
    assert(estimate_solo_boss_opening_cells(cocoon) == cocoon_footprint);
    assert((estimate_solo_boss_opening_cells(moving_objective) == std::vector<BoardCell> { { 10, 2 } }));
    auto ambiguous_opening = moving_objective;
    ambiguous_opening.possible_cells.emplace_back(11, 3);
    assert(estimate_solo_boss_opening_cells(ambiguous_opening).empty());
    ambiguous_opening.possible_cells = { { 13, 2 } };
    assert(estimate_solo_boss_opening_cells(ambiguous_opening).empty());
    assert(estimate_solo_stationary_boss_footprint(boss_objective("假想敌：管", { 2, 4 }, 14)).empty());
    assert(estimate_solo_stationary_boss_footprint(boss_objective("Unknown", { 2, 4 }, 4)).empty());
    const std::map<std::string, PlacementRole> roles {
        { "乌啾", PlacementRole::Healer },    { "冻河", PlacementRole::Ranged }, { "折桠", PlacementRole::Frontline },
        { "烈夏", PlacementRole::Frontline }, { "真理", PlacementRole::Ranged }, { "空弦", PlacementRole::Ranged },
        { "维伊", PlacementRole::Ranged },    { "魔王", PlacementRole::Healer }
    };
    // Same roster/forms as batch1's final eight-body field (events1911).
    // This is a constructed registered-grid scenario, not a claim that the
    // archived quick round had this boss region or these verified directions.
    std::vector<DeploymentUnit> units { member("乌啾", { 7, 3 }, 'N', false, roles.at("乌啾"), *terrain),
                                        member("冻河", { 6, 2 }, 'E', false, roles.at("冻河"), *terrain),
                                        member("折桠", { 6, 1 }, 'E', true, roles.at("折桠"), *terrain),
                                        member("烈夏", { 8, 1 }, 'E', true, roles.at("烈夏"), *terrain),
                                        member("真理", { 5, 1 }, 'E', false, roles.at("真理"), *terrain),
                                        member("空弦", { 7, 1 }, 'E', false, roles.at("空弦"), *terrain),
                                        member("维伊", { 7, 2 }, 'E', false, roles.at("维伊"), *terrain),
                                        member("魔王", { 3, 1 }, 'E', false, roles.at("魔王"), *terrain) };
    // Their directions were still unknown in the archived field; generic
    // layout planning must neither invent coverage nor move those bodies.
    for (auto& unit : units) {
        if (unit.id == "真理" || unit.id == "空弦" || unit.id == "维伊") {
            unit.range_known = false;
            unit.damage_output = false;
            unit.local_range.clear();
            unit.movable = false;
            unit.placement_cells = { unit.cell };
        }
    }
    const auto preference = [&](const std::vector<DeploymentUnit>& candidate) {
        int result = 0;
        for (const auto& unit : candidate) {
            result +=
                engagement_position_value(roles.at(unit.id), unit.cell, unit.facing, unit.local_range, terrain->lanes);
        }
        return result;
    };
    const auto old = propose_deployment(units, terrain->lanes, terrain->buildable, 4000, preference);
    const auto next = propose_deployment(old.units, terrain->lanes, terrain->buildable, 4000, preference, cells);
    assert(old.input_complete && !old.improved && stationary_boss_attackers(old.units, cells) == 0);
    assert(next.input_complete && next.improved && next.boss_coverage_improvement == 1);
    assert(stationary_boss_attackers(next.units, cells) == 1);
    const auto before = review_deployment(old.units, terrain->lanes, terrain->buildable);
    const auto after = review_deployment(next.units, terrain->lanes, terrain->buildable);
    assert(before.complete && after.complete && before.lanes.size() == after.lanes.size());
    assert(after.balanced_block_capacity >= before.balanced_block_capacity);
    assert(after.exposed_support_units <= before.exposed_support_units);
    for (std::size_t index = 0; index < before.lanes.size(); ++index) {
        assert(!before.lanes[index].blocker.empty());
        assert(after.lanes[index].blocker == before.lanes[index].blocker);
        assert(after.lanes[index].covered_approach_cells >= before.lanes[index].covered_approach_cells);
        assert(after.lanes[index].healers.size() >= before.lanes[index].healers.size());
    }
    dump("old", old, cells);
    dump("next", next, cells);
    const auto sequence = sequence_deployment(old.units, next.units);
    assert(sequence.complete && !sequence.steps.empty());
    assert(deployment_change_worth_input(next, sequence));
    const auto expected = deployment_cells_after_step(old.units, sequence.steps.front());
    assert(expected);
    auto observed = old.units;
    std::set<std::string> known;
    for (auto& unit : observed) {
        unit.cell = expected->at(unit.id);
        if (unit.movable) {
            known.insert(unit.id);
        }
        if (unit.id == sequence.steps.front().unit) {
            unit.facing = sequence.steps.front().facing;
        }
    }
    const auto continued = continue_deployment_plan(observed, next.units, known, *expected, true);
    assert(continued.kind != DeploymentPlanContinuationKind::Invalidated);
    assert(continued.kind != DeploymentPlanContinuationKind::Deferred);
    std::cout << "sequence=" << sequence.complete << " steps=" << sequence.steps.size() << '\n';

    // No usable target is exactly the legacy path, including search ordering
    // and bounded evaluations; not merely the same count of attackers.
    const auto no_target = propose_deployment(units, terrain->lanes, terrain->buildable, 4000, preference, {});
    assert(deployment_cell_ledger(no_target.units) == deployment_cell_ledger(old.units));
    assert(no_target.evaluations == old.evaluations && no_target.improved == old.improved);
    assert(no_target.score_improvement == old.score_improvement);
    assert(no_target.preference_improvement == old.preference_improvement);
    for (std::size_t index = 0; index < old.units.size(); ++index) {
        assert(no_target.units[index].facing == old.units[index].facing);
    }

    auto output =
        *std::find_if(next.units.begin(), next.units.end(), [](const auto& unit) { return unit.id == "冻河"; });
    assert(stationary_boss_attackers({ output }, { { 10, 3 }, { 9, 3 }, { 10, 3 } }) == 1);
    assert(stationary_boss_attackers({ output }, { output.cell }) == 1);
    output.healer = true;
    assert(stationary_boss_attackers({ output }, cells) == 0);
    output.healer = false;
    output.holds_position = false;
    assert(stationary_boss_attackers({ output }, cells) == 0);
    output.holds_position = true;
    output.range_known = false;
    assert(stationary_boss_attackers({ output }, cells) == 0);

    // A real Lord could reach this boss only by abandoning the sole ground
    // route. Boss preference cannot compensate for losing that blocker.
    auto guard = member("烈夏", { 3, 1 }, 'E', true, PlacementRole::Frontline, *terrain);
    guard.placement_cells = { { 3, 1 }, { 3, 3 } };
    const std::vector<DeploymentLane> one_lane { { "one", { { 5, 1 }, { 4, 1 }, { 3, 1 }, { 2, 1 } }, 0, 2, 1 } };
    const auto keep_gate = propose_deployment({ guard }, one_lane, guard.placement_cells, 4000, {}, { { 4, 3 } });
    assert(keep_gate.input_complete && keep_gate.units.front().cell == guard.cell);
    assert(stationary_boss_attackers(keep_gate.units, { { 4, 3 } }) == 0);
    assert(!review_deployment(keep_gate.units, one_lane, guard.placement_cells).lanes.front().blocker.empty());

    // Keep the gate blocked and healed. Give up the ranged unit's small-mob
    // approach coverage when its alternate orientation can hit the boss.
    guard.movable = false;
    guard.placement_cells = { guard.cell };
    auto medic = member("乌啾", { 2, 2 }, 'E', false, PlacementRole::Healer, *terrain);
    medic.placement_cells = { medic.cell };
    medic.local_range = { { 1, 1 } };
    medic.movable = false;
    output.id = "main-output";
    output.range_known = true;
    output.cell = { 4, 2 };
    output.placement_cells = { output.cell };
    output.local_range = { { 1, 0 }, { 1, 1 } };
    output.facing = 'N';
    output.movable = true;
    const std::vector<BoardCell> separate_cells { guard.cell, medic.cell, output.cell };
    const std::vector<DeploymentUnit> separate_start { guard, medic, output };
    const auto prioritize_boss = propose_deployment(separate_start, one_lane, separate_cells, 96, {}, { { 5, 3 } });
    assert(prioritize_boss.improved && prioritize_boss.boss_coverage_improvement == 1);
    assert(prioritize_boss.score_improvement < 0 && prioritize_boss.units.front().cell == guard.cell);
    assert(deployment_change_worth_input(prioritize_boss, sequence_deployment(separate_start, prioritize_boss.units)));
    auto main_output = output;
    main_output.boss_basic_attack_weight = 700;
    const auto weighted_boss =
        propose_deployment({ guard, medic, main_output }, one_lane, separate_cells, 96, {}, { { 5, 3 } });
    assert(weighted_boss.boss_coverage_improvement == 700);
    assert(stationary_boss_attackers(weighted_boss.units, { { 5, 3 } }) == 1);
    const std::vector<BoardCell> patrol { { 5, 2 }, { 5, 1 } };
    auto patrol_guard = guard;
    patrol_guard.damage_output = false;
    const std::vector<DeploymentUnit> patrol_start { patrol_guard, medic, main_output };
    const auto moving_boss = propose_deployment(patrol_start, one_lane, separate_cells, 96, {}, { { 4, 1 } }, patrol);
    assert(moving_boss.units.front().cell == guard.cell && moving_boss.units.back().facing == 'N');
    assert(moving_boss.boss_coverage_improvement == 0 && moving_boss.boss_movement_coverage_improvement == 0);
    const auto route_only = propose_deployment(patrol_start, one_lane, separate_cells, 96, {}, {}, patrol);
    assert(route_only.units.back().facing == 'E' && route_only.boss_movement_coverage_improvement == 1400);
    assert(deployment_change_worth_input(route_only, sequence_deployment(patrol_start, route_only.units)));

    // A boss may reach the first checkpoint and stay there. North covers
    // both early positions, even though east covers more later checkpoints.
    const std::vector<BoardCell> first_contact_then_patrol { { 3, 1 }, { 5, 2 }, { 5, 1 } };
    const auto early_contact = propose_deployment(
        route_only.units, one_lane, separate_cells, 96, {}, { { 4, 1 } }, first_contact_then_patrol);
    assert(early_contact.units.front().cell == guard.cell && early_contact.units.back().facing == 'N');
    assert(early_contact.boss_early_contact_floor_improvement > 0 &&
           early_contact.boss_early_contact_sum_improvement > 0);
    assert(early_contact.boss_movement_coverage_improvement == -700);
    assert(deployment_change_worth_input(early_contact, sequence_deployment(route_only.units, early_contact.units)));
    // The supported attacker shares the only useful firing tile with a
    // stronger attacker who cannot be healed by this medic.
    auto covered_attacker = main_output;
    covered_attacker.id = "covered";
    covered_attacker.facing = 'E';
    covered_attacker.local_range = { { 1, 0 } };
    covered_attacker.boss_basic_attack_weight = 500;
    auto uncovered_attacker = covered_attacker;
    uncovered_attacker.id = "uncovered";
    uncovered_attacker.cell = { 3, 3 };
    uncovered_attacker.boss_basic_attack_weight = 700;
    uncovered_attacker.accepts_direct_healing = false;
    covered_attacker.placement_cells = uncovered_attacker.placement_cells = { { 4, 2 }, { 3, 3 } };
    medic.local_range.push_back({ 2, 0 });
    const std::vector<BoardCell> boss_firing_cells { { 5, 2 } };
    const std::vector<BoardCell> firing_terrain { guard.cell, medic.cell, { 4, 2 }, { 3, 3 } };
    assert(boss_movement_attack_weight({ covered_attacker }, boss_firing_cells) == 500);
    assert(boss_movement_attack_weight({ medic, covered_attacker }, boss_firing_cells) == 2500);
    auto unobserved_facing = covered_attacker;
    unobserved_facing.facing_known = false;
    unobserved_facing.movable = false;
    assert(!range_covers(unobserved_facing, { 5, 2 }));
    assert(stationary_boss_attackers({ unobserved_facing }, boss_firing_cells) == 0);
    assert(stationary_boss_attackers({ unobserved_facing }, { { 4, 2 } }) == 1);
    const std::vector<BoardCell> surrounding_footprint { { 5, 2 }, { 4, 3 }, { 3, 2 }, { 4, 1 } };
    assert(stationary_boss_attackers({ unobserved_facing }, surrounding_footprint) == 1);
    assert(stationary_boss_attack_weight({ unobserved_facing }, surrounding_footprint) == 500);
    auto after_synthesis = unobserved_facing;
    after_synthesis.id = "伺夜";
    after_synthesis.movable = true;
    const std::vector<DeploymentUnit> boss_board { guard, medic, after_synthesis };
    const auto repair_boss_facing =
        propose_deployment(boss_board, one_lane, firing_terrain, 96, {}, boss_firing_cells, {}, {}, 1);
    assert(repair_boss_facing.improved && repair_boss_facing.boss_coverage_improvement == 500);
    assert(repair_boss_facing.units.back().facing_known && repair_boss_facing.units.back().facing == 'E');
    const auto repair_sequence = sequence_deployment(boss_board, repair_boss_facing.units);
    assert(repair_sequence.complete && repair_sequence.steps.size() == 1);
    assert(repair_sequence.steps.front().kind == DeploymentStepKind::Face);
    assert(repair_sequence.steps.front().unit == "伺夜");
    assert(sequence_deployment(boss_board, boss_board).steps.empty());
    auto unplanned_move = boss_board;
    unplanned_move.back().cell = { 3, 3 };
    assert(!sequence_deployment(boss_board, unplanned_move).complete);
    auto unknown_medic = medic;
    unknown_medic.facing_known = false;
    assert(boss_movement_attack_weight({ unknown_medic, covered_attacker }, boss_firing_cells) == 500);
    auto skill_attacker = covered_attacker;
    skill_attacker.local_range = { { 0, 1 } };
    skill_attacker.boss_skill_attack_range = { { 1, 0 } };
    skill_attacker.boss_skill_attack_uptime_percent = 40;
    assert(boss_movement_attack_weight({ skill_attacker }, boss_firing_cells) == 200);
    assert(boss_movement_attack_weight({ medic, skill_attacker }, boss_firing_cells) == 1000);
    assert(boss_movement_attack_weight({ medic, skill_attacker }, {}) == 0);
    skill_attacker.facing_known = false;
    assert(boss_movement_attack_weight({ medic, skill_attacker }, boss_firing_cells) == 0);
    const auto sustain_plan = propose_deployment(
        { guard, medic, covered_attacker, uncovered_attacker },
        one_lane,
        firing_terrain,
        96,
        {},
        boss_firing_cells);
    assert(sustain_plan.input_complete && sustain_plan.units[2].cell == covered_attacker.cell);
    assert(stationary_boss_attack_weight(sustain_plan.units, boss_firing_cells) == 500);
    const auto without_healing = propose_deployment(
        { guard, covered_attacker, uncovered_attacker },
        one_lane,
        firing_terrain,
        96,
        {},
        boss_firing_cells);
    assert(without_healing.units[2].cell == covered_attacker.cell && without_healing.boss_coverage_improvement == 200);
    assert(without_healing.boss_sustained_attack_improvement > 0);

    const std::vector<DeploymentLane> trap_routes {
        { "north", { { 6, 1 }, { 5, 1 }, { 4, 1 }, { 3, 1 }, { 2, 1 } }, 0, 2, 2 },
        { "south", { { 6, 3 }, { 5, 3 }, { 4, 3 }, { 3, 3 }, { 2, 3 } }, 0, 2, 2 },
    };
    auto north_blocker = guard;
    north_blocker.cell = { 4, 1 };
    auto south_blocker = north_blocker;
    south_blocker.cell = { 4, 3 };
    auto forward_output = covered_attacker;
    forward_output.cell = { 5, 2 };
    forward_output.facing = 'N';
    forward_output.local_range = { { 1, 0 } };
    const std::vector<DeploymentUnit> trap_field { north_blocker, south_blocker, forward_output };
    assert(fear_trap_charge_count(true, 4, 0) == 0);
    assert(fear_trap_charge_count(true, 4, 4) == 4);
    assert(!fear_trap_charge_count(true, 4, 5));
    assert(!fear_trap_charge_count(true, 4, std::nullopt));
    assert(fear_trap_charge_count(false, 4, std::nullopt) == 0);
    assert(fear_trap_charge_count(true, 1, std::nullopt) == 1);
    const auto trap_priority = [&](BoardCell cell, const std::set<BoardCell>& traps = {}) {
        return fear_trap_route_priority(cell, trap_routes, trap_field, traps);
    };
    assert(trap_priority({ 5, 1 }) > trap_priority({ 3, 1 }));
    assert(trap_priority({ 5, 1 }) > trap_priority({ 6, 1 }));
    assert(trap_priority({ 5, 1 }) > trap_priority({ 5, 3 }));
    assert(trap_priority({ 5, 3 }, { { 5, 1 } }) > trap_priority({ 6, 1 }, { { 5, 1 } }));
    assert(!trap_priority({ 5, 1 }, { { 5, 1 } }));
    assert(!trap_priority({ 4, 1 }) && !trap_priority({ 1, 4 }));
    std::cout << "fixed-boss coverage, fallback, guard and cached continuation checks passed\n";
}
