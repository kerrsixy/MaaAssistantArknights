#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include "AsstCaller.h"
#include "MaaUtils/Encoding.h"
#include "Task/Roguelike/BlackFlow/BlackFlowSession.h"

using namespace asst::blackflow;

BlackFlowPerceptionSnapshot make_snapshot(int current, int action_points, std::uint64_t sequence, MovementKind movement)
{
    BlackFlowPerceptionSnapshot result;
    auto& map = result.observation;
    map.observation_id = "asset-count-" + std::to_string(sequence);
    map.sequence = sequence;
    map.viewport_revision = sequence;
    map.floor = 2;
    map.floor_from_ocr = true;
    map.hud_action_points = action_points;
    map.coverage = ObservationCoverage::PartialViewport;
    map.recognition_ok = true;
    map.allow_exit_supplement = false;
    map.graph_connected = true;
    map.current_marker_temporary_id = current;
    map.current_marker_score = 1.0;
    for (int i = 0; i != 4; ++i) {
        PerceptionNodeObservation node;
        node.temporary_id = i;
        node.position = { 1, i };
        node.exists = true;
        node.type = "empty";
        node.confidence = 1.0;
        node.icon_rect = { 200 + i * 150, 300, 60, 60 };
        map.nodes.push_back(node);
        map.covered_positions.push_back(node.position);
        if (i != 0) {
            PerceptionEdgeObservation edge;
            edge.temporary_first = i - 1;
            edge.temporary_second = i;
            edge.connected = true;
            edge.cnn_connected = true;
            edge.probability = 1.0;
            edge.decision_source = "explicit-test-fixture";
            map.edges.push_back(edge);
        }
    }
    result.run.action_points = action_points;
    result.run.active_movement = movement;
    return result;
}

int charges(const BlackFlowSession& session, MovementKind movement)
{
    const auto& entries = session.run().resources.movement_charges;
    const auto found = entries.find(movement);
    return found == entries.end() ? 0 : found->second;
}

int pieces(const BlackFlowSession& session, MovementKind movement)
{
    const auto& entries = session.run().resources.movement_pieces;
    const auto found = entries.find(movement);
    return found == entries.end() ? 0 : found->second;
}

bool initialize(BlackFlowSession& session, MovementKind movement, int count, std::string* error)
{
    return session.initialize("ending1", error) && session.set_current_floor(2, error) &&
           session.update(make_snapshot(0, 10, 1, movement), error) &&
           session.apply_movement_inventory_observation({ { movement, count } }, true, error);
}

bool consume_one(BlackFlowSession& session, MovementKind movement, int target_index, std::string* error)
{
    const NodeId target = make_stable_node_id(2, GridPosition { 1, target_index }).value_or(InvalidNodeId);
    const auto actions = enumerate_move_actions(session.map().snapshot(), session.run());
    const auto found = std::ranges::find_if(actions, [&](const MoveAction& action) {
        return action.candidate.movement == movement && action.candidate.target == target;
    });
    if (found == actions.end()) {
        if (error) {
            *error = "fixture did not enumerate its adjacent movement";
        }
        return false;
    }
    auto move = found->candidate;
    move.requires_preview_verification = false;
    move.action_point_requirement = 0;
    MovePreview preview;
    preview.reachability = PreviewReachability::Reachable;
    preview.exact_action_point_cost = move.predicted_action_point_cost;
    preview.displayed_type = NodeType::Empty;
    preview.identity_revealed = true;
    if (!session.begin_transaction(move, error) ||
        session.accept_preview(preview, error) != PreviewDisposition::ReadyToCommit || !session.commit({}, error)) {
        return false;
    }
    const auto sequence = session.viewport().viewport_revision() + 1;
    return session.update(
        make_snapshot(
            target_index,
            session.run().resources.action_points - preview.exact_action_point_cost,
            sequence,
            movement),
        error);
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        return 2;
    }
#ifdef _MSC_VER
    _CrtSetReportHook([](int type, char* message, int*) {
        if (type == _CRT_ASSERT) {
            std::fputs(message, stderr);
            std::fflush(stderr);
            std::_Exit(86);
        }
        return 0;
    });
#endif
    const std::filesystem::path repo(argv[1]), out(argv[2]);
    std::filesystem::create_directories(out);
    const auto repo_text = MAA_NS::from_u16(repo.native());
    const auto out_text = MAA_NS::from_u16(out.native());
    if (!AsstSetUserDir(out_text.c_str()) || !AsstLoadResource(repo_text.c_str())) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const auto trace = [](const char* label, const BlackFlowSession& session, MovementKind movement) {
        std::cout << label << " charges=" << charges(session, movement) << " pieces=" << pieces(session, movement)
                  << " ap=" << session.run().resources.action_points << '\n';
    };
    std::string error;
    BlackFlowSession session;
    if (!initialize(session, MovementKind::M02, 2, &error)) {
        std::cerr << "FIXTURE_INIT_FAILED " << error << '\n';
        return 10;
    }
    expect(charges(session, MovementKind::M02) == 2, "two actual one-use M02 pieces provide two charges");
    auto no_use = session;
    expect(
        no_use.apply_movement_inventory_observation({ { MovementKind::M02, 1 } }, false, &error),
        "partial inventory accepted before use");
    expect(charges(no_use, MovementKind::M02) == 2, "partial missing card alone does not prove a consumption");
    if (!consume_one(session, MovementKind::M02, 1, &error)) {
        std::cerr << "FIXTURE_MOVE_FAILED " << error << '\n';
        return 11;
    }
    trace("after_real_commit", session, MovementKind::M02);
    expect(
        charges(session, MovementKind::M02) == 1,
        "public Session commit and map-return consume exactly one M02 charge");
    const auto after_first = session;
    for (int observation = 0; observation != 3; ++observation) {
        expect(
            session.apply_movement_inventory_observation({ { MovementKind::M02, 1 } }, false, &error),
            "partial one-piece observation is accepted");
        trace("after_partial", session, MovementKind::M02);
        expect(
            charges(session, MovementKind::M02) == 1,
            "repeated partial inventory cannot resurrect the consumed M02");
    }
    MovementPanelObservation panel;
    panel.target = MovementKind::M02;
    panel.target_found = true;
    panel.unique_record = true;
    panel.remaining_charges = 1;
    expect(
        session.apply_movement_panel_observation(panel, MovementKind::M02, &error),
        "verified one-use panel is accepted");
    expect(charges(session, MovementKind::M02) == 1, "stale piece accounting cannot mask the remaining-one panel");
    auto genuine_added = after_first;
    expect(
        genuine_added.apply_movement_inventory_observation({ { MovementKind::M02, 2 } }, true, &error),
        "complete inventory may prove an additional actual M02");
    expect(
        charges(genuine_added, MovementKind::M02) == 2,
        "actual complete two-piece evidence may increase the remaining total");
    auto consumed = after_first;
    if (!consume_one(consumed, MovementKind::M02, 2, &error)) {
        std::cerr << "FIXTURE_SECOND_MOVE_FAILED " << error << '\n';
        return 12;
    }
    expect(charges(consumed, MovementKind::M02) == 0, "second real commit exhausts the last actual M02");
    expect(consumed.apply_movement_inventory_observation({}, false, &error), "empty partial inventory is accepted");
    expect(charges(consumed, MovementKind::M02) == 0, "partial absence does not resurrect an exhausted charge");
    for (const auto invalid :
         { std::unordered_map<MovementKind, int> { { MovementKind::M02, 3 }, { MovementKind::Walk, 1 } },
           std::unordered_map<MovementKind, int> { { MovementKind::M02, -1 } } }) {
        const auto before = session.run().resources;
        expect(
            !session.apply_movement_inventory_observation(invalid, false, &error),
            "invalid inventory rejects without partial commit");
        expect(session.run().resources == before, "rejected inventory preserves all previous resources");
    }
    BlackFlowSession multi_use;
    if (!initialize(multi_use, MovementKind::M03, 1, &error)) {
        std::cerr << "FIXTURE_MULTI_INIT_FAILED " << error << '\n';
        return 13;
    }
    expect(charges(multi_use, MovementKind::M03) == 3, "one M03 starts with three uses");
    for (int step = 1; step <= 3; ++step) {
        if (!consume_one(multi_use, MovementKind::M03, step, &error)) {
            std::cerr << "FIXTURE_MULTI_MOVE_FAILED step=" << step << ' ' << error << '\n';
            return 14;
        }
        expect(charges(multi_use, MovementKind::M03) == 3 - step, "real M03 commits consume individual uses");
        const std::unordered_map<MovementKind, int> visible =
            step < 3 ? std::unordered_map<MovementKind, int> { { MovementKind::M03, 1 } }
                     : std::unordered_map<MovementKind, int> {};
        expect(
            multi_use.apply_movement_inventory_observation(visible, false, &error),
            "M03 inventory observation accepted");
        expect(
            charges(multi_use, MovementKind::M03) == 3 - step,
            "M03 partial inventory preserves its actual remaining uses");
    }
    BlackFlowSession two_multi_use;
    if (!initialize(two_multi_use, MovementKind::M03, 2, &error)) {
        std::cerr << "FIXTURE_TWO_MULTI_INIT_FAILED " << error << '\n';
        return 15;
    }
    expect(charges(two_multi_use, MovementKind::M03) == 6, "two M03 pieces start with six actual uses");
    const int destinations[] = { 1, 2, 3, 2, 1 };
    for (int step = 0; step < 5; ++step) {
        if (!consume_one(two_multi_use, MovementKind::M03, destinations[step], &error)) {
            std::cerr << "FIXTURE_TWO_MULTI_MOVE_FAILED " << error << '\n';
            return 16;
        }
        expect(charges(two_multi_use, MovementKind::M03) == 5 - step, "M03 commit history remains authoritative");
    }
    expect(
        two_multi_use.apply_movement_inventory_observation({ { MovementKind::M03, 1 } }, false, &error),
        "partial observation accepts the remaining M03 piece");
    expect(charges(two_multi_use, MovementKind::M03) == 1, "cached two-piece M03 count cannot refill its last use");
    expect(
        two_multi_use.apply_movement_inventory_observation({ { MovementKind::M03, 1 } }, true, &error),
        "complete M03 observation reconciles its actual remaining piece");
    expect(charges(two_multi_use, MovementKind::M03) == 1, "complete one-piece observation does not refill M03");
    std::cout << "MOVEMENT_COUNT checks=" << checks << " failures=" << failures << " network=0 inputs=0\n";
    return failures == 0 ? 0 : 1;
}
