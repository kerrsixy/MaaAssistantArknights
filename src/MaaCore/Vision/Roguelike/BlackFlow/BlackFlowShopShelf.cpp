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
    // The fixed footer and leave button cover shelf pixels even though they are inside the screenshot.
    const int viewport_bottom = std::min(630, image.rows);
    const Rect leave = anchor.get_result().rect;
    for (const auto& name : *result) {
        // Product titles start at the left edge of one of the four cards. This excludes price labels,
        // investment balances, and the fixed shop controls from clipped-product evidence.
        const int column = (name.rect.x - 410 + 20) / 207;
        const int card_left = 410 + column * 207;
        const int card_right = std::min(card_left + 195, image.cols);
        if (column < 0 || column > 3 || std::abs(name.rect.x - card_left) > 32 || name.rect.y < 160 ||
            name.rect.height < 18 || name.rect.width < 40 || name.score < 0.85 || name.text == "前瞻性投资系统" ||
            std::ranges::none_of(name.text, [](unsigned char ch) { return ch >= 0x80; })) {
            continue;
        }
        ShopShelfOffer offer { .name = name.text, .rect = name.rect };
        // A thin leading glyph can be omitted by text detection while the full title
        // remains visible inside its card. Re-read that bounded line instead of
        // guessing the item from an approximate name or relaxing its price check.
        if (name.rect.x - card_left > 20) {
            RegionOCRer title(image);
            title.set_task_info("BlackFlow@RoguelikeTraderShoppingOcr");
            title.set_use_raw(true);
            title.set_bin_threshold(0, 255);
            title.set_roi({ card_left, name.rect.y - 3, card_right - card_left, name.rect.height + 6 });
            if (const auto full = title.analyze(); full && full->score >= 0.90 && full->text.ends_with(name.text)) {
                offer.name = full->text;
                LogDebug << __FUNCTION__ << "Complete shelf title observed" << name.text << offer.name;
            }
        }
        const Rect price_area { card_left, name.rect.y + 130, card_right - card_left, 80 };
        if (price_area.y + price_area.height > viewport_bottom) {
            page.clipped_content = true;
            page.offers.emplace_back(std::move(offer));
            continue;
        }
        Matcher coin(image);
        coin.set_task_info("RoguelikeTraderShopping");
        coin.set_roi(price_area);
        if (coin.analyze()) {
            const auto& icon = coin.get_result().rect;
            const Rect field { icon.x + icon.width, icon.y - 8, card_right - icon.x - icon.width, icon.height + 16 };
            const bool covered = field.x < leave.x + leave.width && field.x + field.width > leave.x &&
                                 field.y < leave.y + leave.height && field.y + field.height > leave.y;
            if (!covered && field.width >= 24 && field.y >= 0 && field.y + field.height <= viewport_bottom) {
                RegionOCRer price(image);
                price.set_task_info("NumberOcrReplace");
                price.set_replace({});
                // Thresholding unaffordable grey digits previously left a 6x5 border fragment;
                // the I->1 replacement then turned that fragment into a plausible low price.
                // Recognize the complete card-bounded field without threshold-based cropping.
                price.set_bin_threshold(0, 255);
                price.set_roi(field);
                int amount = 0;
                if (const auto value = price.analyze();
                    value && value->score >= 0.90 && value->rect.height >= 24 && !value->text.empty() &&
                    std::ranges::all_of(value->text, [](char ch) { return ch >= '0' && ch <= '9'; }) &&
                    utils::chars_to_number(value->text, amount) && amount >= 0 && amount < 1000) {
                    offer.price = amount;
                }
            }
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
    if (lhs.offers.size() != rhs.offers.size() || lhs.clipped_content != rhs.clipped_content) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.offers.size(); ++i) {
        const auto& a = lhs.offers[i];
        const auto& b = rhs.offers[i];
        const auto column = [](const Rect& rect) {
            return (rect.x - 410 + 20) / 207;
        };
        if (a.name != b.name || a.price != b.price || column(a.rect) != column(b.rect) ||
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
            if (previous->clipped_content) {
                LogWarn << __FUNCTION__ << "Shop shelf stopped with clipped products; tail remains unverified";
                return std::nullopt;
            }
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
