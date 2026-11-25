#pragma once

#include "BlackFlowModel.h"

namespace asst::blackflow
{
struct PreviewProgressScope
{
    std::uint64_t run = 0;
    int floor = 0;
    NodeId current = InvalidNodeId;
    NodeId target = InvalidNodeId;
    NodeId landing = InvalidNodeId;
    MovementKind movement = MovementKind::Walk;
    NodeType visual_type = NodeType::Unknown;
    NodeProgress progress = NodeProgress::Active;
    bool operator==(const PreviewProgressScope&) const noexcept = default;
};

class PreviewConflictProgress
{
public:
    // One clean refresh may resolve a conflict. IDs/revisions/time are not progress.
    bool repeated(const PreviewProgressScope& scope)
    {
        if (!m_scope || *m_scope != scope) {
            m_scope = scope;
            return false;
        }
        return true;
    }

    void reset() noexcept { m_scope.reset(); }

private:
    std::optional<PreviewProgressScope> m_scope;
};
}
