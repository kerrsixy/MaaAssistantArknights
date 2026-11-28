#pragma once

#include <limits>
#include <optional>

namespace asst::blackflow
{
enum class RecruitmentHopeState
{
    Unknown,
    UnselectedBalance,
    SelectedRemaining,
};

// A single frame only: no balance survives payment, another voucher or account.
struct RecruitmentHopeReading
{
    RecruitmentHopeState state = RecruitmentHopeState::Unknown;
    std::optional<int> displayed;
    std::optional<int> selected_cost;

    [[nodiscard]] std::optional<int> balance() const noexcept
    {
        if (!displayed || *displayed < 0) {
            return std::nullopt;
        }
        if (state == RecruitmentHopeState::UnselectedBalance) {
            return displayed;
        }
        // Zero may be a clamped unaffordable preview. Only a positive unsigned
        // remainder and this frame's exact selected-card price can be reversed.
        if (state != RecruitmentHopeState::SelectedRemaining || !selected_cost || *selected_cost < 0 ||
            (*displayed == 0 && *selected_cost != 0) || *displayed > std::numeric_limits<int>::max() - *selected_cost) {
            return std::nullopt;
        }
        return *displayed + *selected_cost;
    }
};
}
