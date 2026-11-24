#include "Task/MiniGame/SPA/SPAObservation.h"
#include "Task/MiniGame/SPA/SPAOpeningControls.h"
#include "Task/MiniGame/SPA/SPAProjection.h"
#include "Task/MiniGame/SPA/SPASetupSelection.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <meojson/json.hpp>
#include <opencv2/imgcodecs.hpp>

using namespace asst::spa;

namespace spa_setup_test
{
int checks = 0;

void check(bool value, const char* message)
{
    ++checks;
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::vector<TextObservation> read_texts(const json::value& sample)
{
    std::vector<TextObservation> result;
    for (const auto& value : sample.at("texts").as_array()) {
        result.push_back(
            { value.at("text").as_string(),
              value.at("score").as_double(),
              value.at("x").as_integer(),
              value.at("y").as_integer() });
    }
    return result;
}

cv::Mat read_image(const std::string& filename)
{
    std::ifstream input(std::filesystem::current_path() / filename, std::ios::binary);
    std::vector<unsigned char> bytes(std::istreambuf_iterator<char>(input), {});
    return cv::imdecode(bytes, cv::IMREAD_COLOR);
}
}

int main()
{
    using namespace spa_setup_test;
    const auto portraits = read_image("strategy_report_opening_portraits.png");
    check(!portraits.empty(), "strategy report and opening portraits were captured on device");
    const auto strategy = strategy_portrait(portraits, { 0, 0, 120, 120 });
    const auto opening = strategy_portrait(portraits, { 120, 0, 128, 128 });
    const auto detail = strategy_portrait(portraits, { 248, 0, 128, 128 });
    const auto unrelated = strategy_portrait(portraits, { 376, 0, 128, 128 });
    check(
        strategy_portrait_similarity(strategy, opening) >= StrategyPortraitHintThreshold,
        "selected report portrait finds the same opening tile");
    check(
        strategy_portrait_similarity(strategy, detail) >= StrategyPortraitHintThreshold,
        "opening detail panel translation preserves the portrait");
    check(
        strategy_portrait_similarity(strategy, unrelated) < StrategyPortraitHintThreshold,
        "unrelated opening portrait does not receive the same hint");
    check(
        strategy_portrait_similarity(strategy, cv::Mat::zeros(24, 24, CV_8UC1)) == 0.0,
        "blank locked portrait cannot rank as a visual hint");
    const std::vector<TextObservation> scrolled_strategy { { "2/3 选择策略", .965129, 562, 21 },
                                                           { "请选择策略", .999861, 934, 14 },
                                                           { "决策中", .98, 214, 381 } };
    check(verified_opening_strategy_page(scrolled_strategy), "fixed scrolled strategy anchors admit the stage");
    check(observe_page(scrolled_strategy, {}).page == Page::RunSetup, "scrolled strategy page is not unknown");
    auto missing_prompt = scrolled_strategy;
    missing_prompt.erase(missing_prompt.begin() + 1);
    check(!verified_opening_strategy_page(missing_prompt), "stage counter alone does not prove it is our turn");
    auto lowered_header = scrolled_strategy;
    lowered_header.front().y = 360;
    check(!verified_opening_strategy_page(lowered_header), "body text cannot impersonate the strategy stage title");
    const auto fixture = json::open(std::filesystem::current_path() / "setup_scrolled_archived.json");
    check(fixture.has_value(), "actual native OCR fixture loads");
    std::vector<TextObservation> split;
    for (const auto& sample : fixture->at("samples").as_array()) {
        const auto texts = read_texts(sample);
        const bool expected = sample.at("expected_setup").as_boolean();
        check(verified_pact_setup_page(texts) == expected, "actual setup and unrelated-page anchors");
        if (expected) {
            check(observe_page(texts, {}).page == Page::RunSetup, "scrolled setup admitted to native dispatcher");
        }
        if (std::any_of(texts.begin(), texts.end(), [](const auto& text) { return text.text == "请选择一"; })) {
            split = texts;
        }
    }
    check(!split.empty(), "fixture includes exact failed split instruction");
    const auto reject = [&](auto change, const char* reason) {
        auto texts = split;
        change(texts);
        check(!verified_pact_setup_page(texts), reason);
    };
    reject(
        [](auto& ts) { std::erase_if(ts, [](const auto& t) { return t.text.find("1/3") != std::string::npos; }); },
        "missing fixed stage header rejects");
    reject(
        [](auto& ts) {
            for (auto& t : ts) {
                if (t.text.find("1/3") != std::string::npos) {
                    t.y = 350;
                }
            }
        },
        "body text cannot impersonate stage header");
    reject(
        [](auto& ts) {
            for (auto& t : ts) {
                if (t.text.find("1/3") != std::string::npos) {
                    t.text = "2/3确认本局信息";
                }
            }
        },
        "later selection stage is not pact stage");
    reject(
        [](auto& ts) {
            for (auto& t : ts) {
                if (t.text.find("1/3") != std::string::npos) {
                    t.text = "1/3";
                }
            }
        },
        "step number alone is insufficient");
    reject(
        [](auto& ts) { std::erase_if(ts, [](const auto& t) { return t.text == "准备就绪"; }); },
        "missing ready cannot authorize setup readiness");
    reject(
        [](auto& ts) {
            for (auto& t : ts) {
                if (t.text == "准备就绪") {
                    t.y = 110;
                }
            }
        },
        "battle ready anchor cannot impersonate setup button");
    reject(
        [](auto& ts) {
            for (auto& t : ts) {
                if (t.text == "敌方领袖") {
                    t.x = 100;
                }
            }
        },
        "enemy heading must belong to fixed boss panel");
    reject(
        [](auto& ts) {
            std::erase_if(ts, [](const auto& t) {
                return t.text == "本局禁用干员" || t.text.find("本次模拟中激活的盟约") != std::string::npos;
            });
        },
        "scroll fallback requires pact-specific content");
    reject(
        [](auto& ts) {
            for (auto& t : ts) {
                if (t.text == "准备就绪") {
                    t.score = .84;
                }
            }
        },
        "low confidence ready cannot authorize");
    reject(
        [](auto& ts) {
            for (auto& t : ts) {
                if (t.text == "敌方领袖") {
                    t.score = std::numeric_limits<double>::quiet_NaN();
                }
            }
        },
        "nonfinite anchor cannot authorize");
    auto suffix_only = split;
    std::erase_if(suffix_only, [](const auto& t) { return t.text == "请选择一" || t.text == "本局禁用干员"; });
    check(verified_pact_setup_page(suffix_only), "same-position exact instruction suffix survives split OCR");
    std::set<std::string> original_group { "莱塔尼亚", "伊比利亚", "炎", "谢拉格" };
    const auto top = read_image("setup_selected_top.png");
    const auto clipped = read_image("setup_selected_clipped.png");
    const auto banned = read_image("setup_scrolled_banned.png");
    check(top.cols == 1280 && clipped.cols == 1280 && banned.cols == 1280, "original archived frames load");
    const auto first_top = pact_icon_available(top, { 147, 304, 71, 20 }, true);
    const auto first_clipped = pact_icon_available(clipped, { 148, 247, 70, 20 }, true);
    const auto lower_clipped = pact_icon_available(clipped, { 174, 378, 18, 21 }, true);
    const auto other_clipped = pact_icon_available(clipped, { 253, 377, 57, 25 }, true);
    check(first_top == true, "original preferred icon is genuinely selected");
    check(!first_clipped.has_value(), "clipped preferred icon remains unknown with unchanged geometry limits");
    check(lower_clipped == true && other_clipped == true, "same group's visible lower icons remain selected");
    std::vector<SetupCoreObservation> cores { { "莱塔尼亚", first_clipped },
                                              { "炎", lower_clipped },
                                              { "谢拉格", other_clipped },
                                              { "萨尔贡", false } };
    check(
        choose_verified_setup_core(cores, "莱塔尼亚", original_group) == "炎",
        "retry reconciles clipped preferred member through actual same-group icon");
    check(
        choose_verified_setup_core(cores, "谢拉格", original_group) == "谢拉格",
        "readable preferred member remains preferred");
    check(
        !choose_verified_setup_core(cores, "莱塔尼亚", { "莱塔尼亚", "伊比利亚" }),
        "another group cannot substitute for pre-scan identities");
    check(
        !choose_verified_setup_core({ { "莱塔尼亚", std::nullopt }, { "伊比利亚", false } }, "莱塔尼亚"),
        "unknown and gray never imply selected");
    check(
        !pact_icon_available(banned, { 41, 447, 90, 25 }, true).value_or(false),
        "actual banned-list icon cannot satisfy selected-group restore");
    check(
        !choose_verified_setup_core({ { "维多利亚", false }, { "叙拉古", std::nullopt } }, "莱塔尼亚", original_group),
        "scrolled banned-only page needs another fresh frame or bounded restore");
    std::cout << "Setup observation/selection checks passed: " << checks << '\n';
}
