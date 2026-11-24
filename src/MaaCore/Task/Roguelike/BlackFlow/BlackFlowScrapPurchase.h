#pragma once

#include <optional>
#include <string>

#include "Common/AsstTypes.h"

namespace asst::blackflow
{
[[nodiscard]] std::optional<Rect> scrap_purchase_sold_region(const Rect& title, int image_width, int image_height);

struct ScrapPurchaseIntent
{
    std::string name;
    Rect rect;
    bool keep = false;
    int wallet_before = 0;
    int maximum_price = 0;
};

struct ScrapPurchaseObservation
{
    bool buying_page = false;
    bool confirmation_visible = false;
    std::string sold_item;
    std::optional<int> wallet;
};

enum class ScrapPurchaseState
{
    Idle,
    AwaitingReceipt,
    Confirmed,
    Failed,
};

struct ScrapPurchaseReceipt
{
    ScrapPurchaseIntent intent;
    int paid = 0;
};

// Own one purchase until both the selected shelf slot and the wallet
// acknowledge it. A completed click is input evidence only. It cannot update
// the inventory ledger.
class ScrapPurchaseTransaction
{
public:
    bool begin(ScrapPurchaseIntent intent);
    void note_confirmation_input();
    ScrapPurchaseState observe(const ScrapPurchaseObservation& observation);
    [[nodiscard]] std::optional<ScrapPurchaseReceipt> take_confirmed();

    [[nodiscard]] const std::optional<ScrapPurchaseIntent>& intent() const noexcept { return m_intent; }

    [[nodiscard]] ScrapPurchaseState state() const noexcept { return m_state; }

private:
    static constexpr int MaxObservations = 3;
    std::optional<ScrapPurchaseIntent> m_intent;
    ScrapPurchaseState m_state = ScrapPurchaseState::Idle;
    bool m_confirmation_input = false;
    int m_observations = 0;
    int m_paid = 0;
};
} // namespace asst::blackflow
