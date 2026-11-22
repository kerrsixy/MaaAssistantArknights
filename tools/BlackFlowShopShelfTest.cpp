#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "AsstCaller.h"
#include "Config/TaskData.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Utils/Platform.hpp"
#include "Vision/Roguelike/BlackFlow/BlackFlowShopShelf.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4 && argc != 5) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path repo(argv[1]), fixtures(argv[2]), out(argv[3]);
    std::filesystem::create_directories(out);
    if (!AsstSetUserDir(MAA_NS::from_u16(out.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str()) ||
        (argc == 5 && !Task.load(std::filesystem::path(argv[4])))) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    std::vector<std::filesystem::path> files;
    for (const auto& file : std::filesystem::directory_iterator(fixtures)) {
        if (file.path().extension() == L".png") {
            files.push_back(file.path());
        }
    }
    std::ranges::sort(files);
    expect(files.size() >= 10, "continuous real shop observations are present");
    if (files.empty()) {
        return 4;
    }
    json::array observations;
    std::vector<ShopShelfPage> pages;
    const auto find_target = [](const ShopShelfPage& page) -> std::optional<ShopShelfOffer> {
        for (const auto& offer : page.offers) {
            if (offer.name == "重弹簧" && offer.price == 6) {
                return offer;
            }
        }
        return std::nullopt;
    };
    for (const auto& file : files) {
        const auto page = BlackFlowShopShelf::recognize(MAA_NS::imread(file));
        expect(page.has_value(), "original shop frame is recognized");
        if (!page) {
            continue;
        }
        const bool head_visible =
            std::ranges::any_of(page->offers, [](const auto& offer) { return offer.name == "支援防暴桩"; });
        if (head_visible) {
            if (file.filename() == L"2026.11.22-07.56.13.839_raw.png") {
                // The moving frame's actual OCR score is 0.843686, below the existing 0.85 guard.
                expect(!find_target(*page), "low-confidence moving-frame title remains unclickable");
            }
            else {
                expect(
                    find_target(*page).has_value(),
                    "stable visible spring retains exact canonical title and price6");
            }
        }
        json::array offers;
        for (const auto& offer : page->offers) {
            offers.emplace_back(
                json::object {
                    { "name", offer.name },
                    { "price", offer.price.value_or(-1) },
                    { "rect", json::array { offer.rect.x, offer.rect.y, offer.rect.width, offer.rect.height } } });
        }
        observations.emplace_back(
            json::object { { "file", MAA_NS::from_u16(file.filename().native()) }, { "offers", std::move(offers) } });
        pages.push_back(*page);
    }
    std::ofstream(out / "observations.json") << json::value(observations).to_string();
    if (pages.size() != files.size()) {
        return 5;
    }
    const auto target = find_target(pages.at(1));
    const auto fresh_target = find_target(pages.back());
    expect(target.has_value(), "stable original frame provides the selected offer");
    expect(fresh_target.has_value(), "last actual frame revalidates the selected offer");
    if (!target) {
        return 6;
    }
    int scrolls = 0;
    const BlackFlowShopShelf::Scroll no_input = [&](bool) {
        ++scrolls;
        return false;
    };
    const auto replay = [&](const std::optional<ShopShelfPage>& final_page) {
        int reads = 0;
        return BlackFlowShopShelf::seek(
            *target,
            [&]() -> std::optional<ShopShelfPage> { return ++reads == 1 ? pages.at(1) : final_page; },
            no_input);
    };
    const auto actual = replay(pages.back());
    expect(actual.has_value(), "live final-frame OCR confusion no longer aborts seek");
    expect(
        actual && fresh_target && actual->rect == fresh_target->rect,
        "rectangle comes from fresh final observation");
    auto changed = pages.at(1);
    for (auto& offer : changed.offers) {
        if (offer.name == target->name) {
            offer.price = 7;
        }
    }
    expect(!replay(changed), "changed fresh price is rejected");
    for (auto& offer : changed.offers) {
        if (offer.name == target->name) {
            offer.price.reset();
        }
    }
    expect(!replay(changed), "unknown fresh price is rejected");
    changed = pages.at(1);
    std::erase_if(changed.offers, [&](const auto& offer) { return offer.name == target->name; });
    expect(!replay(changed), "missing fresh product is rejected");
    expect(!replay(std::nullopt), "missing fresh observation is rejected");
    expect(scrolls == 0, "replay has no navigation or click side effects");
    expect(!BlackFlowShopShelf::recognize(cv::Mat::zeros(720, 1280, CV_8UC3)), "blank image is not a shop");
    std::cout << "SHOP_SHELF checks=" << checks << " failures=" << failures << " original_frames=" << files.size()
              << " connections=0 input=0 live_purchase_proven=false\n";
    return failures ? 1 : 0;
}
