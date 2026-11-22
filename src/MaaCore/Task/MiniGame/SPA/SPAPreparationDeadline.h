#pragma once

#include <algorithm>
#include <chrono>
#include <optional>

namespace asst::spa
{
// Source search, held-map recognition and facing reconciliation outlast a
// simple shop click. Unknown clocks do not stop an otherwise usable round.
inline bool deployment_fits_preparation(std::optional<int> remaining, bool unidentified_source)
{
    return !remaining || *remaining >= (unidentified_source ? 25 : 18);
}

inline bool joint_ready_reserves_remaining_time(std::optional<int> remaining, int free_field_slots)
{
    return remaining && *remaining > 0 && *remaining <= 25 && free_field_slots >= 0 && free_field_slots < 8;
}

// Continue a verified countdown through short OCR gaps. Repeated stale
// digits cannot extend a round, and a new round never inherits its deadline.
class PreparationDeadline
{
public:
    using Clock = std::chrono::steady_clock;

    // The number belongs to the captured frame, not to the later OCR result.
    // A missing frame timestamp denotes a cached observation: its old digits
    // cannot seed or extend a deadline, even when they were once verified.
    std::optional<int> observe_frame(
        int round,
        std::optional<int> seconds,
        std::optional<Clock::time_point> frame_started,
        Clock::time_point now)
    {
        if (frame_started && *frame_started <= now) {
            observe(round, seconds, *frame_started);
        }
        return observe(round, std::nullopt, now);
    }

    std::optional<int> observe(int round, std::optional<int> seconds, Clock::time_point now)
    {
        if (round < 1) {
            return std::nullopt;
        }
        if (round != m_round) {
            m_round = round;
            m_deadline.reset();
            m_pending_shortening.reset();
        }
        if (seconds && *seconds >= 0 && *seconds <= 180) {
            const auto observed = now + std::chrono::seconds(*seconds);
            // A missing leading digit (86 -> 6) must not permanently consume
            // the rest of preparation. Pause new actions provisionally, then
            // require another consistent read before shortening substantially.
            if (m_deadline && observed + std::chrono::seconds(5) < *m_deadline) {
                if (m_pending_shortening && observed >= *m_pending_shortening - std::chrono::seconds(3) &&
                    observed <= *m_pending_shortening + std::chrono::seconds(3)) {
                    m_deadline = std::min(*m_pending_shortening, observed);
                    m_pending_shortening.reset();
                }
                else {
                    m_pending_shortening = observed;
                }
            }
            else {
                m_deadline = m_deadline ? std::min(*m_deadline, observed) : observed;
                m_pending_shortening.reset();
            }
        }
        if (!m_deadline) {
            return std::nullopt;
        }
        const auto effective = m_pending_shortening ? std::min(*m_deadline, *m_pending_shortening) : *m_deadline;
        return std::max(0, static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(effective - now).count()));
    }

private:
    int m_round = -1;
    std::optional<Clock::time_point> m_deadline;
    std::optional<Clock::time_point> m_pending_shortening;
};
}
