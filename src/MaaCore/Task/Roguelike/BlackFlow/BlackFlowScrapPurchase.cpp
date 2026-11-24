#include "BlackFlowScrapPurchase.h"

#include <algorithm>
#include <utility>

namespace asst::blackflow
{
std::optional<Rect> scrap_purchase_sold_region(const Rect& title, int image_width, int image_height)
{
    if (title.empty() || title.x < 40 || title.y < 0 || title.x + title.width > image_width ||
        title.y + title.height > image_height || title.y + 135 > image_height) {
        return std::nullopt;
    }
    return Rect(title.x - 40, title.y + 55, std::min(220, image_width - title.x + 40), 80);
}

bool ScrapPurchaseTransaction::begin(ScrapPurchaseIntent intent)
{
    if (m_state != ScrapPurchaseState::Idle || m_intent || intent.name.empty() || intent.wallet_before < 0 ||
        intent.maximum_price <= 0 || intent.maximum_price > intent.wallet_before || intent.rect.empty()) {
        return false;
    }
    m_intent = std::move(intent);
    m_confirmation_input = false;
    m_observations = 0;
    m_paid = 0;
    m_state = ScrapPurchaseState::AwaitingReceipt;
    return true;
}

void ScrapPurchaseTransaction::note_confirmation_input()
{
    if (m_state == ScrapPurchaseState::AwaitingReceipt) {
        m_confirmation_input = true;
    }
}

ScrapPurchaseState ScrapPurchaseTransaction::observe(const ScrapPurchaseObservation& observation)
{
    if (m_state != ScrapPurchaseState::AwaitingReceipt || !m_intent) {
        return m_state;
    }
    ++m_observations;
    if (observation.buying_page && !observation.confirmation_visible && observation.wallet) {
        const int after = *observation.wallet;
        if (after < 0 || after > m_intent->wallet_before || m_intent->wallet_before - after > m_intent->maximum_price) {
            m_state = ScrapPurchaseState::Failed;
        }
        else if (m_confirmation_input && observation.sold_item == m_intent->name && after < m_intent->wallet_before) {
            // Prices may be discounted. With one owned intent, a matching sold slot
            // and a positive debit bounded by the conservative price identify the
            // receipt.
            m_paid = m_intent->wallet_before - after;
            m_state = ScrapPurchaseState::Confirmed;
        }
    }
    if (m_state == ScrapPurchaseState::AwaitingReceipt && m_observations >= MaxObservations) {
        m_state = ScrapPurchaseState::Failed;
    }
    return m_state;
}

std::optional<ScrapPurchaseReceipt> ScrapPurchaseTransaction::take_confirmed()
{
    if (m_state != ScrapPurchaseState::Confirmed || !m_intent) {
        return std::nullopt;
    }
    ScrapPurchaseReceipt receipt { std::move(*m_intent), m_paid };
    m_intent.reset();
    m_state = ScrapPurchaseState::Idle;
    m_confirmation_input = false;
    return receipt;
}
} // namespace asst::blackflow
