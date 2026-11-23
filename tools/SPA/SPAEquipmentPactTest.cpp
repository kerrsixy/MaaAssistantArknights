#include "Task/MiniGame/SPA/SPAEquipmentPolicy.h"
#include "Task/MiniGame/SPA/SPAReplacementPolicy.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    using asst::spa::choose_replacement;
    using asst::spa::equipment_added_pacts;
    using asst::spa::PlacementRole;
    using asst::spa::ReplacementMember;

    const std::vector<std::string> egir { "变形同构体", "阿戈尔重刃" };
    assert(equipment_added_pacts(egir).empty());
    assert(equipment_added_pacts(egir, false).empty());
    assert((equipment_added_pacts(egir, true) == std::vector<std::string> { "阿戈尔" }));
    assert((equipment_added_pacts({ "进阶阿戈尔重刃", "变形同构体" }, true) == std::vector<std::string> { "阿戈尔" }));
    assert(equipment_added_pacts({ "阿戈尔重刃" }, true).empty());
    assert(equipment_added_pacts({ "阿戈尔重刃", "虹彩活血饮料" }, true).empty());
    assert(equipment_added_pacts({ "变形同构体" }, true).empty());
    assert(equipment_added_pacts({ "变形同构体", "虹彩活血饮料" }, true).empty());

    // The initial mech has four slots. Non-selector equipment does not
    // obstruct a uniquely determined conversion after all slots are known.
    std::vector<std::string> four { "变形同构体", "乌萨斯呼吸器", "虹彩活血饮料", "M3茧甲" };
    assert((equipment_added_pacts(four, true) == std::vector<std::string> { "乌萨斯" }));
    std::reverse(four.begin(), four.end());
    assert((equipment_added_pacts(four, true) == std::vector<std::string> { "乌萨斯" }));
    assert(equipment_added_pacts(four, false).empty());

    const ReplacementMember summer { "烈夏", PlacementRole::Ranged, { "乌萨斯" }, 200, 500, 260 };
    const ReplacementMember converter { "折桠", PlacementRole::Frontline, { "乌萨斯" }, 200, 600, 330 };
    ReplacementMember bearer { "机械师", PlacementRole::Frontline, equipment_added_pacts(four, true), 400, 700, 330,
                               true };
    const ReplacementMember tempting_replacement { "拜松", PlacementRole::Frontline, { "炎" }, 12'000 };
    assert(!choose_replacement("乌萨斯", { summer, converter, bearer }, { tempting_replacement }).found);
    bearer.pacts = equipment_added_pacts(four, false);
    assert(choose_replacement("乌萨斯", { summer, converter, bearer }, { tempting_replacement }).outgoing == "机械师");

    // Repeated physical selectors agree; they do not create extra pact
    // memberships. This input requires a complete physical slot observation.
    const std::vector<std::string> six { "精准狙击镜",   "变形同构体", "进阶精准狙击镜",
                                         "虹彩活血饮料", "精准狙击镜", "M3茧甲" };
    assert((equipment_added_pacts(six, true) == std::vector<std::string> { "精准" }));
    assert((equipment_added_pacts({ "变形同构体", "不屈弹射器" }, true) == std::vector<std::string> { "不屈" }));
    assert((equipment_added_pacts({ "变形同构体", "聚合急救箱" }, true) == std::vector<std::string> { "聚合" }));
    assert(
        (equipment_added_pacts({ "变形同构体", "战栗维式重锤", "终极维式重锤", "虹彩活血饮料" }, true) ==
         std::vector<std::string> { "维多利亚" }));

    // The client table gives no ordering/selection rule for mixed selectors.
    // Neither observation order nor a preferred route can resolve this.
    std::vector<std::string> conflict { "变形同构体", "乌萨斯呼吸器", "不屈弹射器", "M3茧甲" };
    assert(equipment_added_pacts(conflict, true).empty());
    std::reverse(conflict.begin(), conflict.end());
    assert(equipment_added_pacts(conflict, true).empty());
    assert(equipment_added_pacts({ "变形同构体", "阿戈尔重刃", "未知装备", "M3茧甲" }, true).empty());
    assert(equipment_added_pacts({ "变形同构体", "变形同构体", "阿戈尔重刃", "M3茧甲" }, true).empty());
    assert(equipment_added_pacts({ "进阶变形同构体", "阿戈尔重刃" }, true).empty());
    assert(equipment_added_pacts({ "变形同构体", "进阶终极维式重锤" }, true).empty());
    assert(equipment_added_pacts({ "变形同构体", "阿戈尔重刃", "博士投影", "M3茧甲" }, true).empty());
    assert(equipment_added_pacts({}, true).empty());
    auto excessive = six;
    excessive.push_back("精准狙击镜");
    assert(equipment_added_pacts(excessive, true).empty());

    // The observed solo Boss preparation used two field-equipment
    // attempts before purchasing two knives and a short blade.
    asst::spa::PersistentEquipmentPreparation boss_gear;
    boss_gear.begin_round(14);
    assert(boss_gear.within_attempt_budget(false));
    assert(!boss_gear.record_assignment("乌萨斯呼吸器", true, false, false));
    assert(!boss_gear.record_assignment("蜂鸣器", false, false, false));
    assert(!boss_gear.within_attempt_budget(false));
    assert(!boss_gear.may_try("蜂鸣器"));
    boss_gear.record_purchase("伊比利亚骨刺刀");
    boss_gear.record_purchase("炎国短刀");
    boss_gear.record_purchase("伊比利亚骨刺刀");
    assert(boss_gear.within_attempt_budget(true));
    assert(boss_gear.record_assignment("伊比利亚骨刺刀", true, false, true));
    assert(boss_gear.may_try("伊比利亚骨刺刀"));
    assert(!boss_gear.record_assignment("伊比利亚骨刺刀", true, false, true));
    assert(!boss_gear.record_assignment("炎国短刀", true, false, true));
    assert(boss_gear.within_attempt_budget(true));

    // An uncertain drop consumes the retry for that name. No second
    // pending purchase hint can justify replaying the action.
    boss_gear.record_purchase("伊比利亚骨刺刀");
    boss_gear.record_purchase("伊比利亚骨刺刀");
    assert(!boss_gear.record_assignment("伊比利亚骨刺刀", false, true, true));
    assert(!boss_gear.may_try("伊比利亚骨刺刀"));
    assert(!boss_gear.record_assignment("谢拉格不融冰", false, false, true));
    assert(!boss_gear.within_attempt_budget(true));
    boss_gear.begin_round(15);
    assert(boss_gear.may_try("伊比利亚骨刺刀") && boss_gear.within_attempt_budget(false));
    assert(!boss_gear.record_assignment("伊比利亚骨刺刀", true, false, false));

    const asst::spa::EquipmentRecipient wearer {
        .name = "忍冬",
        .slots_known = true,
        .equipped = { "伊比利亚骨刺刀" },
        .attacks = true,
        .carry = true,
    };
    const asst::spa::EquipmentRecipient second_wearer { .name = "荒芜拉普兰德", .slots_known = true, .attacks = true };
    const auto knife_assignment =
        asst::spa::choose_equipment_assignment({}, { "伊比利亚骨刺刀" }, { wearer, second_wearer });
    assert(knife_assignment.found && knife_assignment.recipient == "荒芜拉普兰德");

    asst::spa::PersistentEquipmentPreparation solo_shop;
    solo_shop.begin_round(13);
    assert(!solo_shop.has_verified_assignment(13));
    solo_shop.record_assignment("不退者之盾", true, false, false);
    solo_shop.record_assignment("佣兵名牌", true, false, false);
    assert(solo_shop.has_verified_assignment(13) && !solo_shop.has_verified_assignment(14));
    assert(!solo_shop.within_attempt_budget(false));
    assert(solo_shop.within_attempt_budget(false, true));
    solo_shop.record_assignment("护盾无人机", false, false, false);
    assert(!solo_shop.within_attempt_budget(false, true));
    solo_shop.begin_round(14);
    assert(!solo_shop.has_verified_assignment(14) && solo_shop.within_attempt_budget(false));
    solo_shop.record_assignment("不退者之盾", false, true, false);
    solo_shop.record_assignment("佣兵名牌", true, false, false);
    assert(!solo_shop.within_attempt_budget(false, true));

    asst::spa::PersistentEquipmentPreparation blocked_gear;
    const asst::spa::EquipmentRecipient cat { .name = "Miss.Christine", .slots_known = true, .attacks = true,
                                               .carry = true };
    const asst::spa::EquipmentRecipient caster { .name = "异客", .slots_known = true, .attacks = true };
    const std::vector team { cat, caster };
    const std::vector<std::string> coats { "雪祀之衣" };
    const std::multimap<std::string, std::pair<int, int>> poses { { cat.name, { 881, 300 } },
                                                                { caster.name, { 721, 368 } } };
    blocked_gear.begin_round(9);
    blocked_gear.observe_field("m04", poses);
    assert(asst::spa::choose_equipment_assignment({}, coats, team).recipient == cat.name);
    assert(!blocked_gear.record_assignment(coats.front(), false, false, false, cat.name));
    assert(blocked_gear.may_try(coats.front()) && blocked_gear.within_attempt_budget(false));
    assert(asst::spa::choose_equipment_assignment({}, coats, team, blocked_gear.rejected_pairs()).recipient ==
           caster.name);
    blocked_gear.begin_round(10);
    blocked_gear.observe_field("m04", poses);
    assert(blocked_gear.rejected_pairs().contains({ coats.front(), cat.name }));
    assert(!blocked_gear.record_assignment(coats.front(), false, true, false, caster.name));
    assert(!blocked_gear.may_try(coats.front()) &&
           !blocked_gear.rejected_pairs().contains({ coats.front(), caster.name }));
    blocked_gear.begin_round(11);
    blocked_gear.observe_field("m01", poses);
    assert(blocked_gear.rejected_pairs().empty());

    auto geared_carry = cat;
    geared_carry.elite = true;
    geared_carry.equipped = { "维式重锤" };
    asst::spa::EquipmentAssignmentContext jessica_growth;
    jessica_growth.first_equipment_layers = 4;
    assert(asst::spa::choose_equipment_assignment({}, coats, { geared_carry, caster }).recipient == cat.name);
    assert(asst::spa::choose_equipment_assignment(jessica_growth, coats, { geared_carry, caster }).recipient ==
           caster.name);

    std::cout << "Equipment pact evidence tests passed\n";
}
