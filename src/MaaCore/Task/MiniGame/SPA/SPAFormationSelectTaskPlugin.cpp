#include "SPAFormationSelectTaskPlugin.h"

#include "SPAFormation.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <unordered_set>

#include "Controller/Controller.h"
#include "Task/ProcessTask.h"
#include "Vision/OCRer.h"

namespace
{
std::string compact(std::string value)
{
    std::erase(value, ' ');
    std::erase(value, '\t');
    return value;
}
}

bool asst::spa::FormationSelectTaskPlugin::configure(std::vector<std::string> operators, std::string core_pact)
{
    if (operators.empty() || operators.size() > 8) {
        return false;
    }
    std::unordered_set<std::string> unique;
    for (const auto& name : operators) {
        if (name.empty() || name.size() > 64 || !unique.insert(name).second) {
            return false;
        }
    }
    if (core_pact.size() > 64) {
        return false;
    }
    m_operators = std::move(operators);
    m_core_pact = std::move(core_pact);
    m_requested_operators = m_operators;
    m_requested_core = m_core_pact;
    return true;
}

bool asst::spa::FormationSelectTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    return msg == AsstMsg::SubTaskStart && details.get("subtask", std::string()) == "ProcessTask" &&
           details.get("details", "task", std::string()) == "MiniGame@SPA@SelectFormation";
}

bool asst::spa::FormationSelectTaskPlugin::_run()
{
    // A transient OCR/confirmation failure must not leave an auto request
    // permanently empty after observing a valid zero-card opening hand.
    m_operators = m_requested_operators;
    m_core_pact = m_requested_core;
    if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
        process->set_task_delay(180);
    }
    if (m_operators.empty()) {
        return false;
    }
    const bool auto_formation = m_operators.size() == 1 && m_operators.front() == "auto";
    // Formation cards are rendered on the right side of the 3/3 page.  Keep
    // the expensive OCR passes on the header/card/confirm regions instead of
    // rescanning the map chrome and the left help panel.
    // The 5/6-tier empty-hand messages are centred around x~740, while real
    // selectable cards occupy the right side.  Include both in one cropped
    // pass; the map/help chrome to the left of x=600 is still excluded.
    const Rect setup_roi(600, 150, 620, 560);
    // The strategy page can transition asynchronously into the preparation HUD
    // before this explicit formation task gets its first frame.  Treat that as
    // an unverified setup handoff; never interpret a round-1 HUD as a confirmed
    // 3/3 formation or as faction activation evidence.
    {
        OCRer state_ocr(ctrler()->get_image(), auto_formation ? Rect(400, 0, 880, 180) : Rect(0, 0, 1280, 720));
        if (const auto state = state_ocr.analyze()) {
            bool preparation_hud = false;
            bool round_seen = false;
            bool remaining_seen = false;
            bool battle_target_page = false;
            for (const auto& text : *state) {
                const auto value = compact(text.text);
                preparation_hud = preparation_hud || value.find("准备就绪") != std::string::npos;
                round_seen = round_seen || value.find("回合") != std::string::npos;
                remaining_seen = remaining_seen || value.find("剩余可放置角色") != std::string::npos;
                battle_target_page = battle_target_page || value.find("全局作战目标") != std::string::npos ||
                                     value.find("消灭敌方领袖") != std::string::npos ||
                                     value.find("战斗目标") != std::string::npos;
            }
            if (battle_target_page) {
                auto blocked = basic_info_with_what("SPARunSetupBlocked");
                blocked["details"] =
                    json::object { { "reason", "Battle objective page appeared before verified 3/3 formation" },
                                   { "formation_page_verified", false },
                                   { "reopen_required", true },
                                   { "ready_clicked", false },
                                   { "battle_authorized", false },
                                   { "battle_started_unverified", true },
                                   { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, blocked);
                return false;
            }
            if (preparation_hud && round_seen && remaining_seen) {
                auto blocked = basic_info_with_what("SPARunSetupBlocked");
                blocked["details"] =
                    json::object { { "reason", "Formation task observed preparation HUD before 3/3 confirmation" },
                                   { "formation_page_verified", false },
                                   { "reopen_required", true },
                                   { "ready_clicked", false },
                                   { "battle_authorized", false },
                                   { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, blocked);
                return false;
            }
        }
    }

    // Auto mode prefers a formation that can already activate a faction, but
    // that is not a first-round prerequisite.  The game is meant to be
    // developed through later shop/level-up rounds, so a 3/3 page with three
    // real cards and no current faction activation must still be allowed to
    // start. It is recorded as a provisional formation; subsequent rounds
    // continue developing without an opening faction-activation requirement.
    int selection_score = 0;
    bool provisional_formation = false;
    bool empty_formation = false;
    bool activation_warning_seen = false;
    const auto choose_provisional = [&](const std::vector<std::string>& visible_names, const std::string& reason) {
        const auto catalog = operator_pacts();
        std::vector<std::string> candidates;
        std::unordered_set<std::string> seen;
        for (const auto& name : visible_names) {
            if (name.empty() || !seen.insert(name).second) {
                continue;
            }
            // The current-season catalogue is authoritative. A plausible
            // Chinese OCR fragment (for example the connecting overlay) is
            // not an operator and must never become a clickable roster item.
            if (!catalog.contains(name)) {
                continue;
            }
            candidates.emplace_back(name);
            if (candidates.size() == 3) {
                break;
            }
        }
        // The 3/3 page is an optional opening hand.  At high difficulty the
        // current ban set can leave only one (or even zero) usable cards in
        // this view.  That is a normal underdeveloped opening, not a
        // non-combat failure: keep the visible usable cards and let the shop
        // fill the roster during later rounds.
        if (candidates.empty()) {
            return false;
        }
        m_operators = std::move(candidates);
        // “auto” means this is a live preference only; do not claim the
        // provisional cards activated any faction.
        m_core_pact = "auto";
        provisional_formation = true;
        auto info = basic_info_with_what("SPARunSetupProvisional");
        json::array selected;
        for (const auto& name : m_operators) {
            selected.emplace_back(name);
        }
        info["details"] = json::object { { "reason", reason },
                                         { "operators", std::move(selected) },
                                         { "activation_verified", false },
                                         { "formation_page_verified", true },
                                         { "reopen_required", false },
                                         { "battle_authorized", false },
                                         { "run_victory_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        return true;
    };
    if (m_operators.size() == 1 && m_operators.front() == "auto") {
        OCRer candidate_ocr(ctrler()->get_image(), setup_roi);
        const auto recognized = candidate_ocr.analyze();
        if (!recognized) {
            return false;
        }
        std::vector<std::string> visible_names;
        bool empty_opening_hand = false;
        int none_placeholders = 0;
        for (const auto& text : *recognized) {
            if (text.score >= 0.86 && text.rect.x > 850 && text.rect.x < 1180 && text.rect.y > 200 &&
                text.rect.y < 670) {
                const auto value = compact(text.text);
                const bool ascii_only = !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
                    return ch < 0x80;
                });
                const bool ui_label =
                    value.find("阵容") != std::string::npos || value.find("盟约") != std::string::npos ||
                    value.find("准备") != std::string::npos || value.find("取消") != std::string::npos ||
                    value.find("确定") != std::string::npos || value.find("难以") != std::string::npos ||
                    value.find("禁用") != std::string::npos || value.find("本局") != std::string::npos ||
                    value.find("当前") != std::string::npos || value.find("干员") != std::string::npos ||
                    value.find("等级") != std::string::npos || value.find("目标") != std::string::npos ||
                    value.find("上限") != std::string::npos || value.find("金币") != std::string::npos;
                if (!ascii_only && !ui_label && operator_pacts().contains(value)) {
                    visible_names.emplace_back(value);
                }
                if (value == "NONE" || value == "NO") {
                    ++none_placeholders;
                }
            }
        }
        // On the empty opening-hand page the client renders two NONE slots
        // and the explicit “未选择自选编队” marker instead of the older
        // “尚不存在5/6阶自选编队” messages. Treat that page as a valid empty
        // hand so setup can commit once and continue to round one.
        empty_opening_hand =
            none_placeholders >= 2 && std::any_of(recognized->begin(), recognized->end(), [](const auto& text) {
                return text.score >= 0.8 && compact(text.text).find("未选择自选编队") != std::string::npos;
            });
        const auto selection =
            choose_formation_candidates(m_core_pact, visible_names, operator_pacts(), pact_requirements());
        if (!selection.valid) {
            bool no_self_formation_cards = false;
            bool missing_tier_five = false;
            bool missing_tier_six = false;
            for (const auto& text : *recognized) {
                const auto value = compact(text.text);
                missing_tier_five = missing_tier_five || value.find("尚不存在5阶自选编队") != std::string::npos;
                missing_tier_six = missing_tier_six || value.find("尚不存在6阶自选编队") != std::string::npos;
            }
            // The game-side tier messages are authoritative.  They are only
            // rendered when that tier has zero self-formation cards; player
            // names/profile text elsewhere on the page must not veto this
            // conclusion.
            no_self_formation_cards = (missing_tier_five && missing_tier_six) || empty_opening_hand;
            if (no_self_formation_cards) {
                // Some strategies expose no 5/6-tier self-selected cards at
                // all.  The 3/3 page still permits the ready click; this is
                // a valid empty opening hand, not a faction activation.
                m_operators.clear();
                m_core_pact = "auto";
                provisional_formation = true;
                empty_formation = true;
                auto info = basic_info_with_what("SPARunSetupProvisional");
                info["details"] = json::object {
                    { "reason", "No 5/6-tier self-formation cards are available; continue with an empty opening hand" },
                    { "operators", json::array {} },
                    { "activation_verified", false },
                    { "formation_page_verified", true },
                    { "reopen_required", false },
                    { "battle_authorized", false },
                    { "empty_formation", true },
                    { "run_victory_verified", false }
                };
                callback(AsstMsg::SubTaskExtraInfo, info);
            }
            else if (!choose_provisional(visible_names, selection.reason)) {
                auto blocked = basic_info_with_what("SPARunSetupBlocked");
                json::array visible;
                for (const auto& name : visible_names) {
                    visible.emplace_back(name);
                }
                blocked["details"] = json::object { { "reason", selection.reason },
                                                    { "requested_core_pact", m_core_pact },
                                                    { "selection_score", selection.score },
                                                    { "visible_names", std::move(visible) },
                                                    { "formation_page_verified", true },
                                                    { "reopen_required", true },
                                                    { "ready_clicked", false },
                                                    { "battle_authorized", false },
                                                    { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, blocked);
                return false;
            }
        }
        else {
            selection_score = selection.score;
            m_core_pact = selection.core_pact;
            m_operators = selection.operators;
        }
    }

    // If the caller supplied explicit names but left the core as auto (or
    // omitted it for the standalone formation helper), infer it from those
    // names with the same strict catalogue rule.  This keeps every entry path
    // subject to an activation check.
    if (!provisional_formation && (m_core_pact.empty() || m_core_pact == "auto")) {
        const auto selection =
            choose_formation_candidates(m_core_pact, m_operators, operator_pacts(), pact_requirements());
        if (!selection.valid) {
            if (!choose_provisional(m_operators, selection.reason)) {
                auto blocked = basic_info_with_what("SPARunSetupBlocked");
                blocked["details"] =
                    json::object { { "reason", selection.reason },      { "requested_core_pact", m_core_pact },
                                   { "formation_page_verified", true }, { "reopen_required", true },
                                   { "ready_clicked", false },          { "battle_authorized", false },
                                   { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, blocked);
                return false;
            }
        }
        else {
            selection_score = selection.score;
            m_core_pact = selection.core_pact;
            if (m_operators.size() == 1 && m_operators.front() == "auto") {
                m_operators = selection.operators;
            }
        }
    }

    // Explicit operator lists are also checked against the requested core
    // pact.  This prevents a stale/manual list from reaching the ready click
    // even when the game has not yet rendered its warning banner.
    if (!m_core_pact.empty() && m_core_pact != "auto") {
        std::vector<RosterUnit> roster;
        roster.reserve(m_operators.size());
        for (const auto& name : m_operators) {
            roster.push_back({ name, true, {}, std::nullopt });
        }
        const auto formation = evaluate_formation(roster, operator_pacts(), pact_requirements(), { m_core_pact });
        if (!formation.complete ||
            std::find(formation.active_pacts.begin(), formation.active_pacts.end(), m_core_pact) ==
                formation.active_pacts.end()) {
            auto blocked = basic_info_with_what("SPARunSetupBlocked");
            blocked["details"] =
                json::object { { "reason", "Configured formation does not activate the requested core pact" },
                               { "core_pact", m_core_pact },
                               { "formation_members", static_cast<int>(m_operators.size()) },
                               { "formation_page_verified", true },
                               { "reopen_required", true },
                               { "ready_clicked", false },
                               { "battle_authorized", false },
                               { "run_victory_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, blocked);
            return false;
        }
    }
    // Card selection does not reflow the 3/3 grid.  Resolve all requested
    // cards from one frame and then click the cached rectangles; reacquiring
    // full-screen OCR before every card was another avoidable multi-second
    // pause on the emulator.  The confirmation OCR below remains the commit
    // guard, so a stale frame cannot be reported as a successful formation.
    std::map<std::string, Rect> targets;
    if (!m_operators.empty()) {
        const auto selection_image = ctrler()->get_image();
        OCRer selection_ocr(selection_image, auto_formation ? setup_roi : Rect(0, 0, 1280, 720));
        const auto selection_text = selection_ocr.analyze();
        if (!selection_text) {
            return false;
        }
        for (const auto& wanted : m_operators) {
            std::optional<Rect> target;
            for (const auto& text : *selection_text) {
                if (compact(text.text) == wanted && text.score >= 0.86 && text.rect.x > 100 && text.rect.x < 1180 &&
                    text.rect.y > 100 && text.rect.y < 670) {
                    if (target) {
                        LogError << "SPA formation operator is not uniquely visible:" << wanted;
                        return false;
                    }
                    target = text.rect;
                }
            }
            if (!target) {
                LogError << "SPA formation operator is not uniquely visible:" << wanted;
                return false;
            }
            targets.emplace(wanted, *target);
        }
    }
    for (const auto& wanted : m_operators) {
        if (!ctrler()->click(targets.at(wanted)) || !sleep(120)) {
            LogError << "SPA formation operator click failed:" << wanted;
            return false;
        }
    }

    const auto image = ctrler()->get_image();
    OCRer confirm_ocr(image, auto_formation ? Rect(900, 540, 380, 180) : Rect(0, 0, 1280, 720));
    const auto confirmed = confirm_ocr.analyze();
    if (!confirmed) {
        return false;
    }
    std::optional<Rect> confirm;
    // Auto mode already verified the 3/3 header in the candidate pass.  The
    // confirmation pass is intentionally cropped to the lower-right button,
    // so do not require that header to be OCR-visible a second time.
    bool formation_page = auto_formation;
    bool faction_activation_warning = false;
    std::vector<std::string> warning_texts;
    for (const auto& text : *confirmed) {
        const auto value = compact(text.text);
        formation_page =
            formation_page || value.find("3/3") != std::string::npos || value.find("自选编队") != std::string::npos;
        if (value.find("难以在模拟中激活") != std::string::npos || value.find("盟约难以") != std::string::npos) {
            faction_activation_warning = true;
            warning_texts.emplace_back(value);
        }
        if ((value == "确认" || value == "确认自选编队" || value == "准备就绪") && text.score >= 0.86 &&
            text.rect.x > 850 && text.rect.y > 480) {
            if (confirm) {
                return false;
            }
            confirm = text.rect;
        }
    }
    if (faction_activation_warning) {
        activation_warning_seen = true;
        if (provisional_formation) {
            // This warning is expected for a mixed opening hand.  Keep the
            // warning as evidence, but continue to the normal first round.
        }
        else {
            auto blocked = basic_info_with_what("SPARunSetupBlocked");
            json::array warnings;
            for (const auto& value : warning_texts) {
                warnings.emplace_back(value);
            }
            blocked["details"] =
                json::object { { "reason", "Game warns that the selected formation cannot reliably activate a pact" },
                               { "core_pact", m_core_pact },
                               { "formation_page_verified", formation_page },
                               { "warnings", std::move(warnings) },
                               { "reopen_required", true },
                               { "ready_clicked", false },
                               { "battle_authorized", false },
                               { "run_victory_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, blocked);
            return false;
        }
    }
    if (!formation_page || !confirm || !ctrler()->click(*confirm) || !sleep(220)) {
        LogError << "SPA formation confirmation was not uniquely verified";
        return false;
    }
    if (empty_formation) {
        // There are no self-formation cards to select.  The ready click is
        // still the authoritative commit; the solo session may remain on the
        // 3/3 countdown for several seconds before the preparation HUD opens.
        // Do not replay the full formation OCR twenty times while waiting.
        auto info = basic_info_with_what("SPAFormationSelected");
        info["details"] = json::object { { "operators", json::array {} },  { "core_pact", "auto" },
                                         { "selection_score", 0 },         { "formation_page_verified", true },
                                         { "activation_verified", false }, { "provisional_formation", true },
                                         { "empty_formation", true },      { "ready_clicked", true },
                                         { "readiness_committed", true },  { "battle_authorized", false },
                                         { "run_victory_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
            process->override_next("MiniGame@SPA@SelectFormation", { "MiniGame@SPA@Observe" });
        }
        return true;
    }
    // Solo simulation starts asynchronously after the ready click.  The
    // screen remains on 3/3 with “取消准备” while the session is being
    // created; this transition is a valid committed setup state, but it is
    // not yet a battle HUD.  Wait for the actual preparation HUD before
    // authorizing round actions, with a bounded window long enough for slow
    // emulators.
    bool battle_authorized = false;
    bool readiness_committed = false;
    // The transition check only needs the top round marker and the right-side
    // ready/deployment labels.  Full-screen OCR here cost roughly 700-900ms
    // per probe, so a slow emulator could spend 20+ seconds waiting for a
    // page that had already advanced. Keep the bounded probe count, but crop
    // to the header and right HUD where all three markers live.
    for (int probe = 0; probe < 60 && !battle_authorized; ++probe) {
        OCRer battle_ocr(ctrler()->get_image(), Rect(400, 0, 850, 430));
        if (const auto battle_text = battle_ocr.analyze()) {
            bool has_ready = false;
            bool has_cancel_ready = false;
            bool has_round = false;
            bool has_remaining = false;
            for (const auto& text : *battle_text) {
                const auto value = compact(text.text);
                has_ready = has_ready || value.find("准备就绪") != std::string::npos;
                has_cancel_ready = has_cancel_ready || value.find("取消准备") != std::string::npos;
                has_round = has_round || value.find("回合") != std::string::npos;
                has_remaining = has_remaining || value.find("剩余可放置角色") != std::string::npos;
            }
            readiness_committed = readiness_committed || has_cancel_ready;
            battle_authorized = has_ready && has_round && has_remaining;
        }
        if (!battle_authorized && !sleep(180)) {
            return false;
        }
    }
    if (!battle_authorized) {
        LogError << "SPA formation entered a waiting state but battle HUD was not verified"
                 << " committed=" << readiness_committed;
        return false;
    }
    auto info = basic_info_with_what("SPAFormationSelected");
    json::array operators;
    for (const auto& name : m_operators) {
        operators.emplace_back(name);
    }
    info["details"] = json::object { { "operators", std::move(operators) },
                                     { "core_pact", m_core_pact },
                                     { "selection_score", selection_score },
                                     { "formation_page_verified", true },
                                     { "confirmation_clicked", true },
                                     { "readiness_committed", readiness_committed },
                                     { "battle_authorized", battle_authorized },
                                     { "activation_verified", !provisional_formation },
                                     { "provisional_formation", provisional_formation },
                                     { "activation_warning_seen", activation_warning_seen } };
    callback(AsstMsg::SubTaskExtraInfo, info);
    return true;
}

bool asst::spa::FormationSelectTaskPlugin::on_run_fails()
{
    if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
        process->override_next(
            "MiniGame@SPA@SelectFormation",
            m_recovery_task.empty() ? std::vector<std::string> {} : std::vector<std::string> { m_recovery_task });
    }
    return false;
}
