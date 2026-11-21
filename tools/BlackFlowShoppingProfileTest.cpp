#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "AsstCaller.h"
#include "Config/Roguelike/BlackFlow/BlackFlowScrapMarketConfig.h"
#include "Config/Roguelike/RoguelikeShoppingConfig.h"
#include "MaaUtils/Encoding.h"
#include "Utils/Platform.hpp"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        return 2;
    }
    const std::filesystem::path repo(argv[1]), out(argv[2]);
    std::filesystem::create_directories(out);
    if (!AsstSetUserDir(MAA_NS::from_u16(out.native()).c_str())) {
        return 3;
    }
    using namespace asst;
    const auto path = repo / utils::path("resource/roguelike/BlackFlow/shopping.json");
    const auto source = json::open(path);
    if (!source || !BlackFlowScrapMarket.load(repo / utils::path("resource/roguelike/BlackFlow/scrap_market.json")) ||
        !RoguelikeShopping.load(path)) {
        return 4;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const std::vector<std::string> branch_items {
        "沙盘α", "沙盘β", "击坠“神明”", "湮没“光明”", "焚毁“文明”", "破碎噩兆"
    };
    const auto names = [](const auto& goods) {
        std::vector<std::string> result;
        for (const auto& item : goods) {
            result.push_back(item.name);
        }
        return result;
    };
    for (const auto& table : { "default", "final_relics" }) {
        const auto original = names(RoguelikeShopping.get_shop_goods("BlackFlow", table));
        const auto filtered = names(RoguelikeShopping.get_shop_goods("BlackFlow", table, "ending1"));
        auto expected = original;
        for (const auto& item : branch_items) {
            expect(
                std::ranges::find(original, item) != original.end(),
                "other profiles retain each original branch item");
            expect(std::ranges::find(filtered, item) == filtered.end(), "ending1 excludes each unrelated branch item");
            std::erase(expected, item);
        }
        expect(filtered == expected, "all other goods and priority order survive profile filtering");
        expect(
            names(RoguelikeShopping.get_shop_goods("BlackFlow", table, "ending2")) == original,
            "ending2 merged inventory is unchanged");
        expect(
            names(RoguelikeShopping.get_shop_goods("BlackFlow", table, "unconfigured_profile")) == original,
            "unconfigured strategy preserves legacy behavior");
    }
    expect(
        names(RoguelikeShopping.get_goods("BlackFlow", "ending2_sandtable")) ==
            std::vector<std::string> { "沙盘α", "沙盘β" },
        "explicit ending2 sandtable policy remains intact");
    expect(
        names(RoguelikeShopping.get_shop_goods("BlackFlow", "seed", "ending1")) == std::vector<std::string> { "种子" },
        "table without extensions remains usable");
    const auto fixture = out / utils::path("policy_fixture.json");
    const auto load_fixture = [&](const json::value& value) {
        std::ofstream file(fixture);
        file << value.to_string();
        file.close();
        return RoguelikeShopping.load(fixture);
    };
    for (const json::value invalid : { json::value(json::array {}),
                                       json::value(json::object { { "", json::array { "沙盘β" } } }),
                                       json::value(json::object { { "ending1", "沙盘β" } }),
                                       json::value(json::object { { "ending1", json::array { "" } } }),
                                       json::value(json::object { { "ending1", json::array { 5 } } }) }) {
        auto bad = *source;
        bad["ordinary_shop_profile_exclusions"] = invalid;
        expect(!load_fixture(bad), "malformed exclusions reject rather than silently widening purchases");
        const auto current = names(RoguelikeShopping.get_shop_goods("BlackFlow", "default", "ending1"));
        expect(
            std::ranges::find(current, "沙盘β") == current.end(),
            "failed parse preserves the previous valid strategy policy");
    }
    auto legacy = *source;
    legacy.as_object().erase("ordinary_shop_profile_exclusions");
    expect(load_fixture(legacy), "old schema without exclusions remains valid");
    expect(
        names(RoguelikeShopping.get_shop_goods("BlackFlow", "default", "ending1")) ==
            names(RoguelikeShopping.get_shop_goods("BlackFlow", "default")),
        "missing exclusions retain legacy contents");
    expect(RoguelikeShopping.load(path), "restore actual policy after isolated negative fixtures");
    std::cout << "SHOP_PROFILE checks=" << checks << " failures=" << failures << " connections=0 inputs=0\n";
    return failures == 0 ? 0 : 1;
}
