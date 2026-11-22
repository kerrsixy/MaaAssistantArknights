#include "RoguelikeRecruitConfig.h"
#include "Config/Miscellaneous/BattleDataConfig.h"

#include <algorithm>
#include <meojson/json.hpp>
#include <ranges>

#include "Utils/Logger.hpp"

int asst::AccountModuleRecruitPolicy::adjust_priority(int current, bool promoting, bool available) const noexcept
{
    // Module evidence cannot revive an operator rejected by strength or team requirements.
    if (current <= 0) {
        return current;
    }
    if (available) {
        return std::max(current, promoting ? ready_promote_priority : ready_recruit_priority);
    }
    return std::min(current, promoting ? unavailable_promote_priority : unavailable_recruit_priority);
}

const asst::RoguelikeOperInfo&
    asst::RoguelikeRecruitConfig::get_oper_info(const std::string& theme, const std::string& oper_name) noexcept
{
    auto& opers = m_all_opers.at(theme);
    const auto& it = std::ranges::find_if(opers, [&](const auto& pair) { return pair.first.name == oper_name; });
    if (it != opers.end()) {
        return it->second;
    }

    const auto& role = BattleData.get_first_role(oper_name);
    battle::OperNameTag oper_tag { .role = role, .name = oper_name };
    RoguelikeOperInfo info;
    info.role = role;
    info.name = oper_name;
    info.group_id = get_group_ids_of_oper(theme, oper_name);
    opers.emplace(oper_tag, std::move(info));
    return opers.at(oper_tag);
}

const asst::RoguelikeOperInfo&
    asst::RoguelikeRecruitConfig::get_oper_info(const std::string& theme, const battle::OperNameTag& oper_tag) noexcept
{
    auto& opers = m_all_opers.at(theme);
    if (opers.contains(oper_tag)) {
        return opers.at(oper_tag);
    }
    const auto& it = std::ranges::find_if(opers, [&](const auto& pair) {
        return (pair.first.role == battle::Role::Unknown || pair.first.role == oper_tag.role) &&
               pair.first.name == oper_tag.name;
    }); // 配置中要求的为 unk 或者与实际干员职业相等
    if (it != opers.end()) {
        return it->second;
    }
    else {
        RoguelikeOperInfo info;
        info.role = oper_tag.role;
        info.name = oper_tag.name;
        info.group_id = get_group_ids_of_oper(theme, oper_tag.name);
        opers.emplace(oper_tag, std::move(info));
        return opers.at(oper_tag);
    }
}

const asst::RoguelikeGroupInfo&
    asst::RoguelikeRecruitConfig::get_group_info(const std::string& theme, const std::string& group_name) noexcept
{
    auto& groups = m_all_groups.at(theme);
    if (groups.contains(group_name)) {
        return groups.at(group_name);
    }
    else {
        RoguelikeGroupInfo info;
        info.name = group_name;
        groups.emplace(group_name, std::move(info));
        return groups.at(group_name);
    }
}

const std::vector<std::string> asst::RoguelikeRecruitConfig::get_group_names(const std::string& theme) const noexcept
{
    return m_oper_groups.at(theme);
}

const std::vector<asst::RecruitPriorityOffset>
    asst::RoguelikeRecruitConfig::get_team_complete_info(const std::string& theme) const noexcept
{
    return m_team_complete_condition.at(theme);
}

std::string
    asst::RoguelikeRecruitConfig::get_opening_roles(const std::string& theme, const std::string& squad) const noexcept
{
    if (auto theme_it = m_squad_opening_roles.find(theme); theme_it != m_squad_opening_roles.end()) {
        if (auto squad_it = theme_it->second.find(squad); squad_it != theme_it->second.end()) {
            return squad_it->second;
        }
    }
    if (auto it = m_default_opening_roles.find(theme); it != m_default_opening_roles.end()) {
        return it->second;
    }
    return {};
}

int asst::RoguelikeRecruitConfig::get_opening_role_bonus(const std::string& theme) const noexcept
{
    if (auto it = m_opening_role_bonus.find(theme); it != m_opening_role_bonus.end()) {
        return it->second;
    }
    return 500;
}

std::string asst::RoguelikeRecruitConfig::get_default_squad(const std::string& theme) const noexcept
{
    if (auto it = m_default_squad.find(theme); it != m_default_squad.end()) {
        return it->second;
    }
    return {};
}

std::vector<int> asst::RoguelikeRecruitConfig::get_group_ids_of_oper(
    const std::string& theme,
    const std::string& oper_name) const noexcept
{
    return get_group_ids_of_oper(theme, battle::OperNameTag { battle::Role::Unknown, oper_name });
}

std::vector<int> asst::RoguelikeRecruitConfig::get_group_ids_of_oper(
    const std::string& theme,
    const battle::OperNameTag& oper_tag) const noexcept
{
    auto& opers = m_all_opers.at(theme);

    if (oper_tag.role != battle::Role::Unknown) {
        const auto& find_iter = std::ranges::find_if(opers, [&](const auto& pair) { return pair.first == oper_tag; });
        if (find_iter != opers.end()) {
            return find_iter->second.group_id;
        }
    }

    const auto& find_iter =
        std::ranges::find_if(opers, [&](const auto& pair) { return pair.first.name == oper_tag.name; });
    if (find_iter != opers.end()) {
        return find_iter->second.group_id;
    }
    const auto role = oper_tag.role != battle::Role::Unknown ? oper_tag.role : BattleData.get_first_role(oper_tag.name);
    auto location = BattleData.get_location_type(role, oper_tag.name);
    if (location != battle::LocationType::Melee && location != battle::LocationType::Ranged) {
        location = battle::get_role_usual_location(role);
    }
    // Resolve existing canonical fallback identities rather than list positions.
    // Phantom retains its legacy ranged alias; explicit memberships above win.
    const auto& groups = m_all_groups.at(theme);
    for (const auto name : { "其他地面", "其他高台", "高台预备" }) {
        const bool melee = std::string_view(name) == "其他地面";
        if ((location == battle::LocationType::Melee) != melee) {
            continue;
        }
        if (const auto found = groups.find(name); found != groups.end()) {
            return { found->second.id };
        }
    }
    LogWarn << __FUNCTION__ << "Missing canonical fallback group" << theme << oper_tag.name;
    return {};
}

int asst::RoguelikeRecruitConfig::get_group_id_from_name(
    const std::string& theme,
    const std::string& group_name) noexcept
{
    auto& groups = m_all_groups.at(theme);
    if (groups.contains(group_name)) {
        return groups.at(group_name).id;
    }
    else {
        RoguelikeGroupInfo info;
        info.name = group_name;
        groups.emplace(group_name, std::move(info));
        return groups.at(group_name).id;
    }
}

const std::string
    asst::RoguelikeRecruitConfig::get_group_name_from_id(const std::string& theme, const int group_id) const noexcept
{
    auto it =
        std::ranges::find_if(m_all_groups.at(theme), [&](const auto& group) { return group.second.id == group_id; });
    if (it != m_all_groups.at(theme).end()) {
        return it->first;
    }
    return "";
}

bool asst::RoguelikeRecruitConfig::parse(const json::value& json)
{
    LogTraceFunction;

    // 肉鸽名字
    const std::string theme = json.at("theme").as_string();
    clear(theme);
    if (const auto excluded = json.find<json::array>("excluded_opers")) {
        for (const auto& name : *excluded) {
            if (!name.is_string() || name.as_string().empty()) {
                LogError << "Invalid roguelike excluded operator" << theme;
                return false;
            }
            m_excluded_opers[theme].emplace(name.as_string());
        }
    }

    if (auto opening = json.find<json::object>("opening")) {
        m_default_opening_roles[theme] = opening->get("default_roles", std::string {});
        // 开局阶段未覆盖职业的加分，0 关闭（退化为纯 start_priority）。
        m_opening_role_bonus[theme] = opening->get("role_diversity_bonus", 500);
        // 未显式选择分队时使用哪个分队（玩法策略，全在资源里）
        m_default_squad[theme] = opening->get("default_squad", std::string {});
        if (auto squad_roles = opening->find<json::object>("squad_roles")) {
            for (const auto& [squad, roles] : *squad_roles) {
                if (roles.is_string()) {
                    m_squad_opening_roles[theme][squad] = roles.as_string();
                }
            }
        }
    }

    int group_id = 0;
    //{"name":干员组名, "opers":组内干员组成的array}
    for (const auto& group_json : json.at("priority").as_array()) {
        const std::string group_name = group_json.at("name").as_string();
        m_oper_groups[theme].emplace_back(group_name);

        // 干员组信息
        RoguelikeGroupInfo group_info;
        group_info.name = group_name;
        group_info.id = group_id;

        // 干员在组内的顺序,int
        int order_in_group = 0;
        // 遍历"opers"数组
        for (const auto& oper_json : group_json.at("opers").as_array()) {
            battle::Role role = battle::Role::Unknown;
            std::string name = oper_json.at("name").as_string();
            if (auto opt = oper_json.find<std::string>("role")) {
                auto role_str = opt.value();
                role = battle::parse_role_type(role_str, battle::Role::Unknown);
            }
            else {
                const auto& roles = BattleData.get_roles(name);
                if (roles.size() == 1) {
                    role = *roles.cbegin();
                }
                else if (roles.size() > 1) {
                    LogWarn << __FUNCTION__ << "oper:" << name << "has multiple roles, but still not specific any";
                }
            }
            group_info.opers.emplace(name);
            // 肉鸽干员招募信息
            RoguelikeOperInfo oper_info;
            auto iter = m_all_opers[theme].find({ role, name });
            if (iter != m_all_opers[theme].end()) {
                // 干员已存在时仅做更新
                oper_info = iter->second;
            }
            oper_info.name = name;
            oper_info.group_id.push_back(group_id);
            oper_info.order_in_group[group_id] = order_in_group;
            oper_info.recruit_priority = oper_json.get("recruit_priority", oper_info.recruit_priority);
            // 开局优先级是同名干员的全局属性。干员可出现在多个策略组，
            // 不应由 JSON 的后出现顺序覆盖更高的开局分；取各组声明的最大值。
            const int declared_start_priority = oper_json.get("start_priority", oper_info.recruit_priority);
            oper_info.start_priority = std::max(oper_info.start_priority, declared_start_priority);
            oper_info.promote_priority = oper_json.get("promote_priority", oper_info.promote_priority);
            oper_info.is_alternate = oper_json.get("is_alternate", oper_info.is_alternate);
            oper_info.skill = oper_json.get("skill", oper_info.skill);
            oper_info.alternate_skill = oper_json.get("alternate_skill", oper_info.alternate_skill);
            oper_info.skill_usage =
                static_cast<battle::SkillUsage>(oper_json.get("skill_usage", static_cast<int>(oper_info.skill_usage)));
            oper_info.skill_times = oper_json.get("skill_times", oper_info.skill_times);
            oper_info.alternate_skill_usage = static_cast<battle::SkillUsage>(
                oper_json.get("alternate_skill_usage", static_cast<int>(oper_info.alternate_skill_usage)));
            oper_info.alternate_skill_times = oper_json.get("alternate_skill_times", oper_info.alternate_skill_times);
            oper_info.is_key = oper_json.get("is_key", oper_info.is_key);
            oper_info.is_start = oper_json.get("is_start", oper_info.is_start);
            oper_info.auto_retreat = oper_json.get("auto_retreat", oper_info.auto_retreat);
            if (const auto module = oper_json.find<json::object>("account_module")) {
                AccountModuleRecruitPolicy policy;
                policy.module_id = module->get("module_id", "");
                policy.minimum_level = module->get("minimum_level", 3);
                policy.ready_recruit_priority = module->get("ready_recruit_priority", oper_info.recruit_priority);
                policy.ready_promote_priority = module->get("ready_promote_priority", oper_info.promote_priority);
                policy.unavailable_recruit_priority = module->get("unavailable_recruit_priority", 0);
                policy.unavailable_promote_priority = module->get("unavailable_promote_priority", 0);
                const auto valid_priority = [](int value) {
                    return value >= 0 && value <= 1000;
                };
                if (policy.module_id.empty() || policy.minimum_level < 1 || policy.minimum_level > 3 ||
                    !valid_priority(policy.ready_recruit_priority) || !valid_priority(policy.ready_promote_priority) ||
                    !valid_priority(policy.unavailable_recruit_priority) ||
                    !valid_priority(policy.unavailable_promote_priority)) {
                    LogError << __FUNCTION__ << "Invalid account module recruitment policy" << theme << name;
                    return false;
                }
                oper_info.account_module = std::move(policy);
            }

            // __________________will-be-removed-begin__________________
            oper_info.recruit_priority_when_team_full =
                oper_json.get("recruit_priority_when_team_full", oper_info.recruit_priority - 100);
            oper_info.promote_priority_when_team_full =
                oper_json.get("promote_priority_when_team_full", oper_info.promote_priority + 300);
            // __________________will-be-removed-end__________________

            if (auto opt = oper_json.find<json::array>("recruit_priority_offsets")) {
                for (const auto& offset_json : opt.value()) {
                    RecruitPriorityOffset offset;
                    for (const auto& group : offset_json.at("groups").as_array()) {
                        offset.groups.emplace_back(group.as_string());
                    }
                    offset.threshold = offset_json.get("threshold", 0);
                    offset.is_less = offset_json.get("is_less", false);
                    offset.offset = offset_json.get("offset", 0);
                    oper_info.recruit_priority_offsets.emplace_back(std::move(offset));
                }
            }

            if (auto opt = oper_json.find<json::array>("collection_priority_offsets")) {
                for (const auto& offset_json : opt.value()) {
                    CollectionPriorityOffset offset;
                    offset.collection = offset_json.get("collection", "");
                    offset.offset = offset_json.get("offset", 0);
                    oper_info.collection_priority_offsets.emplace_back(std::move(offset));
                }
            }

            m_all_opers[theme][{ role, name }] = std::move(oper_info);
            order_in_group++;
        }
        m_all_groups[theme][group_name] = std::move(group_info);
        group_id++;
    }

    // 对所有存在 offset 组的干员进行初始化
    for (auto& [oper_name, oper_info] : m_all_opers[theme]) { // 所有干员
        if (oper_info.recruit_priority_offsets.empty()) {
            continue;
        }
        for (auto& offset : oper_info.recruit_priority_offsets) { // 干员的所有 offset 策略组
            for (auto& group : offset.groups) {                   // 策略组内的每个干员组
                offset.opers.insert( // 向当前策略组计入这个干员组的无重复干员
                    get_group_info(theme, group).opers.begin(),
                    get_group_info(theme, group).opers.end());
            }
        }
    }

    for (const auto& condition_json : json.at("team_complete_condition").as_array()) {
        RecruitPriorityOffset condition;
        for (const auto& group : condition_json.at("groups").as_array()) {
            std::string group_name = group.as_string();
            condition.groups.emplace_back(group_name);

            condition.opers.insert( // 向当前策略组计入这个干员组的无重复干员
                get_group_info(theme, group_name).opers.begin(),
                get_group_info(theme, group_name).opers.end());
        }
        condition.threshold = condition_json.at("threshold").as_integer();
        m_team_complete_require[theme] += condition.threshold;
        m_team_complete_condition[theme].emplace_back(std::move(condition));
    }

    return true;
}

void asst::RoguelikeRecruitConfig::clear(const std::string& theme)
{
    m_excluded_opers.erase(theme);
    m_all_opers.erase(theme);
    m_all_groups.erase(theme);
    m_oper_groups.erase(theme);
    m_team_complete_condition.erase(theme);
    m_team_complete_require.erase(theme);
    m_default_opening_roles.erase(theme);
    m_squad_opening_roles.erase(theme);
    m_opening_role_bonus.erase(theme);
    m_default_squad.erase(theme);
}
