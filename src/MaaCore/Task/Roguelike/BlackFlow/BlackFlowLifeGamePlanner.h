#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <meojson/json.hpp>

namespace asst::blackflow
{
struct LifeGameNode
{
    std::string id;
    std::string name;
    std::string group_id;
    bool purchasable = false;
    int cost = 0;
    int row = 0;
    int order = 0;
    int priority = 0;
    std::string priority_basis;
    std::vector<std::string> prerequisites;
    std::vector<std::string> successors;
    std::vector<std::string> raw_effects;
};

struct LifeGameEvidence
{
    // Absent IDs remain unknown. Gates belong here only after their active state is verified.
    std::vector<std::string> purchased_ids;
    std::vector<std::string> unpurchased_ids;
    std::optional<int> available_points;
};

enum class LifeGamePlanStatus
{
    Ready,
    NoPoints,
    InsufficientPoints,
    Complete,
    UnknownState,
    PrerequisitesBlocked,
    InvalidEvidence,
    ResourceUnavailable,
};

struct LifeGamePlan
{
    LifeGamePlanStatus status = LifeGamePlanStatus::ResourceUnavailable;
    std::optional<LifeGameNode> next;
    std::vector<std::string> unknown_ids;
    std::string reason;
};

enum class LifeGameRelativePosition
{
    EarlierRow,
    SameRow,
    LaterRow,
    EarlierOrder,
    SameOrder,
    LaterOrder,
    HasPrerequisite,
    HasSuccessor,
};

struct LifeGameIdentityConstraint
{
    std::string anchor_id;
    LifeGameRelativePosition relation = LifeGameRelativePosition::SameRow;
};

struct LifeGameIdentityEvidence
{
    std::string exact_name;
    std::optional<std::string> verified_group_id;
    // These IDs must be positively identified on the same group. Page-switch counts,
    // icon brightness and a clipped viewport are not group evidence.
    std::vector<std::string> same_group_anchor_ids;
    std::vector<LifeGameIdentityConstraint> constraints;
};

enum class LifeGameIdentityStatus
{
    Unique,
    Ambiguous,
    NoMatch,
    InvalidEvidence,
    ResourceUnavailable,
};

struct LifeGameIdentityResult
{
    LifeGameIdentityStatus status = LifeGameIdentityStatus::ResourceUnavailable;
    std::optional<std::string> node_id;
    std::vector<std::string> candidate_ids;
    std::string reason;
};

// Pure planning only: selection never changes evidence or spends points. Callers must
// observe the result of each purchase before requesting another plan.
class BlackFlowLifeGamePlanner
{
public:
    bool load(const json::value& resource, std::string* error = nullptr);
    [[nodiscard]] LifeGamePlan next(const LifeGameEvidence& evidence) const;
    // Relative order constraints apply only within one graph row. A direct-edge
    // constraint requires an observed connection, not guessed nearest neighbors.
    [[nodiscard]] LifeGameIdentityResult identify(const LifeGameIdentityEvidence& evidence) const;

    [[nodiscard]] const std::vector<LifeGameNode>& nodes() const noexcept { return m_nodes; }

private:
    std::vector<LifeGameNode> m_nodes;
    std::unordered_map<std::string, std::size_t> m_index;
};
}
