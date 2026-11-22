#include "SPAObserverTaskPlugin.h"

#include <utility>

#include "Controller/Controller.h"
#include "SPAFormation.h"
#include "SPAImageEvidence.h"
#include "SPANumberOcr.h"
#include "SPAObservation.h"
#include "SPAProjection.h"
#include "Vision/Matcher.h"
#include "Vision/OCRer.h"

bool asst::spa::ObserverTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    const auto task = details.get("details", "task", std::string());
    return msg == AsstMsg::SubTaskStart && details.get("subtask", std::string()) == "ProcessTask" &&
           (task == "MiniGame@SPA@Observe" || task == "MiniGame@SPA@ObserveDifficulty" ||
            // A native `next` chain resolves the page id to its short task name
            // ("Observe") before emitting SubTaskStart.  Accept that spelling
            // as well so cleanup/return-home chains get the same observation
            // callback as a root Custom task.
            task == "Observe" || task == "ObserveResult" || task == "ReturnResultHome" ||
            task == "MiniGame@SPA@ReturnResultHome" ||
            // Cleanup tasks enter the result page immediately before GiveUp or
            // ReturnHome.  Observe that screen at the plugin boundary so the
            // guarded Python wrapper can prove interruption before navigation.
            task == "DiagnosticGiveUp" || task == "DiagnosticReturnHome");
}

bool asst::spa::ObserverTaskPlugin::_run()
{
    const auto image = ctrler()->get_image();
    const int expected = std::exchange(m_expected_preparation_round, -1);
    return (expected > 0 && observe_frame(image, expected)) || observe_frame(image);
}

bool asst::spa::ObserverTaskPlugin::observe_frame(const cv::Mat& image)
{
    return observe_frame(image, -1);
}

bool asst::spa::ObserverTaskPlugin::observe_frame(const cv::Mat& image, int expected_preparation_round)
{
    m_last_observation = json::object {};
    if (image.empty() || image.cols != 1280 || image.rows != 720) {
        LogError << "SPA observer requires a normalized 1280x720 image";
        return false;
    }
    const bool hud_only = expected_preparation_round > 0;
    std::optional<OCRer::ResultsVec> recognized;
    if (hud_only) {
        OCRer header(image, Rect(0, 0, 1280, 147));
        recognized = header.analyze();
        if (!recognized) {
            return false;
        }
        const auto visible = [&](std::string_view label, int minimum_x, int maximum_x) {
            return std::any_of(recognized->begin(), recognized->end(), [&](const auto& text) {
                return text.score >= .90 && text.text == label && text.rect.x >= minimum_x && text.rect.x < maximum_x;
            });
        };
        if (!visible("休息一下", 540, 760) || !visible("准备就绪", 1100, 1280)) {
            return false;
        }
        for (const auto& region : { Rect(950, 344, 330, 173), Rect(475, 478, 410, 39), Rect(0, 600, 1280, 112) }) {
            OCRer surface(image, region);
            if (const auto detected = surface.analyze()) {
                recognized->insert(recognized->end(), detected->begin(), detected->end());
            }
        }
        if (std::any_of(recognized->begin(), recognized->end(), [](const auto& text) {
                return text.score >= .85 && text.text == "确认购买";
            })) {
            return false;
        }
        // An expanded, actionable shop quote must still be present. A faded HUD
        // under a reward or confirmation overlay is not a new preparation read.
        if (!visible("刷新", 1100, 1280) || (!visible("冻结", 985, 1150) && !visible("解冻", 985, 1150))) {
            return false;
        }
    }
    else {
        OCRer ocr(image);
        recognized = ocr.analyze();
        if (!recognized) {
            LogError << "SPA observer OCR failed";
            return false;
        }
    }
    std::vector<TextObservation> texts;
    json::array raw;
    const bool round_label_visible = std::any_of(recognized->begin(), recognized->end(), [](const auto& text) {
        return text.score >= .90 && text.text == "回合" && text.rect.x >= 410 && text.rect.x < 480 && text.rect.y < 65;
    });
    const auto hidden_badge = round_label_visible ? match_hidden_round_badge(image) : std::nullopt;
    for (const auto& text : *recognized) {
        // The top-center round badge is read again by the dedicated numeric
        // ROI below. Generic OCR frequently sees map/background glyphs in the
        // same strip (for example a stray 7), which would create a false
        // contradiction with the trusted round read.
        TextObservation token { text.text, text.score, text.rect.x, text.rect.y };
        if (round_label_visible && is_round_badge_position(token.x, token.y) &&
            (hidden_badge || !is_hidden_round_badge(token))) {
            continue;
        }
        texts.push_back(std::move(token));
        raw.emplace_back(
            json::object { { "text", text.text },
                           { "score", text.score },
                           { "x", text.rect.x },
                           { "y", text.rect.y } });
    }
    // Funds are rendered as a yellow diamond badge at the bottom-right of the hand strip.
    // The round phase banner in the top bar carries the defence life instead, so it must never feed funds.
    if (const auto current_funds = shop_number(image, Rect(1180, 510, 45, 40))) {
        texts.push_back({ std::to_string(*current_funds), 0.65, 1180, 520 });
        raw.emplace_back(
            json::object { { "text", std::to_string(*current_funds) },
                           { "score", 0.65 },
                           { "score_is_lower_bound", true },
                           { "x", 1180 },
                           { "y", 520 },
                           { "source", "funds_roi" } });
    }
    OCRer round(image, Rect(480, 8, 50, 40));
    round.set_without_det(true);
    if (hidden_badge) {
        texts.push_back(*hidden_badge);
        raw.emplace_back(
            json::object { { "text", hidden_badge->text },
                           { "score", hidden_badge->score },
                           { "x", hidden_badge->x },
                           { "y", hidden_badge->y },
                           { "source", "hidden_round_badge" } });
    }
    else if (const auto numbers = round_label_visible ? round.analyze() : std::nullopt) {
        for (const auto& number : *numbers) {
            texts.push_back({ number.text, number.score, 489, 23 });
            raw.emplace_back(
                json::object { { "text", number.text },
                               { "score", number.score },
                               { "x", 489 },
                               { "y", 23 },
                               { "source", "round_roi" } });
        }
    }
    // The counter moves vertically with the shop. Never read the old map
    // coordinate without first locating the live label in this same frame.
    const auto counter = observe_deployment_counter(texts);
    const auto remaining = remaining_deployment_number(image, texts);
    if (remaining) {
        texts.push_back({ std::to_string(*remaining), 0.65, 1255, counter.label_y });
        raw.emplace_back(
            json::object { { "text", std::to_string(*remaining) },
                           { "score", 0.65 },
                           { "score_is_lower_bound", true },
                           { "x", 1255 },
                           { "y", counter.label_y },
                           { "source", "label_anchored_remaining" } });
    }
    const auto identities = strategy_identities();
    auto observed = observe_page(texts, identities);
    if (hud_only &&
        (observed.page != Page::Preparation || observed.contradictory || observed.round != expected_preparation_round ||
         !observed.funds || !observed.remaining_deployment)) {
        return false;
    }
    bool result_frame_saved = false;
    if (observed.page == Page::Result || observed.page == Page::ResultSummary) {
        // Keep the immutable evidence frame before navigation dismisses it.
        result_frame_saved = save_evidence(image, utils::path("debug/SPA/result_observation"), false);
    }
    if (counter.contradictory) {
        observed.contradictory = true;
        observed.remaining_deployment.reset();
    }
    else if (observed.page == Page::Preparation && !observed.contradictory) {
        observed.remaining_deployment = remaining;
    }
    // The large red number under “通过回合” is often omitted by the general
    // OCR detector. Re-read only its fixed result ROI with the numeric OCR
    // fallback; absence remains unknown rather than being guessed.
    if (observed.page == Page::Result && !observed.contradictory && !observed.passed_rounds) {
        const auto result_roi = observed.result_joint ? Rect(250, 640, 110, 75) : Rect(35, 295, 140, 100);
        // Settlement evidence must not inherit the .40 shop-counter fallback.
        if (const auto passed = shop_number(image, result_roi, .90); passed && *passed >= 1 && *passed <= 20) {
            observed.passed_rounds = *passed;
            raw.emplace_back(
                json::object { { "text", std::to_string(*passed) },
                               { "score", .90 },
                               { "score_is_lower_bound", true },
                               { "source", "result_rounds_roi" },
                               { "x", result_roi.x },
                               { "y", result_roi.y } });
        }
    }
    // The banner keeps the defence life next to the round phase, and the upgrade price sits just
    // above the upgrade button; neither shares a pixel range with the funds badge.
    std::optional<int> life;
    std::optional<int> upgrade_price;
    std::optional<int> current_shop_level;
    if (observed.page == Page::Preparation && !observed.contradictory) {
        life = defence_life_number(image);
        std::optional<int> upgrade_center;
        for (const auto& text : *recognized) {
            if (text.score >= 0.85 && text.text == "升级" && text.rect.x >= 0 && text.rect.x < 450 &&
                text.rect.y >= 620 && text.rect.y < 700) {
                upgrade_center = text.rect.x + text.rect.width / 2;
                break;
            }
        }
        // The large digit below LEVEL is the current shop/book level. The
        // upper badge read above is only the next upgrade price; confusing
        // the two makes a one-book run look like a four-book run.
        if (upgrade_center) {
            upgrade_price = shop_number(image, Rect(*upgrade_center - 18, 515, 36, 36));
            current_shop_level = shop_book_level(image, *upgrade_center);
        }
        if (current_shop_level && *current_shop_level >= 1 && *current_shop_level <= 6) {
            observed.shop_level = *current_shop_level;
        }
        observed.upgrade_price = upgrade_price;
    }
    const bool result_text_verified = settlement_text_verified(observed);
    std::optional<int> preparation_seconds;
    if (observed.page == Page::Preparation && !observed.contradictory) {
        preparation_seconds = preparation_countdown(texts, m_joint_countdown_enabled);
        if (!preparation_seconds && m_joint_countdown_enabled) {
            // Full-frame OCR sometimes drops digits from the small CRT clock.
            OCRer clock(image, Rect(1124, 18, 93, 76));
            if (const auto digits = clock.analyze()) {
                std::vector<TextObservation> clock_text;
                for (const auto& item : *digits) {
                    clock_text.push_back({ item.text, item.score, item.rect.x, item.rect.y });
                }
                // The joint display uses leading zeroes (08, 04, 00).
                // An isolated digit can be one part of a split two-digit read.
                preparation_seconds = preparation_countdown(clock_text, true);
                for (const auto& item : clock_text) {
                    raw.emplace_back(
                        json::object { { "text", item.text },
                                       { "score", item.score },
                                       { "x", item.x },
                                       { "y", item.y },
                                       { "source", "preparation_clock_roi" } });
                }
            }
        }
    }
    // The result page alone cannot prove that the return-home transition occurred or identify a hidden boss.
    const bool return_home_verified = false;
    const bool hidden_boss_marker = false;
    const bool boss_identity_known = false;
    const std::string boss_id;
    bool badge_verified = false;
    bool badge_text_verified = false;
    bool strategy_report_identity_verified = false;
    bool strategy_progress_text_verified = false;
    bool strategy_report_evidence_saved = false;
    std::string strategy_badge_tier;
    if (observed.page == Page::StrategyReport && !observed.contradictory && observed.band_id) {
        const auto selected = std::find_if(identities.begin(), identities.end(), [&](const auto& strategy) {
            return strategy.band_id == *observed.band_id;
        });
        strategy_report_identity_verified =
            selected != identities.end() && strategy_report_identity_text_verified(texts, *selected);
        strategy_progress_text_verified = strategy_report_identity_verified && observed.wins &&
                                          strategy_report_progress_text_verified(texts, *selected, *observed.wins);
        badge_text_verified = strategy_progress_text_verified && *observed.wins >= 3;
        if (badge_text_verified) {
            const bool silver = *observed.wins == 6;
            Matcher marker(image);
            marker.set_task_info(silver ? "MiniGame@SPA@StrategySilverMark" : "MiniGame@SPA@StrategyCompletedMark");
            badge_verified = marker.analyze().has_value();
            if (badge_verified) {
                strategy_badge_tier = silver ? "silver" : "orange";
                strategy_report_evidence_saved =
                    save_evidence(image, utils::path("debug/SPA/strategy_report_observation"), false);
            }
        }
    }
    json::array setup_core_candidates;
    json::array setup_core_rects;
    json::array setup_support_rects;
    json::array setup_support_candidates;
    json::array setup_boss_texts;
    if (observed.page == Page::RunSetup && !observed.contradictory) {
        for (const auto& text : *recognized) {
            if (text.score < 0.85 || text.text.empty()) {
                continue;
            }
            if (text.rect.x >= 900 && text.rect.y >= 300 && text.rect.y < 500) {
                setup_boss_texts.emplace_back(text.text);
            }
            else if (text.rect.y >= 120 && text.rect.y < 480 && text.rect.x < 900) {
                setup_core_candidates.emplace_back(text.text);
                setup_core_rects.emplace_back(
                    json::object { { "text", text.text },
                                   { "x", text.rect.x },
                                   { "y", text.rect.y },
                                   { "w", text.rect.width },
                                   { "h", text.rect.height } });
            }
            else if (text.rect.y >= 500 && text.rect.y < 720 && text.rect.x < 900) {
                setup_support_candidates.emplace_back(text.text);
                setup_support_rects.emplace_back(
                    json::object { { "text", text.text },
                                   { "x", text.rect.x },
                                   { "y", text.rect.y },
                                   { "w", text.rect.width },
                                   { "h", text.rect.height } });
            }
        }
    }
    auto info = basic_info_with_what("SPAObservation");
    json::object pact_numbers;
    if (observed.page == Page::Preparation && !observed.contradictory) {
        std::vector<PactHudAnchor> anchors;
        std::vector<TextObservation> centered_numbers;
        const auto rules = pact_requirements();
        for (const auto& text : *recognized) {
            centered_numbers.push_back({ text.text, text.score, text.rect.x + text.rect.width / 2, text.rect.y });
            if (text.score >= .90 && text.rect.width < 100 &&
                std::any_of(rules.begin(), rules.end(), [&](const auto& rule) { return rule.name == text.text; })) {
                anchors.push_back({ text.text, text.rect.x + text.rect.width / 2, text.rect.y });
            }
        }
        for (const auto& anchor : anchors) {
            if (anchor.label_y < 105 || anchor.label_y >= 145 || anchor.center_x <= 300 || anchor.center_x >= 1050) {
                continue;
            }
            const auto number = pact_hud_number(image, anchor.center_x);
            if (!number) {
                continue;
            }
            // Detector OCR can confidently truncate 51 to 5. Read the full
            // anchored counter even when detection supplied a value, then
            // let the shared conflict check reject disagreement. Never pick
            // the larger value or infer a monotonic layer increase.
            centered_numbers.push_back({ std::to_string(number->first), number->second, anchor.center_x, 98 });
            raw.emplace_back(
                json::object { { "text", std::to_string(number->first) },
                               { "score", number->second },
                               { "x", anchor.center_x },
                               { "y", 98 },
                               { "pact", anchor.pact },
                               { "source", "pact_hud_char_roi" } });
        }
        for (const auto& [pact, layers] : observe_pact_layer_numbers(anchors, centered_numbers)) {
            pact_numbers.emplace(pact, layers);
        }
    }
    const char* page = "unknown";
    switch (observed.page) {
    case Page::Spectating:
        page = "spectating";
        break;
    case Page::ReadyWaiting:
        page = "ready_waiting";
        break;
    case Page::Lobby:
        page = "lobby";
        break;
    case Page::StrategyReport:
        page = "strategy_report";
        break;
    case Page::Difficulty:
        page = "difficulty";
        break;
    case Page::Preparation:
        page = "preparation";
        break;
    case Page::Placement:
        page = "placement";
        break;
    case Page::EquipmentSupply:
        page = "equipment_supply";
        break;
    case Page::TacticalDecision:
        page = "tactical_decision";
        break;
    case Page::DataUpdated:
        page = "data_updated";
        break;
    case Page::JointIdleRemoval:
        page = "joint_idle_removal";
        break;
    case Page::JointTeamWaiting:
        page = "joint_team_waiting";
        break;
    case Page::ReadyConfirmation:
        page = "ready_confirmation";
        break;
    case Page::Battle:
        page = "battle";
        break;
    case Page::BountySelection:
        page = "bounty_selection";
        break;
    case Page::RunSetup:
        page = "run_setup";
        break;
    case Page::AuthenticationExpired:
        page = "authentication_expired";
        break;
    case Page::Result:
        page = "result";
        break;
    case Page::ResultSummary:
        page = "result_summary";
        break;
    case Page::Unknown:
        break;
    }
    info["details"] =
        json::object { { "page", page },
                       { "preparation_boss_bar_visible",
                         observed.page == Page::Preparation && preparation_boss_health_bar(image).has_value() },
                       { "setup_stage", observed.page == Page::RunSetup ? "1/3" : "" },
                       { "setup_core_candidates", std::move(setup_core_candidates) },
                       { "setup_core_rects", std::move(setup_core_rects) },
                       { "setup_support_candidates", std::move(setup_support_candidates) },
                       { "setup_support_rects", std::move(setup_support_rects) },
                       { "setup_boss_texts", std::move(setup_boss_texts) },
                       { "contradictory", observed.contradictory },
                       { "band_id", observed.band_id.value_or("") },
                       { "wins_known", observed.wins.has_value() },
                       { "wins", observed.wins.value_or(0) },
                       { "selected_difficulty_known", observed.selected_difficulty.has_value() },
                       { "selected_difficulty", observed.selected_difficulty.value_or(0) },
                       { "badge_verified", badge_verified },
                       { "strategy_badge_tier", strategy_badge_tier },
                       { "badge_text_verified", badge_text_verified },
                       { "strategy_report_identity_verified", strategy_report_identity_verified },
                       { "strategy_progress_text_verified", strategy_progress_text_verified },
                       { "strategy_report_evidence_saved", strategy_report_evidence_saved },
                       { "result_text_verified", result_text_verified },
                       { "result_frame_saved", result_frame_saved },
                       { "result_completed", observed.result_completed },
                       { "result_interrupted", observed.result_interrupted },
                       { "return_home_verified", return_home_verified },
                       { "hidden_boss_marker", hidden_boss_marker },
                       { "boss_identity_known", boss_identity_known },
                       { "boss_id", boss_id },
                       { "passed_rounds_known", observed.passed_rounds.has_value() },
                       { "passed_rounds", observed.passed_rounds.value_or(0) },
                       { "passed_rounds_scope", observed.result_joint ? "alliance" : "personal" },
                       { "personal_passed_rounds_known", observed.personal_passed_rounds.has_value() },
                       { "personal_passed_rounds", observed.personal_passed_rounds.value_or(0) },
                       { "personal_result_completed", observed.personal_result_completed },
                       { "personal_result_interrupted", observed.personal_result_interrupted },
                       { "personal_result_difficulty_name", observed.personal_result_difficulty_name },
                       { "personal_result_identity", observed.personal_result_identity },
                       { "result_difficulty_known", observed.result_difficulty.has_value() },
                       { "result_difficulty", observed.result_difficulty.value_or(0) },
                       { "result_joint", observed.result_joint },
                       { "result_difficulty_name", observed.result_difficulty_name },
                       { "life_known", life.has_value() },
                       { "life", life.value_or(0) },
                       { "upgrade_price_known", upgrade_price.has_value() },
                       { "upgrade_price", upgrade_price.value_or(0) },
                       { "round_known", observed.round.has_value() },
                       { "bonus_round", observed.bonus_round.has_value() },
                       { "round_source",
                         observed.bonus_round == HiddenRoundMode::Independent ? "independent_hidden_core_phase"
                         : observed.bonus_round == HiddenRoundMode::JointRisk ? "joint_risk_bonus_phase"
                                                                              : "numeric_badge" },
                       { "round", observed.round.value_or(0) },
                       { "remaining_deployment_known", observed.remaining_deployment.has_value() },
                       { "preparation_seconds_remaining", preparation_seconds.value_or(0) },
                       { "preparation_seconds_remaining_known", preparation_seconds.has_value() },
                       { "remaining_deployment", observed.remaining_deployment.value_or(0) },
                       { "funds_known", observed.funds.has_value() },
                       { "funds", observed.funds.value_or(0) },
                       { "free_refreshes_known", observed.free_refreshes.has_value() },
                       { "free_refreshes", observed.free_refreshes.value_or(0) },
                       { "free_refreshes_source",
                         observed.paid_refresh_price ? "paid_refresh_badge"
                         : observed.free_refreshes   ? "explicit_free_counter"
                                                     : "unknown" },
                       { "paid_refresh_price_known", observed.paid_refresh_price.has_value() },
                       { "paid_refresh_price", observed.paid_refresh_price.value_or(0) },
                       { "shop_level_known", observed.shop_level.has_value() },
                       { "shop_level", observed.shop_level.value_or(0) },
                       { "enemies_known", observed.enemies_total.has_value() },
                       { "enemies_resolved_known", observed.enemies_resolved.has_value() },
                       { "enemies_resolved", observed.enemies_resolved.value_or(0) },
                       { "enemies_total", observed.enemies_total.value_or(0) },
                       { "texts", std::move(raw) } };
    info["details"]["pact_layers"] = std::move(pact_numbers);
    info["details"]["observation_scope"] = hud_only ? "verified_preparation_hud_and_shop" : "full_frame";
    m_last_observation = info["details"].as_object();
    callback(AsstMsg::SubTaskExtraInfo, info);
    LogInfo << "SPA page observation:" << page << "contradictory:" << observed.contradictory;
    return observed.page != Page::Unknown && !observed.contradictory;
}
