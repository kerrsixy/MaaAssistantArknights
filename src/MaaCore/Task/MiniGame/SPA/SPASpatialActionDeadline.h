#pragma once

#include <optional>

namespace asst::spa
{
enum class SpatialPreparationAction
{
    Move,
    Face,
    Swap
};

enum class SpatialPreparationPhase
{
    Admission,
    BeforeHold,
    Cleanup
};

// Candidate engineering margins from quick4-resume1 R4, not a statistical
// latency upper bound or a game rule. Whole Move/Face took at most 24.994s
// and occupied Swap 34.426s; first held input through reconciliation took
// 19.096s and 25.104s respectively. Add an eight-second cleanup margin and
// round upward. Joint execution with these thresholds is not yet verified.
inline std::optional<int> spatial_action_minimum_seconds(SpatialPreparationAction action, SpatialPreparationPhase phase)
{
    if (phase == SpatialPreparationPhase::Cleanup) {
        return 0;
    }
    if (phase != SpatialPreparationPhase::Admission && phase != SpatialPreparationPhase::BeforeHold) {
        return std::nullopt;
    }
    switch (action) {
    case SpatialPreparationAction::Move:
    case SpatialPreparationAction::Face:
        return phase == SpatialPreparationPhase::Admission ? 35 : 30;
    case SpatialPreparationAction::Swap:
        return phase == SpatialPreparationPhase::Admission ? 45 : 35;
    }
    return std::nullopt;
}

// Obtain remaining from PreparationDeadline.observe(round, nullopt, now) at
// EACH gate. Reusing the admission snapshot after OCR would defeat this gate.
// No known countdown leaves the existing flow available. Cleanup means only
// cancellation/release/reconciliation of an existing transaction; it grants
// no permission to start another movement or facing change.
inline bool spatial_action_fits_preparation(
    std::optional<int> remaining,
    SpatialPreparationAction action,
    SpatialPreparationPhase phase)
{
    if (phase == SpatialPreparationPhase::Cleanup) {
        return true;
    }
    const auto required = spatial_action_minimum_seconds(action, phase);
    return required && (!remaining || *remaining >= *required);
}

// Start the final reviewed exchange before another speculative shop scan can
// consume its admission margin. A full bench needs a verified sale first.
inline bool replacement_window_before_shop_refresh(std::optional<int> remaining, bool full_bench)
{
    const int swap_seconds =
        *spatial_action_minimum_seconds(SpatialPreparationAction::Swap, SpatialPreparationPhase::Admission);
    const int required = swap_seconds + (full_bench ? 10 : 0);
    return remaining && *remaining >= required && *remaining <= required + 15;
}
}
