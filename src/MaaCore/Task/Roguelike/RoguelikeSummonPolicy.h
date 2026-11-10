#pragma once

#include <string_view>

namespace asst::battle::roguelike
{
constexpr bool quarantine_blackflow_summon(std::string_view stage, std::string_view operator_name) noexcept
{
    if (stage != "无主地") {
        return false;
    }
    return operator_name == "深海色" || operator_name == "凛御银灰" || operator_name == "令" ||
           operator_name == "梅尔" || operator_name == "稀音";
}
}
