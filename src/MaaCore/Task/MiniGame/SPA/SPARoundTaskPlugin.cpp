#include "SPARoundTaskPlugin.h"
#include "SPAFormation.h"
#include "SPAImageEvidence.h"
#include "SPAJointEmote.h"

#include "SPANumberOcr.h"
#include "SPAObservation.h"
#include "SPAPlanner.h"
#include "SPAPreparationPolicy.h"
#include "SPAProjection.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "Controller/Controller.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Vision/OCRer.h"

namespace asst::spa
{
static std::optional<int> joint_local_leak_counter(const cv::Mat& image, int total)
{
    const cv::Rect badge_roi(608, 43, 70, 19);
    if (image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3 || total <= 0) {
        return std::nullopt;
    }
    cv::Mat hsv, orange;
    cv::cvtColor(image(badge_roi), hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(3, 110, 130), cv::Scalar(22, 255, 255), orange);
    if (cv::countNonZero(orange) <= 12) {
        return 0;
    }
    if (cv::countNonZero(orange.colRange(8, 29)) < 55) {
        return std::nullopt;
    }
    const auto count = shop_number(image, Rect(645, 44, 27, 17), .90);
    return count && *count > 0 && *count <= total ? count : std::nullopt;
}

static std::optional<std::pair<int, int>> ordinary_battle_enemy_counter(const cv::Mat& image)
{
    // The ordinary HUD has a gold numerator and a white slash and denominator.
    // Exclude the enemy emblem. The text model often reads the slash as "1".
    const cv::Rect counter(629, 15, 76, 28);
    const auto digits = image(counter);
    cv::Mat gold, white;
    cv::inRange(digits, cv::Scalar(0, 130, 185), cv::Scalar(155, 255, 255), gold);
    cv::inRange(digits, cv::Scalar(155, 155, 155), cv::Scalar(255, 255, 255), white);
    const auto numerator = cv::boundingRect(gold);

    cv::Mat labels, stats, centroids;
    const int components = cv::connectedComponentsWithStats(white, labels, stats, centroids);
    std::vector<cv::Rect> glyphs;
    for (int i = 1; i < components; ++i) {
        const auto* row = stats.ptr<int>(i);
        if (row[cv::CC_STAT_AREA] >= 12 && row[cv::CC_STAT_HEIGHT] >= 8) {
            glyphs.emplace_back(
                row[cv::CC_STAT_LEFT],
                row[cv::CC_STAT_TOP],
                row[cv::CC_STAT_WIDTH],
                row[cv::CC_STAT_HEIGHT]);
        }
    }
    if (numerator.empty() || numerator.x < 2 || glyphs.size() < 2 || glyphs.size() > 4) {
        return std::nullopt;
    }
    std::ranges::sort(glyphs, {}, &cv::Rect::x);
    const auto& slash = glyphs.front();
    const auto first_digit = glyphs.begin() + 1;
    if (slash.width < 4 || slash.width > 11 || slash.height > 17 || std::abs(slash.y - numerator.y) > 4 ||
        numerator.br().x + 1 >= slash.x || slash.br().x + 1 >= first_digit->x) {
        return std::nullopt;
    }
    const auto stripe = white(slash);
    const auto top = cv::boundingRect(stripe.rowRange(0, 3));
    const auto bottom = cv::boundingRect(stripe.rowRange(stripe.rows - 3, stripe.rows));
    if (top.empty() || bottom.empty() || top.x <= bottom.x + 2) {
        return std::nullopt;
    }
    auto denominator = *first_digit;
    for (auto i = first_digit + 1; i != glyphs.end(); ++i) {
        denominator |= *i;
    }
    const auto read = [&](const cv::Rect& box) {
        const auto padded = (box + counter.tl() - cv::Point(2, 2)) + cv::Size(4, 4);
        return shop_number(image, make_rect<Rect>(padded), .9);
    };
    const auto resolved = read(numerator);
    const auto total = read(denominator);
    return resolved && total && *total > 0 && *resolved <= *total ? std::optional(std::pair(*resolved, *total))
                                                                  : std::nullopt;
}
}

bool asst::spa::RoundTaskPlugin::configure(
    int round,
    int remaining,
    bool layout_reviewed,
    bool wait_for_next,
    int expected_funds,
    bool spending_reviewed,
    std::string spending_reason,
    bool remaining_inferred,
    bool joint_mode)
{
    if (round < 1 || round > 20 || remaining < 0 || remaining > 8 || !layout_reviewed || expected_funds < 0 ||
        expected_funds > 999 || spending_reason.size() > 512 ||
        (expected_funds > 0 && (!spending_reviewed || spending_reason.empty()))) {
        return false;
    }
    m_round = round;
    m_remaining = remaining;
    m_wait_for_next = wait_for_next;
    m_expected_funds = expected_funds;
    m_spending_reviewed = spending_reviewed;
    m_spending_reason = spending_reason;
    m_remaining_inferred = remaining_inferred;
    m_joint_mode = joint_mode;
    return true;
}

bool asst::spa::RoundTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    return msg == AsstMsg::SubTaskStart && details.get("subtask", std::string()) == "ProcessTask" &&
           details.get("details", "task", std::string()) == "MiniGame@SPA@ReadyRound";
}

bool asst::spa::RoundTaskPlugin::on_run_fails()
{
    if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
        process->override_next("MiniGame@SPA@ReadyRound", {});
    }
    return false;
}

bool asst::spa::RoundTaskPlugin::_run()
{
    m_battle_seen = false;
    m_result_seen = false;
    m_last_result.clear();
    m_first_primary_layers.reset();
    m_latest_primary_layers.reset();
    m_latest_primary_layer_sample_enemies.reset();
    m_latest_pact_layer_snapshot.clear();
    m_observed_teammate_core_pacts.clear();
    m_session_interrupted = false;
    m_next_round = -1;
    if (m_round < 1) {
        return false;
    }
    auto image = ctrler()->get_image();
    OCRer ocr(image);
    std::vector<TextObservation> texts;
    std::optional<Rect> ready;
    bool hidden_core_preparation = false;
    for (int attempt = 0;; ++attempt) {
        if (image.empty() || image.cols != 1280 || image.rows != 720) {
            return false;
        }
        const auto recognized = ocr.analyze();
        if (!recognized) {
            return false;
        }
        texts.clear();
        ready.reset();
        const auto hidden_badge = m_round == HiddenCoreRound ? match_hidden_round_badge(image) : std::nullopt;
        for (const auto& text : *recognized) {
            if (!hidden_badge || !is_round_badge_position(text.rect.x, text.rect.y)) {
                texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
            }
            if (text.text == "准备就绪" && text.score >= 0.9 && text.rect.x > 1100 && text.rect.y < 150) {
                ready = text.rect;
            }
        }
        if (hidden_badge) {
            texts.push_back(*hidden_badge);
        }
        const auto observed = observe_page(texts, {});
        const auto funds = shop_number(image, Rect(1180, 510, 45, 40));
        const auto round =
            hidden_badge && observed.bonus_round ? observed.round : preparation_round_number(image, m_round);
        const auto counter = observe_deployment_counter(texts);
        const auto remaining = remaining_deployment_number(image, texts);
        const bool unreadable_counter =
            !counter.contradictory && counter.label_y >= 0 && !remaining && !observed.remaining_deployment;
        const bool inferred_counter_visible =
            m_remaining_inferred && m_remaining >= 0 && m_remaining <= 8 && !counter.contradictory && !remaining &&
            !observed.remaining_deployment && std::any_of(texts.begin(), texts.end(), [](const auto& text) {
                return text.score >= 0.85 && text.text.find("剩余可放置角色") != std::string::npos;
            });
        const bool remaining_ok = (remaining && *remaining == m_remaining) || inferred_counter_visible;
        const bool page_matches = observed.page == Page::Preparation && !observed.contradictory && ready &&
                                  funds == m_expected_funds && round == m_round;
        if (page_matches && remaining_ok) {
            hidden_core_preparation =
                !m_joint_mode && hidden_badge && observed.bonus_round == HiddenRoundMode::Independent;
            break;
        }
        if (!page_matches || !unreadable_counter || attempt != 0 || !sleep(150)) {
            LogError << "SPA ready preconditions differ from the reviewed formation and budget";
            return false;
        }
        // One frame can catch the deployment label but miss its digit. Wait
        // here instead of sending the caller back through shopping and layout.
        auto receipt = basic_info_with_what("SPARoundReadyCounterResampled");
        receipt["details"] = json::object { { "round", m_round }, { "input_issued", false } };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        image = ctrler()->get_image();
        ocr.set_image(image);
    }
    const bool preparation_frame_saved = save_evidence(image, utils::path("debug/SPA/before_ready"), true);
    if (!ctrler()->click(*ready)) {
        return false;
    }
    // The battle overlay can be shorter than one OCR interval.  A confirmed
    // preparation click plus an observed round increment is still a valid
    // round transition, but it must retain battle_seen=false in the receipt.
    bool ready_clicked = true;

    bool battle_seen = false;
    bool local_finished_captured = false;
    auto last_recognized = std::chrono::steady_clock::now();
    auto next_transition_capture = last_recognized;
    auto next_battle_capture = last_recognized;
    // The client has a bounded battle timer per round. A fixed 240-second
    // watchdog made one stalled non-boss wave consume several minutes and
    // then look like a global automation hang. Keep a generous transition
    // margin, but stay tied to the round's known theoretical limit.
    const int theoretical_limit = battle_time_limit_seconds(m_round, false);
    const int watchdog_seconds = std::max(90, theoretical_limit + 45);
    // Joint readiness is a vote. Waiting for the other players must not
    // consume the battle watchdog or cause another click that revokes Ready.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(m_joint_mode ? 240 : watchdog_seconds);
    if (m_joint_mode) {
        auto info = basic_info_with_what("SPAJointReadySubmitted");
        info["details"] =
            json::object { { "round", m_round }, { "battle_seen", false }, { "all_players_ready_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, info);
    }
    while (!need_exit() && std::chrono::steady_clock::now() < deadline) {
        if (!sleep(1000)) {
            return false;
        }
        image = ctrler()->get_image();
        ocr.set_image(image);
        const auto current = ocr.analyze();
        if (!current) {
            continue;
        }
        texts.clear();
        std::optional<Rect> confirm_ready;
        for (const auto& text : *current) {
            texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
            if (text.score >= 0.9 && text.text == "确认" && text.rect.x > 850 && text.rect.y > 400 &&
                text.rect.y < 600) {
                confirm_ready = text.rect;
            }
        }
        // The client can show this modal even after the balance reached zero
        // (for example when an unused instruction/equipment choice remains).
        // Round entry has already passed the caller's spending review, so the
        // confirmation itself must not be gated on a positive fund balance.
        if (observe_ready_confirmation(texts) && confirm_ready) {
            save_evidence(ctrler()->get_image(), utils::path("debug/SPA/ready_confirmation"), false);
            if (!ctrler()->click(*confirm_ready) || !sleep(1000)) {
                return false;
            }
            auto info = basic_info_with_what("SPARoundReadyConfirmed");
            info["details"] = json::object { { "round", m_round },
                                             { "funds", m_expected_funds },
                                             { "spending_reviewed", true },
                                             { "battle_seen", false } };
            callback(AsstMsg::SubTaskExtraInfo, info);
            continue;
        }
        const auto page = observe_page(texts, {});
        if (page.page == Page::Spectating) {
            return true; // The session executor restores the local battlefield.
        }
        if (page.page == Page::ReadyWaiting) {
            last_recognized = std::chrono::steady_clock::now();
            continue;
        }
        if (page.page == Page::AuthenticationExpired || page.page == Page::JointIdleRemoval) {
            m_session_interrupted = true;
            auto info = basic_info_with_what("SPASessionRecoveryRequired");
            info["details"] = json::object { { "round", m_round },
                                             { "battle_seen", battle_seen },
                                             { "phase", page.page == Page::JointIdleRemoval
                                                            ? "joint_idle_removal"
                                                            : "authentication_expired" },
                                             { "combat_result", "unverified" },
                                             { "resumable", true },
                                             { "run_victory_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, info);
            return true;
        }
        if (battle_seen && page.page != Page::Battle && std::chrono::steady_clock::now() >= next_transition_capture) {
            save_evidence(ctrler()->get_image(), utils::path("debug/SPA/post_battle_page"), false);
            next_transition_capture = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        }
        if (page.contradictory) {
            return false;
        }
        if (page.page == Page::ResultSummary || page.page == Page::JointTeamWaiting || page.page == Page::Reconnecting) {
            return true; // Let lifecycle advance the result or wait for the client.
        }
        if (page.page == Page::Result) {
            m_result_seen = true;
            const bool result_text_verified = settlement_text_verified(page);
            const bool result_frame_saved =
                result_text_verified && save_evidence(image, utils::path("debug/SPA/round_result_observation"), false);
            auto info = basic_info_with_what("SPARoundResultObserved");
            info["details"] = json::object { { "page", "result" },
                                             { "round", m_round },
                                             { "contradictory", page.contradictory },
                                             { "result_completed", page.result_completed },
                                             { "result_interrupted", page.result_interrupted },
                                             { "result_text_verified", result_text_verified },
                                             { "result_frame_saved", result_frame_saved },
                                             { "result_joint", page.result_joint },
                                             { "result_difficulty_known", page.result_difficulty.has_value() },
                                             { "result_difficulty", page.result_difficulty.value_or(0) },
                                             { "result_difficulty_name", page.result_difficulty_name },
                                             { "passed_rounds_known", page.passed_rounds.has_value() },
                                             { "passed_rounds", page.passed_rounds.value_or(0) },
                                             { "battle_seen", battle_seen },
                                             { "run_victory_verified", false },
                                             { "return_home_verified", false } };
            m_last_result = info["details"].as_object();
            callback(AsstMsg::SubTaskExtraInfo, info);
            return true;
        }
        if (page.page == Page::Battle) {
            last_recognized = std::chrono::steady_clock::now();
            const auto teammate = m_joint_mode ? joint_teammate_view(texts) : std::nullopt;
            const bool auxiliary = m_joint_mode && joint_auxiliary_battle(texts);
            const bool other_board = teammate.has_value() || auxiliary;
            if (!battle_seen && !other_board) {
                deadline = std::chrono::steady_clock::now() + std::chrono::seconds(watchdog_seconds);
                auto info = basic_info_with_what("SPARoundStarted");
                info["details"] = json::object { { "round", m_round }, { "layout_reviewed_by_caller", true } };
                info["details"]["funds_before"] = m_expected_funds;
                info["details"]["spending_reviewed_by_caller"] = m_spending_reviewed;
                info["details"]["spending_reason"] = m_spending_reason;
                info["details"]["hidden_core_preparation_verified"] =
                    hidden_core_preparation && preparation_frame_saved;
                callback(AsstMsg::SubTaskExtraInfo, info);
                battle_seen = true;
                m_battle_seen = true;
            }
            const bool local_finished = m_joint_mode && !other_board && joint_local_wave_finished(texts);
            if ((local_finished && !local_finished_captured) ||
                std::chrono::steady_clock::now() >= next_battle_capture) {
                const bool battle_frame_saved = save_evidence(image, utils::path("debug/SPA/battle_sample"), false);
                const auto life = defence_life_number(image);
                auto enemies_resolved = page.enemies_resolved;
                auto enemies_total = page.enemies_total;
                std::string enemy_counter_source = enemies_resolved ? "full_frame" : "unknown";
                if (!enemies_resolved && std::ranges::any_of(texts, is_battle_round_label)) {
                    if (const auto reading = ordinary_battle_enemy_counter(image);
                        reading && (!enemies_total || *enemies_total == reading->second)) {
                        enemies_resolved = reading->first;
                        enemies_total = reading->second;
                        enemy_counter_source = "ordinary_counter_crop";
                    }
                }
                auto info = basic_info_with_what("SPABattleSample");
                info["details"] =
                    json::object { { "round", m_round },
                                   { "battle_frame_saved", battle_frame_saved },
                                   { "theoretical_time_limit_seconds", battle_time_limit_seconds(m_round, false) },
                                   { "life_known", !other_board && life.has_value() },
                                   { "life", other_board ? 0 : life.value_or(0) },
                                   { "enemies_total_known", enemies_total.has_value() },
                                   { "enemies_total", enemies_total.value_or(0) },
                                   { "enemies_resolved_known", enemies_resolved.has_value() },
                                   { "enemies_resolved", enemies_resolved.value_or(0) },
                                   { "enemy_counter_source", enemy_counter_source },
                                   { "unit_survival_verified", false } };
                if (!m_joint_mode && battle_frame_saved) {
                    const auto boss_fill = battle_boss_hp_bar_fill_percent(image);
                    info["details"]["boss_hp_bar_fill_percent"] =
                        boss_fill ? json::value(*boss_fill) : json::value(nullptr);
                }
                if (m_joint_mode && battle_frame_saved) {
                    info["details"]["joint_board_scope"] =
                        auxiliary ? "auxiliary" : teammate ? "teammate" : local_finished ? "local_finished" : "unknown";
                    if (teammate) {
                        info["details"]["joint_teammate_identity"] = *teammate;
                        info["details"]["joint_teammate_life"] = life ? json::value(*life) : json::value(nullptr);
                    }
                    const bool ordinary_wave = std::ranges::any_of(texts, is_battle_round_label);
                    const auto local_leaks = local_finished && ordinary_wave && enemies_total &&
                                                     enemies_resolved == enemies_total
                                                 ? joint_local_leak_counter(image, *enemies_total)
                                                 : std::nullopt;
                    info["details"]["joint_local_wave_finished"] = local_finished;
                    info["details"]["joint_local_leaks"] = local_leaks ? json::value(*local_leaks) : json::value(nullptr);
                    local_finished_captured |= local_finished;
                }
                if (battle_frame_saved) {
                    // Require an exact pact label and a same-frame counter.
                    // The result page does not expose this battlefield HUD.
                    OCRer pact_ocr(image, Rect(300, 94, 760, 54));
                    std::vector<PactHudAnchor> anchors;
                    std::vector<TextObservation> numbers;
                    const auto pacts = pact_requirements();
                    if (const auto known = pact_ocr.analyze()) {
                        for (const auto& text : *known) {
                            const int center = text.rect.x + text.rect.width / 2;
                            numbers.push_back({ text.text, text.score, center, text.rect.y });
                            if (text.score >= .90 && text.rect.width < 100 && text.rect.y >= 105 && text.rect.y < 145 &&
                                center > 300 && center < 1050 &&
                                std::any_of(
                                    pacts.begin(),
                                    pacts.end(),
                                    [&](const auto& pact) { return pact.name == text.text; })) {
                                anchors.push_back({ text.text, center, text.rect.y });
                            }
                        }
                    }
                    for (const auto& anchor : anchors) {
                        if (const auto number = pact_hud_number(image, anchor.center_x)) {
                            numbers.push_back({ std::to_string(number->first), number->second, anchor.center_x, 98 });
                        }
                    }
                    auto layers = observe_pact_layer_numbers(anchors, numbers);
                    std::erase_if(layers, [](const auto& item) { return item.second > 999; });
                    if (!layers.empty()) {
                        json::object values;
                        for (const auto& [pact, value] : layers) {
                            values[pact] = value;
                        }
                        if (teammate && !auxiliary) {
                            info["details"]["joint_teammate_pact_layers"] = std::move(values);
                            if (const auto core = leading_core_pact(layers)) {
                                m_observed_teammate_core_pacts[*teammate] = *core;
                                info["details"]["joint_teammate_leading_observed_core_pact"] = *core;
                            }
                        }
                        else if (!other_board) {
                            m_latest_pact_layer_snapshot = layers;
                            info["details"]["pact_layers"] = std::move(values);
                        }
                    }
                    if (const auto match = layers.find(m_primary_pact); !other_board && match != layers.end()) {
                        if (!m_first_primary_layers) {
                            m_first_primary_layers = match->second;
                        }
                        m_latest_primary_layers = match->second;
                        m_latest_primary_layer_sample_enemies = enemies_resolved;
                        info["details"]["primary_pact"] = m_primary_pact;
                        info["details"]["primary_layers"] = match->second;
                        info["details"]["primary_layers_source"] = "battle_hud";
                    }
                }
                if (hidden_core_preparation) {
                    json::array titles;
                    for (const auto& text : texts) {
                        if (std::isfinite(text.score) && text.score >= .85 && text.x >= 90 && text.x < 250 &&
                            text.y >= 0 && text.y < 65) {
                            auto title = text.text;
                            std::erase(title, ' ');
                            if (title.starts_with("规程等级")) {
                                titles.emplace_back(std::move(title));
                            }
                        }
                    }
                    info["details"]["regulation_hud_titles"] = std::move(titles);
                }
                callback(AsstMsg::SubTaskExtraInfo, info);
                next_battle_capture = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            }
            if (m_joint_emote_probe && !m_joint_emote_probe_attempted && local_finished) {
                const auto communication = std::ranges::find_if(*current, [](const auto& text) {
                    return text.score >= .90 && text.text == "交流" && text.rect.x >= 0 && text.rect.x < 145 &&
                           text.rect.y >= 640 && text.rect.y < 705;
                });
                if (communication != current->end()) {
                    m_joint_emote_probe_attempted = true;
                    std::optional<int> emote_page;
                    bool panel_verified = false;
                    bool emote_input = false;
                    std::optional<int> column;
                    if (ctrler()->click(communication->rect) && sleep(250)) {
                        const auto panel = ctrler()->get_image();
                        save_evidence(panel, utils::path("debug/SPA/joint_emote_palette"), false);
                        emote_page = joint_emote_page(image, panel);
                        if (emote_page) {
                            OCRer waiting(panel, Rect(430, 640, 402, 64));
                            std::vector<TextObservation> footer;
                            if (const auto read = waiting.analyze()) {
                                for (const auto& text : *read) {
                                    footer.push_back({ text.text, text.score, text.rect.x, text.rect.y });
                                }
                            }
                            panel_verified = joint_local_wave_finished(footer);
                            if (panel_verified) {
                                const auto leaks = page.enemies_total
                                                       ? joint_local_leak_counter(image, *page.enemies_total)
                                                       : std::nullopt;
                                column = joint_emote_column(*emote_page, leaks);
                                emote_input = ctrler()->click(Point(70 + 92 * *column, 480));
                            }
                            if (emote_input && sleep(250)) {
                                save_evidence(ctrler()->get_image(), utils::path("debug/SPA/joint_emote_after"), false);
                            }
                        }
                    }
                    auto sent = basic_info_with_what("SPAJointEmoteProbe");
                    sent["details"] = json::object { { "round", m_round },
                                                     { "first_page_verified", panel_verified && emote_page == 1 },
                                                     { "page_verified", panel_verified },
                                                     { "page", emote_page ? json::value(*emote_page) : json::value(nullptr) },
                                                     { "column", column ? json::value(*column + 1) : json::value(nullptr) },
                                                     { "emote_input_issued", emote_input },
                                                     { "delivery_verified", false } };
                    callback(AsstMsg::SubTaskExtraInfo, sent);
                }
            }
            if (!m_wait_for_next) {
                return true;
            }
        }
        else if (page.page == Page::DataUpdated || page.page == Page::AuthenticationExpired) {
            auto info = basic_info_with_what("SPARoundSessionRecoveryRequired");
            info["details"] = json::object { { "round", m_round },
                                             { "battle_seen", battle_seen },
                                             { "combat_result", "unverified" },
                                             { "resumable", true } };
            callback(AsstMsg::SubTaskExtraInfo, info);
            return true;
        }
        else if (
            (page.page == Page::EquipmentSupply || page.page == Page::BountySelection ||
             page.page == Page::TacticalDecision) &&
            battle_seen) {
            auto info = basic_info_with_what("SPARoundChoiceRequired");
            info["details"] = json::object { { "after_round", m_round },
                                             { "choice_kind",
                                               page.page == Page::EquipmentSupply    ? "equipment_supply"
                                               : page.page == Page::TacticalDecision ? "tactical_decision"
                                                                                     : "bounty_selection" },
                                             { "run_victory_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, info);
            return true;
        }
        else if (page.page == Page::Preparation && ready_clicked) {
            const auto next_round = preparation_round_number(image, m_round + 1);
            if (next_round && *next_round == m_round + 1) {
                m_next_round = *next_round;
                auto info = basic_info_with_what("SPARoundTransition");
                info["details"] = json::object { { "from_round", m_round },
                                                 { "to_round", *next_round },
                                                 { "battle_seen", battle_seen },
                                                 { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, info);
                return true;
            }
        }
        else if (
            page.page == Page::Unknown &&
            std::chrono::steady_clock::now() - last_recognized >= std::chrono::seconds(30)) {
            save_evidence(ctrler()->get_image(), utils::path("debug/SPA/round_transition"), false);
            LogError << "SPA encountered an unrecognized stage; preserving the run for reconciliation";
            return false;
        }
    }
    LogError << "SPA round transition timed out or was interrupted";
    return false;
}
