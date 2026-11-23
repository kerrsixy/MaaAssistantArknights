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

enum class SummonRetreatIntent
{
    ProtectOwnerFromBoss,
    ReclaimDeploymentTile,
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
    // Vision or a required deployment site may propose inspection. A proposal
    // never authorizes retreat without the selected exact identity and button.
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

// An input-free transaction policy. Both intents require fresh exact identity.
// Protection additionally requires a live Boss; reclamation is limited to one
// requested deployment tile and never invents Boss evidence.
class BlackFlowSummonRetreat
{
public:
    BlackFlowSummonRetreat(
        std::string battle_key,
        std::string summon_name,
        std::string summon_id,
        Point owner_tile,
        std::vector<Point> allowed_tiles,
        SummonRetreatIntent intent = SummonRetreatIntent::ProtectOwnerFromBoss);

    SummonRetreatDecision observe(const SummonRetreatObservation& observation);
    SummonRetreatDecision abandon(const SummonRetreatObservation& observation);
    bool acknowledge_normal(const SummonRetreatObservation& observation);
    bool complete_handoff();
    void input_result(SummonRetreatAction action, bool succeeded);
    bool authorized(SummonRetreatAction action) const noexcept;

    bool boss_dead() const noexcept { return m_boss_dead; }

    SummonRetreatIntent intent() const noexcept { return m_intent; }

    const std::vector<Point>& allowed_tiles() const noexcept { return m_allowed_tiles; }

    std::optional<Point> pending_tile() const noexcept { return m_pending_tile; }

    bool awaiting_normal_ack() const noexcept
    {
        return m_phase == Phase::CheckingRemoval || m_phase == Phase::AwaitNormalAck;
    }

    bool handoff_pending() const noexcept { return m_phase == Phase::HandingOff; }

private:
    enum class Phase
    {
        Idle,
        Selecting,
        Verifying,
        Retreating,
        CheckingRemoval,
        Cancelling,
        AwaitNormalAck,
        HandingOff,
    };

    SummonRetreatDecision cancel();
    void reset_transaction();
    bool accept(const SummonRetreatObservation& observation);

    std::string m_battle_key;
    std::string m_summon_name;
    std::string m_summon_id;
    Point m_owner_tile;
    std::vector<Point> m_allowed_tiles;
    SummonRetreatIntent m_intent;
    unsigned m_normal_ack_count = 0;
    std::uint64_t m_normal_ack_first_ms = 0;
    Phase m_phase = Phase::Idle;
    std::optional<Point> m_pending_tile;
    std::uint64_t m_sequence = 0;
    std::uint64_t m_observed_ms = 0;
    std::uint64_t m_started_ms = 0;
    bool m_boss_dead = false;
    SummonRetreatAction m_authorized_action = SummonRetreatAction::None;
};
} // namespace asst::blackflow
