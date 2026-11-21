#include "SPAShopTaskPlugin.h"
#include "SPABenchSalePolicy.h"
#include "SPABountyPolicy.h"
#include "SPAEquipmentCatalogue.h"

#include "SPAImageEvidence.h"
#include "SPANumberOcr.h"
#include "SPAObservation.h"
#include "SPAOperatorCatalogue.h"
#include "SPAProjection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>

#include "Config/TemplResource.h"
#include "Controller/Controller.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Vision/Matcher.h"
#include "Vision/OCRer.h"
#include "Vision/RegionOCRer.h"

namespace asst::spa
{
std::optional<OperatorCardObservation>
    selected_operator_card(const cv::Mat& image, std::string_view verified_panel_name)
{
    if (image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3) {
        return std::nullopt;
    }
    std::vector<std::string> names;
    for (const auto& entry : OperatorCatalogue) {
        names.emplace_back(entry.name);
    }
    OCRer title(image, Rect(0, 170, 340, 45));
    std::vector<TextObservation> texts;
    if (const auto recognized = title.analyze()) {
        for (const auto& text : *recognized) {
            texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
        }
    }
    if (verified_panel_name.empty() && !operator_panel_identity(texts, names)) {
        return std::nullopt;
    }
    // Exclude both the class emblem and the small LV label. Including them
    // made the normal level-one card look like a low-confidence 17.
    OCRer level(image, Rect(103, 210, 62, 34));
    if (const auto recognized = level.analyze()) {
        for (const auto& text : *recognized) {
            texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
        }
    }
    return observe_operator_card(texts, names, verified_panel_name);
}

std::optional<int> defence_life_number(const cv::Mat& image)
{
    const auto roi = defence_life_number_roi(image);
    if (!roi) {
        return std::nullopt;
    }
    OCRer number(image, make_rect<Rect>(*roi));
    number.set_without_det(true);
    number.set_use_char_model(true);
    const auto text = number.analyze();
    if (!text || text->size() != 1 || !std::isfinite(text->front().score) || text->front().score < .90 ||
        text->front().text.empty() || text->front().text.size() > 3 ||
        !std::all_of(text->front().text.begin(), text->front().text.end(), [](unsigned char c) {
            return c >= '0' && c <= '9';
        })) {
        return std::nullopt;
    }
    int value = 0;
    for (const auto digit : text->front().text) {
        value = value * 10 + digit - '0';
    }
    return value <= 999 ? std::optional(value) : std::nullopt;
}

std::optional<int> shop_book_level(const cv::Mat& image, int upgrade_center)
{
    const cv::Rect roi(upgrade_center - 24, 568, 48, 50);
    if (image.empty() || image.type() != CV_8UC3 || (roi & cv::Rect(0, 0, image.cols, image.rows)) != roi) {
        return std::nullopt;
    }
    OCRer ocr(image, make_rect<Rect>(roi));
    ocr.set_without_det(true);
    std::optional<int> level;
    if (const auto text = ocr.analyze(); text && text->size() == 1 && text->front().score >= .90) {
        level = parse_shop_level(text->front().text);
    }
    const auto& one = TemplResource::get_instance().get_templ("MiniGame@SPA@ShopBookOne.png");
    if (shop_book_one_glyph_matches(image(roi), one)) {
        return level && *level != 1 ? std::nullopt : std::optional(1);
    }
    return level;
}

std::optional<int> remaining_deployment_number(const cv::Mat& image, const std::vector<TextObservation>& texts)
{
    const auto counter = observe_deployment_counter(texts);
    if (counter.contradictory || counter.label_y < 0) {
        return std::nullopt;
    }
    if (counter.value) {
        return counter.value;
    }
    const auto value = shop_number(image, Rect(1254, counter.label_y - 6, 26, 40));
    return value && *value >= 0 && *value <= 8 ? value : std::nullopt;
}

std::optional<std::pair<int, double>> pact_hud_number(const cv::Mat& image, int center_x)
{
    if (image.empty() || image.size() != cv::Size(1280, 720) || center_x <= 300 || center_x >= 1050) {
        return std::nullopt;
    }
    const auto read = [&](const Rect& roi, double confidence = .95) -> std::optional<std::pair<int, double>> {
        OCRer number(image, roi);
        number.set_without_det(true);
        number.set_use_char_model(true);
        const auto recognized = number.analyze();
        if (!recognized || recognized->size() != 1) {
            return std::nullopt;
        }
        const auto& token = recognized->front();
        if (!std::isfinite(token.score) || token.score < confidence || token.text.empty() || token.text.size() > 3 ||
            !std::all_of(token.text.begin(), token.text.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) {
            return std::nullopt;
        }
        int value = 0;
        for (const auto digit : token.text) {
            value = value * 10 + digit - '0';
        }
        return std::pair { value, static_cast<double>(token.score) };
    };
    const auto primary = read(Rect(center_x - 23, 98, 46, 18), .55);
    if (primary && primary->second >= .95) {
        return primary;
    }
    // The ring around a lit pact can obscure a single zero in the wide
    // crop. Only an already recognized zero may use the tighter crops:
    // cropping an unreadable multi-digit number must not create a zero.
    if (primary && primary->first == 0) {
        const auto zero_in_region = [&](const Rect& roi) -> std::optional<double> {
            RegionOCRer digit(image, roi);
            digit.set_use_char_model(true);
            const auto token = digit.analyze();
            return token && token->text == "0" && token->score >= .95 ? std::optional<double>(token->score)
                                                                      : std::nullopt;
        };
        const auto inner = zero_in_region(Rect(center_x - 15, 98, 30, 18));
        const auto centered = zero_in_region(Rect(center_x - 14, 100, 28, 14));
        if (inner && centered) {
            return std::pair { 0, std::min(*inner, *centered) };
        }
    }
    // The blue ring's lower-left arc can become a spurious slash in the
    // wide crop (real Victoria "6" was read as "/6"). Keep the confidence
    // threshold and require agreement from two narrower same-frame crops.
    const auto first = read(Rect(center_x - 19, 96, 38, 18));
    const auto second = read(Rect(center_x - 19, 98, 38, 18));
    if (!first || !second || first->first != second->first) {
        return std::nullopt;
    }
    return std::pair { first->first, std::min(first->second, second->second) };
}

std::optional<std::pair<int, double>> sale_credit_number(const cv::Mat& image, int button_x, int button_y)
{
    if (image.empty() || image.size() != cv::Size(1280, 720) || button_x < 600 || button_x >= 900 || button_y < 320 ||
        button_y >= 470) {
        return std::nullopt;
    }
    const auto read = [&](const Rect& roi) -> std::optional<std::pair<int, double>> {
        OCRer number(image, roi);
        number.set_without_det(true);
        number.set_use_char_model(true);
        const auto recognized = number.analyze();
        if (!recognized || recognized->size() != 1) {
            return std::nullopt;
        }
        const auto& token = recognized->front();
        const auto value = sale_credit_from_panel({ { token.text, token.score, roi.x, roi.y } }, button_x, button_y);
        return token.score >= .95 && value ? std::optional(std::pair { *value, static_cast<double>(token.score) })
                                           : std::nullopt;
    };
    if (const auto quote = read(Rect(button_x + 44, button_y - 51, 60, 27))) {
        return quote;
    }
    // The wide crop may include a sprite behind the right edge of the badge.
    // Retry inside the plate, keeping the same signed quote and confidence checks.
    return read(Rect(button_x + 52, button_y - 45, 37, 16));
}

std::optional<int> pact_layers(const cv::Mat& image, const std::string& pact)
{
    OCRer labels(image);
    const auto recognized = labels.analyze();
    std::optional<Rect> roi;
    if (recognized) {
        for (const auto& label : *recognized) {
            if (label.text == pact && label.score >= 0.9 && label.rect.y >= 105 && label.rect.y < 140 &&
                label.rect.x > 300 && label.rect.x < 950) {
                if (roi) {
                    return std::nullopt;
                }
                roi = Rect(label.rect.x + label.rect.width / 2 - 15, 94, 30, 22);
            }
        }
    }
    return roi ? shop_number(image, *roi) : std::nullopt;
}

std::optional<int> shop_number(const cv::Mat& image, const Rect& roi, double minimum_confidence)
{
    OCRer ocr(image, roi);
    ocr.set_without_det(true);
    ocr.set_replace({ { "o", "0" }, { "O", "0" } }, true);
    const auto parse = [minimum_confidence](const OCRer::ResultsVecOpt& recognized) -> std::optional<int> {
        if (!recognized || recognized->size() != 1) {
            return std::nullopt;
        }
        const auto& number = recognized->front();
        // The large book digit below LEVEL is rendered with a glow and is
        // routinely scored around 0.40–0.55 by the numeric OCR.  Keep the
        // fallback bounded to a numeric ROI and accept a lower-confidence
        // single counter here; callers still validate the semantic range
        // (shop level 1..6, funds/price limits, etc.).
        if (!std::isfinite(number.score) || number.score < minimum_confidence || number.text.empty() ||
            number.text.size() > 3 ||
            !std::all_of(number.text.begin(), number.text.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            return std::nullopt;
        }
        int value = 0;
        for (char c : number.text) {
            value = value * 10 + c - '0';
        }
        return value;
    };
    if (const auto value = parse(ocr.analyze())) {
        return value;
    }
    // Tiny gold, teal and white counters can be low-confidence against animated backgrounds.
    cv::Mat mask;
    cv::inRange(image(make_rect<cv::Rect>(roi)), cv::Scalar(0, 90, 140), cv::Scalar(130, 255, 255), mask);
    cv::Mat green;
    cv::inRange(image(make_rect<cv::Rect>(roi)), cv::Scalar(70, 140, 0), cv::Scalar(255, 255, 130), green);
    cv::bitwise_or(mask, green, mask);
    // Settlement counters use the protocol's red accent rather than the
    // gold/teal shop palette. Keep this constrained to the caller's numeric
    // ROI so red map graphics cannot become a number globally.
    cv::Mat red;
    cv::inRange(image(make_rect<cv::Rect>(roi)), cv::Scalar(0, 0, 140), cv::Scalar(130, 130, 255), red);
    cv::bitwise_or(mask, red, mask);
    cv::Mat white;
    cv::inRange(image(make_rect<cv::Rect>(roi)), cv::Scalar(180, 180, 180), cv::Scalar(255, 255, 255), white);
    cv::bitwise_or(mask, white, mask);
    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centroids;
    const int components = cv::connectedComponentsWithStats(mask, labels, stats, centroids);
    for (int component = 1; component < components; ++component) {
        const int left = stats.at<int>(component, cv::CC_STAT_LEFT);
        const int top = stats.at<int>(component, cv::CC_STAT_TOP);
        const int width = stats.at<int>(component, cv::CC_STAT_WIDTH);
        const int height = stats.at<int>(component, cv::CC_STAT_HEIGHT);
        if (left == 0 || top == 0 || left + width >= mask.cols || top + height >= mask.rows) {
            mask.setTo(0, labels == component);
        }
    }
    std::vector<cv::Point> foreground;
    cv::findNonZero(mask, foreground);
    if (foreground.empty()) {
        return std::nullopt;
    }
    cv::Mat padded;
    cv::copyMakeBorder(mask(cv::boundingRect(foreground)), padded, 8, 8, 8, 8, cv::BORDER_CONSTANT, cv::Scalar(0));
    cv::Mat enlarged;
    cv::resize(padded, enlarged, {}, 4, 4, cv::INTER_NEAREST);
    cv::Mat bgr;
    cv::cvtColor(enlarged, bgr, cv::COLOR_GRAY2BGR);
    OCRer enhanced(bgr);
    enhanced.set_without_det(true);
    enhanced.set_replace({ { "o", "0" }, { "O", "0" } }, true);
    return parse(enhanced.analyze());
}

std::optional<TextObservation> match_hidden_round_badge(const cv::Mat& image)
{
    Matcher badge(image);
    badge.set_task_info("MiniGame@SPA@HiddenRoundBadge");
    if (const auto match = badge.analyze()) {
        return TextObservation { "??", match->score, match->rect.x, match->rect.y };
    }
    return std::nullopt;
}

std::optional<int> preparation_round_number(const cv::Mat& image, int expected_round, double confidence)
{
    if (image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3) {
        return std::nullopt;
    }
    if (expected_round != HiddenCoreRound) {
        return shop_number(image, Rect(480, 8, 50, 40), confidence);
    }
    const auto badge = match_hidden_round_badge(image);
    OCRer header(image, Rect(90, 0, 440, 65));
    std::vector<TextObservation> texts;
    if (const auto recognized = header.analyze()) {
        for (const auto& text : *recognized) {
            if (!badge || !is_round_badge_position(text.rect.x, text.rect.y)) {
                texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
            }
        }
    }
    if (badge) {
        texts.push_back(*badge);
    }
    if (std::any_of(texts.begin(), texts.end(), is_hidden_round_badge)) {
        const bool numeric_badge = std::any_of(texts.begin(), texts.end(), [](const auto& text) {
            return text.score >= .65 && is_round_badge_position(text.x, text.y) && !text.text.empty() &&
                   std::all_of(text.text.begin(), text.text.end(), [](char c) { return c >= '0' && c <= '9'; });
        });
        return !numeric_badge && observe_hidden_round(texts) ? std::optional(HiddenCoreRound) : std::nullopt;
    }
    return shop_number(image, Rect(480, 8, 50, 40), confidence);
}
}

bool asst::spa::ShopTaskPlugin::configure(std::string name, int price, int funds, int round, int slot)
{
    if (name.empty() || price < 0 || funds < price || funds > 999 || round < 1 || round > 20 || slot < -1 || slot > 5) {
        return false;
    }
    m_name = std::move(name);
    m_price = price;
    m_funds = funds;
    m_round = round;
    m_slot = slot;
    m_upgrade = false;
    m_refresh = false;
    m_strategy_refresh = false;
    m_equipment_discount = false;
    m_freeze = false;
    m_supply = false;
    m_supply_confirmation_issued = false;
    m_bounty = false;
    m_tactical = false;
    return true;
}

bool asst::spa::ShopTaskPlugin::configure_equipment_discount(
    std::string name,
    int base_price,
    int funds,
    int round,
    int slot)
{
    if (base_price < 1 || slot < 0 || !configure(std::move(name), base_price - 1, funds, round, slot)) {
        return false;
    }
    m_equipment_discount = true;
    return true;
}

bool asst::spa::ShopTaskPlugin::configure_supply(std::string name, int slot)
{
    if (slot < -1 || slot > 5 || !configure(std::move(name), 0, 0, 1)) {
        return false;
    }
    m_supply = true;
    m_supply_slot = slot;
    return true;
}

bool asst::spa::ShopTaskPlugin::configure_bounty(std::string name, bool confirm, int slot)
{
    if (slot < -1 || slot > 2 || !configure(std::move(name), 0, 0, 1)) {
        return false;
    }
    m_bounty = true;
    m_bounty_confirm = confirm;
    m_bounty_slot = slot;
    return true;
}

bool asst::spa::ShopTaskPlugin::configure_tactical(std::string name, bool confirm, int slot)
{
    if (slot < 0 || !configure_bounty(std::move(name), confirm, slot)) {
        return false;
    }
    m_tactical = true;
    return true;
}

std::optional<int> asst::spa::paid_shop_refresh_price(const cv::Mat& image)
{
    const auto price = shop_number(image, Rect(1180, 414, 30, 30));
    return price == 1 ? price : std::nullopt;
}

bool asst::spa::ShopTaskPlugin::configure_refresh(int price, int funds, int round)
{
    // Price 1 is the normal button cost; price 0 is a verified free-refresh
    // charge supplied by a pact/operator/item.
    if ((price != 0 && price != 1) || !configure("刷新", price, funds, round)) {
        return false;
    }
    m_refresh = true;
    return true;
}

bool asst::spa::ShopTaskPlugin::configure_strategy_refresh(int funds, int round, int shop_level)
{
    if (shop_level < 1 || shop_level >= 6 || !configure_refresh(1, funds, round)) {
        return false;
    }
    m_strategy_refresh = true;
    m_strategy_shop_level = shop_level;
    return true;
}

bool asst::spa::ShopTaskPlugin::configure_freeze(int funds, int round)
{
    if (funds < 0 || funds > 999 || round < 1 || round > 20) {
        return false;
    }
    m_name = "冻结";
    m_price = 0;
    m_funds = funds;
    m_round = round;
    m_slot = -1;
    m_upgrade = false;
    m_refresh = false;
    m_strategy_refresh = false;
    m_freeze = true;
    m_supply = false;
    m_bounty = false;
    m_tactical = false;
    return true;
}

bool asst::spa::ShopTaskPlugin::configure_upgrade(int price, int funds, int round)
{
    if (!configure("升级", price, funds, round)) {
        return false;
    }
    m_upgrade = true;
    return true;
}

bool asst::spa::ShopTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    const auto task = details.get("details", "task", std::string());
    return msg == AsstMsg::SubTaskStart && details.get("subtask", std::string()) == "ProcessTask" &&
           task == (m_bounty    ? "MiniGame@SPA@SelectBounty"
                    : m_supply  ? "MiniGame@SPA@SelectSupply"
                    : m_freeze  ? "MiniGame@SPA@FreezeShop"
                    : m_refresh ? "MiniGame@SPA@RefreshShop"
                    : m_upgrade ? "MiniGame@SPA@UpgradeShop"
                                : "MiniGame@SPA@BuyShopItem");
}

bool asst::spa::ShopTaskPlugin::on_run_fails()
{
    if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
        process->override_next("MiniGame@SPA@BuyShopItem", {});
        process->override_next("MiniGame@SPA@UpgradeShop", {});
        process->override_next("MiniGame@SPA@RefreshShop", {});
        process->override_next("MiniGame@SPA@FreezeShop", {});
        process->override_next("MiniGame@SPA@SelectSupply", {});
        process->override_next("MiniGame@SPA@SelectBounty", {});
    }
    return false;
}

bool asst::spa::ShopTaskPlugin::run_strategy_refresh(const cv::Mat& initial)
{
    OCRer ocr(initial);
    const auto before = ocr.analyze();
    if (!before) {
        return false;
    }
    std::vector<TextObservation> texts;
    std::optional<int> level;
    for (const auto& text : *before) {
        texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
        if (text.score >= .85 && text.text == "升级" && text.rect.x < 500 && text.rect.y > 620) {
            const int center = text.rect.x + text.rect.width / 2;
            OCRer level_ocr(initial, Rect(center - 24, 568, 48, 50));
            level_ocr.set_without_det(true);
            if (const auto read = level_ocr.analyze(); read && read->size() == 1 && read->front().score >= .4) {
                level = parse_shop_level(read->front().text);
            }
        }
    }
    const auto funds = shop_number(initial, Rect(1180, 510, 45, 40));
    const auto round = preparation_round_number(initial, m_round);
    if (observe_page(texts, {}).page != Page::Preparation || funds != m_funds || round != m_round ||
        level != m_strategy_shop_level || !active_strategy_ready(initial)) {
        LogWarn << "SPA higher-tier strategy refresh preconditions are unknown";
        return false;
    }
    const auto fingerprint = [](const OCRer::ResultsVec& items) {
        std::vector<std::pair<int, std::string>> names;
        for (const auto& text : items) {
            if (text.score >= .85 && text.rect.y >= 630 && text.rect.y <= 690) {
                if (const auto slot = shop_column_from_right(text.rect.x)) {
                    names.emplace_back(*slot, text.text);
                }
            }
        }
        std::sort(names.begin(), names.end());
        return names;
    };
    const auto previous_shop = fingerprint(*before);
    const Point view(1155, 235);
    if (previous_shop.empty() || !ctrler()->click(view) || !sleep(400)) {
        return false;
    }
    std::string description;
    OCRer detail(ctrler()->get_image(), Rect(750, 145, 390, 180));
    if (const auto read = detail.analyze()) {
        for (const auto& text : *read) {
            if (text.score >= .85) {
                description += text.text;
            }
        }
    }
    // This executor supports this exact instruction only. Merely recognizing
    // the portrait or a teal icon is insufficient to issue a paid command.
    const bool identity =
        description.find("人才锁定") != std::string::npos && description.find("雅拉") != std::string::npos &&
        description.find("刷新") != std::string::npos &&
        (description.find("高一阶") != std::string::npos || description.find("高1阶") != std::string::npos);
    if (!ctrler()->click(view) || !sleep(300)) {
        return false;
    }
    const auto ready = ctrler()->get_image();
    if (!identity || !active_strategy_ready(ready) || shop_number(ready, Rect(1180, 510, 45, 40)) != m_funds ||
        !ctrler()->click(Point(1235, 235))) {
        LogWarn << "SPA higher-tier strategy identity or ready state was not verified";
        return false;
    }
    auto issued = basic_info_with_what("SPAStrategyRefreshInputIssued");
    issued["details"] = json::object { { "strategy_id", "band_support" },
                                       { "round", m_round },
                                       { "funds_before", m_funds },
                                       { "expected_tier", m_strategy_shop_level + 1 },
                                       { "effect_verified", false } };
    callback(AsstMsg::SubTaskExtraInfo, issued);
    std::optional<int> after_funds;
    bool shop_changed = false;
    std::string higher_tier_name;
    for (int attempt = 0; attempt < 3 && !need_exit(); ++attempt) {
        if (!sleep(attempt == 0 ? 500 : 250)) {
            return false;
        }
        const auto after_image = ctrler()->get_image();
        after_funds = shop_number(after_image, Rect(1180, 510, 45, 40));
        if (after_funds != m_funds - 1) {
            continue;
        }
        ocr.set_image(after_image);
        if (const auto read = ocr.analyze()) {
            const auto offers = fingerprint(*read);
            shop_changed = !offers.empty() && offers != previous_shop;
            for (const auto& [slot, name] : offers) {
                const auto fact =
                    std::find_if(OperatorCatalogue.begin(), OperatorCatalogue.end(), [&](const auto& item) {
                        return item.name == name && item.tier == m_strategy_shop_level + 1;
                    });
                if (fact != OperatorCatalogue.end()) {
                    higher_tier_name = name;
                    break;
                }
            }
        }
        if (shop_changed && !higher_tier_name.empty()) {
            break;
        }
    }
    const bool verified = after_funds == m_funds - 1 && shop_changed && !higher_tier_name.empty();
    auto receipt = basic_info_with_what("SPAStrategyRefreshOutcome");
    receipt["details"] = json::object { { "strategy_id", "band_support" },
                                        { "round", m_round },
                                        { "funds_before", m_funds },
                                        { "funds_after", after_funds ? *after_funds : -1 },
                                        { "payment_verified", after_funds == m_funds - 1 },
                                        { "shop_changed_verified", shop_changed },
                                        { "higher_tier_name", higher_tier_name },
                                        { "effect_verified", verified },
                                        { "resumable", true } };
    callback(AsstMsg::SubTaskExtraInfo, receipt);
    return verified;
}

std::optional<int> asst::spa::equipment_shop_price(const cv::Mat& image, int slot)
{
    if (slot < 0 || slot > 5) {
        return std::nullopt;
    }
    if (const auto price = shop_number(image, Rect(1050 - slot * 162, 515, 24, 25))) {
        return price;
    }
    const auto zero = shop_number(image, Rect(1038 - slot * 162, 515, 24, 25));
    return zero == 0 ? zero : std::nullopt;
}

bool asst::spa::ShopTaskPlugin::run_equipment_discount(const cv::Mat& image)
{
    const auto quote = [&](const cv::Mat& frame, bool require_view) -> std::optional<int> {
        OCRer ocr(frame);
        const auto read = ocr.analyze();
        if (!read || preparation_round_number(frame, m_round) != m_round ||
            shop_number(frame, Rect(1180, 510, 45, 40)) != m_funds) {
            return std::nullopt;
        }
        std::vector<TextObservation> texts;
        int matches = 0;
        bool view_visible = false;
        for (const auto& text : *read) {
            texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
            if (text.score < .85) {
                continue;
            }
            matches += text.text == m_name && text.rect.y >= 630 && text.rect.y <= 690 &&
                       shop_column_from_right(text.rect.x) == m_slot;
            view_visible = view_visible || (text.text == "查看" && text.rect.x >= 1120 && text.rect.x <= 1190 &&
                                            text.rect.y >= 195 && text.rect.y < 270);
        }
        if (matches != 1 || observe_page(texts, {}).page != Page::Preparation || (require_view && !view_visible)) {
            return std::nullopt;
        }
        return equipment_shop_price(frame, m_slot);
    };
    const int base_price = m_price + 1;
    const Point view(1155, 235);
    if (!active_strategy_ready(image) || quote(image, true) != base_price || !ctrler()->click(view) || !sleep(400)) {
        return false;
    }
    std::string description;
    OCRer detail(ctrler()->get_image(), Rect(750, 145, 390, 180));
    if (const auto items = detail.analyze()) {
        for (const auto& item : *items) {
            if (item.score >= .85) {
                description += item.text;
            }
        }
    }
    const bool identity =
        description.find("天空盒") != std::string::npos && description.find("内部货源") != std::string::npos;
    if (!ctrler()->click(view) || !sleep(300)) {
        return false;
    }
    const auto ready = ctrler()->get_image();
    if (!identity || !active_strategy_ready(ready) || quote(ready, true) != base_price ||
        !ctrler()->click(Point(1235, 235))) {
        return false;
    }
    std::optional<int> after;
    for (int attempt = 0; attempt < 2 && !need_exit(); ++attempt) {
        if (!sleep(attempt == 0 ? 380 : 200)) {
            break;
        }
        after = quote(ctrler()->get_image(), false);
        if (after == m_price) {
            break;
        }
    }
    auto receipt = basic_info_with_what("SPAStrategyEquipmentDiscountOutcome");
    receipt["details"] = json::object { { "strategy_id", "band_price" },
                                        { "round", m_round },
                                        { "name", m_name },
                                        { "slot", m_slot },
                                        { "base_price", base_price },
                                        { "observed_price", after.value_or(-1) },
                                        { "effect_verified", after == m_price },
                                        { "purchase_committed", false } };
    callback(AsstMsg::SubTaskExtraInfo, receipt);
    return after == m_price;
}

bool asst::spa::ShopTaskPlugin::_run()
{
    m_input_reconciliation_required = false;
    m_bench_capacity_rejection_verified = false;
    if (m_name.empty()) {
        return false;
    }
    auto image = ctrler()->get_image();
    if (image.empty() || image.cols != 1280 || image.rows != 720) {
        return false;
    }
    if (m_strategy_refresh) {
        return run_strategy_refresh(image);
    }
    if (m_equipment_discount) {
        return run_equipment_discount(image);
    }
    if (!m_supply && !m_bounty && !m_tactical && !m_refresh && !m_freeze) {
        OCRer toggle(image, Rect(1100, 620, 180, 90));
        std::vector<TextObservation> controls;
        bool collapsed = false;
        if (const auto read = toggle.analyze()) {
            for (const auto& text : *read) {
                controls.push_back({ text.text, text.score, text.rect.x, text.rect.y });
                collapsed = collapsed || observe_shop_toggle(controls.back()) == false;
            }
        }
        if (collapsed) {
            OCRer ready(image, Rect(1090, 80, 190, 70));
            if (const auto read = ready.analyze()) {
                for (const auto& text : *read) {
                    controls.push_back({ text.text, text.score, text.rect.x, text.rect.y });
                }
            }
            if (const auto round = preparation_round_number(image, m_round, .9)) {
                controls.push_back({ std::to_string(*round), .9, 489, 23 });
            }
            const auto expand = shop_expansion_control(controls, m_round);
            if (!expand || !ctrler()->click(Point(expand->x + 20, expand->y + 12)) || !sleep(350)) {
                return false;
            }
            image = ctrler()->get_image();
            toggle.set_image(image);
            bool expanded = false;
            if (const auto read = toggle.analyze()) {
                for (const auto& text : *read) {
                    expanded =
                        expanded || observe_shop_toggle({ text.text, text.score, text.rect.x, text.rect.y }) == true;
                }
            }
            if (!expanded) {
                return false;
            }
            // Re-read the offer instead of clicking a card from an old frame.
            m_prevalidated_offer = false;
            save_evidence(image, utils::path("debug/SPA/shop_restored_before_transaction"), false);
            auto restored = basic_info_with_what("SPAShopRestoredBeforeTransaction");
            restored["details"] = json::object { { "round", m_round },
                                                 { "name", m_name },
                                                 { "expanded_verified", true },
                                                 { "transaction_issued", false } };
            callback(AsstMsg::SubTaskExtraInfo, restored);
        }
    }

    // In phase one the caller's preparation observation has already matched
    // the operator name, right-hand slot, price and funds.  Re-running a
    // full-screen OCR here was the dominant per-action cost (about 1.5 s)
    // and added no new identity information.  Click the verified card,
    // OCR only the compact confirmation area, and retain the authoritative
    // post-payment funds check.  If the confirmation label is temporarily
    // unreadable, fail closed so the caller can re-observe without replaying
    // a possibly accepted purchase.
    // Zero-price offers use a separate temporary/recruitment confirmation
    // surface on the current client.  Keep those on the strict identity path;
    // the observation-reuse shortcut is only for ordinary paid cards.
    if (m_prevalidated_offer && m_price > 0 && !m_upgrade && !m_refresh && !m_freeze && !m_supply && !m_bounty) {
        if (m_slot < 0 || m_slot > 5) {
            return false;
        }
        // shop_column_from_right() indexes by the OCR label's left edge
        // (roughly 1110 - slot*164), while the clickable card centre is
        // about 45 px to its right.  The old fast path used the label edge
        // formula directly and clicked the gap before slot 3/4, leaving the
        // controller apparently stuck in preparation after a successful
        // deployment.
        const int center_x = 1010 - m_slot * 162;
        const Rect card(center_x - 70, 625, 140, 70);
        const int price_x = (m_price == 0 ? 1038 : 1050) - m_slot * 162;
        const auto price = shop_number(image, Rect(price_x, 515, 24, 25));
        if (!price || *price != m_price || !ctrler()->click(card) || !sleep(m_fast_mode ? 240 : 450)) {
            return false;
        }
        std::optional<Rect> confirm;
        for (int attempt = 0; attempt < 3 && !confirm; ++attempt) {
            const auto selected = ctrler()->get_image();
            OCRer confirmation_ocr(selected, Rect(100, 590, 1000, 130));
            if (const auto recognized = confirmation_ocr.analyze()) {
                std::vector<TextObservation> texts;
                for (const auto& text : *recognized) {
                    texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
                }
                if (reconcile_capacity_rejection(selected, texts, m_slot)) {
                    return false;
                }
                if (const auto control = purchase_confirmation_control(texts, m_slot)) {
                    for (const auto& text : *recognized) {
                        if (text.text == control->text && text.rect.x == control->x && text.rect.y == control->y) {
                            confirm = text.rect;
                        }
                    }
                }
            }
            if (!confirm && attempt < 2 && !sleep(200)) {
                return false;
            }
        }
        if (!confirm) {
            m_input_reconciliation_required = true;
            auto uncertain = basic_info_with_what("SPAPurchaseConfirmationUnobserved");
            uncertain["details"] = json::object { { "name", m_name },
                                                  { "round", m_round },
                                                  { "card_open_input_committed", true },
                                                  { "purchase_confirmation_committed", false },
                                                  { "resumable", true } };
            callback(AsstMsg::SubTaskExtraInfo, uncertain);
            return false;
        }
        if (!ctrler()->click(*confirm) || !sleep(m_fast_mode ? 350 : 700)) {
            return require_input_reconciliation(
                "SPAPurchaseReconciliationRequired",
                "Confirmation input was not reconciled",
                true);
        }
        // The B-server updates the funds badge a few frames after the
        // confirmation click.  A single read can still show the old balance
        // even though the purchase was committed, which used to turn a
        // successful dynamic purchase into a non-combat task failure.
        std::optional<int> after;
        for (int attempt = 0; attempt < 4; ++attempt) {
            after = shop_number(ctrler()->get_image(), Rect(1180, 510, 45, 40));
            if (after && *after == m_funds - m_price) {
                break;
            }
            if (attempt < 3 && !sleep(250)) {
                return false;
            }
        }
        if (!after || *after != m_funds - m_price) {
            LogError << "SPA prevalidated purchase result is uncertain; reconcile before another purchase";
            return require_input_reconciliation(
                "SPAPurchaseReconciliationRequired",
                "Funds after purchase are unverified",
                true);
        }
        auto info = basic_info_with_what("SPAShopPurchase");
        info["details"] = json::object { { "name", m_name },
                                         { "price", m_price },
                                         { "funds_before", m_funds },
                                         { "funds_after", *after },
                                         { "round", m_round },
                                         { "promotion_verified", false },
                                         { "observation_reused", true },
                                         { "confirmation_fallback", false } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        return true;
    }

    // Phase-one upgrades use a fixed, observer-verified price.  The upgrade
    // confirmation is rendered in the lower-left panel on this client, while
    // the generic shop path searched the full frame and could miss it after a
    // short animation.  Use a compact confirmation ROI and reconcile the
    // authoritative funds delta; a direct one-click upgrade is accepted when
    // the delta is already visible.
    if (m_fast_mode && m_upgrade) {
        // The upgrade card shifts horizontally with the visible hand/shop
        // layout (we have observed its label around x=200 and x=360).  A
        // fixed rectangle therefore sometimes clicks empty space and leaves
        // phase one stopped at the first scheduled upgrade.  Locate the
        // lower-left "升级" label in a small crop, then click that verified
        // hitbox instead of guessing its absolute position.
        OCRer upgrade_ocr(image, Rect(0, 570, 500, 150));
        std::optional<Rect> upgrade_label;
        if (const auto recognized = upgrade_ocr.analyze()) {
            for (const auto& text : *recognized) {
                if (text.score >= 0.85 && text.text == "升级" && text.rect.x < 450 && text.rect.y >= 620) {
                    if (upgrade_label) {
                        return false;
                    }
                    upgrade_label = text.rect;
                }
            }
        }
        if (!upgrade_label) {
            return false;
        }
        const int upgrade_center = upgrade_label->x + upgrade_label->width / 2;
        const auto live_price = shop_number(image, Rect(upgrade_center - 18, 515, 36, 36));
        const auto live_funds = shop_number(image, Rect(1180, 510, 45, 40));
        if (!live_price || *live_price != m_price || live_funds != m_funds) {
            return false;
        }
        const auto read_level = [](const cv::Mat& frame, int& center) -> std::optional<int> {
            OCRer labels(frame, Rect(0, 620, 450, 80));
            if (const auto result = labels.analyze()) {
                for (const auto& text : *result) {
                    if (text.score >= 0.85 && text.text == "升级") {
                        center = text.rect.x + text.rect.width / 2;
                        return shop_book_level(frame, center);
                    }
                }
            }
            return std::nullopt;
        };
        const auto level_before = m_price == 0 ? shop_book_level(image, upgrade_center) : std::nullopt;
        if (!ctrler()->click(*upgrade_label) || !sleep(350)) {
            return false;
        }
        const auto selected = ctrler()->get_image();
        OCRer confirmation_ocr(selected, Rect(0, 570, 500, 150));
        std::optional<Rect> confirm;
        if (const auto recognized = confirmation_ocr.analyze()) {
            for (const auto& text : *recognized) {
                if (text.score >= 0.85 && text.text.find("确认升级") != std::string::npos && text.rect.y >= 600) {
                    if (confirm) {
                        return false;
                    }
                    confirm = text.rect;
                }
            }
        }
        if (confirm && (!ctrler()->click(*confirm) || !sleep(500))) {
            return false;
        }
        const auto after_image = ctrler()->get_image();
        const auto after = shop_number(after_image, Rect(1180, 510, 45, 40));
        int after_center = -1;
        const auto level_after = m_price == 0 ? read_level(after_image, after_center) : std::nullopt;
        const auto next_price =
            after_center >= 0 ? shop_number(after_image, Rect(after_center - 18, 515, 36, 36)) : std::nullopt;
        const bool glyph_step = level_before && level_after && *level_after == *level_before + 1;
        // Unchanged money cannot prove a free upgrade. A known starting
        // level and the zero-to-positive next-price reset independently
        // establish this single upgrade when the new glyph is unreadable.
        const bool free_transition = m_price == 0 && level_before && *level_before >= 1 && *level_before < 6 &&
                                     !level_after && next_price && *next_price > 0;
        if (!after || *after != m_funds - m_price || (m_price == 0 && !glyph_step && !free_transition)) {
            LogError << "SPA fast upgrade payment could not be reconciled";
            return false;
        }
        const int actual_price = m_funds - *after;
        auto info = basic_info_with_what("SPAShopUpgrade");
        info["details"] = json::object { { "price", m_price },
                                         { "actual_price", actual_price },
                                         { "funds_before", m_funds },
                                         { "funds_after", *after },
                                         { "round", m_round },
                                         { "level_verified", glyph_step },
                                         { "level_derived_from_free_upgrade_transition", free_transition },
                                         { "confirmation_roi_reused", true } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        return true;
    }
    OCRer ocr(image);
    const auto recognized = ocr.analyze();
    if (!recognized) {
        return false;
    }
    std::vector<TextObservation> texts;
    if (m_bounty) {
        std::optional<Rect> target;

        // Candidate descriptions are frequently split across two OCR boxes
        // (for example "冒失" + "的小弟").  Match within each card instead
        // of requiring one box to contain the complete name.
        struct BountyCard
        {
            int left;
            int right;
            std::string joined;
            std::optional<Rect> anchor;
            bool title_match = false;
            std::vector<std::pair<Rect, std::string>> parts;
        };

        std::array<BountyCard, 3> cards { { { 170, 470, {}, std::nullopt, false, {} },
                                            { 470, 820, {}, std::nullopt, false, {} },
                                            { 820, 1150, {}, std::nullopt, false, {} } } };
        for (const auto& text : *recognized) {
            texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
            if (text.score < 0.85 || text.rect.x < 170 || text.rect.x >= 1150) {
                continue;
            }
            const bool in_description = text.rect.y >= 330 && text.rect.y < 430;
            const bool in_title = text.rect.y >= 260 && text.rect.y < 330;
            const bool exact_title = in_title && normalize_bounty_title(text.text) == normalize_bounty_title(m_name);
            if (!in_description && !exact_title) {
                continue;
            }
            for (auto& card : cards) {
                if (text.rect.x >= card.left && text.rect.x < card.right) {
                    if (in_description) {
                        card.parts.emplace_back(text.rect, text.text);
                    }
                    else if (exact_title) {
                        // Current client builds expose the stable target name
                        // in the title (for example “悬赏·频次I”), while the
                        // description only names the spawned enemy.  Keep
                        // title identity as a separate flag so a title hit
                        // still selects the card center safely.
                        card.title_match = true;
                    }
                    break;
                }
            }
        }
        // Boss bounty titles can wrap onto a second line. Compare the whole
        // title inside its own card; descriptions do not contain the suffix.
        for (const auto& offer : observe_tactical_offers(texts)) {
            if (!offer.title.empty() && normalize_bounty_title(offer.title) == normalize_bounty_title(m_name)) {
                cards[offer.slot].title_match = true;
            }
        }
        for (const auto& card : cards) {
            if (m_bounty_slot >= 0 && &card != &cards[m_bounty_slot]) {
                continue;
            }
            auto parts = card.parts;
            std::sort(parts.begin(), parts.end(), [](const auto& lhs, const auto& rhs) {
                return std::tie(lhs.first.y, lhs.first.x) < std::tie(rhs.first.y, rhs.first.x);
            });
            std::string joined;
            for (const auto& part : parts) {
                joined += part.second;
            }
            if (card.title_match || (!parts.empty() && joined.find(m_name) != std::string::npos)) {
                if (target) {
                    LogError << "SPA bounty target is ambiguous";
                    return false;
                }
                // The description text is not itself a reliable hitbox.  Use
                // the card center after identity matching so split OCR lines
                // cannot leave the tap on a non-interactive label.
                target = Rect(card.left, 245, card.right - card.left, 235);
            }
        }
        LogInfo << "SPA bounty match name=" << m_name
                << " target=" << (target ? std::to_string(target->x) + "," + std::to_string(target->y) : "none");
        const auto bounty_page = observe_page(texts, {}).page;
        LogInfo << "SPA bounty page=" << static_cast<int>(bounty_page);
        const bool bounty_header = std::any_of(texts.begin(), texts.end(), [](const auto& text) {
            return text.text.find("悬赏决策") != std::string::npos || text.text.find("悬赏決策") != std::string::npos;
        });
        // On the confirmation frame the selected card's description can be
        // re-OCR'd with a different glyph (磨袭/磨砻, etc.).  The stable
        // “确认选择” button still sits inside the selected card; use its
        // x-coordinate to recover the card identity instead of rejecting a
        // valid already-selected bounty.
        if (!target && m_bounty_confirm && !m_tactical) {
            for (const auto& text : *recognized) {
                if (text.score < 0.75 || text.text.find("确认选择") == std::string::npos || text.rect.y < 450 ||
                    text.rect.y >= 550) {
                    continue;
                }
                for (const auto& card : cards) {
                    if (m_bounty_slot >= 0 && &card != &cards[m_bounty_slot]) {
                        continue;
                    }
                    if (text.rect.x >= card.left && text.rect.x < card.right) {
                        target = Rect(card.left, 245, card.right - card.left, 235);
                        break;
                    }
                }
                if (target) {
                    break;
                }
            }
        }
        if ((m_tactical ? bounty_page != Page::TacticalDecision
                        : bounty_page != Page::BountySelection && !bounty_header) ||
            !target) {
            return false;
        }
        if (m_bounty_confirm) {
            std::optional<Rect> confirm;
            for (const auto& text : *recognized) {
                if (text.text.find("确认选择") != std::string::npos && text.score >= 0.75 && text.rect.y >= 450 &&
                    text.rect.y < 550 && text.rect.x >= target->x - 100 && text.rect.x < target->x + 280) {
                    if (confirm) {
                        return false;
                    }
                    confirm = text.rect;
                }
            }
            const auto funds_before = shop_number(image, Rect(1180, 510, 45, 40));
            // Bounty selection does not spend funds; the counter may be
            // temporarily obscured by the selection overlay, so it is only
            // an optional consistency check.
            LogInfo << "SPA bounty confirm="
                    << (confirm ? std::to_string(confirm->x) + "," + std::to_string(confirm->y) : "none");
            if (!confirm) {
                return require_input_reconciliation(
                    "SPAChoiceConfirmationUnobserved",
                    "Selected choice has no observed confirmation control",
                    false);
            }
            if (!confirm || !ctrler()->click(*confirm)) {
                return false;
            }
            auto info =
                basic_info_with_what(m_tactical ? "SPATacticalConfirmationIssued" : "SPABountyConfirmationIssued");
            info["details"] = json::object { { "enemy_text", m_name }, { "selection_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, info);
            for (int attempt = 0; attempt < 8 && sleep(1000); ++attempt) {
                const auto after = ctrler()->get_image();
                OCRer page_ocr(after);
                const auto recognized_after = page_ocr.analyze();
                texts.clear();
                bool bounty_overlay_visible = false;
                bool confirmation_visible = false;
                if (recognized_after) {
                    for (const auto& text : *recognized_after) {
                        texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
                        if (text.score >= 0.75 && (text.text.find("悬赏决策") != std::string::npos ||
                                                   text.text.find("选择悬赏目标") != std::string::npos)) {
                            bounty_overlay_visible = true;
                        }
                        if (text.score >= 0.75 && text.text.find("确认选择") != std::string::npos) {
                            confirmation_visible = true;
                        }
                    }
                }
                const auto page = observe_page(texts, {});
                const auto funds_after = shop_number(after, Rect(1180, 510, 45, 40));
                if (page.page == Page::Preparation && !page.contradictory &&
                    (m_tactical || !funds_before || !funds_after || *funds_before == *funds_after)) {
                    info = basic_info_with_what(
                        m_tactical ? "SPATacticalSelectionConfirmed" : "SPABountySelectionConfirmed");
                    info["details"] = json::object { { "enemy_text", m_name },
                                                     { "selection_verified", true },
                                                     { "enemy_spawn_verified", false },
                                                     { "funds", funds_after ? *funds_after : -1 } };
                    callback(AsstMsg::SubTaskExtraInfo, info);
                    return true;
                }
                // The game briefly renders the preparation map before the
                // normal HUD OCR becomes stable.  Once the confirmation
                // button and bounty header are both gone, the selection is
                // committed even if that transition frame is classified as
                // unknown/contradictory.  Do not require funds or round OCR
                // during this bounded recovery window: bounty selection does
                // not spend funds and the next observer pass revalidates the
                // preparation state.
                if (!m_tactical && !bounty_overlay_visible && !confirmation_visible &&
                    page.page != Page::BountySelection) {
                    info = basic_info_with_what("SPABountySelectionConfirmed");
                    info["details"] = json::object { { "enemy_text", m_name },
                                                     { "selection_verified", true },
                                                     { "enemy_spawn_verified", false },
                                                     { "funds", funds_after ? *funds_after : -1 },
                                                     { "transition_frame_recovery", true } };
                    callback(AsstMsg::SubTaskExtraInfo, info);
                    return true;
                }
            }
            return false;
        }
        if (!ctrler()->click(*target) || !sleep(1000)) {
            return false;
        }
        auto info = basic_info_with_what(m_tactical ? "SPATacticalSelectionIssued" : "SPABountySelectionIssued");
        info["details"] = json::object { { "enemy_text", m_name }, { "selection_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        return true;
    }
    if (m_supply) {
        for (const auto& text : *recognized) {
            texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
        }
        const auto own_turn = supply_own_turn(texts);
        const bool joint_supply = own_turn.has_value();
        if (own_turn == false) {
            return false;
        }
        std::optional<Rect> target;
        for (const auto& text : *recognized) {
            const bool title_band =
                joint_supply ? (text.rect.y >= 110 && text.rect.y < 180) || (text.rect.y >= 410 && text.rect.y < 480)
                             : text.rect.y >= 250 && text.rect.y < 330;
            if (text.text == m_name && text.score >= 0.9 && text.rect.x >= 175 && text.rect.x < 1150 && title_band) {
                const int slot =
                    equipment_supply_slot(text.rect.x).value_or(-1) + (joint_supply && text.rect.y >= 410 ? 3 : 0);
                if (m_supply_slot >= 0 && slot != m_supply_slot) {
                    continue;
                }
                if (target) {
                    LogError << "SPA supply target is ambiguous";
                    return false;
                }
                target = text.rect;
            }
        }
        if (observe_page(texts, {}).page != Page::EquipmentSupply || !target || !ctrler()->click(*target) ||
            !sleep(1200)) {
            LogError << "SPA supply selection preconditions or input failed";
            return false;
        }
        auto info = basic_info_with_what("SPASupplySelectionIssued");
        info["details"] =
            json::object { { "name", m_name }, { "slot", m_supply_slot }, { "inventory_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        const auto funds_before = shop_number(image, Rect(1180, 510, 45, 40));
        image = ctrler()->get_image();
        ocr.set_image(image);
        const auto selected = ocr.analyze();
        std::optional<Rect> confirmation;
        bool identity_verified = false;
        std::vector<TextObservation> selected_supply_texts;
        if (selected) {
            for (const auto& text : *selected) {
                selected_supply_texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
                if (text.score >= 0.9 && text.text == m_name && std::abs(text.rect.x - target->x) < 30 &&
                    std::abs(text.rect.y - target->y) < 30) {
                    identity_verified = true;
                }
                if (text.score >= 0.9 && text.text == "确认选择" && std::abs(text.rect.x - target->x) < 100 &&
                    (joint_supply ? text.rect.y >= target->y + 160 && text.rect.y < target->y + 280
                                  : text.rect.y >= 460 && text.rect.y < 530)) {
                    if (confirmation) {
                        return false;
                    }
                    confirmation = text.rect;
                }
            }
        }
        if ((!joint_supply && !funds_before) || !identity_verified || !confirmation ||
            (joint_supply && supply_own_turn(selected_supply_texts) != true) || !ctrler()->click(*confirmation)) {
            LogError << "SPA selected supply confirmation was not verified";
            return false;
        }
        m_supply_confirmation_issued = true;
        info = basic_info_with_what("SPASupplyConfirmationIssued");
        info["details"] = json::object { { "name", m_name }, { "inventory_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        if (!sleep(1200)) {
            return false;
        }
        std::optional<int> funds_after;
        bool restored = false;
        bool expanded = false;
        for (int attempt = 0; attempt < 5 && !need_exit(); ++attempt) {
            image = ctrler()->get_image();
            ocr.set_image(image);
            const auto result = ocr.analyze();
            texts.clear();
            bool ready = false;
            std::optional<Rect> expand;
            if (result) {
                for (const auto& text : *result) {
                    texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
                    if (text.score >= 0.9 && text.text == "准备就绪" && text.rect.x > 1100 && text.rect.y < 150) {
                        ready = true;
                    }
                    if (text.score >= 0.9 && parse_shop_toggle(text.text) == false && text.rect.x > 1100 &&
                        text.rect.y > 620) {
                        expand = text.rect;
                    }
                }
            }
            funds_after = shop_number(image, Rect(1180, 510, 45, 40));
            const auto page = observe_page(texts, {});
            if (supply_preparation_restored(joint_supply, page.page, funds_before, funds_after)) {
                restored = true;
                break;
            }
            if (!expanded && page.page != Page::EquipmentSupply && ready && expand &&
                (joint_supply || funds_after == funds_before)) {
                if (!ctrler()->click(*expand)) {
                    return false;
                }
                expanded = true;
            }
            if (!sleep(1000)) {
                return false;
            }
        }
        if (!restored) {
            LogError << "SPA supply outcome requires reconciliation; no retry will be issued";
            return false;
        }
        info = basic_info_with_what("SPASupplySelectionConfirmed");
        info["details"] = json::object { { "name", m_name },
                                         { "funds_before", funds_before.value_or(-1) },
                                         { "funds_after", funds_after.value_or(-1) },
                                         { "joint_round_income_may_change_funds", joint_supply },
                                         { "inventory_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        return true;
    }
    std::optional<Rect> name_rect;
    std::optional<Rect> freeze_rect;
    bool upgrade_confirmation_open = false;
    for (const auto& text : *recognized) {
        texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
        if (m_refresh && text.score >= 0.9 && text.text == "刷新" && text.rect.x > 1100 && text.rect.y >= 400 &&
            text.rect.y < 500) {
            name_rect = text.rect;
        }
        if (m_upgrade && text.score >= 0.85 && (text.text == "升级" || text.text == "确认升级") && text.rect.x < 450 &&
            text.rect.y >= 630) {
            name_rect = text.rect;
            upgrade_confirmation_open = text.text == "确认升级";
        }
        if (m_freeze && text.score >= 0.9 && text.text == "冻结" && text.rect.x > 950 && text.rect.x < 1120 &&
            text.rect.y >= 380 && text.rect.y < 520) {
            if (freeze_rect) {
                LogError << "SPA freeze button is ambiguous";
                return false;
            }
            freeze_rect = text.rect;
        }
        if (!m_upgrade && !m_refresh && text.score >= 0.85 && text.text == m_name && text.rect.y >= 630 &&
            text.rect.y <= 690 && shop_column_from_right(text.rect.x) &&
            (m_slot == -1 || *shop_column_from_right(text.rect.x) == m_slot)) {
            if (name_rect) {
                LogError << "SPA shop target identity is ambiguous";
                return false;
            }
            name_rect = text.rect;
        }
    }
    const auto page = observe_page(texts, {});
    auto funds = shop_number(image, Rect(1180, 510, 45, 40));
    if (!funds) {
        funds = page.funds;
    }
    const auto round = preparation_round_number(image, m_round);
    // The preparation observer may report a contradiction when expanded shop
    // cards contribute a duplicate round/funds token.  The dedicated round
    // and funds ROIs above are the authoritative values for a shop action;
    // rejecting the action solely on that aggregate flag caused valid cards
    // to be skipped after a previous purchase.
    if (page.page != Page::Preparation || (!m_freeze && !name_rect) || !funds || *funds != m_funds || !round ||
        *round != m_round) {
        LogError << "SPA shop purchase preconditions were not verified page=" << static_cast<int>(page.page)
                 << " contradictory=" << page.contradictory << " target=" << name_rect.has_value()
                 << " funds=" << (funds ? std::to_string(*funds) : "none") << "/" << m_funds
                 << " round=" << (round ? std::to_string(*round) : "none") << "/" << m_round << " slot=" << m_slot;
        return false;
    }
    if (m_freeze) {
        if (!freeze_rect || !ctrler()->click(*freeze_rect) || !sleep(m_fast_mode ? 350 : 900)) {
            return false;
        }
        ocr.set_image(ctrler()->get_image());
        const auto after = ocr.analyze();
        bool toggled = false;
        if (after) {
            for (const auto& text : *after) {
                if (text.score >= 0.85 && (text.text == "解冻" || text.text == "已冻结") && text.rect.x > 950 &&
                    text.rect.x < 1120 && text.rect.y >= 380 && text.rect.y < 520) {
                    toggled = true;
                    break;
                }
            }
        }
        if (!toggled) {
            LogError << "SPA shop freeze outcome was not verified; no retry will be issued";
            return false;
        }
        auto info = basic_info_with_what("SPAShopFreezeVerified");
        info["details"] = json::object { { "funds", m_funds }, { "round", m_round }, { "shop_frozen", true } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        return true;
    }
    if (m_refresh) {
        if (m_price == 1 && paid_shop_refresh_price(image) != m_price) {
            LogError << "SPA refresh price was not verified";
            return false;
        }
        const auto fingerprint = [](const auto& items) {
            std::vector<std::string> names;
            for (const auto& text : items) {
                if (text.score >= 0.85 && text.rect.x >= 125 && text.rect.x < 1100 && text.rect.y >= 630 &&
                    text.rect.y <= 690) {
                    names.push_back(text.text);
                }
            }
            std::sort(names.begin(), names.end());
            names.erase(std::unique(names.begin(), names.end()), names.end());
            return names;
        };
        const auto before_names = fingerprint(*recognized);
        if (before_names.empty() || !ctrler()->click(*name_rect) || !sleep(m_fast_mode ? 350 : 700)) {
            return false;
        }
        // A “请选择奖励” overlay can appear immediately after the refresh
        // input when the inter-round reward page is due. Treat it as a
        // resumable handoff to the parent reward handler, never as a failed
        // refresh and never as a reason to burn the whole action budget.
        std::optional<int> after;
        OCRer::ResultsVecOpt refreshed;
        bool verified = false;
        // A full-page OCR pass costs roughly one second on the target
        // emulator.  The paid refresh has a much cheaper authoritative
        // postcondition: the funds counter must decrease by the button cost.
        // Poll that small ROI first and only fall back to one full OCR pass
        // when the counter is still unchanged (usually a reward transition).
        // This keeps a normal refresh below a couple of seconds instead of
        // spending 8 full OCR passes (10+ seconds) on one action.
        for (int attempt = 0; attempt < 3 && !need_exit(); ++attempt) {
            image = ctrler()->get_image();
            bool reward_transition = false;
            bool full_ocr_this_attempt = false;
            after = shop_number(image, Rect(1180, 510, 45, 40));
            // For a paid refresh, a reconciled funds delta is sufficient to
            // prove that this exact click was accepted. The next observer
            // pass verifies the newly rendered cards and keeps the planner
            // from acting on a stale frame.
            if (m_price > 0 && after && *after == m_funds - m_price) {
                verified = true;
                break;
            }
            // Free refreshes do not change funds, so keep the identity check
            // for that path. It is bounded to the first/last poll and does
            // not turn every paid refresh into a full OCR loop.
            if (m_price == 0 && (attempt == 0 || attempt == 2)) {
                ocr.set_image(image);
                refreshed = ocr.analyze();
                full_ocr_this_attempt = true;
                reward_transition =
                    refreshed && std::any_of(refreshed->begin(), refreshed->end(), [](const auto& text) {
                        return text.score >= 0.85 && text.text.find("请选择奖励") != std::string::npos;
                    });
                if (reward_transition) {
                    auto deferred = basic_info_with_what("SPAShopRefreshDeferredReward");
                    deferred["details"] = json::object { { "funds_before", m_funds },
                                                         { "funds_after", after ? *after : m_funds },
                                                         { "round", m_round },
                                                         { "reward_transition", true },
                                                         { "resumable", true } };
                    callback(AsstMsg::SubTaskExtraInfo, deferred);
                    return true;
                }
                if (refreshed && !fingerprint(*refreshed).empty() && fingerprint(*refreshed) != before_names) {
                    verified = true;
                    break;
                }
            }
            // If the reward toast intercepted the first refresh input, the
            // funds remain unchanged. Once the toast has disappeared, issue
            // one bounded retry; never repeat when the payment already
            // reconciled or when the transition was not observed.
            if (after && *after == m_funds) {
                if (!full_ocr_this_attempt && (attempt == 0 || attempt == 2)) {
                    ocr.set_image(image);
                    refreshed = ocr.analyze();
                    reward_transition =
                        refreshed && std::any_of(refreshed->begin(), refreshed->end(), [](const auto& text) {
                            return text.score >= 0.85 && text.text.find("请选择奖励") != std::string::npos;
                        });
                    if (reward_transition) {
                        // The refresh input was accepted while an inter-round
                        // reward overlay was opening. This is a resumable
                        // page transition, not a failed refresh and never a
                        // reason to wait through more OCR polls. Let the
                        // parent observer handle the bounty/equipment page.
                        auto deferred = basic_info_with_what("SPAShopRefreshDeferredReward");
                        deferred["details"] = json::object { { "funds_before", m_funds },
                                                             { "funds_after", after ? *after : m_funds },
                                                             { "round", m_round },
                                                             { "reward_transition", true },
                                                             { "resumable", true } };
                        callback(AsstMsg::SubTaskExtraInfo, deferred);
                        return true;
                    }
                }
            }
            if (!sleep(m_fast_mode ? 220 : 300)) {
                return false;
            }
        }
        if (!verified || !after) {
            LogError << "SPA refresh outcome was not verified; no retry will be issued";
            return false;
        }
        auto info = basic_info_with_what("SPAShopRefresh");
        info["details"] = json::object { { "price", m_price },
                                         { "funds_before", m_funds },
                                         { "funds_after", *after },
                                         { "round", m_round },
                                         { "shop_changed_verified", true } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        return true;
    }
    if (m_upgrade) {
        const int cost_x = name_rect->x + name_rect->width / 2 - 5;
        const auto price = shop_number(image, Rect(cost_x, 515, 24, 25));
        if (!price || *price != m_price) {
            LogError << "SPA shop upgrade price or click failed";
            return false;
        }
        if (!upgrade_confirmation_open && (!ctrler()->click(*name_rect) || !sleep(m_fast_mode ? 350 : 800))) {
            return false;
        }
        image = ctrler()->get_image();
        ocr.set_image(image);
        const auto confirmation = ocr.analyze();
        std::optional<Rect> upgrade_button;
        if (confirmation) {
            for (const auto& text : *confirmation) {
                if (text.score >= 0.85 && text.text == "确认升级" && text.rect.x < 450 && text.rect.y >= 630) {
                    upgrade_button = text.rect;
                }
            }
        }
        const auto confirmation_funds = shop_number(image, Rect(1180, 510, 45, 40));
        const auto confirmation_price = shop_number(image, Rect(cost_x, 515, 24, 25));
        if (!upgrade_button || !confirmation_funds || *confirmation_funds != m_funds || !confirmation_price ||
            *confirmation_price != m_price || !ctrler()->click(*upgrade_button) || !sleep(m_fast_mode ? 550 : 1200)) {
            LogError << "SPA shop upgrade confirmation was not verified";
            return false;
        }
        const auto after = shop_number(ctrler()->get_image(), Rect(1180, 510, 45, 40));
        if (!after || *after != m_funds - m_price) {
            LogError << "SPA shop upgrade payment could not be reconciled";
            return false;
        }
        auto info = basic_info_with_what("SPAShopUpgrade");
        info["details"] = json::object { { "price", m_price },
                                         { "funds_before", m_funds },
                                         { "funds_after", *after },
                                         { "round", m_round },
                                         { "level_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        return true;
    }
    const int slot = *shop_column_from_right(name_rect->x);
    // Free-promotion badges center the zero differently from paid gold-coin prices.
    const int price_x = (m_price == 0 ? 1038 : 1050) - slot * 162;
    auto price = shop_number(image, Rect(price_x, 515, 24, 25));
    if (!price && m_price == 0) {
        price = equipment_shop_price(image, slot);
    }
    if (!price || *price != m_price || !ctrler()->click(*name_rect) || !sleep(m_fast_mode ? 350 : 800)) {
        LogError << "SPA shop price or selection could not be verified";
        return false;
    }
    image = ctrler()->get_image();
    ocr.set_image(image);
    const auto detail = ocr.analyze();
    if (!detail) {
        return require_input_reconciliation(
            "SPAPurchaseConfirmationUnobserved",
            "Selected shop card is unreadable",
            false);
    }
    bool identity = false;
    std::optional<Rect> confirm;
    std::vector<TextObservation> detail_texts;
    for (const auto& text : *detail) {
        detail_texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
        if (text.score >= 0.85 && text.text == m_name && text.rect.x < 350 && text.rect.y < 300) {
            identity = true;
        }
    }
    if (reconcile_capacity_rejection(image, detail_texts, slot)) {
        return false;
    }
    if (const auto control = purchase_confirmation_control(detail_texts, slot)) {
        for (const auto& text : *detail) {
            if (text.text == control->text && text.rect.x == control->x && text.rect.y == control->y) {
                confirm = text.rect;
            }
        }
    }
    auto before_confirm = shop_number(image, Rect(1180, 510, 45, 40));
    if (!before_confirm) {
        texts.clear();
        for (const auto& text : *detail) {
            texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
        }
        before_confirm = observe_page(texts, {}).funds;
    }
    if (!identity || !confirm || !before_confirm || *before_confirm != m_funds) {
        LogError << "SPA selected shop item identity or funds changed before confirmation";
        return require_input_reconciliation(
            "SPAPurchaseConfirmationUnobserved",
            "Selected card identity, confirmation or funds are unverified",
            false);
    }
    // Never retry a confirmation when the resulting state is uncertain.
    if (!ctrler()->click(*confirm) || !sleep(m_fast_mode ? 500 : 1200)) {
        return require_input_reconciliation(
            "SPAPurchaseReconciliationRequired",
            "Confirmation input was not reconciled",
            true);
    }
    image = ctrler()->get_image();
    const auto after = shop_number(image, Rect(1180, 510, 45, 40));
    if (!after || *after != m_funds - m_price) {
        LogError << "SPA purchase result is uncertain; reconcile before another purchase";
        return require_input_reconciliation(
            "SPAPurchaseReconciliationRequired",
            "Funds after purchase are unverified",
            true);
    }
    if (!m_fast_mode) {
        ocr.set_image(image);
        const auto post = ocr.analyze();
        if (!post) {
            return require_input_reconciliation(
                "SPAPurchaseReconciliationRequired",
                "Purchased slot is unreadable",
                true);
        }
        if (m_price == 0) {
            std::vector<TextObservation> post_texts;
            for (const auto& text : *post) {
                post_texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
            }
            if (observe_page(post_texts, {}).page != Page::Preparation ||
                std::none_of(
                    post_texts.begin(),
                    post_texts.end(),
                    [](const auto& text) { return observe_shop_toggle(text) == true; }) ||
                purchase_confirmation_control(post_texts, slot)) {
                return require_input_reconciliation(
                    "SPAPurchaseReconciliationRequired",
                    "A zero-price purchase requires the expanded preparation shop after confirmation",
                    true);
            }
        }
        for (const auto& text : *post) {
            if (text.score >= 0.85 && text.text == m_name && text.rect.y >= 630 && text.rect.y <= 690 &&
                shop_column_from_right(text.rect.x) == slot) {
                LogError << "SPA purchased shop slot did not clear; reconcile before continuing";
                return require_input_reconciliation(
                    "SPAPurchaseReconciliationRequired",
                    "Purchased slot did not clear",
                    true);
            }
        }
    }
    auto info = basic_info_with_what("SPAShopPurchase");
    info["details"] = json::object { { "name", m_name },        { "price", m_price }, { "funds_before", m_funds },
                                     { "funds_after", *after }, { "round", m_round }, { "promotion_verified", false } };
    callback(AsstMsg::SubTaskExtraInfo, info);
    return true;
}

bool asst::spa::ShopTaskPlugin::reconcile_capacity_rejection(
    const cv::Mat& frame,
    const std::vector<TextObservation>& detail,
    int slot)
{
    if (!selected_purchase_unavailable(detail, slot)) {
        return false;
    }
    const bool equipment = std::any_of(EquipmentCatalogue.begin(), EquipmentCatalogue.end(), [&](const auto& item) {
        return item.name == m_name;
    });
    if (!equipment && m_price <= 0) {
        return false;
    }
    OCRer toast(frame, Rect(440, 472, 450, 47));
    const auto recognized = toast.analyze();
    const auto warning = [equipment](const TextObservation& text) {
        return selected_purchase_capacity_warning(text, equipment);
    };
    const bool clipped_warning =
        recognized && std::any_of(recognized->begin(), recognized->end(), [&](const auto& text) {
            return warning({ text.text, text.score, text.rect.x, text.rect.y });
        });
    if ((!clipped_warning && std::none_of(detail.begin(), detail.end(), warning)) ||
        shop_number(frame, Rect(1180, 510, 45, 40)) != m_funds) {
        return false;
    }
    if (!ctrler()->click(Point(1040, 80)) || !sleep(m_fast_mode ? 250 : 450)) {
        return false;
    }
    m_bench_capacity_rejection_verified = !equipment;
    m_equipment_capacity_rejection_verified = equipment;
    auto info = basic_info_with_what(
        equipment ? "SPAShopPurchaseRejectedByEquipmentCapacity" : "SPAShopPurchaseRejectedByBenchCapacity");
    info["details"] = json::object { { "name", m_name },
                                     { "round", m_round },
                                     { "funds", m_funds },
                                     { "slot", slot },
                                     { "purchase_confirmation_committed", false } };
    callback(AsstMsg::SubTaskExtraInfo, info);
    return true;
}

bool asst::spa::ShopTaskPlugin::require_input_reconciliation(
    std::string_view event,
    std::string_view reason,
    bool confirmation_issued)
{
    m_input_reconciliation_required = true;
    auto info = basic_info_with_what(std::string(event));
    info["details"] = json::object { { "name", m_name },
                                     { "round", m_round },
                                     { "reason", std::string(reason) },
                                     { "confirmation_input_issued", confirmation_issued },
                                     { "resumable", true },
                                     { "result_verified", false } };
    callback(AsstMsg::SubTaskExtraInfo, info);
    return false;
}
