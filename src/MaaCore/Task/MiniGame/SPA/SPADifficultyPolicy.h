#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace asst::spa
{
inline constexpr int KeepDifficulty = -1;
inline constexpr int MaximumDifficulty = std::numeric_limits<int>::max();
inline constexpr int JointTrainingDifficulty = 0;
inline constexpr std::array<std::string_view, 5> JointDifficultyNames { "入门协议",
                                                                        "标准模拟",
                                                                        "险境模拟",
                                                                        "绝境模拟",
                                                                        "终极模拟" };

inline constexpr std::optional<int> joint_difficulty_from_label(std::string_view label)
{
    const auto it = std::find(JointDifficultyNames.begin(), JointDifficultyNames.end(), label);
    return it == JointDifficultyNames.end() ? std::nullopt
                                            : std::optional(static_cast<int>(it - JointDifficultyNames.begin()));
}

enum class DifficultyScroll
{
    Higher,
    Lower,
    Hold
};

inline constexpr DifficultyScroll joint_difficulty_scroll(
    int desired,
    int current,
    std::optional<int> lowest_visible,
    std::optional<int> highest_visible)
{
    if (lowest_visible && highest_visible && desired >= *lowest_visible && desired <= *highest_visible) {
        return DifficultyScroll::Hold;
    }
    return desired > highest_visible.value_or(current) ? DifficultyScroll::Higher : DifficultyScroll::Lower;
}

class DifficultyTargetStability
{
public:
    void clear() { m_previous.reset(); }

    bool observe(int level, const std::array<int, 4>& rect)
    {
        const std::array<int, 3> current { level, rect[0] * 2 + rect[2], rect[1] * 2 + rect[3] };
        const bool settled = m_previous && (*m_previous)[0] == current[0] &&
                             std::abs((*m_previous)[1] - current[1]) <= 24 &&
                             std::abs((*m_previous)[2] - current[2]) <= 16;
        m_previous = current;
        return settled;
    }

private:
    std::optional<std::array<int, 3>> m_previous;
};

inline bool valid_difficulty_request(int value, int cap = 20)
{
    return value == KeepDifficulty || value == MaximumDifficulty || (value >= 1 && value <= cap);
}

inline std::optional<int> resolve_difficulty(int requested, int current, const std::vector<int>& unlocked, int cap = 20)
{
    if (!valid_difficulty_request(requested, cap)) {
        return std::nullopt;
    }
    if (requested == KeepDifficulty) {
        return current >= 1 && current <= cap ? std::optional(current) : std::nullopt;
    }
    if (std::find(unlocked.begin(), unlocked.end(), requested) != unlocked.end()) {
        return requested;
    }
    int maximum = 0;
    for (const auto level : unlocked) {
        if (level >= 1 && level <= cap) {
            maximum = std::max(maximum, level);
        }
    }
    return maximum > 0 ? std::optional(maximum) : std::nullopt;
}
}
