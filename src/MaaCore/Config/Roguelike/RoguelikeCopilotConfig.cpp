#include "RoguelikeCopilotConfig.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

#include "Task/Roguelike/BlackFlow/BlackFlowBattleVariant.h"
#include <meojson/json.hpp>

#include "Utils/Logger.hpp"

using namespace asst::battle;
using namespace asst::battle::roguelike;

bool asst::RoguelikeCopilotConfig::load(const std::filesystem::path& path)
{
    LogTraceFunction;

    bool ret = true;
    auto previous_stages = m_stage_data;
    auto previous_openings = m_hunter_openings;
    auto previous_reservations = m_reserved_core_groups;
    using StageMap = decltype(m_stage_data);
    using OpeningMap = decltype(m_hunter_openings);
    using ReservationMap = decltype(m_reserved_core_groups);

    struct LoadRollback
    {
        StageMap& stages;
        StageMap& old_stages;
        OpeningMap& openings;
        OpeningMap& old_openings;
        ReservationMap& reservations;
        ReservationMap& old_reservations;
        bool accepted = false;

        ~LoadRollback()
        {
            Logger::level::trace.set_enabled(true);
            if (!accepted) {
                stages.swap(old_stages);
                openings.swap(old_openings);
                reservations.swap(old_reservations);
            }
        }
    } rollback { m_stage_data,      previous_stages,        m_hunter_openings,
                 previous_openings, m_reserved_core_groups, previous_reservations };

    std::unordered_map<std::string, int> batch_names;
    std::unordered_set<std::string> referenced_bases;
    Logger::level::trace.set_enabled(false);
    for (auto& entry : std::filesystem::recursive_directory_iterator(path)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        if (const auto document = json::open(entry.path()); document && document->is_object()) {
            const auto name = document->find<std::string>("stage_name");
            if (name) {
                ++batch_names[*name];
                if (const auto variant = blackflow::parse_battle_variant_key(*name)) {
                    referenced_bases.emplace(variant->first);
                }
            }
        }
        ret &= AbstractConfig::load(entry.path());
    }
    Logger::level::trace.set_enabled(true);
    // Existing variants also constrain a later batch that overlays only bases.
    for (const auto& [key, data] : m_stage_data) {
        if (const auto condition = blackflow::parse_battle_variant_key(key)) {
            referenced_bases.emplace(condition->first);
        }
    }
    for (const auto& [name, count] : batch_names) {
        if (count > 1 && (referenced_bases.contains(name) || name.find("#total") != std::string::npos)) {
            LogError << __FUNCTION__ << "Duplicate variant or referenced base in one load batch" << name;
            ret = false;
        }
    }
    for (const auto& [key, variant] : m_stage_data) {
        const auto condition = blackflow::parse_battle_variant_key(key);
        if (!condition) {
            continue;
        }
        const auto base = m_stage_data.find(condition->first);
        if (base == m_stage_data.end() || base->second.deploy_plan_only != variant.deploy_plan_only ||
            base->second.reserve_deploy_cost != variant.reserve_deploy_cost ||
            reserved_core_groups(base->first) != reserved_core_groups(key)) {
            LogError << __FUNCTION__ << "Variant base or deployment budget rules conflict" << key;
            ret = false;
        }
    }
    // The guard also restores registry and trace state while DEBUG parse
    // exceptions unwind; the original exception is deliberately not caught.
    rollback.accepted = ret;
    return ret;
}

std::optional<CombatData> asst::RoguelikeCopilotConfig::get_stage_data(const std::string& stage_name) const
{
    auto it = m_stage_data.find(stage_name);
    if (it == m_stage_data.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::pair<int, std::string>>
    asst::RoguelikeCopilotConfig::get_stage_variant_keys(const std::string& base_stage) const
{
    std::vector<std::pair<int, std::string>> result;
    for (const auto& [key, data] : m_stage_data) {
        if (const auto condition = blackflow::parse_battle_variant_key(key);
            condition && condition->first == base_stage) {
            result.emplace_back(condition->second, key);
        }
    }
    std::ranges::sort(result);
    return result;
}

const std::vector<std::string>& asst::RoguelikeCopilotConfig::reserved_core_groups(const std::string& stage_name) const
{
    static const std::vector<std::string> empty;
    const auto it = m_reserved_core_groups.find(stage_name);
    return it == m_reserved_core_groups.end() ? empty : it->second;
}

std::optional<asst::blackflow::HunterOpening>
    asst::RoguelikeCopilotConfig::get_hunter_opening(const std::string& stage_name) const
{
    const auto it = m_hunter_openings.find(stage_name);
    return it == m_hunter_openings.end() ? std::nullopt : std::optional(it->second);
}

bool asst::RoguelikeCopilotConfig::parse(const json::value& json)
{
#ifdef ASST_DEBUG
    LogTraceFunction;
#endif

    std::string stage_name = json.at("stage_name").as_string();
    if (stage_name.find("#total") != std::string::npos || json.contains("variant_of") || json.contains("when")) {
        const auto condition = blackflow::parse_battle_variant_key(stage_name);
        const auto base = json.find<std::string>("variant_of");
        const auto when = json.find<json::object>("when");
        const auto plans = json.find<json::array>("deploy_plan");
        const auto only = json.find<bool>("deploy_plan_only");
        const auto reserve = json.find<bool>("reserve_deploy_cost");
        const auto groups = json.find<json::array>("reserve_starting_core_groups");
        if (!condition || !base || *base != condition->first || !when || when->size() != 1 ||
            !when->contains("total_enemies") || !when->at("total_enemies").is_number() ||
            when->at("total_enemies").as_double() != condition->second || !plans || plans->empty() || !only ||
            !reserve || !groups) {
            LogError << __FUNCTION__ << "Invalid explicit battle variant declaration" << stage_name;
            return false;
        }
        const std::unordered_set<std::string> allowed { "stage_name",
                                                        "variant_of",
                                                        "when",
                                                        "deploy_plan",
                                                        "deploy_plan_only",
                                                        "reserve_deploy_cost",
                                                        "reserve_starting_core_groups",
                                                        "doc" };
        for (const auto& [key, value] : json.as_object()) {
            if (!allowed.contains(key)) {
                LogError << __FUNCTION__ << "Variant may override only deploy_plan" << stage_name << key;
                return false;
            }
        }
    }
    CombatData data;
    data.stage_name = stage_name;
    // clang-format off
    static const std::unordered_map<std::string, DeployDirection> DeployDirectionMapping = {
        { "Right", DeployDirection::Right }, { "RIGHT", DeployDirection::Right },
        { "right", DeployDirection::Right }, { "右", DeployDirection::Right },

        { "Left", DeployDirection::Left },   { "LEFT", DeployDirection::Left },
        { "left", DeployDirection::Left },   { "左", DeployDirection::Left },

        { "Up", DeployDirection::Up },       { "UP", DeployDirection::Up },
        { "up", DeployDirection::Up },       { "上", DeployDirection::Up },

        { "Down", DeployDirection::Down },   { "DOWN", DeployDirection::Down },
        { "down", DeployDirection::Down },   { "下", DeployDirection::Down },

        { "None", DeployDirection::None },   { "NONE", DeployDirection::None },
        { "none", DeployDirection::None },   { "无", DeployDirection::None },
    }; // clang-format on
    if (auto opt = json.find<json::array>("replacement_home")) {
        for (auto& point : opt.value()) {
            ReplacementHome home;
            home.location = Point(point["location"][0].as_integer(), point["location"][1].as_integer());
            const std::string& direction_str = point.get("direction", "none");
            if (auto iter = DeployDirectionMapping.find(direction_str); iter != DeployDirectionMapping.end()) {
                home.direction = iter->second;
            }
            else {
                home.direction = DeployDirection::None;
            }
            data.replacement_home.emplace_back(std::move(home));
        }
    }
    if (auto opt = json.find<json::array>("blacklist_location")) {
        for (auto& point : opt.value()) {
            data.blacklist_location.emplace(point[0].as_integer(), point[1].as_integer());
        }
    }
    data.use_dice_stage = !json.get("not_use_dice", false);
    data.deploy_plan_only = json.get("deploy_plan_only", false);
    data.reserve_deploy_cost = json.get("reserve_deploy_cost", false);
    std::vector<std::string> reserved_core_groups;
    if (json.contains("reserve_starting_core_groups")) {
        const auto& groups = json.at("reserve_starting_core_groups");
        if (!groups.is_array()) {
            LogError << __FUNCTION__ << "Core reservation groups must be an array";
            return false;
        }
        for (const auto& group : groups.as_array()) {
            if (!group.is_string() || group.as_string().empty()) {
                LogError << __FUNCTION__ << "Core reservation group must be a nonempty string";
                return false;
            }
            reserved_core_groups.push_back(group.as_string());
        }
    }

    if (auto opt = json.find<json::value>("force_air_defense_when_deploy_blocking_num")) {
        data.stop_deploy_blocking_num = opt.value().get("melee_num", INT_MAX);
        data.force_deploy_air_defense_num = opt.value().get("air_defense_num", 0);
        if (data.force_deploy_air_defense_num == 0) {
            data.stop_deploy_blocking_num = INT_MAX;
        }
        data.force_ban_medic = opt.value().get("ban_medic", false);
    }
    else {
        data.stop_deploy_blocking_num = INT_MAX;
        data.force_deploy_air_defense_num = 0;
        data.force_ban_medic = false;
    }

    constexpr int RoleNumber = 9;
    static constexpr std::array<Role, RoleNumber> RoleOrder = {
        Role::Warrior, Role::Pioneer, Role::Medic,   Role::Tank,  Role::Sniper,
        Role::Caster,  Role::Support, Role::Special, Role::Drone,
    };

    if (auto opt = json.find<json::array>("role_order")) {
        const auto& raw_roles = opt.value();
        using std::views::filter, std::views::transform;
        std::unordered_set<Role> specified_role;
        std::vector<Role> role_order;
        bool is_legal = true;
        if (std::ranges::find_if_not(raw_roles | std::views::all, std::mem_fn(&json::value::is_string)) !=
            raw_roles.end()) {
            Log.error("Role should be string");
            return false;
        }
        auto roles = raw_roles | filter(&json::value::is_string) | transform(&json::value::as_string) |
                     transform([&](std::string name) {
                         utils::tolowers(name);
                         return name;
                     });
        for (const std::string& role_name : roles) {
            const auto role = get_role_type(role_name);
            if (role == Role::Unknown) [[unlikely]] {
                Log.error("Unknown Role:", role_name);
                is_legal = false;
                break;
            }
            if (specified_role.contains(role)) [[unlikely]] {
                Log.error("Duplicated Role:", role_name);
                is_legal = false;
                break;
            }
            specified_role.emplace(role);
            role_order.emplace_back(role);
        }
        if (is_legal) [[likely]] {
            std::ranges::copy(
                RoleOrder | filter([&](Role role) { return !specified_role.contains(role); }),
                std::back_inserter(role_order));
            if (role_order.size() != RoleNumber) [[unlikely]] {
                Log.error("Unexpected role_order size:", role_order.size());
                return false;
            }
            std::ranges::move(role_order, data.role_order.begin());
        }
        else {
            Log.error("Illegal role_order detected");
            return false;
        }
    }
    else {
        data.role_order = RoleOrder;
    }

    if (auto opt = json.find<json::array>("force_deploy_direction")) {
        for (auto& point : opt.value()) {
            ForceDeployDirection fd_dir;
            Point location = Point(point["location"][0].as_integer(), point["location"][1].as_integer());
            const std::string& direction_str = point.get("direction", "none");
            if (auto iter = DeployDirectionMapping.find(direction_str); iter != DeployDirectionMapping.end()) {
                fd_dir.direction = iter->second;
            }
            else {
                fd_dir.direction = DeployDirection::None;
            }
            if (fd_dir.direction == DeployDirection::None) [[unlikely]] {
                Log.error("Unknown direction");
                return false;
            }
            std::unordered_set<Role> fd_role;
            for (auto& role_name : point["role"].as_array()) {
                auto role_name_str = role_name.as_string();
                const auto role = battle::parse_role_type(role_name_str, battle::Role::Unknown);
                if (role == Role::Unknown) [[unlikely]] {
                    LogError << __FUNCTION__ << " Unknown role name:" << role_name_str;
                    return false;
                }
                fd_role.emplace(role);
            }
            fd_dir.role = std::move(fd_role);
            const auto operator_names = point.get("operator_names", std::vector<std::string> {});
            fd_dir.operator_names.insert(operator_names.begin(), operator_names.end());
            data.force_deploy_direction.emplace(location, fd_dir);
        }
    }

    if (auto opt = json.find<json::array>("deploy_plan")) {
        int rank = 1;
        int plan_index = 0;
        for (auto& deploy_info : opt.value()) {
            DeployInfoWithRank info;
            info.plan_index = plan_index++;
            info.max_deployments = deploy_info.get("max_deployments", INT_MAX);
            if (info.max_deployments < 1) {
                LogError << __FUNCTION__ << "Deployment limit must be positive" << stage_name;
                return false;
            }
            info.minimum_elite = deploy_info.get("minimum_elite", 0);
            if (info.minimum_elite < 0 || info.minimum_elite > 2) {
                LogError << __FUNCTION__ << "Invalid deployment elite requirement" << stage_name;
                return false;
            }
            info.operator_names = deploy_info.get("operator_names", std::vector<std::string> {});
            info.excluded_operator_names = deploy_info.get("excluded_operator_names", std::vector<std::string> {});
            info.exclude_starting_core = deploy_info.get("exclude_starting_core", false);
            if (deploy_info.contains("prefer_group_order") && !deploy_info.at("prefer_group_order").is<bool>()) {
                LogError << __FUNCTION__ << "Invalid deployment group-order preference" << stage_name;
                return false;
            }
            info.prefer_group_order = deploy_info.get("prefer_group_order", false);
            if (deploy_info.contains("require_persistent_blocking") &&
                !deploy_info.at("require_persistent_blocking").is<bool>()) {
                LogError << __FUNCTION__ << "Invalid persistent blocking requirement" << stage_name;
                return false;
            }
            info.require_persistent_blocking = deploy_info.get("require_persistent_blocking", false);
            if (deploy_info.contains("minimum_base_block_count")) {
                const auto& minimum = deploy_info.at("minimum_base_block_count");
                if (!minimum.is_number() || !std::isfinite(minimum.as_double()) || minimum.as_double() < 0 ||
                    minimum.as_double() > std::numeric_limits<int>::max() ||
                    std::floor(minimum.as_double()) != minimum.as_double()) {
                    LogError << __FUNCTION__ << "Invalid base blocking requirement" << stage_name;
                    return false;
                }
                info.minimum_base_block_count = static_cast<int>(minimum.as_double());
            }
            const auto read_tile_locations = [&](const std::string& key, std::vector<Point>& result) {
                if (!deploy_info.contains(key)) {
                    return true;
                }
                const auto values = deploy_info.find<json::array>(key);
                if (!values) {
                    return false;
                }
                for (const auto& value : *values) {
                    if (!value.is_array() || value.as_array().size() != 2 || !value[0].is<int>() ||
                        !value[1].is<int>() || value[0].as_integer() < 0 || value[1].as_integer() < 0) {
                        return false;
                    }
                    const Point point(value[0].as_integer(), value[1].as_integer());
                    if (std::ranges::find(result, point) == result.end()) {
                        result.push_back(point);
                    }
                }
                return true;
            };
            if (!read_tile_locations("required_attack_tiles", info.required_attack_tiles) ||
                !read_tile_locations("required_range_tiles", info.required_range_tiles) ||
                !read_tile_locations("preferred_attack_tiles", info.preferred_attack_tiles) ||
                !read_tile_locations("exclusive_locations", info.exclusive_locations)) {
                LogError << __FUNCTION__ << "Invalid deployment tile constraints" << stage_name;
                return false;
            }
            info.location = Point(deploy_info["location"][0].as_integer(), deploy_info["location"][1].as_integer());
            const std::string& direction_str = deploy_info.get("direction", "none");
            if (auto iter = DeployDirectionMapping.find(direction_str); iter != DeployDirectionMapping.end()) {
                info.direction = iter->second;
            }
            if (auto condition = deploy_info.find<json::array>("condition")) {
                info.kill_lower_bound = condition.value()[0].as_integer();
                info.kill_upper_bound = condition.value()[1].as_integer();
            }
            for (auto& group : deploy_info.at("groups").as_array()) {
                std::string group_name = group.as_string();
                info.rank = rank;
                rank++;
                if (data.deploy_plan.contains(group_name)) {
                    data.deploy_plan[group_name].emplace_back(info);
                }
                else {
                    data.deploy_plan[group_name] = std::vector<DeployInfoWithRank> {};
                    data.deploy_plan[group_name].emplace_back(info);
                }
            }
        }
    }

    std::optional<blackflow::HunterOpening> hunter_opening;
    if (json.contains("hunter_bait_plan") || json.contains("hunter_bait_plans")) {
        std::string error;
        hunter_opening = blackflow::HunterOpening::parse(json, &error);
        if (!hunter_opening) {
            LogError << __FUNCTION__ << "Invalid hunter opening plan" << stage_name << error;
            return false;
        }
        data.hunter_bait_plan = hunter_opening->bait_plans.front().plan_index;
    }

    if (auto opt = json.find<json::array>("retreat_plan")) {
        for (auto& retreat_info : opt.value()) {
            DeployInfoWithRank info;
            info.location = Point(retreat_info["location"][0].as_integer(), retreat_info["location"][1].as_integer());
            if (auto condition = retreat_info.find<json::array>("condition")) {
                info.kill_lower_bound = condition.value()[0].as_integer();
                info.kill_upper_bound = condition.value()[1].as_integer();
            }
            data.retreat_plan.emplace_back(info);
        }
    }

    if (hunter_opening) {
        m_hunter_openings.insert_or_assign(stage_name, std::move(*hunter_opening));
    }
    else {
        m_hunter_openings.erase(stage_name);
    }
    if (reserved_core_groups.empty()) {
        m_reserved_core_groups.erase(stage_name);
    }
    else {
        m_reserved_core_groups.insert_or_assign(stage_name, std::move(reserved_core_groups));
    }
    m_stage_data.insert_or_assign(std::move(stage_name), std::move(data));
    return true;
}
