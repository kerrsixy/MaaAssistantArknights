#include "SPAStrategyCardTaskPlugin.h"
#include "SPAImageEvidence.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <tuple>

#include "Config/GeneralConfig.h"
#include "Config/TemplResource.h"
#include "Controller/Controller.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "SPAObservation.h"
#include "SPAOpeningControls.h"
#include "SPAStrategyEligibility.h"
#include "Task/ProcessTask.h"
#include "Vision/Matcher.h"
#include "Vision/OCRer.h"
#include <opencv2/imgproc.hpp>

namespace
{
std::string compact(std::string value)
{
    std::erase(value, ' ');
    std::erase(value, '\t');
    return value;
}

std::vector<int> opening_strategy_rows(const cv::Mat& image, const cv::Mat& edge, bool details_open)
{
    if (image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3 || edge.empty()) {
        return {};
    }
    constexpr int top = 108;
    constexpr int bottom = 640;
    const int left = details_open ? 390 : 654;
    std::array<cv::Mat, 4> matches;
    for (int col = 0; col < 4; ++col) {
        cv::Mat gray;
        cv::cvtColor(
            image(cv::Rect(left + col * 140 - 2, top, 13, bottom - top + edge.rows)),
            gray,
            cv::COLOR_BGR2GRAY);
        cv::matchTemplate(gray, edge, matches[col], cv::TM_CCOEFF_NORMED);
    }
    std::vector<int> rows;
    double previous_score = 0;
    for (int y = top; y <= bottom; ++y) {
        int supported = 0;
        double score = 0;
        for (const auto& match : matches) {
            double confidence = 0;
            cv::minMaxLoc(match.row(y - top), nullptr, &confidence);
            if (confidence >= .91) {
                ++supported;
                score += confidence;
            }
        }
        if (supported < 2) {
            continue;
        }
        if (rows.empty() || y - rows.back() > 22) {
            rows.push_back(y);
            previous_score = score;
        }
        else if (score > previous_score) {
            rows.back() = y;
            previous_score = score;
        }
    }
    return rows;
}

bool opening_strategy_cell_selected(const cv::Mat& image, const asst::Rect& card)
{
    return asst::spa::opening_strategy_tile_highlighted(image, { card.x, card.y, card.width, card.height });
}
}

bool asst::spa::StrategyCardTaskPlugin::configure(int strategy_index, bool prefer_unclaimed, std::string strategy_id)
{
    // -1 means choose any currently available strategy card.  It is an
    // availability mode only; it must never be interpreted as a faction lock.
    if (strategy_index < -1 || strategy_index >= 16) {
        return false;
    }
    m_strategy_index = strategy_index;
    m_selected_strategy_index = strategy_index >= 0 ? strategy_index : -1;
    m_auto_selected = false;
    m_prefer_unclaimed = prefer_unclaimed;
    m_requested_strategy = std::move(strategy_id);
    return true;
}

bool asst::spa::StrategyCardTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    return msg == AsstMsg::SubTaskStart && details.get("subtask", std::string()) == "ProcessTask" &&
           details.get("details", "task", std::string()) == "MiniGame@SPA@SelectStrategy";
}

bool asst::spa::StrategyCardTaskPlugin::_run()
{
    const auto selection_started = std::chrono::steady_clock::now();
    if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
        process->set_task_delay(180);
    }
    auto image = ctrler()->get_image();
    if (image.empty() || image.size() != cv::Size(1280, 720)) {
        return false;
    }
    // The strategy grid, stage header and confirmation control all live in
    // the central/right portion of the setup page.  Keep the left map chrome
    // out of OCR; this cuts the expensive first pass without changing any
    // coordinate semantics (OCRer returns screen-space rectangles).
    OCRer ocr(image, Rect(400, 0, 880, 720));
    auto recognized = ocr.analyze();
    if (!recognized) {
        return false;
    }
    const auto block_setup_transition = [&](const std::string& reason, bool battle_started_unverified) {
        auto blocked = basic_info_with_what("SPARunSetupBlocked");
        blocked["details"] = json::object { { "reason", reason },
                                            { "formation_page_verified", false },
                                            { "reopen_required", true },
                                            { "ready_clicked", false },
                                            { "battle_authorized", false },
                                            { "battle_started_unverified", battle_started_unverified },
                                            { "run_victory_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, blocked);
    };
    const auto transition_state = [&](const auto& texts) {
        bool ready_button = false;
        bool round_marker = false;
        bool remaining_marker = false;
        bool battle_target_page = false;
        bool activation_warning = false;
        bool formation_page = false;
        for (const auto& text : texts) {
            const auto value = compact(text.text);
            ready_button = ready_button || value.find("准备就绪") != std::string::npos;
            round_marker = round_marker || value.find("回合") != std::string::npos;
            remaining_marker = remaining_marker || value.find("剩余可放置角色") != std::string::npos;
            battle_target_page = battle_target_page || value.find("全局作战目标") != std::string::npos ||
                                 value.find("消灭敌方领袖") != std::string::npos ||
                                 value.find("战斗目标") != std::string::npos;
            activation_warning = activation_warning || value.find("难以在模拟中激活") != std::string::npos ||
                                 value.find("盟约难以") != std::string::npos;
            formation_page = formation_page || value.find("3/3") != std::string::npos ||
                             value.find("自选编队") != std::string::npos ||
                             value.find("确定自选编队") != std::string::npos;
        }
        const bool preparation_hud = ready_button && (round_marker || remaining_marker);
        // The game renders this warning on the 3/3 formation page before the
        // operator choices are applied.  Let FormationSelectTaskPlugin handle
        // that page; only block when the warning appears outside a verified
        // formation context or remains after formation selection.
        if (activation_warning && !formation_page) {
            block_setup_transition("Game warns that the selected formation cannot reliably activate a pact", false);
            return true;
        }
        if (battle_target_page) {
            block_setup_transition("Battle objective page appeared before verified 3/3 formation", true);
            return true;
        }
        if (preparation_hud) {
            block_setup_transition("Preparation HUD appeared before verified 3/3 formation", false);
            return true;
        }
        return false;
    };
    bool stage_two = false;
    bool stage_three_already = false;
    for (const auto& text : *recognized) {
        const auto value = compact(text.text);
        if ((value.find("2/3") != std::string::npos || value == "请选择策略") && text.score >= 0.8) {
            stage_two = true;
        }
        if ((value.find("3/3") != std::string::npos || value.find("确定自选编队") != std::string::npos) &&
            text.score >= 0.8) {
            stage_three_already = true;
        }
    }
    if (stage_three_already) {
        if (transition_state(*recognized)) {
            return false;
        }
        if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
            process->override_next("MiniGame@SPA@SelectStrategy", { "MiniGame@SPA@SelectFormation" });
        }
        auto info = basic_info_with_what("SPAStrategyCardSelected");
        info["details"] = json::object { { "strategy_index", m_selected_strategy_index },
                                         { "stage_two_verified", false },
                                         { "stage_three_verified", true },
                                         { "selection_observed", false },
                                         { "advanced_before_task", true },
                                         { "auto_selected", false } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        return true;
    }
    if (!stage_two) {
        LogError << "SPA strategy card task did not see the verified 2/3 page";
        return false;
    }
    save_evidence(image, utils::path("debug/SPA/strategy_opening"), false);
    auto countdown_seconds = opening_strategy_display_countdown(image);
    for (const auto& text : *recognized) {
        if (text.score >= .9 && text.rect.x >= 1100 && text.rect.x < 1235 && text.rect.y < 72 &&
            text.rect.height >= 28) {
            if (const auto seconds = opening_strategy_countdown_seconds(compact(text.text))) {
                countdown_seconds = std::min(countdown_seconds.value_or(*seconds), *seconds);
            }
        }
    }
    // Use the conservative capture start and leave time to come back to the
    // winning viewport, read its details and confirm before the game assigns a card.
    const auto inspection_deadline =
        selection_started + std::chrono::seconds(opening_strategy_inspection_seconds(countdown_seconds));
    bool details_open = std::any_of(recognized->begin(), recognized->end(), [](const auto& text) {
        return text.score >= 0.8 && text.text.find("确认选择") != std::string::npos && text.rect.x >= 970;
    });
    const auto& tile_edge = TemplResource::get_instance().get_templ("MiniGame@SPA@OpeningStrategyTileEdge.png");
    cv::Mat edge;
    if (!tile_edge.empty()) {
        cv::cvtColor(tile_edge, edge, cv::COLOR_BGR2GRAY);
    }
    const auto slot_at = [&](int index, const std::vector<int>& rows, bool panel_open) {
        const auto [x, initial_y, width, height] = opening_strategy_card_rect(index, panel_open);
        if (rows.empty()) {
            return Rect(x, initial_y, width, height);
        }
        const int y = rows.at(index / 4);
        return Rect(x, y, width, std::min(128, 705 - y));
    };
    const auto selected_tile_matches = [&](const cv::Mat& frame, int index, const Rect& clicked_slot) {
        // Opening the details panel can move the entire grid. Verify the
        // highlight against the same column on the post-click frame.
        const auto rows = opening_strategy_rows(frame, edge, true);
        const int x = opening_strategy_card_rect(index % 4, true)[0];
        if (rows.empty()) {
            return opening_strategy_cell_selected(
                frame,
                Rect(x, clicked_slot.y, clicked_slot.width, clicked_slot.height));
        }
        return std::count_if(rows.begin(), rows.end(), [&](int y) {
                   return std::abs(y - clicked_slot.y) < 65 &&
                          opening_strategy_cell_selected(frame, Rect(x, y, 125, 128));
               }) == 1;
    };
    const auto scroll = [&](bool down) {
        const int x = details_open ? 865 : 1115;
        int duration = down ? 400 : 350;
        if (ctrler()->get_controller_type() == ControllerType::Adb) {
            const auto multiplier = Config.get_options().adb_swipe_duration_multiplier;
            if (multiplier > 0) {
                duration = std::max(1, static_cast<int>(duration / multiplier));
            }
        }
        const bool moved = down ? ctrler()->swipe(Point(x, 640), Point(x, 315), duration)
                                : ctrler()->swipe(Point(x, 150), Point(x, 675), duration);
        return moved && sleep(240);
    };
    const auto viewport_marker = [&](const cv::Mat& frame) {
        cv::Mat marker;
        cv::resize(frame(cv::Rect(details_open ? 390 : 654, 108, 545, 580)), marker, cv::Size(24, 24));
        return marker;
    };
    int selected_strategy_index = m_strategy_index;
    std::optional<Rect> selected_card;
    bool specific_selected = false;
    std::string expected_default_identity;
    int inspected_strategy_cards = 0;
    std::vector<std::string> occupied_reported;
    const auto report_occupied = [&](const std::optional<StrategyIdentity>& identity, bool occupied) {
        if (!identity || !occupied ||
            std::find(occupied_reported.begin(), occupied_reported.end(), identity->band_id) !=
                occupied_reported.end()) {
            return;
        }
        occupied_reported.push_back(identity->band_id);
        auto info = basic_info_with_what("SPAStrategyOccupiedObserved");
        info["details"] = json::object { { "strategy_id", identity->band_id },
                                         { "occupied_strategy_ids", occupied_reported },
                                         { "occupied_label_verified", true },
                                         { "teammate_faction_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, info);
    };
    std::map<int, std::pair<std::string, bool>> first_page_observations;
    if (!m_requested_strategy.empty()) {
        const auto identities = strategy_identities();
        const auto wanted = std::find_if(identities.begin(), identities.end(), [&](const auto& identity) {
            return identity.band_id == m_requested_strategy;
        });
        const auto opening_context = m_context_provider ? m_context_provider() : OpeningStrategyContext {};
        const bool requested_mode_available = strategy_available_in_mode(m_requested_strategy, opening_context.mode);
        int requested_scrolls = 0;
        int requested_probes = 0;
        const int catalogue_hint =
            wanted == identities.end() ? -1 : static_cast<int>(std::distance(identities.begin(), wanted));
        // Leave time for the default chooser if the requested card cannot be
        // verified. Within this budget, finish a visible viewport before panning.
        const auto deadline = inspection_deadline - std::chrono::seconds(5);
        cv::Mat last_marker;
        bool requested_unavailable = false;
        for (int page = 0; wanted != identities.end() && requested_mode_available && page < 7 && !need_exit() &&
                           std::chrono::steady_clock::now() < deadline && !specific_selected && !requested_unavailable;
             ++page) {
            image = ctrler()->get_image();
            const auto marker = viewport_marker(image);
            if (!last_marker.empty() && cv::norm(marker, last_marker, cv::NORM_L1) / (marker.total() * 3) < 3) {
                break;
            }
            auto rows = opening_strategy_rows(image, edge, details_open);
            if (page > 0 && rows.empty()) {
                break;
            }
            const int slots = rows.empty() ? 16 : std::min(16, static_cast<int>(rows.size()) * 4);
            for (const int index : opening_strategy_probe_order(slots, page == 0 ? catalogue_hint : -1)) {
                if (need_exit() || std::chrono::steady_clock::now() >= deadline) {
                    break;
                }
                const auto slot = slot_at(index, rows, details_open);
                if (!ctrler()->click(slot) || !sleep(100)) {
                    return false;
                }
                ++requested_probes;
                auto panel_image = ctrler()->get_image();
                OCRer panel(panel_image, Rect(970, 320, 310, 400));
                bool identity = false, unavailable = false, occupied = false;
                bool confirmation = false;
                std::optional<StrategyIdentity> observed_identity;
                if (const auto details = panel.analyze()) {
                    for (const auto& text : *details) {
                        const auto value = compact(text.text);
                        if (text.score < 0.8) {
                            continue;
                        }
                        details_open = details_open || value.find("确认选择") != std::string::npos;
                        confirmation = confirmation || value.find("确认选择") != std::string::npos;
                        unavailable = unavailable || strategy_unavailable_label(value);
                        occupied = occupied || (text.score >= .9 && value == "已被选择");
                        for (const auto& known : identities) {
                            if (value == known.name || value == known.leader) {
                                observed_identity = known;
                                identity = known.band_id == wanted->band_id;
                                break;
                            }
                        }
                    }
                }
                const auto selected_slot = slot_at(index, rows, details_open);
                bool highlighted = identity ? selected_tile_matches(panel_image, index, slot)
                                            : opening_strategy_cell_selected(panel_image, selected_slot);
                bool enabled = highlighted && strategy_confirmation_enabled_on_frame(panel_image);
                const bool selection_reobserved = identity && confirmation && !unavailable && !enabled && sleep(180);
                if (selection_reobserved) {
                    // The title can become readable before the grid finishes sliding.
                    // Let the original selection settle before considering another card.
                    panel_image = ctrler()->get_image();
                    highlighted = selected_tile_matches(panel_image, index, slot);
                    enabled = highlighted && strategy_confirmation_enabled_on_frame(panel_image);
                }
                if (!highlighted) {
                    if (identity && confirmation) {
                        save_evidence(
                            panel_image,
                            utils::path("debug/SPA/strategy_requested_highlight_unverified"),
                            false);
                    }
                    report_occupied(observed_identity, occupied);
                    continue;
                }
                unavailable = unavailable || !enabled;
                report_occupied(observed_identity, occupied);
                if (page == 0 && observed_identity) {
                    first_page_observations[index] = { observed_identity->band_id, confirmation && !unavailable };
                }
                if (identity && confirmation && unavailable) {
                    requested_unavailable = true;
                    break;
                }
                const auto context = m_context_provider ? m_context_provider() : OpeningStrategyContext {};
                if (identity && observed_identity && confirmation && !unavailable &&
                    assess_opening_strategy(m_requested_strategy, context).eligible) {
                    specific_selected = true;
                    selected_strategy_index = index;
                    m_selected_strategy_index = index;
                    selected_card = selected_slot;
                    auto match = basic_info_with_what("SPAStrategyRequestedMatch");
                    match["details"] = json::object { { "requested_strategy", m_requested_strategy },
                                                      { "catalogue_hint", catalogue_hint },
                                                      { "viewport", page },
                                                      { "slot", index },
                                                      { "probed_cards", requested_probes },
                                                      { "countdown_seconds", countdown_seconds.value_or(-1) },
                                                      { "selection_reobserved", selection_reobserved },
                                                      { "confirmation_issued", false } };
                    callback(AsstMsg::SubTaskExtraInfo, match);
                    break;
                }
            }
            if (!specific_selected && !requested_unavailable && page < 6 &&
                std::chrono::steady_clock::now() < deadline && !need_exit()) {
                last_marker = viewport_marker(ctrler()->get_image());
                if (!scroll(true)) {
                    return false;
                }
                ++requested_scrolls;
            }
        }
        if (!specific_selected) {
            auto fallback = basic_info_with_what("SPAStrategyFallback");
            const auto unlock = strategy_unlock_rule(m_requested_strategy);
            fallback["details"] =
                json::object { { "requested_strategy", m_requested_strategy },
                               { "reason",
                                 requested_mode_available
                                     ? "Could not verify a selectable requested card within the opening search"
                                     : "Requested strategy is unavailable in this simulation mode" },
                               { "requested_mode", opening_context.mode },
                               { "probed_cards", requested_probes },
                               { "viewport_scrolls", requested_scrolls },
                               { "countdown_seconds", countdown_seconds.value_or(-1) },
                               { "explicitly_unavailable", requested_unavailable },
                               { "search_time_exhausted", std::chrono::steady_clock::now() >= deadline },
                               { "unlock_requirement", unlock ? std::string(unlock->description) : std::string() },
                               { "fallback", "default" },
                               { "badge_priority", false },
                               { "run_abandoned", false } };
            callback(AsstMsg::SubTaskExtraInfo, fallback);
            m_requested_strategy.clear();
            m_prefer_unclaimed = false;
            // Return to the top before the default chooser inspects portraits.
            for (int page = 0; page < requested_scrolls; ++page) {
                if (!scroll(false)) {
                    return false;
                }
            }
            image = ctrler()->get_image();
            OCRer current(image, Rect(400, 0, 880, 720));
            recognized = current.analyze();
            if (!recognized) {
                return false;
            }
            selected_strategy_index = -1;
        }
    }
    bool selected_badge_known = false;
    bool selected_completed = false;
    if (selected_strategy_index < 0) {
        // Card colour is visibility evidence, not tactical value. Read each
        // inspected card's actual identity and availability before comparing
        // current-season effects against the run's pact bans.
        auto& templates = TemplResource::get_instance();
        const auto& orange_badge = templates.get_templ("MiniGame@SPA@StrategyCompletedMark.png");
        const auto& silver_badge = templates.get_templ("MiniGame@SPA@StrategySilverMark.png");
        const auto identities = strategy_identities();
        const auto inspection_started = std::chrono::steady_clock::now();
        const auto deadline = inspection_deadline;
        std::optional<StrategyIdentity> initially_highlighted;
        bool initial_identity_conflict = false;
        bool initial_unavailable = false;
        for (const auto& text : *recognized) {
            if (text.score < .85 || text.rect.x < 970) {
                continue;
            }
            const auto value = compact(text.text);
            initial_unavailable = initial_unavailable || strategy_unavailable_label(value);
            if (text.rect.y < 345 || text.rect.y >= 455) {
                continue;
            }
            for (const auto& identity : identities) {
                if (value == identity.name || value == identity.leader) {
                    initial_identity_conflict =
                        initial_identity_conflict ||
                        (initially_highlighted && initially_highlighted->band_id != identity.band_id);
                    initially_highlighted = identity;
                }
            }
        }
        const bool use_initial_highlight = details_open && initially_highlighted && !initial_identity_conflict &&
                                           !initial_unavailable && strategy_confirmation_enabled_on_frame(image) &&
                                           first_page_observations.empty();
        std::vector<OpeningStrategyCard> candidates;
        std::map<int, std::string> identified;
        std::vector<std::vector<int>> viewport_rows;
        json::array card_evidence;
        cv::Mat last_marker;
        for (int page = 0; page < 4 && !need_exit(); ++page) {
            image = ctrler()->get_image();
            const auto marker = viewport_marker(image);
            if (!last_marker.empty() && cv::norm(marker, last_marker, cv::NORM_L1) / (marker.total() * 3) < 3) {
                break;
            }
            auto rows = opening_strategy_rows(image, edge, details_open);
            if (page > 0 && rows.empty()) {
                break;
            }
            if (rows.size() > 4) {
                rows.resize(4);
            }
            viewport_rows.push_back(rows);
            const int slots = rows.empty() ? 16 : static_cast<int>(rows.size()) * 4;
            int initially_selected = -1;
            std::vector<OpeningStrategyCard> visible;
            for (int index = 0; index < slots; ++index) {
                const auto slot = slot_at(index, rows, details_open);
                if (page == 0 && use_initial_highlight && opening_strategy_cell_selected(image, slot)) {
                    initially_selected = initially_selected == -1 ? index : -2;
                }
                const auto has_badge = [&](const cv::Mat& badge_template) {
                    if (badge_template.empty()) {
                        return false;
                    }
                    Matcher badge(image, Rect(slot.x + 85, slot.y, 40, 45));
                    badge.set_templ(badge_template);
                    badge.set_threshold(.80);
                    return badge.analyze().has_value();
                };
                const bool completed = has_badge(orange_badge) || has_badge(silver_badge);
                visible.push_back(
                    { index, false, completed || (!orange_badge.empty() && !silver_badge.empty()), completed, 0 });
            }
            std::stable_sort(visible.begin(), visible.end(), [&](const auto& a, const auto& b) {
                return std::pair(a.index == initially_selected, m_prefer_unclaimed && a.badge_known && !a.completed) >
                       std::pair(b.index == initially_selected, m_prefer_unclaimed && b.badge_known && !b.completed);
            });
            // Reserve an inspection budget per viewport so a slow first page does not consume the entire search.
            const auto page_deadline = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(7));
            for (auto& candidate : visible) {
                const bool reuse_highlight = page == 0 && candidate.index == initially_selected;
                if (need_exit() || (!reuse_highlight && std::chrono::steady_clock::now() >= page_deadline)) {
                    break;
                }
                std::string id;
                bool selectable = false;
                const auto cached = first_page_observations.find(candidate.index);
                if (reuse_highlight) {
                    id = initially_highlighted->band_id;
                    selectable = true;
                }
                else if (page == 0 && cached != first_page_observations.end()) {
                    std::tie(id, selectable) = cached->second;
                }
                else {
                    const auto slot = slot_at(candidate.index, rows, details_open);
                    if (!ctrler()->click(slot) || !sleep(100)) {
                        return false;
                    }
                    ++inspected_strategy_cards;
                    const auto panel_image = ctrler()->get_image();
                    OCRer panel(panel_image, Rect(970, 320, 310, 400));
                    std::optional<StrategyIdentity> identity;
                    bool unavailable = false, confirmation = false, occupied = false;
                    if (const auto texts = panel.analyze()) {
                        for (const auto& text : *texts) {
                            if (text.score < .85) {
                                continue;
                            }
                            const auto value = compact(text.text);
                            confirmation = confirmation || value.find("确认选择") != std::string::npos;
                            unavailable = unavailable || strategy_unavailable_label(value);
                            occupied = occupied || (text.score >= .9 && value == "已被选择");
                            for (const auto& known : identities) {
                                if (value == known.name || value == known.leader) {
                                    identity = known;
                                    break;
                                }
                            }
                        }
                    }
                    details_open = details_open || confirmation || identity.has_value();
                    if (!identity ||
                        !opening_strategy_cell_selected(panel_image, slot_at(candidate.index, rows, details_open))) {
                        continue;
                    }
                    report_occupied(identity, occupied);
                    id = identity->band_id;
                    selectable = confirmation && !unavailable && strategy_confirmation_enabled_on_frame(panel_image);
                }
                const auto context = m_context_provider ? m_context_provider() : OpeningStrategyContext {};
                const auto assessment = assess_opening_strategy(id, context);
                candidate.available = selectable && assessment.eligible;
                candidate.support_score = assessment.score;
                candidate.index += 16 * page;
                identified.emplace(candidate.index, id);
                candidates.push_back(candidate);
                card_evidence.emplace_back(
                    json::object { { "strategy_index", candidate.index % 16 },
                                   { "viewport", page },
                                   { "strategy_id", id },
                                   { "identity_known", true },
                                   { "available", candidate.available },
                                   { "badge_known", candidate.badge_known },
                                   { "completed", candidate.completed },
                                   { "support_score", candidate.support_score },
                                   { "reason", assessment.reason } });
            }
            if (page == 3 || std::chrono::steady_clock::now() + std::chrono::seconds(5) >= deadline) {
                break;
            }
            last_marker = viewport_marker(ctrler()->get_image());
            if (!scroll(true)) {
                return false;
            }
        }
        // Later inspected occupied cards may change earlier candidates' route
        // pressure. Re-score all known identities without additional UI input.
        const auto final_context = m_context_provider ? m_context_provider() : OpeningStrategyContext {};
        for (auto& candidate : candidates) {
            if (const auto found = identified.find(candidate.index); found != identified.end()) {
                candidate.support_score = assess_opening_strategy(found->second, final_context).score;
            }
        }
        selected_strategy_index = choose_opening_strategy_card(candidates, m_prefer_unclaimed);
        if (selected_strategy_index < 0) {
            LogError << "SPA auto strategy mode found no card candidate";
            return false;
        }
        expected_default_identity = identified.at(selected_strategy_index);
        m_auto_selected = true;
        const auto& selected = *std::find_if(candidates.begin(), candidates.end(), [&](const auto& candidate) {
            return candidate.index == selected_strategy_index;
        });
        const int selected_viewport = selected.index / 16;
        selected_strategy_index = selected.index % 16;
        m_selected_strategy_index = selected_strategy_index;
        // Reset to a known top before replaying the same list gesture. Validate the row and the
        // selected identity again because the client can move the grid when a detail panel opens.
        const int last_viewport = static_cast<int>(viewport_rows.size()) - 1;
        const auto current_rows = opening_strategy_rows(ctrler()->get_image(), edge, details_open);
        if (selected_viewport != last_viewport || current_rows != viewport_rows.at(selected_viewport)) {
            for (int reset = 0; reset <= last_viewport; ++reset) {
                if (!scroll(false)) {
                    return false;
                }
            }
            for (int page = 0; page < selected_viewport; ++page) {
                if (!scroll(true)) {
                    return false;
                }
            }
        }
        const auto rows = opening_strategy_rows(ctrler()->get_image(), edge, details_open);
        if ((selected_viewport > 0 && rows.empty()) ||
            (!rows.empty() && selected_strategy_index / 4 >= static_cast<int>(rows.size()))) {
            return false;
        }
        selected_card = slot_at(selected_strategy_index, rows, details_open);
        selected_badge_known = selected.badge_known;
        selected_completed = selected.completed;
        auto preference = basic_info_with_what("SPAStrategyCardPriority");
        preference["details"] =
            json::object { { "prefer_unclaimed_strategy", m_prefer_unclaimed },
                           { "strategy_index", selected_strategy_index },
                           { "badge_known", selected_badge_known },
                           { "completed", selected_completed },
                           { "unclaimed_priority", m_prefer_unclaimed && selected_badge_known && !selected_completed },
                           { "selection_basis", "verified_identity_effects_and_pact_bans" },
                           { "inspected_cards", inspected_strategy_cards },
                           { "inspection_ms",
                             std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - inspection_started)
                                 .count() },
                           { "countdown_seconds_at_opening", countdown_seconds.value_or(-1) },
                           { "identified_cards", identified.size() },
                           { "reused_requested_search_cards", first_page_observations.size() },
                           { "identified_slots", candidates.size() },
                           { "scanned_viewports", viewport_rows.size() },
                           { "selected_viewport", selected_viewport },
                           { "pact_availability_known", final_context.pact_availability_known },
                           { "occupied_strategy_ids", final_context.occupied_strategy_ids },
                           { "route_contention_inferred", true },
                           { "all_catalogue_pages_scanned", false },
                           { "candidates", std::move(card_evidence) } };
        callback(AsstMsg::SubTaskExtraInfo, preference);
    }
    const auto [x, y, width, height] = opening_strategy_card_rect(selected_strategy_index, details_open);
    const Rect card = selected_card.value_or(Rect(x, y, width, height));
    // The selection click is synchronous on the client. Polling the next
    // frame below is enough; a full one-second delay made this stage feel
    // frozen on fast emulators and multiplied when ProcessTask retried.
    if (!specific_selected && (!ctrler()->click(card) || !sleep(180))) {
        return false;
    }
    // Selecting a strategy card only highlights it.  The game requires the
    // separate bottom-right confirmation before it advances to formation.
    const auto selected_image = ctrler()->get_image();
    save_evidence(selected_image, utils::path("debug/SPA/strategy_selected"), false);
    // Read the right-hand detail panel and its confirmation together. The
    // observed leader/title identifies the strategy without assuming that a
    // grid slot remains the same strategy after scrolling or account changes.
    OCRer selected_ocr(selected_image, Rect(900, 100, 380, 620));
    const auto selected_text = selected_ocr.analyze();
    if (!selected_text) {
        return false;
    }
    std::optional<Rect> confirm_rect;
    std::optional<StrategyIdentity> selected_identity;
    bool selected_unavailable = false;
    bool selected_identity_conflict = false;
    const auto identities = strategy_identities();
    for (const auto& text : *selected_text) {
        const auto value = compact(text.text);
        selected_unavailable = selected_unavailable || (text.rect.x >= 970 && strategy_unavailable_label(value));
        if (text.score >= 0.85 && text.rect.x >= 970 && text.rect.y >= 345 && text.rect.y < 455) {
            for (const auto& identity : identities) {
                if (value == identity.name || value == identity.leader) {
                    selected_identity_conflict = selected_identity_conflict ||
                                                 (selected_identity && selected_identity->band_id != identity.band_id);
                    if (!selected_identity) {
                        selected_identity = identity;
                    }
                    break;
                }
            }
        }
        if (text.score >= 0.8 && (value.find("确认选择") != std::string::npos || value == "确认")) {
            confirm_rect = text.rect;
        }
    }
    const auto& expected_identity = specific_selected ? m_requested_strategy : expected_default_identity;
    const bool selected_tile_verified =
        !expected_identity.empty() && selected_tile_matches(selected_image, selected_strategy_index, card);
    if (selected_identity_conflict ||
        (!expected_identity.empty() &&
         (!selected_identity || selected_identity->band_id != expected_identity || !selected_tile_verified))) {
        LogError << "SPA strategy confirmation did not match the planned slot and identity" << expected_identity
                 << (selected_identity ? selected_identity->band_id : "") << card << selected_tile_verified;
        return false;
    }
    if (!m_requested_strategy.empty() && (!selected_identity || selected_identity->band_id != m_requested_strategy)) {
        LogError << "SPA requested strategy identity changed before confirmation" << m_requested_strategy;
        return false;
    }
    if (selected_unavailable || !confirm_rect || !strategy_confirmation_enabled_on_frame(selected_image)) {
        m_requested_strategy.clear();
        LogWarn << "SPA strategy confirmation is unavailable; retry with the current default pool";
        return false;
    }
    if (!ctrler()->click(*confirm_rect) || !sleep(300)) {
        LogError << "SPA strategy card selection did not expose a confirmation button";
        return false;
    }
    bool stage_three = false;
    bool joint_waiting = false;
    const bool joint_mode = m_context_provider && m_context_provider().mode == "joint";
    for (int frame = 0; frame < 3 && !need_exit(); ++frame) {
        const auto after = ctrler()->get_image();
        OCRer confirm(after, Rect(400, 0, 520, 120));
        if (const auto confirmed = confirm.analyze()) {
            if (transition_state(*confirmed)) {
                return false;
            }
            stage_three = std::any_of(confirmed->begin(), confirmed->end(), [](const auto& text) {
                const auto value = compact(text.text);
                return text.score >= .8 &&
                       (value.find("3/3") != std::string::npos || value.find("自选编队") != std::string::npos ||
                        value.find("确认编队") != std::string::npos);
            });
        }
        if (!stage_three && joint_mode && selected_identity) {
            OCRer waiting(after, Rect(640, 120, 640, 600));
            if (const auto waiting_text = waiting.analyze()) {
                bool committed = false, same_leader = false;
                for (const auto& text : *waiting_text) {
                    if (text.score >= 0.85) {
                        const auto value = compact(text.text);
                        committed = committed || value == "已选定策略";
                        same_leader = same_leader || value == selected_identity->leader;
                    }
                }
                joint_waiting = committed && same_leader;
            }
        }
        if (stage_three || joint_waiting || (frame < 2 && !sleep(220))) {
            break;
        }
    }
    if (!stage_three && !joint_waiting) {
        LogError << "SPA strategy card click was not followed by a verified 3/3 page";
        return false;
    }
    auto info = basic_info_with_what("SPAStrategyCardSelected");
    info["details"] = json::object { { "strategy_index", m_selected_strategy_index },
                                     { "stage_two_verified", true },
                                     { "stage_three_verified", stage_three },
                                     { "joint_selection_committed", joint_waiting },
                                     { "auto_selected", m_auto_selected },
                                     { "prefer_unclaimed_strategy", m_prefer_unclaimed },
                                     { "badge_known", selected_badge_known },
                                     { "completed", selected_completed } };
    if (!expected_default_identity.empty()) {
        info["details"]["selection_basis"] = "verified_identity_effects_and_pact_bans";
        info["details"]["inspected_cards"] = inspected_strategy_cards;
        info["details"]["all_catalogue_pages_scanned"] = false;
    }
    if (selected_identity) {
        info["details"]["strategy_id"] = selected_identity->band_id;
        info["details"]["strategy_name"] = selected_identity->name;
        info["details"]["strategy_leader"] = selected_identity->leader;
    }
    callback(AsstMsg::SubTaskExtraInfo, info);
    if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
        process->override_next(
            "MiniGame@SPA@SelectStrategy",
            { joint_waiting ? "MiniGame@SPA@LifecycleDispatch" : "MiniGame@SPA@SelectFormation" });
    }
    return true;
}

bool asst::spa::StrategyCardTaskPlugin::on_run_fails()
{
    if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
        process->override_next(
            "MiniGame@SPA@SelectStrategy",
            m_recovery_task.empty() ? std::vector<std::string> {} : std::vector<std::string> { m_recovery_task });
    }
    return false;
}
