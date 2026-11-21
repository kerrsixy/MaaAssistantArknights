#include "RoguelikeShoppingConfig.h"

#include <algorithm>
#include <utility>

#include <meojson/json.hpp>

#include "Config/Roguelike/BlackFlow/BlackFlowScrapMarketConfig.h"
#include "Utils/Logger.hpp"

bool asst::RoguelikeShoppingConfig::parse_goods(const json::array& json, std::vector<RoguelikeGoods>& goods)
{
    static const std::unordered_map<std::string, battle::Role> RoleMap = {
        { "CASTER", battle::Role::Caster }, { "MEDIC", battle::Role::Medic },     { "PIONEER", battle::Role::Pioneer },
        { "SNIPER", battle::Role::Sniper }, { "SPECIAL", battle::Role::Special }, { "SUPPORT", battle::Role::Support },
        { "TANK", battle::Role::Tank },     { "WARRIOR", battle::Role::Warrior },
    };
    for (const auto& goods_json : json) {
        RoguelikeGoods item;
        item.name = goods_json.at("name").as_string();
        if (item.name.empty()) {
            LogError << __FUNCTION__ << "Shopping item name must not be empty";
            return false;
        }
        if (const auto roles = goods_json.find_value("roles")) {
            if (!roles->is<std::vector<std::string>>()) {
                LogError << __FUNCTION__ << "Shopping roles must be an array of strings" << item.name;
                return false;
            }
            for (const auto& role : roles->as_array()) {
                const auto found = RoleMap.find(role.as_string());
                if (found == RoleMap.end()) {
                    LogError << __FUNCTION__ << "Unknown shopping role" << role.as_string();
                    return false;
                }
                item.roles.emplace_back(found->second);
            }
        }
        if (const auto chars = goods_json.find_value("chars")) {
            if (!chars->is<std::vector<std::string>>()) {
                LogError << __FUNCTION__ << "Shopping chars must be an array of strings" << item.name;
                return false;
            }
            item.chars = chars->as<std::vector<std::string>>();
        }
        item.promotion = goods_json.get("promotion", 0);
        item.promotion_rarity = goods_json.get("promotion_rarity", 6);
        item.no_longer_buy = goods_json.get("no_longer_buy", false);
        item.ignore_no_longer_buy = goods_json.get("ignore_no_longer_buy", false);
        item.decrease_collapse = goods_json.get("decrease_collapse", false);
        // 招募券门槛：is_recruit_ticket + min_hope，缺省时行为与旧版完全一致
        item.is_recruit_ticket = goods_json.get(
            "is_recruit_ticket",
            item.name.find("招募券") != std::string::npos || item.name.find("人事调度函") != std::string::npos);
        item.min_hope = goods_json.get("min_hope", 0);
        if (const auto role = goods_json.find<std::string>("recruit_role")) {
            const auto found = RoleMap.find(*role);
            if (found == RoleMap.end()) {
                LogError << __FUNCTION__ << "Unknown recruitment voucher role" << *role;
                return false;
            }
            item.recruit_role = found->second;
            item.recruit_role_target = goods_json.get("recruit_role_target", 2);
            if (item.recruit_role_target < 1) {
                LogError << __FUNCTION__ << "Recruitment role target must be positive" << item.name;
                return false;
            }
        }
        if (const auto price = goods_json.find<int>("price")) {
            if (*price <= 0) {
                LogError << __FUNCTION__ << "Shopping item price must be positive" << item.name;
                return false;
            }
            item.price = *price;
        }
        goods.emplace_back(std::move(item));
    }
    return true;
}

bool asst::RoguelikeShoppingConfig::parse(const json::value& json)
{
    LogTraceFunction;

    const std::string theme = json.at("theme").as_string();
    const bool strategy_shopping = strategy_shopping_enabled(theme);
    if (!strategy_shopping) {
        return parse_legacy(json, theme);
    }
    Tables tables;
    // priority 保留为默认买表，其他主题无需迁移。
    if (!parse_goods(json.at("priority").as_array(), tables.buy["default"])) {
        return false;
    }
    if (const auto buy = json.find_value("buy_tables")) {
        if (!buy->is_object()) {
            LogError << __FUNCTION__ << "buy_tables must be an object" << theme;
            return false;
        }
        for (const auto& [name, table] : buy->as_object()) {
            if (name.empty() || name == "default" || !table.is_array()) {
                LogError << __FUNCTION__ << "Invalid shopping buy table" << theme << name;
                return false;
            }
            if (!parse_goods(table.as_array(), tables.buy[name])) {
                return false;
            }
        }
    }
    if (const auto sell = json.find_value("sell_tables")) {
        if (!sell->is_object()) {
            LogError << __FUNCTION__ << "sell_tables must be an object" << theme;
            return false;
        }
        for (const auto& [name, table] : sell->as_object()) {
            if (name.empty() || !table.is<std::vector<std::string>>()) {
                LogError << __FUNCTION__ << "Invalid shopping sell table" << theme << name;
                return false;
            }
            auto names = table.as<std::vector<std::string>>();
            if (std::ranges::any_of(names, [](const std::string& item) { return item.empty(); })) {
                LogError << __FUNCTION__ << "Shopping sell table contains an empty name" << theme << name;
                return false;
            }
            tables.sell.emplace(name, std::move(names));
        }
    }
    if (const auto extensions = json.find_value("ordinary_shop_extensions")) {
        if (!extensions->is_object()) {
            LogError << __FUNCTION__ << "Ordinary shop extensions must be an object";
            return false;
        }
        for (const auto& [table, sources] : extensions->as_object()) {
            if (!tables.buy.contains(table) || !sources.is<std::vector<std::string>>()) {
                LogError << __FUNCTION__ << "Invalid ordinary shop extension" << table;
                return false;
            }
            auto names = sources.as<std::vector<std::string>>();
            for (const auto& source : names) {
                if (source != "$scrap_market" && !tables.buy.contains(source)) {
                    LogError << __FUNCTION__ << "Unknown ordinary shop extension source" << source;
                    return false;
                }
            }
            tables.ordinary_shop_extensions.emplace(table, std::move(names));
        }
    }
    if (const auto exclusions = json.find_value("ordinary_shop_profile_exclusions")) {
        if (!exclusions->is_object()) {
            LogError << __FUNCTION__ << "Ordinary shop profile exclusions must be an object";
            return false;
        }
        for (const auto& [profile, values] : exclusions->as_object()) {
            if (profile.empty() || !values.is<std::vector<std::string>>()) {
                LogError << __FUNCTION__ << "Invalid ordinary shop profile exclusions" << profile;
                return false;
            }
            auto names = values.as<std::vector<std::string>>();
            if (std::ranges::any_of(names, [](const auto& name) { return name.empty(); })) {
                LogError << __FUNCTION__ << "Ordinary shop exclusion contains an empty name" << profile;
                return false;
            }
            tables.ordinary_shop_profile_exclusions.emplace(profile, std::move(names));
        }
    }
    m_tables.insert_or_assign(theme, std::move(tables));
    return true;
}

std::vector<asst::RoguelikeGoods>
    asst::RoguelikeShoppingConfig::get_shop_goods(const std::string& theme, const std::string& table) const
{
    return get_shop_goods(theme, table, "");
}

std::vector<asst::RoguelikeGoods> asst::RoguelikeShoppingConfig::get_shop_goods(
    const std::string& theme,
    const std::string& table,
    const std::string& profile) const
{
    auto goods = get_goods(theme, table);
    if (!strategy_shopping_enabled(theme)) {
        return goods;
    }
    const auto& tables = m_tables.at(theme);
    const auto extension = tables.ordinary_shop_extensions.find(table);
    const auto append = [&](const RoguelikeGoods& item) {
        if (std::ranges::none_of(goods, [&](const auto& existing) { return existing.name == item.name; })) {
            goods.emplace_back(item);
        }
    };
    const auto sources =
        extension == tables.ordinary_shop_extensions.end() ? std::vector<std::string> {} : extension->second;
    for (const auto& source : sources) {
        if (source == "$scrap_market") {
            for (const auto& scrap : BlackFlowScrapMarket.items()) {
                if (scrap.price) {
                    RoguelikeGoods item;
                    item.name = scrap.name;
                    item.price = scrap.price;
                    append(item);
                }
            }
        }
        else {
            for (const auto& item : tables.buy.at(source)) {
                append(item);
            }
        }
    }
    // Apply the selected strategy's exclusions after every extension has been
    // merged, so a common fallback table cannot reintroduce another ending's item.
    if (const auto excluded = tables.ordinary_shop_profile_exclusions.find(profile);
        excluded != tables.ordinary_shop_profile_exclusions.end()) {
        std::erase_if(goods, [&](const auto& item) {
            return std::ranges::find(excluded->second, item.name) != excluded->second.end();
        });
    }
    return goods;
}

bool asst::RoguelikeShoppingConfig::parse_legacy(const json::value& json, const std::string& theme)
{
    m_goods.erase(theme);
    auto& goods = m_goods[theme];
    if (!parse_goods(json.at("priority").as_array(), goods)) {
        return false;
    }
    // 保留各类别中的资源优先顺序，先挑选适用藏品，再考虑招募券。
    std::stable_partition(goods.begin(), goods.end(), [](const RoguelikeGoods& item) {
        return !item.is_recruit_ticket;
    });
    return true;
}

bool asst::RoguelikeShoppingConfig::has_tables(
    const std::string& theme,
    const std::string& buy_table,
    const std::string& sell_table) const
{
    const auto found = m_tables.find(theme);
    return found != m_tables.end() && found->second.buy.contains(buy_table) &&
           (sell_table.empty() || found->second.sell.contains(sell_table));
}

void asst::RoguelikeShoppingConfig::clear()
{
    m_goods.clear();
    m_tables.clear();
}
