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
    std::cout << "ROUTE_RESERVE checks=" << checks << " failures=" << failures << " device_inputs=0\n";
    return failures ? 1 : 0;
}
