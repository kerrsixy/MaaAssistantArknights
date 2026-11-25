#pragma once

#include <cstdint>
#include <meojson/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace asst::blackflow
{
struct PauseTransitionFrame
{
    std::int64_t captured_ms = 0;
    std::int64_t received_ms = 0;
    bool paused = false;
    bool strong_running = false;
    bool selected_ui_owned = false;
    bool hud_known = false;
    int kills = -1;
    int total = -1;
    bool banner = false;
    std::string banner_file;
};

struct PauseTransitionDecision
{
    bool end_transition = false;
    std::string reason;
    json::value details;
};

// One battle-scoped proof and at most one pending observation; no controller,
// queue, clocks, callbacks or gameplay operations are owned by this policy.
class BlackFlowPauseTransition
{
public:
    void begin_battle(std::string key);
    void observe(const PauseTransitionFrame& frame);
    bool begin_pause(const PauseTransitionFrame& frame, json::value details);

    bool pending() const noexcept { return m_pause.has_value(); }

    bool has_banner() const noexcept { return m_banner.has_value(); }

    bool has_decision() const noexcept { return m_decision.has_value(); }

    json::value pending_details() const;

    void battle_loop_left() noexcept { m_waiting_successor = pending(); }

    void reject(std::string reason);
    bool observe_successor(std::string_view task, bool started, bool failed, bool positive_match, std::int64_t now_ms);
    std::optional<PauseTransitionDecision> take_decision();

private:
    static constexpr std::int64_t BannerMaxAgeMs = 2000;
    static constexpr std::int64_t SuccessorMaxAgeMs = 30'000;

    struct Pending
    {
        std::int64_t captured_ms = 0;
        int total = -1;
        json::value details;
    };

    std::string m_battle_key;
    std::optional<PauseTransitionFrame> m_banner;
    std::optional<Pending> m_pause;
    std::optional<PauseTransitionDecision> m_decision;
    bool m_waiting_successor = false;
    std::uint64_t m_sequence = 0;
};
}
