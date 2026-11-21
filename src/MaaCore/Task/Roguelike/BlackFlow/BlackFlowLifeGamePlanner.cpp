#include "BlackFlowLifeGamePlanner.h"

#include <algorithm>
#include <set>
#include <tuple>
#include <unordered_set>

bool asst::blackflow::BlackFlowLifeGamePlanner::load(const json::value& resource, std::string* error)
{
    const auto fail = [error](const std::string& message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    const auto entries = resource.find<json::array>("nodes");
    if (resource.get("schema_version", 0) != 1 || resource.get("expected_node_count", 0) != 67 || !entries ||
        entries->size() != 67 || resource.get("prerequisite_policy", std::string()) != "all_verified_conservative") {
        return fail("Invalid Life Game schema, node count or prerequisite policy");
    }
    std::vector<LifeGameNode> nodes;
    std::unordered_map<std::string, std::size_t> index;
    std::set<std::pair<int, int>> positions;
    std::unordered_set<std::string> groups;
    int purchase_count = 0;
    for (const auto& entry : *entries) {
        LifeGameNode node;
        node.id = entry.get("id", std::string());
        node.name = entry.get("name", std::string());
        node.group_id = entry.get("group_id", std::string());
        const auto type = entry.get("node_type", std::string());
        node.purchasable = type == "NORMAL" || type == "KEY";
        node.cost = entry.get("cost", -1);
        node.row = entry.get("row", 0);
        node.order = entry.get("order", 0);
        node.priority = entry.get("priority", -1);
        node.priority_basis = entry.get("priority_basis", std::string());
        const auto prerequisites = entry.find<std::vector<std::string>>("prerequisites");
        const auto successors = entry.find<std::vector<std::string>>("successors");
        const auto effects = entry.find<std::vector<std::string>>("raw_effects");
        if (node.id.empty() || node.name.empty() ||
            (node.group_id != "group_1" && node.group_id != "group_2" && node.group_id != "group_3" &&
             node.group_id != "group_4" && node.group_id != "group_5") ||
            (type != "NORMAL" && type != "KEY" && type != "DIFFICULTY") ||
            (node.purchasable ? node.cost <= 0 : node.cost != 0) || node.row <= 0 || node.order <= 0 ||
            node.priority < 0 || node.priority_basis.empty() || !prerequisites || !successors || !effects ||
            effects->empty() || !index.emplace(node.id, nodes.size()).second ||
            !positions.emplace(node.row, node.order).second) {
            return fail(
                "Invalid or duplicate Life Game node: " + node.id + " type=" + type +
                " cost=" + std::to_string(node.cost) + " priority=" + std::to_string(node.priority) + " arrays=" +
                std::to_string(bool(prerequisites)) + std::to_string(bool(successors)) + std::to_string(bool(effects)));
        }
        node.prerequisites = *prerequisites;
        node.successors = *successors;
        node.raw_effects = *effects;
        groups.insert(node.group_id);
        purchase_count += node.purchasable ? 1 : 0;
        nodes.emplace_back(std::move(node));
    }
    if (purchase_count != 64 || groups.size() != 5) {
        return fail("Life Game must contain 64 purchase nodes, 3 read-only gates and 5 groups");
    }
    std::vector<std::size_t> indegree(nodes.size());
    std::vector<std::size_t> ready;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        std::unordered_set<std::string> fronts;
        std::unordered_set<std::string> backs;
        for (const auto& id : node.prerequisites) {
            const auto found = index.find(id);
            if (found == index.end() || id == node.id || !fronts.insert(id).second ||
                std::ranges::find(nodes[found->second].successors, node.id) == nodes[found->second].successors.end()) {
                return fail("Invalid Life Game prerequisite edge");
            }
        }
        for (const auto& id : node.successors) {
            const auto found = index.find(id);
            if (found == index.end() || id == node.id || !backs.insert(id).second ||
                std::ranges::find(nodes[found->second].prerequisites, node.id) ==
                    nodes[found->second].prerequisites.end()) {
                return fail("Invalid Life Game successor edge");
            }
        }
        indegree[i] = node.prerequisites.size();
        if (indegree[i] == 0) {
            ready.emplace_back(i);
        }
    }
    if (ready.size() != 1) {
        return fail("Life Game must have one connected root");
    }
    std::size_t visited = 0;
    while (!ready.empty()) {
        const auto current = ready.back();
        ready.pop_back();
        ++visited;
        for (const auto& id : nodes[current].successors) {
            const auto successor = index.at(id);
            if (--indegree[successor] == 0) {
                ready.emplace_back(successor);
            }
        }
    }
    if (visited != nodes.size()) {
        return fail("Life Game graph contains a cycle or disconnected nodes");
    }
    m_nodes = std::move(nodes);
    m_index = std::move(index);
    if (error) {
        error->clear();
    }
    return true;
}

asst::blackflow::LifeGamePlan asst::blackflow::BlackFlowLifeGamePlanner::next(const LifeGameEvidence& evidence) const
{
    LifeGamePlan result;
    if (m_nodes.empty()) {
        result.reason = "Life Game resource has not been loaded";
        return result;
    }
    std::unordered_set<std::string> purchased;
    std::unordered_set<std::string> unpurchased;
    for (const auto& id : evidence.purchased_ids) {
        if (!m_index.contains(id) || !purchased.insert(id).second) {
            result.status = LifeGamePlanStatus::InvalidEvidence;
            result.reason = "Unknown or duplicate purchased ID";
            return result;
        }
    }
    for (const auto& id : evidence.unpurchased_ids) {
        if (!m_index.contains(id) || purchased.contains(id) || !unpurchased.insert(id).second) {
            result.status = LifeGamePlanStatus::InvalidEvidence;
            result.reason = "Unknown, duplicate or contradictory unpurchased ID";
            return result;
        }
    }
    if (evidence.available_points && *evidence.available_points < 0) {
        result.status = LifeGamePlanStatus::InvalidEvidence;
        result.reason = "Available points cannot be negative";
        return result;
    }
    if (std::ranges::all_of(m_nodes, [&purchased](const auto& node) {
            return !node.purchasable || purchased.contains(node.id);
        })) {
        result.status = LifeGamePlanStatus::Complete;
        result.reason = "Every purchasable node is verified purchased";
        return result;
    }
    if (!evidence.available_points) {
        result.status = LifeGamePlanStatus::UnknownState;
        result.reason = "Available point balance is unverified";
        return result;
    }
    if (*evidence.available_points == 0) {
        result.status = LifeGamePlanStatus::NoPoints;
        result.reason = "Verified zero points; no purchase is possible";
        return result;
    }
    const LifeGameNode* best = nullptr;
    bool unaffordable = false;
    std::set<std::string> unknown;
    for (const auto& node : m_nodes) {
        if (!node.purchasable || purchased.contains(node.id)) {
            continue;
        }
        if (!unpurchased.contains(node.id)) {
            unknown.insert(node.id);
            continue;
        }
        bool prerequisites_met = true;
        for (const auto& id : node.prerequisites) {
            if (!purchased.contains(id)) {
                prerequisites_met = false;
                if (!unpurchased.contains(id)) {
                    unknown.insert(id);
                }
            }
        }
        if (!prerequisites_met) {
            continue;
        }
        if (node.cost > *evidence.available_points) {
            unaffordable = true;
            continue;
        }
        if (!best || node.priority > best->priority ||
            (node.priority == best->priority && std::tie(node.cost, node.row, node.order, node.id) <
                                                    std::tie(best->cost, best->row, best->order, best->id))) {
            best = &node;
        }
    }
    result.unknown_ids.assign(unknown.begin(), unknown.end());
    if (best) {
        result.status = LifeGamePlanStatus::Ready;
        result.next = *best;
        result.reason =
            "Verified unpurchased, all prerequisite states verified active, affordable; highest resource priority";
    }
    else if (unaffordable) {
        result.status = LifeGamePlanStatus::InsufficientPoints;
        result.reason = "Verified eligible nodes exceed the available point balance";
    }
    else if (!unknown.empty()) {
        result.status = LifeGamePlanStatus::UnknownState;
        result.reason = "Missing node or prerequisite evidence; absent IDs are not assumed unpurchased";
    }
    else {
        result.status = LifeGamePlanStatus::PrerequisitesBlocked;
        result.reason =
            "No purchasable node has all prerequisite states verified active; gates are never auto-purchased";
    }
    return result;
}

asst::blackflow::LifeGameIdentityResult
    asst::blackflow::BlackFlowLifeGamePlanner::identify(const LifeGameIdentityEvidence& evidence) const
{
    LifeGameIdentityResult result;
    if (m_nodes.empty()) {
        result.reason = "Life Game resource has not been loaded";
        return result;
    }
    const auto invalid = [&result](const std::string& reason) {
        result.status = LifeGameIdentityStatus::InvalidEvidence;
        result.reason = reason;
        return result;
    };
    if (evidence.exact_name.empty()) {
        return invalid("Exact node title is missing");
    }
    auto group = evidence.verified_group_id;
    if (group && !std::ranges::any_of(m_nodes, [&group](const auto& node) { return node.group_id == *group; })) {
        return invalid("Unknown verified group ID");
    }
    std::unordered_set<std::string> anchors;
    for (const auto& id : evidence.same_group_anchor_ids) {
        const auto found = m_index.find(id);
        if (found == m_index.end() || !anchors.insert(id).second) {
            return invalid("Unknown or duplicate same-group anchor ID");
        }
        const auto& anchor_group = m_nodes[found->second].group_id;
        if (group && *group != anchor_group) {
            return invalid("Same-group anchors contradict the verified group");
        }
        group = anchor_group;
    }
    for (const auto& constraint : evidence.constraints) {
        const auto found = m_index.find(constraint.anchor_id);
        if (found == m_index.end()) {
            return invalid("Unknown constraint anchor ID");
        }
        // A real prerequisite/successor edge may cross a group boundary. Global
        // row constraints also do not assert same-group membership. Only the
        // explicit group and same-group anchors above may restrict that domain.
        if (constraint.relation < LifeGameRelativePosition::EarlierRow ||
            constraint.relation > LifeGameRelativePosition::HasSuccessor) {
            return invalid("Unknown identity relation");
        }
    }
    for (const auto& node : m_nodes) {
        if (node.name != evidence.exact_name || (group && node.group_id != *group)) {
            continue;
        }
        bool matches = true;
        for (const auto& constraint : evidence.constraints) {
            const auto& anchor = m_nodes[m_index.at(constraint.anchor_id)];
            switch (constraint.relation) {
            case LifeGameRelativePosition::EarlierRow:
                matches = node.row < anchor.row;
                break;
            case LifeGameRelativePosition::SameRow:
                matches = node.row == anchor.row;
                break;
            case LifeGameRelativePosition::LaterRow:
                matches = node.row > anchor.row;
                break;
            case LifeGameRelativePosition::EarlierOrder:
                matches = node.row == anchor.row && node.order < anchor.order;
                break;
            case LifeGameRelativePosition::SameOrder:
                matches = node.row == anchor.row && node.order == anchor.order;
                break;
            case LifeGameRelativePosition::LaterOrder:
                matches = node.row == anchor.row && node.order > anchor.order;
                break;
            case LifeGameRelativePosition::HasPrerequisite:
                matches = std::ranges::find(node.prerequisites, anchor.id) != node.prerequisites.end();
                break;
            case LifeGameRelativePosition::HasSuccessor:
                matches = std::ranges::find(node.successors, anchor.id) != node.successors.end();
                break;
            default:
                return invalid("Unknown identity relation");
            }
            if (!matches) {
                break;
            }
        }
        if (matches) {
            result.candidate_ids.emplace_back(node.id);
        }
    }
    if (result.candidate_ids.empty()) {
        result.status = LifeGameIdentityStatus::NoMatch;
        result.reason = "No resource node satisfies the supplied title and positive constraints";
    }
    else if (result.candidate_ids.size() == 1) {
        result.status = LifeGameIdentityStatus::Unique;
        result.node_id = result.candidate_ids.front();
        result.reason = "Exactly one node satisfies the supplied title and positive constraints";
    }
    else {
        result.status = LifeGameIdentityStatus::Ambiguous;
        result.reason = "Several resource nodes remain; obtain another positive anchor or observed edge";
    }
    return result;
}
