#pragma once

#include <optional>

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
    Confirmed
};
enum class DeploymentKind
{
    Operator,
    ReusableSummon,
    Consumable
};

constexpr bool deployment_retry_allowed(int unverified_attempts)
{
    return unverified_attempts >= 0 && unverified_attempts < 2;
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
