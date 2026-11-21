#include "Task/MiniGame/SPA/SPADeploymentPlanCache.h"
#include "Task/MiniGame/SPA/SPAReplacementPolicy.h"
#include <cassert>
#include <iostream>

using namespace asst::spa;

static DeploymentUnit unit(const std::string& name, BoardCell cell, char facing = 'E', bool movable = true)
{
    DeploymentUnit result;
    result.id = name;
    result.cell = cell;
    result.facing = facing;
    result.movable = movable;
    result.placement_cells = {
        { 6, 1 }, { 7, 1 }, { 7, 2 }, { 7, 3 }, { 8, 1 }, { 8, 2 }, { 8, 3 }, { 8, 4 }, { 9, 4 }
    };
    if (!movable) {
        result.placement_cells = { cell };
    }
    return result;
}

int main()
{
    // Archived quick4-resume1.log R4: lines 10150/10569 record Card's
    // (7,1)->(8,3) swap and Mousse at (7,1) with unknown facing; line 11124
    // subsequently observes Mousse facing E. The complete original target
    // was not logged. Use an explicit controlled target below, not a claim
    // that these were all the original planner's goals or exact saved frames.
    const std::vector<DeploymentUnit> before { unit("卡达", { 7, 1 }),     unit("夏栎", { 8, 2 }, 'E', false),
                                               unit("慕斯", { 8, 3 }),     unit("折光", { 7, 2 }),
                                               unit("拉普兰德", { 9, 4 }), unit("深靛", { 6, 1 }),
                                               unit("褐果", { 7, 3 }),     unit("霍尔海雅", { 8, 4 }) };
    auto single_move = before;
    single_move[0].cell = { 8, 1 };
    single_move[0].facing = 'N';
    const auto move_plan = sequence_deployment(before, single_move);
    assert(move_plan.complete && move_plan.steps.size() == 1);
    assert(move_plan.steps.front().kind == DeploymentStepKind::Move && move_plan.steps.front().facing == 'N');
    auto single_swap = before;
    std::swap(single_swap[0].cell, single_swap[2].cell);
    const auto swap_plan = sequence_deployment(before, single_swap);
    assert(swap_plan.complete && swap_plan.steps.size() == 2);
    assert(swap_plan.steps[0].kind == DeploymentStepKind::Swap && swap_plan.steps[1].kind == DeploymentStepKind::Face);
    assert(swap_plan.steps[1].unit == swap_plan.steps[0].displaced);
    auto target = before;
    target[0].cell = { 8, 3 };
    target[2].cell = { 8, 1 };
    const DeploymentStep swapped { DeploymentStepKind::Swap, "卡达", "慕斯", { 7, 1 }, { 8, 3 }, 'E' };
    const auto expected = deployment_cells_after_step(before, swapped);
    assert(expected && expected->at("卡达") == BoardCell(8, 3) && expected->at("慕斯") == BoardCell(7, 1));
    auto after = before;
    after[0].cell = { 8, 3 };
    after[2].cell = { 7, 1 };
    after[2].movable = false;
    after[2].placement_cells = { after[2].cell };
    const std::set<std::string> known { "卡达", "折光", "拉普兰德", "深靛", "褐果", "霍尔海雅" };
    assert(!sequence_deployment(after, target, 1500).complete);
    auto decision = continue_deployment_plan(after, target, known, *expected, true);
    assert(decision.kind == DeploymentPlanContinuationKind::RestoreFacing && decision.step);
    assert(decision.step->kind == DeploymentStepKind::Face && decision.step->unit == "慕斯");
    assert(decision.step->from == BoardCell(7, 1) && decision.step->to == BoardCell(7, 1));
    assert(decision.step->facing == 'E');
    // Native execution still accepts the explicit planned direction while
    // retaining an unknown observed source direction in its failure key.
    const std::vector<GridPlacementMember> observed_field { { 7, 1, PlacementRole::Frontline, "慕斯", '\0' },
                                                            { 8, 3, PlacementRole::Ranged, "卡达", 'E' } };
    const std::map<std::string, bool> forms { { "慕斯", false }, { "卡达", false } };
    const auto failure_key = spatial_action_failure_key("m01#normal", *decision.step, observed_field, forms);
    assert(failure_key && failure_key->find("慕斯:7.1:initial:unknown") != std::string::npos);
    assert(spatial_receiver_failure_key("m01#normal", *decision.step, observed_field, forms));
    assert(!after[2].movable && !known.contains("慕斯")); // No evidence mutated by future-state reasoning.
    assert(deployment_cells_after_step(after, *decision.step) == expected);

    auto restored = after;
    restored[2].movable = true;
    restored[2].placement_cells = before[2].placement_cells;
    auto known_restored = known;
    known_restored.insert("慕斯");
    decision = continue_deployment_plan(restored, target, known_restored, *expected, true);
    assert(decision.kind == DeploymentPlanContinuationKind::Step && decision.step);
    assert(decision.step->unit == "慕斯" && decision.step->kind == DeploymentStepKind::Move);
    assert(decision.step->to == BoardCell(8, 1)); // Same goal; Card does not get moved a second time.
    const auto expected_final = deployment_cells_after_step(restored, *decision.step);
    auto final = restored;
    final[2].cell = { 8, 1 };
    assert(expected_final);
    decision = continue_deployment_plan(final, target, known_restored, *expected_final, true);
    assert(decision.kind == DeploymentPlanContinuationKind::Complete && !decision.step);
    // Initial unknown Summer was never granted an arbitrary Face action.
    assert(!known_restored.contains("夏栎"));
    auto newly_seen = final;
    newly_seen[1].facing = 'N';
    newly_seen[1].movable = true;
    newly_seen[1].placement_cells = before[0].placement_cells;
    auto all_known = known_restored;
    all_known.insert("夏栎");
    decision = continue_deployment_plan(newly_seen, target, all_known, *expected_final, true);
    assert(decision.kind == DeploymentPlanContinuationKind::Complete && !decision.step);

    decision = continue_deployment_plan(after, target, known, *expected, false);
    assert(decision.kind == DeploymentPlanContinuationKind::Invalidated && !decision.step);
    auto external_move = after;
    external_move[3].cell = { 8, 1 };
    decision = continue_deployment_plan(external_move, target, known, *expected, true);
    assert(decision.kind == DeploymentPlanContinuationKind::Invalidated && !decision.step);
    auto missing = after;
    missing.pop_back();
    decision = continue_deployment_plan(missing, target, known, *expected, true);
    assert(decision.kind == DeploymentPlanContinuationKind::Invalidated && !decision.step);
    auto duplicate = after;
    duplicate[3].id = duplicate[0].id;
    assert(!deployment_cell_ledger(duplicate));
    auto invalid_target = target;
    invalid_target[2].placement_cells = { { 8, 1 } };
    decision = continue_deployment_plan(after, invalid_target, known, *expected, true);
    assert(decision.kind == DeploymentPlanContinuationKind::Invalidated && !decision.step);

    // Budget exhaustion keeps the goal and yields no action. It must not
    // trigger another optimizer run or prevent the caller from getting Ready.
    decision = continue_deployment_plan(after, target, known, *expected, true, 1);
    assert(decision.kind == DeploymentPlanContinuationKind::Deferred && !decision.step);
    auto illegal_step = swapped;
    illegal_step.displaced = "夏栎";
    assert(!deployment_cells_after_step(before, illegal_step));
    illegal_step = swapped;
    illegal_step.kind = DeploymentStepKind::Move;
    illegal_step.displaced.clear();
    assert(!deployment_cells_after_step(before, illegal_step));

    // At the target cell an unknown facing cannot be accepted merely because
    // the legacy unit field carries its fallback E. A planned N remains N.
    auto north_target = target;
    north_target[2].facing = 'N';
    auto unknown_final = final;
    unknown_final[2].movable = false;
    unknown_final[2].placement_cells = { unknown_final[2].cell };
    decision = continue_deployment_plan(unknown_final, north_target, known, *expected_final, true);
    assert(decision.kind == DeploymentPlanContinuationKind::RestoreFacing && decision.step->facing == 'N');
    std::cout << "Cached deployment target continuity passed; native integration and live timing remain unverified\n";
}
