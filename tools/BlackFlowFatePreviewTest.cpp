#include <algorithm>
#include <filesystem>
#include <iostream>

#include "AsstCaller.h"
#include "Config/Roguelike/BlackFlow/BlackFlowNodeExecutionConfig.h"
#include "MaaUtils/Encoding.h"
#include "Task/Roguelike/BlackFlow/BlackFlowModel.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    int checks = 0, failures = 0;
    const auto expect = [&](bool passed, const char* label) {
        ++checks;
        if (!passed) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const std::filesystem::path repo(argv[1]), out(argv[2]);
    std::filesystem::create_directories(out);
    AsstSetUserDir(MAA_NS::from_u16(out.native()).c_str());
    expect(AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str()), "full resources accept the Fate node type");
    expect(
        node_type_from_string("fate") == NodeType::Fate && to_string(NodeType::Fate) == "fate",
        "Fate type round trips");
    expect(
        BlackFlowNodeExecution.preview_node_type("命运所指") == NodeType::Fate,
        "live OCR title is an accepted preview name");
    const auto traversal = default_traversal_for(NodeType::Fate);
    expect(
        traversal.enterable && traversal.repeatable,
        "Fate remains repeatable rather than becoming an ordinary incident");
    expect(
        !is_combat_node_type(NodeType::Fate) && !is_transfer_node(NodeType::Fate),
        "Fate is neither combat nor a paired door");
    expect(
        std::ranges::find(all_target_node_types(), NodeType::Fate) != all_target_node_types().end(),
        "walk target types include Fate");
    const auto route =
        BlackFlowNodeExecution.resolve_route({ .floor = 5, .node_type = NodeType::Fate, .page_intent = "default" });
    expect(
        route && route->get().task == "BlackFlow@Roguelike@Page-Encounter",
        "Fate uses the encounter dispatch chain");

    NormalizedMap map;
    MapObservationBatch batch;
    batch.floor = 5;
    ObservedNode observed;
    observed.position = { 4, 2 };
    observed.type = NodeType::Unknown;
    observed.name = "";
    observed.identity_state = NodeIdentityState::Unclassified;
    observed.identity_revealed = false;
    batch.nodes.emplace_back(observed);
    std::string error;
    expect(map.merge(batch, &error), "import the real unclassified map-node shape");
    const auto id = *make_stable_node_id(5, observed.position);
    MovePreview preview;
    preview.reachability = PreviewReachability::Reachable;
    preview.displayed_type = NodeType::Fate;
    preview.displayed_name = "命运所指";
    preview.identity_revealed = true;
    bool changed = false;
    expect(map.accept_preview(id, preview, changed, &error) && changed, "preview reveals the Fate identity");
    for (int frame = 0; frame < 3; ++frame) {
        expect(map.merge(batch, &error), "refresh an unclassified map observation");
        const auto* node = map.snapshot().find_node(id);
        expect(
            node && node->type == NodeType::Fate && node->name == "命运所指" && node->traversal.repeatable &&
                node->identity_state == NodeIdentityState::Classified,
            "missing glyph classification cannot erase verified Fate identity");
        expect(map.accept_preview(id, preview, changed, &error) && !changed, "repeated Fate preview converges");
    }
    batch.nodes.front().type = NodeType::BattleNormal;
    batch.nodes.front().name = "作战";
    batch.nodes.front().identity_state = NodeIdentityState::Classified;
    batch.nodes.front().identity_revealed = true;
    expect(
        map.merge(batch, &error) && map.snapshot().find_node(id)->type == NodeType::BattleNormal,
        "a positively classified replacement still updates the node");
    std::cout << "FATE_NODE checks=" << checks << " failures=" << failures << " connections=0 inputs=0\n";
    return failures == 0 ? 0 : 1;
}
