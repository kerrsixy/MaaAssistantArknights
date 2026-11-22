#pragma once

#include "SPAStrategyCatalogue.h"

#include <algorithm>
#include <string>
#include <vector>

namespace asst::spa
{
struct StrategyGuideRoute
{
    std::string guide_id;
    std::string primary;
    std::vector<std::string> strategies;
    bool core_primary = true;
    std::vector<std::string> support_pacts;
    std::vector<std::string> required_available_pacts;
    bool planner_supported = true;
};

// Player-guide summaries refreshed 2026-11-18, not independent win evidence.
// Source references and structured round plans: config/MiniGame/SPAGuideRoutes.json.
inline const std::vector<StrategyGuideRoute>& strategy_guide_routes()
{
    static const std::vector<StrategyGuideRoute> routes {
        { "ursus", "乌萨斯", { "band_jesica", "band_mess" }, true, { "奥术", "迅捷" } },
        { "ursus-gravel", "乌萨斯", { "band_mech" }, true, { "卡西米尔", "不屈", "灵巧", "奇迹" } },
        { "lethania", "莱塔尼亚", { "band_jesica", "band_mess" }, true, { "精准", "奥术", "远见" } },
        { "lethania-mouth", "莱塔尼亚", { "band_evolve" }, true, { "奥术", "精准", "远见", "聚合" } },
        { "lethania-iso", "莱塔尼亚", { "band_damaztic" }, true, { "精准", "奥术", "远见" } },
        { "egir", "阿戈尔", { "band_lmlee" } },
        { "sargon", "萨尔贡", { "band_lmlee", "band_fund" } },
        { "kjerag", "谢拉格", { "band_lmlee", "band_fund" } },
        { "kazimierz", "卡西米尔", { "band_sarkazb", "band_damaztic" } },
        { "iberia", "伊比利亚", { "band_damaztic" }, true, { "精准", "奥术" } },
        { "victoria", "维多利亚", { "band_sarkazb", "band_lmlee" } },
        { "siracusa", "叙拉古", { "band_damaztic", "band_siracusa" }, true, { "精准", "聚合" } },
        { "yan", "炎", { "band_gain" }, true, { "灵巧", "远见" } },
        { "yan-duyaoye", "炎", { "band_yan" }, true, { "坚守", "聚合", "灵巧", "协防干员" } },
        { "preci-arcane", "精准", { "band_mech" }, false, { "奥术", "远见", "灵巧", "聚合", "奇迹" } },
        { "preci-russo", "精准", { "band_ioleta" }, false, { "灵巧", "坚守" } },
        { "preci-anaconda", "精准", { "band_mech" }, false, { "奥术", "协防干员" } },
        { "reverse", "奇迹", { "band_sell", "band_bonus" }, false, { "投资人", "远见" } },
        { "skland-lethania-arcane",
          "莱塔尼亚",
          { "band_weakness", "band_transfer", "band_bonus" },
          true,
          { "精准", "奥术", "聚合", "远见" },
          { "精准", "奥术" } },
        { "skland-precision-arcane",
          "精准",
          { "band_weakness" },
          false,
          { "奥术", "聚合", "灵巧", "远见" },
          { "精准", "奥术" } },
        { "skland-ursus-weakness", "乌萨斯", { "band_weakness" }, true, { "奥术", "不屈" } },
        { "fuzou-1",
          "莱塔尼亚",
          { "band_bonus" },
          true,
          { "奥术", "精准", "聚合", "奇迹", "远见" },
          { "精准", "奥术" } },
        { "yan-bison4", "炎", { "band_closeout" }, true, { "坚守", "投资人", "灵巧", "聚合", "奇迹" } },
        // This gear-centric build is indexed under Lingqiao by its author.
        // Do not relabel it as Precision to force an unsupported executor.
        { "tiepi-scope", "灵巧", { "band_mech" }, false, { "奇迹", "坚守", "聚合", "协防干员" }, {}, false },
        { "victoria-stead",
          "维多利亚",
          { "band_jesica", "band_bonus", "band_prepfin", "band_damaztic" },
          true,
          { "坚守", "奇迹", "灵巧" },
          { "坚守" } },
        { "iberia-3", "伊比利亚", { "band_prepfin", "band_damaztic" }, true, { "坚守", "聚合" }, { "坚守", "聚合" } }
    };
    return routes;
}

inline bool guide_pacts_available(const StrategyGuideRoute& route, const std::vector<std::string>& disabled)
{
    return route.planner_supported && std::find(disabled.begin(), disabled.end(), route.primary) == disabled.end() &&
           std::none_of(
               route.required_available_pacts.begin(),
               route.required_available_pacts.end(),
               [&](const auto& pact) { return std::find(disabled.begin(), disabled.end(), pact) != disabled.end(); });
}

inline std::vector<std::string> guided_support_choices(
    const std::string& strategy,
    const std::string& primary,
    const std::vector<std::string>& disabled = {})
{
    std::vector<std::string> supports;
    for (const auto& route : strategy_guide_routes()) {
        if (route.primary != primary || !guide_pacts_available(route, disabled) ||
            std::find(route.strategies.begin(), route.strategies.end(), strategy) == route.strategies.end()) {
            continue;
        }
        for (const auto& support : route.support_pacts) {
            if (std::find(supports.begin(), supports.end(), support) == supports.end()) {
                supports.push_back(support);
            }
        }
    }
    return supports;
}

struct GuidedPrimaryChoices
{
    bool guide_found = false;
    bool requires_noncore_planner = false;
    std::vector<std::string> pacts;
};

inline GuidedPrimaryChoices
    guided_primary_choices(const std::string& strategy, const std::vector<std::string>& disabled = {})
{
    GuidedPrimaryChoices result;
    for (const auto& route : strategy_guide_routes()) {
        if (!route.planner_supported ||
            std::find(route.strategies.begin(), route.strategies.end(), strategy) == route.strategies.end()) {
            continue;
        }
        result.guide_found = true;
        if (!guide_pacts_available(route, disabled)) {
            continue;
        }
        result.requires_noncore_planner = result.requires_noncore_planner || !route.core_primary;
        if (std::find(result.pacts.begin(), result.pacts.end(), route.primary) == result.pacts.end()) {
            result.pacts.push_back(route.primary);
        }
    }
    return result;
}

inline std::string_view declared_strategy_pact(std::string_view strategy)
{
    const auto facts = std::ranges::find(StrategyCatalogue, strategy, &StrategyCatalogueEntry::id);
    return facts != StrategyCatalogue.end() ? facts->pact : std::string_view {};
}

// A teammate's general strategy does not declare a faction. Guide examples
// can guide our own build without guessing a teammate's pool demand.
inline int inferred_route_contention(
    const std::string& pact,
    const std::vector<std::string>& occupied_strategies,
    const std::vector<std::string>& disabled)
{
    return !pact.empty() && std::ranges::find(disabled, pact) == disabled.end() &&
                   std::ranges::any_of(occupied_strategies, [&](const auto& id) { return declared_strategy_pact(id) == pact; })
               ? 100
               : 0;
}
}
