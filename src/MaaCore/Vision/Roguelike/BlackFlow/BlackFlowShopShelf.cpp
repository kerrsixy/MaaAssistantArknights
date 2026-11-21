#include "BlackFlowShopShelf.h"

#include <algorithm>
#include <cmath>
#include <tuple>

#include "Utils/DebugImageHelper.hpp"
#include "Utils/Logger.hpp"
#include "Utils/StringMisc.hpp"
#include "Vision/Matcher.h"
#include "Vision/OCRer.h"
#include "Vision/RegionOCRer.h"

std::optional<asst::blackflow::ShopShelfPage> asst::blackflow::BlackFlowShopShelf::recognize(const cv::Mat& image)
{
    if (image.empty()) {
        return std::nullopt;
    }
    Matcher anchor(image);
    anchor.set_task_info("BlackFlow@Roguelike@StageTraderLeave");
    if (!anchor.analyze()) {
        return std::nullopt;
    }
#ifdef ASST_DEBUG
    utils::save_debug_image(image, utils::path("debug/blackflow_shop"), true);
#endif
    OCRer names(image);
    names.set_task_info("BlackFlow@RoguelikeTraderShoppingOcr");
    const auto result = names.analyze();
    if (!result) {
        return std::nullopt;
    }
    ShopShelfPage page;
    for (const auto& name : *result) {
        Matcher coin(image);
        coin.set_task_info("RoguelikeTraderShopping");
        const Rect price_area = name.rect.move({ -20, 130, 200, 80 });
        // A clipped price row is not evidence that a product is absent or sold out.
        if (price_area.y < 0 || price_area.y + price_area.height > image.rows) {
            continue;
        }
        coin.set_roi(price_area);
        if (!coin.analyze()) {
            continue;
        }
        ShopShelfOffer offer { .name = name.text, .rect = name.rect };
        const auto& icon = coin.get_result().rect;
        RegionOCRer price(image);
        price.set_task_info("NumberOcrReplace");
        price.set_roi({ icon.x + icon.width, icon.y - 8, 90, icon.height + 16 });
        int amount = 0;
        if (const auto value = price.analyze();
            value && utils::chars_to_number(value->text, amount) && amount > 0 && amount < 1000) {
            offer.price = amount;
        }
        page.offers.emplace_back(std::move(offer));
    }
    std::ranges::sort(page.offers, [](const auto& lhs, const auto& rhs) {
        return std::tie(lhs.name, lhs.rect.y, lhs.rect.x) < std::tie(rhs.name, rhs.rect.y, rhs.rect.x);
    });
    return page;
}

bool asst::blackflow::BlackFlowShopShelf::same_view(const ShopShelfPage& lhs, const ShopShelfPage& rhs)
{
    if (lhs.offers.size() != rhs.offers.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.offers.size(); ++i) {
        const auto& a = lhs.offers[i];
        const auto& b = rhs.offers[i];
        if (a.name != b.name || a.price != b.price || std::abs(a.rect.x - b.rect.x) > 4 ||
            std::abs(a.rect.y - b.rect.y) > 4) {
            return false;
        }
    }
    return true;
}

std::optional<asst::blackflow::ShopShelfPage>
    asst::blackflow::BlackFlowShopShelf::rewind(const Observe& observe, const Scroll& scroll, int limit)
{
    auto previous = observe();
    if (!previous) {
        return std::nullopt;
    }
    int stable = 0;
    for (int i = 0; i < limit; ++i) {
        if (!scroll(false)) {
            return std::nullopt;
        }
        auto current = observe();
        if (!current) {
            return std::nullopt;
        }
        stable = same_view(*previous, *current) ? stable + 1 : 0;
        previous = std::move(current);
        if (stable >= 2) {
            return previous;
        }
    }
    LogWarn << __FUNCTION__ << "Shop shelf head was not established";
    return std::nullopt;
}

std::optional<asst::blackflow::ShopShelfInventory>
    asst::blackflow::BlackFlowShopShelf::scan(const Observe& observe, const Scroll& scroll, int limit)
{
    auto previous = rewind(observe, scroll, limit);
    if (!previous) {
        return std::nullopt;
    }
    ShopShelfInventory inventory;
    int stable = 0;
    for (int i = 0; i < limit; ++i) {
        ++inventory.observations;
        for (const auto& offer : previous->offers) {
            if (std::ranges::none_of(inventory.offers, [&](const auto& known) {
                    return known.name == offer.name && known.price == offer.price;
                })) {
                inventory.offers.emplace_back(offer);
            }
        }
        if (!scroll(true)) {
            return std::nullopt;
        }
        auto current = observe();
        if (!current) {
            return std::nullopt;
        }
        stable = same_view(*previous, *current) ? stable + 1 : 0;
        previous = std::move(current);
        if (stable >= 2) {
            return inventory;
        }
    }
    LogWarn << __FUNCTION__ << "Shop shelf tail was not established";
    return std::nullopt;
}

std::optional<asst::blackflow::ShopShelfOffer> asst::blackflow::BlackFlowShopShelf::seek(
    const ShopShelfOffer& target,
    const Observe& observe,
    const Scroll& scroll,
    int limit)
{
    auto page = rewind(observe, scroll, limit);
    int stable = 0;
    for (int i = 0; page && i < limit; ++i) {
        const auto found = std::ranges::find_if(page->offers, [&](const auto& offer) {
            return offer.name == target.name && offer.price == target.price;
        });
        if (found != page->offers.end()) {
            // Re-observe after returning to the product; the cached rectangle is intentionally discarded.
            const auto fresh = observe();
            if (!fresh) {
                return std::nullopt;
            }
            const auto verified = std::ranges::find_if(fresh->offers, [&](const auto& offer) {
                return offer.name == target.name && offer.price == target.price;
            });
            return verified == fresh->offers.end() ? std::nullopt : std::optional(*verified);
        }
        if (!scroll(true)) {
            return std::nullopt;
        }
        auto next = observe();
        if (!next) {
            return std::nullopt;
        }
        stable = same_view(*page, *next) ? stable + 1 : 0;
        if (stable >= 2) {
            return std::nullopt;
        }
        page = std::move(next);
    }
    return std::nullopt;
}
