#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Common/AsstTypes.h"

namespace asst::blackflow
{
enum class BossVitalState
{
    Unknown,
    ConfirmedAlive,
    ConfirmedDead,
};

enum class SummonRetreatAction
{
    None,
    Select,
    CancelSelection,
    RetreatSelection,
};

struct SummonRetreatObservation
{
    std::string battle_key;
    std::uint64_t sequence = 0;
    std::uint64_t observed_ms = 0;
    std::uint64_t received_ms = 0;
    BossVitalState boss = BossVitalState::Unknown;
    bool user_paused = false;
    bool normal_view = false;
    bool selection_view = false;
    bool owner_present = false;
    // Vision candidates are proposals only. A candidate never authorizes retreat.
    std::vector<Point> candidates;
    std::vector<Point> confirmed_empty_tiles;
    std::string selected_name;
    std::string selected_id;
    bool retreat_button_present = false;
};

struct SummonRetreatDecision
{
    SummonRetreatAction action = SummonRetreatAction::None;
    std::optional<Point> tile;
    bool removal_confirmed = false;
};

// An input-free transaction policy. The actor must supply fresh Boss evidence
// and verify the currently selected identity, then report the actual input result.
// Missing Boss evidence is never converted to death or to permission to retreat.
class BlackFlowSummonRetreat
{
public:
    BlackFlowSummonRetreat(
        std::string battle_key,
        std::string summon_name,
        std::string summon_id,
        Point owner_tile,
        std::vector<Point> allowed_tiles);

    SummonRetreatDecision observe(const SummonRetreatObservation& observation);
    void input_result(SummonRetreatAction action, bool succeeded);
    bool authorized(SummonRetreatAction action) const noexcept;

    bool boss_dead() const noexcept { return m_boss_dead; }

private:
    enum class Phase
    {
        Idle,
        Selecting,
        Verifying,
        Retreating,
        CheckingRemoval,
        Cancelling,
    };

    SummonRetreatDecision cancel();
    void reset_transaction();

    std::string m_battle_key;
    std::string m_summon_name;
    std::string m_summon_id;
    Point m_owner_tile;
    std::vector<Point> m_allowed_tiles;
    Phase m_phase = Phase::Idle;
    std::optional<Point> m_pending_tile;
    std::uint64_t m_sequence = 0;
    std::uint64_t m_observed_ms = 0;
    std::uint64_t m_started_ms = 0;
    bool m_boss_dead = false;
    SummonRetreatAction m_authorized_action = SummonRetreatAction::None;
};
}
