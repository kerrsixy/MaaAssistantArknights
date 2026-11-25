#include <iostream>

#include "Task/Roguelike/BlackFlow/BlackFlowPlanner.h"

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
    const GridPosition start { 0, 0 }, first { 0, 1 }, second { 0, 2 }, end { 0, 3 };
    const GridPosition remote1 { 3, 0 }, remote2 { 3, 1 }, remote3 { 3, 2 };
    const std::vector<GridPosition> positions { start,   first,   second,  end,      { 1, 0 }, { 2, 0 },
                                                remote1, remote2, remote3, { 2, 2 }, { 1, 2 } };
    MapObservationBatch batch;
    batch.floor = 3;
    for (const auto position : positions) {
        ObservedNode node;
        node.position = position;
        node.type = position == end ? NodeType::Final
                    : position == first || position == second || position == remote1 || position == remote2 ||
                            position == remote3
                        ? NodeType::Incident
                        : NodeType::Empty;
        node.name = position == start ? "start" : position == end ? "exit" : "reward";
        node.identity_state = NodeIdentityState::Classified;
        node.identity_revealed = node.identity_from_template = true;
        batch.nodes.push_back(node);
    }
    const auto edge = [&](GridPosition first_point, GridPosition second_point) {
        batch.edges.push_back({ first_point, second_point, EdgeKnowledge::Confirmed, {} });
    };
    edge(start, first);
    edge(first, second);
    edge(second, end);
    edge(start, { 1, 0 });
    edge({ 1, 0 }, { 2, 0 });
    edge({ 2, 0 }, remote1);
    edge(remote1, remote2);
    edge(remote2, remote3);
    edge(remote3, { 2, 2 });
    edge({ 2, 2 }, { 1, 2 });
    edge({ 1, 2 }, second);
    NormalizedMap map;
    std::string error;
    expect(map.merge(batch, &error), "merge a near reward route and a connected long alternative");
    RunState run;
    run.floor = 3;
    run.current_node = *make_stable_node_id(3, start);
    run.resources.action_points = 4;
    run.resources.movement_charges[MovementKind::M11] = 3;
    run.resources.movement_pieces[MovementKind::M11] = 1;
    ResolvedPolicy policy;
    Milestone reward;
    reward.id = "optional_rewards";
    reward.kind = MilestoneKind::Preferred;
    reward.floor_begin = reward.floor_end = 3;
    reward.required_count = 99;
    reward.weight = 2;
    reward.selector.node_types = { NodeType::Incident };
    policy.milestones.push_back(reward);
    FactStore facts;
    MissionState mission;
    BlackFlowPlanRequest request;
    request.map = &map.snapshot();
    request.run = &run;
    request.policy = &policy;
    request.facts = &facts;
    request.mission = &mission;
    request.route_search.time_budget_ms = 5000;
    request.route_search.expansions_per_root = 512;
    BlackFlowPlanner planner;
    const auto low = planner.plan(request);
    expect(static_cast<bool>(low), "small optional reward gain has a complete exit route");
    int low_uses = 0;
    for (const auto& step : low.decision.planned_route_steps) {
        low_uses += step.move.movement != MovementKind::Walk;
    }
    std::cout << "LOW_REWARD first=" << (low ? low.decision.selected->action_id : low.error)
              << " processing=" << low_uses << '\n';
    expect(
        low && low.decision.selected->movement == MovementKind::Walk &&
            low.decision.selected->target == *make_stable_node_id(3, first),
        "hypothetical distance to remote rewards cannot make the near walk worse");
    expect(low_uses == 0, "small additional reward does not spend persistent moves without net benefit");
    policy.milestones.front().weight = 20;
    const auto high = planner.plan(request);
    expect(static_cast<bool>(high), "large reward gain remains reachable");
    const auto progress = high.decision.planned_milestone_progress.find(reward.id);
    expect(
        high && progress != high.decision.planned_milestone_progress.end() && progress->second >= 3,
        "a real large reward gain may justify extra movement resources");
    policy.milestones.front().reward_weight_min = 0;
    const auto uncertain = planner.plan(request);
    expect(static_cast<bool>(uncertain), "unknown reward upside retains a complete exit route");
    int uncertain_uses = 0;
    for (const auto& step : uncertain.decision.planned_route_steps) {
        uncertain_uses += step.move.movement != MovementKind::Walk;
    }
    expect(
        uncertain && uncertain.decision.selected->movement == MovementKind::Walk && uncertain_uses == 0,
        "a zero lower bound does not spend persistent charges as guaranteed large rewards");
    std::cout << "ROUTE_VALUE checks=" << checks << " failures=" << failures << " inputs=0 live_route_proven=false\n";
    return failures ? 1 : 0;
}
