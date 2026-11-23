#include <iostream>

#include "Task/Roguelike/BlackFlow/BlackFlowPlanner.h"
#include "Task/Roguelike/BlackFlow/BlackFlowPolicy.h"

int main()
{
    using namespace asst::blackflow;
    int checks = 0, failures = 0;
    const auto expect = [&](bool value, const char* label) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    ResourceRegistry resources;
    ResolvedPolicy policy;
    ResourceReserve reserve;
    reserve.id = "carry_full_map";
    reserve.resource = "persistent_full_map_movement";
    reserve.minimum = 1;
    policy.reserves.push_back(reserve);
    RunState run;
    run.floor = 3;
    run.resources.action_points = 4;
    run.resources.ingots = 20;
    run.resources.movement_charges[MovementKind::M09] = 1;
    const auto candidate = [](const char* id, MovementKind movement, int duration, int lasting_uses) {
        PolicyCandidate value;
        value.legal = value.safe = true;
        value.move.action_id = id;
        value.move.movement = movement;
        value.estimated_duration = duration;
        value.processing_move_count = lasting_uses;
        value.persistent_processing_move_count = lasting_uses;
        return value;
    };
    auto direct = candidate("direct_full_map", MovementKind::M09, 4, 3);
    auto detour = candidate("walk_then_spend_full_map", MovementKind::Walk, 6, 5);
    const std::string key = "route.resource_after.persistent_full_map_movement";
    direct.facts.set(key, std::int64_t { 0 });
    detour.facts.set(key, std::int64_t { 0 });
    PolicyExecutor executor;
    const auto choose = [&](const std::vector<PolicyCandidate>& values) {
        return executor.choose(policy, {}, {}, run, resources, {}, values);
    };
    const auto selected = [](const PolicyDecision& decision, const char* id) {
        return decision.selected && decision.selected->action_id == id;
    };

    // Run40 compared whole-route rewards with only the first move's inventory.
    // Both routes spend the same reserved item, so a walk prefix is not a
    // preservation advantage. The shorter route also uses fewer other items.
    auto decision = choose({ detour, direct });
    expect(selected(decision, "direct_full_map"), "a walk prefix cannot disguise later depletion");
    expect(decision.eligible_candidates == 2, "both equally depleted routes remain comparable");
    expect(
        decision.released_reserve_ids == std::vector<std::string> { "carry_full_map" },
        "reserve release is explicit when no evaluated route retains the item");
    decision = choose({ direct, detour });
    expect(selected(decision, "direct_full_map"), "candidate input order cannot create a detour preference");

    detour.facts.set(key, std::int64_t { 1 });
    decision = choose({ direct, detour });
    expect(selected(decision, "walk_then_spend_full_map"), "a genuinely preserved full-map use remains valuable");
    expect(decision.released_reserve_ids.empty(), "a satisfiable reserve is retained");
    expect(decision.eligible_candidates == 1, "only actual preservation satisfies the reserve");

    auto forced = direct;
    forced.move.action_id = "only_exit";
    decision = choose({ forced });
    expect(selected(decision, "only_exit"), "the last item may still be used for the only evaluated exit");
    expect(!decision.released_reserve_ids.empty(), "an unavoidable final use is diagnosed");

    direct.facts.erase(key);
    detour.facts.erase(key);
    decision = choose({ direct, detour });
    expect(selected(decision, "walk_then_spend_full_map"), "legacy one-step callers retain their existing semantics");

    policy.reserves.front().resource = "ingots";
    policy.reserves.front().minimum = 10;
    direct.facts.set("route.resource_after.ingots", std::int64_t { 0 });
    detour.facts.set("route.resource_after.ingots", std::int64_t { 0 });
    decision = choose({ direct, detour });
    expect(decision.eligible_candidates == 2, "movement projection cannot invent future spending of currency");
    expect(decision.released_reserve_ids.empty(), "unspent cash still satisfies its immediate reserve");

    policy.reserves.front() = reserve;
    direct.facts.set(key, std::int64_t { 1 });
    detour.facts.set(key, std::int64_t { 1 });
    direct.estimated_duration = detour.estimated_duration = 4;
    decision = choose({ detour, direct });
    expect(selected(decision, "direct_full_map"), "equal rewards and duration prefer fewer persistent uses");

    // Exercise the planner that publishes the terminal resource facts, not
    // only the policy consumer. A cheap walk prefix still needs the sole
    // full-map use later to cross the disconnected graph.
    MapObservationBatch observation;
    observation.floor = 3;
    const std::vector<GridPosition> positions { { 0, 0 }, { 0, 1 }, { 1, 2 }, { 1, 3 } };
    for (std::size_t index = 0; index < positions.size(); ++index) {
        ObservedNode node;
        node.position = positions[index];
        node.type = index == 3 ? NodeType::Final : NodeType::Incident;
        node.name = index == 2 ? "reward" : "transit";
        node.identity_state = NodeIdentityState::Classified;
        node.identity_revealed = true;
        node.identity_from_template = true;
        observation.nodes.push_back(node);
    }
    observation.edges.push_back({ positions[0], positions[1], EdgeKnowledge::Confirmed, {} });
    observation.edges.push_back({ positions[2], positions[3], EdgeKnowledge::Confirmed, {} });
    NormalizedMap map;
    std::string error;
    expect(map.merge(observation, &error), "merge a disconnected route fixture");
    ResolvedPolicy route_policy;
    route_policy.reserves.push_back(reserve);
    Milestone reward;
    reward.id = "useful_reward";
    reward.kind = MilestoneKind::Preferred;
    reward.floor_begin = reward.floor_end = 3;
    reward.weight = 12;
    reward.selector.node_names = { "reward" };
    route_policy.milestones.push_back(reward);
    RunState route_run;
    route_run.floor = 3;
    route_run.current_node = *make_stable_node_id(3, positions[0]);
    route_run.resources.action_points = 3;
    route_run.resources.movement_charges[MovementKind::M11] = 1;
    route_run.resources.movement_pieces[MovementKind::M11] = 1;
    FactStore facts;
    MissionState mission;
    BlackFlowPlanRequest request;
    request.map = &map.snapshot();
    request.run = &route_run;
    request.policy = &route_policy;
    request.facts = &facts;
    request.mission = &mission;
    request.route_search.time_budget_ms = 2000;
    BlackFlowPlanner planner;
    auto plan = planner.plan(request);
    expect(static_cast<bool>(plan), "disconnected graph has an evaluated terminal route");
    expect(
        plan && plan.decision.selected->movement == MovementKind::M11 &&
            plan.decision.selected->target == *make_stable_node_id(3, positions[2]),
        "planner avoids a walk detour that merely postpones the same full-map use");
    expect(
        plan && !plan.decision.released_reserve_ids.empty(),
        "planner reports that every evaluated complete route spends the last full-map use");

    observation.edges.push_back({ positions[1], positions[2], EdgeKnowledge::Confirmed, {} });
    expect(map.merge(observation, &error), "connect a walk-only route to the reward and exit");
    plan = planner.plan(request);
    expect(
        plan && plan.decision.selected->movement == MovementKind::Walk,
        "a real walk-only route retains the item and uses expiring floor action points");

    // An unconstrained high-reward continuation spends all three teleports.
    // Rejecting its first move must not discard another continuation from that
    // same move which collects rewards and still retains a teleport.
    MapObservationBatch choice_map;
    choice_map.floor = 2;
    const std::vector<GridPosition> choice_positions { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 2, 0 }, { 2, 2 } };
    for (std::size_t index = 0; index < choice_positions.size(); ++index) {
        ObservedNode node;
        node.position = choice_positions[index];
        node.type = index == 2 ? NodeType::Final : NodeType::Incident;
        node.name = index == 0 ? "start" : index == 2 ? "exit" : "valuable_reward";
        node.identity_state = NodeIdentityState::Classified;
        node.identity_revealed = node.identity_from_template = true;
        choice_map.nodes.push_back(node);
    }
    choice_map.edges.push_back({ choice_positions[0], choice_positions[1], EdgeKnowledge::Confirmed, {} });
    choice_map.edges.push_back({ choice_positions[1], choice_positions[2], EdgeKnowledge::Confirmed, {} });
    NormalizedMap choices;
    expect(choices.merge(choice_map, &error), "merge reward alternatives with a conserving walk continuation");
    RunState choice_run;
    choice_run.floor = 2;
    choice_run.current_node = *make_stable_node_id(2, choice_positions[0]);
    choice_run.resources.action_points = 6;
    choice_run.resources.movement_charges[MovementKind::M11] = 3;
    choice_run.resources.movement_pieces[MovementKind::M11] = 1;
    ResolvedPolicy choice_policy;
    choice_policy.reserves.push_back(reserve);
    Milestone valuable;
    valuable.id = "profitable_reward";
    valuable.kind = MilestoneKind::Preferred;
    valuable.floor_begin = valuable.floor_end = 2;
    valuable.required_count = 99;
    valuable.weight = 20;
    valuable.selector.node_names = { "valuable_reward" };
    choice_policy.milestones.push_back(valuable);
    BlackFlowPlanRequest choice_request;
    choice_request.map = &choices.snapshot();
    choice_request.run = &choice_run;
    choice_request.policy = &choice_policy;
    choice_request.facts = &facts;
    choice_request.mission = &mission;
    choice_request.route_search.time_budget_ms = 10'000;
    choice_request.route_search.expansions_per_root = 256;
    const auto conserving = planner.plan(choice_request);
    expect(static_cast<bool>(conserving), "reward comparison has an exit witness");
    expect(
        conserving && conserving.decision.selected->target != *make_stable_node_id(2, choice_positions[2]),
        "an unconstrained continuation must not force immediate exit while a conserving reward route exists");
    const auto progress = conserving.decision.planned_milestone_progress.find(valuable.id);
    expect(
        conserving && progress != conserving.decision.planned_milestone_progress.end() && progress->second >= 2,
        "reserve-aware continuation obtains two rewards before leaving");
    int full_map_uses = 0;
    for (const auto& step : conserving.decision.planned_route_steps) {
        full_map_uses += step.move.movement == MovementKind::M11;
    }
    expect(full_map_uses <= 2, "the selected complete route actually retains at least one charge");
    expect(conserving.decision.released_reserve_ids.empty(), "a feasible reserve is not relaxed");

    choice_request.route_search.time_budget_ms = 1000;
    const auto default_budget = planner.plan(choice_request);
    const auto default_progress = default_budget.decision.planned_milestone_progress.find(valuable.id);
    expect(
        default_budget && default_progress != default_budget.decision.planned_milestone_progress.end() &&
            default_progress->second >= 2,
        "the normal search time budget still finds a profitable conserving continuation");
    choice_run.resources.movement_charges[MovementKind::M11] = 2;
    const auto tighter_budget = planner.plan(choice_request);
    int tighter_uses = 0;
    for (const auto& step : tighter_budget.decision.planned_route_steps) {
        tighter_uses += step.move.movement == MovementKind::M11;
    }
    expect(
        tighter_budget && tighter_budget.decision.selected->movement == MovementKind::Walk && tighter_uses == 0,
        "two-charge stock must not spend a teleport because a conserving walking continuation was overlooked");
    const auto tighter_progress = tighter_budget.decision.planned_milestone_progress.find(valuable.id);
    expect(
        tighter_budget && tighter_progress != tighter_budget.decision.planned_milestone_progress.end() &&
            tighter_progress->second >= 1,
        "a tighter stock still collects the reachable reward");
    choice_run.resources.movement_charges[MovementKind::M11] = 3;
    choice_request.route_search.time_budget_ms = 10'000;

    choice_policy.reserves.clear();
    const auto unrestricted = planner.plan(choice_request);
    const auto all_progress = unrestricted.decision.planned_milestone_progress.find(valuable.id);
    expect(
        unrestricted && all_progress != unrestricted.decision.planned_milestone_progress.end() &&
            all_progress->second == 3,
        "without a reserve the higher-reward route is still eligible");
    choice_policy.reserves.push_back(reserve);
    choice_policy.reserves.front().active_if.constant = false;
    const auto inactive = planner.plan(choice_request);
    expect(
        inactive && inactive.decision.planned_milestone_progress.at(valuable.id) == 3,
        "an inactive reserve does not suppress a profitable continuation");
    choice_policy.reserves.front().active_if.constant = true;
    choice_policy.reserves.front().release_if.constant = true;
    const auto released = planner.plan(choice_request);
    expect(
        released && released.decision.planned_milestone_progress.at(valuable.id) == 3,
        "a declared release permits spending for the profitable continuation");
    choice_policy.reserves.front().release_if.constant = false;
    choice_policy.reserves.front().minimum = 4;
    const auto unavoidable = planner.plan(choice_request);
    expect(
        unavoidable && !unavoidable.decision.released_reserve_ids.empty(),
        "an impossible reserve still releases instead of blocking the run");

    std::cout << "ROUTE_RESERVE checks=" << checks << " failures=" << failures << " device_inputs=0\n";
    return failures ? 1 : 0;
}
