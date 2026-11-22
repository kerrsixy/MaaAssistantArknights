#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace asst::spa
{
struct LifecyclePage
{
    std::string page;
    std::string setup_stage;
    bool result_verified = false;
    bool result_recorded = false;
    bool reconnect_available = false;
    bool personal_selected = false;
    std::string mode = "independent";
    bool joint_selected = false;
    bool quick_start_available = false;
    bool joint_setup_committed = false;
    bool loading_verified = false;
    bool battle_seen = false;
    bool issued_start_required = false;
};

inline bool lifecycle_login_start_game_enabled(std::string_view mode)
{
    // Joint recovery must preserve the running client. StartUpTask still
    // performs its UI login navigation when process startup is disabled.
    return mode != "joint";
}

// A loading screen or a verified formation commit can be followed by
// unclassified frames before the preparation HUD appears.
class LifecycleLoadingGrace
{
public:
    using Clock = std::chrono::steady_clock;

    void note_formation_ready(Clock::time_point now = Clock::now())
    {
        if (!m_formation_ready) {
            m_formation_ready = now;
        }
    }

    bool formation_pending() const { return m_formation_ready.has_value(); }

    bool formation_wait_expired(Clock::time_point now) const
    {
        return m_formation_ready && now - *m_formation_ready >= std::chrono::seconds(45);
    }

    bool should_wait(const LifecyclePage& state, Clock::time_point now = Clock::now())
    {
        if (state.loading_verified) {
            m_last_loading = now;
            return true;
        }
        if (state.page != "unknown" || !state.setup_stage.empty()) {
            m_last_loading.reset();
            if (state.setup_stage != "3/3") {
                m_formation_ready.reset();
            }
            return false;
        }
        if (m_formation_ready && now >= *m_formation_ready && !formation_wait_expired(now)) {
            return true;
        }
        return m_last_loading && now >= *m_last_loading && now - *m_last_loading < std::chrono::seconds(15);
    }

private:
    std::optional<Clock::time_point> m_last_loading;
    std::optional<Clock::time_point> m_formation_ready;
};

// The session executor owns battle and transaction progress. Navigation must
// change its observed state instead of clicking the same control indefinitely.
class LifecycleNavigationGuard
{
public:
    bool allow(const LifecyclePage& state, std::string_view next)
    {
        if (next == "MiniGame@SPA@LifecycleExecute" || next == "MiniGame@SPA@LifecycleWait" ||
            next == "MiniGame@SPA@LifecycleComplete") {
            m_key.clear();
            m_attempts = 0;
            return true;
        }
        const auto key = state.page + ":" + state.setup_stage + ":" + state.mode + ":" + std::string(next) +
                         (state.personal_selected ? ":personal" : "") + (state.joint_selected ? ":joint" : "") +
                         (state.reconnect_available ? ":reconnect" : "");
        if (key != m_key) {
            m_key = key;
            m_attempts = 0;
        }
        return ++m_attempts <= 2;
    }

private:
    std::string m_key;
    int m_attempts = 0;
};

// Return resource task names rather than executing navigation in the planner.
// An empty name requests recovery without input; it must not start a new run.
inline std::string lifecycle_next(const LifecyclePage& state)
{
    if (state.loading_verified) {
        return "MiniGame@SPA@LifecycleWait";
    }
    if (state.page == "data_updated") {
        return "MiniGame@SPA@LifecycleAckSync";
    }
    if (state.page == "authentication_expired") {
        return "MiniGame@SPA@LifecycleLogin";
    }
    if (state.issued_start_required &&
        (state.reconnect_available || !state.setup_stage.empty() || state.page == "run_setup" ||
         state.page == "preparation" || state.page == "battle" || state.page == "placement" ||
         state.page == "bounty_selection" || state.page == "equipment_supply" || state.page == "tactical_decision" ||
         state.page == "ready_confirmation" || state.page == "result_summary" || state.page == "result")) {
        return {};
    }
    if (state.page == "result_summary") {
        return "MiniGame@SPA@ResultSummaryNext";
    }
    if (state.page == "result") {
        return state.result_verified ? "MiniGame@SPA@ReturnResultHome" : "MiniGame@SPA@LifecycleWait";
    }
    if (state.page == "lobby") {
        if (state.result_recorded) {
            return "MiniGame@SPA@LifecycleComplete";
        }
        if (state.battle_seen && !state.reconnect_available) {
            // Returning to the lobby does not prove that the observed game
            // settled. Let the caller stop and reconcile the missing result.
            return {};
        }
        if (state.mode == "joint") {
            // An existing match is authoritative even when the selected-tab
            // label is clipped. Do not toggle modes before reconnecting it.
            if (state.reconnect_available) {
                return "MiniGame@SPA@ReconnectCurrent";
            }
            if (!state.joint_selected) {
                return "MiniGame@SPA@LifecycleSelectJoint";
            }
            return "MiniGame@SPA@LifecycleOpenJoint";
        }
        if (!state.personal_selected) {
            return "MiniGame@SPA@LifecycleSelectPersonal";
        }
        if (state.reconnect_available) {
            return "MiniGame@SPA@ReconnectSolo";
        }
        return state.mode == "quick" ? "MiniGame@SPA@LifecycleQuickStart" : "MiniGame@SPA@OpenIndependentStart";
    }
    if (state.page == "difficulty") {
        if (state.mode == "quick") {
            return state.quick_start_available ? "MiniGame@SPA@LifecycleQuickConfirm"
                                               : "MiniGame@SPA@LifecycleDifficultyBack";
        }
        return "MiniGame@SPA@SelectDifficulty";
    }
    if (state.joint_setup_committed &&
        (state.setup_stage == "3/3" || (state.mode == "joint" && state.setup_stage == "2/3"))) {
        return "MiniGame@SPA@LifecycleWait";
    }
    if (state.setup_stage == "2/3") {
        return "MiniGame@SPA@SelectStrategy";
    }
    if (state.setup_stage == "3/3") {
        return "MiniGame@SPA@SelectFormation";
    }
    if (state.page == "run_setup" || state.setup_stage == "1/3") {
        return "MiniGame@SPA@SetupPacts";
    }
    if (state.page == "preparation" || state.page == "battle" || state.page == "placement" ||
        state.page == "bounty_selection" || state.page == "equipment_supply" || state.page == "tactical_decision" ||
        state.page == "ready_confirmation" || state.page == "spectating" || state.page == "ready_waiting") {
        return "MiniGame@SPA@LifecycleExecute";
    }
    if (state.page == "strategy_report") {
        return "MiniGame@SPA@ReportBack";
    }
    if (state.mode == "quick" && state.quick_start_available) {
        return "MiniGame@SPA@LifecycleQuickConfirm";
    }
    // Each recovery candidate verifies its own navigation surface. Never use
    // a generic Back/abandon action on an unrecognized battle frame.
    return "MiniGame@SPA@LifecycleFindLobby";
}
}
