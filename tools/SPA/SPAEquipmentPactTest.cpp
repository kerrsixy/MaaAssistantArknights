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

    std::cout << "Equipment pact evidence tests passed\n";
}
