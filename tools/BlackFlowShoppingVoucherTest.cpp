#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include "AsstCaller.h"
#include "Config/Roguelike/RoguelikeShoppingConfig.h"
#include "MaaUtils/Encoding.h"
#include "Utils/Platform.hpp"

int wmain(int argc, wchar_t** argv)
{
#ifdef _MSC_VER
    _CrtSetReportHook([](int type, char* message, int*) {
        if (type == _CRT_ASSERT) {
            std::fputs(message, stderr);
            std::fflush(stderr);
            std::_Exit(86);
        }
        return 0;
    });
#endif
    if (argc != 3 && argc != 4) {
        return 2;
    }
    const std::filesystem::path repo(argv[1]), out(argv[2]);
    std::filesystem::create_directories(out);
    if (!AsstSetUserDir(MAA_NS::from_u16(out.native()).c_str())) {
        return 3;
    }
    using namespace asst;
    using Role = battle::Role;
    using Counts = std::unordered_map<Role, std::size_t>;
    using Promotions = std::unordered_map<Role, std::array<std::size_t, 6>>;
    const auto catalog = json::open(repo / utils::path("resource/roguelike/BlackFlow/valuation_catalog.json"));
    if (!catalog) {
        return 4;
    }
    const auto& actual = catalog->at("items").at("rogue_6_recruit_ticket_double_3_vip");
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    expect(actual.at("name").as_string() == "精英远程招募券", "fixture uses the actual mixed voucher SKU");
    expect(
        actual.at("raw_ticket").at("professionList").as<std::vector<std::string>>() ==
            std::vector<std::string> { "SNIPER", "MEDIC" },
        "raw SKU confirms the exact two professions");
    auto voucher = json::value(
        json::object { { "name", actual.at("name") },
                       { "is_recruit_ticket", true },
                       { "min_hope", 1 },
                       { "price", 8 },
                       { "roles", json::array { "CASTER" } },
                       { "recruit_role", "ALL" },
                       { "recruit_roles", actual.at("raw_ticket").at("professionList") },
                       { "recruit_role_target", 2 } });
    const auto make_config = [](const json::value& entry) {
        return json::value(
            json::object { { "theme", "BlackFlow" },
                           { "priority", json::array { json::object { { "name", "baseline_marker" } } } },
                           { "buy_tables", json::object { { "mixed", json::array { entry } } } },
                           { "ordinary_shop_extensions", json::object { { "default", json::array { "mixed" } } } } });
    };
    const auto fixture = out / utils::path("voucher_config_fixture.json");
    const auto load_fixture = [&](const json::value& value) {
        std::ofstream file(fixture);
        file << value.to_string();
        file.close();
        return RoguelikeShopping.load(fixture);
    };
    const auto valid = make_config(voucher);
    const bool loaded = load_fixture(valid);
    expect(loaded, "precise array is accepted despite the obsolete ALL field");
    if (!loaded || RoguelikeShopping.get_goods("BlackFlow", "mixed").empty()) {
        return 5;
    }
    const auto goods = RoguelikeShopping.get_goods("BlackFlow", "mixed").front();
    expect(goods.recruit_roles == std::vector<Role> { Role::Sniper, Role::Medic }, "both exact roles are retained");
    expect(!goods.recruit_role, "precise array does not retain a conflicting legacy role");
    expect(goods.roles == std::vector<Role> { Role::Caster }, "owned-team roles retain their separate meaning");
    expect(goods.min_hope == 1 && goods.price == 8, "the interface does not change hope or cash policy");
    const auto merged = RoguelikeShopping.get_shop_goods("BlackFlow", "default");
    expect(
        merged.size() == 2 && merged.back().recruit_roles == goods.recruit_roles,
        "extension preserves mixed semantics");
    const Counts covered { { Role::Sniper, 2 }, { Role::Medic, 2 } };
    expect(!goods.has_recruitment_need(covered, {}), "both professions covered without promotion means skip");
    expect(
        goods.has_recruitment_need(Counts { { Role::Sniper, 2 }, { Role::Medic, 0 } }, {}),
        "covered sniper does not hide a missing medic");
    expect(
        goods.has_recruitment_need(Counts { { Role::Sniper, 0 }, { Role::Medic, 2 } }, {}),
        "covered medic does not hide a missing sniper");
    for (const auto role : { Role::Sniper, Role::Medic }) {
        Promotions pending;
        pending[role][5] = 1;
        expect(goods.has_recruitment_need(covered, pending), "an existing allowed profession may still need promotion");
    }
    Promotions unrelated;
    unrelated[Role::Tank][5] = 1;
    expect(!goods.has_recruitment_need(covered, unrelated), "a defender promotion cannot justify a ranged voucher");
    Counts empty_counts;
    Promotions empty_promotions;
    expect(goods.has_recruitment_need(empty_counts, empty_promotions), "uncovered allowed professions remain useful");
    expect(
        empty_counts.empty() && empty_promotions.empty(),
        "demand observation does not insert synthetic roster entries");

    for (const json::value invalid : { json::value(json::array {}),
                                       json::value("SNIPER"),
                                       json::value(nullptr),
                                       json::value(json::array { "SNIPER", "UNKNOWN" }),
                                       json::value(json::array { "SNIPER", "SNIPER" }),
                                       json::value(json::array { "SNIPER", 7 }),
                                       json::value(json::array { "ALL" }) }) {
        auto entry = voucher;
        entry["recruit_roles"] = invalid;
        entry["recruit_role"] = "SNIPER";
        auto bad = make_config(entry);
        bad["priority"] = json::array { json::object { { "name", "must_not_replace_valid_state" } } };
        expect(!load_fixture(bad), "invalid explicit array rejects rather than falling back to the legacy role");
        const auto current = RoguelikeShopping.get_shop_goods("BlackFlow", "default");
        expect(
            current.size() == 2 && current.front().name == "baseline_marker" &&
                current.back().recruit_roles == goods.recruit_roles &&
                !current.back().has_recruitment_need(covered, {}),
            "failed parse preserves the previously committed tables and demand semantics");
    }
    for (const json::value target : { json::value(0),
                                      json::value(-1),
                                      json::value("2"),
                                      json::value(2.5),
                                      json::value(true),
                                      json::value(2'147'483'648LL) }) {
        auto entry = voucher;
        entry["recruit_role_target"] = target;
        expect(!load_fixture(make_config(entry)), "new array target must be a positive representable integer");
        expect(
            RoguelikeShopping.get_goods("BlackFlow", "mixed").front().recruit_role_target == 2,
            "invalid new target preserves the last valid threshold");
    }
    auto entry = voucher;
    entry["recruit_role_target"] = 3;
    expect(load_fixture(make_config(entry)), "mixed voucher target is configurable");
    expect(
        RoguelikeShopping.get_goods("BlackFlow", "mixed").front().has_recruitment_need(covered, {}),
        "a higher configured target leaves room for either allowed profession");
    entry.as_object().erase("recruit_roles");
    entry["recruit_role"] = "SNIPER";
    entry["recruit_role_target"] = 2;
    expect(load_fixture(make_config(entry)), "missing new field preserves the legacy single-role schema");
    const auto legacy = RoguelikeShopping.get_goods("BlackFlow", "mixed").front();
    expect(
        legacy.recruit_roles.empty() && legacy.recruit_role == Role::Sniper,
        "legacy role remains available to old consumers");
    expect(
        !legacy.has_recruitment_need(Counts { { Role::Sniper, 2 } }, {}),
        "a missing medic cannot change a single sniper voucher's old demand rule");
    Promotions sniper_promotion;
    sniper_promotion[Role::Sniper][5] = 1;
    expect(
        legacy.has_recruitment_need(covered, sniper_promotion),
        "single-role existing-member promotion remains useful");
    entry.as_object().erase("recruit_role");
    expect(load_fixture(make_config(entry)), "old unconstrained voucher remains accepted");
    expect(
        RoguelikeShopping.get_goods("BlackFlow", "mixed").front().has_recruitment_need(covered, {}),
        "no new array or legacy role retains the old unconstrained behavior");
    expect(
        RoguelikeShopping.load(repo / utils::path("resource/roguelike/BlackFlow/shopping.json")),
        "actual legacy shopping resource still parses after the new interface");
    if (argc == 4) {
        expect(RoguelikeShopping.load(std::filesystem::path(argv[3])), "candidate complete resource parses");
        const auto candidate_goods = RoguelikeShopping.get_shop_goods("BlackFlow", "default", "ending1");
        const auto mixed = std::ranges::find(candidate_goods, "精英远程招募券", &RoguelikeGoods::name);
        expect(mixed != candidate_goods.end(), "candidate voucher is reachable through the actual profile extensions");
        if (mixed != candidate_goods.end()) {
            expect(
                mixed->recruit_roles == std::vector<Role> { Role::Sniper, Role::Medic } && mixed->min_hope == 4 &&
                    mixed->is_recruit_ticket,
                "candidate retains exact professions and configured hope budget");
            expect(
                mixed->has_recruitment_need(
                    Counts { { Role::Sniper, 1 }, { Role::Caster, 1 }, { Role::Tank, 1 }, { Role::Special, 1 } },
                    {}),
                "actual run28 role context lacks a medic and admits the profession-need check only");
            expect(
                !mixed->has_recruitment_need(Counts { { Role::Sniper, 2 }, { Role::Medic, 2 } }, {}),
                "candidate rejects a covered pair without promotion needs");
        }
    }
    std::cout << "SHOP_VOUCHER checks=" << checks << " failures=" << failures << " connections=0 inputs=0\n";
    return failures == 0 ? 0 : 1;
}
