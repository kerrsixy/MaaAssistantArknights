#include "SPAEquipmentPolicy.h"
#include "SPACashEquipmentPolicy.h"
#include "SPACombatStats.h"
#include "SPAConsumablePolicy.h"
#include "SPAEquipmentCatalogue.h"
#include "SPAFormation.h"
#include "SPAObservation.h"
#include "SPAOperatorCatalogue.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <utility>

int asst::spa::equipment_capacity(const std::string& recipient, bool elite)
{
    const auto stats = season_combat_stats(recipient, elite);
    return stats ? stats->equipment_slots : 0;
}

bool asst::spa::equipment_title_matches(const std::string& expected, const std::string& observed)
{
    return std::any_of(EquipmentCatalogue.begin(), EquipmentCatalogue.end(), [&](const auto& item) {
        return item.name == expected && (observed == expected || observed == "进阶" + expected);
    });
}

std::optional<std::vector<std::string>>
    asst::spa::equipment_panel_items(const std::vector<TextObservation>& texts, const std::string& recipient)
{
    if (recipient.empty() || operator_panel_identity(texts, { recipient }) != recipient) {
        return std::nullopt;
    }
    std::vector<std::string> result;
    for (const auto& text : texts) {
        if (!std::isfinite(text.score) || text.score < .85 || text.x < 90 || text.x >= 350 || text.y < 500 ||
            text.y >= 720) {
            continue;
        }
        for (const auto& profile : equipment_profiles()) {
            if (equipment_title_matches(profile.name, text.text) &&
                std::find(result.begin(), result.end(), profile.name) == result.end()) {
                result.push_back(profile.name);
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> asst::spa::equipment_added_pacts(const std::vector<std::string>& equipped, bool slots_known)
{
    if (!slots_known || equipped.size() < 2 || equipped.size() > 6) {
        return {};
    }
    // act3autochess/V3_1 trapChessDataDict.giveBondId, corroborated by
    // trap_1073_acarm073's talent. The selector items have canGiveBond=false;
    // their names alone never grant membership. The client metadata supplies
    // no priority rule for different selectors on the four/six-slot mech.
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 24> selectors { {
        { "炎国短刀", "炎" },           { "维式重锤", "维多利亚" },       { "战栗维式重锤", "维多利亚" },
        { "坚固维式重锤", "维多利亚" }, { "加速维式重锤", "维多利亚" },   { "灼燃维式重锤", "维多利亚" },
        { "电流维式重锤", "维多利亚" }, { "终极维式重锤", "维多利亚" },   { "阿戈尔重刃", "阿戈尔" },
        { "萨尔贡浓茶", "萨尔贡" },     { "伊比利亚骨刺刀", "伊比利亚" }, { "莱塔尼亚小鼓", "莱塔尼亚" },
        { "谢拉格不融冰", "谢拉格" },   { "乌萨斯呼吸器", "乌萨斯" },     { "卡西米尔竞技旗", "卡西米尔" },
        { "叙拉古正装", "叙拉古" },     { "精准狙击镜", "精准" },         { "突袭手雷", "突袭" },
        { "坚守盾牌", "坚守" },         { "不屈弹射器", "不屈" },         { "迅捷作战粮", "迅捷" },
        { "灵巧涡轮手套", "灵巧" },     { "奥术法阵", "奥术" },           { "聚合急救箱", "聚合" },
    } };
    int transformers = 0;
    std::string_view selected_pact;
    for (const auto& observed : equipped) {
        const auto item = std::find_if(EquipmentCatalogue.begin(), EquipmentCatalogue.end(), [&](const auto& entry) {
            return observed == entry.name || (!entry.advanced.empty() && observed == "进阶" + std::string(entry.name));
        });
        // An unrecognized/consumed slot could hide another selector or an
        // unresolved equipment transition; it is not a harmless empty slot.
        if (item == EquipmentCatalogue.end() || item->consumed) {
            return {};
        }
        if (item->name == "变形同构体") {
            ++transformers;
            continue;
        }
        const auto selector = std::find_if(selectors.begin(), selectors.end(), [&](const auto& entry) {
            return entry.first == item->name;
        });
        if (selector == selectors.end()) {
            continue;
        }
        if (!selected_pact.empty() && selected_pact != selector->second) {
            return {};
        }
        selected_pact = selector->second;
    }
    return transformers == 1 && !selected_pact.empty() ? std::vector<std::string> { std::string(selected_pact) }
                                                       : std::vector<std::string> {};
}

bool asst::spa::safe_pact_recruitment_recipient(
    const std::string& name,
    const std::string& primary,
    bool equipment_known,
    const std::vector<std::string>& equipped)
{
    if (primary.empty() || !equipment_known || !equipped.empty()) {
        return false;
    }
    const auto catalog = operator_pacts();
    const auto member = catalog.find(name);
    return member != catalog.end() && member->second.size() == 1 && member->second.front() == primary;
}

bool asst::spa::pact_recruitment_offer_eligible(const std::string& name, const std::string& pact, int shop_level)
{
    if (pact.empty() || shop_level < 1 || shop_level > 6) {
        return false;
    }
    const auto catalog = operator_pacts();
    const auto member = catalog.find(name);
    const auto card = std::find_if(OperatorCatalogue.begin(), OperatorCatalogue.end(), [&](const auto& item) {
        return item.name == name;
    });
    return member != catalog.end() && card != OperatorCatalogue.end() && card->tier <= shop_level &&
           std::find(member->second.begin(), member->second.end(), pact) != member->second.end();
}

asst::spa::EquipmentAssignment asst::spa::choose_equipment_assignment(
    const EquipmentAssignmentContext& context,
    const std::vector<std::string>& candidates,
    const std::vector<EquipmentRecipient>& recipients)
{
    EquipmentAssignment best;
    for (const auto& name : candidates) {
        const bool freeze_gear = name == "谢拉格不融冰";
        const auto item =
            std::find_if(equipment_profiles().begin(), equipment_profiles().end(), [&](const auto& profile) {
                return profile.name == name;
            });
        if (item == equipment_profiles().end() || item->consumable || item->role == EquipmentRole::Unknown ||
            item->role == EquipmentRole::Economy || (item->role == EquipmentRole::Tradeoff && name != "源石溶剂") ||
            (item->role == EquipmentRole::Redeployment && name != "不屈弹射器")) {
            continue;
        }
        for (const auto& target : recipients) {
            if (target.name.empty() || !target.slots_known ||
                target.equipped.size() >= static_cast<size_t>(equipment_capacity(target.name, target.elite)) ||
                (name == "阿戈尔重刃" && (!target.carry || !target.attacks)) ||
                std::find(target.equipped.begin(), target.equipped.end(), name) != target.equipped.end()) {
                continue;
            }
            if ((item->role == EquipmentRole::Damage && !target.attacks && !target.healer) ||
                ((name == "源石溶剂" || name == "不屈弹射器") &&
                 (!target.deliberate_redeploy || !context.growth_battle_available)) ||
                (item->role == EquipmentRole::Defense && !target.frontline && name != "雪祀之衣" && name != "伪装服") ||
                (item->role == EquipmentRole::Growth && !target.active_own_pact) ||
                (item->role == EquipmentRole::SkillRecovery && !target.skill_growth_engine &&
                 !target.freeze_skill_engine) ||
                (item->role == EquipmentRole::Control && !target.attacks)) {
                continue;
            }
            if ((name == "护盾无人机" && !target.healer) ||
                ((name == "雪祀之衣" || name == "灵巧涡轮手套") && target.frontline) ||
                ((name == "虹彩活血饮料" || name == "汲取者之镰") && context.field_has_healer) ||
                (name == "虹彩活血饮料" && !target.frontline) ||
                (name == "精准狙击镜" && (!target.attacks || target.max_range_distance < 3)) ||
                (name == "天马之枪" && !target.attacks) ||
                (name == "炎国短刀" && !target.skill_growth_engine && !target.carry) ||
                ((name == "伊比利亚骨刺刀" || name == "多功能补剂" || name == "谢拉格不融冰" || name == "动能单手弩" ||
                  name == "动能轰击弩" || name == "汲取者之喙" || name == "汲取者之镰") &&
                 !target.attacks) ||
                (name == "M3茧甲" && target.deliberate_redeploy) ||
                ((name == "驱逐者警棍" || name == "驱逐者瓦斯" || name == "驱逐者防暴装备") &&
                 !context.status_source_known) ||
                (name == "瓶装烈性反应" && !context.elemental_source_known) ||
                (name == "终极维式重锤" && context.special_hammer_types <= 0)) {
                continue;
            }
            int score = item->score + (target.carry ? 70 : 0);
            if (freeze_gear && context.kjerag_freeze_growth_active) {
                score += target.name == "耶拉" ? 120 : target.max_range_distance * 8;
            }
            if (item->role == EquipmentRole::SkillRecovery && target.freeze_skill_engine) {
                score += 90;
            }
            score += item->role == EquipmentRole::Defense && target.frontline ? 60 : 0;
            score += item->role == EquipmentRole::Sustain && target.healer ? 50 : 0;
            score += item->role == EquipmentRole::SkillRecovery && target.skill_growth_engine ? 90 : 0;
            score += item->role == EquipmentRole::Growth && target.skill_growth_engine ? 60 : 0;
            score += (name == "源石溶剂" || name == "不屈弹射器") && target.deliberate_redeploy ? 90 : 0;
            // Spread generic equipment when the immediate role benefit ties;
            // filling the second slot is still allowed when it scores higher.
            score -= static_cast<int>(target.equipped.size()) * 12;
            if (!best.found || score > best.score) {
                best = { true, name, target.name, score };
            }
        }
    }
    return best;
}

std::optional<int> asst::spa::equipment_cash_gain(const std::string& name, const std::string& description)
{
    if (name != "盟约之币" || description.find("装备时销毁") == std::string::npos) {
        return std::nullopt;
    }
    const bool normal = description.find("获得1资金") != std::string::npos;
    const bool advanced = description.find("获得2资金") != std::string::npos;
    return normal != advanced ? std::optional<int>(normal ? 1 : 2) : std::nullopt;
}

std::optional<int> asst::spa::identity_tag_growth_quote(std::string_view description)
{
    if (description.find("装备时销毁") == std::string_view::npos ||
        description.find("无需激活盟约") == std::string_view::npos) {
        return std::nullopt;
    }
    const auto grant = [&](std::string_view token) {
        const auto start = description.find(token);
        if (start == std::string_view::npos) {
            return false;
        }
        const auto end = start + token.size();
        return end == description.size() || description[end] < '0' || description[end] > '9';
    };
    const bool normal = grant("所属盟约层数+3");
    const bool advanced = grant("所属盟约层数+6");
    return normal != advanced ? std::optional(normal ? 3 : 6) : std::nullopt;
}

bool asst::spa::safe_identity_tag_recipient(
    const std::string& name,
    const std::string& primary,
    bool elite,
    const std::vector<std::string>& equipped)
{
    static const auto catalog = operator_pacts();
    const auto found = catalog.find(name);
    return found != catalog.end() &&
           std::find(found->second.begin(), found->second.end(), primary) != found->second.end() &&
           equipped.size() < static_cast<size_t>(equipment_capacity(name, elite)) &&
           std::none_of(
               equipped.begin(),
               equipped.end(),
               [](const auto& item) { return equipment_title_matches("变形同构体", item); });
}

std::optional<asst::spa::IdentityTagTarget> asst::spa::choose_identity_tag_target(
    const std::string& primary,
    const std::vector<std::string>& support_pacts,
    const std::vector<std::string>& disabled_pacts,
    const std::map<std::string, int>& observed_layers,
    const std::vector<EquipmentRecipient>& recipients,
    bool allow_observed_secondary)
{
    static const auto catalog = operator_pacts();
    std::optional<IdentityTagTarget> best;
    int best_score = 0;
    for (const auto& recipient : recipients) {
        const auto membership = catalog.find(recipient.name);
        if (!recipient.slots_known || membership == catalog.end()) {
            continue;
        }
        for (const auto& pact : membership->second) {
            const auto layers = observed_layers.find(pact);
            if (layers == observed_layers.end() || layers->second < 0 || layers->second > 996 ||
                std::ranges::find(disabled_pacts, pact) != disabled_pacts.end() ||
                !safe_identity_tag_recipient(recipient.name, pact, recipient.elite, recipient.equipped)) {
                continue;
            }
            const int priority = pact == primary                                                 ? 3
                                 : std::ranges::find(support_pacts, pact) != support_pacts.end() ? 2
                                 : allow_observed_secondary && layers->second > 0                ? 1
                                                                                                 : 0;
            if (priority == 0 || (!allow_observed_secondary && pact != primary)) {
                continue;
            }
            const int score = priority * 10 + (recipient.carry ? 2 : 0) + (recipient.active_own_pact ? 1 : 0);
            if (score > best_score) {
                best = IdentityTagTarget { recipient.name, pact };
                best_score = score;
            }
        }
    }
    return best;
}

bool asst::spa::is_recurring_cash_equipment(std::string_view name)
{
    return name == "精打细算玩偶";
}

bool asst::spa::is_deferred_cash_equipment(std::string_view name)
{
    return name == "见钱眼开玩偶" || is_recurring_cash_equipment(name);
}

std::optional<int> asst::spa::equipment_next_round_cash_gain(const std::string& name, const std::string& description)
{
    if (!is_deferred_cash_equipment(name) || description.find("装备时销毁") == std::string::npos) {
        return std::nullopt;
    }
    const bool recurring = is_recurring_cash_equipment(name);
    const bool normal =
        description.find(recurring ? "之后每回合开始时额外获得1资金" : "下回合额外获得2资金") != std::string::npos;
    const bool advanced =
        description.find(recurring ? "之后每回合开始时额外获得2资金" : "下回合额外获得4资金") != std::string::npos;
    return normal != advanced ? std::optional<int>((recurring ? 1 : 2) * (normal ? 1 : 2)) : std::nullopt;
}

bool asst::spa::profitable_next_round_cash_offer(const EquipmentContext& context, const EquipmentProfile& offer)
{
    const auto gain = equipment_next_round_cash_gain(offer.name, offer.normal_effect);
    if (!gain || !context.target_deployed || context.round < 1 || context.round >= context.boss_round ||
        context.boss_round > 20 || offer.price < 0 || offer.price > context.funds || context.shop_level < offer.tier ||
        offer.tier <= 0) {
        return false;
    }
    const int credits = is_recurring_cash_equipment(offer.name) ? context.boss_round - context.round : 1;
    return offer.price < credits * *gain;
}

const std::vector<asst::spa::EquipmentProfile>& asst::spa::equipment_profiles()
{
    static const std::vector<EquipmentProfile> p = [] {
        std::vector<EquipmentProfile> profiles {
            { "维式重锤", 1, EquipmentRole::Damage, false, false, 30, 1 },
            { "坚守盾牌", 1, EquipmentRole::Defense, false, false, 30, 1 },
            { "盟约之币", 1, EquipmentRole::Economy, true, false, 22, 1 },
            { "随身身份牌", 1, EquipmentRole::Growth, true, false, 42, 1 },
            { "源石溶剂", 1, EquipmentRole::Tradeoff, false, true, 36, 1 },
            { "虹彩活血饮料", 1, EquipmentRole::Sustain, false, false, 20, 1 },
            { "雪祀之衣", 1, EquipmentRole::Defense, false, false, 18, 1 },
            { "歌利亚头盔", 2, EquipmentRole::Defense, false, false, 26, 3 },
            { "紧急调度券", 2, EquipmentRole::Economy, true, false, 16, 2 },
            { "不屈弹射器", 1, EquipmentRole::Growth, false, false, 28, 2 },
            { "战栗维式重锤", 2, EquipmentRole::Growth, false, false, 28, 2 },
            { "萨尔贡浓茶", 2, EquipmentRole::Growth, false, false, 28, 2 },
            { "精打细算玩偶", 3, EquipmentRole::Economy, false, false, 20, 2 },
            { "简易通讯机", 2, EquipmentRole::Economy, false, false, 20, 2 },
            { "见钱眼开玩偶", 1, EquipmentRole::Economy, false, false, 20, 2 },
            { "卫戍推荐信", 2, EquipmentRole::Unknown, false, false, 8, 2 },
            { "聚合急救箱", 2, EquipmentRole::Growth, false, false, 28, 2 },
            { "灵巧涡轮手套", 2, EquipmentRole::Growth, false, false, 28, 2 },
            { "叙拉古正装", 2, EquipmentRole::Growth, false, false, 28, 3 },
            { "精准狙击镜", 2, EquipmentRole::Growth, false, false, 28, 3 },
            { "炎国短刀", 2, EquipmentRole::Growth, false, false, 28, 3 },
            { "迅捷作战粮", 2, EquipmentRole::Growth, false, false, 28, 3 },
            { "阿戈尔重刃", 2, EquipmentRole::Growth, false, false, 28, 3 },
            { "奥术法阵", 1, EquipmentRole::Growth, false, false, 28, 3 },
            { "坚固维式重锤", 2, EquipmentRole::Growth, false, false, 28, 3 },
            { "加速维式重锤", 2, EquipmentRole::Growth, false, false, 28, 3 },
            { "突袭手雷", 1, EquipmentRole::Growth, false, false, 28, 3 },
            { "驮兽储蓄罐", 3, EquipmentRole::Economy, false, false, 20, 3 },
            { "乌萨斯呼吸器", 2, EquipmentRole::Growth, false, false, 28, 3 },
            { "多功能补剂", 2, EquipmentRole::Damage, false, false, 24, 3 },
            { "伊比利亚骨刺刀", 2, EquipmentRole::Growth, false, false, 28, 3 },
            { "定期存折", 2, EquipmentRole::Economy, false, false, 20, 3 },
            { "不退者之盾", 2, EquipmentRole::Unknown, false, false, 8, 3 },
            { "寻呼模块", 4, EquipmentRole::Economy, false, false, 20, 4 },
            { "蜂鸣器", 1, EquipmentRole::Damage, false, false, 24, 4 },
            { "伪装服", 1, EquipmentRole::Damage, false, false, 24, 4 },
            { "卡西米尔竞技旗", 2, EquipmentRole::Growth, false, false, 28, 4 },
            { "灼燃维式重锤", 2, EquipmentRole::Growth, false, false, 28, 4 },
            { "护盾无人机", 2, EquipmentRole::Defense, false, false, 22, 4 },
            { "M3茧甲", 2, EquipmentRole::Unknown, false, false, 8, 4 },
            { "莱塔尼亚小鼓", 2, EquipmentRole::Growth, false, false, 28, 4 },
            { "谢拉格不融冰", 2, EquipmentRole::Growth, false, false, 28, 4 },
            { "可靠怀表", 2, EquipmentRole::Damage, false, false, 24, 4 },
            { "一次性促销券", 2, EquipmentRole::Economy, false, false, 20, 4 },
            { "驱逐者警棍", 2, EquipmentRole::Damage, false, false, 24, 4 },
            { "动能单手弩", 2, EquipmentRole::Damage, false, false, 24, 4 },
            { "汲取者之喙", 2, EquipmentRole::Damage, false, false, 24, 4 },
            { "双模机械臂", 3, EquipmentRole::Damage, false, false, 24, 5 },
            { "信标", 3, EquipmentRole::Unknown, false, false, 8, 5 },
            { "拟态物质", 4, EquipmentRole::Economy, false, false, 20, 5 },
            { "博士投影", 5, EquipmentRole::Economy, false, false, 20, 5 },
            { "天马之盔", 3, EquipmentRole::Defense, false, false, 22, 5 },
            { "电流维式重锤", 2, EquipmentRole::Growth, false, false, 28, 5 },
            { "佣兵名牌", 2, EquipmentRole::Damage, false, false, 24, 5 },
            { "人事部记录", 3, EquipmentRole::Economy, false, false, 20, 5 },
            { "瓶装烈性反应", 2, EquipmentRole::Damage, false, false, 24, 5 },
            { "不退者力场", 3, EquipmentRole::Defense, false, false, 22, 5 },
            { "驱逐者瓦斯", 3, EquipmentRole::Damage, false, false, 24, 5 },
            { "投机骰子", 3, EquipmentRole::Economy, false, false, 20, 5 },
            { "天马之枪", 3, EquipmentRole::Damage, false, false, 50, 6 },
            { "人事部文档", 4, EquipmentRole::Economy, false, false, 20, 6 },
            { "变形同构体", 3, EquipmentRole::Unknown, false, false, 8, 6 },
            { "动能加速器", 3, EquipmentRole::Damage, false, false, 24, 6 },
            { "汲取者之心", 3, EquipmentRole::Damage, false, false, 24, 6 },
            { "终极维式重锤", 3, EquipmentRole::Growth, false, false, 28, 6 },
            { "不退者卫护系统", 6, EquipmentRole::Damage, false, false, 24, 6 },
            { "动能轰击弩", 6, EquipmentRole::Damage, false, false, 24, 6 },
            { "汲取者之镰", 6, EquipmentRole::Damage, false, false, 24, 6 },
            { "驱逐者防暴装备", 6, EquipmentRole::Damage, false, false, 24, 6 },
            { "天马之耀", 6, EquipmentRole::Damage, false, false, 24, 6 },
        };
        // Names associated with a pact do not imply layer gain. These roles
        // describe the actual printed effects, independent of transfer recipes.
        const std::vector<std::pair<std::string, EquipmentRole>> corrected_roles {
            { "不屈弹射器", EquipmentRole::Redeployment },  { "战栗维式重锤", EquipmentRole::Damage },
            { "萨尔贡浓茶", EquipmentRole::SkillRecovery }, { "卫戍推荐信", EquipmentRole::Growth },
            { "聚合急救箱", EquipmentRole::Sustain },       { "灵巧涡轮手套", EquipmentRole::Damage },
            { "叙拉古正装", EquipmentRole::Damage },        { "精准狙击镜", EquipmentRole::Damage },
            { "炎国短刀", EquipmentRole::Damage },          { "迅捷作战粮", EquipmentRole::SkillRecovery },
            { "阿戈尔重刃", EquipmentRole::Damage },        { "奥术法阵", EquipmentRole::Control },
            { "坚固维式重锤", EquipmentRole::Damage },      { "加速维式重锤", EquipmentRole::Damage },
            { "突袭手雷", EquipmentRole::Control },         { "乌萨斯呼吸器", EquipmentRole::Defense },
            { "多功能补剂", EquipmentRole::Growth },        { "伊比利亚骨刺刀", EquipmentRole::Damage },
            { "不退者之盾", EquipmentRole::Defense },       { "蜂鸣器", EquipmentRole::Defense },
            { "伪装服", EquipmentRole::Defense },           { "卡西米尔竞技旗", EquipmentRole::Damage },
            { "灼燃维式重锤", EquipmentRole::Damage },      { "护盾无人机", EquipmentRole::Sustain },
            { "M3茧甲", EquipmentRole::Defense },           { "莱塔尼亚小鼓", EquipmentRole::SkillRecovery },
            { "谢拉格不融冰", EquipmentRole::Control },     { "可靠怀表", EquipmentRole::SkillRecovery },
            { "驱逐者瓦斯", EquipmentRole::Control },       { "终极维式重锤", EquipmentRole::Damage }
        };
        for (auto& profile : profiles) {
            const auto facts =
                std::find_if(EquipmentCatalogue.begin(), EquipmentCatalogue.end(), [&](const auto& entry) {
                    return entry.name == profile.name;
                });
            if (facts != EquipmentCatalogue.end()) {
                profile.tier = facts->tier;
                profile.price = facts->price;
                profile.consumable = facts->consumed;
                profile.normal_effect = facts->normal;
                profile.advanced_effect = facts->advanced;
            }
            for (const auto& [name, role] : corrected_roles) {
                if (profile.name == name) {
                    profile.role = role;
                    break;
                }
            }
        }
        return profiles;
    }();
    return p;
}

asst::spa::EquipmentDecision
    asst::spa::choose_equipment(const EquipmentContext& context, const std::vector<EquipmentProfile>& offers)
{
    EquipmentDecision result;
    if (context.funds < 0 || context.round < 1 || context.boss_round < context.round || context.boss_round > 20 ||
        (!context.target_deployed && !context.free_supply) || offers.empty()) {
        result.reason = "Equipment context is incomplete";
        return result;
    }
    const int rounds_left = context.boss_round - context.round;
    int best_price = 0;
    for (const auto& offer : offers) {
        if (offer.name.empty() || offer.price < 0 || (offer.price == 0 && !context.free_supply) ||
            offer.price > context.funds) {
            continue;
        }
        if (!context.free_supply && context.shop_level > 0 && offer.tier > 0 && offer.tier > context.shop_level) {
            continue;
        }
        if (offer.name == "定期存折" && !context.free_supply &&
            !profitable_passbook_purchase(context.round, context.boss_round, offer.price)) {
            continue;
        }
        const bool profitable_toy = profitable_next_round_cash_offer(context, offer);
        if (is_deferred_cash_equipment(offer.name) && !context.free_supply && !profitable_toy) {
            continue;
        }
        bool executable_projection = false;
        if (offer.name == "博士投影" && !context.free_supply) {
            const auto normal = promotion_consumable_quote(offer.name, offer.normal_effect);
            const auto advanced = promotion_consumable_quote(offer.name, offer.advanced_effect);
            executable_projection = context.direct_promotion_usable && context.target_deployed &&
                                    !context.target_elite && rounds_left >= 1 && normal && advanced &&
                                    normal->effect == PromotionConsumableEffect::DirectPromotion &&
                                    normal->timing == PromotionConsumableTiming::NextRoundStart &&
                                    advanced->effect == PromotionConsumableEffect::DirectPromotion &&
                                    advanced->timing == PromotionConsumableTiming::OnEquip;
            if (!executable_projection) {
                continue;
            }
        }
        int score = offer.score;
        if (context.freeze_engine_assignment) {
            score += 25 + std::min(rounds_left, 5) * 7;
        }
        if (profitable_toy) {
            score += 15;
        }
        if (offer.name == "阿戈尔重刃" && context.target_is_carry && context.primary_active) {
            score += 60;
        }
        if (offer.role == EquipmentRole::Redeployment && !context.intentional_redeploy_loop) {
            score -= 80;
        }
        if (offer.role == EquipmentRole::Growth && offer.name != "随身身份牌" && !context.primary_active) {
            score -= 80;
        }
        if (offer.role == EquipmentRole::Damage && !context.target_is_carry) {
            score -= 15;
        }
        if (offer.role == EquipmentRole::Tradeoff && !context.healer_covers && !context.intentional_redeploy_loop) {
            score -= 100;
        }
        if (offer.requires_healing && !context.healer_covers && !context.intentional_redeploy_loop) {
            score -= 100;
        }
        if (offer.name == "源石溶剂" && context.intentional_redeploy_loop) {
            // The loss of HP is the trigger, not a drawback, for a Gravel /
            // Unyielding redeploy engine.  Prefer it over generic sustain
            // equipment when a deliberate growth-loop candidate is supplied.
            score += 90;
        }
        if (offer.role == EquipmentRole::Economy && rounds_left < 2 &&
            !(offer.name == "盟约之币" && rounds_left >= 1) && !profitable_toy && !executable_projection) {
            score -= 25;
        }
        if (offer.consumable && offer.role == EquipmentRole::Growth && offer.name != "随身身份牌" &&
            !context.primary_active) {
            score -= 50;
        }
        if (!result.buy || score > result.score || (score == result.score && offer.price < best_price)) {
            result = { score > 0 || context.free_supply,
                       offer.name,
                       offer.role,
                       score,
                       context.free_supply ? "Acquire free supply; assignment remains a separate verified decision"
                       : score > 0         ? "Buy only when target, funds, pact state and boss window are verified"
                                           : "Offer has no positive verified value in the current context" };
            best_price = offer.price;
        }
    }
    return result;
}
