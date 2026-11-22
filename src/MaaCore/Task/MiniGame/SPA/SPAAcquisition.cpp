#include "SPAAcquisition.h"
#include "SPAEquipmentCatalogue.h"
#include "SPAFormation.h"
#include "SPAPlanner.h"
#include <algorithm>
#include <map>
#include <set>

bool asst::spa::verify_strategy_copy_delta(
    const ObservedOperatorInventory& before,
    const ObservedOperatorInventory& after,
    const std::string& recipient,
    bool elite)
{
    const auto pacts = operator_pacts();
    if (!before.complete || !after.complete || !pacts.contains(recipient)) {
        return false;
    }

    int count = 0;
    for (const auto& [name, form] : before.members) {
        if (!pacts.contains(name) || form.initial < 0 || form.initial >= promotion_copies_required(name) ||
            form.elite < 0 || form.elite > 9 || form.initial + form.elite == 0) {
            return false;
        }
        count += form.initial + form.elite;
    }
    if (count >= 10) {
        return false;
    }
    auto expected = before.members;
    auto& gained = expected[recipient];
    ++(elite ? gained.elite : gained.initial);
    return gained.initial < promotion_copies_required(recipient) && expected == after.members;
}

asst::spa::PactRecruitmentDelta asst::spa::verify_pact_recruitment_delta(
    const ObservedOperatorInventory& before,
    const ObservedOperatorInventory& after,
    const std::string& pact,
    int expected_grants)
{
    if (!before.complete || !after.complete) {
        return { false, {}, {}, "Complete physical roster observations are required" };
    }
    if (pact.empty() || (expected_grants != 1 && expected_grants != 2)) {
        return { false, {}, {}, "A known recipient pact and one or two grants are required" };
    }
    const auto pacts = operator_pacts();
    std::set<std::string> names;
    for (const auto* inventory : { &before, &after }) {
        int physical_count = 0;
        for (const auto& [name, counts] : inventory->members) {
            if (!pacts.contains(name) || counts.initial < 0 || counts.elite < 0 || counts.initial > 18 ||
                counts.elite > 18) {
                return { false, {}, {}, "Unknown identity or invalid physical copy counts" };
            }
            const int required = promotion_copies_required(name);
            if (required < 2 || counts.initial >= required) {
                return { false, {}, {}, "An automatic promotion is still unresolved" };
            }
            physical_count += counts.initial + counts.elite;
            names.insert(name);
        }
        if (physical_count > 18) {
            return { false, {}, {}, "Observed operators exceed field and bench capacity" };
        }
    }
    PactRecruitmentDelta result;
    int total_grants = 0;
    for (const auto& name : names) {
        const auto old = before.members.find(name);
        const auto current = after.members.find(name);
        const OperatorFormCounts source = old == before.members.end() ? OperatorFormCounts {} : old->second;
        const OperatorFormCounts target = current == after.members.end() ? OperatorFormCounts {} : current->second;
        const int required = promotion_copies_required(name);
        const int grants = target.initial + required * target.elite - source.initial - required * source.elite;
        if (grants < 0 || grants > expected_grants ||
            target.elite != source.elite + (source.initial + grants) / required ||
            target.initial != (source.initial + grants) % required) {
            return { false, {}, {}, "Physical roster change is not an initial-copy grant and synthesis" };
        }
        if (grants > 0) {
            const auto& membership = pacts.at(name);
            if (std::find(membership.begin(), membership.end(), pact) == membership.end()) {
                return { false, {}, {}, "A gained operator does not belong to the recipient pact" };
            }
            total_grants += grants;
            result.gained_initial_operators.insert(result.gained_initial_operators.end(), grants, name);
            result.promoted_operators.insert(result.promoted_operators.end(), target.elite - source.elite, name);
        }
    }
    if (total_grants != expected_grants) {
        return { false, {}, {}, "Observed grant count does not match the consumable quote" };
    }
    result.verified = true;
    result.reason = "Complete physical snapshots match the quoted pact recruitment and automatic synthesis";
    return result;
}

std::optional<std::vector<asst::spa::AcquiredEffect>>
    asst::spa::acquired_effects(const std::string& name, bool elite, int shop_level)
{
    using K = AcquiredEffectKind;
    const int multiplier = elite ? 2 : 1;
    const std::map<std::string, std::vector<AcquiredEffect>> fixed = {
        { "灰喉", { { K::Layers, "精准", 2 * multiplier } } },
        { "摆渡人", { { K::Layers, "坚守", 1 * multiplier }, { K::Layers, "不屈", 1 * multiplier } } },
        { "霜叶", { { K::Layers, "灵巧", 2 * multiplier } } },
        { "拜松", { { K::Layers, "炎", 2 * multiplier } } },
        { "褐果", { { K::Layers, "聚合", 2 * multiplier } } },
        { "拉普兰德", { { K::Layers, "叙拉古", 2 * multiplier } } },
        { "雪猎", { { K::Layers, "谢拉格", 4 * multiplier }, { K::Layers, "精准", 4 * multiplier } } },
        { "锡人", { { K::Layers, "投资人", 4 * multiplier } } },
        { "宴", { { K::Layers, "不屈", 4 * multiplier } } },
        { "仇白", { { K::Layers, "炎", 6 * multiplier }, { K::Layers, "灵巧", 6 * multiplier } } },
        { "黍", { { K::Layers, "炎", 8 * multiplier }, { K::Layers, "远见", 4 * multiplier } } },
        { "逻各斯", { { K::Layers, "奥术", 10 * multiplier }, { K::Layers, "远见", 5 * multiplier } } },
        { "承曦格雷伊", { { K::Equipment, "精准狙击镜", multiplier } } },
        { "车尔尼", { { K::Equipment, "莱塔尼亚小鼓", multiplier } } },
        { "卡涅利安", { { K::Equipment, "迅捷作战粮", multiplier } } },
        { "耶拉", { { K::Equipment, "谢拉格不融冰", multiplier } } },
        { "乌有", { { K::Equipment, "盟约之币", multiplier } } },
        { "奥尔佳", { { K::Equipment, "乌萨斯呼吸器", multiplier } } },
        { "奥斯塔", { { K::FreeRefresh, "", multiplier } } },
        { "灵知", { { K::SpecialRefresh, "谢拉格", multiplier } } },
        { "缪尔赛思", { { K::Summon, "变形同构体", multiplier } } },
        { "凯瑟琳", { { K::Equipment, elite ? "特殊维式重锤" : "维式重锤", 1, true } } },
        { "极光", { { K::Stateful, "谢拉格", 0, false, true } } },
        { "忍冬", { { K::Stateful, "免费刷新", 0, false, true } } },
    };
    if (name == "矩") {
        if (shop_level < 1 || shop_level > 6) {
            return std::nullopt;
        }
        return std::vector<AcquiredEffect> { { K::Layers, "炎", shop_level * multiplier } };
    }
    const auto found = fixed.find(name);
    return found == fixed.end() ? std::nullopt : std::optional(found->second);
}

std::vector<std::string> asst::spa::acquired_operator_names()
{
    return { "灰喉",   "摆渡人", "霜叶",     "拜松",       "褐果",   "拉普兰德", "雪猎", "锡人", "宴",
             "仇白",   "黍",     "逻各斯",   "承曦格雷伊", "车尔尼", "卡涅利安", "耶拉", "乌有", "奥尔佳",
             "奥斯塔", "灵知",   "缪尔赛思", "凯瑟琳",     "极光",   "忍冬",     "矩" };
}

std::vector<std::string> asst::spa::acquired_equipment_hints(const std::string& name, bool elite)
{
    std::vector<std::string> result;
    const auto effects = acquired_effects(name, elite);
    if (!effects) {
        return result;
    }
    for (const auto& effect : *effects) {
        if (effect.kind != AcquiredEffectKind::Equipment || effect.amount <= 0 || effect.randomized_quality ||
            effect.requires_observed_amount ||
            !std::any_of(EquipmentCatalogue.begin(), EquipmentCatalogue.end(), [&](const auto& item) {
                return item.name == effect.target;
            })) {
            continue;
        }
        if (std::find(result.begin(), result.end(), effect.target) == result.end()) {
            result.push_back(effect.target);
        }
    }
    return result;
}
