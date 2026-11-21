#include "SPAGrowth.h"
#include "SPAFormation.h"
#include <algorithm>
#include <set>

bool asst::spa::is_redeployment_growth_engine(
    const std::string& name,
    const std::vector<std::string>& active_pacts,
    bool elite)
{
    const auto found = growth_profiles().find(name);
    if (found == growth_profiles().end()) {
        return false;
    }
    const auto& profile = found->second;
    const int gain = elite ? profile.elite_gain : profile.normal_gain;
    const int cap = elite ? profile.elite_cap : profile.normal_cap;
    return profile.kind == GrowthKind::CappedConditional && profile.phase == GrowthPhase::BattleEvent && gain > 0 &&
           cap > gain && profile.normal_description.find("部署时") != std::string::npos &&
           std::any_of(profile.pacts.begin(), profile.pacts.end(), [&](const auto& pact) {
               return std::find(active_pacts.begin(), active_pacts.end(), pact) != active_pacts.end();
           });
}

bool asst::spa::is_front_growth_target(const std::string& source, const std::string& target, const std::string& primary)
{
    if (primary.empty()) {
        return false;
    }
    static const auto pacts = operator_pacts();
    const auto member = pacts.find(target);
    if (member == pacts.end() ||
        std::find(member->second.begin(), member->second.end(), primary) == member->second.end()) {
        return false;
    }
    if (source == "空弦" || source == "渡桥" || source == "帕拉斯" || source == "旅骨") {
        return true;
    }
    const auto growth = growth_profiles().find(target);
    if (growth == growth_profiles().end() || !is_primary_growth_engine(target, primary)) {
        return false;
    }
    const auto& profile = growth->second;
    if (source == "华法琳") {
        return profile.phase == GrowthPhase::BattleEvent && profile.kind == GrowthKind::CappedConditional &&
               profile.normal_description.find("开启技能") != std::string::npos;
    }
    if (source == "阿米娅") {
        return profile.phase == GrowthPhase::BattleEvent && profile.kind == GrowthKind::CappedConditional;
    }
    return source == "塞雷娅" && profile.phase == GrowthPhase::PreparationEnd &&
           (profile.kind == GrowthKind::FixedPhase || profile.kind == GrowthKind::PopulationPhase);
}

bool asst::spa::is_primary_growth_engine(const std::string& name, const std::string& pact)
{
    const auto& profiles = growth_profiles();
    const auto found = profiles.find(name);
    // Temporary injection and conversion operators are still valid growth
    // hands for route selection.  They may not contribute to the fixed-layer
    // projection in the same way as fixed/capped sources, but they are often
    // the first reliable signal that a faction can actually stack in the
    // current shop (for example 烈夏/折桠 for 乌萨斯).  Excluding them made a
    // live two-card stacker hand look like "no route" and incorrectly kept a
    // stale opening faction.
    return found != profiles.end() &&
           (found->second.kind == GrowthKind::FixedPhase || found->second.kind == GrowthKind::CappedConditional ||
            found->second.kind == GrowthKind::TemporaryInjection || found->second.kind == GrowthKind::Conversion ||
            found->second.kind == GrowthKind::SalePriceDependent ||
            found->second.kind == GrowthKind::PopulationPhase) &&
           std::find(found->second.pacts.begin(), found->second.pacts.end(), pact) != found->second.pacts.end();
}

asst::spa::UrsusLayerCycle asst::spa::assess_ursus_layer_cycle(
    const std::vector<GrowthSource>& sources,
    std::optional<int> distinct_ursus_members)
{
    UrsusLayerCycle cycle;
    // The second pact tier uses six different operators. 乌啾 counts bodies;
    // it can therefore gain capacity without changing the defeat reward.
    const bool advanced_pact = distinct_ursus_members && *distinct_ursus_members >= 6 && *distinct_ursus_members <= 8;
    cycle.minimum_temporary_layers_per_qualifying_defeat = advanced_pact ? 4 : 2;
    std::set<std::string> modeled;
    for (const auto& unit : sources) {
        if (!unit.deployed || !modeled.insert(unit.name).second) {
            continue;
        }
        const auto found = growth_profiles().find(unit.name);
        if (found == growth_profiles().end()) {
            continue;
        }
        const auto& profile = found->second;
        if (std::find(profile.pacts.begin(), profile.pacts.end(), "乌萨斯") == profile.pacts.end()) {
            continue;
        }
        if (unit.name == "塔露拉") {
            cycle.minimum_temporary_layers_per_qualifying_defeat = 4 + 2 * unit.elite + 2 * advanced_pact;
        }
        const int gain = unit.elite ? profile.elite_gain : profile.normal_gain;
        if (profile.kind == GrowthKind::TemporaryInjection && profile.phase == GrowthPhase::BattleStart) {
            cycle.opening_temporary_layers += gain;
        }
        if (profile.kind == GrowthKind::Conversion && profile.phase == GrowthPhase::PreparationStart) {
            if (unit.name != "乌啾") {
                cycle.conversion_capacity += gain;
            }
            else if (unit.eligible_population && *unit.eligible_population >= 1 && *unit.eligible_population <= 8) {
                cycle.conversion_capacity += gain * *unit.eligible_population;
            }
            else {
                cycle.conversion_population_unknown = true;
            }
        }
    }
    cycle.converted_from_opening = std::min(cycle.opening_temporary_layers, cycle.conversion_capacity);
    cycle.converted_with_one_qualifying_defeat = std::min(
        cycle.opening_temporary_layers + cycle.minimum_temporary_layers_per_qualifying_defeat,
        cycle.conversion_capacity);
    return cycle;
}

asst::spa::GrowthProjection asst::spa::project_primary_growth(
    const std::string& pact,
    int round,
    int boss_round,
    bool active,
    const std::vector<GrowthSource>& deployed,
    std::optional<int> distinct_ursus_members)
{
    GrowthProjection result;
    if (pact.empty() || round < 1 || boss_round < round || boss_round > 20 || deployed.size() > 20) {
        return result;
    }
    std::set<std::string> identities;
    for (const auto& unit : deployed) {
        if (unit.name.empty() || !identities.insert(unit.name).second ||
            (unit.eligible_population && (*unit.eligible_population < 0 || *unit.eligible_population > 64))) {
            return result;
        }
    }
    result.valid = true;
    result.preparation_ends = boss_round - round + 1;
    result.completed_battles_before_boss = boss_round - round;
    if (pact == "乌萨斯" && active) {
        result.ursus_cycle = assess_ursus_layer_cycle(deployed, distinct_ursus_members);
        result.ursus_converted_opening_layers_if_unchanged =
            result.ursus_cycle.converted_from_opening * result.completed_battles_before_boss;
    }
    for (const auto& unit : deployed) {
        const auto found = growth_profiles().find(unit.name);
        if (found == growth_profiles().end()) {
            continue;
        }
        const auto& profile = found->second;
        if (!unit.deployed && !profile.works_on_bench) {
            continue;
        }
        const bool relevant = std::find(profile.pacts.begin(), profile.pacts.end(), pact) != profile.pacts.end();
        if (!relevant) {
            // Dynamic recipients/core-pact effects may affect this pact but need spatial/stateful modeling.
            if (profile.kind == GrowthKind::SupportOnly || profile.kind == GrowthKind::Unmodeled ||
                profile.dynamic_recipients) {
                ++result.unmodeled_sources;
            }
            continue;
        }
        if (profile.dynamic_recipients) {
            ++result.unmodeled_sources;
        }
        const auto override = profile.pact_formulas.find(pact);
        const GrowthFormula formula =
            override == profile.pact_formulas.end()
                ? GrowthFormula { profile.normal_gain, profile.elite_gain, profile.normal_cap, profile.elite_cap }
                : override->second;
        const int gain = unit.elite ? formula.elite_gain : formula.normal_gain;
        const int cap = unit.elite ? formula.elite_cap : formula.normal_cap;
        const auto remaining_phase_triggers = [&](GrowthPhase phase) {
            // The current preparation-start trigger already happened before
            // this observation. Its next occurrence is next round, including
            // the boss preparation. Preparation-end is still ahead now.
            return phase == GrowthPhase::PreparationEnd || phase == GrowthPhase::ShopRefresh
                       ? result.preparation_ends
                       : result.completed_battles_before_boss;
        };
        if (profile.kind == GrowthKind::PopulationPhase) {
            result.engine_names.push_back(unit.name);
            if (!unit.eligible_population) {
                ++result.unmodeled_sources;
            }
            else if (active) {
                // This is the current verified population held unchanged for
                // a scenario, not a promise that future rows/gear stay fixed.
                result.population_phase_additions_if_unchanged +=
                    gain * *unit.eligible_population * remaining_phase_triggers(profile.phase);
            }
        }
        else if (profile.kind == GrowthKind::SalePriceDependent) {
            ++result.sale_price_sources;
            result.engine_names.push_back(unit.name);
            // Actual prices are stateful. Only the printed per-preparation
            // cap is a bounded scenario; neither the sale quote nor the
            // recipient's gain can be replaced with a constant one fund.
            if (active && profile.phase == GrowthPhase::PreparationEnd && cap > 0) {
                result.conditional_preparation_cap_additions += cap * result.preparation_ends;
            }
        }
        else if (profile.kind == GrowthKind::Conversion) {
            ++result.conversion_sources;
        }
        else if (profile.kind == GrowthKind::TemporaryInjection) {
            ++result.temporary_sources;
        }
        else if (profile.kind == GrowthKind::FixedPhase || profile.kind == GrowthKind::CappedConditional) {
            result.engine_names.push_back(unit.name);
            if (active && profile.kind == GrowthKind::FixedPhase) {
                std::set<GrowthPhase> phases(profile.additional_phases.begin(), profile.additional_phases.end());
                phases.insert(profile.phase);
                for (const auto phase : phases) {
                    if (phase != GrowthPhase::ShopRefresh) {
                        result.fixed_phase_additions += gain * remaining_phase_triggers(phase);
                    }
                }
            }
            if (active && profile.kind == GrowthKind::CappedConditional) {
                if (profile.phase == GrowthPhase::ShopRefresh || profile.phase == GrowthPhase::PreparationEnd ||
                    profile.phase == GrowthPhase::PreparationStart) {
                    result.conditional_preparation_cap_additions += cap * remaining_phase_triggers(profile.phase);
                }
                else {
                    result.conditional_cap_additions += cap * result.completed_battles_before_boss;
                }
            }
        }
        else {
            ++result.unmodeled_sources;
        }
    }
    return result;
}
