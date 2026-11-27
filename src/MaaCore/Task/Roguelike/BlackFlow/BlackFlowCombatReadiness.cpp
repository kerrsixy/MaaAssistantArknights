#include "BlackFlowCombatReadiness.h"

#include <algorithm>
#include <climits>
#include <functional>
#include <map>
#include <set>

#include "BlackFlowRecruitmentTerminalDuty.h"
#include "Config/Miscellaneous/TilePack.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"

namespace asst::blackflow::combat_readiness_detail
{
bool complete_matching(const std::vector<std::vector<bool>>& edges, std::size_t count)
{
    std::vector<int> owners(count, -1);
    std::function<bool(std::size_t, std::vector<bool>&)> visit = [&](std::size_t body, std::vector<bool>& seen) {
        for (std::size_t slot = 0; slot < count; ++slot) {
            if (seen[slot] || !edges[body][slot]) {
                continue;
            }
            seen[slot] = true;
            if (owners[slot] < 0 || visit(static_cast<std::size_t>(owners[slot]), seen)) {
                owners[slot] = static_cast<int>(body);
                return true;
            }
        }
        return false;
    };
    for (std::size_t body = 0; body < edges.size(); ++body) {
        std::vector<bool> seen(count, false);
        visit(body, seen);
    }
    return std::ranges::none_of(owners, [](int body) { return body < 0; });
}
}

namespace asst::blackflow
{
std::optional<std::vector<CombatReadinessProfile>>
    parse_combat_readiness_profiles(const json::value& input, std::string* error)
{
    const auto reject = [&](const char* reason) -> std::optional<std::vector<CombatReadinessProfile>> {
        if (error) {
            *error = reason;
        }
        return std::nullopt;
    };
    if (!input.is_array() || input.as_array().size() > 64) {
        return reject("Combat readiness profiles must be a bounded array");
    }
    std::vector<CombatReadinessProfile> profiles;
    std::set<std::string> ids, titles;
    for (const auto& item : input.as_array()) {
        if (!item.is_object() || item.as_object().size() != 3 || !item.contains("id") ||
            !item.contains("stage_title") || !item.contains("variants") || !item.at("id").is_string() ||
            !item.at("stage_title").is_string() || !item.at("variants").is_array()) {
            return reject("Invalid combat readiness profile fields");
        }
        CombatReadinessProfile profile { .id = item.at("id").as_string(),
                                         .stage_title = item.at("stage_title").as_string() };
        if (profile.id.empty() || profile.stage_title.empty() || !ids.insert(profile.id).second ||
            !titles.insert(profile.stage_title).second || item.at("variants").as_array().empty() ||
            item.at("variants").as_array().size() > 16) {
            return reject("Empty or duplicate combat readiness profile");
        }
        std::set<std::string> variants;
        for (const auto& value : item.at("variants").as_array()) {
            if (!value.is_object() || value.as_object().size() != 2 || !value.contains("stage_key") ||
                !value.contains("ordinary_healing_plans") || !value.at("stage_key").is_string() ||
                !value.at("ordinary_healing_plans").is_array()) {
                return reject("Invalid combat readiness variant fields");
            }
            CombatReadinessVariant variant { .stage_key = value.at("stage_key").as_string() };
            const auto& plans = value.at("ordinary_healing_plans").as_array();
            if (variant.stage_key.empty() || !variants.insert(variant.stage_key).second || plans.empty() ||
                plans.size() > 6) {
                return reject("Empty or duplicate combat readiness variant");
            }
            std::set<int> indices;
            for (const auto& index : plans) {
                if (!index.is<int>() || index.as_double() < 0 || index.as_double() > 127 ||
                    !indices.insert(index.as_integer()).second) {
                    return reject("Invalid combat readiness plan index");
                }
                variant.ordinary_healing_plans.push_back(index.as_integer());
            }
            profile.variants.push_back(std::move(variant));
        }
        if (!variants.contains(profile.stage_title)) {
            return reject("Combat readiness profile must include its base stage");
        }
        profiles.push_back(std::move(profile));
    }
    return profiles;
}

std::string combat_readiness_profile_terms(const CombatReadinessProfile& profile)
{
    json::array variants;
    for (const auto& variant : profile.variants) {
        variants.emplace_back(
            json::object { { "stage_key", variant.stage_key },
                           { "ordinary_healing_plans", variant.ordinary_healing_plans } });
    }
    return json::value(
               json::object { { "id", profile.id }, { "stage_title", profile.stage_title }, { "variants", variants } })
        .to_string();
}

std::string combat_roster_scope(
    const RoguelikeStatus& status,
    const std::string& client,
    const std::string& uid,
    const std::string& core)
{
    if (client.empty() || uid.empty() || status.run_id.empty()) {
        return {};
    }
    std::vector<std::string> names;
    for (const auto& [name, oper] : status.opers) {
        static_cast<void>(oper);
        names.push_back(name);
    }
    std::ranges::sort(names);
    json::array roster;
    if (names.size() <= 128) {
        for (const auto& name : names) {
            const auto& oper = status.opers.at(name);
            const auto& prep = oper.preparation;
            roster.emplace_back(
                json::object { { "name", name },
                               { "elite", oper.elite },
                               { "level", oper.level },
                               { "id", prep.operator_id },
                               { "skill", prep.selected_skill },
                               { "ranks", prep.displayed_skill_levels },
                               { "training", prep.training_sequence },
                               { "selection", prep.selection_sequence } });
        }
    }
    return json::value(
               json::object { { "client", client },
                              { "uid", uid },
                              { "run_id", status.run_id },
                              { "core", core },
                              { "count", names.size() },
                              { "roster", roster } })
        .to_string();
}

CombatReadinessEvidence evaluate_combat_readiness(
    const CombatReadinessProfile& profile,
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const RoguelikeStatus& status,
    const std::string& client,
    const std::string& uid,
    const std::string& core,
    bool roster_complete)
{
    CombatReadinessEvidence result { .profile_id = profile.id,
                                     .profile_terms = combat_readiness_profile_terms(profile),
                                     .roster_scope = combat_roster_scope(status, client, uid, core),
                                     .reason = "Current roster identity or authored healing requirements unverified",
                                     .roster_complete = roster_complete };
    if (result.roster_scope.empty() || !catalog.valid() || status.opers.size() > 128) {
        return result;
    }
    std::set<std::string> expected { profile.stage_title }, declared;
    for (const auto& [total, key] : RoguelikeCopilot.get_stage_variant_keys(profile.stage_title)) {
        static_cast<void>(total);
        expected.insert(key);
    }
    for (const auto& variant : profile.variants) {
        declared.insert(variant.stage_key);
    }
    if (expected != declared) {
        result.reason = "Possible stage variants do not exactly match the reviewed requirement profile";
        return result;
    }
    const auto geometry = TilePack::calc(profile.stage_title);
    if (geometry.normal_tile_info.empty()) {
        result.reason = "Shared stage geometry unverified";
        return result;
    }
    const TerminalDutyContext context { .strategy = "ending1",
                                        .stage_name = profile.stage_title,
                                        .client = client,
                                        .uid = uid,
                                        .run_id = status.run_id,
                                        .core = core };
    bool unknown = false, missing = false;
    for (const auto& variant : profile.variants) {
        const auto stage = RoguelikeCopilot.get_stage_data(variant.stage_key);
        if (!stage) {
            result.reason = "Required variant-specific deployment resource missing";
            return result;
        }
        std::map<int, std::pair<battle::roguelike::DeployInfoWithRank, std::vector<std::string>>> plans;
        for (const auto& [group, entries] : stage->deploy_plan) {
            for (const auto& plan : entries) {
                if (std::ranges::find(variant.ordinary_healing_plans, plan.plan_index) !=
                    variant.ordinary_healing_plans.end()) {
                    auto [it, inserted] =
                        plans.try_emplace(plan.plan_index, std::pair { plan, std::vector<std::string> {} });
                    static_cast<void>(inserted);
                    it->second.second.push_back(group);
                }
            }
        }
        if (plans.size() != variant.ordinary_healing_plans.size() || std::ranges::any_of(plans, [](const auto& row) {
                const auto& plan = row.second.first;
                return plan.required_range_tiles.empty() || plan.kill_lower_bound != 0 ||
                       plan.kill_upper_bound != 9999 || plan.max_deployments != INT_MAX ||
                       !plan.exclusive_locations.empty();
            })) {
            result.reason = "Required healing plan missing or conditional";
            return result;
        }
        std::vector<std::vector<bool>> lower, upper;
        for (const auto& [name, oper] : status.opers) {
            std::vector<bool> yes, possible;
            for (int index : variant.ordinary_healing_plans) {
                const auto& [plan, groups] = plans.at(index);
                const auto admitted =
                    admits_current_authored_duty(name, oper, plan, groups, *stage, context, catalog, geometry);
                yes.push_back(admitted == std::optional<bool>(true));
                possible.push_back(admitted != std::optional<bool>(false));
            }
            lower.push_back(std::move(yes));
            upper.push_back(std::move(possible));
        }
        const bool met = combat_readiness_detail::complete_matching(lower, plans.size());
        const bool gap = roster_complete && !combat_readiness_detail::complete_matching(upper, plans.size());
        result.variant_results.push_back(
            variant.stage_key + (met   ? ":necessary_geometry_present"
                                 : gap ? ":known_gap"
                                       : ":unknown"));
        unknown = unknown || !met;
        missing = missing || gap;
    }
    result.state = missing   ? CombatReadinessState::Missing
                   : unknown ? CombatReadinessState::Unknown
                             : CombatReadinessState::Supported;
    result.reason = missing ? "Complete roster cannot fill reviewed ordinary healing duties for every possible variant"
                    : unknown ? "Ordinary healing coverage lacks a positive witness; roster absence is not inferred"
                              : "Distinct current bodies meet necessary ordinary healing geometry only; active healing "
                                "uptime and HPS unproven";
    return result;
}

json::value combat_readiness_to_json(const CombatReadinessEvidence& evidence)
{
    const char* state = evidence.state == CombatReadinessState::Supported ? "supported_necessary_conditions_only"
                        : evidence.state == CombatReadinessState::Missing ? "known_gap"
                                                                          : "unknown";
    return json::object { { "state", state },
                          { "profile", evidence.profile_id },
                          { "profile_terms", evidence.profile_terms },
                          { "roster_scope", evidence.roster_scope },
                          { "roster_complete", evidence.roster_complete },
                          { "reason", evidence.reason },
                          { "variants", evidence.variant_results },
                          { "HPS_or_survival_proven", false } };
}
}
