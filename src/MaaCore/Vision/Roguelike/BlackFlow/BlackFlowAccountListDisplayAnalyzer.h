#pragma once

#include <optional>

#include "Vision/VisionHelper.h"

namespace asst::blackflow
{
enum class AccountListBatchEdit
{
    Disabled,
    Enabled,
};

struct AccountListDisplayObservation
{
    AccountListBatchEdit batch_edit = AccountListBatchEdit::Disabled;
    Rect toggle_rect;
};

// White rook without the batch label is disabled; yellow rook plus the exact
// batch label is enabled. The card/module layout alone does not prove either.
class BlackFlowAccountListDisplayAnalyzer final : public VisionHelper
{
public:
    using VisionHelper::VisionHelper;
    [[nodiscard]] std::optional<AccountListDisplayObservation> analyze() const;
};
} // namespace asst::blackflow
