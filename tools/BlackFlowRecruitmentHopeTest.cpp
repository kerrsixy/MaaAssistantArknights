#include <iostream>
#include <limits>

#include "Vision/Roguelike/BlackFlow/BlackFlowRecruitmentHope.h"

int main()
{
    using namespace asst::blackflow;
    int checks = 0, failures = 0;
    const auto expect = [&](RecruitmentHopeReading reading, std::optional<int> expected, const char* message) {
        ++checks;
        if (reading.balance() != expected) {
            ++failures;
            std::cerr << "FAIL " << message << '\n';
        }
    };
    expect({ RecruitmentHopeState::SelectedRemaining, 2, 4 }, 6, "selected price restores pre-payment balance");
    expect({ RecruitmentHopeState::UnselectedBalance, 2, {} }, 2, "real insufficient balance stays insufficient");
    expect({ RecruitmentHopeState::UnselectedBalance, 6, {} }, 6, "unselected balance needs no price");
    expect({ RecruitmentHopeState::SelectedRemaining, 6, 0 }, 6, "free selection preserves balance");
    expect({ RecruitmentHopeState::SelectedRemaining, 0, 0 }, 0, "free selection at zero stays zero");
    expect({ RecruitmentHopeState::SelectedRemaining, 0, 6 }, {}, "clamped preview cannot invent balance");
    expect({ RecruitmentHopeState::SelectedRemaining, 2, {} }, {}, "offscreen or ambiguous price stays unknown");
    expect({ RecruitmentHopeState::SelectedRemaining, {}, 4 }, {}, "missing digits stay unknown");
    expect({ RecruitmentHopeState::Unknown, 6, 4 }, {}, "missing selection identity stays unknown");
    expect({ RecruitmentHopeState::SelectedRemaining, -2, 4 }, {}, "negative preview cannot be reversed");
    expect({ RecruitmentHopeState::SelectedRemaining, 2, -1 }, {}, "invalid price stays unknown");
    expect({ RecruitmentHopeState::SelectedRemaining, std::numeric_limits<int>::max(), 1 }, {}, "overflow is rejected");
    expect({ RecruitmentHopeState::Unknown, {}, {} }, {}, "a new frame cannot inherit the previous balance");
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
