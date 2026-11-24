#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Vision/Oper/OperBoxImageAnalyzer.h"

namespace asst::blackflow
{
struct VerifiedAccountListTarget
{
    OperBoxInfo oper;
    Rect name_rect;
    double name_score = 0.0;
    std::string recognized_name;
};

struct AccountListTargetLocation
{
    bool list_verified = false;
    bool page_anchor_verified = false;
    bool used_verified_page_geometry = false;
    bool full_page_scanned = false;
    std::string page_anchor;
    std::string reason;
    std::vector<OperBoxInfo> page;
    std::optional<VerifiedAccountListTarget> target;
};

// Cached page data provides coordinates only. Page and exact target text are
// verified on this reader's current frame before any coordinate can be reused.
class BlackFlowAccountTargetLocator final : public VisionHelper
{
public:
    using VisionHelper::VisionHelper;

    [[nodiscard]] AccountListTargetLocation locate(
        const std::string& operator_id,
        const std::string& operator_name,
        const std::vector<OperBoxInfo>& verified_page,
        const Rect& footer_offset) const;
};
}
