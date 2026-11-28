#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace asst::blackflow
{
struct ScrapRefreshSlot
{
    int slot = -1;
    // The caller resolves OCR to an exact canonical market name.
    std::string canonical_name;
    bool sold = false;
};

struct ScrapRefreshShelf
{
    // False when any slot, canonical identity or sold state is unknown.
    bool complete = false;
    std::array<ScrapRefreshSlot, 6> slots;
};

// Verified 1280x720 buying layout: three columns, two rows. Quantize the
// canonical title's left edge, never the OCR enumeration order or title width.
inline std::optional<int> scrap_refresh_slot_from_title(int x, int y, int width, int height)
{
    if (x < 0 || x > 1280 || width <= 0 || width > 1280 - x || y < 0 || y > 720 || height <= 0 || height > 720 - y) {
        return std::nullopt;
    }
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 3; ++column) {
            if (std::abs(x - (625 + column * 206)) <= 30 && std::abs(y - (179 + row * 210)) <= 25) {
                return row * 3 + column;
            }
        }
    }
    return std::nullopt;
}

inline bool
    scrap_refresh_capacity_can_fit(int used, int capacity, std::optional<int> exact_inventory_total, int acquisitions)
{
    return exact_inventory_total && *exact_inventory_total >= 0 && used == *exact_inventory_total && used >= 0 &&
           capacity > 0 && capacity <= 64 && used <= capacity && acquisitions > 0 && acquisitions <= capacity - used;
}

struct ScrapRefreshIntent
{
    int wallet_before = 0;
    int maximum_cost = 0;
    int refresh_index = 0;
    // A nonzero, monotonically increasing capture token, also owned by callbacks.
    std::uint64_t frame_sequence = 0;
    ScrapRefreshShelf shelf_before;
};

// All fields must come from this one newly captured frame, never cached OCR.
struct ScrapRefreshObservation
{
    std::uint64_t frame_sequence = 0;
    bool buying_page = false;
    bool confirmation_visible = false;
    std::optional<int> wallet;
    std::optional<ScrapRefreshShelf> shelf;
};

enum class ScrapRefreshState
{
    Idle,
    AwaitingReceipt,
    Confirmed,
    Failed,
};

enum class ScrapRefreshFailure
{
    None,
    InvalidDebit,
    ReceiptUnconfirmed,
};

struct ScrapRefreshReceipt
{
    ScrapRefreshIntent intent;
    int paid = 0;
};

struct ScrapMovementRefreshContext
{
    std::optional<int> wallet;
    std::optional<int> reserved;
    int maximum_cost = 0;
    int refresh_index = 0;
    bool inventory_complete = false;
    bool capacity_known_and_available = false;
    bool shop_pool_known = false;
    int budget_denominator = 4;
};

struct ScrapMovementRefreshTarget
{
    std::string canonical_name;
    // A fresh observed price or a reliable conservative table price.
    std::optional<int> trusted_price;
    bool canonical_source_known = false;
    bool fills_missing_shape = false;
    bool already_bought_this_visit = false;
    bool in_current_shop_pool = false;
    bool capacity_legal_after_purchase = false;
};

struct ScrapMovementRefreshDecision
{
    bool allow = false;
    std::string canonical_name;
    int maximum_cost = 0;
    std::string_view reason;
};

// Ordinary-shop search heuristic, not expected profit: preserve reserves and
// enough cash to buy the cheapest eligible missing movement shape. Call only
// after exhausting worthwhile offers on the current page. The caller verifies
// canonical processing-item identity, shape gaps, capacity and shop-pool data.
inline ScrapMovementRefreshDecision decide_scrap_movement_refresh(
    const ScrapMovementRefreshContext& context,
    const std::vector<ScrapMovementRefreshTarget>& targets)
{
    const auto reject = [](std::string_view reason) {
        return ScrapMovementRefreshDecision { false, {}, 0, reason };
    };
    if (!context.wallet || !context.reserved) {
        return reject("funds_unknown");
    }
    if (*context.wallet < 0 || *context.reserved < 0 || context.maximum_cost <= 0 || context.refresh_index < 0 ||
        context.budget_denominator <= 0) {
        return reject("invalid_input");
    }
    if (context.refresh_index != 0) {
        return reject("already_refreshed");
    }
    if (!context.inventory_complete) {
        return reject("inventory_unknown");
    }
    if (!context.capacity_known_and_available) {
        return reject("capacity_unavailable");
    }
    if (!context.shop_pool_known) {
        return reject("shop_pool_unknown");
    }
    const ScrapMovementRefreshTarget* selected = nullptr;
    for (const auto& target : targets) {
        if ((target.canonical_source_known && target.canonical_name.empty()) ||
            (target.trusted_price && *target.trusted_price < 0)) {
            return reject("invalid_input");
        }
        if (!target.canonical_source_known || target.canonical_name.empty() || !target.trusted_price ||
            !target.fills_missing_shape || target.already_bought_this_visit || !target.in_current_shop_pool ||
            !target.capacity_legal_after_purchase) {
            continue;
        }
        if (!selected || *target.trusted_price < *selected->trusted_price ||
            (*target.trusted_price == *selected->trusted_price && target.canonical_name < selected->canonical_name)) {
            selected = &target;
        }
    }
    if (!selected) {
        return reject("no_eligible_target");
    }
    // Nonnegative int inputs fit this widened product and sum. A requirement
    // beyond the wallet's representable range must never wrap into approval.
    const std::int64_t required = static_cast<std::int64_t>(*selected->trusted_price) +
                                  static_cast<std::int64_t>(context.budget_denominator) * context.maximum_cost;
    if (required > std::numeric_limits<int>::max()) {
        return reject("budget_overflow");
    }
    const std::int64_t budget = static_cast<std::int64_t>(*context.wallet) - *context.reserved;
    if (budget < required) {
        return reject("insufficient_budget");
    }
    return { true, selected->canonical_name, context.maximum_cost, "budgeted_search" };
}

// Own one refresh until a fresh buying-page frame proves both its debit and
// complete shelf change. This model never changes the inventory or kept items.
class ScrapRefreshTransaction
{
public:
    bool begin(ScrapRefreshIntent intent)
    {
        if (m_state != ScrapRefreshState::Idle || intent.wallet_before < 0 || intent.maximum_cost <= 0 ||
            intent.maximum_cost > intent.wallet_before || intent.refresh_index < 0 ||
            intent.frame_sequence <= m_last_observation_sequence || !valid_shelf(intent.shelf_before)) {
            return false;
        }
        m_last_observation_sequence = intent.frame_sequence;
        m_intent = std::move(intent);
        m_confirmation_input = false;
        m_observations = 0;
        m_paid = 0;
        m_failure = ScrapRefreshFailure::None;
        m_state = ScrapRefreshState::AwaitingReceipt;
        return true;
    }

    void note_confirmation_input(std::uint64_t intent_frame_sequence)
    {
        if (m_state == ScrapRefreshState::AwaitingReceipt && m_intent &&
            m_intent->frame_sequence == intent_frame_sequence) {
            m_confirmation_input = true;
        }
    }

    ScrapRefreshState observe(const ScrapRefreshObservation& observation)
    {
        if (m_state != ScrapRefreshState::AwaitingReceipt || !m_intent) {
            return m_state;
        }
        ++m_observations;
        const bool fresh = observation.frame_sequence > m_last_observation_sequence;
        if (fresh) {
            m_last_observation_sequence = observation.frame_sequence;
        }
        if (fresh && observation.buying_page && !observation.confirmation_visible && observation.wallet) {
            const int after = *observation.wallet;
            // Validate before subtracting: both valid balances are nonnegative.
            if (after < 0 || after > m_intent->wallet_before ||
                m_intent->wallet_before - after > m_intent->maximum_cost) {
                fail(ScrapRefreshFailure::InvalidDebit);
            }
            else if (
                m_confirmation_input && after < m_intent->wallet_before && observation.shelf &&
                valid_shelf(*observation.shelf) && shelf_changed(m_intent->shelf_before, *observation.shelf)) {
                m_paid = m_intent->wallet_before - after;
                m_state = ScrapRefreshState::Confirmed;
            }
        }
        if (m_state == ScrapRefreshState::AwaitingReceipt && m_observations >= MaxObservations) {
            fail(ScrapRefreshFailure::ReceiptUnconfirmed);
        }
        return m_state;
    }

    [[nodiscard]] std::optional<ScrapRefreshReceipt> take_confirmed()
    {
        if (m_state != ScrapRefreshState::Confirmed || !m_intent) {
            return std::nullopt;
        }
        ScrapRefreshReceipt receipt { std::move(*m_intent), m_paid };
        m_intent.reset();
        m_confirmation_input = false;
        m_state = ScrapRefreshState::Idle;
        return receipt;
    }

    [[nodiscard]] ScrapRefreshState state() const noexcept { return m_state; }

    [[nodiscard]] ScrapRefreshFailure failure() const noexcept { return m_failure; }

    [[nodiscard]] const std::optional<ScrapRefreshIntent>& intent() const noexcept { return m_intent; }

private:
    static bool valid_shelf(const ScrapRefreshShelf& shelf)
    {
        if (!shelf.complete) {
            return false;
        }
        std::array<bool, 6> seen {};
        for (const auto& entry : shelf.slots) {
            if (entry.slot < 0 || entry.slot >= static_cast<int>(seen.size()) || entry.canonical_name.empty() ||
                seen[static_cast<std::size_t>(entry.slot)]) {
                return false;
            }
            seen[static_cast<std::size_t>(entry.slot)] = true;
        }
        return true;
    }

    // Both shelves have exactly one validated entry per slot. Storage order is
    // immaterial; OCR result reordering cannot manufacture a new generation.
    static bool shelf_changed(const ScrapRefreshShelf& before, const ScrapRefreshShelf& after)
    {
        for (const auto& old : before.slots) {
            for (const auto& current : after.slots) {
                if (old.slot == current.slot &&
                    (old.canonical_name != current.canonical_name || old.sold != current.sold)) {
                    return true;
                }
            }
        }
        return false;
    }

    void fail(ScrapRefreshFailure reason)
    {
        m_failure = reason;
        m_state = ScrapRefreshState::Failed;
    }

    static constexpr int MaxObservations = 3;
    ScrapRefreshState m_state = ScrapRefreshState::Idle;
    ScrapRefreshFailure m_failure = ScrapRefreshFailure::None;
    std::optional<ScrapRefreshIntent> m_intent;
    std::uint64_t m_last_observation_sequence = 0;
    bool m_confirmation_input = false;
    int m_observations = 0;
    int m_paid = 0;
};
} // namespace asst::blackflow
