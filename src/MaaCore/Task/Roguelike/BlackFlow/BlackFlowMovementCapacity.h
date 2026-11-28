#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace asst::blackflow
{
// Only a complete used/capacity expression is evidence. No OCR substitutions
// or missing slash recovery; preserve the native capacity bound of 64 slots.
inline std::optional<std::pair<int, int>> recognize_movement_capacity_from_text(std::string_view text)
{
    int values[2] {};
    bool digits[2] {};
    int side = 0;
    for (const unsigned char ch : text) {
        if (std::isspace(ch) != 0) {
            continue;
        }
        if (ch == '/' && side == 0 && digits[0]) {
            side = 1;
            continue;
        }
        if (ch < '0' || ch > '9') {
            return std::nullopt;
        }
        values[side] = values[side] * 10 + (ch - '0');
        if (values[side] > 64) {
            return std::nullopt;
        }
        digits[side] = true;
    }
    if (side != 1 || !digits[1] || values[1] <= 0 || values[0] > values[1]) {
        return std::nullopt;
    }
    return std::pair { values[0], values[1] };
}

struct MovementCapacityTextFragment
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    std::string text;
};

// A detector may split the narrow footer into digits and slash. Merge only
// nonoverlapping, nearby fragments on the same line, then parse the whole row.
inline std::optional<std::pair<int, int>>
    movement_capacity_from_fragments(std::vector<MovementCapacityTextFragment> fragments)
{
    if (fragments.empty() || fragments.size() > 5) {
        return std::nullopt;
    }
    std::ranges::sort(fragments, {}, &MovementCapacityTextFragment::x);
    std::string text;
    int previous_right = -1;
    const auto& first = fragments.front();
    for (const auto& fragment : fragments) {
        if (fragment.x < 0 || fragment.x > 1280 || fragment.width <= 0 || fragment.width > 1280 - fragment.x ||
            fragment.y < 0 || fragment.y > 720 || fragment.height <= 0 || fragment.height > 720 - fragment.y ||
            fragment.text.empty() ||
            (previous_right >= 0 && (fragment.x < previous_right || fragment.x - previous_right > 16)) ||
            std::abs((fragment.y * 2 + fragment.height) - (first.y * 2 + first.height)) > 12) {
            return std::nullopt;
        }
        previous_right = fragment.x + fragment.width;
        text += fragment.text;
    }
    return recognize_movement_capacity_from_text(text);
}
} // namespace asst::blackflow
