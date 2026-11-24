#pragma once

#include <cmath>

namespace asst
{
// Failed matching is unknown. A real E0 match remains a distinct observation.
// Exact ties between different phases are ambiguous, not hash iteration order.
class RecruitEliteEvidence
{
public:
    void consider(int elite, double score) noexcept
    {
        if (elite < 0 || elite > 2 || !std::isfinite(score) || score <= 0.0 || score > 1.0) {
            return;
        }
        if (score > m_score) {
            m_elite = elite;
            m_score = score;
            m_ambiguous = false;
        }
        else if (score == m_score && elite != m_elite) {
            m_ambiguous = true;
        }
    }

    [[nodiscard]] int elite() const noexcept { return m_ambiguous ? -1 : m_elite; }

    [[nodiscard]] double score() const noexcept { return m_score; }

private:
    int m_elite = -1;
    double m_score = 0.0;
    bool m_ambiguous = false;
};

// A temporary badge belongs only to the first card, never to the next detected name.
constexpr bool has_first_slot_temporary_badge(
    bool badge_detected,
    int x,
    int y,
    int list_x,
    int list_y,
    int list_width,
    int list_height)
{
    return badge_detected && list_width > 0 && list_height > 0 && x >= list_x && y >= list_y &&
           x < list_x + list_width / 2 && y < list_y + list_height / 4;
}
}
