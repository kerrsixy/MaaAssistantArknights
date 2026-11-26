#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "BlackFlowTaskPort.h"

#include "Utils/Logger.hpp"

namespace asst::blackflow
{
enum class RoutingCycleStatus
{
    MoveCommitted,
    MoveCommittedToMap,
    MovementSelectionRequired,
    MovementInventoryObservationRequired,
    ReplanRequired,
    PreviewNeedsDismiss,
    PreviewNeedsReplan,
    InventoryCleaned,
    ConfirmationNeedsDismiss,
    SessionTerminated,
    NeedsPageRecovery,
    Failed,
    NoSafeStageAlternative,
};

struct RoutingCycleOutcome
{
    RoutingCycleStatus status = RoutingCycleStatus::Failed;
    std::string failure_code;
    std::string error;
};

template <typename Session>
bool refresh_with_retries(Session& session, IBlackFlowTaskPort& port, std::string* error)
{
    const std::optional<int> current_floor = session.current_floor();
    if (!current_floor.has_value()) {
        if (error != nullptr) {
            *error = "current floor has not been recognized by NextLevel";
        }
        return false;
    }

    std::string latest_error;
    // 战斗/事件返回地图时，当前标记可能连续数帧不可见；给视觉层更多
    // 重试机会，避免把短暂过渡帧误判成整层地图失效并重开本局。
    for (int attempt = 0; attempt < 5; ++attempt) {
        BlackFlowObservationRequest request;
        request.floor = *current_floor;
        request.attempt_count = attempt + 1;
        request.verified_difficulty = session.observed_difficulty();
        request.run_revision = session.run_revision();
        BlackFlowPerceptionSnapshot snapshot;
        std::string current_error;
        if (port.refresh(request, snapshot, &current_error) && session.update(snapshot, &current_error)) {
            return true;
        }
        const auto* failure = current_error.empty() ? "unknown" : current_error.c_str();
        if (attempt == 0) {
            Log.info(
                "BlackFlow map rebuild attempt failed",
                "floor",
                request.floor,
                "attempt",
                attempt + 1,
                "of",
                5,
                "error",
                failure);
        }
        else {
            Log.debug(
                "BlackFlow map rebuild attempt failed",
                "floor",
                request.floor,
                "attempt",
                attempt + 1,
                "of",
                5,
                "error",
                failure);
        }
        if (!current_error.empty()) {
            latest_error = std::move(current_error);
        }
    }
    if (error != nullptr) {
        *error = latest_error.empty() ? "map rebuild failed twice" : std::move(latest_error);
    }
    return false;
}

template <typename Session>
bool session_requires_movement_inventory_observation(const Session& session)
{
    if constexpr (requires { session.movement_inventory_refresh_required(); }) {
        return session.movement_inventory_refresh_required();
    }
    else {
        return false;
    }
}

template <typename Session>
bool session_requires_map_settle(Session& session)
{
    if constexpr (requires {
                      session.map_settle_required();
                      session.mark_map_settled();
                  }) {
        if (!session.map_settle_required()) {
            return false;
        }
        session.mark_map_settled();
        return true;
    }
    else {
        return false;
    }
}

template <typename Session>
bool validate_session_commit(Session& session, std::string* error)
{
    if constexpr (requires { session.validate_commit(error); }) {
        return session.validate_commit(error);
    }
    else {
        return true;
    }
}

template <typename Session>
bool session_requires_page_dispatch(const Session& session)
{
    if constexpr (requires { session.page_dispatch_required(); }) {
        return session.page_dispatch_required();
    }
    else {
        return true;
    }
}

template <typename Session>
RoutingCycleOutcome execute_preview_cycle(Session& session, IBlackFlowTaskPort& port)
{
    std::string error;
    if (session.transaction() == nullptr) {
        return { RoutingCycleStatus::Failed,
                 "transaction_proposal_failed",
                 "move preview has no proposed transaction" };
    }

    MovePreview preview;
    bool panel_open = false;
    const MoveCandidate candidate = session.transaction()->proposal();
    if (!port.preview(candidate, session.viewport(), preview, panel_open, &error)) {
        session.cancel_transaction();
        if (panel_open) {
            return { RoutingCycleStatus::PreviewNeedsDismiss, "move_preview_failed", std::move(error) };
        }
        return { RoutingCycleStatus::Failed, "move_preview_failed", std::move(error) };
    }
    const PreviewDisposition disposition = session.accept_preview(std::move(preview), &error);
    if constexpr (requires { session.terminated(); }) {
        if (session.terminated()) {
            return { RoutingCycleStatus::SessionTerminated, "preview_identity_conflict_no_progress", std::move(error) };
        }
    }
    if (disposition == PreviewDisposition::ReplanAfterDismiss) {
        return { RoutingCycleStatus::PreviewNeedsReplan, {}, {} };
    }
    if (disposition == PreviewDisposition::Failed || session.transaction() == nullptr) {
        session.cancel_transaction();
        return { RoutingCycleStatus::PreviewNeedsDismiss, "move_preview_rejected", std::move(error) };
    }
    if (!validate_session_commit(session, &error)) {
        session.cancel_transaction();
        return { RoutingCycleStatus::PreviewNeedsDismiss, "move_confirmation_invalidated", std::move(error) };
    }
    EnteredPageObservation entered_page;
    const MoveConfirmationStatus confirmation = port.confirm(*session.transaction(), entered_page, &error);
    if (confirmation == MoveConfirmationStatus::InventoryCleaned) {
        session.cancel_transaction();
        if constexpr (requires { session.request_movement_inventory_observation(); }) {
            session.request_movement_inventory_observation();
        }
        return { RoutingCycleStatus::InventoryCleaned, {}, {} };
    }
    if (confirmation == MoveConfirmationStatus::NeedsDismiss) {
        session.cancel_transaction();
        return { RoutingCycleStatus::ConfirmationNeedsDismiss, "move_confirmation_exhausted", std::move(error) };
    }
    if (confirmation == MoveConfirmationStatus::Failed) {
        session.cancel_transaction();
        return { RoutingCycleStatus::Failed, "move_confirmation_failed", std::move(error) };
    }
    if (!session.commit(std::move(entered_page), &error)) {
        return { RoutingCycleStatus::Failed, "move_confirmation_state_failed", std::move(error) };
    }
    return { session_requires_page_dispatch(session) ? RoutingCycleStatus::MoveCommitted
                                                     : RoutingCycleStatus::MoveCommittedToMap,
             {},
             {} };
}

template <typename Session>
RoutingCycleOutcome execute_pending_routing_cycle(Session& session, IBlackFlowTaskPort& port)
{
    std::string error;
    if (!session.begin_pending_transaction(&error)) {
        session.clear_pending_candidate();
        return { RoutingCycleStatus::ReplanRequired, "pending_movement_invalidated", std::move(error) };
    }
    return execute_preview_cycle(session, port);
}

// Stack-only provenance for the mandatory refresh in this routing call.
// A GUI/pending replan cannot opt in, and unknown state retains the old preflight.
template <typename Session>
std::optional<std::array<std::uint64_t, 7>> routing_observation_stamp(Session& session)
{
    if constexpr (requires {
                      session.run_revision();
                      session.movement_inventory_refresh_required();
                      session.map_settle_required();
                      session.current_floor();
                      session.map().floor();
                      session.map().snapshot();
                      session.viewport().viewport_revision();
                      session.pending_candidate();
                      session.transaction();
                      session.page_context();
                  }) {
        const auto floor = session.current_floor();
        const auto& map = session.map().snapshot();
        if (!floor || *floor <= 0 || session.run().floor != *floor || session.map().floor() != *floor ||
            session.run_revision() == 0 || map.revision == 0 || session.viewport().viewport_revision() == 0 ||
            map.find_node(session.run().current_node) == nullptr || session.movement_inventory_refresh_required() ||
            session.map_settle_required() || session.pending_candidate() || session.transaction() ||
            session.page_context()) {
            return std::nullopt;
        }
        for (const auto& edge : map.edges()) {
            if (edge.knowledge == EdgeKnowledge::Unknown || edge.evidence.forced_by_connectivity_constraint) {
                return std::nullopt;
            }
        }
        return std::array<std::uint64_t, 7> {
            session.run_revision(),
            static_cast<std::uint64_t>(*floor),
            map.revision,
            session.viewport().viewport_revision(),
            session.run().current_node,
            session.run().resources_revision,
            session.run().costs.revision,
        };
    }
    return std::nullopt;
}

template <typename Session>
RoutingCycleOutcome
    execute_replanning_cycle(Session& session, IBlackFlowTaskPort& port, bool mandatory_map_refreshed_this_call = false)
{
    std::string error;
    if (session.terminated()) {
        return { RoutingCycleStatus::SessionTerminated, {}, {} };
    }
    if constexpr (requires { session.ghost_preview_stop_pending(); }) {
        if (session.ghost_preview_stop_pending()) {
            return { RoutingCycleStatus::NoSafeStageAlternative,
                     "ghost_preview_identity_unverified",
                     "listed combat title could not be bound; stopped after verified map cancellation" };
        }
    }
    const auto before_planning = mandatory_map_refreshed_this_call ? routing_observation_stamp(session) : std::nullopt;
    BlackFlowPlan plan = session.plan(&error);
    if (!plan) {
        if constexpr (requires { session.has_avoided_stage_nodes(); }) {
            if (session.has_avoided_stage_nodes()) {
                return { RoutingCycleStatus::NoSafeStageAlternative,
                         "no_safe_ghost_alternative",
                         "no confirmed safe route remains with source-confirmed combat exclusions: " + error };
            }
        }
        Log.error(
            "BlackFlow planning failed",
            "error",
            error,
            "current node",
            session.run().current_node,
            "floor",
            session.run().floor);
        return { RoutingCycleStatus::Failed, "planning_failed", std::move(error) };
    }
    const MoveCandidate candidate = *plan.decision.selected;

    // Only a same-call mandatory refresh followed by unchanged, memory-only
    // planning can satisfy this preflight. Independent/GUI/pending replans
    // retain the full refresh below; preview still verifies identity and cost.
    const bool current_observation_verified = before_planning &&
                                              before_planning == routing_observation_stamp(session) &&
                                              !candidate.requires_preview_verification;
    if constexpr (requires { session.map(); }) {
        const Node* target = session.map().snapshot().find_node(candidate.target);
        if (target != nullptr && is_combat_node_type(target->type) && !current_observation_verified) {
            const std::uint64_t map_revision_before = session.map().snapshot().revision;
            const NodeId current_node_before = session.run().current_node;
            const std::uint64_t resources_revision_before = session.run().resources_revision;
            const std::uint64_t cost_revision_before = session.run().costs.revision;
            if (!refresh_with_retries(session, port, &error)) {
                return { RoutingCycleStatus::NeedsPageRecovery, "ghost_map_preflight_failed", std::move(error) };
            }
            const std::uint64_t map_revision_after = session.map().snapshot().revision;
            const NodeId current_node_after = session.run().current_node;
            const std::uint64_t resources_revision_after = session.run().resources_revision;
            const std::uint64_t cost_revision_after = session.run().costs.revision;
            if (current_node_after != current_node_before || resources_revision_after != resources_revision_before ||
                cost_revision_after != cost_revision_before) {
                Log.info(
                    "BlackFlow combat preflight changed routing state; replanning",
                    "before",
                    map_revision_before,
                    "after",
                    map_revision_after,
                    "current node before",
                    current_node_before,
                    "current node after",
                    current_node_after,
                    "resources revision before",
                    resources_revision_before,
                    "resources revision after",
                    resources_revision_after,
                    "cost revision before",
                    cost_revision_before,
                    "cost revision after",
                    cost_revision_after,
                    "target",
                    candidate.target);
                return { RoutingCycleStatus::ReplanRequired, "ghost_map_changed", {} };
            }
        }
    }
    if constexpr (requires {
                      session.run().active_movement;
                      session.save_pending_candidate(candidate, &error);
                  }) {
        if (!session.run().active_movement.has_value() || *session.run().active_movement != candidate.movement) {
            if (!session.save_pending_candidate(candidate, &error)) {
                return { RoutingCycleStatus::Failed, "movement_selection_proposal_failed", std::move(error) };
            }
            return { RoutingCycleStatus::MovementSelectionRequired, {}, {} };
        }
    }
    if (!session.begin_transaction(candidate, &error)) {
        return { RoutingCycleStatus::Failed, "transaction_proposal_failed", std::move(error) };
    }
    return execute_preview_cycle(session, port);
}

// This cheap UI gate permits only inventory observation, never planning on the stored map.
[[nodiscard]] bool movement_inventory_map_ready(const cv::Mat& image);

template <typename Session>
bool session_allows_inventory_before_refresh(Session& session)
{
    if constexpr (requires {
                      session.profile();
                      session.map().floor();
                      session.map().snapshot();
                      session.run();
                      session.pending_candidate();
                      session.page_context();
                      session.transaction();
                      session.completed_page_changes_floor();
                      session.ghost_preview_stop_pending();
                  }) {
        const auto floor = session.current_floor();
        if (!session_requires_movement_inventory_observation(session) || session.profile().empty() || !floor ||
            session.run().floor != *floor || session.map().floor() != *floor ||
            session.map().snapshot().revision == 0 ||
            session.map().snapshot().find_node(session.run().current_node) == nullptr || session.pending_candidate() ||
            session.completed_page_changes_floor() || session.ghost_preview_stop_pending()) {
            return false;
        }
        const auto* transaction = session.transaction();
        // A processing move is charged only when the fresh map reconciles it.
        // Inventory must not clear or restore that pending charge first.
        if (transaction && (transaction->stage() != MoveTransactionStage::PageResolved ||
                            transaction->proposal().movement != MovementKind::Walk)) {
            return false;
        }
        const auto& page = session.page_context();
        return !page || (transaction && page->resolution_reported && page->floor == *floor &&
                         page->node == transaction->proposal().target);
    }
    return false;
}

template <typename Session>
RoutingCycleOutcome execute_routing_cycle(
    Session& session,
    IBlackFlowTaskPort& port,
    bool inventory_entry_verified = false,
    bool* inventory_preflight_allowed = nullptr)
{
    std::string error;
    // 终止判定先于地图重建：楼层识别就可能已经把这一局判完，那就不该再为一张用不上的地图折腾。
    if (session.terminated()) {
        return { RoutingCycleStatus::SessionTerminated, {}, {} };
    }
    if (inventory_entry_verified && inventory_preflight_allowed && *inventory_preflight_allowed &&
        session_allows_inventory_before_refresh(session)) {
        *inventory_preflight_allowed = false;
        return { RoutingCycleStatus::MovementInventoryObservationRequired, {}, {} };
    }
    if (inventory_preflight_allowed) {
        *inventory_preflight_allowed = false;
    }
    if (!refresh_with_retries(session, port, &error)) {
        return { RoutingCycleStatus::NeedsPageRecovery, "map_rebuild_failed", std::move(error) };
    }
    if (inventory_preflight_allowed) {
        *inventory_preflight_allowed = true;
    }
    if (session_requires_movement_inventory_observation(session)) {
        if (inventory_preflight_allowed) {
            *inventory_preflight_allowed = false;
        }
        return { RoutingCycleStatus::MovementInventoryObservationRequired, {}, {} };
    }
    if (session_requires_map_settle(session)) {
        return { RoutingCycleStatus::ReplanRequired, {}, {} };
    }

    if constexpr (requires {
                      session.pending_candidate();
                      session.refresh_pending_candidate(&error);
                  }) {
        if (session.pending_candidate().has_value()) {
            if (!session.refresh_pending_candidate(&error)) {
                session.clear_pending_candidate();
                Log.info("BlackFlow selected movement intention invalidated", error);
                return { RoutingCycleStatus::ReplanRequired, "pending_movement_invalidated", std::move(error) };
            }
            return execute_pending_routing_cycle(session, port);
        }
    }

    return execute_replanning_cycle(session, port, true);
}
} // namespace asst::blackflow

