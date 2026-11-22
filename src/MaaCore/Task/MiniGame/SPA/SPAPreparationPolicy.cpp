#include "SPAPreparationPolicy.h"
#include "SPAEconomyPolicy.h"
#include "SPAGrowth.h"
#include "SPAGuideRoutes.h"
#include "SPAOperatorCatalogue.h"
#include "SPAPlanner.h"
#include "SPAStrategyCatalogue.h"
#include <algorithm>
#include <cmath>
#include <tuple>

std::optional<asst::spa::OperatorFormCounts> asst::spa::owned_form_counts(const OwnedOperator& owned)
{
    if (owned.copies < 0 || owned.copies > 20 ||
        (owned.known_physical_copies && *owned.known_physical_copies != owned.copies)) {
        return std::nullopt;
    }
    if (owned.form_counts) {
        const auto& counts = *owned.form_counts;
        if (counts.initial < 0 || counts.initial > 20 || counts.elite < 0 || counts.elite > 20 ||
            counts.initial + counts.elite != owned.copies || owned.elite != (counts.elite > 0)) {
            return std::nullopt;
        }
        return counts;
    }
    if (!owned.elite) {
        return OperatorFormCounts { owned.copies, 0 };
    }
    // Older callers used both (0,true) and (1,true) for one elite body.
    // Neither spelling can establish the form split of two or more bodies.
    return owned.copies <= 1 ? std::optional(OperatorFormCounts { 0, 1 }) : std::nullopt;
}

bool asst::spa::set_owned_form_counts(OwnedOperator& owned, OperatorFormCounts counts)
{
    // The temporary model can hold up to two new copies before automatic
    // synthesis is reconciled; the stable field plus bench limit is 18.
    if (counts.initial < 0 || counts.initial > 20 || counts.elite < 0 || counts.elite > 20 ||
        counts.initial + counts.elite > 20) {
        return false;
    }
    owned.form_counts = counts;
    owned.copies = counts.initial + counts.elite;
    owned.known_physical_copies = owned.copies;
    owned.elite = counts.elite > 0;
    if (owned.copies == 0) {
        owned.deployed = false;
    }
    return true;
}

bool asst::spa::add_initial_copies(OwnedOperator& owned, int count)
{
    auto counts = owned_form_counts(owned);
    if (!counts || count < 1 || count > 2 || counts->initial + count > 20) {
        return false;
    }
    counts->initial += count;
    return set_owned_form_counts(owned, *counts);
}

bool asst::spa::add_purchased_operator(OwnedOperator& owned, bool elite)
{
    if (!elite) {
        return add_initial_copies(owned);
    }
    auto counts = owned_form_counts(owned);
    if (!counts) {
        return false;
    }
    ++counts->elite;
    return set_owned_form_counts(owned, *counts);
}

bool asst::spa::ordinary_purchase_completes_promotion(const PreparationPolicy& policy, const std::string& name)
{
    const auto found = policy.owned.find(name);
    if (policy.tactical_elite_purchase_pending || found == policy.owned.end()) {
        return false;
    }
    const auto forms = owned_form_counts(found->second);
    const auto threshold = policy.promotion_copies.find(name);
    const int required =
        threshold == policy.promotion_copies.end() ? promotion_copies_required(name) : threshold->second;
    return forms && forms->initial > 0 && forms->initial + 1 == required;
}

bool asst::spa::promote_initial_copies(OwnedOperator& owned, int required)
{
    auto counts = owned_form_counts(owned);
    if (!counts || required < 2 || required > 6 || counts->initial < required) {
        return false;
    }
    counts->initial -= required;
    ++counts->elite;
    return set_owned_form_counts(owned, *counts);
}

bool asst::spa::restore_owned_operator(const json::value& row, OwnedOperator& owned)
{
    if (!row.is_object()) {
        return false;
    }
    for (const std::string key : { "initial_copies", "elite_copies", "physical_copies" }) {
        if (!row.contains(key)) {
            continue;
        }
        const auto& value = row.at(key);
        if (!value.is_number()) {
            return false;
        }
        const double count = value.as_double();
        if (!std::isfinite(count) || count < 0 || count > 20 || count != std::floor(count)) {
            return false;
        }
    }
    const auto initial = row.find<int>("initial_copies");
    const auto elite = row.find<bool>("elite");
    const auto deployed = row.find<bool>("deployed");
    if (!initial || !elite || !deployed || *initial < 0 || *initial > 20 ||
        (row.contains("elite_copies") && !row.find<int>("elite_copies")) ||
        (row.contains("form_counts_known") && !row.find<bool>("form_counts_known")) ||
        (row.contains("physical_copies") && !row.find<int>("physical_copies"))) {
        return false;
    }
    OwnedOperator result { *initial, *elite, *deployed };
    if (const auto count = row.find<int>("elite_copies")) {
        if (!row.get("form_counts_known", true) || *elite != (*count > 0) ||
            !set_owned_form_counts(result, { *initial, *count })) {
            return false;
        }
    }
    else if (const auto counts = owned_form_counts(result)) {
        if (!row.get("form_counts_known", true) || !set_owned_form_counts(result, *counts)) {
            return false;
        }
    }
    else if (row.get("form_counts_known", false)) {
        return false;
    }
    if ((row.contains("physical_copies") && row.at("physical_copies").as_integer() != result.copies) ||
        (*deployed && result.copies == 0)) {
        return false;
    }
    if (row.contains("physical_copies")) {
        result.known_physical_copies = result.copies;
    }
    owned = std::move(result);
    return true;
}

std::optional<int>
    asst::spa::bench_population_from_roster(const std::map<std::string, OwnedOperator>& owned, int field_count)
{
    if (field_count < 0 || field_count > 8) {
        return std::nullopt;
    }
    int total = 0;
    for (const auto& [name, unit] : owned) {
        if (const auto counts = owned_form_counts(unit)) {
            total += counts->initial + counts->elite;
        }
        else if (
            !unit.form_counts && unit.elite && unit.known_physical_copies == unit.copies && unit.copies >= 2 &&
            unit.copies <= 18) {
            // An old row may lack its form split while retaining a known
            // total physical quantity. Do not invent either form's count.
            total += unit.copies;
        }
        else {
            return std::nullopt;
        }
        if (total > 18) {
            return std::nullopt;
        }
    }
    const int bench = total - field_count;
    return bench >= 0 && bench <= 10 ? std::optional(bench) : std::nullopt;
}

json::object asst::spa::owned_operator_snapshot(const std::string& name, const OwnedOperator& owned)
{
    json::object row { { "name", name }, { "elite", owned.elite }, { "deployed", owned.deployed } };
    const auto counts = owned_form_counts(owned);
    row["form_counts_known"] = counts.has_value();
    if (counts) {
        row["initial_copies"] = counts->initial;
        row["elite_copies"] = counts->elite;
        row["physical_copies"] = counts->initial + counts->elite;
    }
    else {
        // Retain an ambiguous legacy row without inventing an elite count.
        row["initial_copies"] = owned.copies;
        if (owned.known_physical_copies == owned.copies) {
            row["physical_copies"] = owned.copies;
        }
    }
    return row;
}

bool asst::spa::reconcile_promotion_reward(
    PreparationPolicy& policy,
    const std::string& source,
    const std::string& reward)
{
    if (reward.empty() || source == reward) {
        return false;
    }
    auto selected = policy.owned.contains(reward) ? policy.owned.at(reward) : OwnedOperator {};
    if (!add_initial_copies(selected)) {
        return false;
    }
    std::optional<OwnedOperator> promoted;
    int required = 0;
    if (!source.empty()) {
        const auto found = policy.owned.find(source);
        if (found == policy.owned.end()) {
            return false;
        }
        // A promoted deployed source returns to its corresponding board tile.
        // Keep its deployment status instead of deleting it or moving it off board.
        const auto override = policy.promotion_copies.find(source);
        required = override == policy.promotion_copies.end() ? promotion_copies_required(source) : override->second;
        promoted = found->second;
        if (!promote_initial_copies(*promoted, required)) {
            return false;
        }
    }
    if (promoted) {
        policy.owned.at(source) = *promoted;
        policy.bench_occupied = std::max(0, policy.bench_occupied - (required - 1));
    }
    policy.owned[reward] = std::move(selected);
    // Preserve any already deployed copy; the new reward itself is initial.
    policy.bench_occupied = std::min(10, policy.bench_occupied + 1);
    return true;
}

#include "SPAFormation.h"
#include "SPAGrowth.h"
#include "SPARouteProfile.h"
#include <map>
#include <set>

namespace
{
using namespace asst::spa;

bool contains(const std::vector<std::string>& values, const std::string& value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

int conservative_minimum_shop_level(const std::string& pact)
{
    const auto found = route_profiles().find(pact);
    // Unknown factions keep a conservative two-book floor until live pool
    // evidence calibrates them.  This is a planning hint, never a hard gate.
    return found == route_profiles().end() ? 2 : found->second.minimum_shop_level;
}

std::vector<std::string> derive_support_pacts(
    const PreparationPolicy& policy,
    const std::vector<ShopOffer>& offers,
    const std::map<std::string, std::vector<std::string>>& pacts,
    const std::string& primary)
{
    struct Candidate
    {
        std::string name;
        int score = 0;
        bool bench_counts = false;
    };

    std::vector<Candidate> candidates;
    const auto requirements = pact_requirements();
    const auto route = route_profiles().find(primary);
    const auto guide_supports = guided_support_choices(policy.selected_strategy_id, primary, policy.disabled_pacts);
    const auto disabled = [&](const std::string& name) {
        return contains(policy.disabled_pacts, name);
    };
    const auto conditional_support = [&](const std::string& name) {
        return route != route_profiles().end() && contains(route->second.conditional_support_pacts, name);
    };
    std::set<std::string> primary_members;
    for (const auto& [name, owned] : policy.owned) {
        const auto found = pacts.find(name);
        if (found != pacts.end() && (owned.copies > 0 || owned.elite) && contains(found->second, primary)) {
            primary_members.insert(name);
        }
    }
    for (const auto& offer : offers) {
        const auto found = pacts.find(offer.name);
        if (offer.price >= 0 && found != pacts.end() && contains(found->second, primary)) {
            primary_members.insert(offer.name);
        }
    }
    for (const auto& rule : requirements) {
        if (rule.core || rule.name == primary || disabled(rule.name) ||
            (rule.members < 2 && !conditional_support(rule.name))) {
            continue;
        }
        // Harmony contributes an activation member. Two actual core names
        // plus one Harmony unit suffice for a normal three-member core.
        if (rule.name == "调和" && primary_members.size() < 2) {
            continue;
        }
        std::set<std::string> owned_members;
        std::set<std::string> ready_reserves;
        std::set<std::string> offer_members;
        int shared = 0;
        for (const auto& [name, owned] : policy.owned) {
            const auto found = pacts.find(name);
            if (found == pacts.end() || !contains(found->second, rule.name)) {
                continue;
            }
            if (owned.copies <= 0 && !owned.elite) {
                continue;
            }
            if (owned.deployed || rule.bench_counts) {
                owned_members.insert(name);
            }
            else if (policy.deployed_population >= 0 || policy.primary_locked_after_activation) {
                ready_reserves.insert(name);
            }
            if ((owned_members.contains(name) || ready_reserves.contains(name)) && contains(found->second, primary)) {
                ++shared;
            }
        }
        for (const auto& offer : offers) {
            const auto found = pacts.find(offer.name);
            if (offer.price < 0 || found == pacts.end() || !contains(found->second, rule.name)) {
                continue;
            }
            offer_members.insert(offer.name);
            if (!owned_members.contains(offer.name) && !ready_reserves.contains(offer.name) &&
                contains(found->second, primary)) {
                ++shared;
            }
        }
        auto available_members = owned_members;
        available_members.insert(ready_reserves.begin(), ready_reserves.end());
        available_members.insert(offer_members.begin(), offer_members.end());
        if (available_members.size() < static_cast<size_t>(rule.members)) {
            continue;
        }
        const int already = static_cast<int>(owned_members.size());
        const int needed = std::max(0, rule.members - already);
        if (!rule.bench_counts && policy.deployed_population >= 0 &&
            needed > std::max(1, 8 - policy.deployed_population)) {
            // Scarce deployment slots can be shared by an immediate bridge,
            // but a pile of reserves cannot activate a new formation here.
            continue;
        }
        const bool guide_preferred = contains(guide_supports, rule.name);
        const bool route_preferred = guide_preferred || (route != route_profiles().end() &&
                                                         contains(route->second.preferred_support_pacts, rule.name));
        if (rule.name == "投资人" && !route_preferred) {
            continue;
        }
        // An unrelated ready-made auxiliary must not start a third roster.
        // Require an actual primary bridge and a documented compatible
        // effect, except for explicitly useful bench/Harmony support.
        // A strategy-specific guide can document synergy without a dual-pact
        // member. It still needs distinct activation members and a growth engine.
        if ((!route_preferred || (shared == 0 && !guide_preferred)) && !conditional_support(rule.name) &&
            !rule.bench_counts) {
            continue;
        }
        const bool utility_without_layers = rule.bench_counts || rule.name == "投资人" || rule.name == "调和";
        bool growth_available = false;
        for (const auto& [name, owned] : policy.owned) {
            const auto profile = growth_profiles().find(name);
            growth_available =
                growth_available || ((owned.copies > 0 || owned.elite) &&
                                     (owned.deployed || ready_reserves.contains(name) ||
                                      (profile != growth_profiles().end() && profile->second.works_on_bench)) &&
                                     is_primary_growth_engine(name, rule.name));
        }
        for (const auto& offer : offers) {
            growth_available =
                growth_available || (offer.price >= 0 && is_primary_growth_engine(offer.name, rule.name));
        }
        if (!utility_without_layers && !growth_available) {
            continue;
        }
        const int conditional_bonus = conditional_support(rule.name) ? 450 : 0;
        const int score = std::min(already, rule.members) * 500 +
                          std::min(static_cast<int>(offer_members.size()), needed) * 700 + shared * 350 +
                          std::min(static_cast<int>(ready_reserves.size()), needed) * 450 +
                          (rule.bench_counts ? 150 : 0) + (route_preferred ? 300 : 0) + conditional_bonus;
        candidates.push_back({ rule.name, score, rule.bench_counts });
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        return lhs.name < rhs.name;
    });
    std::vector<std::string> result;
    bool selected_field_pact = false;
    bool selected_bench_pact = false;
    for (const auto& candidate : candidates) {
        bool& selected = candidate.bench_counts ? selected_bench_pact : selected_field_pact;
        if (!selected) {
            result.emplace_back(candidate.name);
            selected = true;
        }
    }
    return result;
}
}

std::vector<std::string> asst::spa::planning_support_pacts(
    const PreparationPolicy& policy,
    const std::vector<ShopOffer>& offers,
    const std::map<std::string, std::vector<std::string>>& pacts)
{
    if (!policy.support_pacts.empty()) {
        return policy.support_pacts;
    }
    if (policy.derive_support_pacts && policy.shop_level >= 3) {
        return derive_support_pacts(policy, offers, pacts, policy.primary_pact);
    }
    return {};
}

bool asst::spa::is_primary_output_candidate(const std::string& name, const std::string& pact)
{
    const auto route = route_profiles().find(pact);
    return route != route_profiles().end() &&
           std::find(route->second.output_candidates.begin(), route->second.output_candidates.end(), name) !=
               route->second.output_candidates.end();
}

bool asst::spa::is_terminal_output_candidate(const std::string& name, const std::string& pact)
{
    if (pact == "阿戈尔") {
        return name == "克莱门莎" || name == "乌尔比安";
    }
    return is_primary_output_candidate(name, pact);
}

namespace
{
std::pair<bool, bool> kjerag_freeze_roles(const asst::spa::PreparationPolicy& policy, bool deployed_only)
{
    bool stacker = false;
    bool provider = false;
    for (const auto& [name, unit] : policy.owned) {
        if ((unit.copies <= 0 && !unit.elite) || (deployed_only && !unit.deployed)) {
            continue;
        }
        stacker = stacker || asst::spa::is_kjerag_freeze_stacker(name);
        provider = provider || asst::spa::is_kjerag_freeze_provider(name) ||
                   (unit.deployed && policy.kjerag_ice_carriers.contains(name));
    }
    return { stacker, provider };
}

bool kjerag_has_growth_window(const asst::spa::PreparationPolicy& policy)
{
    return policy.primary_pact == "谢拉格" && !contains(policy.disabled_pacts, "谢拉格") && !policy.boss_preparation &&
           (policy.growth_scenario_horizon_round <= 0 || policy.round < policy.growth_scenario_horizon_round);
}
}

bool asst::spa::kjerag_freeze_pair_available(const PreparationPolicy& policy, bool deployed_only)
{
    if (!kjerag_has_growth_window(policy)) {
        return false;
    }
    const auto [stacker, provider] = kjerag_freeze_roles(policy, deployed_only);
    return stacker && provider;
}

bool asst::spa::kjerag_recruit_completes_freeze_pair(
    const PreparationPolicy& policy,
    const std::string& name,
    bool deployed_only)
{
    if (!kjerag_has_growth_window(policy)) {
        return false;
    }
    const auto own = policy.owned.find(name);
    if (own != policy.owned.end() && (own->second.copies > 0 || own->second.elite)) {
        return false;
    }
    const auto [stacker, provider] = kjerag_freeze_roles(policy, deployed_only);
    return !(stacker && provider) && (stacker || is_kjerag_freeze_stacker(name)) &&
           (provider || is_kjerag_freeze_provider(name));
}

int asst::spa::primary_growth_support_value(
    const std::string& name,
    const PreparationPolicy& policy,
    const std::map<std::string, std::vector<std::string>>& pacts)
{
    const auto source = pacts.find(name);
    if (policy.primary_pact.empty() || policy.primary_pact == "auto" || source == pacts.end() ||
        contains(source->second, policy.primary_pact) || contains(policy.disabled_pacts, policy.primary_pact)) {
        return 0;
    }
    int actual_members = 0;
    std::vector<RosterUnit> roster;
    for (const auto& [owned_name, owned] : policy.owned) {
        if (owned.copies <= 0 && !owned.elite) {
            continue;
        }
        roster.push_back({ owned_name, owned.deployed, {}, owned.elite });
        const auto member = pacts.find(owned_name);
        actual_members += member != pacts.end() && contains(member->second, policy.primary_pact);
    }
    auto cores = policy.enabled_core_pacts;
    if (cores.empty()) {
        cores.push_back(policy.primary_pact);
    }
    const auto formation =
        evaluate_formation(roster, pacts, pact_requirements(), enabled_formation_pacts(cores, policy.disabled_pacts));
    const bool active = formation.complete && contains(formation.active_pacts, policy.primary_pact);
    // Harmony can itself complete the developing core. It does not require
    // a completed auxiliary pact of the helper's own printed membership.
    if (name == "魔王" && actual_members >= 2 && !contains(policy.disabled_pacts, "调和")) {
        return 1800;
    }
    if (!active) {
        return 0;
    }
    if (name == "夏栎" || name == "星源") {
        const auto primary = policy.observed_pact_layers.find(policy.primary_pact);
        if (primary == policy.observed_pact_layers.end()) {
            return 0;
        }
        for (const auto& pact : formation.active_pacts) {
            const auto layers = policy.observed_pact_layers.find(pact);
            // Tied maxima have no verified recipient selection rule here.
            // An unreadable active pact is not a zero-layer competitor.
            if (layers == policy.observed_pact_layers.end() ||
                (pact != policy.primary_pact && layers->second >= primary->second)) {
                return 0;
            }
        }
        return 1200;
    }
    const auto growth = growth_profiles().find(name);
    if (growth != growth_profiles().end() && !growth->second.dynamic_recipients &&
        is_primary_growth_engine(name, policy.primary_pact)) {
        return 1500;
    }
    for (const auto& [target, owned] : policy.owned) {
        if (owned.deployed && (owned.copies > 0 || owned.elite) &&
            is_front_growth_target(name, target, policy.primary_pact)) {
            return 1200;
        }
    }
    return 0;
}

bool asst::spa::belongs_to_planned_lineup(
    const std::string& name,
    const PreparationPolicy& policy,
    const std::map<std::string, std::vector<std::string>>& pacts)
{
    const auto traits = pacts.find(name);
    if (traits == pacts.end() || policy.primary_pact.empty() || policy.primary_pact == "auto" ||
        contains(policy.disabled_pacts, policy.primary_pact)) {
        return false;
    }
    if (contains(traits->second, policy.primary_pact) || primary_growth_support_value(name, policy, pacts) > 0) {
        return true;
    }
    const auto supports = planning_support_pacts(policy, { { name, 0, 0 } }, pacts);
    const auto requirements = pact_requirements();
    for (const auto& support : supports) {
        if (!contains(traits->second, support) || contains(policy.disabled_pacts, support)) {
            continue;
        }
        const auto rule = std::find_if(requirements.begin(), requirements.end(), [&](const auto& entry) {
            return entry.name == support && !entry.core;
        });
        if (rule == requirements.end()) {
            continue;
        }
        int members = 0;
        for (const auto& [owned_name, owned] : policy.owned) {
            const auto memberships = pacts.find(owned_name);
            if (owned_name != name && (owned.copies > 0 || owned.elite) && (owned.deployed || rule->bench_counts) &&
                memberships != pacts.end() && contains(memberships->second, support)) {
                ++members;
            }
        }
        // A label alone has no combat value: this member must activate the
        // planned secondary, or already belong to its active roster.
        if (members + 1 >= rule->members) {
            return true;
        }
    }
    return false;
}

asst::spa::PrimaryRoleAssessment asst::spa::assess_primary_roles(
    const std::string& pact,
    const std::vector<std::string>& deployed_names,
    const std::map<std::string, std::vector<std::string>>& pacts)
{
    PrimaryRoleAssessment result;
    if (pact.empty()) {
        return result;
    }
    std::set<std::string> seen;
    for (const auto& name : deployed_names) {
        if (name.empty() || !seen.insert(name).second) {
            continue;
        }
        const auto found = pacts.find(name);
        if (found == pacts.end()) {
            continue;
        }
        // A support source such as the Lord of Fiends grows active core
        // pacts without belonging to their printed operator membership.
        if (is_primary_growth_engine(name, pact)) {
            ++result.growth_engine_owned;
            result.growth_engines.emplace_back(name);
        }
        if (contains(found->second, pact) && is_primary_output_candidate(name, pact)) {
            ++result.output_owned;
            result.outputs.emplace_back(name);
        }
    }
    return result;
}

asst::spa::DynamicPactChoice asst::spa::choose_dynamic_primary_pact(
    const PreparationPolicy& policy,
    const std::vector<ShopOffer>& offers,
    const std::map<std::string, std::vector<std::string>>& pacts,
    int available_funds)
{
    DynamicPactChoice result;
    const auto requirements = pact_requirements();
    const auto is_enabled = [&](const std::string& pact) {
        const auto rule =
            std::find_if(requirements.begin(), requirements.end(), [&](const auto& item) { return item.name == pact; });
        if (rule != requirements.end() && !rule->core) {
            return route_profiles().contains(pact);
        }
        return policy.enabled_core_pacts.empty() || contains(policy.enabled_core_pacts, pact);
    };
    const auto is_disabled = [&](const std::string& pact) {
        return contains(policy.disabled_pacts, pact);
    };
    const std::string preferred = policy.primary_pact == "auto" ? std::string() : policy.primary_pact;
    const bool use_requested = !policy.requested_primary_pact.empty() && policy.requested_primary_pact != "auto" &&
                               !is_disabled(policy.requested_primary_pact) && is_enabled(policy.requested_primary_pact);
    const auto guided = guided_primary_choices(policy.selected_strategy_id, policy.disabled_pacts);
    // Admit a documented auxiliary-led primary only when it has a route
    // profile. Ordinary card-driven fallback still searches core factions.
    const bool use_guide = !use_requested && guided.guide_found &&
                           std::any_of(guided.pacts.begin(), guided.pacts.end(), [&](const auto& pact) {
                               return is_enabled(pact) && !is_disabled(pact);
                           });
    result.guide_restricted = use_guide;
    result.noncore_guide_pending = std::any_of(guided.pacts.begin(), guided.pacts.end(), [&](const auto& pact) {
        return std::any_of(requirements.begin(), requirements.end(), [&](const auto& item) {
            return item.name == pact && !item.core && !route_profiles().contains(pact);
        });
    });
    for (const auto& requirement : requirements) {
        const bool guided_auxiliary = !requirement.core && route_profiles().contains(requirement.name) &&
                                      ((use_guide && contains(guided.pacts, requirement.name)) ||
                                       (use_requested && requirement.name == policy.requested_primary_pact));
        if ((!requirement.core && !guided_auxiliary) || requirement.members <= 0 || !is_enabled(requirement.name) ||
            is_disabled(requirement.name) || (use_requested && requirement.name != policy.requested_primary_pact) ||
            (use_guide && !contains(guided.pacts, requirement.name))) {
            continue;
        }
        DynamicPactSignal signal;
        signal.pact = requirement.name;
        signal.required_members = requirement.members;
        signal.minimum_shop_level = conservative_minimum_shop_level(requirement.name);
        std::set<std::string> owned_members;
        std::set<std::string> deployed;
        std::set<std::string> shop;
        std::set<std::string> growth_names;
        std::set<std::string> output_names;
        std::map<std::string, int> new_member_prices;
        int dual_role_owned = 0;
        int dual_role_offers = 0;
        for (const auto& [name, owned] : policy.owned) {
            const auto found = pacts.find(name);
            if ((owned.copies <= 0 && !owned.elite) || found == pacts.end() ||
                !contains(found->second, requirement.name)) {
                continue;
            }
            owned_members.insert(name);
            if (owned.deployed) {
                deployed.insert(name);
            }
            if (is_primary_growth_engine(name, requirement.name)) {
                growth_names.insert(name);
                ++signal.growth_engine_owned;
                const auto profile = growth_profiles().find(name);
                signal.growth_engine_eligible +=
                    owned.deployed || (profile != growth_profiles().end() && profile->second.works_on_bench);
            }
            if (is_primary_output_candidate(name, requirement.name)) {
                output_names.insert(name);
                ++signal.output_owned;
            }
            dual_role_owned +=
                is_primary_growth_engine(name, requirement.name) && is_primary_output_candidate(name, requirement.name);
        }
        for (const auto& offer : offers) {
            if (offer.price < 0 || (available_funds >= 0 && offer.price > available_funds)) {
                continue;
            }
            const auto found = pacts.find(offer.name);
            if (found == pacts.end() || !contains(found->second, requirement.name)) {
                continue;
            }
            if (!owned_members.contains(offer.name)) {
                const auto [quote, inserted] = new_member_prices.emplace(offer.name, offer.price);
                if (!inserted) {
                    quote->second = std::min(quote->second, offer.price);
                }
            }
            if (!shop.insert(offer.name).second) {
                continue;
            }
            const bool new_growth =
                is_primary_growth_engine(offer.name, requirement.name) && growth_names.insert(offer.name).second;
            const bool new_output =
                is_primary_output_candidate(offer.name, requirement.name) && output_names.insert(offer.name).second;
            if (new_growth) {
                ++signal.growth_engine_offers;
            }
            if (new_output) {
                ++signal.output_offers;
            }
            dual_role_offers += new_growth && new_output;
        }
        signal.distinct_members = static_cast<int>(owned_members.size() + new_member_prices.size());
        signal.reachable_members = signal.distinct_members;
        if (available_funds >= 0) {
            std::vector<int> costs;
            for (const auto& [name, price] : new_member_prices) {
                costs.push_back(price);
            }
            std::sort(costs.begin(), costs.end());
            int budget = available_funds;
            int slots = policy.bench_occupied >= 0 ? std::max(0, 10 - policy.bench_occupied) : 0;
            signal.reachable_members = static_cast<int>(owned_members.size());
            for (int cost : costs) {
                if (slots == 0 || cost > budget) {
                    break;
                }
                ++signal.reachable_members;
                --slots;
                budget -= cost;
            }
        }
        signal.deployed_members = static_cast<int>(deployed.size());
        signal.native_growth_available = requirement.name == "阿戈尔" && signal.deployed_members >= requirement.members;
        signal.shop_members = static_cast<int>(shop.size());
        signal.activation_ready = signal.reachable_members >= signal.required_members;
        signal.book_window_met = policy.shop_level <= 0 || policy.shop_level >= signal.minimum_shop_level ||
                                 signal.deployed_members >= signal.required_members;
        signal.role_signal = signal.growth_engine_owned > 0 || signal.growth_engine_offers > 0 ||
                             signal.output_owned > 0 || signal.output_offers > 0;
        // A deployed member is stronger evidence than an unbought offer. A
        // stacker/output appearing now is the next strongest signal; density
        // breaks ties. The preferred opening pact gets only a small tie-break,
        // so it cannot lock the run to a faction that never appears.
        // A single early transition card must not lock the route.  A live
        // shop offer that is a named stacker or terminal is stronger evidence
        // than one already-owned member, because the user can still pivot
        // before activation.  Existing deployed members and multi-card
        // density retain weight so an established route is not abandoned for
        // a random off-faction card.
        signal.score = signal.deployed_members * 500 + signal.growth_engine_offers * 1000 +
                       signal.growth_engine_owned * 400 + signal.output_offers * 900 + signal.output_owned * 300 +
                       signal.shop_members * 200 + signal.reachable_members * 80 +
                       (requirement.name == preferred ? 25 : 0);
        // One physical identity can fill both roles; it is still one live
        // card, not a two-card activation package. Count its stronger signal
        // once instead of making an established route oscillate to it.
        signal.score -= dual_role_owned * 300 + dual_role_offers * 900;
        signal.inferred_contention =
            inferred_route_contention(signal.pact, policy.occupied_strategy_ids, policy.disabled_pacts);
        // A soft preference keeps every legal fallback available and preserves
        // the investment/budget checks below for an already developed faction.
        signal.score -= signal.inferred_contention * 6;
        // A faction can be worth keeping before its named engine/carry is
        // visible when the observed distinct-member density already reaches
        // the activation threshold.  This is the important distinction
        // between an opening preference and a live shop decision: density is
        // evidence, while a missing engine/carry lowers the score and may
        // still make the faction lose to a better candidate.
        signal.viable = signal.role_signal || signal.activation_ready || signal.deployed_members >= requirement.members;
        // A low book is a timing constraint, not a route rejection.  Growth
        // hands commonly appear in III-IV while an opening shop is still at
        // I-II; keep the route as a provisional line when the live pool has
        // density or a named role signal, and let the economy planner decide
        // when to upgrade. Missing activation is never a round-number cutoff.
        if (signal.viable) {
            result.ranking.emplace_back(signal);
        }
    }

    std::sort(result.ranking.begin(), result.ranking.end(), [&](const auto& lhs, const auto& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        if (lhs.reachable_members != rhs.reachable_members) {
            return lhs.reachable_members > rhs.reachable_members;
        }
        if (lhs.growth_engine_offers != rhs.growth_engine_offers) {
            return lhs.growth_engine_offers > rhs.growth_engine_offers;
        }
        return lhs.pact < rhs.pact;
    });
    if (result.ranking.empty()) {
        result.reason = "No enabled faction has a verified current growth or output signal";
        return result;
    }
    result.valid = true;
    result.primary_pact = result.ranking.front().pact;
    result.score = result.ranking.front().score;
    // Aegir has a route-specific engine in the pact itself. Converge on
    // that owned three-member base before chasing another route's outputs.
    // Observed layers protect an already more developed alternative; they
    // are an investment check, never comparable DPS across different pacts.
    const auto native = std::find_if(result.ranking.begin(), result.ranking.end(), [](const auto& signal) {
        return signal.native_growth_available;
    });
    if (native != result.ranking.end()) {
        auto alternative = std::find_if(result.ranking.begin(), result.ranking.end(), [&](const auto& signal) {
            return signal.pact == preferred && signal.pact != native->pact;
        });
        if (alternative == result.ranking.end() && result.ranking.front().pact != native->pact) {
            alternative = result.ranking.begin();
        }
        const bool other_engine_ready = alternative != result.ranking.end() &&
                                        alternative->deployed_members >= alternative->required_members &&
                                        alternative->growth_engine_eligible > 0;
        const auto native_layers = policy.observed_pact_layers.find(native->pact);
        const auto other_layers = alternative == result.ranking.end()
                                      ? policy.observed_pact_layers.end()
                                      : policy.observed_pact_layers.find(alternative->pact);
        const bool investment_not_worse = native_layers != policy.observed_pact_layers.end() &&
                                          other_layers != policy.observed_pact_layers.end() &&
                                          native_layers->second >= other_layers->second;
        if (!other_engine_ready || investment_not_worse) {
            result.primary_pact = native->pact;
            result.score = native->score;
            result.reason = "Converge on the owned Aegir consumption engine; build its chain and recruit its output";
            return result;
        }
    }
    // An activated transition faction is not a permanent commitment. Keep
    // its actual layer investment, and require an affordable activation plus
    // growth hand before considering a switch. This never gates a battle.
    if (policy.primary_locked_after_activation && !preferred.empty() && is_enabled(preferred) &&
        !is_disabled(preferred) && result.primary_pact != preferred &&
        (!use_guide || contains(guided.pacts, preferred)) &&
        (!use_requested || preferred == policy.requested_primary_pact)) {
        const auto current = std::find_if(result.ranking.begin(), result.ranking.end(), [&](const auto& signal) {
            return signal.pact == preferred;
        });
        const auto layers = policy.observed_pact_layers.find(preferred);
        const auto keep_current = [&](const std::string& reason) {
            result.primary_pact = preferred;
            result.score = current == result.ranking.end() ? 0 : current->score;
            result.reason = reason;
        };
        if (layers == policy.observed_pact_layers.end() || available_funds < 0) {
            keep_current("Keep the active faction while its layer investment or replacement budget is unknown");
            return result;
        }
        const auto& challenger = result.ranking.front();
        std::set<std::string> owned_members;
        bool owned_engine = false;
        for (const auto& [name, unit] : policy.owned) {
            const auto traits = pacts.find(name);
            if ((unit.copies <= 0 && !unit.elite) || traits == pacts.end() ||
                !contains(traits->second, challenger.pact)) {
                continue;
            }
            owned_members.insert(name);
            owned_engine = owned_engine || is_primary_growth_engine(name, challenger.pact);
        }
        std::map<std::string, int> priced_members;
        for (const auto& offer : offers) {
            const auto traits = pacts.find(offer.name);
            if (offer.price < 0 || offer.price > available_funds || owned_members.contains(offer.name) ||
                traits == pacts.end() || !contains(traits->second, challenger.pact)) {
                continue;
            }
            const auto existing = priced_members.find(offer.name);
            if (existing == priced_members.end() || offer.price < existing->second) {
                priced_members[offer.name] = offer.price;
            }
        }
        // At most six distinct visible offers. Enumerate actual packages,
        // rather than counting three cards that the budget cannot purchase.
        bool affordable_growth_package = false;
        if (priced_members.size() <= 6) {
            const std::vector<std::pair<std::string, int>> packages(priced_members.begin(), priced_members.end());
            for (size_t mask = 0; mask < (size_t { 1 } << packages.size()); ++mask) {
                int cost = 0;
                int slots = 0;
                size_t members = owned_members.size();
                bool engine = owned_engine;
                for (size_t index = 0; index < packages.size(); ++index) {
                    if ((mask & (size_t { 1 } << index)) != 0) {
                        cost += packages[index].second;
                        ++slots;
                        ++members;
                        engine = engine || is_primary_growth_engine(packages[index].first, challenger.pact);
                    }
                }
                if (cost <= available_funds && slots <= std::max(0, 10 - policy.bench_occupied) &&
                    members >= static_cast<size_t>(challenger.required_members) && engine) {
                    affordable_growth_package = true;
                    break;
                }
            }
        }
        // Layer count is a sunk-investment preference, not a damage forecast.
        const int retention = 450 + std::clamp(layers->second, 0, 999) * 20;
        if (!affordable_growth_package ||
            (current != result.ranking.end() && result.score < current->score + retention)) {
            keep_current("Keep the active faction until a funded growth package outweighs its existing investment");
            return result;
        }
    }
    // Do not oscillate between two one-card routes because a single early
    // offer changes the raw score. Before activation, retain a route that
    // already owns at least one distinct member unless the challenger is
    // materially stronger; once two members are owned, require an even
    // larger lead to abandon the developing line. This keeps the opening
    // preference provisional while preserving the user's “follow the cards”
    // rule when a genuinely denser route appears.
    if (!preferred.empty() && result.primary_pact != preferred) {
        const auto current = std::find_if(result.ranking.begin(), result.ranking.end(), [&](const auto& signal) {
            return signal.pact == preferred;
        });
        if (current != result.ranking.end() && current->distinct_members > 0) {
            // Two fielded members with a ready engine are a real investment
            // even before activation. Unbought engines receive a high score
            // and lose it on purchase, so an incomplete speculative package
            // can otherwise consume the round budget and immediately lose
            // to the same installed formation.
            const bool established_opening = !policy.primary_locked_after_activation && available_funds >= 0 &&
                                             current->deployed_members >= std::max(2, current->required_members - 1) &&
                                             current->growth_engine_eligible > 0;
            if (established_opening && !result.ranking.front().activation_ready) {
                result.primary_pact = preferred;
                result.score = current->score;
                result.reason = "Keep the fielded growth engine until the alternative can fund an activation";
                return result;
            }
            const int hysteresis = current->distinct_members >= 2 ? 450 : 300;
            if (result.score < current->score + hysteresis) {
                result.primary_pact = preferred;
                result.score = current->score;
                result.reason = "Keep the developing faction until a challenger has a materially stronger live signal";
            }
        }
    }
    if (result.reason.empty()) {
        result.reason = result.primary_pact == preferred
                            ? "Keep the current faction because its observed growth/output signal remains strongest"
                            : "Pivot to the faction with the strongest observed density, growth hand, or output hand";
    }
    return result;
}

int asst::spa::dynamic_target_shop_level(
    const PreparationPolicy& policy,
    const std::map<std::string, std::vector<std::string>>& pacts)
{
    const auto route = route_profiles().find(policy.primary_pact);
    int target = std::max(2, route == route_profiles().end() ? 2 : route->second.minimum_shop_level);
    if (policy.early_board_ready && policy.last_round_life_loss <= 5) {
        target = std::max(target, 4);
    }
    int members = 0;
    int deployed = 0;
    bool harmony = false;
    bool egir_terminal_owned = false;
    for (const auto& [name, unit] : policy.owned) {
        if (unit.copies <= 0 && !unit.elite) {
            continue;
        }
        deployed += unit.deployed ? 1 : 0;
        egir_terminal_owned = egir_terminal_owned || name == "乌尔比安" || name == "克莱门莎";
        const auto found = pacts.find(name);
        harmony = harmony || (unit.deployed && found != pacts.end() && contains(found->second, "调和") &&
                              !contains(policy.disabled_pacts, "调和"));
        if (found != pacts.end() && contains(found->second, policy.primary_pact)) {
            ++members;
        }
    }
    // The route's minimum is its starting window, not the final shop cap.
    // Open terminal tiers after assembling the faction, and tier VI once the
    // deployed roster can support it. Never substitute a round number for a tier.
    if (members + (harmony ? 1 : 0) >= 3) {
        // Egir can obtain Clementia from the book-V promotion reward, or
        // Urbian directly at V. Already owning an attack terminal is not a
        // reason to spend another round's budget opening VI by population.
        const bool keep_egir_terminal_window =
            policy.primary_pact == "阿戈尔" && egir_terminal_owned && policy.last_round_life_loss < 6;
        target = std::max(target, deployed >= 6 && !keep_egir_terminal_window ? 6 : 5);
    }
    else if (members >= 2 && deployed >= 4) {
        target = std::max(target, 4);
    }
    return target;
}

std::optional<asst::spa::FixedGrowthPurchaseReservation> asst::spa::fixed_growth_purchase_reservation(
    const PreparationPolicy& policy,
    int funds,
    int upgrade_price,
    const std::vector<ShopOffer>& offers,
    const std::map<std::string, std::vector<std::string>>& pacts)
{
    const auto layers = policy.observed_pact_layers.find(policy.primary_pact);
    if (!policy.dynamic_decision_mode || !policy.growth_primary_active_verified || policy.primary_pact.empty() ||
        policy.primary_pact == "auto" || contains(policy.disabled_pacts, policy.primary_pact) ||
        layers == policy.observed_pact_layers.end() || layers->second < 0 || funds < 0 || upgrade_price < 0 ||
        upgrade_price > funds || policy.round < 1 || policy.growth_scenario_horizon_round < policy.round ||
        policy.growth_scenario_horizon_round > 20 || policy.bench_occupied < 0 || policy.bench_occupied >= 10) {
        return std::nullopt;
    }
    // choose_preparation does not call the upgrade policy. Reuse its current
    // affordable choice instead of reserving funds for an unrelated catalogue
    // entry or a hypothetical future refresh.
    const auto choice = choose_preparation(policy, funds, offers, pacts);
    if (choice.action != PreparationAction::Buy || choice.price < 0 || choice.price > funds ||
        (!choice.selected_pact.empty() && choice.selected_pact != policy.primary_pact)) {
        return std::nullopt;
    }
    const auto owned = policy.owned.find(choice.name);
    if (owned != policy.owned.end() && (owned->second.copies != 0 || owned->second.elite)) {
        // A second initial copy is not a new guaranteed trigger. Completing
        // an observed promotion already has its own upgrade protection.
        return std::nullopt;
    }
    const auto source = growth_profiles().find(choice.name);
    if (source == growth_profiles().end() || source->second.kind != GrowthKind::FixedPhase ||
        !contains(source->second.pacts, policy.primary_pact)) {
        return std::nullopt;
    }
    const auto& profile = source->second;
    if (!profile.works_on_bench && (policy.deployed_population < 0 || policy.deployed_population >= 8)) {
        // A positive replacement heuristic does not prove that swapping this
        // body preserves the other growth sources. Keep this guard narrow.
        return std::nullopt;
    }
    const auto override = profile.pact_formulas.find(policy.primary_pact);
    const int gain = override == profile.pact_formulas.end() ? profile.normal_gain : override->second.normal_gain;
    if (gain <= 0) {
        return std::nullopt;
    }
    const auto scenario = project_primary_growth(
        policy.primary_pact,
        policy.round,
        policy.growth_scenario_horizon_round,
        true,
        { { choice.name, false, !profile.works_on_bench } });
    if (!scenario.valid || scenario.fixed_phase_additions <= 0) {
        return std::nullopt;
    }
    return FixedGrowthPurchaseReservation { choice.name,
                                            choice.price,
                                            layers->second,
                                            policy.growth_scenario_horizon_round,
                                            gain,
                                            scenario.fixed_phase_additions / gain,
                                            scenario.fixed_phase_additions,
                                            !profile.works_on_bench,
                                            funds - upgrade_price < choice.price };
}

namespace asst::spa
{
struct SameShopPromotionPurchase
{
    const ShopOffer* first;
    int total_cost;
};

static std::optional<SameShopPromotionPurchase> affordable_same_shop_promotion(
    const PreparationPolicy& policy,
    int funds,
    const std::vector<ShopOffer>& offers,
    const std::map<std::string, std::vector<std::string>>& pacts)
{
    if (!policy.dynamic_decision_mode || policy.tactical_elite_purchase_pending || policy.boss_preparation ||
        policy.deployed_population < 4 || policy.bench_occupied < 0 || policy.bench_occupied > 8 ||
        policy.primary_pact.empty() || policy.primary_pact == "auto" ||
        contains(policy.disabled_pacts, policy.primary_pact)) {
        return std::nullopt;
    }
    std::set<int> slots;
    std::map<std::string, std::vector<const ShopOffer*>> by_name;
    for (const auto& offer : offers) {
        if (offer.right_slot < 0 || offer.right_slot > 5 || !slots.insert(offer.right_slot).second || offer.price < 0 ||
            offer.price > 99) {
            return std::nullopt;
        }
        const auto membership = pacts.find(offer.name);
        if (offer.price <= funds && membership != pacts.end() && contains(membership->second, policy.primary_pact)) {
            by_name[offer.name].push_back(&offer);
        }
    }
    std::optional<SameShopPromotionPurchase> best;
    for (auto& [name, quotes] : by_name) {
        const auto owned = policy.owned.find(name);
        const auto forms =
            owned == policy.owned.end() ? std::optional(OperatorFormCounts {}) : owned_form_counts(owned->second);
        if (!forms || forms->elite > 0) {
            continue;
        }
        const auto override = policy.promotion_copies.find(name);
        const int required =
            override == policy.promotion_copies.end() ? promotion_copies_required(name) : override->second;
        const int missing = required - forms->initial;
        if (missing < 2 || missing > static_cast<int>(quotes.size()) || missing > 10 - policy.bench_occupied) {
            continue;
        }
        std::sort(quotes.begin(), quotes.end(), [](const auto* lhs, const auto* rhs) {
            return std::tie(lhs->price, lhs->right_slot) < std::tie(rhs->price, rhs->right_slot);
        });
        int cost = 0;
        for (int index = 0; index < missing; ++index) {
            cost += quotes[index]->price;
        }
        const bool primary_role = is_primary_growth_engine(name, policy.primary_pact) ||
                                  is_primary_output_candidate(name, policy.primary_pact);
        if (cost <= funds && (cost <= 3 || primary_role) && (!best || cost < best->total_cost)) {
            best = SameShopPromotionPurchase { quotes.front(), cost };
        }
    }
    return best;
}

static int known_primary_acquisition_layers(const PreparationPolicy& policy, const std::string& name)
{
    const auto observed = policy.observed_pact_layers.find(policy.primary_pact);
    if (!policy.growth_primary_active_verified || observed == policy.observed_pact_layers.end() ||
        observed->second < 0 || observed->second >= 999) {
        return 0;
    }
    const auto effects = acquired_effects(name, policy.tactical_elite_purchase_pending, policy.shop_level);
    int layers = 0;
    if (effects) {
        for (const auto& effect : *effects) {
            if (effect.kind == AcquiredEffectKind::Layers && effect.target == policy.primary_pact &&
                !effect.requires_observed_amount) {
                layers += effect.amount;
            }
        }
    }
    return std::min(layers, 999 - observed->second);
}

static bool first_fitting_growth_purchase(const PreparationPolicy& policy, const std::string& name, int layers)
{
    if (layers < 4 || policy.deployed_population < 0) {
        return false;
    }
    const auto owned = policy.owned.find(name);
    if (owned != policy.owned.end() && (owned->second.copies != 0 || owned->second.elite)) {
        return false;
    }
    const auto fit = policy.full_field_offer_improvements.find(name);
    return policy.deployed_population < 8 || (fit != policy.full_field_offer_improvements.end() && fit->second > 0);
}

static int preparation_growth_weight(const PreparationPolicy& policy)
{
    if (!policy.dynamic_decision_mode || policy.boss_preparation || policy.round < 1) {
        return 0;
    }
    const int opportunities = std::clamp(policy.growth_scenario_horizon_round - policy.round - 1, 0, 7);
    const auto observed = policy.observed_pact_layers.find(policy.primary_pact);
    const int layer_need =
        observed == policy.observed_pact_layers.end() ? 750 : 1000 - std::clamp(observed->second, 0, 999);
    const int pressure = std::clamp(policy.last_round_life_loss, 0, 10) * 7;
    return 800 * opportunities * layer_need / 1000 * (100 - pressure) / 100;
}

static std::optional<ShopUpgradeHold> plan_shop_upgrade_hold(
    const PreparationPolicy& policy,
    int funds,
    int upgrade_price,
    const std::vector<ShopOffer>& offers,
    const std::map<std::string, std::vector<std::string>>& pacts)
{
    const auto route = route_profiles().find(policy.primary_pact);
    const bool paid_boss_upgrade = policy.boss_preparation && upgrade_price > 0;
    if (route == route_profiles().end() || contains(policy.disabled_pacts, policy.primary_pact)) {
        return paid_boss_upgrade
                   ? std::optional(
                         ShopUpgradeHold { "Keep boss-round funds until a currently useful recruit is identified" })
                   : std::nullopt;
    }
    const bool accepts_recruits = policy.bench_occupied >= 0 && policy.bench_occupied < 10;
    const bool below_growth_window = policy.shop_level < std::max(3, route->second.minimum_shop_level);
    const bool can_refresh = policy.allow_refresh && !policy.shop_frozen_this_round;
    const int refresh_cost = policy.free_refreshes > 0 ? 0 : 1;
    const bool next_preparation =
        !policy.boss_preparation && policy.round > 0 && policy.economy_scenario_horizon_round > policy.round;
    const bool stable_board = policy.early_board_ready && policy.last_round_life_loss >= 0 &&
                              policy.last_round_life_loss <= 2 && policy.current_life >= 8;
    const bool discount_wait_available =
        policy.shop_discount_wait_level != policy.shop_level || policy.shop_discount_wait_round == policy.round;
    const bool inactive_route = !policy.primary_locked_after_activation && !policy.growth_primary_active_verified;
    if (policy.shop_level >= 2 && accepts_recruits && upgrade_price > 0 && inactive_route && next_preparation) {
        const auto rules = pact_requirements();
        const auto rule = std::find_if(rules.begin(), rules.end(), [&](const auto& candidate) {
            return candidate.core && candidate.name == policy.primary_pact;
        });
        const auto belongs_to_primary = [&](const std::string& name) {
            const auto member = pacts.find(name);
            return member != pacts.end() && contains(member->second, policy.primary_pact);
        };
        const auto owns = [&](const std::string& name) {
            const auto owned = policy.owned.find(name);
            return owned != policy.owned.end() && (owned->second.copies > 0 || owned->second.elite);
        };
        int known_members = 0;
        bool member_fielded = false;
        bool harmony_fielded = false;
        for (const auto& [name, owned] : policy.owned) {
            if (owned.copies <= 0 && !owned.elite) {
                continue;
            }
            if (belongs_to_primary(name)) {
                ++known_members;
                member_fielded = member_fielded || owned.deployed;
            }
            const auto traits = pacts.find(name);
            harmony_fielded =
                harmony_fielded || (owned.deployed && traits != pacts.end() && contains(traits->second, "调和") &&
                                    !contains(policy.disabled_pacts, "调和"));
        }
        const int missing = rule == rules.end() ? 0 : rule->members - known_members - (harmony_fielded ? 1 : 0);
        if (member_fielded && missing > 0) {
            const auto is_funded_activation_offer = [&](const ShopOffer& offer) {
                const auto fit = policy.full_field_offer_improvements.find(offer.name);
                const bool fieldable = (policy.deployed_population >= 0 && policy.deployed_population < 8) ||
                                       (fit != policy.full_field_offer_improvements.end() && fit->second > 0);
                return offer.right_slot >= 0 && offer.right_slot <= 5 && offer.price > funds - upgrade_price &&
                       offer.price <= funds && belongs_to_primary(offer.name) && !owns(offer.name) && fieldable;
            };
            if (std::ranges::any_of(offers, is_funded_activation_offer)) {
                const auto purchase = choose_preparation(policy, funds, offers, pacts);
                if (purchase.action == PreparationAction::Buy && !purchase.is_equipment &&
                    std::ranges::any_of(offers, [&](const auto& offer) {
                        return offer.name == purchase.name && offer.right_slot == purchase.right_slot &&
                               is_funded_activation_offer(offer);
                    })) {
                    return ShopUpgradeHold { "Recruit " + purchase.name +
                                             " toward primary-pact activation before upgrading" };
                }
            }
            const auto retention = round_balance_retention(policy.selected_strategy_id, funds);
            if (!below_growth_window && stable_board && can_refresh && discount_wait_available && retention &&
                !retention->carries) {
                // If the existing tiers can supply all missing names, spend
                // one round looking for activation before opening more cards.
                // Catalogue access is a possibility, not a forecast draw.
                int available_names = 0;
                int recruit_cost = funds + 1;
                std::optional<int> growth_recruit_cost;
                for (const auto& card : OperatorCatalogue) {
                    const std::string name(card.name);
                    if (card.tier <= policy.shop_level && card.price > 0 && belongs_to_primary(name) && !owns(name)) {
                        ++available_names;
                        recruit_cost = std::min(recruit_cost, card.price);
                        if (is_primary_growth_engine(name, policy.primary_pact)) {
                            growth_recruit_cost = std::min(growth_recruit_cost.value_or(card.price), card.price);
                        }
                    }
                }
                // When only one member is missing, look for a card that both
                // activates the pact and contributes to growth. If that card
                // cannot be funded this round, keep the cheapest viable route.
                const int growth_budget =
                    refresh_cost + std::max(growth_recruit_cost.value_or(recruit_cost), policy.minimum_recruit_price);
                const int search_budget = missing == 1 && funds >= growth_budget
                                              ? growth_budget
                                              : refresh_cost + std::max(recruit_cost, policy.minimum_recruit_price);
                if (available_names >= missing && funds >= search_budget && funds - upgrade_price < search_budget) {
                    return ShopUpgradeHold {
                        "Reserve a refresh and recruit at the current tier for primary-pact activation",
                        {},
                        0,
                        std::max(0, upgrade_price - 1)
                    };
                }
            }
        }
    }
    if (below_growth_window && !paid_boss_upgrade) {
        // A route's recommended entry tier can be higher than a visible
        // engine's tier. A paid upgrade must not erase its recruitment budget.
        if (policy.shop_level < 2 || !accepts_recruits || upgrade_price <= 0 || policy.boss_preparation ||
            policy.primary_locked_after_activation || policy.growth_primary_active_verified || policy.round < 1 ||
            policy.growth_scenario_horizon_round <= policy.round) {
            return std::nullopt;
        }
        const auto purchase = choose_preparation(policy, funds, offers, pacts);
        const auto traits = pacts.find(purchase.name);
        const auto fit = policy.full_field_offer_improvements.find(purchase.name);
        const bool fieldable = (policy.deployed_population >= 0 && policy.deployed_population < 8) ||
                               (fit != policy.full_field_offer_improvements.end() && fit->second > 0);
        if (purchase.action == PreparationAction::Buy && !purchase.is_equipment &&
            purchase.price > funds - upgrade_price && !policy.owned.contains(purchase.name) && traits != pacts.end() &&
            contains(traits->second, policy.primary_pact) &&
            is_primary_growth_engine(purchase.name, policy.primary_pact) && fieldable) {
            return ShopUpgradeHold { "Recruit " + purchase.name +
                                     " while its verified quote fits the current growth plan" };
        }
        return std::nullopt;
    }
    const bool book_five = policy.shop_level == 5;
    const bool terminal_owned = std::any_of(policy.owned.begin(), policy.owned.end(), [&](const auto& entry) {
        return (entry.second.copies > 0 || entry.second.elite) &&
               is_terminal_output_candidate(entry.first, policy.primary_pact);
    });
    const bool engine_owned = std::any_of(policy.owned.begin(), policy.owned.end(), [&](const auto& entry) {
        return (entry.second.copies > 0 || entry.second.elite) &&
               is_primary_growth_engine(entry.first, policy.primary_pact);
    });
    int new_tier_recruit_price = 0;
    for (const auto& card : OperatorCatalogue) {
        if (card.tier != policy.shop_level + 1 || card.price <= 0) {
            continue;
        }
        const std::string name(card.name);
        const auto membership = pacts.find(name);
        const auto owned = policy.owned.find(name);
        if (membership == pacts.end() ||
            (owned != policy.owned.end() && (owned->second.copies > 0 || owned->second.elite))) {
            continue;
        }
        // The live controller also fills carries from the route's own output
        // list. Those alternatives still need to fix a missing/weak role.
        const bool explicit_carry =
            contains(policy.carries, name) && !is_primary_output_candidate(name, policy.primary_pact);
        if (!explicit_carry && !contains(membership->second, policy.primary_pact)) {
            continue;
        }
        const bool direct_output = is_terminal_output_candidate(name, policy.primary_pact) &&
                                   (!terminal_owned || policy.last_round_life_loss >= 6);
        if (explicit_carry || direct_output || (!engine_owned && is_primary_growth_engine(name, policy.primary_pact))) {
            new_tier_recruit_price =
                new_tier_recruit_price > 0 ? std::min(new_tier_recruit_price, card.price) : card.price;
        }
    }

    // A lower-tier pair can produce a capped tier-VI reward at book V.
    // At other levels, hold only for an actionable plan whose budget or
    // current-tier access benefits from waiting. The draw weights are unknown.
    const OperatorCatalogueEntry* pair = nullptr;
    if (accepts_recruits && can_refresh) {
        for (const auto& card : OperatorCatalogue) {
            if (card.tier > policy.shop_level || card.price <= 0 ||
                !ordinary_purchase_completes_promotion(policy, std::string(card.name))) {
                continue;
            }
            const std::string name(card.name);
            if (!belongs_to_planned_lineup(name, policy, pacts) ||
                (!book_five && card.tier != policy.shop_level &&
                 !is_primary_growth_engine(name, policy.primary_pact))) {
                continue;
            }
            if (!pair || card.price < pair->price || (card.price == pair->price && card.tier > pair->tier)) {
                pair = &card;
            }
        }
    }
    const bool upgrade_grants_faction_refresh =
        policy.selected_strategy_id == "band_pepe" && policy.primary_pact == "萨尔贡" && policy.shop_level % 2 == 1;
    if (paid_boss_upgrade) {
        const int boss_refresh_cost = upgrade_grants_faction_refresh ? 0 : refresh_cost;
        if (!accepts_recruits || !can_refresh || new_tier_recruit_price == 0 ||
            funds - upgrade_price < boss_refresh_cost + new_tier_recruit_price) {
            return ShopUpgradeHold {
                "Keep boss-round funds when a useful new-tier recruit cannot be bought this round"
            };
        }
    }
    if (book_five && new_tier_recruit_price == 0 && !upgrade_grants_faction_refresh) {
        return ShopUpgradeHold { "Book V already gives tier-VI promotion rewards; VI opens no missing route target",
                                 pair ? std::string(pair->name) : std::string(),
                                 pair ? pair->price + refresh_cost : 0 };
    }
    const bool can_roll_for_pair = pair && funds >= pair->price + refresh_cost;
    const bool pair_competes_for_gold = can_roll_for_pair && funds - upgrade_price < pair->price + refresh_cost;
    const bool preserve_book_five_pair = book_five && stable_board && (next_preparation || can_roll_for_pair);
    if (pair &&
        (preserve_book_five_pair ||
         (pair_competes_for_gold && (book_five || (stable_board && next_preparation && !new_tier_recruit_price))))) {
        return ShopUpgradeHold { "Use the current shop to complete an owned promotion before expanding the pool",
                                 std::string(pair->name),
                                 pair->price + refresh_cost };
    }
    if (!stable_board || !next_preparation || upgrade_price <= 0 || !accepts_recruits) {
        return std::nullopt;
    }
    // Waiting pays off only if this round's gold can do something useful.
    // During the next preparation, today's quote is replaced by actual OCR.
    const auto purchase = choose_preparation(policy, funds, offers, pacts);
    const bool current_purchase =
        purchase.action == PreparationAction::Buy && purchase.price > 0 && purchase.price > funds - upgrade_price;
    const bool can_open_and_recruit =
        can_refresh && new_tier_recruit_price > 0 && funds - upgrade_price >= refresh_cost + new_tier_recruit_price;
    if (!current_purchase || can_open_and_recruit) {
        return std::nullopt;
    }
    // A visible sale-price growth source can start stacking in the current
    // formation. A prior decision to wait for a cheaper upgrade does not
    // exhaust the recruitment priority for a newly available engine.
    const auto growth = growth_profiles().find(purchase.name);
    const auto fit = policy.full_field_offer_improvements.find(purchase.name);
    const bool fieldable = (policy.deployed_population >= 0 && policy.deployed_population < 8) ||
                           (fit != policy.full_field_offer_improvements.end() && fit->second > 0);
    if (!purchase.is_equipment && purchase.selected_pact == policy.primary_pact &&
        policy.growth_primary_active_verified && !policy.owned.contains(purchase.name) && fieldable &&
        growth != growth_profiles().end() && growth->second.kind == GrowthKind::SalePriceDependent &&
        contains(growth->second.pacts, policy.primary_pact)) {
        return ShopUpgradeHold { "Recruit " + purchase.name + " to add sale-price pact growth before upgrading" };
    }
    if (purchase.growth_score > 0 && discount_wait_available) {
        return ShopUpgradeHold { "Fund " + purchase.name + " for the current growth window before another book",
                                 {},
                                 0,
                                 std::max(0, upgrade_price - 1) };
    }
    if (!purchase.is_equipment && purchase.selected_pact == policy.primary_pact &&
        first_fitting_growth_purchase(policy, purchase.name, known_primary_acquisition_layers(policy, purchase.name))) {
        // This card improves the field and has a deterministic on-acquisition
        // effect. It can justify buying before a paid upgrade independently
        // of the one-round discount decision already taken at this book.
        return ShopUpgradeHold { "Reserve " + purchase.name +
                                 " for immediate main-pact growth and an improved lineup" };
    }
    if (discount_wait_available) {
        return ShopUpgradeHold { "Spend on the current board and review the one-gold cheaper upgrade next round",
                                 {},
                                 0,
                                 std::max(0, upgrade_price - 1) };
    }
    return std::nullopt;
}
}

bool asst::spa::should_upgrade_dynamic_shop(
    const PreparationPolicy& policy,
    int funds,
    int upgrade_price,
    const std::vector<ShopOffer>& offers,
    const std::map<std::string, std::vector<std::string>>& pacts,
    std::optional<ShopUpgradeHold>* hold)
{
    if (hold) {
        hold->reset();
    }
    if (!policy.dynamic_decision_mode || funds < 0 || upgrade_price < 0 || funds < upgrade_price ||
        policy.shop_level <= 0) {
        return false;
    }
    const int target_level = dynamic_target_shop_level(policy, pacts);
    if (policy.shop_level >= target_level) {
        return false;
    }
    const bool promotion_offered = std::any_of(offers.begin(), offers.end(), [&](const auto& offer) {
        return offer.price >= 0 && offer.price <= funds && ordinary_purchase_completes_promotion(policy, offer.name);
    });
    if (policy.shop_level < 5 && upgrade_price == 0) {
        return true;
    }
    if (promotion_offered) {
        return false;
    }
    if (const auto package = affordable_same_shop_promotion(policy, funds, offers, pacts);
        package && funds - upgrade_price < package->total_cost) {
        if (hold) {
            *hold = ShopUpgradeHold { "Reserve the visible, affordable same-shop promotion before upgrading",
                                      package->first->name,
                                      package->total_cost };
        }
        return false;
    }
    const auto retention = round_balance_retention(policy.selected_strategy_id, funds);
    if (policy.shop_level <= 2 && retention && !retention->carries && upgrade_price > 0 && policy.early_board_ready &&
        policy.last_round_life_loss == 0 && !policy.boss_preparation && policy.round > 0 &&
        policy.round < policy.economy_scenario_horizon_round) {
        if (policy.shop_discount_wait_level == policy.shop_level && policy.shop_discount_wait_round == policy.round) {
            return false;
        }
        if (policy.shop_discount_wait_level != policy.shop_level && policy.bench_occupied >= 0 &&
            policy.deployed_population + policy.bench_occupied <= 6) {
            const int budget_after_upgrade = funds - upgrade_price;
            const auto first = choose_preparation(policy, budget_after_upgrade, offers, pacts);
            if (first.action == PreparationAction::Buy && first.price > 0 && !first.banked_value_purchase &&
                budget_after_upgrade - first.price == 1 && !policy.owned.contains(first.name)) {
                auto second_offers = offers;
                std::erase_if(second_offers, [&](const auto& offer) { return offer.name == first.name; });
                auto projected = policy;
                projected.owned.emplace(first.name, OwnedOperator { 1, false, false });
                ++projected.bench_occupied;
                const auto second = choose_preparation(projected, funds - first.price, second_offers, pacts);
                if (second.action == PreparationAction::Buy && second.price > 1 &&
                    !policy.owned.contains(second.name) && belongs_to_planned_lineup(second.name, policy, pacts)) {
                    if (hold) {
                        *hold = ShopUpgradeHold { "Develop the current field with " + first.name + " and " +
                                                      second.name + " before paying for the next book",
                                                  {},
                                                  0,
                                                  std::max(0, upgrade_price - 1) };
                    }
                    return false;
                }
            }
        }
    }
    if (auto timing = plan_shop_upgrade_hold(policy, funds, upgrade_price, offers, pacts)) {
        if (hold) {
            *hold = std::move(timing);
        }
        return false;
    }
    if (upgrade_price == 0) {
        return true;
    }
    // A clean opening can still be too small for the next wave. Preserve at
    // least one immediately recruitable new body if the field and bench
    // together cannot reach the four-unit opening board.
    if (policy.round >= 2 && policy.round <= 4 && policy.deployed_population >= 0 && policy.deployed_population < 4 &&
        policy.bench_occupied >= 0 && policy.deployed_population + policy.bench_occupied < 4) {
        std::optional<int> recruit_budget;
        for (const auto& offer : offers) {
            if (offer.right_slot < 0 || offer.right_slot > 5 || offer.price <= 0 || offer.price > funds ||
                !pacts.contains(offer.name)) {
                continue;
            }
            const auto owned = policy.owned.find(offer.name);
            if (owned != policy.owned.end() && (owned->second.copies > 0 || owned->second.elite)) {
                continue;
            }
            recruit_budget = std::min(recruit_budget.value_or(offer.price), offer.price);
        }
        if (recruit_budget && funds - upgrade_price < *recruit_budget) {
            return false;
        }
    }
    // A visible first Specter is the early Egir chain head. Do not consume
    // its recruitment budget in the book-I shortcut merely because the
    // preceding wave was clean. This reserves an actionable purchase only;
    // absent cards, a full bench or another route do not delay advancement.
    if (policy.primary_pact == "阿戈尔" && !contains(policy.disabled_pacts, "阿戈尔") && policy.bench_occupied >= 0 &&
        policy.bench_occupied < 10 && policy.deployed_population >= 0 && policy.deployed_population < 8) {
        const auto ghost = policy.owned.find("幽灵鲨");
        const bool head_owned = ghost != policy.owned.end() && (ghost->second.copies > 0 || ghost->second.elite);
        if (!head_owned && std::any_of(offers.begin(), offers.end(), [&](const auto& offer) {
                return offer.name == "幽灵鲨" && offer.price >= 0 && offer.price <= funds;
            })) {
            return false;
        }
    }
    if (const auto reservation = fixed_growth_purchase_reservation(policy, funds, upgrade_price, offers, pacts);
        reservation && reservation->reserve_before_upgrade) {
        return false;
    }
    // Leaving book I should not wait for a faction engine or ranged unit
    // that may only appear in a higher shop. Preserve one recruitment budget
    // while taking the first real upgrade window after the initial board.
    if (policy.shop_level == 1 && policy.deployed_population >= 0 && policy.deployed_population < 4) {
        // A two-unit board that already leaked must not spend its whole
        // next budget upgrading. Later faction hints cannot bypass this
        // stabilization check. A proven clean two-blocker opening may level
        // immediately; otherwise retain a normal recruitment budget.
        return policy.deployed_population >= 2 &&
               (funds >= upgrade_price + 2 || (policy.last_round_life_loss == 0 && policy.early_frontline_count >= 2));
    }
    if (policy.shop_level >= 2 && !policy.primary_pact.empty() && policy.primary_pact != "auto") {
        std::set<std::string> owned_primary;
        bool owned_engine = false;
        bool harmony = false;
        for (const auto& [name, unit] : policy.owned) {
            if (unit.copies <= 0 && !unit.elite) {
                continue;
            }
            const auto traits = pacts.find(name);
            if (traits == pacts.end()) {
                continue;
            }
            harmony = harmony ||
                      (unit.deployed && contains(traits->second, "调和") && !contains(policy.disabled_pacts, "调和"));
            if (contains(traits->second, policy.primary_pact)) {
                owned_primary.insert(name);
                owned_engine = owned_engine || is_primary_growth_engine(name, policy.primary_pact);
            }
        }
        const int needed = 3 - static_cast<int>(owned_primary.size()) - (harmony ? 1 : 0);
        std::map<std::string, int> unique_offers;
        for (const auto& offer : offers) {
            const auto traits = pacts.find(offer.name);
            if (offer.price < 0 || offer.price > funds || owned_primary.contains(offer.name) || traits == pacts.end() ||
                !contains(traits->second, policy.primary_pact)) {
                continue;
            }
            const auto [item, inserted] = unique_offers.emplace(offer.name, offer.price);
            if (!inserted) {
                item->second = std::min(item->second, offer.price);
            }
        }
        if (needed > 0 && needed <= 3 && unique_offers.size() <= 6) {
            const std::vector<std::pair<std::string, int>> options(unique_offers.begin(), unique_offers.end());
            for (unsigned mask = 1; mask < (1U << options.size()); ++mask) {
                int count = 0, price = 0;
                bool engine = owned_engine;
                for (size_t index = 0; index < options.size(); ++index) {
                    if ((mask & (1U << index)) != 0) {
                        ++count;
                        price += options[index].second;
                        engine = engine || is_primary_growth_engine(options[index].first, policy.primary_pact);
                    }
                }
                // A real affordable activation+engine package is actionable
                // now. Do not spend its recruitment budget on a higher book
                // merely because the preceding wave happened to be clean.
                // Missing cards never delay the next useful book window.
                if (count == needed && price <= funds && engine && count <= 10 - policy.bench_occupied) {
                    return false;
                }
            }
        }
    }
    // Once both lanes have a basic combat roster, open the III-IV growth
    // window before chasing extra low-tier support-pact members.
    if (policy.early_board_ready && policy.last_round_life_loss <= 5 && policy.shop_level < 4) {
        return true;
    }
    if (policy.primary_pact.empty() || policy.primary_pact == "auto") {
        return false;
    }
    // An affordable card from the selected faction is actionable now. Keep
    // the round's funds for it instead of upgrading blindly; role and tier
    // scoring is handled by the route selector and the next shop observation.
    for (const auto& offer : offers) {
        if (offer.price < 0 || offer.price > funds) {
            continue;
        }
        const auto found = pacts.find(offer.name);
        if (found != pacts.end() && contains(found->second, policy.primary_pact)) {
            if (policy.deployed_population == 8) {
                const auto improvement = policy.full_field_offer_improvements.find(offer.name);
                // An offered faction label alone must not veto advancement
                // when this exact full field has no executable improvement.
                // Completing a promotion was already protected above.
                if (improvement != policy.full_field_offer_improvements.end() && improvement->second <= 0) {
                    continue;
                }
            }
            const auto owned = policy.owned.find(offer.name);
            if (owned != policy.owned.end() &&
                (owned->second.elite || owned->second.copies >= promotion_copies_required(offer.name))) {
                continue;
            }
            return false;
        }
    }
    return true;
}

bool asst::spa::promotion_reward_starts_chain(const PreparationPolicy& policy, const std::string& reward)
{
    const auto owned = policy.owned.find(reward);
    if (owned == policy.owned.end()) {
        return false;
    }
    const auto counts = owned_form_counts(owned->second);
    if (!counts || counts->initial <= 0) {
        return false;
    }
    const auto override = policy.promotion_copies.find(reward);
    const int required =
        override == policy.promotion_copies.end() ? promotion_copies_required(reward) : override->second;
    return counts->initial + 1 == required;
}

bool asst::spa::promotion_reward_chain_observed(
    bool expected,
    bool selection_confirmed,
    int free_slots,
    const std::vector<std::string>& previous_choices,
    const std::vector<std::string>& current_choices)
{
    const std::set<std::string> before(previous_choices.begin(), previous_choices.end());
    const std::set<std::string> after(current_choices.begin(), current_choices.end());
    return expected && selection_confirmed && free_slots >= 2 && before.size() >= 2 && after.size() >= 2 &&
           before != after;
}

std::vector<asst::spa::PromotionRewardRank> asst::spa::rank_promotion_rewards(
    const PreparationPolicy& policy,
    const std::vector<std::string>& choices,
    const std::map<std::string, std::vector<std::string>>& pacts)
{
    std::vector<PromotionRewardRank> ranking;
    std::set<std::string> seen;
    const auto rules = pact_requirements();
    for (const auto& name : choices) {
        const auto traits = pacts.find(name);
        if (traits == pacts.end() || !seen.insert(name).second) {
            continue;
        }
        int score = 0;
        if (policy.dynamic_decision_mode) {
            const auto owned = policy.owned.find(name);
            const auto counts =
                owned == policy.owned.end() ? std::optional(OperatorFormCounts {}) : owned_form_counts(owned->second);
            if (!counts) {
                continue;
            }
            const int copies = counts->initial;
            const bool elite = counts->elite > 0;
            const auto threshold = policy.promotion_copies.find(name);
            const int required =
                threshold == policy.promotion_copies.end() ? promotion_copies_required(name) : threshold->second;
            const bool new_member = copies == 0 && !elite;
            const bool primary =
                !contains(policy.disabled_pacts, policy.primary_pact) && contains(traits->second, policy.primary_pact);
            const bool output =
                contains(policy.carries, name) || is_primary_output_candidate(name, policy.primary_pact);
            if (primary && new_member) {
                score += 900;
                score += output ? 2200 : 0;
                score += is_primary_growth_engine(name, policy.primary_pact) ? 1400 : 0;
            }
            score += primary_growth_support_value(name, policy, pacts);
            if (copies > 0 && copies < required) {
                score += copies + 1 == required ? 1800 : 400;
            }
            if (new_member) {
                const auto combat = policy.early_board_scores.find(name);
                if (combat != policy.early_board_scores.end()) {
                    score += combat->second;
                }
                // Count distinct eligible members, including bench-only
                // pacts. A merely shared label is not an activation bonus.
                for (const auto& rule : rules) {
                    if (rule.core || rule.members < 2 || contains(policy.disabled_pacts, rule.name) ||
                        !contains(traits->second, rule.name)) {
                        continue;
                    }
                    int members = 0;
                    for (const auto& [member_name, member] : policy.owned) {
                        const auto member_traits = pacts.find(member_name);
                        if ((member.copies > 0 || member.elite) && (member.deployed || rule.bench_counts) &&
                            member_traits != pacts.end() && contains(member_traits->second, rule.name)) {
                            ++members;
                        }
                    }
                    score += members + 1 == rule.members ? 600 : 0;
                }
            }
            // Tier is only a tie-break between otherwise useful free cards.
            // It must not override an observed role gap or a growth engine.
            const auto fact = std::find_if(OperatorCatalogue.begin(), OperatorCatalogue.end(), [&](const auto& item) {
                return item.name == name;
            });
            score += fact != OperatorCatalogue.end() ? fact->tier * 20 : 0;
            score -= elite ? 800 : 0;
        }
        ranking.push_back({ name, score });
    }
    std::stable_sort(ranking.begin(), ranking.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.score > rhs.score;
    });
    return ranking;
}

namespace asst::spa
{
struct ScoredShopPurchase
{
    const ShopOffer* offer;
    int score;
    bool support = false;
    int growth_score = 0;
};

static bool can_recruit_after_paid_refresh(
    const PreparationPolicy& policy,
    int funds,
    const std::map<std::string, std::vector<std::string>>& pacts)
{
    const int after_refresh = funds - 1;
    if (after_refresh < policy.minimum_recruit_price) {
        return false;
    }
    const bool planning_growth =
        preparation_growth_weight(policy) > 0 && !policy.primary_pact.empty() && policy.primary_pact != "auto";
    if (!policy.dynamic_decision_mode || (after_refresh != 1 && !planning_growth)) {
        return true;
    }
    // The visible book and post-refresh budget must reach a usable card on
    // this route. A future book's stacker or carry gets no credit here.
    return std::any_of(OperatorCatalogue.begin(), OperatorCatalogue.end(), [&](const auto& card) {
        if (card.price <= 0 || card.price > after_refresh || card.tier > policy.shop_level) {
            return false;
        }
        const std::string name(card.name);
        const auto owned = policy.owned.find(name);
        return (owned == policy.owned.end() || !owned->second.elite) && belongs_to_planned_lineup(name, policy, pacts);
    });
}

static int additional_primary_layers_from_refresh(const PreparationPolicy& policy)
{
    if (!policy.growth_primary_active_verified || !policy.ordinary_refreshes_this_round ||
        *policy.ordinary_refreshes_this_round < 0 || contains(policy.disabled_pacts, policy.primary_pact)) {
        return 0;
    }
    int layers = 0;
    const int preceding_refreshes = *policy.ordinary_refreshes_this_round;
    const auto& growth = growth_profiles();
    for (const auto& [name, owned] : policy.owned) {
        const auto found = growth.find(name);
        if (found == growth.end() || owned.copies != 1 || !owned_form_counts(owned)) {
            continue;
        }
        const auto& profile = found->second;
        if (profile.kind != GrowthKind::CappedConditional || (!owned.deployed && !profile.works_on_bench) ||
            !contains(profile.pacts, policy.primary_pact)) {
            continue;
        }
        const auto override = profile.pact_formulas.find(policy.primary_pact);
        const auto formula =
            override == profile.pact_formulas.end()
                ? GrowthFormula { profile.normal_gain, profile.elite_gain, profile.normal_cap, profile.elite_cap }
                : override->second;
        const int gain = owned.elite ? formula.elite_gain : formula.normal_gain;
        const int cap = owned.elite ? formula.elite_cap : formula.normal_cap;
        if (profile.phase == GrowthPhase::ShopRefresh && preceding_refreshes == 0) {
            layers += std::min(gain, cap);
        }
        else if (profile.scales_with_round_refreshes && gain > 0 && cap > 0) {
            const int counted = std::clamp(preceding_refreshes, 0, cap / gain + 1);
            layers += std::min(cap, (counted + 1) * gain) - std::min(cap, counted * gain);
        }
    }
    return layers;
}

static std::optional<PreparationChoice> choose_open_field_recruit(
    const PreparationPolicy& policy,
    int funds,
    const std::vector<ShopOffer>& offers,
    const std::map<std::string, std::vector<std::string>>& pacts)
{
    const auto retention = round_balance_retention(policy.selected_strategy_id, funds);
    // Existing bench stock has first claim on the visible field vacancies.
    if (funds <= 0 || !policy.dynamic_decision_mode || policy.boss_preparation || !retention || retention->carries ||
        policy.deployed_population < 0 || policy.bench_occupied < 0 ||
        policy.deployed_population + policy.bench_occupied >= 8) {
        return std::nullopt;
    }

    const ShopOffer* best = nullptr;
    int best_score = 0;
    for (const auto& offer : offers) {
        if (offer.price < 0 || offer.price > funds || offer.right_slot < 0 || offer.right_slot > 5 ||
            !pacts.contains(offer.name) || policy.owned.contains(offer.name)) {
            continue;
        }
        const auto value = policy.early_board_scores.find(offer.name);
        if (value == policy.early_board_scores.end() || value->second <= UnclassifiedEarlyBoardScore) {
            continue;
        }
        const int score = value->second - offer.price * 20;
        if (score > best_score) {
            best = &offer;
            best_score = score;
        }
    }
    if (!best) {
        return std::nullopt;
    }
    return PreparationChoice { PreparationAction::Buy,
                               best->name,
                               best->price,
                               best->right_slot,
                               "Spend expiring funds on a distinct combat operator for a vacant field slot",
                               policy.primary_pact == "auto" ? std::string {} : policy.primary_pact };
}

static const ScoredShopPurchase& prefer_spending_last_coin(
    const ScoredShopPurchase& best,
    const std::vector<ScoredShopPurchase>& eligible,
    const PreparationPolicy& policy,
    int funds,
    int score_tolerance)
{
    const auto retention = round_balance_retention(policy.selected_strategy_id, funds);
    if (!policy.dynamic_decision_mode || !retention || retention->carries || funds - best.offer->price != 1) {
        return best;
    }
    // Only use already scored, purchasable cards. Do not invent a refund,
    // a future discount, or an extra physical slot to make the sums fit.
    const auto spends_balance = [&](const auto& candidate) {
        const int remaining = funds - candidate.offer->price;
        const bool can_buy_companion = remaining > 0 && policy.bench_occupied >= 0 && policy.bench_occupied <= 8 &&
                                       std::any_of(eligible.begin(), eligible.end(), [&](const auto& companion) {
                                           return companion.offer->name != candidate.offer->name &&
                                                  companion.offer->right_slot != candidate.offer->right_slot &&
                                                  companion.offer->price == remaining;
                                       });
        return remaining == 0 || can_buy_companion;
    };
    if (spends_balance(best)) {
        return best;
    }
    const auto alternative = std::find_if(eligible.begin(), eligible.end(), [&](const auto& candidate) {
        return candidate.score >= best.score - score_tolerance && spends_balance(candidate);
    });
    return alternative == eligible.end() ? best : *alternative;
}
}

bool asst::spa::is_one_coin_bank_offer(const ShopOffer& offer)
{
    return offer.price == 1 && (offer.name == "真理" || offer.name == "至简");
}

asst::spa::PreparationChoice asst::spa::choose_preparation(
    const PreparationPolicy& policy,
    int funds,
    const std::vector<ShopOffer>& offers,
    const std::map<std::string, std::vector<std::string>>& pacts)
{
    PreparationChoice result;
    result.reason = "No verified affordable primary-pact improvement";
    PreparationPolicy resolved = policy;

    // Capacity is a hard shop constraint.  Check it before dynamic pact
    // selection: when the live page says the ten-card bench is full, a failed
    // faction probe must never fall through to a refresh loop that only burns
    // the round budget.  The caller will still run deployment/reconciliation
    // and then proceed to battle with the current roster.
    if (policy.bench_occupied >= 10) {
        result.reason = "Bench capacity is full; deploy, swap or sell before recruiting";
        return result;
    }
    if (const auto package = affordable_same_shop_promotion(policy, funds, offers, pacts)) {
        return { PreparationAction::Buy,
                 package->first->name,
                 package->first->price,
                 package->first->right_slot,
                 "Start a main-pact promotion backed by the visible copies and current funds",
                 policy.primary_pact };
    }

    // Phase one is a lifecycle smoke path, not the dynamic faction planner.
    // It must spend available funds on the cheapest verified card so the
    // roster reaches the deployment loop quickly; waiting for a faction
    // signal and repeatedly refreshing here made a valid run look stuck in
    // the shop.  Phase two keeps the branch disabled and uses the scoring
    // logic below for pact density, stackers, carries and economy.
    if (policy.basic_mode && !policy.dynamic_decision_mode && policy.bench_occupied >= 0 &&
        policy.bench_occupied < 10) {
        const ShopOffer* cheapest = nullptr;
        for (const auto& offer : offers) {
            if (offer.right_slot < 0 || offer.right_slot > 5 || offer.price < 0 || offer.price > funds) {
                continue;
            }
            const auto owned = policy.owned.find(offer.name);
            const int copies = owned == policy.owned.end() ? 0 : owned->second.copies;
            const auto promotion = policy.promotion_copies.find(offer.name);
            const int required =
                promotion == policy.promotion_copies.end() ? promotion_copies_required(offer.name) : promotion->second;
            if (copies < 0 || copies >= required || (owned != policy.owned.end() && owned->second.elite)) {
                continue;
            }
            if (!cheapest || offer.price < cheapest->price ||
                (offer.price == cheapest->price && offer.right_slot < cheapest->right_slot)) {
                cheapest = &offer;
            }
        }
        if (cheapest) {
            return { PreparationAction::Buy,
                     cheapest->name,
                     cheapest->price,
                     cheapest->right_slot,
                     "Phase one: buy the cheapest verified affordable card",
                     {} };
        }
        // Phase one is deliberately deterministic: once no affordable card
        // is available, stop spending this preparation window. Do not fall
        // through into the dynamic refresh/freeze policy, which adds several
        // seconds of shop I/O while providing no additional lifecycle
        // coverage. The dynamic planner owns that decision in phase two.
        result.reason = "Phase one: no affordable card remains; proceed with the current roster";
        return result;
    }
    if (policy.dynamic_decision_mode && !policy.primary_locked_after_activation && policy.deployed_population >= 0 &&
        policy.deployed_population <= 8 && !policy.early_board_scores.empty()) {
        const ShopOffer* best_early = nullptr;
        int best_early_score = 0;
        std::vector<ScoredShopPurchase> early_purchases;
        const ShopOffer* full_field_promotion = nullptr;
        int full_field_promotion_score = 0;
        for (const auto& offer : offers) {
            if (offer.price < 0 || offer.price > funds || offer.right_slot < 0 || offer.right_slot > 5 ||
                !pacts.contains(offer.name)) {
                continue;
            }
            const auto owned = policy.owned.find(offer.name);
            const int copies = owned == policy.owned.end() ? 0 : owned->second.copies;
            const auto threshold = policy.promotion_copies.find(offer.name);
            const int required =
                threshold == policy.promotion_copies.end() ? promotion_copies_required(offer.name) : threshold->second;
            const bool completes_promotion = ordinary_purchase_completes_promotion(policy, offer.name);
            if (!completes_promotion && (copies >= required || (owned != policy.owned.end() && owned->second.elite))) {
                continue;
            }
            const auto value = policy.early_board_scores.find(offer.name);
            const int combat_value = value == policy.early_board_scores.end() ? 0 : value->second;
            if (policy.deployed_population == 8 && completes_promotion) {
                // Keep an affordable missing-role repair ahead of this
                // optional promotion, but do not discard the final copy
                // merely because every field slot is occupied.
                const int score = combat_value - offer.price * 20;
                if (!full_field_promotion || score > full_field_promotion_score) {
                    full_field_promotion = &offer;
                    full_field_promotion_score = score;
                }
                continue;
            }
            const auto exchange = policy.full_field_offer_improvements.find(offer.name);
            if (policy.deployed_population == 8 && !contains(pacts.at(offer.name), policy.primary_pact) &&
                exchange != policy.full_field_offer_improvements.end() && exchange->second <= 0) {
                continue;
            }
            // Full deployment is not proof of a balanced board. A missing
            // healer/ranged/blocking role can still be recruited for a
            // subsequent evaluated exchange, without buying more duplicates
            // merely because a role is absent from the current field.
            if (policy.deployed_population == 8 && (combat_value < 900 || copies > 0)) {
                continue;
            }
            // Four deployed guards do not provide ranged damage or healing.
            // Fill observed role gaps and take an offered final copy even
            // when it belongs to another faction; do not roll to force it.
            if (policy.deployed_population >= 4 && combat_value < 900 && !completes_promotion) {
                continue;
            }
            int score = combat_value - offer.price * 20;
            if (copies > 0) {
                score += copies + 1 >= required ? 1200 : 500;
            }
            early_purchases.push_back({ &offer, score });
            if (!best_early || score > best_early_score) {
                best_early = &offer;
                best_early_score = score;
            }
        }
        if (!best_early) {
            best_early = full_field_promotion;
        }
        if (best_early) {
            if (best_early != full_field_promotion) {
                const ScoredShopPurchase best_purchase { best_early, best_early_score };
                const auto& budgeted = prefer_spending_last_coin(best_purchase, early_purchases, policy, funds, 20);
                if (budgeted.offer != best_early) {
                    return { PreparationAction::Buy,
                             budgeted.offer->name,
                             budgeted.offer->price,
                             budgeted.offer->right_slot,
                             "Fill the same opening role with visible purchases that use the final coin",
                             {} };
                }
            }
            return { PreparationAction::Buy,
                     best_early->name,
                     best_early->price,
                     best_early->right_slot,
                     best_early == full_field_promotion
                         ? "Complete an offered promotion on the full field before refreshing"
                         : "Build the early combat board and take offered pairs before faction speculation",
                     {} };
        }
    }
    if (policy.primary_pact == "auto" || policy.dynamic_primary_pact) {
        const auto dynamic = choose_dynamic_primary_pact(policy, offers, pacts, funds);
        if (!dynamic.valid) {
            if (policy.allow_refresh && policy.free_refreshes > 0) {
                return { PreparationAction::Refresh,
                         "刷新",
                         0,
                         -1,
                         "Use a verified free refresh before spending the round budget",
                         {},
                         true };
            }
            if (const auto recruit = choose_open_field_recruit(policy, funds, offers, pacts)) {
                return *recruit;
            }
            if (policy.allow_refresh && can_recruit_after_paid_refresh(policy, funds, pacts)) {
                return { PreparationAction::Refresh,
                         "刷新",
                         1,
                         -1,
                         "No faction growth/output signal yet; refresh while retaining a recruitment budget",
                         {},
                         false };
            }
            result.reason = dynamic.reason;
            return result;
        }
        resolved.primary_pact = dynamic.primary_pact;
        result.selected_pact = dynamic.primary_pact;
    }
    if (resolved.primary_pact.empty() || funds < 0 || resolved.bench_occupied < 0 || resolved.bench_occupied >= 10 ||
        resolved.minimum_recruit_price < 1) {
        result.reason = "Preparation policy or inventory observation is incomplete";
        return result;
    }
    result.selected_pact = resolved.primary_pact;
    resolved.support_pacts = planning_support_pacts(resolved, offers, pacts);
    int primary_owned = 0;
    for (const auto& [name, owned] : resolved.owned) {
        const auto found = pacts.find(name);
        if ((owned.copies > 0 || owned.elite) && found != pacts.end() &&
            std::find(found->second.begin(), found->second.end(), resolved.primary_pact) != found->second.end()) {
            ++primary_owned;
        }
    }
    // The native controller already tries deployment/replacement before
    // shopping. A benched faction member may be temporarily unavailable;
    // that cannot veto other executable purchases or refreshes this round.
    const ShopOffer* best = nullptr;
    int best_score = 0;
    bool best_is_support = false;
    int best_growth_score = 0;
    std::vector<ScoredShopPurchase> eligible_purchases;

    std::vector<std::string> current_field;
    for (const auto& [name, unit] : resolved.owned) {
        if (unit.deployed && (unit.copies > 0 || unit.elite)) {
            current_field.push_back(name);
        }
    }
    const auto current_roles = assess_primary_roles(resolved.primary_pact, current_field, pacts);
    const bool terminal_owned = std::any_of(resolved.owned.begin(), resolved.owned.end(), [&](const auto& entry) {
        return (entry.second.copies > 0 || entry.second.elite) &&
               is_terminal_output_candidate(entry.first, resolved.primary_pact);
    });
    const bool has_iberia_sale_engine =
        resolved.primary_pact == "伊比利亚" &&
        std::any_of(resolved.owned.begin(), resolved.owned.end(), [](const auto& entry) {
            return (entry.second.copies > 0 || entry.second.elite) && (entry.first == "旅骨" || entry.first == "棘刺");
        });
    const auto requirements = pact_requirements();
    const auto primary_requirement = std::find_if(requirements.begin(), requirements.end(), [&](const auto& rule) {
        return rule.name == resolved.primary_pact;
    });
    const int growth_weight = preparation_growth_weight(resolved);
    std::set<int> slots;
    const ShopOffer* best_unaffordable = nullptr;
    int best_unaffordable_score = 0;
    for (const auto& offer : offers) {
        if (offer.right_slot < 0 || offer.right_slot > 5 || !slots.insert(offer.right_slot).second || offer.price < 0) {
            return { PreparationAction::Review, {}, 0, -1, "Shop offer identity or slot is contradictory" };
        }
        const auto found = pacts.find(offer.name);
        if (found == pacts.end()) {
            continue;
        }
        const bool primary =
            std::find(found->second.begin(), found->second.end(), resolved.primary_pact) != found->second.end();
        const auto owned = resolved.owned.find(offer.name);
        const int copies = owned == resolved.owned.end() ? 0 : owned->second.copies;
        const auto promotion = resolved.promotion_copies.find(offer.name);
        const int required =
            promotion == resolved.promotion_copies.end() ? promotion_copies_required(offer.name) : promotion->second;
        const bool completes_promotion = ordinary_purchase_completes_promotion(resolved, offer.name);
        if (copies < 0 ||
            (!completes_promotion && (copies >= required || (owned != resolved.owned.end() && owned->second.elite)))) {
            continue;
        }
        if (offer.price > funds) {
            if (primary && !resolved.shop_frozen_this_round) {
                const bool carry = contains(resolved.carries, offer.name) ||
                                   is_primary_output_candidate(offer.name, resolved.primary_pact);
                const bool engine = is_primary_growth_engine(offer.name, resolved.primary_pact);
                const auto combat = resolved.early_board_scores.find(offer.name);
                const bool repairs_live_role = owned != resolved.owned.end() && owned->second.deployed &&
                                               resolved.last_round_life_loss > 0 &&
                                               combat != resolved.early_board_scores.end() && combat->second >= 900;
                const bool useful_promotion = completes_promotion && (carry || engine || repairs_live_role);
                const auto primary_rule = std::find_if(requirements.begin(), requirements.end(), [&](const auto& rule) {
                    return rule.name == resolved.primary_pact &&
                           (rule.core || route_profiles().contains(resolved.primary_pact));
                });
                const bool completes_owned_formation =
                    primary_rule != requirements.end() && copies == 0 && primary_owned + 1 >= primary_rule->members;
                // A named engine in a speculative shop is not a commitment.
                // Preserve a missing role only for an established faction or
                // an activation package backed by distinct owned members.
                // Actual useful synthesis remains eligible independently.
                if (!resolved.primary_locked_after_activation && !completes_owned_formation && !useful_promotion) {
                    continue;
                }
                std::vector<std::string> deployed;
                for (const auto& [name, unit] : resolved.owned) {
                    if (unit.deployed && (unit.copies > 0 || unit.elite)) {
                        deployed.push_back(name);
                    }
                }
                const auto roles = assess_primary_roles(resolved.primary_pact, deployed, pacts);
                const bool missing_output =
                    roles.output_owned == 0 ||
                    (!terminal_owned && is_terminal_output_candidate(offer.name, resolved.primary_pact));
                const bool missing_engine = roles.growth_engine_owned == 0;
                if (missing_output && !missing_engine && !carry) {
                    continue;
                }
                // A low-value faction filler does not justify preserving
                // all shop slots while the actual carry is still missing.
                if (!useful_promotion && !(carry && missing_output) && !(engine && missing_engine)) {
                    continue;
                }
                const int score = (carry && missing_output ? 2000 : 0) + (engine && missing_engine ? 1500 : 0) +
                                  (useful_promotion ? 1200 : 0) + (offer.price <= funds + 1 ? 200 : 0);
                if (!best_unaffordable || score > best_unaffordable_score ||
                    (score == best_unaffordable_score && offer.price < best_unaffordable->price)) {
                    best_unaffordable = &offer;
                    best_unaffordable_score = score;
                }
            }
            continue;
        }
        // Buying the final copy starts the promotion reconciler. Forbidding
        // the third copy here prevented dynamic mode from ever upgrading a pair.
        bool completes_support = false;
        if (!primary && copies == 0) {
            for (const auto& requirement : requirements) {
                if (requirement.core || contains(resolved.disabled_pacts, requirement.name) ||
                    (requirement.members < 2 && requirement.name != "调和") ||
                    std::find(resolved.support_pacts.begin(), resolved.support_pacts.end(), requirement.name) ==
                        resolved.support_pacts.end() ||
                    std::find(found->second.begin(), found->second.end(), requirement.name) == found->second.end()) {
                    continue;
                }
                int members = 0;
                for (const auto& [name, unit] : resolved.owned) {
                    const auto traits = pacts.find(name);
                    if ((unit.copies > 0 || unit.elite) && (unit.deployed || requirement.bench_counts) &&
                        traits != pacts.end() &&
                        std::find(traits->second.begin(), traits->second.end(), requirement.name) !=
                            traits->second.end()) {
                        ++members;
                    }
                }
                const bool harmony_useful = requirement.name != "调和" || primary_owned >= 2;
                completes_support = completes_support || (harmony_useful && members + 1 == requirement.members);
            }
        }
        const int growth_support = primary_growth_support_value(offer.name, resolved, pacts);
        const auto exchange = resolved.full_field_offer_improvements.find(offer.name);
        const bool adds_first_healer = resolved.primary_locked_after_activation && resolved.deployed_population == 8 &&
                                       copies == 0 && resolved.first_healer_replacement_offers.contains(offer.name) &&
                                       exchange != resolved.full_field_offer_improvements.end() && exchange->second > 0;
        if (!primary && !completes_support && growth_support == 0 && !adds_first_healer) {
            continue;
        }
        if (!primary && resolved.deployed_population == 8 && copies + 1 < required &&
            exchange != resolved.full_field_offer_improvements.end() && exchange->second <= 0) {
            continue;
        }
        const bool carry =
            contains(resolved.carries, offer.name) || is_primary_output_candidate(offer.name, resolved.primary_pact);
        const bool engine = is_primary_growth_engine(offer.name, resolved.primary_pact);
        const bool missing_terminal =
            !terminal_owned && is_terminal_output_candidate(offer.name, resolved.primary_pact);
        const int new_member_priority = primary_owned < 3 ? 2000 : 300;
        const auto route = route_profiles().find(resolved.primary_pact);
        bool preferred_support = false;
        if (!primary && route != route_profiles().end()) {
            for (const auto& pact : found->second) {
                if (std::find(resolved.support_pacts.begin(), resolved.support_pacts.end(), pact) !=
                        resolved.support_pacts.end() &&
                    std::find(
                        route->second.preferred_support_pacts.begin(),
                        route->second.preferred_support_pacts.end(),
                        pact) != route->second.preferred_support_pacts.end()) {
                    preferred_support = true;
                    break;
                }
            }
        }
        // basic_mode also selects the guarded executor in dynamic mode. It
        // must not silently replace phase-two role scoring with cheap-card
        // selection; the actual phase-one policy already returned above.
        const int promotion_bonus = completes_promotion ? 1400 : 0;
        const int acquisition_layers = primary ? known_primary_acquisition_layers(resolved, offer.name) : 0;
        const int first_growth_recruit_bonus =
            first_fitting_growth_purchase(resolved, offer.name, acquisition_layers) ? 450 : 0;
        const bool enables_sale_growth =
            has_iberia_sale_engine && copies == 0 && has_iberia_sale_price_trait(offer.name);
        const auto fit = resolved.full_field_offer_improvements.find(offer.name);
        const bool fieldable = (resolved.deployed_population >= 0 && resolved.deployed_population < 8) ||
                               (fit != resolved.full_field_offer_improvements.end() && fit->second > 0) ||
                               (owned != resolved.owned.end() && owned->second.deployed && completes_promotion);
        const auto profile = growth_profiles().find(offer.name);
        const bool can_contribute = fieldable || (profile != growth_profiles().end() && profile->second.works_on_bench);
        const bool completes_freeze_pair = fieldable && kjerag_recruit_completes_freeze_pair(resolved, offer.name);
        // Positional or enemy-event recipients still matter, but their
        // application is less certain than a self-contained layer source.
        const int engine_relevance = engine && can_contribute && (copies == 0 || completes_promotion)
                                         ? (profile->second.dynamic_recipients ? 60 : 100)
                                         : 0;
        const bool advances_activation = primary && copies == 0 && fieldable &&
                                         primary_requirement != requirements.end() &&
                                         primary_owned < primary_requirement->members;
        const int growth_relevance = std::max(
            { engine_relevance,
              advances_activation ? 85 : 0,
              can_contribute && copies == 0 && (growth_support > 0 || completes_support || enables_sale_growth) ? 80
                                                                                                                : 0,
              first_growth_recruit_bonus > 0 ? 70 : 0 });
        const int growth_score = growth_weight * growth_relevance / 100;
        const int score =
            growth_score + promotion_bonus + std::min(acquisition_layers * 100, 1000) + first_growth_recruit_bonus +
            (enables_sale_growth ? 1250 : 0) + (completes_freeze_pair ? 1800 : 0) +
            (primary
                 ? (carry ? (copies == 0 && (current_roles.output_owned == 0 || missing_terminal) ? 4000 : 500) : 0) +
                       (engine ? (copies == 0 && current_roles.growth_engine_owned == 0 ? 3000 : 1000) : 0) +
                       (copies == 0 ? new_member_priority : 20)
                 : std::max({ growth_support, 1500 * completes_support, adds_first_healer ? exchange->second : 0 }) +
                       (preferred_support ? 250 : 0));
        eligible_purchases.push_back({ &offer, score, !primary, growth_score });
        if (!best || score > best_score || (score == best_score && offer.price < best->price)) {
            best = &offer;
            best_score = score;
            best_is_support = !primary;
            best_growth_score = growth_score;
        }
    }
    if (best) {
        const ScoredShopPurchase best_purchase { best, best_score, best_is_support, best_growth_score };
        const auto& budgeted = prefer_spending_last_coin(best_purchase, eligible_purchases, resolved, funds, 0);
        return { PreparationAction::Buy,
                 budgeted.offer->name,
                 budgeted.offer->price,
                 budgeted.offer->right_slot,
                 budgeted.offer != best      ? "Use the final ordinary coin on equally ranked, visible purchases"
                 : budgeted.growth_score > 0 ? "Fund actionable pact growth in the remaining preparation window"
                 : resolved.first_healer_replacement_offers.contains(budgeted.offer->name)
                     ? "Recruit the first healer for a screened position covering a route blocker"
                 : budgeted.support
                     ? "Recruit a useful growth helper or a distinct configured support-pact member"
                     : "Prioritize primary-pact activation, a verified growth formula and the configured carry",
                 resolved.primary_pact,
                 false,
                 false,
                 false,
                 budgeted.growth_score };
    }
    if (best_unaffordable && resolved.allow_freeze && !resolved.shop_frozen_this_round &&
        std::none_of(offers.begin(), offers.end(), [&](const auto& offer) {
            return offer.right_slot >= 0 && offer.right_slot <= 5 && offer.price >= 0 && offer.price <= funds;
        })) {
        int useful_other_slots = 0, unused_slots = 0;
        for (const auto& offer : offers) {
            if (&offer == best_unaffordable) {
                continue;
            }
            const auto owned = resolved.owned.find(offer.name);
            const bool usable = (owned == resolved.owned.end() || !owned->second.elite) &&
                                belongs_to_planned_lineup(offer.name, resolved, pacts);
            useful_other_slots += usable ? 1 : 0;
            unused_slots += usable ? 0 : 1;
        }
        // Heuristic opportunity cost, not an estimated draw probability or
        // damage forecast. Preserving one filler pair loses the entire
        // natural refresh; a missing terminal can justify that same cost.
        const int retention_value = best_unaffordable_score + useful_other_slots * 250;
        const int refresh_opportunity_cost =
            400 + unused_slots * 450 + (can_recruit_after_paid_refresh(resolved, funds, pacts) ? 600 : 0);
        if (retention_value > refresh_opportunity_cost) {
            return { PreparationAction::Freeze,
                     "冻结",
                     0,
                     -1,
                     "Preserve " + best_unaffordable->name + "; retention=" + std::to_string(retention_value) +
                         ", natural-refresh cost=" + std::to_string(refresh_opportunity_cost) +
                         ", unused slots=" + std::to_string(unused_slots),
                     resolved.primary_pact,
                     false };
        }
        result.reason = "Do not freeze " + best_unaffordable->name + "; retention=" + std::to_string(retention_value) +
                        ", natural-refresh cost=" + std::to_string(refresh_opportunity_cost) +
                        ", unused slots=" + std::to_string(unused_slots);
    }
    // Preserve enough of the current round's budget to recruit after the paid refresh.
    if (resolved.allow_refresh && resolved.free_refreshes > 0 && !resolved.shop_frozen_this_round) {
        return {
            PreparationAction::Refresh, "刷新", 0, -1, "Use a verified free refresh before spending the round budget",
            resolved.primary_pact,      true
        };
    }
    if (const auto recruit = choose_open_field_recruit(resolved, funds, offers, pacts)) {
        return *recruit;
    }
    const auto retention = round_balance_retention(resolved.selected_strategy_id, funds);
    const bool reserve_for_next_round =
        retention && retention->carries && resolved.dynamic_decision_mode && resolved.round > 0 &&
        resolved.economy_scenario_horizon_round > resolved.round && resolved.growth_primary_active_verified &&
        resolved.deployed_population >= 4 && resolved.last_round_life_loss == 0 && funds <= 5;
    if (reserve_for_next_round) {
        result.reason = round_balance_review_reason(resolved.selected_strategy_id, funds);
        return result;
    }
    if (resolved.allow_refresh && !resolved.shop_frozen_this_round &&
        can_recruit_after_paid_refresh(resolved, funds, pacts)) {
        return { PreparationAction::Refresh, "刷新", 1, -1, "Refresh while retaining a recruitment budget",
                 resolved.primary_pact,      false };
    }
    if (funds == 1 && resolved.dynamic_decision_mode && resolved.allow_refresh && !resolved.shop_frozen_this_round &&
        retention && !retention->carries) {
        if (const int layers = additional_primary_layers_from_refresh(resolved); layers > 0) {
            return { .action = PreparationAction::Refresh,
                     .name = "刷新",
                     .price = 1,
                     .reason = "Use the expiring coin for a confirmed refresh growth source below its cap",
                     .selected_pact = resolved.primary_pact,
                     .expected_refresh_layers = layers };
        }
    }
    // Ordinary cash expires, but a single verified one-price body can be held
    // until a later preparation needs its actual sale quote. Never buy and
    // sell it in this same preparation: that would restore expiring cash.
    // Keep this fallback behind useful cards, shop retention and free refreshes.
    if (funds == 1 && resolved.dynamic_decision_mode && resolved.one_coin_bank_allowed &&
        !resolved.tactical_elite_purchase_pending && !resolved.primary_pact.empty() &&
        resolved.deployed_population == 8 && resolved.bench_occupied >= 0 && resolved.bench_occupied <= 8 &&
        resolved.free_refreshes == 0 && resolved.round > 0 &&
        resolved.economy_scenario_horizon_round > resolved.round &&
        !contains(resolved.disabled_pacts, resolved.primary_pact) && resolved.selected_strategy_id != "band_cannot" &&
        resolved.selected_strategy_id != "band_substitute" && resolved.selected_strategy_id != "band_random" &&
        std::any_of(
            StrategyCatalogue.begin(),
            StrategyCatalogue.end(),
            [&](const auto& strategy) { return strategy.id == resolved.selected_strategy_id; }) &&
        // A newly held bank must not become the same shop's funding-sale
        // target. Even an unaffordable card below the freeze threshold can
        // cause that loop, converting the bank straight back to expiring cash.
        std::none_of(offers.begin(), offers.end(), [&](const auto& offer) {
            return belongs_to_planned_lineup(offer.name, resolved, pacts);
        })) {
        for (const auto& offer : offers) {
            // Current-season truth/simplification traits explicitly buy for 1;
            // the ordinary initial-body sale quote is 1. Do not extrapolate
            // discounts, changing sale prices or acquisition rewards here.
            if (!is_one_coin_bank_offer(offer) || resolved.owned.contains(offer.name) ||
                contains(resolved.carries, offer.name) || belongs_to_planned_lineup(offer.name, resolved, pacts)) {
                continue;
            }
            const auto facts = std::find_if(OperatorCatalogue.begin(), OperatorCatalogue.end(), [&](const auto& item) {
                return item.name == offer.name;
            });
            const auto fit = resolved.full_field_offer_improvements.find(offer.name);
            if (facts == OperatorCatalogue.end() || facts->price != 1 || facts->tier > resolved.shop_level ||
                facts->sale_has_side_effect || facts->trait_mentions_bench ||
                fit == resolved.full_field_offer_improvements.end() || fit->second != 0) {
                continue;
            }
            return { PreparationAction::Buy,
                     offer.name,
                     1,
                     offer.right_slot,
                     "Retain the last ordinary coin as one off-route initial body across the funds wipe; "
                     "a later sale must verify identity, quote and funds",
                     resolved.primary_pact,
                     false,
                     false,
                     true };
        }
    }
    return result;
}

bool asst::spa::bench_operator_can_deploy(const PreparationPolicy& policy, const std::string& name)
{
    const auto unit = policy.owned.find(name);
    if (unit == policy.owned.end() || unit->second.deployed || unit->second.copies <= 0) {
        return false;
    }
    const auto special = policy.promotion_copies.find(name);
    const int required = special == policy.promotion_copies.end() ? promotion_copies_required(name) : special->second;
    return unit->second.elite || unit->second.copies < required;
}

std::optional<std::string> asst::spa::choose_pending_deployment(
    const PreparationPolicy& policy,
    int remaining,
    const std::vector<std::string>& target_names)
{
    if (remaining <= 0 || remaining > 8) {
        return std::nullopt;
    }
    const std::string gift = "“暴躁铁皮”";
    if (policy.selected_strategy_id == "band_mech" && !policy.owned.contains(gift) &&
        std::find(target_names.begin(), target_names.end(), gift) != target_names.end()) {
        // This is a named discovery request. Its live identity and deployment
        // must be verified before adding any physical copy to the roster.
        return gift;
    }
    if (policy.bench_occupied <= 0 || policy.bench_occupied > 10) {
        return std::nullopt;
    }
    if (policy.basic_mode && std::find(target_names.begin(), target_names.end(), "*") != target_names.end()) {
        // Phase one deliberately permits identity-agnostic deployment after
        // the guarded bench scan. The live card is reconciled by SwapTask;
        // this fallback keeps an OCR fragment from blocking a full bench.
        return std::string("*");
    }
    const auto eligible = [&](const std::string& name) {
        return bench_operator_can_deploy(policy, name) &&
               std::find(target_names.begin(), target_names.end(), name) != target_names.end();
    };
    for (const auto& name : policy.carries) {
        if (eligible(name)) {
            return name;
        }
    }
    for (const auto& name : target_names) {
        if (eligible(name)) {
            return name;
        }
    }
    return std::nullopt;
}
