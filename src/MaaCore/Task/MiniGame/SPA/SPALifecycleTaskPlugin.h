#pragma once

#include <chrono>
#include <functional>

#include "SPALifecycle.h"
#include "Task/AbstractTaskPlugin.h"

namespace asst::spa
{
class LifecycleTaskPlugin final : public AbstractTaskPlugin
{
public:
    LifecycleTaskPlugin(
        const AsstCallback& callback,
        Assistant* inst,
        std::string_view chain,
        std::function<bool()> execute,
        std::string client_type,
        std::string mode = "independent",
        std::optional<int> fresh_independent_difficulty = std::nullopt);
    bool verify(AsstMsg msg, const json::value& details) const override;
    void observe_event(const json::value& details);

    void note_battle(bool seen) { m_battle_seen = m_battle_seen || seen; }

    void note_solo_result(const json::object& result);

    void note_input_reconciliation(std::string_view event) { m_reconciliation_event = event; }

protected:
    bool _run() override;

private:
    std::function<bool()> m_execute;
    std::string m_client_type;
    std::string m_mode;
    std::optional<int> m_fresh_independent_difficulty;
    bool m_fresh_independent_start_issued = false;
    bool m_fresh_independent_hud_verified = false;
    mutable std::string m_phase;
    std::optional<json::object> m_result;
    std::optional<json::object> m_personal_result;
    bool m_battle_seen = false;
    std::string m_reconciliation_event;
    LifecycleNavigationGuard m_navigation_guard;
    LifecycleLoadingGrace m_loading_grace;
    int m_dispatches = 0;
    std::optional<std::chrono::steady_clock::time_point> m_joint_search_started;
};
}
