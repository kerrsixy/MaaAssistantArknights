#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace asst::spa
{
inline constexpr bool is_immediate_cash_equipment(std::string_view name)
{
    return name == "盟约之币" || name == "定期存折";
}

inline constexpr bool profitable_passbook_purchase(int round, int boss_round, int price)
{
    return round >= 1 && boss_round <= 20 && price >= 0 && boss_round - round > price;
}

// Both strings must come from the selected inventory item's same screenshot.
// The caller owns page/title verification; catalogue text is never live proof.
struct CashEquipmentPanelEvidence
{
    std::string equipment;
    std::string selected_title;
    std::string effect_text;
    int round = -1;
    std::uint64_t frame_id = 0;
    bool title_verified = false;
    bool effect_complete = false;
};

struct CashEquipmentRedemptionQuote
{
    std::string equipment;
    int amount = 0;
    int round = -1;
    std::uint64_t frame_id = 0;
};

// Positive immediate cash only. Zero is a valid held balance, but cannot
// authorize redemption. In particular, the client's initial zero template
// and its {showtag_value_1} placeholder are not a spendable live quote.
inline std::optional<CashEquipmentRedemptionQuote> cash_equipment_redemption_quote(
    const CashEquipmentPanelEvidence& panel,
    int current_round,
    std::uint64_t current_frame_id)
{
    if (!panel.title_verified || !panel.effect_complete || current_round < 1 || current_round > 20 ||
        panel.round != current_round || current_frame_id == 0 || panel.frame_id != current_frame_id ||
        panel.selected_title != panel.equipment || !is_immediate_cash_equipment(panel.equipment)) {
        return std::nullopt;
    }
    std::string text;
    for (const char ch : panel.effect_text) {
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            text.push_back(ch);
        }
    }
    const std::string_view gain_word = "获得";
    const auto gain = text.find(gain_word);
    if (gain == std::string::npos || text.find(gain_word, gain + gain_word.size()) != std::string::npos) {
        return std::nullopt;
    }
    const auto prefix = std::string_view(text).substr(0, gain);
    if (prefix != "装备时销毁，" && prefix != "装备时销毁，并" && prefix != "装备时销毁并" && prefix != "装备时销毁," &&
        prefix != "装备时销毁,并") {
        return std::nullopt;
    }
    const auto amount_start = gain + gain_word.size();
    const auto amount_end = text.find("资金", amount_start);
    if (amount_end == std::string::npos || amount_end <= amount_start || amount_end - amount_start > 3 ||
        (amount_end - amount_start > 1 && text[amount_start] == '0')) {
        return std::nullopt;
    }
    int amount = 0;
    const auto parsed = std::from_chars(text.data() + amount_start, text.data() + amount_end, amount);
    if (parsed.ec != std::errc() || parsed.ptr != text.data() + amount_end || amount <= 0) {
        return std::nullopt;
    }
    auto suffix = std::string_view(text).substr(amount_end + std::string_view("资金").size());
    if (suffix.ends_with("。")) {
        suffix.remove_suffix(std::string_view("。").size());
    }
    if (panel.equipment == "盟约之币") {
        if ((amount != 1 && amount != 2) || !suffix.empty()) {
            return std::nullopt;
        }
    }
    else {
        // One to three digits is this OCR parser's accepted format, not a
        // game balance cap or an amount inferred from elapsed rounds.
        if (amount > 999 || (suffix != "（持有时每回合+1）" && suffix != "(持有时每回合+1)")) {
            return std::nullopt;
        }
    }
    return CashEquipmentRedemptionQuote { panel.equipment, amount, current_round, current_frame_id };
}
} // namespace asst::spa
