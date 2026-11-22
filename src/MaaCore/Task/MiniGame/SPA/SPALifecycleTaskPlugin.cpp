#include "SPALifecycleTaskPlugin.h"

#include <charconv>

#include "Controller/Controller.h"
#include "SPALifecycle.h"
#include "SPALifecycleFrame.h"
#include "SPAObservation.h"
#include "SPAObserverTaskPlugin.h"
#include "SPARunBatch.h"
#include "Task/Interface/StartUpTask.h"
#include "Vision/Matcher.h"

asst::spa::LifecycleTaskPlugin::LifecycleTaskPlugin(
    const AsstCallback& callback,
    Assistant* inst,
    std::string_view chain,
    std::function<bool()> execute,
    std::string client_type,
    std::string mode,
    std::optional<int> fresh_independent_difficulty) :
    AbstractTaskPlugin(callback, inst, chain),
    m_execute(std::move(execute)),
    m_client_type(std::move(client_type)),
    m_mode(std::move(mode)),
    m_fresh_independent_difficulty(fresh_independent_difficulty)
{
    set_retry_times(0);
}

bool asst::spa::LifecycleTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    if (msg != AsstMsg::SubTaskStart || details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }
    const auto task = details.get("details", "task", std::string());
    for (const auto* phase : { "LifecycleDispatch", "LifecycleExecute", "LifecycleLogin", "LifecycleComplete" }) {
        if (task == std::string("MiniGame@SPA@") + phase) {
            m_phase = phase;
            return true;
        }
    }
    return false;
}

bool asst::spa::LifecycleTaskPlugin::_run()
{
    auto* process = dynamic_cast<ProcessTask*>(m_task_ptr);
    if (!process) {
        return false;
    }
    auto stop_for_reconciliation = [&] {
        process->override_next("MiniGame@SPA@" + m_phase, {});
        auto receipt = basic_info_with_what("SPALifecycleRecoveryRequired");
        receipt["details"] = json::object { { "reason", "An issued input requires reconciliation before resuming" },
                                            { "event", m_reconciliation_event },
                                            { "resumable", true },
                                            { "run_abandoned", false } };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
    };
    if (!m_reconciliation_event.empty()) {
        stop_for_reconciliation();
        return true;
    }
    if (m_phase == "LifecycleExecute") {
        // The session executor owns its policy and keeps it across reconnects.
        // A transient native failure returns to the resource dispatcher.
        const bool executed = m_execute();
        auto receipt = basic_info_with_what("SPALifecycleSessionReturned");
        receipt["details"] = json::object { { "executor_returned", executed }, { "resumable", !need_exit() } };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        if (!m_reconciliation_event.empty()) {
            stop_for_reconciliation();
        }
        return !need_exit();
    }
    if (m_phase == "LifecycleLogin") {
        m_fresh_independent_hud_verified = false;
        StartUpTask startup(m_callback, m_inst);
        startup.set_task_id(m_task_id);
        const bool recovered =
            startup.set_params(
                json::object { { "client_type", m_client_type },
                               { "start_game_enabled", lifecycle_login_start_game_enabled(m_mode) } }) &&
            startup.run();
        auto receipt = basic_info_with_what("SPALifecycleRecovery");
        receipt["details"] = json::object { { "login_recovered", recovered }, { "run_abandoned", false } };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        return !need_exit();
    }
    if (m_phase == "LifecycleComplete") {
        auto receipt = basic_info_with_what("SPALifecycleCompleted");
        receipt["details"] = m_result.value_or(json::object {});
        receipt["details"]["return_home_verified"] = m_result.has_value();
        receipt["details"]["battle_seen"] = m_battle_seen;
        // Resuming at settlement proves cleanup, not the preceding battle.
        receipt["details"]["phase1_complete"] = m_result.has_value() && m_battle_seen;
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        return true;
    }

    ObserverTaskPlugin observer(m_callback, m_inst, m_task_chain);
    observer.set_task_id(m_task_id);
    observer.set_retry_times(0);
    bool blank_formation_frame = false;
    if (m_loading_grace.formation_pending()) {
        const auto frame = ctrler()->get_image();
        blank_formation_frame = is_blank_formation_frame(frame);
        if (!blank_formation_frame) {
            observer.observe_frame(frame);
        }
    }
    else {
        observer.run();
    }
    const auto& state = observer.last_observation();
    LifecyclePage page;
    page.mode = m_mode;
    page.page = state.get("page", std::string("unknown"));
    page.issued_start_required = m_fresh_independent_difficulty.has_value() && !m_fresh_independent_start_issued;
    if (page.page == "result_summary" && !state.get("contradictory", false) &&
        (state.get("personal_passed_rounds_known", false) || state.get("personal_result_interrupted", false)) &&
        !state.get("personal_result_identity", std::string()).empty()) {
        m_personal_result = state;
    }
    page.setup_stage = state.get("setup_stage", std::string());
    page.result_verified = !state.get("contradictory", true) && state.get("result_text_verified", false) &&
                           (state.get("result_completed", false) != state.get("result_interrupted", false));
    const bool mismatched_result = m_fresh_independent_difficulty && page.page == "result" &&
                                   state.get("result_difficulty_known", false) &&
                                   state.get("result_difficulty", -1) != *m_fresh_independent_difficulty;
    if (page.page == "result" && page.result_verified && !page.issued_start_required && !mismatched_result) {
        m_result = state;
        if (m_personal_result && state.get("result_joint", false) &&
            m_personal_result->get("personal_result_difficulty_name", std::string()) ==
                state.get("result_difficulty_name", std::string())) {
            for (const auto* key : { "personal_passed_rounds_known",
                                     "personal_passed_rounds",
                                     "personal_result_completed",
                                     "personal_result_interrupted",
                                     "personal_result_difficulty_name",
                                     "personal_result_identity" }) {
                (*m_result)[key] = m_personal_result->at(key);
            }
            (*m_result)["personal_result_source"] = "local_summary";
        }
    }
    page.result_recorded = m_result.has_value();
    m_battle_seen = m_battle_seen || page.page == "battle";
    page.battle_seen = m_battle_seen;
    bool different_independent_difficulty = mismatched_result;
    if (const auto texts = state.find<json::array>("texts")) {
        bool terminal_home = false, terminal_overview = false, protocol_entry = false;
        bool menu_home = false, menu_recruit = false, quick_selected = false;
        bool joint_setup_wait = false, formation_ready = false;
        std::vector<TextObservation> loading_texts;
        for (const auto& item : *texts) {
            if (item.get("score", 0.0) < 0.8) {
                continue;
            }
            const auto text = item.get("text", std::string());
            constexpr std::string_view independent_label = "规程等级";
            if (m_fresh_independent_difficulty && item.get("x", 0) >= 100 && item.get("x", 0) <= 250 &&
                item.get("y", 720) < 75 && text.starts_with(independent_label)) {
                const auto digits = std::string_view(text).substr(independent_label.size());
                int actual = 0;
                const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), actual);
                if (parsed.ec == std::errc {} && parsed.ptr == digits.data() + digits.size() && actual >= 1 &&
                    actual <= 20) {
                    const bool match = actual == *m_fresh_independent_difficulty;
                    m_fresh_independent_hud_verified = m_fresh_independent_hud_verified || match;
                    different_independent_difficulty = different_independent_difficulty || !match;
                }
            }
            loading_texts.push_back({ text, item.get("score", 0.0), item.get("x", -1), item.get("y", -1) });
            joint_setup_wait = joint_setup_wait || text == "已选定策略" || text == "取消准备";
            formation_ready = formation_ready || text == "取消准备";
            page.reconnect_available = page.reconnect_available || text == "断线重连";
            page.joint_selected = page.joint_selected || text == "搜寻队友" || text == "同盟模拟";
            page.personal_selected = page.personal_selected || text == "独立模拟" || text == "极速模拟";
            page.quick_start_available = page.quick_start_available || (text == "开始模拟" && item.get("y", 0) >= 550);
            quick_selected = quick_selected || (text == "极速模拟" && item.get("x", 0) >= 900 &&
                                                item.get("y", 0) >= 150 && item.get("y", 720) < 500);
            terminal_home = terminal_home || text == "Terminal";
            terminal_overview = terminal_overview || text == "前往上一次作战";
            protocol_entry = protocol_entry || text == "模拟开放中";
            menu_home = menu_home || text == "首页";
            menu_recruit = menu_recruit || text == "公开招募";
            // Setup headers can be present before the page classifier settles.
            for (const auto* stage : { "1/3", "2/3", "3/3" }) {
                if (text.find(stage) != std::string::npos && item.get("y", 720) < 120) {
                    page.setup_stage = stage;
                }
            }
        }
        page.quick_start_available = page.quick_start_available && quick_selected;
        page.loading_verified = page.page == "joint_team_waiting" || verified_loading_page(loading_texts);
        page.joint_strategy_own_turn_verified = m_mode == "joint" && joint_strategy_own_turn(loading_texts);
        // Solo setup can also retain its 3/3 header after ready. Never replay
        // formation selection while its cancel-ready control is visible.
        page.joint_setup_committed = m_mode == "joint" ? joint_setup_wait : formation_ready;
        if (page.joint_setup_committed &&
            (page.setup_stage == "3/3" || (m_mode == "joint" && page.setup_stage == "2/3"))) {
            if (m_mode != "joint") {
                m_loading_grace.note_formation_ready();
            }
            auto receipt = basic_info_with_what(m_mode == "joint" ? "SPAJointSetupWaiting" : "SPASoloSetupWaiting");
            receipt["details"] = json::object { { "stage", page.setup_stage },
                                                { "own_selection_committed", true },
                                                { "all_players_ready_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, receipt);
            // Keep the common bounded dispatcher below; a stuck ready panel
            // must not create an unbounded loop or cancel readiness.
        }
        if (page.page == "unknown") {
            if (menu_home && menu_recruit) {
                page.page = "navigation_menu";
            }
            else if (protocol_entry) {
                page.page = "protocol_entry";
            }
            else if (terminal_overview) {
                page.page = "terminal_overview";
            }
            else if (terminal_home) {
                page.page = "main_home";
            }
        }
    }
    if (page.page == "lobby") {
        m_fresh_independent_hud_verified = false;
        Matcher selected(ctrler()->get_image(), Rect(885, 520, 185, 80));
        selected.set_templ("MiniGame@SPA@PersonalSelected.png");
        selected.set_threshold(0.95);
        page.personal_selected = page.personal_selected || selected.analyze().has_value();
    }
    if (!page.setup_stage.empty() || page.page == "preparation" || page.page == "battle") {
        m_joint_search_started.reset();
    }
    // Matching has blank animation frames before its text appears. A verified
    // start grants a bounded no-input wait, never permission to navigate away.
    if (m_mode == "joint" && m_joint_search_started && page.page == "unknown" &&
        std::chrono::steady_clock::now() - *m_joint_search_started < std::chrono::seconds(180)) {
        page.loading_verified = true;
    }
    const bool observed_loading = page.loading_verified;
    const auto observed_at = LifecycleLoadingGrace::Clock::now();
    const bool loading_wait = m_loading_grace.should_wait(page, observed_at);
    const bool blank_formation_timeout =
        blank_formation_frame && !loading_wait && m_loading_grace.formation_wait_expired(observed_at);
    page.loading_verified = loading_wait;
    auto next = lifecycle_next(page);
    if (blank_formation_timeout || (m_fresh_independent_difficulty && different_independent_difficulty)) {
        next.clear();
    }
    else if (
        m_fresh_independent_difficulty && next == "MiniGame@SPA@LifecycleExecute" &&
        !m_fresh_independent_hud_verified) {
        next = "MiniGame@SPA@LifecycleWait";
    }
    if (next.empty()) {
        process->override_next("MiniGame@SPA@LifecycleDispatch", {});
        auto receipt = basic_info_with_what("SPALifecycleRecoveryRequired");
        receipt["details"] = json::object {
            { "page", page.page },
            { "reason",
              page.page == "joint_idle_removal" ? "Game displayed the idle removal prompt. No confirmation was issued."
              : blank_formation_timeout ? "Formation was committed but the client remained blank past the loading window"
              : different_independent_difficulty
                  ? "Observed a different independent difficulty than the verified requested start"
              : page.issued_start_required ? "An existing game appeared before this execution verified a fresh start"
                                           : "Observed battle returned to lobby without a verified settlement" },
            { "battle_seen", m_battle_seen },
            { "result_recorded", page.result_recorded },
            { "return_home_verified", false },
            { "new_run_started", false },
            { "input_issued", false },
            { "resumable", true },
            { "run_abandoned", false }
        };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        return true;
    }
    if (!m_navigation_guard.allow(page, next)) {
        process->override_next("MiniGame@SPA@LifecycleDispatch", {});
        auto receipt = basic_info_with_what("SPALifecycleRecoveryRequired");
        receipt["details"] =
            json::object { { "page", page.page },
                           { "next", next },
                           { "reason", "Navigation did not change the observed page after two attempts" },
                           { "repeated_input_suppressed", true },
                           { "resumable", true },
                           { "run_abandoned", false } };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        return true;
    }
    // A bounded navigation wait leaves the active game intact for resumption.
    if (++m_dispatches > 160 && next != "MiniGame@SPA@LifecycleComplete") {
        process->override_next("MiniGame@SPA@LifecycleDispatch", {});
        auto receipt = basic_info_with_what("SPALifecycleRecoveryRequired");
        receipt["details"] = json::object { { "page", page.page }, { "resumable", true }, { "run_abandoned", false } };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        return true;
    }
    process->override_next("MiniGame@SPA@LifecycleDispatch", { next });
    auto receipt = basic_info_with_what("SPALifecycleTransition");
    receipt["details"] = json::object { { "page", page.page },
                                        { "next", next },
                                        { "loading_observed", observed_loading },
                                        { "loading_grace_wait", loading_wait && !observed_loading },
                                        { "blank_formation_frame", blank_formation_frame },
                                        { "run_abandoned", false } };
    callback(AsstMsg::SubTaskExtraInfo, receipt);
    return true;
}

void asst::spa::LifecycleTaskPlugin::note_solo_result(const json::object& result)
{
    if (m_mode == "joint" || !round_settlement_can_be_retained(result)) {
        return;
    }
    const json::value receipt(result);
    if (m_fresh_independent_difficulty &&
        (!m_fresh_independent_start_issued || !receipt.get("result_difficulty_known", false) ||
         receipt.get("result_difficulty", -1) != *m_fresh_independent_difficulty)) {
        return;
    }
    if ((m_mode == "quick" && receipt.get("result_difficulty_name", std::string()) != "极速模拟") ||
        (m_mode == "independent" && !receipt.get("result_difficulty_known", false))) {
        return;
    }
    m_result = result;
}

void asst::spa::LifecycleTaskPlugin::observe_event(const json::value& details)
{
    const auto what = details.get("what", std::string());
    if (what == "SPAFormationSelected" && m_mode != "joint" &&
        details.get("details", "formation_page_verified", false) &&
        details.get("details", "readiness_committed", false)) {
        m_loading_grace.note_formation_ready();
    }
    if (what == "SPASimulationStartIssued" && details.get("details", "mode", std::string()) == "joint") {
        m_joint_search_started = std::chrono::steady_clock::now();
    }
    if (what == "SPASimulationStartIssued" && m_fresh_independent_difficulty &&
        details.get("details", "mode", std::string()) == "independent" &&
        details.get("details", "difficulty", -1) == *m_fresh_independent_difficulty) {
        m_fresh_independent_start_issued = true;
    }
    if (what == "SPAPurchaseConfirmationUnobserved" || what == "SPAPurchaseReconciliationRequired" ||
        what == "SPAChoiceConfirmationUnobserved" || what == "SPAWithdrawalReconciliationRequired" ||
        what == "SPADeploymentReconciliationRequired" || what == "SPAPromotionDeferred") {
        note_input_reconciliation(what);
    }
    if (what == "SPARoundStarted" || what == "SPABattleSample" ||
        (what == "SPARoundTransition" && details.get("details", "battle_seen", false))) {
        m_battle_seen = true;
    }
}
