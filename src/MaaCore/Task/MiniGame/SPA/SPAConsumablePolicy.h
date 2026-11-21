#pragma once

#include "SPAAcquisition.h"
#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <string>

namespace asst::spa
{
enum class PromotionConsumableEffect
{
    FirstPactRecruitment,
    DirectPromotion
};

enum class PromotionConsumableTiming
{
    OnEquip,
    NextRoundStart
};

struct PromotionConsumableQuote
{
    PromotionConsumableEffect effect;
    PromotionConsumableTiming timing;
    int initial_grants = 0;
};

inline bool strategy_core_consumable_supported(
    const std::string& strategy,
    const std::string& recipient,
    const std::string& equipment,
    bool recipient_elite)
{
    return strategy == "band_mech" && recipient == "“暴躁铁皮”" && !recipient_elite &&
           (equipment == "简易通讯机" || equipment == "博士投影");
}

// Read the complete live description, not the item name, price, or shop tier.
// A quote is neither proof of consumption nor a roster mutation receipt.
inline std::optional<PromotionConsumableQuote>
    promotion_consumable_quote(const std::string& name, std::string description)
{
    std::erase_if(description, [](unsigned char value) { return std::isspace(value) != 0; });
    const auto has = [&](const std::string& text) {
        return description.find(text) != std::string::npos;
    };
    if (name == "简易通讯机") {
        const bool one = has("随机获得1名");
        const bool two = has("随机获得2名");
        if (!has("装备时销毁") || !has("拥有该干员第一盟约的干员") || one == two) {
            return std::nullopt;
        }
        return PromotionConsumableQuote { PromotionConsumableEffect::FirstPactRecruitment,
                                          PromotionConsumableTiming::OnEquip,
                                          two ? 2 : 1 };
    }
    if (name == "博士投影") {
        const bool delayed = has("装备后下个回合开始时销毁") && has("并使该干员晋升为精锐干员");
        const bool immediate = has("装备时销毁") && has("立即使该干员晋升为精锐干员");
        if (delayed == immediate) {
            return std::nullopt;
        }
        return PromotionConsumableQuote { PromotionConsumableEffect::DirectPromotion,
                                          delayed ? PromotionConsumableTiming::NextRoundStart
                                                  : PromotionConsumableTiming::OnEquip,
                                          0 };
    }
    return std::nullopt;
}

inline bool promotion_consumable_due(const PromotionConsumableQuote& quote, int contact_round, int observed_round)
{
    if (contact_round < 1 || observed_round < contact_round) {
        return false;
    }
    return quote.timing == PromotionConsumableTiming::OnEquip ||
           (quote.timing == PromotionConsumableTiming::NextRoundStart && observed_round > contact_round);
}

// Direct promotion converts one physical initial body to one elite body.
// It does not synthesize three copies, grant a reward, or prove field poses.
inline bool verify_direct_promotion_delta(
    const ObservedOperatorInventory& before,
    const ObservedOperatorInventory& after,
    const std::string& recipient,
    const PromotionConsumableQuote& quote,
    int contact_round,
    int observed_round)
{
    if (!before.complete || !after.complete || recipient.empty() ||
        quote.effect != PromotionConsumableEffect::DirectPromotion || quote.initial_grants != 0 ||
        !promotion_consumable_due(quote, contact_round, observed_round)) {
        return false;
    }
    std::set<std::string> names;
    for (const auto* inventory : { &before, &after }) {
        int total = 0;
        for (const auto& [name, counts] : inventory->members) {
            if (name.empty() || counts.initial < 0 || counts.elite < 0 || counts.initial > 18 || counts.elite > 18) {
                return false;
            }
            total += counts.initial + counts.elite;
            names.insert(name);
        }
        if (total > 18) {
            return false;
        }
    }
    if (!names.contains(recipient)) {
        return false;
    }
    for (const auto& name : names) {
        const auto old = before.members.find(name);
        const auto current = after.members.find(name);
        const auto source = old == before.members.end() ? OperatorFormCounts {} : old->second;
        const auto target = current == after.members.end() ? OperatorFormCounts {} : current->second;
        const int changed = name == recipient ? 1 : 0;
        if (source.initial < changed || target.initial != source.initial - changed ||
            target.elite != source.elite + changed) {
            return false;
        }
    }
    return true;
}
}
