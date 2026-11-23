#include "BlackFlowSummonRetreat.h"

#include <algorithm>
#include <utility>

using namespace asst::blackflow;

BlackFlowSummonRetreat::BlackFlowSummonRetreat(
    std::string battle_key,
    std::string summon_name,
    std::string summon_id,
    asst::Point owner_tile,
    std::vector<asst::Point> allowed_tiles,
    SummonRetreatIntent intent) :
    m_battle_key(std::move(battle_key)),
    m_summon_name(std::move(summon_name)),
    m_summon_id(std::move(summon_id)),
    m_owner_tile(owner_tile),
    m_allowed_tiles(std::move(allowed_tiles)),
    m_intent(intent)
{
}

void BlackFlowSummonRetreat::reset_transaction()
{
    m_phase = Phase::Idle;
    m_pending_tile.reset();
    m_initial_alive.reset();
    m_started_ms = 0;
    m_normal_ack_count = 0;
    m_normal_ack_first_ms = 0;
}

SummonRetreatDecision BlackFlowSummonRetreat::cancel()
{
    m_initial_alive.reset();
    m_phase = Phase::Cancelling;
    m_normal_ack_count = 0;
    m_authorized_action = SummonRetreatAction::CancelSelection;
    return { SummonRetreatAction::CancelSelection, m_pending_tile };
}

bool BlackFlowSummonRetreat::accept(const SummonRetreatObservation& observation)
{
    if (observation.battle_key != m_battle_key || m_battle_key.empty() || observation.sequence <= m_sequence ||
        observation.observed_ms < m_observed_ms || observation.received_ms < observation.observed_ms ||
        observation.received_ms - observation.observed_ms > 500) {
        m_initial_alive.reset();
        return false;
    }
    m_sequence = observation.sequence;
    m_observed_ms = observation.observed_ms;
    m_received_ms = observation.received_ms;
    return true;
}

SummonRetreatDecision BlackFlowSummonRetreat::abandon(const SummonRetreatObservation& observation)
{
    m_initial_alive.reset();
    m_authorized_action = SummonRetreatAction::None;
    if (!accept(observation) || observation.user_paused || !observation.selection_view ||
        (m_phase != Phase::Verifying && m_phase != Phase::Retreating && m_phase != Phase::CheckingRemoval &&
         m_phase != Phase::AwaitNormalAck && m_phase != Phase::HandingOff)) {
        return {};
    }
    return cancel();
}

bool BlackFlowSummonRetreat::acknowledge_normal(const SummonRetreatObservation& observation)
{
    m_initial_alive.reset();
    m_authorized_action = SummonRetreatAction::None;
    if (!observation.normal_view || observation.selection_view || observation.user_paused ||
        (m_phase != Phase::Verifying && m_phase != Phase::CheckingRemoval && m_phase != Phase::AwaitNormalAck &&
         m_phase != Phase::HandingOff)) {
        m_normal_ack_count = 0;
        return false;
    }
    if (!accept(observation)) {
        return false;
    }
    if (m_normal_ack_count == 0 || observation.observed_ms - m_normal_ack_first_ms > 1500) {
        m_normal_ack_count = 1;
        m_normal_ack_first_ms = observation.observed_ms;
        return false;
    }
    if (observation.observed_ms - m_normal_ack_first_ms < 100) {
        return false;
    }
    m_normal_ack_count = 2;
    m_phase = Phase::HandingOff;
    return true;
}

bool BlackFlowSummonRetreat::complete_handoff()
{
    m_authorized_action = SummonRetreatAction::None;
    if (m_phase != Phase::HandingOff) {
        return false;
    }
    reset_transaction();
    return true;
}

SummonRetreatDecision BlackFlowSummonRetreat::observe(const SummonRetreatObservation& observation)
{
    m_authorized_action = SummonRetreatAction::None;
    if (!accept(observation)) {
        return {};
    }
    const bool protect = m_intent == SummonRetreatIntent::ProtectOwnerFromBoss;
    if (!protect && (m_allowed_tiles.size() != 1 || m_allowed_tiles.front() == m_owner_tile)) {
        return {};
    }
    if (protect && observation.boss == BossVitalState::ConfirmedDead) {
        m_boss_dead = true;
        m_initial_alive.reset();
    }
    if (!observation.owner_present || observation.user_paused || observation.normal_view) {
        // A normal view must acquire its own fresh Alive evidence, rather than
        // reviving a selected-view credential from a prior UI state.
        m_initial_alive.reset();
    }
    // The actor does not own the user's pause and may not cancel or resume it.
    if (observation.user_paused) {
        return {};
    }
    if (m_phase == Phase::AwaitNormalAck || m_phase == Phase::HandingOff) {
        return {};
    }
    const bool selected_lease = protect && m_phase == Phase::Verifying && initial_alive_usable(observation);
    if (m_boss_dead || (protect && observation.boss != BossVitalState::ConfirmedAlive && !selected_lease) ||
        !observation.owner_present) {
        if ((m_phase == Phase::Verifying || m_phase == Phase::Retreating) && observation.selection_view) {
            return cancel();
        }
        if (observation.normal_view) {
            reset_transaction();
        }
        return {};
    }
    if (m_phase == Phase::Selecting || m_phase == Phase::Retreating || m_phase == Phase::Cancelling) {
        return {};
    }
    if (m_phase == Phase::CheckingRemoval && observation.normal_view) {
        const bool absent = std::ranges::find(observation.confirmed_empty_tiles, *m_pending_tile) !=
                                observation.confirmed_empty_tiles.end() &&
                            std::ranges::find(observation.candidates, *m_pending_tile) == observation.candidates.end();
        // A completed input alone is not removal evidence. A fresh normal view
        // must also show the owner, and token vision must certify this tile.
        if (absent) {
            const auto tile = m_pending_tile;
            reset_transaction();
            return { SummonRetreatAction::None, tile, true };
        }
        if (observation.observed_ms - m_started_ms >= 1500) {
            reset_transaction();
        }
        return {};
    }
    if (m_phase == Phase::Verifying) {
        if (observation.observed_ms - m_started_ms >= 1500) {
            return observation.selection_view ? cancel() : (reset_transaction(), SummonRetreatDecision {});
        }
        if (!observation.selection_view) {
            return {};
        }
        if ((!observation.selected_name.empty() && observation.selected_name != m_summon_name) ||
            (!observation.selected_id.empty() && observation.selected_id != m_summon_id)) {
            return cancel();
        }
        if (observation.selected_name == m_summon_name && observation.selected_id == m_summon_id &&
            observation.retreat_button_present) {
            m_phase = Phase::Retreating;
            m_authorized_action = SummonRetreatAction::RetreatSelection;
            return { SummonRetreatAction::RetreatSelection, m_pending_tile };
        }
        return {};
    }
    if (m_phase != Phase::Idle || !observation.normal_view || m_summon_name.empty() || m_summon_id.empty()) {
        return {};
    }
    for (const auto& tile : observation.candidates) {
        if (tile == m_owner_tile || std::ranges::find(m_allowed_tiles, tile) == m_allowed_tiles.end()) {
            continue;
        }
        m_pending_tile = tile;
        m_started_ms = observation.observed_ms;
        if (protect) {
            m_initial_alive = InitialAliveCredential { observation.sequence,
                                                       observation.observed_ms,
                                                       observation.received_ms,
                                                       false };
        }
        m_phase = Phase::Selecting;
        m_authorized_action = SummonRetreatAction::Select;
        return { SummonRetreatAction::Select, tile };
    }
    return {};
}

void BlackFlowSummonRetreat::input_result(SummonRetreatAction action, bool succeeded)
{
    m_authorized_action = SummonRetreatAction::None;
    if (action == SummonRetreatAction::Select && m_phase == Phase::Selecting) {
        if (succeeded) {
            m_phase = Phase::Verifying;
            if (m_initial_alive) {
                m_initial_alive->select_accepted = true;
            }
        }
        else {
            reset_transaction();
        }
    }
    else if (action == SummonRetreatAction::RetreatSelection && m_phase == Phase::Retreating) {
        // One input attempt consumes the initial evidence, even on rejection.
        m_initial_alive.reset();
        m_phase = succeeded ? Phase::CheckingRemoval : Phase::Verifying;
    }
    else if (action == SummonRetreatAction::CancelSelection && m_phase == Phase::Cancelling) {
        if (succeeded) {
            m_phase = Phase::AwaitNormalAck;
        }
        else {
            m_phase = Phase::Verifying;
        }
    }
}

bool BlackFlowSummonRetreat::authorized(SummonRetreatAction action) const noexcept
{
    if (action == SummonRetreatAction::None || action != m_authorized_action) {
        return false;
    }
    if (action == SummonRetreatAction::CancelSelection) {
        return m_phase == Phase::Cancelling;
    }
    return !m_boss_dead && ((action == SummonRetreatAction::Select && m_phase == Phase::Selecting) ||
                            (action == SummonRetreatAction::RetreatSelection && m_phase == Phase::Retreating));
}

bool BlackFlowSummonRetreat::initial_alive_usable(const SummonRetreatObservation& observation) const noexcept
{
    return m_initial_alive && m_initial_alive->select_accepted && !m_boss_dead &&
           observation.battle_key == m_battle_key && observation.owner_present && !observation.user_paused &&
           observation.selection_view && !observation.normal_view && observation.boss == BossVitalState::Unknown &&
           observation.observed_ms >= m_initial_alive->observed_ms &&
           observation.observed_ms - m_initial_alive->observed_ms < 1500 &&
           observation.received_ms >= observation.observed_ms &&
           observation.received_ms - observation.observed_ms <= 500 &&
           observation.received_ms - m_initial_alive->observed_ms < 1500;
}

SummonRetreatAuthorization BlackFlowSummonRetreat::authorize_input(
    SummonRetreatAction action,
    const SummonRetreatObservation& observation,
    std::uint64_t now_ms,
    std::optional<asst::Point> current_owner_tile,
    std::optional<asst::Point> requested_tile)
{
    SummonRetreatAuthorization result;
    result.checked_ms = now_ms;
    result.observation_valid_through_ms = observation.observed_ms + 500;
    result.transaction_valid_before_ms = m_started_ms + 1500;
    if (m_initial_alive) {
        result.initial_alive_sequence = m_initial_alive->sequence;
        result.initial_alive_observed_ms = m_initial_alive->observed_ms;
        result.initial_alive_received_ms = m_initial_alive->received_ms;
    }
    const auto denied = [&] {
        m_authorized_action = SummonRetreatAction::None;
        m_initial_alive.reset();
        if (m_phase == Phase::Cancelling) {
            // No cancel input was sent. Retain our UI ownership, but allow a
            // later fresh selected frame to request cleanup again.
            m_phase = Phase::Verifying;
        }
        return result;
    };
    if (!authorized(action) || observation.battle_key != m_battle_key || observation.sequence != m_sequence ||
        observation.observed_ms != m_observed_ms || observation.received_ms != m_received_ms ||
        now_ms < observation.received_ms || now_ms - observation.observed_ms > 500 || observation.user_paused ||
        !m_pending_tile || requested_tile != m_pending_tile) {
        return denied();
    }
    if (action == SummonRetreatAction::CancelSelection) {
        // Expiry forbids retreat, but must not prevent closing our own panel.
        result.allowed = observation.selection_view && !observation.normal_view;
        result.basis =
            result.allowed ? SummonRetreatEvidenceBasis::OwnedSelectionCleanup : SummonRetreatEvidenceBasis::None;
        if (!result.allowed) {
            return denied();
        }
        m_authorized_action = SummonRetreatAction::None;
        return result;
    }
    if (!observation.owner_present || current_owner_tile != std::optional<asst::Point>(m_owner_tile) ||
        now_ms < m_started_ms || now_ms - m_started_ms >= 1500) {
        return denied();
    }
    const bool protect = m_intent == SummonRetreatIntent::ProtectOwnerFromBoss;
    if (action == SummonRetreatAction::Select) {
        result.allowed = observation.normal_view && !observation.selection_view &&
                         (!protect || observation.boss == BossVitalState::ConfirmedAlive);
        result.basis = !result.allowed ? SummonRetreatEvidenceBasis::None
                       : protect       ? SummonRetreatEvidenceBasis::FreshNormalAlive
                                       : SummonRetreatEvidenceBasis::RequestedTileInspection;
    }
    else if (action == SummonRetreatAction::RetreatSelection) {
        const bool identity = observation.selection_view && !observation.normal_view &&
                              observation.selected_name == m_summon_name && observation.selected_id == m_summon_id &&
                              observation.retreat_button_present;
        const bool leased = protect && initial_alive_usable(observation);
        result.allowed = identity && (!protect || observation.boss == BossVitalState::ConfirmedAlive || leased);
        result.basis = !result.allowed ? SummonRetreatEvidenceBasis::None
                       : !protect      ? SummonRetreatEvidenceBasis::SelectedExactIdentity
                       : leased        ? SummonRetreatEvidenceBasis::InitialNormalAliveLease
                                       : SummonRetreatEvidenceBasis::FreshSelectedAlive;
    }
    if (!result.allowed) {
        return denied();
    }
    m_authorized_action = SummonRetreatAction::None;
    return result;
}

void BlackFlowSummonRetreat::invalidate_input_permission() noexcept
{
    m_initial_alive.reset();
    m_authorized_action = SummonRetreatAction::None;
}
