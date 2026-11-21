#include "Task/MiniGame/SPA/SPAObservation.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <meojson/json.hpp>

using namespace asst::spa;
static int checks = 0;
static void check(bool value, const char* why)
{
    ++checks;
    if (!value) {
        std::cerr << "FAIL: " << why << '\n';
        std::exit(1);
    }
}

static std::vector<TextObservation> read_texts(const json::value& row)
{
    std::vector<TextObservation> texts;
    for (const auto& item : row.at("texts").as_array()) {
        texts.push_back({ item.at("text").as_string(), item.at("score").as_double(),
                          item.at("x").as_integer(), item.at("y").as_integer() });
    }
    return texts;
}

int main()
{
    const auto fixture = json::open(std::filesystem::current_path() / "spa-paid-refresh-archived-fixture.json");
    check(fixture.has_value(), "load unmodified paid-price OCR archives");
    for (const auto& row : fixture->at("samples").as_array()) {
        const auto observed = observe_page(read_texts(row), {});
        check(observed.page == Page::Preparation && !observed.contradictory, "actual archive remains preparation");
        check(observed.round == row.at("expected_round").as_integer(), "paid badge does not change round");
        check(observed.remaining_deployment == row.at("expected_remaining_deployment").as_integer(),
              "deployment count remains independent of paid badge");
        check(observed.paid_refresh_price == 1 && observed.free_refreshes == 0,
              "actual anchored one-price establishes known zero free charges");
    }
    const auto original = read_texts(fixture->at("samples").as_array()[0]);
    const auto is_price = [](const auto& t) { return t.text == "1" && t.x == 1181 && t.y == 422; };
    const auto reject = [&](auto mutate, const char* why) {
        auto texts = original;
        mutate(texts);
        const auto observed = observe_page(texts, {});
        check(!observed.paid_refresh_price && !observed.free_refreshes, why);
    };
    reject([&](auto& ts) { std::erase_if(ts, is_price); }, "missing price is unknown, not free zero");
    reject([](auto& ts) { std::erase_if(ts, [](const auto& t) { return t.text == "刷新"; }); },
           "price without refresh anchor is unknown");
    reject([](auto& ts) { std::erase_if(ts, [](const auto& t) { return t.text == "冻结"; }); },
           "missing freeze control is not the reviewed shop geometry");
    reject([&](auto& ts) { for (auto& t : ts) if (is_price(t)) t.score = .94; }, "price needs strong confidence");
    reject([&](auto& ts) { for (auto& t : ts) if (is_price(t)) t.score = std::numeric_limits<double>::quiet_NaN(); },
           "NaN confidence cannot establish a price");
    reject([](auto& ts) { for (auto& t : ts) if (t.text == "刷新") t.score = .89; },
           "refresh anchor needs strong confidence");
    reject([](auto& ts) { for (auto& t : ts) if (t.text == "冻结") t.y -= 10; },
           "freeze must align with refresh");
    reject([&](auto& ts) { for (auto& t : ts) if (is_price(t)) t.x = 1255; },
           "right-side deployment digit cannot be the refresh price");
    reject([&](auto& ts) { for (auto& t : ts) if (is_price(t)) t.y = 379; },
           "deployment-row digit cannot be the refresh price");
    reject([&](auto& ts) { for (auto& t : ts) if (is_price(t)) t.y = 520; },
           "funds-row digit cannot be the refresh price");
    reject([&](auto& ts) { for (auto& t : ts) if (is_price(t)) t.y = 445; },
           "refresh-caption row is not the price badge");
    reject([&](auto& ts) { for (auto& t : ts) if (is_price(t)) t.text = "0"; },
           "unreviewed zero badge is not evidence of zero free charges");
    reject([&](auto& ts) { for (auto& t : ts) if (is_price(t)) t.text = "2"; },
           "unreviewed alternate price remains unknown");
    reject([](auto& ts) { ts.push_back({ "0", .99, 1185, 423 }); }, "conflicting nearby numeric tokens rejected");
    reject([](auto& ts) { ts.push_back({ "刷新", .99, 1194, 447 }); }, "ambiguous refresh anchors rejected");
    reject([](auto& ts) { ts.push_back({ "免费", .99, 1100, 480 }); }, "unreadable free count prevents paid-zero inference");
    reject([](auto& ts) { ts.push_back({ "2", .99, 1180, 520 }); }, "contradictory funds invalidate paid inference too");

    auto free = original;
    free.push_back({ "FREE x3", .99, 1153, 419 });
    auto observed = observe_page(free, {});
    check(observed.free_refreshes == 3 && !observed.paid_refresh_price,
          "explicit free count takes precedence over a background paid token");
    auto unrelated = original;
    unrelated.push_back({ "FREE x9", .99, 200, 640 });
    observed = observe_page(unrelated, {});
    check(observed.free_refreshes == 0 && observed.paid_refresh_price == 1,
          "unrelated card text is not a free-refresh counter");
    auto not_preparation = original;
    std::erase_if(not_preparation, [](const auto& t) { return t.text == "准备就绪"; });
    check(!observe_page(not_preparation, {}).paid_refresh_price, "no paid inference outside preparation");

    const auto free_fixture = json::open(std::filesystem::current_path() / "spa-free-refresh-conflict-fixture.json");
    check(free_fixture.has_value(), "load prior actual FREE/deployment fixture");
    observed = observe_page(read_texts(*free_fixture), {});
    check(observed.page == Page::Preparation && !observed.contradictory && observed.remaining_deployment == 0 &&
              observed.free_refreshes == 1 && !observed.paid_refresh_price,
          "actual FREE x1 and deployment zero remain distinct");
    std::cout << checks << " archived paid-refresh checks passed; no new OCR or device input\n";
}
