#include "Task/MiniGame/SPA/SPABenchSalePolicy.h"
#include <cstdlib>
#include <iostream>

using namespace asst::spa;

static int checks = 0;

static void check(bool value, const char* explanation)
{
    ++checks;
    if (!value) {
        std::cerr << "FAIL: " << explanation << '\n';
        std::exit(1);
    }
}

static PreparationPolicy full_egir()
{
    PreparationPolicy p;
    p.primary_pact = "阿戈尔";
    p.primary_locked_after_activation = true;
    p.dynamic_decision_mode = true;
    p.selected_strategy_id = "band_amiya";
    p.one_coin_bank_allowed = true;
    p.economy_scenario_horizon_round = 14;
    p.round = 7;
    p.shop_level = 6;
    p.deployed_population = 8;
    p.bench_occupied = 0;
    for (const auto& name : { "幽灵鲨", "归溟幽灵鲨", "深海色", "絮雨", "斯卡蒂", "安哲拉", "乌尔比安", "歌蕾蒂娅" }) {
        p.owned[name] = { 1, false, true };
    }
    p.full_field_offer_improvements = { { "真理", 0 }, { "至简", 0 } };
    return p;
}

int main()
{
    const auto pacts = operator_pacts();
    const std::vector<ShopOffer> shop { { "真理", 1, 0 }, { "至简", 1, 1 }, { "米格鲁", 2, 2 } };
    auto p = full_egir();
    const auto first = choose_preparation(p, 1, shop, pacts);
    check(
        first.action == PreparationAction::Buy && first.banked_value_purchase && first.name == "真理" &&
            first.price == 1 && !first.is_equipment && !first.free_refresh,
        "real preparation selector retains one quoted coin");
    auto second = choose_preparation(p, 1, { { "至简", 1, 3 } }, pacts);
    check(
        second.action == PreparationAction::Buy && second.banked_value_purchase && second.name == "至简",
        "both explicit one-price initial bodies qualify");

    for (int funds : { -1, 0, 2, 3 }) {
        check(!choose_preparation(p, funds, shop, pacts).banked_value_purchase, "only the final coin is banked");
    }
    const auto reject = [&](auto mutate, const char* why) {
        auto changed = p;
        mutate(changed);
        check(!choose_preparation(changed, 1, shop, pacts).banked_value_purchase, why);
    };
    reject([](auto& c) { c.one_coin_bank_allowed = false; }, "native evidence opt-in required");
    reject([](auto& c) { c.dynamic_decision_mode = false; }, "no legacy/basic-path change");
    reject([](auto& c) { c.primary_locked_after_activation = false; }, "unsettled route is not banked");
    reject([](auto& c) { c.economy_scenario_horizon_round = 0; }, "unknown horizon is not a future payout");
    reject([](auto& c) { c.economy_scenario_horizon_round = c.round; }, "no later preparation to redeem");
    reject([](auto& c) { c.round = 0; }, "unknown round rejected");
    reject([](auto& c) { c.deployed_population = 7; }, "an empty field slot may auto-deploy the bank");
    reject([](auto& c) { c.deployed_population = -1; }, "unknown field count rejected");
    reject([](auto& c) { c.bench_occupied = -1; }, "unknown bench rejected");
    reject([](auto& c) { c.bench_occupied = 9; }, "reserve one free bench slot after buying");
    reject([](auto& c) { c.bench_occupied = 10; }, "full bench cannot buy");
    reject([](auto& c) { c.full_field_offer_improvements.clear(); }, "missing replacement evaluation rejected");
    reject(
        [](auto& c) { c.full_field_offer_improvements = { { "真理", 1 }, { "至简", 1 } }; },
        "positive exchanges must not be labeled bench banks");
    reject([](auto& c) { c.shop_level = 1; }, "native one-price traits remain tier gated");
    reject([](auto& c) { c.disabled_pacts = { "阿戈尔" }; }, "disabled primary is not an established route");
    for (const std::string id : { "band_cannot", "band_substitute", "band_random", "", "unknown" }) {
        reject([&](auto& c) { c.selected_strategy_id = id; }, "cash inheritance, sale replacement or unknown strategy");
    }
    reject(
        [](auto& c) {
            c.owned["真理"] = { 1, false, false };
            c.owned["至简"] = { 1, false, false };
        },
        "do not add a duplicate or trigger synthesis");
    reject([](auto& c) { c.carries = { "真理", "至简" }; }, "configured carries cannot be sale banks");

    auto free = p;
    free.free_refreshes = 1;
    const auto refreshed = choose_preparation(free, 1, shop, pacts);
    check(
        refreshed.action == PreparationAction::Refresh && refreshed.free_refresh && !refreshed.banked_value_purchase,
        "free refresh precedes storing the coin");
    const auto core = choose_preparation(p, 1, { { "真理", 1, 0 }, { "克莱门莎", 1, 1 } }, pacts);
    check(
        core.action == PreparationAction::Buy && core.name == "克莱门莎" && !core.banked_value_purchase,
        "actual affordable carry takes priority");
    check(
        !choose_preparation(p, 1, { { "真理", 1, 0 }, { "克莱门莎", 4, 1 } }, pacts).banked_value_purchase,
        "do not create a same-page funding-sale loop for an expensive carry");
    check(
        !choose_preparation(p, 1, { { "真理", 2, 0 } }, pacts).banked_value_purchase,
        "a trait is not permission to override the current shop quote");
    check(
        !choose_preparation(p, 1, { { "波登可", 1, 0 } }, pacts).banked_value_purchase,
        "sale side effects are not a normal cash bank");
    check(
        !choose_preparation(p, 1, { { "真理", 1, 0 }, { "至简", 1, 0 } }, pacts).banked_value_purchase,
        "duplicate shop slot is contradictory");

    // Simulate only the verified receipt's state changes, not the input or its
    // success. No cash is credited before the native seller verifies it.
    auto held = p;
    check(add_initial_copies(held.owned[first.name]), "one initial-body receipt can be recorded");
    ++held.bench_occupied;
    check(
        !choose_pending_deployment(held, 0, { first.name }),
        "full field keeps the bank off the ordinary deploy path");
    check(!choose_full_bench_sale(held), "holding one body does not cause same-round full-bench liquidation");
    const std::vector<ShopOffer> after_purchase { { "至简", 1, 1 }, { "米格鲁", 2, 2 } };
    check(
        !choose_funding_purchase(held, 0, after_purchase, pacts),
        "same shop cannot immediately fund an off-route card");
    check(
        !choose_preparation(held, 0, after_purchase, pacts).banked_value_purchase,
        "zero post-purchase cash cannot buy another bank");
    ++held.round;
    const auto funding = choose_funding_purchase(held, 3, { { "克莱门莎", 4, 0 } }, pacts);
    check(
        funding && funding->offer.name == "克莱门莎" && funding->deficit == 1,
        "next preparation's actual carry shortage requests one coin");
    const auto sale = choose_full_bench_sale(held, {}, 1);
    check(sale && *sale == "真理", "existing funding seller accepts the retained initial body");
    held.owned.erase("真理");
    held.owned["至简"] = { 1, false, false };
    const auto second_sale = choose_full_bench_sale(held, {}, 1);
    check(second_sale && *second_sale == "至简", "the second allowed bank also passes the existing sale guards");
    auto almost_full = p;
    almost_full.bench_occupied = 8;
    check(
        choose_preparation(almost_full, 1, shop, pacts).banked_value_purchase,
        "eight benched bodies permit one bank and keep the final slot free");
    std::cout << checks << " one-coin bank checks passed; native input and live wins are not asserted\n";
}
