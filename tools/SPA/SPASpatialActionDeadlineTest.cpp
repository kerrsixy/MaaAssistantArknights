#include "Task/MiniGame/SPA/SPASpatialActionDeadline.h"
#include "Task/MiniGame/SPA/SPAPreparationDeadline.h"
#include <cassert>
#include <chrono>
#include <iostream>

int main()
{
    using namespace asst::spa;
    using Action = SpatialPreparationAction;
    using Phase = SpatialPreparationPhase;
    for (const auto action : { Action::Move, Action::Face, Action::Swap }) {
        const int admission = action == Action::Swap ? 45 : 35;
        const int before_hold = action == Action::Swap ? 35 : 30;
        assert(spatial_action_minimum_seconds(action, Phase::Admission) == admission);
        assert(spatial_action_minimum_seconds(action, Phase::BeforeHold) == before_hold);
        assert(spatial_action_fits_preparation(admission, action, Phase::Admission));
        assert(!spatial_action_fits_preparation(admission - 1, action, Phase::Admission));
        assert(spatial_action_fits_preparation(before_hold, action, Phase::BeforeHold));
        assert(!spatial_action_fits_preparation(before_hold - 1, action, Phase::BeforeHold));
        assert(!spatial_action_fits_preparation(0, action, Phase::Admission));
        assert(!spatial_action_fits_preparation(-1, action, Phase::BeforeHold));
        assert(spatial_action_fits_preparation(std::nullopt, action, Phase::Admission));
        assert(spatial_action_fits_preparation(std::nullopt, action, Phase::BeforeHold));
        for (const auto remaining : { -1, 0, 1, 180 }) {
            assert(spatial_action_fits_preparation(remaining, action, Phase::Cleanup));
        }
        assert(spatial_action_fits_preparation(std::nullopt, action, Phase::Cleanup));
    }

    assert(joint_ready_reserves_remaining_time(25, 7));
    assert(joint_ready_reserves_remaining_time(8, 0));
    assert(!joint_ready_reserves_remaining_time(26, 7));
    assert(!joint_ready_reserves_remaining_time(0, 7));
    assert(!joint_ready_reserves_remaining_time(15, 8));
    assert(!joint_ready_reserves_remaining_time(15, -1));
    assert(!joint_ready_reserves_remaining_time(std::nullopt, 7));

    const auto now = PreparationDeadline::Clock::time_point {} + std::chrono::hours(24);
    PreparationDeadline clock;
    const auto admission_snapshot = clock.observe(4, 45, now);
    assert(spatial_action_fits_preparation(admission_snapshot, Action::Swap, Phase::Admission));
    // OCR/identity work consumed time. A missing fresh digit preserves the
    // verified clock, not the old number and not an unlimited unknown budget.
    auto remaining = clock.observe(4, std::nullopt, now + std::chrono::seconds(10));
    assert(remaining == 35);
    assert(spatial_action_fits_preparation(remaining, Action::Swap, Phase::BeforeHold));
    remaining = clock.observe(4, std::nullopt, now + std::chrono::seconds(11));
    assert(remaining == 34);
    assert(!spatial_action_fits_preparation(remaining, Action::Swap, Phase::BeforeHold));
    assert(spatial_action_fits_preparation(admission_snapshot, Action::Swap, Phase::BeforeHold));
    // This contrast catches a caller incorrectly reusing its initial snapshot.
    remaining = clock.observe(4, 45, now + std::chrono::seconds(12));
    assert(remaining == 33); // Repeated stale OCR cannot extend the deadline.
    assert(!spatial_action_fits_preparation(remaining, Action::Swap, Phase::BeforeHold));
    remaining = clock.observe(4, std::nullopt, now + std::chrono::seconds(46));
    assert(remaining == 0);
    assert(!spatial_action_fits_preparation(remaining, Action::Face, Phase::BeforeHold));
    assert(spatial_action_fits_preparation(remaining, Action::Swap, Phase::Cleanup));

    // A round with no verified timer is genuinely unknown. It must neither
    // inherit the expired prior round nor block ordinary solo/joint planning.
    remaining = clock.observe(5, std::nullopt, now + std::chrono::seconds(47));
    assert(!remaining);
    assert(spatial_action_fits_preparation(remaining, Action::Move, Phase::Admission));
    assert(spatial_action_fits_preparation(remaining, Action::Swap, Phase::BeforeHold));

    PreparationDeadline move_clock;
    assert(spatial_action_fits_preparation(move_clock.observe(1, 35, now), Action::Move, Phase::Admission));
    remaining = move_clock.observe(1, std::nullopt, now + std::chrono::seconds(5));
    assert(remaining == 30 && spatial_action_fits_preparation(remaining, Action::Move, Phase::BeforeHold));
    remaining = move_clock.observe(1, std::nullopt, now + std::chrono::seconds(6));
    assert(remaining == 29 && !spatial_action_fits_preparation(remaining, Action::Move, Phase::BeforeHold));
    assert(spatial_action_fits_preparation(remaining, Action::Move, Phase::Cleanup));

    // A 45-second frame taking six seconds to capture/recognize leaves 39,
    // not a new 45-second window at the observer's return boundary.
    PreparationDeadline frame_clock;
    remaining = frame_clock.observe_frame(4, 45, now, now + std::chrono::seconds(6));
    assert(remaining == 39);
    assert(!spatial_action_fits_preparation(remaining, Action::Swap, Phase::Admission));
    assert(spatial_action_fits_preparation(remaining, Action::Move, Phase::Admission));
    // Cached digits are not a fresh clock source, including when they claim
    // much more time than the live deadline or arrive after its expiry.
    remaining = frame_clock.observe_frame(4, 180, std::nullopt, now + std::chrono::seconds(30));
    assert(remaining == 15);
    remaining = frame_clock.observe_frame(4, 180, std::nullopt, now + std::chrono::seconds(46));
    assert(remaining == 0);
    assert(!spatial_action_fits_preparation(remaining, Action::Face, Phase::BeforeHold));
    assert(spatial_action_fits_preparation(remaining, Action::Face, Phase::Cleanup));
    // A cached frame in a new round must clear stale digits without turning
    // an expired same-round clock into an unlimited unknown clock above.
    assert(!frame_clock.observe_frame(5, 180, std::nullopt, now + std::chrono::seconds(47)));
    using namespace std::chrono_literals;
    PreparationDeadline joint_round_twelve;
    assert(joint_round_twelve.observe(12, 114, now) == 114);
    assert(joint_round_twelve.observe(12, 110, now + 4s) == 110);
    assert(joint_round_twelve.observe(12, 6, now + 8s) == 6);
    assert(joint_round_twelve.shortening_requires_confirmation());
    assert(joint_round_twelve.observe(12, 104, now + 10s) == 104);
    assert(!joint_round_twelve.shortening_requires_confirmation());
    assert(joint_round_twelve.observe(12, 5, now + 11s) == 5);
    assert(joint_round_twelve.observe(12, 5, now + 11s) == 5);
    assert(joint_round_twelve.shortening_requires_confirmation());
    assert(joint_round_twelve.observe(12, 4, now + 12s) == 4);
    assert(!joint_round_twelve.shortening_requires_confirmation());
    PreparationDeadline missing_after_shortening;
    assert(missing_after_shortening.observe(12, 110, now) == 110);
    assert(missing_after_shortening.observe(12, 6, now + 4s) == 6);
    assert(missing_after_shortening.shortening_requires_confirmation());
    assert(missing_after_shortening.observe(12, std::nullopt, now + 11s) == 99);
    assert(!missing_after_shortening.shortening_requires_confirmation());
    remaining = frame_clock.observe_frame(5, 40, now + std::chrono::seconds(48), now + std::chrono::seconds(55));
    assert(remaining == 33);
    // Floor fractional elapsed time conservatively, and reject a future
    // timestamp rather than creating a deadline beyond the current frame.
    PreparationDeadline subsecond_clock;
    assert(subsecond_clock.observe_frame(1, 35, now, now + std::chrono::milliseconds(1)) == 34);
    assert(subsecond_clock.observe_frame(1, 180, now + std::chrono::hours(1), now + std::chrono::seconds(36)) == 0);
    PreparationDeadline cached_only_clock;
    assert(!cached_only_clock.observe_frame(1, 90, std::nullopt, now));

    assert(!spatial_action_fits_preparation(std::nullopt, static_cast<Action>(99), Phase::Admission));
    assert(!spatial_action_fits_preparation(std::nullopt, Action::Move, static_cast<Phase>(99)));
    std::cout
        << "Spatial action deadline boundaries passed; thresholds are sample margins and joint use is unverified\n";
}
