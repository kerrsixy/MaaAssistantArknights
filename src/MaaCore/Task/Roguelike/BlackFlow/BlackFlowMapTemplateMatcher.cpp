#include "BlackFlowMapTemplateMatcher.h"

#include <algorithm>
#include <set>

namespace asst::blackflow
{
const MapTemplate* match_map_template(const MapObservationBatch& observation, std::span<const MapTemplate> templates)
{
    using MapEdges = std::set<std::pair<GridPosition, GridPosition>>;
    MapEdges observed;
    // 推断边与直接识别的边同等有效；未识别到的模板边不影响匹配。
    for (const auto& edge : observation.edges) {
        if (edge.knowledge != EdgeKnowledge::Absent) {
            const auto endpoints = std::minmax(edge.first, edge.second);
            observed.emplace(endpoints.first, endpoints.second);
        }
    }
    const MapTemplate* selected = nullptr;
    for (const auto& definition : templates) {
        if (definition.floor != observation.floor) {
            continue;
        }
        MapEdges expected;
        for (const auto& [first, second] : definition.edges) {
            const auto endpoints = std::minmax(first, second);
            expected.emplace(endpoints.first, endpoints.second);
        }
        if (!std::ranges::includes(expected, observed)) {
            continue;
        }
        // 匹配有歧义时放弃补全，不影响实际地图。
        if (selected != nullptr) {
            return nullptr;
        }
        selected = &definition;
    }
    return selected;
}

void supplement_map_exits(MapObservationBatch& observation, std::span<const GridPosition> exits, NodeType exit_type)
{
    // 第二层只有一个出口。模板末端包含可能位置，不代表它们同时是出口。
    // 当前帧已识别到出口时以实测为准；否则仅补全唯一未揭示的候选。
    std::optional<GridPosition> floor2_candidate;
    if (observation.floor == 2 && exit_type == NodeType::Final) {
        if (std::ranges::any_of(observation.nodes, [](const auto& node) {
                return node.type == NodeType::Final && !node.identity_from_template.value_or(false);
            })) {
            return;
        }
        for (const auto& position : exits) {
            const auto node = std::ranges::find(observation.nodes, position, &ObservedNode::position);
            if (node == observation.nodes.end() ||
                (node->type.has_value() && node->type != NodeType::Unknown && node->type != NodeType::HideInvisible &&
                 !node->identity_from_template.value_or(false))) {
                continue;
            }
            if (floor2_candidate.has_value()) {
                // 仍有多个可能时保留未知，交给节点预览取证，不伪造双出口。
                return;
            }
            floor2_candidate = position;
        }
        if (!floor2_candidate) {
            return;
        }
    }
    // Template terminals are only positive hints for this observed topology. A floor may
    // refresh its exit to another position (including hide_invisible), so an unmatched
    // terminal must remain unknown and must not be synthesized as a fixed exit node.
    for (const auto& position : exits) {
        if (floor2_candidate && position != *floor2_candidate) {
            continue;
        }
        auto node = std::ranges::find(observation.nodes, position, &ObservedNode::position);
        if (node == observation.nodes.end()) {
            // 仅补全已识别节点的身份，避免创建没有点击坐标的移动目标。
            continue;
        }
        // 识别本来就认出是险路尽头的不算模板补全；其余的在游戏里还没揭开，紧急逃生座椅去不了。
        // 险路恶敌一定是揭开的，模板只补上识别漏掉的身份。
        node->identity_from_template = exit_type == NodeType::Final && node->type != NodeType::Final;
        node->type = exit_type;
        node->name = exit_type == NodeType::BattleBoss ? "险路恶敌" : "险路尽头";
        node->identity_state = NodeIdentityState::Classified;
        node->identity_revealed = !node->identity_from_template.value_or(false);
    }
}
} // namespace asst::blackflow
