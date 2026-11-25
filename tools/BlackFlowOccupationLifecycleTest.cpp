#include "AsstCaller.h"
#include "Config/Roguelike/BlackFlow/BlackFlowNodeExecutionConfig.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Task/Roguelike/BlackFlow/BlackFlowMovePreviewAnalyzer.h"
#include "Task/Roguelike/BlackFlow/BlackFlowSession.h"
#include "Utils/Platform.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

using namespace asst;
using namespace asst::blackflow;
constexpr NodeId Source = 1'125'899'973'951'494ULL, Target = 1'125'899'940'397'062ULL;

MapObservationBatch batch_from(const json::value& raw)
{
    MapObservationBatch batch;
    batch.floor = raw.at("floor").as_integer();
    batch.coverage = ObservationCoverage::FullMap;
    std::map<NodeId, GridPosition> positions;
    for (const auto& n : raw.at("nodes").as_array()) {
        ObservedNode node;
        node.position = { n.at("row").as_integer(), n.at("column").as_integer() };
        node.type = node_type_from_string(n.at("type").as_string()).value();
        node.name = n.at("name").as_string();
        node.progress = n.at("progress").as_string() == "completed" ? NodeProgress::Completed : NodeProgress::Active;
        node.identity_revealed = n.at("identity_revealed").as_boolean();
        node.identity_state = n.at("identity_state").as_string() == "classified" ? NodeIdentityState::Classified
                              : n.at("identity_state").as_string() == "hidden"   ? NodeIdentityState::Hidden
                                                                                 : NodeIdentityState::Unclassified;
        positions[n.at("id").as_unsigned_long_long()] = node.position;
        batch.nodes.push_back(node);
    }
    for (const auto& e : raw.at("edges").as_array()) {
        batch.edges.push_back(
            { positions.at(e.at("first").as_unsigned_long_long()),
              positions.at(e.at("second").as_unsigned_long_long()),
              e.at("knowledge").as_string() == "confirmed" ? EdgeKnowledge::Confirmed
              : e.at("knowledge").as_string() == "absent"  ? EdgeKnowledge::Absent
                                                           : EdgeKnowledge::Unknown,
              { e.at("probability").as_double(),
                e.at("cnn_connected").as_boolean(),
                e.at("forced_by_connectivity_constraint").as_boolean(),
                e.at("decision_source").as_string() } });
    }
    return batch;
}

BlackFlowPerceptionSnapshot session_from(const json::value& raw, std::uint64_t sequence)
{
    BlackFlowPerceptionSnapshot result;
    auto& o = result.observation;
    o.observation_id = "saved-run75-" + std::to_string(sequence);
    o.sequence = o.viewport_revision = sequence;
    o.floor = raw.at("floor").as_integer();
    o.floor_from_ocr = true;
    o.rows = 5;
    o.columns = 8;
    o.hud_action_points = 4; // Captured HUD; other internal state is not recovered.
    o.coverage = ObservationCoverage::FullMap;
    o.recognition_ok = true;
    o.allow_exit_supplement = false;
    o.graph_connected = true;
    o.current_marker_score = 1;
    std::map<NodeId, int> ids;
    for (const auto& n : raw.at("nodes").as_array()) {
        const auto id = n.at("id").as_unsigned_long_long();
        PerceptionNodeObservation node;
        node.temporary_id = static_cast<int>(ids.size());
        ids[id] = node.temporary_id;
        node.position = { n.at("row").as_integer(), n.at("column").as_integer() };
        node.exists = true;
        node.type = n.at("type").as_string();
        node.displayed_name = n.at("name").as_string();
        node.confidence = 1;
        node.icon_rect = { 200 + node.position.column * 90, 100 + node.position.row * 90, 50, 50 };
        o.nodes.push_back(node);
        o.covered_positions.push_back(node.position);
        if (id == Source) {
            o.current_marker_temporary_id = node.temporary_id;
        }
    }
    for (const auto& e : raw.at("edges").as_array()) {
        PerceptionEdgeObservation edge;
        edge.temporary_first = ids.at(e.at("first").as_unsigned_long_long());
        edge.temporary_second = ids.at(e.at("second").as_unsigned_long_long());
        edge.connected = e.at("knowledge").as_string() == "confirmed";
        edge.cnn_connected = e.at("cnn_connected").as_boolean();
        edge.probability = e.at("probability").as_double();
        edge.decision_source = "saved-snapshot";
        o.edges.push_back(edge);
    }
    result.run.action_points = 4;
    result.run.active_movement = MovementKind::Walk;
    return result;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 5) {
        return 2;
    }
    const std::filesystem::path resources(argv[1]), evidence(argv[2]), image_file(argv[3]), output(argv[4]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(resources.native()).c_str())) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto check = [&](bool value, const char* label) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const auto image = MAA_NS::imread(image_file);
    MovePreview preview;
    std::string error;
    check(recognize_move_preview_identity(image, preview, &error), "actual stopped-scene raw panel OCR");
    check(
        preview.displayed_type == NodeType::BattleNormal && preview.displayed_name == "作战",
        "category is a battle, never rest default");
    check(preview.stage_title == "进退趋同", "same-frame stage title retained");
    check(
        preview.occupation && preview.occupation->base_type == NodeType::Rest &&
            preview.occupation->entry_type == NodeType::BattleNormal &&
            preview.occupation->cleared_type == NodeType::Empty,
        "same-frame resident description establishes base/effective/cleared types");
    preview.reachability = PreviewReachability::Reachable;
    preview.exact_action_point_cost = 1;
    if (!preview.occupation) {
        std::cerr << "occupation OCR error=" << error << '\n';
        return 1;
    }
    check(
        !parse_rest_occupation_description("居民占领了不期而遇节点，战胜后此节点将变为林间空地", "进退趋同"),
        "unsupported base is unknown");
    check(
        !parse_rest_occupation_description("居民占领了安全的角落节点，战胜后此节点将变为林间空地", ""),
        "missing same-frame title cannot assert occupation");
    std::unordered_set<std::string> paired_observations;
    std::optional<json::value> unpaired_initial;
    for (const auto& f : std::filesystem::directory_iterator(evidence / utils::path("floor-4/snapshots"))) {
        if (f.path().extension() != utils::path(".json")) {
            continue;
        }
        const auto j = json::open(f.path());
        if (j && j->find<std::string>("source") && *j->find<std::string>("source") == "preview" &&
            j->get("current_node", std::uint64_t {}) == Source) {
            paired_observations.emplace(j->at("observation_id").as_string());
        }
    }
    std::vector<std::pair<int, std::filesystem::path>> files;
    for (const auto& f : std::filesystem::directory_iterator(evidence / utils::path("floor-4/snapshots"))) {
        if (f.path().extension() != utils::path(".json")) {
            continue;
        }
        const auto j = json::open(f.path());
        if (!j || (!j->find<std::string>("source") || *j->find<std::string>("source") != "recognition") ||
            j->get("current_node", std::uint64_t {}) != Source) {
            continue;
        }
        const auto node = std::ranges::find_if(j->at("nodes").as_array(), [](const auto& n) {
            return n.at("id").as_unsigned_long_long() == Target;
        });
        if (node == j->at("nodes").as_array().end() || node->at("type").as_string() != "rest") {
            continue;
        }
        if (!paired_observations.contains(j->at("observation_id").as_string())) {
            unpaired_initial = *j;
            continue;
        }
        files.emplace_back(std::stoi(f.path().stem().string().substr(6)), f.path());
    }
    std::ranges::sort(files);
    check(files.size() == 49, "all49 actual saved recognition cycles selected");
    if (files.empty()) {
        return 1;
    }
    NormalizedMap map;
    check(unpaired_initial.has_value(), "unpairedBF-O43 initial recognition is retained as separate evidence");
    if (unpaired_initial) {
        check(
            map.merge(batch_from(*unpaired_initial), &error) &&
                map.snapshot().find_node(Target)->type == NodeType::Rest,
            "initial unpaired frame has only baseRest knowledge");
    }
    int semantic_changes = 0;
    for (const auto& [number, file] : files) {
        const auto raw = json::open(file).value();
        check(map.merge(batch_from(raw), &error), "merge actual saved visual map");
        bool changed = false;
        check(map.accept_preview(Target, preview, changed, &error), "bind actual occupation to the same selected node");
        semantic_changes += changed;
        const Node* n = map.snapshot().find_node(Target);
        check(
            n && n->type == NodeType::BattleNormal && n->occupation && n->occupation->base_type == NodeType::Rest &&
                n->name == "进退趋同",
            "effective battle does not erase base rest identity or stage");
    }
    check(semantic_changes == 1, "49 repeated visual-rest observations converge after one semantic refinement");
    NodeSelector rest_selector, battle_selector;
    rest_selector.node_types = { NodeType::Rest };
    battle_selector.node_types = { NodeType::BattleNormal };
    check(
        !rest_selector.matches(*map.snapshot().find_node(Target)) &&
            battle_selector.matches(*map.snapshot().find_node(Target)),
        "existing planner selectors consume battle risk/reward kind rather than rest treatment");
    const Node* occupied = map.snapshot().find_node(Target);
    const auto dispatch = BlackFlowNodeExecution.resolve_route({ 4, occupied->type, occupied->name, "default" });
    check(
        dispatch && dispatch->get().task.find("Page-Combat") != std::string::npos,
        "existing dispatcher chooses combat, not rest page, while retaining actual stage title");
    auto initial = json::open(files.front().second).value();
    auto batch = batch_from(initial);
    auto changed_visual = batch;
    for (auto& n : changed_visual.nodes) {
        if (n.position == GridPosition { 1, 3 }) {
            n.type = NodeType::Shop;
            n.name = "诡意行商";
        }
    }
    check(
        map.merge(changed_visual, &error) && !map.snapshot().find_node(Target)->occupation &&
            map.snapshot().find_node(Target)->type == NodeType::Shop,
        "genuine new visual identity invalidates occupation");
    check(map.merge(batch, &error), "restore explicit test premise");
    bool changed = false;
    check(map.accept_preview(Target, preview, changed, &error), "re-establish supported occupation");
    map.clear_occupations();
    check(
        map.snapshot().find_node(Target)->type == NodeType::Rest && !map.snapshot().find_node(Target)->occupation,
        "actual movement scope reset restores base facts pending new preview");
    check(map.accept_preview(Target, preview, changed, &error), "occupation can be independently reverified");
    auto no_body = preview;
    no_body.occupation.reset();
    check(
        map.accept_preview(Target, no_body, changed, &error) && map.snapshot().find_node(Target)->occupation,
        "missing body alone does not erase still-battle occupation evidence");
    auto released = no_body;
    released.displayed_type = NodeType::Rest;
    released.displayed_name = "安全的角落";
    released.stage_title.clear();
    check(
        map.accept_preview(Target, released, changed, &error) && changed &&
            map.snapshot().find_node(Target)->type == NodeType::Rest && !map.snapshot().find_node(Target)->occupation &&
            map.snapshot().find_node(Target)->name == "安全的角落",
        "fresh classified rest entry releases occupation and stale battle title");
    check(map.accept_preview(Target, preview, changed, &error), "reverify occupation for new-entry test");
    auto new_entry = released;
    new_entry.displayed_type = NodeType::Shop;
    new_entry.displayed_name = "诡意行商";
    check(
        map.accept_preview(Target, new_entry, changed, &error) && !map.snapshot().find_node(Target)->occupation &&
            map.snapshot().find_node(Target)->name == "诡意行商",
        "different nonoccupation entry cannot retain old occupation title");
    check(
        map.merge(batch, &error) && map.accept_preview(Target, preview, changed, &error),
        "restore actual map premise before postbattle case");
    NormalizedMap unknown_map;
    auto unknown_batch = batch;
    for (auto& n : unknown_batch.nodes) {
        if (n.position == GridPosition { 1, 3 }) {
            n.type = NodeType::Unknown;
            n.name = "";
        }
    }
    check(
        unknown_map.merge(unknown_batch, &error) && unknown_map.accept_preview(Target, preview, changed, &error) &&
            unknown_map.snapshot().find_node(Target)->occupation,
        "explicit resident description can establish previously unknown base identity");
    auto cleared = batch;
    for (auto& n : cleared.nodes) {
        if (n.position == GridPosition { 1, 3 }) {
            n.type = NodeType::Empty;
            n.name = "";
            n.progress = NodeProgress::Completed;
        }
    }
    check(
        map.merge(cleared, &error) && map.snapshot().find_node(Target)->type == NodeType::Empty &&
            !map.snapshot().find_node(Target)->occupation,
        "observed postbattle empty clears occupation");
    auto floor_changed = batch;
    floor_changed.floor = 5;
    check(
        map.merge(floor_changed, &error) && map.snapshot().find_node(Target) == nullptr,
        "new floor discards old occupation");
    PreviewConflictProgress guard;
    PreviewProgressScope scope {
        1, 4, Source, Target, Target, MovementKind::Walk, NodeType::Rest, NodeProgress::Active
    };
    check(
        !guard.repeated(scope) && guard.repeated(scope),
        "same conflicting move gets one refresh then finite rejection");
    auto different = scope;
    ++different.run;
    check(!guard.repeated(different), "new run resets conflict");
    different = scope;
    ++different.floor;
    check(!guard.repeated(different), "new floor resets conflict");
    different = scope;
    ++different.current;
    check(!guard.repeated(different), "actual movement resets conflict");
    different = scope;
    different.visual_type = NodeType::Shop;
    check(!guard.repeated(different), "genuine known new identity resets conflict");
    auto run_session = [&](bool explain, bool mismatch = false) {
        BlackFlowSession session;
        std::string message;
        bool ok = session.initialize("ending1", &message) && session.set_current_floor(4, &message);
        auto premise = initial;
        if (mismatch) {
            for (auto& node : premise["nodes"].as_array()) {
                if (node.at("id").as_unsigned_long_long() == Target) {
                    node["type"] = "shop";
                    node["name"] = "诡意行商";
                }
            }
        }
        auto p = preview;
        if (!explain) {
            p.occupation.reset();
        }
        std::vector<int> dispositions;
        for (int i = 0; ok && i < 2; ++i) {
            ok = session.update(session_from(premise, 100 + i), &message);
            const auto actions = enumerate_move_actions(session.map().snapshot(), session.run());
            const auto action = std::ranges::find_if(actions, [](const MoveAction& a) {
                return a.candidate.movement == MovementKind::Walk && a.candidate.target == Target;
            });
            if (!ok || action == actions.end()) {
                ok = false;
                break;
            }
            ok = session.begin_transaction(action->candidate, &message);
            if (!ok) {
                break;
            }
            dispositions.push_back(static_cast<int>(session.accept_preview(p, &message)));
        }
        if (!ok) {
            std::cerr << "SESSION premise error=" << message << '\n';
        }
        check(ok, "actual Session saved-map premise is legal under explicit minimal run parameters");
        if (explain && !mismatch) {
            check(
                dispositions == std::vector<int> { 1, 0 } && !session.terminated(),
                "actual Session replans once then accepts verified occupation without bypassing commit gates");
            check(session.validate_commit(&message), "existing commit validation still applies to verified occupation");
            session.cancel_transaction();
            auto released = p;
            released.occupation.reset();
            released.displayed_type = NodeType::Rest;
            released.displayed_name = "安全的角落";
            released.stage_title.clear();
            std::vector<int> releases;
            for (int r = 0; r < 2; ++r) {
                bool release_ok = session.update(session_from(premise, 150 + r), &message);
                const auto moves = enumerate_move_actions(session.map().snapshot(), session.run());
                const auto walk = std::ranges::find_if(moves, [](const MoveAction& a) {
                    return a.candidate.movement == MovementKind::Walk && a.candidate.target == Target;
                });
                release_ok = release_ok && walk != moves.end() && session.begin_transaction(walk->candidate, &message);
                check(release_ok, "fresh rest release retains existing transaction gates");
                if (release_ok) {
                    releases.push_back(static_cast<int>(session.accept_preview(released, &message)));
                }
            }
            check(
                releases == std::vector<int> { 1, 0 } && !session.terminated() &&
                    !session.map().snapshot().find_node(Target)->occupation,
                "actual Session accepts genuine occupation release once then remains stable");
            session.cancel_transaction();
            check(session.update(session_from(premise, 175), &message), "restore map before reoccupation");
            const auto re_actions = enumerate_move_actions(session.map().snapshot(), session.run());
            const auto re_walk = std::ranges::find_if(re_actions, [](const MoveAction& a) {
                return a.candidate.movement == MovementKind::Walk && a.candidate.target == Target;
            });
            check(
                re_walk != re_actions.end() && session.begin_transaction(re_walk->candidate, &message) &&
                    session.accept_preview(p, &message) == PreviewDisposition::ReplanAfterDismiss,
                "released node can acquire independently reverified occupation");
            session.cancel_transaction();
            auto moved = session_from(premise, 200);
            for (const auto& node : moved.observation.nodes) {
                if (node.type == "empty" && node.position != GridPosition { 2, 3 }) {
                    moved.observation.current_marker_temporary_id = node.temporary_id;
                    break;
                }
            }
            check(
                session.update(moved, &message) && !session.map().snapshot().find_node(Target)->occupation &&
                    session.map().snapshot().find_node(Target)->type == NodeType::Rest,
                "actual Session moving-current counterfactual clears scoped occupation pending new preview");
        }
        else {
            check(
                dispositions == std::vector<int> { 1, 2 } && session.terminated(),
                "unexplained repeated cross-type conflict becomes explicit workflow failure");
        }
    };
    run_session(true);
    run_session(false);
    run_session(true, true);
    auto lifecycle = [&](bool completed, bool failed, NodeType returned_type) {
        BlackFlowSession session;
        std::string message;
        bool legal = session.initialize("ending1", &message) && session.set_current_floor(4, &message);
        for (int i = 0; legal && i < 2; ++i) {
            legal = session.update(session_from(initial, 300 + i), &message);
            const auto moves = enumerate_move_actions(session.map().snapshot(), session.run());
            const auto walk = std::ranges::find_if(moves, [](const MoveAction& a) {
                return a.candidate.movement == MovementKind::Walk && a.candidate.target == Target;
            });
            legal = legal && walk != moves.end() && session.begin_transaction(walk->candidate, &message);
            if (legal) {
                legal = session.accept_preview(preview, &message) ==
                        (i == 0 ? PreviewDisposition::ReplanAfterDismiss : PreviewDisposition::ReadyToCommit);
            }
        }
        EnteredPageObservation entered;
        entered.classified_type = NodeType::BattleNormal;
        entered.matched_texts = { "作战", "进退趋同" };
        legal = legal && session.commit(entered, &message) && session.mark_page_running(&message);
        check(
            legal && session.page_context() && session.page_context()->node_type == NodeType::BattleNormal &&
                session.page_context()->node_name == "进退趋同",
            "actual Session commits and runs resident combat page with real stage title");
        check(
            session.map().snapshot().find_node(Target)->type == NodeType::BattleNormal,
            "commit and running do not prematurely clear occupied battle");
        if (!legal) {
            std::cerr << "LIFECYCLE prerequisite " << message << '\n';
            return;
        }
        if (completed) {
            const auto configured = BlackFlowNodeExecution.get_task_result("BlackFlow@Roguelike@Event-NodeCompleted");
            check(
                configured && configured->get().kind == NodeTaskResultKind::PageCompleted,
                "completion follows actual node execution config consumer");
            legal = configured && session.apply_node_task_result(configured->get(), json::object {}, &message);
        }
        else {
            NodeTaskResult intermediate;
            intermediate.kind = NodeTaskResultKind::Intermediate;
            intermediate.succeeded = false;
            legal = session.apply_node_task_result(intermediate, json::object {}, &message);
            if (failed) {
                session.fail("test_combat_failed", "explicit failure fixture", FailureDisposition::StopTask);
            }
        }
        check(
            legal && session.map().snapshot().find_node(Target)->type == NodeType::BattleNormal,
            "page callback alone does not clear map before verified return");
        const auto callbacks = session.take_telemetry_events();
        int resolution_count = 0;
        bool resolution_consistent = true;
        for (const auto& event : callbacks) {
            if (event.what == "BlackFlowNodeResolution") {
                ++resolution_count;
                const json::value details(event.details);
                resolution_consistent &= details.get("node_type", "") == "battle_normal" &&
                                         details.get("event_name", "") == "进退趋同" &&
                                         details.get("becomes_empty", false) && !details.get("repeatable", true);
            }
        }
        check(
            resolution_count == (completed ? 1 : 0) && resolution_consistent,
            "node resolution reports completed battle entry and verified becomesEmpty exactly once; failures emit "
            "none");
        auto returned = session_from(initial, 350);
        returned.observation.hud_action_points = 3;
        returned.run.action_points = 3;
        for (auto& node : returned.observation.nodes) {
            if (node.position == GridPosition { 1, 3 }) {
                returned.observation.current_marker_temporary_id = node.temporary_id;
                node.type = std::string(to_string(returned_type));
                node.displayed_name = returned_type == NodeType::Rest           ? "安全的角落"
                                      : returned_type == NodeType::BattleNormal ? "进退趋同"
                                                                                : "";
            }
        }
        const bool reconciled = session.update(returned, &message);
        check(
            reconciled && session.run().current_node == Target && session.run().resources.action_points == 3 &&
                !session.page_context(),
            "committed move reconciles actual landing and exact1AP before page finalization");
        const auto final_callbacks = session.take_telemetry_events();
        check(
            std::ranges::none_of(final_callbacks, [](const auto& e) { return e.what == "BlackFlowNodeResolution"; }),
            "map finalization does not duplicate the already-reported node resolution");
        const auto final_facts = session.facts();
        const auto* credit = final_facts.find("milestone.ending1_late_combat.progress");
        check(
            credit && std::holds_alternative<std::int64_t>(*credit) &&
                std::get<std::int64_t>(*credit) == (completed ? 1 : 0),
            "existing mission credits completed battle once, never baseRest/Empty or unfinished failure");
        const auto* final_node = session.map().snapshot().find_node(Target);
        if (completed) {
            check(
                reconciled && final_node && final_node->type == NodeType::Empty &&
                    final_node->progress == NodeProgress::Completed && !final_node->occupation,
                "completed occupied battle becomes Empty even if return visual still base/battle/unknown");
        }
        else {
            check(
                final_node && final_node->type != NodeType::Empty,
                "failed or unresolved page never manufactures successful Empty");
        }
        if (completed && reconciled) {
            auto settled = returned;
            settled.observation.sequence = settled.observation.viewport_revision = 351;
            for (auto& node : settled.observation.nodes) {
                if (node.position == GridPosition { 1, 3 }) {
                    node.type = "empty";
                    node.displayed_name.clear();
                }
            }
            check(
                session.update(settled, &message) &&
                    session.map().snapshot().find_node(Target)->type == NodeType::Empty &&
                    !session.map().snapshot().find_node(Target)->occupation,
                "next settled map refresh continues from Empty without occupation resurrection");
        }
    };
    lifecycle(true, false, NodeType::Rest);
    lifecycle(true, false, NodeType::BattleNormal);
    lifecycle(true, false, NodeType::Unknown);
    lifecycle(true, false, NodeType::Empty);
    lifecycle(false, false, NodeType::Rest);
    lifecycle(false, true, NodeType::Rest);
    json::object result { { "checks", checks },
                          { "failures", failures },
                          { "saved_recognition_cycles", static_cast<int>(files.size()) },
                          { "semantic_identity_changes", semantic_changes },
                          { "stage_title", preview.stage_title },
                          { "post_stop_panel_fixture", true },
                          { "historical49_panels_had_no_saved_raw_body", true },
                          { "session_non_map_parameters_explicit_fixture_not_full_state_reconstruction", true },
                          { "connected_devices", 0 },
                          { "gameplay_inputs", 0 } };
    std::ofstream(output / utils::path("occupation_lifecycle.json")) << json::value(result);
    std::cout << "OCCUPATION_LIFECYCLE checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
