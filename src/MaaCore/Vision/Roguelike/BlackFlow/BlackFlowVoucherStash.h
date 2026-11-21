#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "Vision/VisionHelper.h"

namespace asst::blackflow
{
struct VoucherStashCount
{
    int used = 0;
    int capacity = 0;
};

struct VoucherStashObservation
{
    bool recruitment_page = false;
    bool enabled_button = false;
    Rect button;
    std::optional<VoucherStashCount> count;
    std::string successor_page;

    [[nodiscard]] bool can_store() const noexcept;
};

enum class VoucherStashProof
{
    Pending,
    CounterIncreased,
    SuccessorPage,
};

class BlackFlowVoucherStash final : public VisionHelper
{
public:
    using VisionHelper::VisionHelper;

    [[nodiscard]] VoucherStashObservation observe() const;

    [[nodiscard]] static std::optional<VoucherStashCount> parse_count(std::string_view text);
    [[nodiscard]] static VoucherStashProof
        verify(const VoucherStashObservation& before, const VoucherStashObservation& after) noexcept;
};

class VoucherStashAttempt
{
public:
    [[nodiscard]] bool begin(const VoucherStashObservation& observation);

    [[nodiscard]] bool pending() const noexcept { return m_before.has_value(); }

    [[nodiscard]] const std::optional<VoucherStashObservation>& before() const noexcept { return m_before; }

    void reset() noexcept { m_before.reset(); }

private:
    std::optional<VoucherStashObservation> m_before;
};
}
