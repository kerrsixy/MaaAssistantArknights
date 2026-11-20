#include "Task/MiniGame/SPA/SPAConsumablePolicy.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace asst::spa;
    const auto phone = promotion_consumable_quote("简易通讯机", "装备时销毁，随机获得1名拥有该干员第一盟约的干员");
    const auto upgraded_phone =
        promotion_consumable_quote("简易通讯机", "装备时销毁，随机获得 2 名拥有该干员第一盟约的干员");
    assert(phone && phone->effect == PromotionConsumableEffect::FirstPactRecruitment && phone->initial_grants == 1);
    assert(upgraded_phone && upgraded_phone->initial_grants == 2);
    assert(!promotion_consumable_quote("寻呼模块", "装备时销毁，随机获得1名拥有该干员第一盟约的干员"));
    assert(!promotion_consumable_quote("简易通讯机", "装备时销毁，随机获得1名干员"));
    assert(!promotion_consumable_quote("简易通讯机", "装备时销毁，随机获得3名拥有该干员第一盟约的干员"));
    assert(!promotion_consumable_quote("简易通讯机", "装备时销毁，随机获得1名拥有该干员第一盟约的干员，随机获得2名"));

    const auto delayed = promotion_consumable_quote("博士投影", "装备后下个回合开始时销毁，并使该干员晋升为精锐干员");
    const auto immediate = promotion_consumable_quote("博士投影", "装备时销毁，立即使该干员晋升为精锐干员");
    assert(delayed && delayed->timing == PromotionConsumableTiming::NextRoundStart);
    assert(immediate && immediate->timing == PromotionConsumableTiming::OnEquip);
    assert(!promotion_consumable_due(*delayed, 3, 3));
    assert(promotion_consumable_due(*delayed, 3, 4));
    assert(promotion_consumable_due(*immediate, 3, 3));
    assert(!promotion_consumable_due(*immediate, 3, 2));
    assert(!promotion_consumable_due(*immediate, -1, 3));
    assert(!promotion_consumable_quote("博士投影", "使该干员晋升为精锐干员"));
    assert(!promotion_consumable_quote(
        "博士投影",
        "装备后下个回合开始时销毁，并使该干员晋升为精锐干员；装备时销毁，立即使该干员晋升为精锐干员"));

    const std::string mech = "“暴躁铁皮”";
    assert(strategy_core_consumable_supported("band_mech", mech, "简易通讯机", false));
    assert(strategy_core_consumable_supported("band_mech", mech, "博士投影", false));
    assert(!strategy_core_consumable_supported("band_bonus", mech, "博士投影", false));
    assert(!strategy_core_consumable_supported("band_mech", "森蚺", "简易通讯机", false));
    assert(!strategy_core_consumable_supported("band_mech", mech, "寻呼模块", false));
    assert(!strategy_core_consumable_supported("band_mech", mech, "博士投影", true));

    ObservedOperatorInventory before { true, { { mech, { 1, 0 } }, { "空弦", { 1, 1 } } } };
    ObservedOperatorInventory after { true, { { mech, { 0, 1 } }, { "空弦", { 1, 1 } } } };
    assert(verify_direct_promotion_delta(before, after, mech, *immediate, 3, 3));
    assert(!verify_direct_promotion_delta(before, after, mech, *delayed, 3, 3));
    assert(verify_direct_promotion_delta(before, after, mech, *delayed, 3, 4));
    assert(!verify_direct_promotion_delta(before, after, mech, *phone, 3, 3));
    after.complete = false;
    assert(!verify_direct_promotion_delta(before, after, mech, *immediate, 3, 3));
    after.complete = true;
    before.complete = false;
    assert(!verify_direct_promotion_delta(before, after, mech, *immediate, 3, 3));
    before.complete = true;
    after.members["空弦"].initial = 2;
    assert(!verify_direct_promotion_delta(before, after, mech, *immediate, 3, 3));
    after.members["空弦"].initial = 1;
    before.members[mech].initial = 3;
    // A three-copy synthesis is not a Doctor Projection receipt.
    assert(!verify_direct_promotion_delta(before, after, mech, *immediate, 3, 3));
    before.members[mech] = { 2, 1 };
    after.members[mech] = { 1, 2 };
    assert(verify_direct_promotion_delta(before, after, mech, *immediate, 3, 3));
    before.members[mech].initial = 0;
    assert(!verify_direct_promotion_delta(before, after, mech, *immediate, 3, 3));
    std::cout << "Promotion consumable evidence tests passed.\n";
}
