#include <algorithm>
#include <iostream>

#include "Vision/Roguelike/BlackFlow/BlackFlowShopShelf.h"

int main()
{
    using namespace asst::blackflow;
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const ShopShelfOffer target { "target", 5, { 10, 100, 100, 20 } };
    for (int scenario = 0; scenario < 4; ++scenario) {
        int reads = 0, scrolls = 0;
        const auto observe = [&]() -> std::optional<ShopShelfPage> {
            ++reads;
            auto observed = target;
            observed.rect.x = reads == 1 ? 77 : 88;
            if (reads == 2) {
                if (scenario == 1) {
                    observed.price = 6;
                }
                else if (scenario == 2) {
                    return std::nullopt;
                }
                else if (scenario == 3) {
                    return ShopShelfPage {};
                }
            }
            return ShopShelfPage { { observed } };
        };
        const auto scroll = [&](bool) {
            ++scrolls;
            return true;
        };
        const auto found = BlackFlowShopShelf::seek(target, observe, scroll);
        expect(scrolls == 0, "visible target does not require navigation");
        expect(reads == 2, "visible target is reobserved before returning clickable evidence");
        if (scenario == 0) {
            expect(found && found->rect.x == 88, "latest rectangle replaces both cached and initial coordinates");
        }
        else {
            expect(!found, "changed price, failed observation and disappeared target are rejected");
        }
    }
    std::vector<ShopShelfPage> pages { { { { "target", 5, { 91, 100, 100, 20 } } } },
                                       { { { "other", 5, { 20, 100, 100, 20 } } } },
                                       { { { "tail", 8, { 30, 100, 100, 20 } } } } };
    int index = 2, reads = 0, ups = 0, downs = 0;
    const auto observe = [&]() -> std::optional<ShopShelfPage> {
        ++reads;
        return pages.at(index);
    };
    const auto scroll = [&](bool down) {
        down ? ++downs : ++ups;
        index = std::clamp(index + (down ? 1 : -1), 0, 2);
        return true;
    };
    const auto found = BlackFlowShopShelf::seek(target, observe, scroll);
    expect(
        found && found->rect.x == 91 && ups >= 2,
        "offscreen target still requires verified rewind and fresh coordinates");
    index = 2;
    pages[0].offers[0].price = 6;
    expect(
        !BlackFlowShopShelf::seek(target, observe, scroll),
        "old cached price never bypasses current shelf evidence");
    index = 2;
    const auto failed_scroll = [](bool) {
        return false;
    };
    expect(!BlackFlowShopShelf::seek(target, observe, failed_scroll), "navigation failure is not a clickable match");
    const auto failed_observe = []() -> std::optional<ShopShelfPage> {
        return std::nullopt;
    };
    expect(
        !BlackFlowShopShelf::seek(target, failed_observe, scroll),
        "unobservable current page cannot be searched blindly");
    for (const int limit : { 0, -1 }) {
        reads = ups = downs = 0;
        expect(!BlackFlowShopShelf::seek(target, observe, scroll, limit), "nonpositive navigation bound is rejected");
        expect(reads == 0 && ups == 0 && downs == 0, "disabled search performs no observation or navigation");
    }
    std::cout << "SHOP_SEEK checks=" << checks << " failures=" << failures << " connections=0 inputs=0\n";
    return failures == 0 ? 0 : 1;
}
