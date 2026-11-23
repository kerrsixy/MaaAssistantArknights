#pragma once

#include "Task/AbstractTaskPlugin.h"
#include <map>
#include <optional>
#include <utility>

namespace asst::spa
{
class RoundTaskPlugin final : public AbstractTaskPlugin
{
public:
    using AbstractTaskPlugin::AbstractTaskPlugin;
    bool configure(
        int round,
        int remaining,
        bool layout_reviewed,
        bool wait_for_next,
        int expected_funds = 0,
        bool spending_reviewed = false,
        std::string spending_reason = {},
        bool remaining_inferred = false,
        bool joint_mode = false);

    void track_primary_pact(std::string name) { m_primary_pact = std::move(name); }
    void allow_joint_emote_probe(bool enabled) { m_joint_emote_probe = enabled; }
    bool joint_emote_probe_attempted() const { return m_joint_emote_probe_attempted; }

    std::optional<int> latest_primary_layers() const { return m_latest_primary_layers; }

    bool primary_layers_increased_during_battle() const
    {
        return m_first_primary_layers && m_latest_primary_layers &&
               *m_latest_primary_layers > *m_first_primary_layers;
    }

    std::optional<int> latest_primary_layer_sample_enemies() const { return m_latest_primary_layer_sample_enemies; }

    const std::map<std::string, int>& latest_pact_layer_snapshot() const { return m_latest_pact_layer_snapshot; }

    const std::map<std::string, std::string>& observed_teammate_core_pacts() const
    {
        return m_observed_teammate_core_pacts;
    }
    bool battle_seen() const { return m_battle_seen; }

    bool result_seen() const { return m_result_seen; }

    const json::object& last_result() const { return m_last_result; }

    bool session_interrupted() const { return m_session_interrupted; }

    int next_round() const { return m_next_round; }
    bool post_battle_choice_seen() const { return m_post_battle_choice_seen; }

    bool verify(AsstMsg msg, const json::value& details) const override;

protected:
    bool _run() override;
    bool on_run_fails() override;

private:
    std::string m_primary_pact;
    std::optional<int> m_first_primary_layers;
    std::optional<int> m_latest_primary_layers;
    std::optional<int> m_latest_primary_layer_sample_enemies;
    std::map<std::string, int> m_latest_pact_layer_snapshot;
    std::map<std::string, std::string> m_observed_teammate_core_pacts;
    bool m_battle_seen = false;
    bool m_result_seen = false;
    json::object m_last_result;
    bool m_session_interrupted = false;
    int m_next_round = -1;
    bool m_post_battle_choice_seen = false;
    int m_round = -1;
    int m_remaining = -1;
    bool m_wait_for_next = true;
    int m_expected_funds = 0;
    bool m_spending_reviewed = false;
    std::string m_spending_reason;
    bool m_remaining_inferred = false;
    bool m_joint_mode = false;
    bool m_joint_emote_probe = false;
    bool m_joint_emote_probe_attempted = false;
};
}
