#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace asst
{
struct RoguelikeOperPreparation
{
    std::string operator_id;
    std::array<int, 3> displayed_skill_levels { -1, -1, -1 };
    int selected_skill = -1;
    std::uint64_t training_sequence = 0;
    std::uint64_t selection_sequence = 0;
};
}
