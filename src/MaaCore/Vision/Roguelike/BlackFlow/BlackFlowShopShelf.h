#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Common/AsstTypes.h"
#include "MaaUtils/NoWarningCVMat.hpp"

namespace asst::blackflow
{
struct ShopShelfOffer
{
    std::string name;
    std::optional<int> price;
    Rect rect;
};

struct ShopShelfPage
{
    std::vector<ShopShelfOffer> offers;
};

struct ShopShelfInventory
{
    std::vector<ShopShelfOffer> offers;
    int observations = 0;
};

// Navigation supplies fresh observations. Coordinates from earlier pages are never clickable evidence.
class BlackFlowShopShelf
{
public:
    using Observe = std::function<std::optional<ShopShelfPage>()>;
    using Scroll = std::function<bool(bool down)>;

    static std::optional<ShopShelfPage> recognize(const cv::Mat& image);
    static bool same_view(const ShopShelfPage& lhs, const ShopShelfPage& rhs);

    static std::optional<ShopShelfInventory> scan(const Observe& observe, const Scroll& scroll, int limit = 8);
    static std::optional<ShopShelfOffer>
        seek(const ShopShelfOffer& target, const Observe& observe, const Scroll& scroll, int limit = 8);

private:
    static std::optional<ShopShelfPage> rewind(const Observe& observe, const Scroll& scroll, int limit);
};
}
