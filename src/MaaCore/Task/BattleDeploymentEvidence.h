#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "Common/AsstTypes.h"

namespace asst::battle
{
enum class TileAvailability
{
    Unknown,
    Deployable,
    Blocked,
};

constexpr bool tile_may_be_selected(TileAvailability state, bool observation_enabled)
{
    if (!observation_enabled) {
        return state != TileAvailability::Blocked;
    }
    return state == TileAvailability::Deployable;
}

constexpr bool preview_is_deployment_evidence(bool preview_has_device_data)
{
    return preview_has_device_data;
}

enum class DeploymentOutcome
{
    Unknown,
    Rejected,
    Confirmed,
    // Our exact occupant UI transaction owns recovery; this is not placement
    // failure or success and must not consume a placement retry.
    RecoveryPending
};
enum class DeploymentKind
{
    Operator,
    ReusableSummon,
    Consumable
};

// Identity is independent of the role requested for deployment. A unique
// device is not automatically the expected operator or a reclaimable Soul.
struct SelectedObjectIdentity
{
    std::string name;
    std::string id;
    bool unique = false;
    bool device = false;
};

struct SelectionCancellationReceipt
{
    bool schema_verified = false;
    bool input_attempted = false;
    bool input_accepted = false;
};

inline bool cancellation_input_accepted(const SelectionCancellationReceipt& receipt)
{
    return receipt.schema_verified && receipt.input_attempted && receipt.input_accepted;
}

constexpr bool selected_frame_is_fresh(std::uint64_t observed_ms, std::uint64_t now_ms)
{
    return observed_ms > 0 && now_ms >= observed_ms && now_ms - observed_ms <= 500;
}

struct SelectedObjectObservation
{
    SelectedObjectIdentity identity;
    Point requested_tile;
    // Association with our accepted click, not independent selected-cell CV.
    std::optional<Point> selection_request_tile;
    std::optional<bool> expected_identity_matches;
    std::optional<bool> card_present;
    std::uint64_t observed_ms = 0;
    std::uint64_t received_ms = 0;
    bool normal_before_selection = false;
    bool selected_view = false;
    bool camera_unchanged = false;
    bool selection_input_accepted = false;
    // Raw receipt of the existing Controller after the formal task's cancel
    // schema was validated. Normal views/bar still have to prove UI handoff.
    SelectionCancellationReceipt cancellation;
};

inline bool owns_selected_object_request(const SelectedObjectObservation& observation)
{
    return observation.normal_before_selection && observation.camera_unchanged &&
           observation.selection_input_accepted && observation.selection_request_tile == observation.requested_tile;
}

inline bool non_soul_occupancy_evidence(
    const SelectedObjectObservation& observation,
    const std::string& soul_id,
    std::uint64_t now_ms)
{
    return owns_selected_object_request(observation) && observation.selected_view &&
           cancellation_input_accepted(observation.cancellation) && observation.card_present == true &&
           observation.expected_identity_matches == false && observation.identity.unique &&
           observation.identity.device && !observation.identity.name.empty() && !observation.identity.id.empty() &&
           observation.identity.id != soul_id && observation.received_ms >= observation.observed_ms &&
           now_ms >= observation.received_ms && selected_frame_is_fresh(observation.observed_ms, now_ms);
}

constexpr bool deployment_retry_allowed(int unverified_attempts)
{
    return unverified_attempts >= 0 && unverified_attempts < 2;
}

constexpr bool
    shares_deployment_tile_retry_budget(DeploymentOutcome outcome, bool input_accepted, bool scoped_inspection)
{
    return outcome == DeploymentOutcome::Unknown ||
           (outcome == DeploymentOutcome::Rejected && input_accepted && scoped_inspection);
}

template <typename Commit>
constexpr bool commit_verified_deployment(DeploymentOutcome outcome, Commit commit)
{
    if (outcome != DeploymentOutcome::Confirmed) {
        return false;
    }
    commit();
    return true;
}

struct DeploymentEvidence
{
    bool input_accepted = false;
    std::optional<bool> card_present;
    std::optional<bool> target_identity_matches;
    int absent_observations = 0;
};

constexpr DeploymentOutcome evaluate_deployment(DeploymentKind kind, const DeploymentEvidence& evidence)
{
    if (!evidence.input_accepted) {
        return DeploymentOutcome::Rejected;
    }
    if (kind == DeploymentKind::Consumable) {
        return evidence.card_present == false && evidence.absent_observations >= 2 ? DeploymentOutcome::Confirmed
                                                                                   : DeploymentOutcome::Unknown;
    }
    // 待部署区无法可靠读取时，允许用目标格详情页的正向身份识别确认主干员。
    // 召唤物或其他单位的详情页不满足此条件；卡片仍在场的冲突证据也保持未知。
    if (evidence.target_identity_matches == true &&
        (kind == DeploymentKind::ReusableSummon || evidence.card_present == false ||
         !evidence.card_present.has_value())) {
        return DeploymentOutcome::Confirmed;
    }
    if (evidence.card_present == true && evidence.target_identity_matches == false) {
        return DeploymentOutcome::Rejected;
    }
    return DeploymentOutcome::Unknown;
}
}
