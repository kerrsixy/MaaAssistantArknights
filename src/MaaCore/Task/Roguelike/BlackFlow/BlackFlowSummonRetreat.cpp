#include "BlackFlowSummonRetreat.h"

#include <algorithm>
#include <utility>

using namespace asst::blackflow;

BlackFlowSummonRetreat::BlackFlowSummonRetreat(
    std::string battle_key,
    std::string summon_name,
    std::string summon_id,
    asst::Point owner_tile,
    std::vector<asst::Point> allowed_tiles) :
    m_battle_key(std::move(battle_key)),
    m_summon_name(std::move(summon_name)),
    m_summon_id(std::move(summon_id)),
    m_owner_tile(owner_tile),
    m_allowed_tiles(std::move(allowed_tiles))
{
}

void BlackFlowSummonRetreat::reset_transaction()
{
    m_phase = Phase::Idle;
    m_pending_tile.reset();
}

SummonRetreatDecision BlackFlowSummonRetreat::cancel()
{
    m_phase = Phase::Cancelling;
    m_authorized_action = SummonRetreatAction::CancelSelection;
    return { SummonRetreatAction::CancelSelection, m_pending_tile };
}

SummonRetreatDecision BlackFlowSummonRetreat::observe(const SummonRetreatObservation& observation)
{
    m_authorized_action = SummonRetreatAction::None;
    if (observation.battle_key != m_battle_key || m_battle_key.empty() || observation.sequence <= m_sequence ||
        observation.observed_ms < m_observed_ms || observation.received_ms < observation.observed_ms ||
        observation.received_ms - observation.observed_ms > 500) {
        return {};
    }
    m_sequence = observation.sequence;
    m_observed_ms = observation.observed_ms;
    if (observation.boss == BossVitalState::ConfirmedDead) {
        m_boss_dead = true;
    }
    // The actor does not own the user's pause and may not cancel or resume it.
    if (observation.user_paused) {
        return {};
    }
    if (m_boss_dead || observation.boss != BossVitalState::ConfirmedAlive || !observation.owner_present) {
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
        }
        else {
            reset_transaction();
        }
    }
    else if (action == SummonRetreatAction::RetreatSelection && m_phase == Phase::Retreating) {
        m_phase = succeeded ? Phase::CheckingRemoval : Phase::Verifying;
    }
    else if (action == SummonRetreatAction::CancelSelection && m_phase == Phase::Cancelling) {
        if (succeeded) {
            reset_transaction();
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
