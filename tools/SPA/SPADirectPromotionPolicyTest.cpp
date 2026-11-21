#include "Task/MiniGame/SPA/SPADirectPromotionPolicy.h"
#include "Task/MiniGame/SPA/SPAEquipmentPolicy.h"
#include "Task/MiniGame/SPA/SPAGrowth.h"
#include <cassert>

using namespace asst::spa;

int main()
{
    const auto initial_growth = project_primary_growth("阿戈尔", 5, 9, true, { { "魔王", false, true } });
    const auto elite_growth = project_primary_growth("阿戈尔", 5, 9, true, { { "魔王", true, true } });
    assert(initial_growth.valid && elite_growth.valid);
    assert(elite_growth.fixed_phase_additions > initial_growth.fixed_phase_additions);
    assert(direct_promotion_recipient_supported(
        "魔王",
        1,
        0,
        true,
        true,
        true,
        0,
        false,
        elite_growth.fixed_phase_additions - initial_growth.fixed_phase_additions));
    const auto inactive = project_primary_growth("阿戈尔", 5, 9, false, { { "魔王", true, true } });
    assert(inactive.fixed_phase_additions == 0);
    const auto profile = std::find_if(equipment_profiles().begin(), equipment_profiles().end(), [](const auto& item) {
        return item.name == "博士投影";
    });
    assert(profile != equipment_profiles().end());
    const auto normal_quote = promotion_consumable_quote(profile->name, profile->normal_effect);
    const auto advanced_quote = promotion_consumable_quote(profile->name, profile->advanced_effect);
    assert(normal_quote && advanced_quote);
    assert(normal_quote->timing == PromotionConsumableTiming::NextRoundStart);
    assert(advanced_quote->timing == PromotionConsumableTiming::OnEquip);

    EquipmentContext purchase;
    purchase.funds = profile->price;
    purchase.round = 8;
    purchase.boss_round = 9;
    purchase.shop_level = profile->tier;
    purchase.target_deployed = true;
    purchase.direct_promotion_usable =
        direct_promotion_recipient_supported("“暴躁铁皮”", 1, 0, true, true, true, 3, true, 0);
    // Actual purchase scoring: one future preparation still lets the normal
    // projection improve the boss recipient. Economy's -25 used to reject it.
    const auto buy = choose_equipment(purchase, { *profile });
    assert(buy.buy && buy.name == "博士投影" && buy.score > 0);
    purchase.round = 5;
    assert(choose_equipment(purchase, { *profile }).buy);
    purchase.round = 8;
    purchase.direct_promotion_usable = direct_promotion_recipient_supported(
        "魔王",
        1,
        0,
        true,
        true,
        true,
        0,
        false,
        elite_growth.fixed_phase_additions - initial_growth.fixed_phase_additions);
    assert(choose_equipment(purchase, { *profile }).buy);
    const auto allowed = purchase;
    purchase.direct_promotion_usable =
        direct_promotion_recipient_supported("“暴躁铁皮”", 1, 0, true, true, true, 4, true, 0);
    assert(!choose_equipment(purchase, { *profile }).buy);
    purchase.direct_promotion_usable =
        direct_promotion_recipient_supported("魔王", 1, 0, true, true, true, 0, false, inactive.fixed_phase_additions);
    assert(!choose_equipment(purchase, { *profile }).buy);
    purchase.direct_promotion_usable =
        direct_promotion_recipient_supported("“暴躁铁皮”", 2, 0, true, true, true, 0, true, 0);
    assert(!choose_equipment(purchase, { *profile }).buy);
    purchase.direct_promotion_usable =
        direct_promotion_recipient_supported("“暴躁铁皮”", 1, 0, true, false, true, 0, true, 0);
    assert(!choose_equipment(purchase, { *profile }).buy);
    purchase = allowed;
    purchase.direct_promotion_usable = false;
    purchase.target_is_carry = true;
    assert(!choose_equipment(purchase, { *profile }).buy);
    purchase = allowed;
    --purchase.funds;
    assert(!choose_equipment(purchase, { *profile }).buy);
    purchase = allowed;
    --purchase.shop_level;
    assert(!choose_equipment(purchase, { *profile }).buy);
    purchase = allowed;
    purchase.target_elite = true;
    assert(!choose_equipment(purchase, { *profile }).buy);
    purchase = allowed;
    purchase.round = purchase.boss_round;
    assert(!choose_equipment(purchase, { *profile }).buy);
    purchase = allowed;
    purchase.target_deployed = false;
    assert(!choose_equipment(purchase, { *profile }).buy);
    auto unquoted = *profile;
    unquoted.normal_effect.clear();
    assert(!choose_equipment(allowed, { unquoted }).buy);
    unquoted = *profile;
    unquoted.advanced_effect.clear();
    assert(!choose_equipment(allowed, { unquoted }).buy);

    const auto bankbook = std::find_if(equipment_profiles().begin(), equipment_profiles().end(), [](const auto& item) {
        return item.name == "定期存折";
    });
    assert(bankbook != equipment_profiles().end());
    assert(!choose_equipment(allowed, { *bankbook }).buy);
    assert(direct_promotion_recipient_supported("“暴躁铁皮”", 1, 0, true, true, true, 3, true, 0));
    assert(direct_promotion_recipient_supported("魔王", 1, 0, true, true, true, 0, false, 10));
    assert(!direct_promotion_recipient_supported("魔王", 1, 0, true, true, true, 0, false, 0));
    assert(!direct_promotion_recipient_supported("“暴躁铁皮”", 2, 0, true, true, true, 0, true, 0));
    assert(!direct_promotion_recipient_supported("“暴躁铁皮”", 1, 1, true, true, true, 0, true, 0));
    assert(!direct_promotion_recipient_supported("“暴躁铁皮”", 1, 0, true, true, true, 4, true, 0));
    assert(!direct_promotion_recipient_supported("“暴躁铁皮”", 1, 0, true, false, true, 0, true, 0));
    assert(!direct_promotion_recipient_supported("“暴躁铁皮”", 1, 0, true, true, false, 0, true, 0));
    assert(!direct_promotion_recipient_supported("unknown", 1, 0, true, true, true, 0, true, 10));
    const PromotionConsumableQuote normal { PromotionConsumableEffect::DirectPromotion,
                                            PromotionConsumableTiming::NextRoundStart,
                                            0 };
    const PromotionConsumableQuote advanced { PromotionConsumableEffect::DirectPromotion,
                                              PromotionConsumableTiming::OnEquip,
                                              0 };
    assert(!direct_promotion_form_transition(normal, 4, 4, true, true, true, 1, 0));
    assert(direct_promotion_form_transition(normal, 4, 5, true, true, true, 1, 0));
    assert(direct_promotion_form_transition(advanced, 4, 4, true, true, true, 1, 0));
    assert(!direct_promotion_form_transition(normal, 4, 5, true, true, false, 1, 0));
    assert(!direct_promotion_form_transition(normal, 4, 5, true, false, true, 1, 0));
    assert(!direct_promotion_form_transition(normal, 4, 5, false, true, true, 1, 0));
    assert(!direct_promotion_form_transition(normal, 4, 5, true, true, true, 3, 0));
    assert(!direct_promotion_form_transition(normal, 4, 5, true, true, true, 0, 1));
    const PromotionConsumableQuote phone { PromotionConsumableEffect::FirstPactRecruitment,
                                           PromotionConsumableTiming::OnEquip,
                                           1 };
    assert(!direct_promotion_form_transition(phone, 4, 5, true, true, true, 1, 0));
}
