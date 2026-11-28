#pragma once

#include <optional>

#include "Vision/VisionHelper.h"

namespace asst::blackflow
{
enum class AccountListOrder
{
    Ascending,
    Descending,
};

class BlackFlowAccountListOrderAnalyzer final : public VisionHelper
{
public:
    using VisionHelper::VisionHelper;
    std::optional<AccountListOrder> analyze() const;
};
}
