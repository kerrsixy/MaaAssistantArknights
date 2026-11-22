#pragma once

#include "SPADeploymentReview.h"
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace asst::spa
{
using DeploymentCellLedger = std::map<std::string, BoardCell>;

inline std::optional<DeploymentCellLedger> deployment_cell_ledger(const std::vector<DeploymentUnit>& units)
{
    DeploymentCellLedger result;
    std::set<BoardCell> occupied;
    for (const auto& unit : units) {
        if (unit.id.empty() || !result.emplace(unit.id, unit.cell).second || !occupied.insert(unit.cell).second) {
            return std::nullopt;
        }
    }
    return result.empty() ? std::nullopt : std::optional(result);
}

// This is an expected outcome for the next observed frame, not a committed
// position ledger. The executor must still confirm identity and coordinates.
inline std::optional<DeploymentCellLedger>
    deployment_cells_after_step(const std::vector<DeploymentUnit>& current, const DeploymentStep& step)
{
    auto result = deployment_cell_ledger(current);
    if (!result || !result->contains(step.unit) || result->at(step.unit) != step.from ||
        std::string("ESWN").find(step.facing) == std::string::npos) {
        return std::nullopt;
    }
    const auto occupant =
        std::find_if(result->begin(), result->end(), [&](const auto& item) { return item.second == step.to; });
    if (step.kind == DeploymentStepKind::Face) {
        return step.from == step.to && step.displaced.empty() ? result : std::nullopt;
    }
    if (step.from == step.to) {
        return std::nullopt;
    }
    if (step.kind == DeploymentStepKind::Swap) {
        if (step.displaced.empty() || step.displaced == step.unit || occupant == result->end() ||
            occupant->first != step.displaced) {
            return std::nullopt;
        }
        result->at(step.displaced) = step.from;
    }
    else if (step.kind != DeploymentStepKind::Move || !step.displaced.empty() || occupant != result->end()) {
        return std::nullopt;
    }
    result->at(step.unit) = step.to;
    return result;
}

enum class DeploymentPlanContinuationKind
{
    Step,
    RestoreFacing,
    Complete,
    Deferred,
    Invalidated
};

struct DeploymentPlanContinuation
{
    DeploymentPlanContinuationKind kind = DeploymentPlanContinuationKind::Deferred;
    std::optional<DeploymentStep> step;
    std::string reason;
};

// A fixed target belongs to one round, roster/forms, equipment/pacts and map
// context. Only the caller can verify that context and the field identities.
// Unknown facing after a verified swap is not an external layout change.
inline DeploymentPlanContinuation continue_deployment_plan(
    const std::vector<DeploymentUnit>& current,
    const std::vector<DeploymentUnit>& target,
    const std::set<std::string>& directions_known,
    const DeploymentCellLedger& expected_cells,
    bool context_matches,
    int state_budget = 1500)
{
    const auto observed_cells = deployment_cell_ledger(current);
    const auto target_cells = deployment_cell_ledger(target);
    if (!context_matches || !observed_cells || !target_cells || *observed_cells != expected_cells ||
        current.size() != target.size()) {
        return { DeploymentPlanContinuationKind::Invalidated, std::nullopt, "External context or field cells changed" };
    }

    auto effective_target = target;
    auto continuation_board = current;
    for (auto& unit : continuation_board) {
        const auto goal = std::find_if(effective_target.begin(), effective_target.end(), [&](const auto& item) {
            return item.id == unit.id;
        });
        if (goal == effective_target.end() || std::string("ESWN").find(goal->facing) == std::string::npos) {
            return { DeploymentPlanContinuationKind::Invalidated,
                     std::nullopt,
                     "Cached target identities are inconsistent" };
        }
        if (!goal->movable) {
            // A unit whose facing was already unknown at planning time was
            // deliberately immovable. A later observed direction must not
            // turn the original fallback E into a new Face instruction.
            goal->facing = unit.facing;
            continue;
        }
        if (directions_known.contains(unit.id)) {
            continue;
        }
        if (std::find(goal->placement_cells.begin(), goal->placement_cells.end(), unit.cell) ==
            goal->placement_cells.end()) {
            return { DeploymentPlanContinuationKind::Invalidated,
                     std::nullopt,
                     "Observed cell is outside cached unit terrain" };
        }
        // The cached plan gives this unit a legal destination and an explicit
        // facing. Its last verified swap accounts for its current cell. Keep
        // the observed facing unknown until the verified Move or Face commits.
        unit.facing = '\0';
        unit.movable = true;
        unit.placement_cells = goal->placement_cells;
    }

    const auto sequence = sequence_deployment(continuation_board, effective_target, state_budget);
    if (!sequence.complete) {
        return { DeploymentPlanContinuationKind::Deferred,
                 std::nullopt,
                 "Existing target awaits bounded reconciliation" };
    }
    if (sequence.steps.empty()) {
        return { DeploymentPlanContinuationKind::Complete, std::nullopt, "Existing target reached" };
    }
    const auto& step = sequence.steps.front();
    if (step.kind == DeploymentStepKind::Face && !directions_known.contains(step.unit)) {
        return { DeploymentPlanContinuationKind::RestoreFacing, step, "Restore facing at the planned cell" };
    }
    return { DeploymentPlanContinuationKind::Step, step, "Continue the existing target" };
}
}
