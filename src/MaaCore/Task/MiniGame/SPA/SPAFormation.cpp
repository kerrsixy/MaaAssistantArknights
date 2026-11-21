#include "SPAFormation.h"
#include "SPAGrowth.h"

#include <algorithm>
#include <set>

namespace
{
const std::map<std::string, std::set<std::string>>& terminal_hints()
{
    static const std::map<std::string, std::set<std::string>> hints {
        { "乌萨斯", { "塔露拉", "怒潮凛冬" } },
        { "伊比利亚", { "蓝毒", "棘刺" } },
        { "莱塔尼亚", { "霍尔海雅", "黑键", "艾雅法拉" } },
        { "炎", { "望", "烛煌", "夕" } },
        // Initial Specter is a useful transition blocker with on-deploy
        // growth, not evidence that the high-tier output package is present.
        { "阿戈尔", { "乌尔比安", "克莱门莎" } },
        { "卡西米尔", { "玛恩纳", "耀骑士临光" } },
        { "叙拉古", { "拉普兰德", "荒芜拉普兰德" } },
        { "维多利亚", { "水灯心", "涤火杰西卡", "杰西卡", "烛煌" } },
        { "谢拉格", { "银灰", "灵知", "凛御银灰" } },
        { "萨尔贡", { "佩佩", "鸿雪", "异客" } },
    };
    return hints;
}
}

std::vector<std::string> asst::spa::enabled_formation_pacts(
    const std::vector<std::string>& enabled_core_pacts,
    const std::vector<std::string>& disabled_pacts)
{
    std::vector<std::string> result;
    for (const auto& rule : pact_requirements()) {
        if (std::find(disabled_pacts.begin(), disabled_pacts.end(), rule.name) != disabled_pacts.end()) {
            continue;
        }
        if (!rule.core ||
            std::find(enabled_core_pacts.begin(), enabled_core_pacts.end(), rule.name) != enabled_core_pacts.end()) {
            result.push_back(rule.name);
        }
    }
    return result;
}

asst::spa::FormationEvaluation asst::spa::evaluate_formation(
    const std::vector<RosterUnit>& roster,
    const std::map<std::string, std::vector<std::string>>& catalog,
    const std::vector<PactRequirement>& requirements,
    const std::vector<std::string>& enabled_pacts)
{
    FormationEvaluation result;
    const std::set<std::string> enabled(enabled_pacts.begin(), enabled_pacts.end());
    result.complete = !enabled.empty();
    std::map<std::string, PactRequirement> rules;
    std::map<std::string, std::set<std::string>> members;
    std::map<std::string, int> deployed_counts;
    int elite_count = 0;
    bool elite_unknown = false;
    for (const auto& rule : requirements) {
        if (rule.name.empty() || rule.members <= 0 || !rules.emplace(rule.name, rule).second) {
            result.complete = false;
        }
    }
    for (const auto& pact : enabled) {
        if (!rules.contains(pact)) {
            result.complete = false;
        }
    }
    for (const auto& unit : roster) {
        if (unit.deployed) {
            elite_unknown = elite_unknown || !unit.elite.has_value();
            elite_count += unit.elite.value_or(false) ? 1 : 0;
        }
        const auto found = catalog.find(unit.name);
        if (found == catalog.end()) {
            result.complete = false;
            continue;
        }
        auto pacts = found->second;
        pacts.insert(pacts.end(), unit.added_pacts.begin(), unit.added_pacts.end());
        const std::set<std::string> unique_pacts(pacts.begin(), pacts.end());
        for (const auto& pact : unique_pacts) {
            const auto rule = rules.find(pact);
            if (rule == rules.end()) {
                result.complete = false;
                continue;
            }
            if (!enabled.contains(pact)) {
                continue;
            }
            if (unit.deployed) {
                ++deployed_counts[pact];
            }
            if (unit.deployed || rule->second.bench_counts) {
                members[pact].insert(unit.name);
            }
        }
    }
    const auto harmony_rule = rules.find("调和");
    const bool harmony_active = enabled.contains("调和") && harmony_rule != rules.end() &&
                                members["调和"].size() >= static_cast<size_t>(harmony_rule->second.members);
    if (enabled.contains("绝技") && elite_unknown) {
        result.complete = false;
    }
    for (const auto& [name, rule] : rules) {
        const int distinct = static_cast<int>(members[name].size());
        result.distinct_members[name] = distinct;
        const int count = name == "绝技" ? elite_count : distinct;
        const int activation = count + (rule.core && harmony_active ? 1 : 0);
        result.activation_members[name] = activation;
        const bool solitary_allowed = name != "独行" || deployed_counts[name] == 1;
        if (enabled.contains(name) && activation >= rule.members && solitary_allowed) {
            result.active_pacts.emplace_back(name);
            // Harmony changes base activation, not the literal distinct-member bonus condition.
            if (rule.advanced_members > 0 && count >= rule.advanced_members) {
                result.advanced_pacts.emplace_back(name);
            }
        }
    }
    if (!result.complete) {
        // A partial/ambiguous roster is not activation evidence.  Keep the
        // diagnostic member counts, but clear operational pact claims so
        // callers cannot accidentally run a faction from an incomplete scan.
        result.active_pacts.clear();
        result.advanced_pacts.clear();
        result.amiya_bonus_percent.reset();
    }
    else {
        const auto count = result.active_pacts.size();
        result.amiya_bonus_percent = count >= 5 ? 40 : count >= 4 ? 30 : count >= 3 ? 20 : 0;
    }
    return result;
}

asst::spa::FormationCandidateSelection asst::spa::choose_formation_candidates(
    const std::string& requested_core_pact,
    const std::vector<std::string>& visible_names,
    const std::map<std::string, std::vector<std::string>>& catalog,
    const std::vector<PactRequirement>& requirements)
{
    FormationCandidateSelection result;
    std::map<std::string, PactRequirement> rules;
    for (const auto& rule : requirements) {
        if (rule.core && rule.members > 0 && !rule.name.empty()) {
            rules.emplace(rule.name, rule);
        }
    }

    const bool automatic = requested_core_pact.empty() || requested_core_pact == "auto";
    if (!automatic) {
        const auto rule = rules.find(requested_core_pact);
        if (rule == rules.end()) {
            result.reason = "Requested core pact is not a known faction";
            return result;
        }
        result.core_pact = requested_core_pact;
        result.required_members = rule->second.members;
    }

    std::map<std::string, std::vector<std::string>> eligible;
    std::set<std::string> seen;
    for (const auto& name : visible_names) {
        if (name.empty() || !seen.insert(name).second) {
            continue;
        }
        const auto operator_it = catalog.find(name);
        if (operator_it == catalog.end()) {
            continue;
        }
        const std::set<std::string> pacts(operator_it->second.begin(), operator_it->second.end());
        for (const auto& [pact, rule] : rules) {
            if (pacts.contains(pact)) {
                eligible[pact].emplace_back(name);
            }
        }
    }

    if (automatic) {
        // The current 3/3 page is the first reliable faction signal. Prefer a
        // visible growth engine, then a visible terminal carry, then density;
        // requirement order is only the final deterministic tie-breaker.
        for (const auto& rule : requirements) {
            if (!rule.core || rule.members <= 0) {
                continue;
            }
            const auto found = eligible.find(rule.name);
            if (found == eligible.end() || found->second.size() < static_cast<size_t>(rule.members)) {
                continue;
            }
            int growth = 0;
            for (const auto& name : found->second) {
                growth += is_primary_growth_engine(name, rule.name) ? 1 : 0;
            }
            int output = 0;
            const auto hints = terminal_hints().find(rule.name);
            if (hints != terminal_hints().end()) {
                for (const auto& name : found->second) {
                    output += hints->second.contains(name) ? 1 : 0;
                }
            }
            const int score = growth * 700 + output * 600 + static_cast<int>(found->second.size()) * 120;
            if (result.core_pact.empty() || score > result.score ||
                (score == result.score && found->second.size() > result.eligible_visible.size())) {
                result.core_pact = rule.name;
                result.required_members = rule.members;
                result.eligible_visible = found->second;
                result.score = score;
            }
        }
    }
    else {
        const auto found = eligible.find(result.core_pact);
        if (found != eligible.end()) {
            result.eligible_visible = found->second;
        }
    }

    if (result.core_pact.empty()) {
        result.reason = "No known core faction has enough visible members";
        return result;
    }
    if (result.eligible_visible.size() < static_cast<size_t>(result.required_members)) {
        result.reason = "Visible formation has fewer distinct members than the core activation requirement";
        return result;
    }
    result.operators.assign(result.eligible_visible.begin(), result.eligible_visible.begin() + result.required_members);
    result.valid = true;
    result.reason = "Distinct visible operators satisfy the core faction requirement";
    return result;
}
