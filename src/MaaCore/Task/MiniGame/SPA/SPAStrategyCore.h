#pragma once

#include <string_view>

namespace asst::spa
{
// Reviewed strategy-specific foundation, independent of the supporting pact.
// This does not reserve ordinary transition recruits for other strategies.
inline bool is_strategy_core(std::string_view strategy, std::string_view name)
{
    return strategy == "band_mech" && name == "“暴躁铁皮”";
}
}
