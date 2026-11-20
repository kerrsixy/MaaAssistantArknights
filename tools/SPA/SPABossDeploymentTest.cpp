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
    const std::vector<DeploymentUnit> patrol_start { guard, medic, main_output };
    const auto moving_boss = propose_deployment(patrol_start, one_lane, separate_cells, 96, {}, { { 4, 1 } }, patrol);
    assert(moving_boss.units.front().cell == guard.cell && moving_boss.units.back().facing == 'E');
    assert(moving_boss.boss_coverage_improvement == -700 && moving_boss.boss_movement_coverage_improvement == 1400);
    assert(deployment_change_worth_input(moving_boss, sequence_deployment(patrol_start, moving_boss.units)));
    std::cout << "fixed-boss coverage, fallback, guard and cached continuation checks passed\n";
}
