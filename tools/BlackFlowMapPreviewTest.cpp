#include <iostream>

#include "Task/Roguelike/BlackFlow/BlackFlowModel.h"

int main()
{
    using namespace asst::blackflow;
    int checks = 0;
    int failures = 0;
    const auto check = [&](bool passed, const char* label) {
        ++checks;
        if (!passed) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };

    NormalizedMap map;
    MapObservationBatch observed;
    observed.floor = 2;
    ObservedNode resident;
    resident.position = { 1, 3 };
    resident.type = NodeType::BattleNormal;
    resident.name = "";
    resident.identity_state = NodeIdentityState::Classified;
    resident.identity_revealed = true;
    resident.marker_type = "savage";
    observed.nodes.emplace_back(resident);
    std::string error;
    check(map.merge(observed, &error), "import resident battle without a readable map label");
    const auto target = *make_stable_node_id(2, resident.position);
    MovePreview preview;
    preview.reachability = PreviewReachability::Reachable;
    preview.exact_action_point_cost = 1;
    preview.displayed_type = NodeType::BattleNormal;
    preview.displayed_name = "作战";
    preview.identity_revealed = true;
    bool changed = false;
    check(map.accept_preview(target, preview, changed, &error) && changed, "first preview refines the label");

    // The failed live run alternated a blank map label and a readable preview.
    // Repeated observations must converge so the session can confirm the move.
    for (int cycle = 0; cycle < 20; ++cycle) {
        const auto revision = map.snapshot().revision;
        check(map.merge(observed, &error), "refresh resident map");
        check(map.snapshot().find_node(target)->name == "作战", "retain preview label");
        check(map.snapshot().revision == revision, "blank label does not advance the map revision");
        check(map.accept_preview(target, preview, changed, &error) && !changed, "repeat preview is stable");
    }

    observed.nodes.front().name = "强买强卖";
    check(
        map.merge(observed, &error) && map.snapshot().find_node(target)->name == "强买强卖",
        "a readable same-type label replaces the previous label");
    observed.nodes.front().name = "";
    observed.nodes.front().progress = NodeProgress::Completed;
    observed.nodes.front().buff_scope = true;
    check(map.merge(observed, &error), "merge non-name changes");
    check(
        map.snapshot().find_node(target)->progress == NodeProgress::Completed &&
            map.snapshot().find_node(target)->buff_scope,
        "preserving a label does not freeze progress or weather");
    observed.nodes.front().type = NodeType::Empty;
    check(
        map.merge(observed, &error) && map.snapshot().find_node(target)->name.empty() &&
            map.snapshot().find_node(target)->type == NodeType::Empty,
        "an explicit different type clears the stale label");
    observed.nodes.front().type = NodeType::HideBattle;
    observed.nodes.front().identity_state = NodeIdentityState::Hidden;
    observed.nodes.front().identity_revealed = false;
    check(
        map.merge(observed, &error) && !map.snapshot().find_node(target)->identity_revealed,
        "new hidden identity is not overridden by the old preview");
    observed.floor = 3;
    check(
        map.merge(observed, &error) && map.floor() == 3 && map.snapshot().find_node(target) == nullptr,
        "floor transition discards previous floor identities");
    // run37 floor2 repeatedly read the full resident label on the map, then
    // reduced it to the configured category token in the preview. Neither the
    // location, node type nor reachability changed during these real cycles.
    NormalizedMap resident_map;
    MapObservationBatch resident_batch;
    resident_batch.floor = 2;
    ObservedNode resident_node;
    resident_node.position = { 2, 3 };
    resident_node.type = NodeType::BattleSavage;
    resident_node.name = "“居民”据点";
    resident_node.progress = NodeProgress::Active;
    resident_node.identity_state = NodeIdentityState::Classified;
    resident_node.identity_revealed = true;
    resident_batch.nodes.push_back(resident_node);
    check(resident_map.merge(resident_batch, &error), "import actual run37 resident label and classified type");
    const auto resident_id = *make_stable_node_id(2, resident_node.position);
    MovePreview category_preview;
    category_preview.reachability = PreviewReachability::Reachable;
    category_preview.exact_action_point_cost = 1;
    category_preview.displayed_type = NodeType::BattleSavage;
    category_preview.displayed_name = "据点";
    category_preview.identity_revealed = true;
    for (int cycle = 0; cycle < 35; ++cycle) {
        const auto revision = resident_map.snapshot().revision;
        check(resident_map.merge(resident_batch, &error), "refresh run37 resident map identity");
        check(
            resident_map.accept_preview(resident_id, category_preview, changed, &error) && !changed,
            "same classified category can be confirmed without another replan");
        check(
            resident_map.snapshot().find_node(resident_id)->name == "“居民”据点",
            "a category-only preview preserves the more informative map label");
        check(resident_map.snapshot().revision == revision, "unchanged resident facts do not churn map revisions");
    }
    category_preview.displayed_type = NodeType::Shop;
    category_preview.displayed_name = "诡异行商";
    check(
        resident_map.accept_preview(resident_id, category_preview, changed, &error) && changed &&
            resident_map.snapshot().find_node(resident_id)->type == NodeType::Shop &&
            resident_map.snapshot().find_node(resident_id)->name == "诡异行商",
        "a genuinely different preview type still changes the routing identity");
    check(error.empty(), "no model error");
    std::cout << "MAP_PREVIEW checks=" << checks << " failures=" << failures << " game_inputs=0\n";
    return failures == 0 ? 0 : 1;
}
