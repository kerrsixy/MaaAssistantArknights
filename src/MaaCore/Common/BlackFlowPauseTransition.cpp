#include "BlackFlowPauseTransition.h"

#include <utility>

namespace asst::blackflow
{
void BlackFlowPauseTransition::begin_battle(std::string key)
{
    // Caller must consume the rejection before beginning another battle.
    reject("battle_reset");
    m_battle_key = std::move(key);
    m_banner.reset();
    m_waiting_successor = false;
}

void BlackFlowPauseTransition::observe(const PauseTransitionFrame& frame)
{
    if (pending()) {
        if (frame.captured_ms <= m_pause->captured_ms || frame.received_ms < frame.captured_ms ||
            frame.received_ms - m_pause->captured_ms > SuccessorMaxAgeMs) {
            reject("pending_expired_or_clock_unknown");
        }
        else if (frame.strong_running) {
            reject("strong_running_returned");
        }
        else if (frame.hud_known && frame.total != m_pause->total) {
            reject("battle_counter_identity_changed");
        }
        else if (frame.hud_known && frame.kills < m_pause->total) {
            reject("battle_counter_regressed_before_result_ack");
        }
    }
    if (!pending() && frame.strong_running) {
        m_banner.reset();
    }
    if (!pending() && frame.banner && frame.hud_known && frame.total > 0 && frame.kills == frame.total &&
        !frame.banner_file.empty() && frame.captured_ms > 0 && frame.received_ms >= frame.captured_ms &&
        frame.received_ms - frame.captured_ms <= BannerMaxAgeMs &&
        (!m_banner || frame.captured_ms > m_banner->captured_ms)) {
        m_banner = frame;
    }
}

bool BlackFlowPauseTransition::begin_pause(const PauseTransitionFrame& frame, json::value details)
{
    if (pending()) {
        return true;
    }
    details["pause_observation_id"] = m_battle_key + ":pause" + std::to_string(++m_sequence);
    details["captured_ms"] = frame.captured_ms;
    details["received_ms"] = frame.received_ms;
    details["current_hud_known"] = frame.hud_known;
    details["selected_ui_owned"] = frame.selected_ui_owned;
    details["current_kills"] = frame.kills;
    details["current_total"] = frame.total;
    details["first_ending_clear_proven"] = false;
    const bool proof = frame.paused && !frame.selected_ui_owned && frame.hud_known && frame.total > 0 &&
                       frame.kills == frame.total && m_banner && frame.total == m_banner->total &&
                       frame.captured_ms > m_banner->captured_ms && frame.received_ms >= frame.captured_ms &&
                       frame.received_ms - m_banner->captured_ms <= BannerMaxAgeMs;
    if (!proof) {
        m_decision = PauseTransitionDecision { false,
                                               "no_recent_same_battle_banner_and_current_complete_hud",
                                               std::move(details) };
        return false;
    }
    details["banner_frame"] = m_banner->banner_file;
    details["banner_captured_ms"] = m_banner->captured_ms;
    details["classification_deadline_ms"] = frame.captured_ms + SuccessorMaxAgeMs;
    details["classification_pending"] = true;
    m_pause = Pending { frame.captured_ms, frame.total, std::move(details) };
    m_waiting_successor = false;
    return true;
}

void BlackFlowPauseTransition::reject(std::string reason)
{
    if (m_pause) {
        m_decision = PauseTransitionDecision { false, std::move(reason), std::move(m_pause->details) };
        m_pause.reset();
    }
    m_waiting_successor = false;
    m_banner.reset();
}

bool BlackFlowPauseTransition::observe_successor(
    std::string_view task,
    bool started,
    bool failed,
    bool positive_match,
    std::int64_t now_ms)
{
    if (!pending() || !m_waiting_successor) {
        return false;
    }
    if (failed || now_ms < m_pause->captured_ms || now_ms - m_pause->captured_ms > SuccessorMaxAgeMs) {
        reject(failed ? "successor_error_or_stop" : "successor_timeout");
        return true;
    }
    if (!started) {
        return false;
    }
    if ((task == "BlackFlow@Roguelike@MissionCompletedFlag" || task == "BlackFlow@Roguelike@MissionFailedFlag" ||
         task == "BlackFlow@Roguelike@MissionFailedFlag2") &&
        positive_match) {
        auto details = std::move(m_pause->details);
        details["positive_successor_task"] = std::string(task);
        details["successor_received_ms"] = now_ms;
        m_decision = PauseTransitionDecision { true,
                                               "local_banner_then_nonbattle_then_positive_result_ack",
                                               std::move(details) };
        m_pause.reset();
        m_banner.reset();
        m_waiting_successor = false;
        return true;
    }
    if (task == "BlackFlow@Roguelike@StartActionReCheck" || task.ends_with("@LoadingText") ||
        task.ends_with("@LoadingIcon")) {
        return false;
    }
    reject("unexpected_successor_before_result_ack");
    return true;
}

json::value BlackFlowPauseTransition::pending_details() const
{
    return m_pause ? m_pause->details : json::value(nullptr);
}

std::optional<PauseTransitionDecision> BlackFlowPauseTransition::take_decision()
{
    auto result = std::move(m_decision);
    m_decision.reset();
    return result;
}
}
