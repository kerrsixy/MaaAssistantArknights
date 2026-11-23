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

enum class SummonRetreatEvidenceBasis
{
    None,
    FreshNormalAlive,
    FreshSelectedAlive,
    InitialNormalAliveLease,
    RequestedTileInspection,
    SelectedExactIdentity,
    OwnedSelectionCleanup,
};

// This is an input-time authorization snapshot, not a new observation. In
// particular InitialNormalAliveLease never rewrites selected Boss Unknown.
struct SummonRetreatAuthorization
{
    bool allowed = false;
    SummonRetreatEvidenceBasis basis = SummonRetreatEvidenceBasis::None;
    std::uint64_t checked_ms = 0;
    std::uint64_t observation_valid_through_ms = 0;
    std::uint64_t transaction_valid_before_ms = 0;
    std::uint64_t initial_alive_sequence = 0;
    std::uint64_t initial_alive_observed_ms = 0;
    std::uint64_t initial_alive_received_ms = 0;
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
    void invalidate_input_permission() noexcept;
    // State-only query for diagnostics/tests. Real input additionally requires
    // authorize_input immediately before the controller call, with no evidence
    // IO.
    bool authorized(SummonRetreatAction action) const noexcept;
    SummonRetreatAuthorization authorize_input(
        SummonRetreatAction action,
        const SummonRetreatObservation& observation,
        std::uint64_t now_ms,
        std::optional<Point> current_owner_tile,
        std::optional<Point> requested_tile);

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

    struct InitialAliveCredential
    {
        std::uint64_t sequence;
        std::uint64_t observed_ms;
        std::uint64_t received_ms;
        bool select_accepted = false;
    };

    bool initial_alive_usable(const SummonRetreatObservation& observation) const noexcept;
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
    std::uint64_t m_received_ms = 0;
    std::optional<InitialAliveCredential> m_initial_alive;
    std::uint64_t m_started_ms = 0;
    bool m_boss_dead = false;
    SummonRetreatAction m_authorized_action = SummonRetreatAction::None;
};
} // namespace asst::blackflow
