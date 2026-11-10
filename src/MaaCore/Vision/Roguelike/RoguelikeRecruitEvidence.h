#pragma once

namespace asst
{
// A temporary badge belongs only to the first card, never to the next detected name.
constexpr bool has_first_slot_temporary_badge(
    bool badge_detected, int x, int y, int list_x, int list_y, int list_width, int list_height)
{
    return badge_detected && list_width > 0 && list_height > 0 && x >= list_x && y >= list_y &&
           x < list_x + list_width / 2 && y < list_y + list_height / 4;
}
}
