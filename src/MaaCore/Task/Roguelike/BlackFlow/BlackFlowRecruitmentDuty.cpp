#include "BlackFlowRecruitmentDuty.h"

#include <algorithm>
#include <cmath>

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"

namespace asst::blackflow::recruitment_duty_detail
{
DutyBound presence(const std::vector<std::array<DutyBound, 2>>& bodies, std::size_t task)
{
    DutyBound result { 0, 0 };
    for (const auto& body : bodies) {
        result.lower = std::max(result.lower, body[task].lower);
        result.upper = std::max(result.upper, body[task].upper);
    }
    return result;
}

DutyBound replacement_marginal(DutyBound other, DutyBound old, DutyBound next)
{
    DutyBound result { 1, -1 };
    // Keep the same other-roster value on both sides of the replacement.
    for (int o = other.lower; o <= other.upper; ++o) {
        for (int a = old.lower; a <= old.upper; ++a) {
            for (int b = next.lower; b <= next.upper; ++b) {
                const int difference = std::max(o, b) - std::max(o, a);
                result.lower = std::min(result.lower, difference);
                result.upper = std::max(result.upper, difference);
            }
        }
    }
    return result;
}
}

namespace asst::blackflow
{
namespace projected_output_detail
{
std::optional<double> number(const json::value& object, const char* key)
{
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_number()) {
        return std::nullopt;
    }
    const double value = object.at(key).as_double();
    return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}

struct AttributeVariants
{
    std::string range;
    std::vector<PrimaryComponentVariant> variants;
};

std::optional<AttributeVariants> attributes(
    const json::object& op,
    int elite,
    int level,
    const ProjectedOutputAttributes& evidence,
    std::string& reason)
{
    const auto reject = [&](const char* text) -> std::optional<AttributeVariants> {
        reason = text;
        return std::nullopt;
    };
    const auto phases = op.find<json::array>("phases"), ranges = op.find<json::array>("ranges"),
               trust = op.find<json::array>("trust_ATK"), potential = op.find<json::array>("potential_ATK"),
               own = op.find<json::array>("own_by_phase_potential");
    if (!phases || !ranges || !trust || !potential || !own || elite < 0 ||
        phases->size() <= static_cast<std::size_t>(elite) || ranges->size() <= static_cast<std::size_t>(elite) ||
        own->size() <= static_cast<std::size_t>(elite) || !phases->at(elite).is_array() || !own->at(elite).is_array() ||
        trust->size() != 2 || potential->size() != 6) {
        return reject("attribute_bounds_source_missing");
    }
    const auto& frames = phases->at(elite).as_array();
    if (frames.size() != 2 || own->at(elite).as_array().size() != 6) {
        return reject("invalid_source_keyframes");
    }
    const auto first = number(frames.at(0), "level"), last = number(frames.at(1), "level"),
               atk0 = number(frames.at(0), "atk"), atk1 = number(frames.at(1), "atk"),
               period0 = number(frames.at(0), "period"), period1 = number(frames.at(1), "period");
    if (!first || !last || !atk0 || !atk1 || !period0 || !period1 || level < *first || level > *last ||
        *last < *first) {
        return reject("typed_attribute_or_skill_inputs_invalid");
    }
    for (const auto& value : *trust) {
        if (!value.is_number() || !std::isfinite(value.as_double())) {
            return reject("invalid_trust_bounds");
        }
    }
    for (const auto& value : *potential) {
        if (!value.is_number() || !std::isfinite(value.as_double())) {
            return reject("invalid_potential_bounds");
        }
    }
    if (trust->at(0).as_double() > trust->at(1).as_double()) {
        return reject("reversed_source_bounds");
    }
    const double ratio = *last == *first ? 0 : (level - *first) / (*last - *first);
    const double bare = *atk0 + ratio * (*atk1 - *atk0), period = *period0 + ratio * (*period1 - *period0);
    double tl = trust->at(0).as_double(), th = trust->at(1).as_double();
    int pl = 0, ph = 5;
    if (!evidence.source.empty() && evidence.trust_percent) {
        if (*evidence.trust_percent < 0 || *evidence.trust_percent > 200) {
            return reject("invalid_trust_percentage");
        }
        if (*evidence.trust_percent == 0) {
            th = tl;
        }
        else if (*evidence.trust_percent >= 100) {
            tl = th;
        }
    }
    if (!evidence.source.empty() && evidence.potential) {
        if (*evidence.potential < 1 || *evidence.potential > 6) {
            return reject("invalid_potential_rank");
        }
        pl = ph = *evidence.potential - 1;
    }
    AttributeVariants result { .range = ranges->at(elite).as_string() };
    for (int p = pl; p <= ph; ++p) {
        const auto& self = own->at(elite).as_array().at(p);
        const auto attack = number(self, "atk_fraction"), speed = number(self, "aspd"),
                   bonus = number(self, "first_deploy_sp_upper");
        if (!attack || !speed || !bonus || *bonus < 0) {
            return reject("reviewed_own_talent_bounds_missing");
        }
        result.variants.push_back(
            PrimaryComponentVariant {
                .pre_talent_attack = { std::floor(bare + tl + potential->at(p).as_double()),
                                       std::ceil(bare + th + potential->at(p).as_double()) },
                .own_atk_fraction = *attack,
                .own_aspd = *speed,
                .base_period = period,
                .first_deploy_sp_upper = *bonus,
            });
    }
    return result;
}

}

bool projected_output_strictly_better(const ProjectedOutputEstimate& a, const ProjectedOutputEstimate& b)
{
    return a.qualified && b.qualified && a.objective == b.objective && !a.range.empty() && a.range == b.range &&
           primary_component_curves_strictly_better(
               a.lower,
               b.upper,
               a.objective != PrimaryProjectionObjective::OutsideIntrinsicReference);
}

json::value projected_output_to_json(const ProjectedOutputEstimate& x)
{
    json::array limitations;
    for (const auto& item : x.limitations) {
        limitations.emplace_back(item);
    }
    const auto profiles = [](const auto& values) {
        json::array rows;
        for (const auto& value : values) {
            rows.emplace_back(
                json::object {
                    { "base_primary_damage", value.base_damage },
                    { "base_period", value.base_period },
                    { "active_primary_damage", value.skill_damage },
                    { "active_period", value.skill_period },
                    { "active_hits", value.hits },
                    { "reported_intrinsic_SP_units", value.sp_debt },
                });
        }
        return rows;
    };
    return json::object { { "qualified", x.qualified },
                          { "objective",
                            x.objective == PrimaryProjectionObjective::OutsideIntrinsicReference
                                ? "outside_intrinsic_acquired_target_reference_not_current_run_dominance"
                                : "current_run_primary_components" },
                          { "limitations", std::move(limitations) },
                          { "marginal_known_positive", x.marginal_known_positive },
                          { "operator_id", x.id },
                          { "range_id", x.range },
                          { "reason", x.reason },
                          { "evidence_source", x.evidence_source },
                          { "effective_rank", x.effective_rank },
                          { "lower_profiles", x.lower.size() },
                          { "upper_profiles", x.upper.size() },
                          { "lower_component_facts", profiles(x.lower) },
                          { "upper_component_facts", profiles(x.upper) },
                          { "SP_units_used_for_ordering",
                            x.objective == PrimaryProjectionObjective::CurrentRunComponents },
                          { "visible_panel_ATK_used", false },
                          { "defense_domain", "all_nonnegative_via_physical_curve_breakpoints_not_observed_enemy_DEF" },
                          { "stun_state", "full_source_bounds_not_assumed_stunned" },
                          { "scope", "projected_acquired_target_primary_physical_components_not_total_team_strength" },
                          { "battle_DP", nullptr },
                          { "HPS", nullptr },
                          { "full_DPS", nullptr } };
}

ProjectedOutputEstimate BlackFlowRecruitmentDutyCatalog::projected_output(
    const std::string& name,
    int elite,
    int level,
    int slot,
    int rank,
    const ProjectedOutputAttributes& attributes,
    const RecruitmentOutputEvidence& modifiers) const
{
    return primary_components(name, elite, level, slot, rank, attributes, &modifiers);
}

ProjectedOutputEstimate BlackFlowRecruitmentDutyCatalog::intrinsic_primary(
    const std::string& name,
    int elite,
    int level,
    int slot,
    int rank,
    const ProjectedOutputAttributes& attributes,
    const std::string& source) const
{
    auto out = primary_components(name, elite, level, slot, rank, attributes, nullptr);
    out.evidence_source = source + ":" + attributes.source;
    if (source.empty()) {
        out.qualified = false;
        out.reason = "intrinsic_reference_context_source_unbound";
    }
    out.limitations = { "external_effects_excluded_by_reference_objective_not_assumed_zero",
                        "no_future_map_enemy_or_deadline_observation",
                        "primary_components_not_full_DPS_or_survival",
                        "equal_base_reference_footprint_cohort_only",
                        "intrinsic_SP_debt_reported_not_an_activation_deadline_or_component_ordering_constraint" };
    return out;
}

ProjectedOutputEstimate BlackFlowRecruitmentDutyCatalog::primary_components(
    const std::string& name,
    int elite,
    int level,
    int slot,
    int rank,
    const ProjectedOutputAttributes& attributes,
    const RecruitmentOutputEvidence* modifiers) const
{
    ProjectedOutputEstimate out;
    out.id = RoguelikeTraining.unique_identity(name);
    out.effective_rank = rank;
    out.evidence_source = modifiers ? modifiers->source : "outside_intrinsic_reference";
    out.objective = modifiers ? PrimaryProjectionObjective::CurrentRunComponents
                              : PrimaryProjectionObjective::OutsideIntrinsicReference;
    const auto reject = [&](const char* reason) {
        out.reason = reason;
        return out;
    };
    const auto maximum_rank = RoguelikeTraining.maximum_training(name, slot);
    if (!m_valid || out.id.empty() || !m_operators.contains(out.id) || elite < 0 || elite > 2 || rank < 1 ||
        !maximum_rank || rank > *maximum_rank ||
        rank > (elite == 0   ? 4
                : elite == 1 ? 7
                             : 10) ||
        RoguelikeTraining.skill_availability(name, slot, elite, level) != SkillAvailability::Available) {
        return reject("actual_identity_phase_level_or_rank_unproven");
    }
    std::optional<PrimaryExternalBounds> external;
    if (modifiers) {
        if (!modifiers->external_modifiers_bounded || modifiers->source.empty()) {
            return reject("external_modifiers_unbounded_not_zero");
        }
        external = PrimaryExternalBounds { .atk_fraction = modifiers->external_atk_fraction,
                                           .aspd = modifiers->external_aspd };
    }
    try {
        const auto op = m_operators.at(out.id).find<json::object>("projected_attributes");
        const auto row = skill(out.id, slot, rank);
        const auto model = row.find<json::object>("projected_primary");
        if (!op || !model || model->get("kind", "") != "physical_acquired_target" ||
            !model->contains("range_unchanged") || !model->at("range_unchanged").is<bool>() ||
            !model->at("range_unchanged").as_boolean()) {
            return reject("selected_default_or_alternate_skill_model_unreviewed");
        }
        const auto hits = projected_output_detail::number(*model, "hits"),
                   add = projected_output_detail::number(*model, "interval_add"),
                   sp = projected_output_detail::number(row, "sp_cost"),
                   initial = projected_output_detail::number(row, "initial_sp");
        const auto scales = model->find<json::array>("scale_bounds");
        if (!hits || !add || !sp || !initial || *hits <= 0 || *sp < 0 || *initial < 0 ||
            row.get("sp_type", "") != "INCREASE_WITH_TIME" || !scales || scales->size() != 2) {
            return reject("typed_attribute_or_skill_inputs_invalid");
        }
        for (const auto& v : *scales) {
            if (!v.is_number() || !std::isfinite(v.as_double()) || v.as_double() <= 0) {
                return reject("invalid_damage_scale");
            }
        }
        if (scales->at(0).as_double() > scales->at(1).as_double()) {
            return reject("reversed_source_bounds");
        }
        std::string attribute_reason;
        const auto parsed = projected_output_detail::attributes(*op, elite, level, attributes, attribute_reason);
        if (!parsed) {
            return reject(attribute_reason.c_str());
        }
        for (auto variant : parsed->variants) {
            variant.skill_interval_add = *add;
            variant.skill_scale = { scales->at(0).as_double(), scales->at(1).as_double() };
            variant.hits = *hits;
            variant.sp_cost = *sp;
            variant.initial_sp = *initial;
            const auto component = calculate_primary_component(variant, external);
            if (!component) {
                return reject("invalid_computed_attribute_or_modifier_bounds");
            }
            out.lower.push_back(component->lower);
            out.upper.push_back(component->upper);
        }
        out.range = parsed->range;
        out.qualified = !out.range.empty();
        out.reason = modifiers ? "bounded_trust_potential_own_talents_and_explicit_external_conditions_not_full_DPS"
                               : "outside_intrinsic_primary_components_estimate_not_current_run_dominance";
        return out;
    }
    catch (const json::exception&) {
        return reject("malformed_projected_source_unknown_not_abort");
    }
}

ConditionalPrimaryEstimate BlackFlowRecruitmentDutyCatalog::conditional_primary(
    const std::string& name,
    int elite,
    int level,
    int slot,
    int displayed_or_effective_rank,
    const ProjectedOutputAttributes& attributes,
    const PrimarySequenceConditions& conditions,
    const std::string& source) const
{
    ConditionalPrimaryEstimate out { .id = RoguelikeTraining.unique_identity(name), .source = source };
    const auto reject = [&](const std::string& reason) {
        out.qualified = false;
        out.reason = reason;
        return out;
    };
    const auto rank = normalize_displayed_skill_level_for_run(name, slot, elite, level, displayed_or_effective_rank);
    if (!m_valid || out.id.empty() || !m_operators.contains(out.id) || !rank || source.empty()) {
        return reject("conditional_identity_phase_slot_rank_or_source_unproven");
    }
    out.effective_rank = *rank;
    if (conditions.target != ConditionalPrimaryTarget::Ground) {
        return reject("target_class_not_proven_ground_reference");
    }
    if (conditions.source.empty() && (conditions.isolated_target || conditions.steal_succeeded ||
                                      conditions.stolen_aspd || conditions.remaining_ammunition)) {
        return reject("conditional_observation_provenance_missing");
    }
    const auto bounds = [](const json::value& value) -> std::optional<std::array<double, 2>> {
        if (!value.is_array() || value.as_array().size() != 2) {
            return std::nullopt;
        }
        const auto& values = value.as_array();
        if (!values.at(0).is_number() || !values.at(1).is_number()) {
            return std::nullopt;
        }
        const std::array<double, 2> result { values.at(0).as_double(), values.at(1).as_double() };
        return std::isfinite(result[0]) && std::isfinite(result[1]) && result[0] <= result[1] ? std::optional(result)
                                                                                              : std::nullopt;
    };
    try {
        const auto& raw = m_operators.at(out.id);
        const auto op = raw.find<json::object>("projected_attributes");
        const auto row = skill(out.id, slot, *rank);
        const auto model = row.find<json::object>("conditional_primary");
        if (!op || !model || model->get("kind", "") != "physical_conditional_modes" ||
            model->get("target_class", "") != "ground_reference" || !model->contains("requires_sustain_coverage") ||
            !model->at("requires_sustain_coverage").is<bool>() || !model->contains("range_unchanged") ||
            !model->at("range_unchanged").is<bool>() || !model->at("range_unchanged").as_boolean() ||
            model->get("source_skill_sha256", "") != m_public_skill_sha ||
            model->get("source_skill_id", "") != raw.at("slots").as_array().at(slot - 1).get("skill_id", "")) {
            return reject("selected_conditional_skill_model_or_source_unreviewed");
        }
        std::string reason;
        const auto parsed = projected_output_detail::attributes(*op, elite, level, attributes, reason);
        if (!parsed || parsed->range.empty()) {
            return reject(reason.empty() ? "attribute_range_unknown" : reason);
        }
        out.range = parsed->range;
        out.requires_sustain_coverage = model->at("requires_sustain_coverage").as_boolean();
        const auto attack = projected_output_detail::number(*model, "atk_fraction"),
                   interval = projected_output_detail::number(*model, "interval_add"),
                   sp = projected_output_detail::number(row, "sp_cost"),
                   initial = projected_output_detail::number(row, "initial_sp");
        const auto modes = model->find<json::array>("modes"),
                   main = model->find<json::array>("main_target_scale_by_phase"),
                   ammo_self = model->find<json::array>("ammo_self_atk_by_phase_potential"),
                   limitations = model->find<json::array>("limitations");
        if (!attack || !interval || !sp || !initial || *sp < 0 || *initial < 0 ||
            row.get("sp_type", "") != "INCREASE_WITH_TIME" || !modes || modes->empty() || !main ||
            main->size() <= static_cast<std::size_t>(elite) || !ammo_self ||
            ammo_self->size() <= static_cast<std::size_t>(elite) || !ammo_self->at(elite).is_array() ||
            ammo_self->at(elite).as_array().size() != 6 || !limitations) {
            return reject("conditional_skill_rows_or_phase_conditions_invalid");
        }
        const auto target_scale = bounds(main->at(elite));
        if (!target_scale || (*target_scale)[0] <= 0) {
            return reject("main_target_factor_bounds_invalid");
        }
        for (const auto& item : *limitations) {
            if (!item.is_string()) {
                return reject("conditional_limitations_not_typed");
            }
            out.limitations.push_back(item.as_string());
        }
        out.sp_debt = { std::max(0.0, *sp - *initial), std::max(0.0, *sp - *initial) };
        for (const auto& raw_mode : *modes) {
            if (!raw_mode.is_object()) {
                return reject("conditional_mode_not_object");
            }
            const auto scale = projected_output_detail::number(raw_mode, "scale"),
                       projectiles = projected_output_detail::number(raw_mode, "projectiles");
            const std::string key = raw_mode.get("key", ""), allocation = raw_mode.get("allocation", "");
            if (key.empty() || !scale || *scale <= 0 || !projectiles || *projectiles < 1 ||
                std::floor(*projectiles) != *projectiles || *projectiles > 64 ||
                (allocation != "acquired" && allocation != "random_in_range")) {
                return reject("conditional_hit_allocation_invalid");
            }
            PrimarySequenceMode mode;
            mode.interval_add = *interval;
            const int count = static_cast<int>(*projectiles);
            mode.hits.push_back(
                PhysicalHitScale {
                    .scale = { *scale * (*target_scale)[0], *scale * (*target_scale)[1] },
                    .count = { allocation == "random_in_range" && conditions.isolated_target != true ? 0 : count,
                               count },
                });
            if (raw_mode.contains("ammunition")) {
                const auto ammo = projected_output_detail::number(raw_mode, "ammunition");
                if (!ammo || *ammo < 1 || std::floor(*ammo) != *ammo || *ammo > 10'000) {
                    return reject("conditional_ammunition_invalid");
                }
                const int base = static_cast<int>(*ammo);
                mode.ammunition = std::array<int, 2> { base, base };
            }
            if (raw_mode.contains("steal_max_aspd")) {
                const auto speed = projected_output_detail::number(raw_mode, "steal_max_aspd"),
                           extra = projected_output_detail::number(raw_mode, "steal_bonus_ammo");
                if (!speed || *speed < 0 || !extra || *extra < 0 || std::floor(*extra) != *extra || *extra > 10'000 ||
                    !mode.ammunition ||
                    (conditions.stolen_aspd &&
                     (!conditions.steal_succeeded || !std::isfinite(*conditions.stolen_aspd) ||
                      *conditions.stolen_aspd < 0 || *conditions.stolen_aspd > *speed ||
                      (conditions.steal_succeeded == false && *conditions.stolen_aspd > 0)))) {
                    return reject("steal_observation_or_ammunition_condition_invalid");
                }
                mode.extra_aspd = conditions.steal_succeeded == false ? std::array<double, 2> { 0, 0 }
                                                                      : std::array<double, 2> { 0, *speed };
                if (conditions.stolen_aspd) {
                    mode.extra_aspd = { *conditions.stolen_aspd, *conditions.stolen_aspd };
                }
                if (conditions.steal_succeeded != false) {
                    (*mode.ammunition)[1] += static_cast<int>(*extra);
                }
                if (conditions.steal_succeeded == true) {
                    (*mode.ammunition)[0] = (*mode.ammunition)[1];
                }
            }
            else if (conditions.steal_succeeded || conditions.stolen_aspd) {
                return reject("steal_condition_supplied_to_nonstealing_mode");
            }
            if (conditions.remaining_ammunition) {
                if (!mode.ammunition || *conditions.remaining_ammunition < 0 ||
                    *conditions.remaining_ammunition > (*mode.ammunition)[1]) {
                    return reject("remaining_ammunition_outside_source_capacity");
                }
                if (*conditions.remaining_ammunition == 0) {
                    return reject("observed_ammunition_exhausted");
                }
                mode.ammunition =
                    std::array<int, 2> { *conditions.remaining_ammunition, *conditions.remaining_ammunition };
            }
            ConditionalPrimaryModeEstimate output { .key = key,
                                                    .ammunition_is_observed_remaining =
                                                        conditions.remaining_ammunition.has_value() };
            output.added_aspd = mode.extra_aspd;
            output.ally_opportunity_cost_unresolved =
                raw_mode.contains("steal_max_aspd") && conditions.steal_succeeded != false;
            if (raw_mode.contains("simultaneous_target_capacity")) {
                const auto capacity = projected_output_detail::number(raw_mode, "simultaneous_target_capacity");
                if (!capacity || *capacity < 1 || *capacity > 64 || std::floor(*capacity) != *capacity) {
                    return reject("simultaneous_target_capacity_invalid");
                }
                output.simultaneous_target_capacity = static_cast<int>(*capacity);
            }
            const int first_potential =
                !attributes.source.empty() && attributes.potential ? *attributes.potential - 1 : 0;
            for (std::size_t i = 0; i < parsed->variants.size(); ++i) {
                const auto conditional_attack = bounds(ammo_self->at(elite).as_array().at(first_potential + i));
                if (!conditional_attack) {
                    return reject("conditional_own_ammo_talent_bounds_invalid");
                }
                mode.extra_atk_fraction = { *attack + (*conditional_attack)[0], *attack + (*conditional_attack)[1] };
                auto variant = parsed->variants[i];
                variant.sp_cost = *sp;
                variant.initial_sp = *initial;
                const auto sequence = calculate_primary_sequence(variant, mode, std::nullopt);
                if (!sequence) {
                    return reject("conditional_primary_numeric_inputs_invalid");
                }
                out.sp_debt[0] =
                    std::min(out.sp_debt[0], std::max(0.0, *sp - *initial - variant.first_deploy_sp_upper));
                output.variants.push_back(*sequence);
            }
            out.modes.push_back(std::move(output));
        }
        out.qualified = !out.modes.empty();
        out.reason = "source_conditioned_intrinsic_hit_sequences_not_recruitment_winner_or_live_DPS";
        return out;
    }
    catch (const json::exception&) {
        return reject("malformed_conditional_source_unknown_not_abort");
    }
}

json::value conditional_primary_to_json(const ConditionalPrimaryEstimate& value)
{
    const auto sequence_json = [](const PhysicalHitSequence& sequence) {
        json::array hits;
        for (const auto& hit : sequence.hits) {
            hits.emplace_back(json::object { { "damage_per_hit", hit.damage }, { "count", hit.count } });
        }
        return json::object { { "hits", hits },
                              { "period", sequence.period },
                              { "ammunition",
                                sequence.ammunition ? json::value(*sequence.ammunition) : json::value(nullptr) } };
    };
    json::array modes;
    for (const auto& mode : value.modes) {
        json::array variants;
        for (const auto& variant : mode.variants) {
            variants.emplace_back(
                json::object { { "lower", sequence_json(variant.lower) }, { "upper", sequence_json(variant.upper) } });
        }
        modes.emplace_back(
            json::object { { "key", mode.key },
                           { "variants", variants },
                           { "simultaneous_target_capacity",
                             mode.simultaneous_target_capacity ? json::value(*mode.simultaneous_target_capacity)
                                                               : json::value(nullptr) },
                           { "added_aspd", mode.added_aspd },
                           { "ally_opportunity_cost_unresolved", mode.ally_opportunity_cost_unresolved },
                           { "ammunition_is_observed_remaining", mode.ammunition_is_observed_remaining } });
    }
    return json::object { { "qualified", value.qualified },
                          { "operator_id", value.id },
                          { "range", value.range },
                          { "effective_rank", value.effective_rank },
                          { "source", value.source },
                          { "reason", value.reason },
                          { "sp_debt_not_activation_seconds", value.sp_debt },
                          { "requires_sustain_coverage", value.requires_sustain_coverage },
                          { "survival_proven", false },
                          { "modes", modes },
                          { "limitations", value.limitations },
                          { "full_DPS", nullptr },
                          { "unknown_external_assumed_zero", false },
                          { "automatic_recruitment_choice", false } };
}

RecruitmentSkillPlan
    resolve_recruitment_skill_plan(int preferred, int alternate, const std::array<SkillAvailability, 3>& availability)
{
    RecruitmentSkillPlan result { .preferred = preferred, .alternate = alternate, .availability = availability };
    const auto available = [&](int slot) {
        return slot >= 1 && slot <= 3 && availability[slot - 1] == SkillAvailability::Available;
    };
    if (available(preferred)) {
        result.selected = preferred;
        result.reason = "configured_preferred_verified_available_at_offer_phase";
    }
    else if (available(alternate)) {
        result.selected = alternate;
        result.reason = "preferred_not_verified_available_use_verified_alternate";
    }
    else {
        result.reason = "no_configured_skill_verified_available_unknown_is_not_available";
    }
    return result;
}

void RecruitmentOriginLedger::clear()
{
    m_client.clear();
    m_uid.clear();
    m_run_id.clear();
    m_receipts.clear();
}

bool RecruitmentOriginLedger::bind(const std::string& client, const std::string& uid, const std::string& run_id)
{
    if (client.empty() || uid.empty() || run_id.empty()) {
        clear();
        return false;
    }
    if (client != m_client || uid != m_uid || run_id != m_run_id) {
        clear();
        m_client = client;
        m_uid = uid;
        m_run_id = run_id;
    }
    return true;
}

std::optional<RecruitmentOriginReceipt> RecruitmentOriginLedger::find(
    const std::string& client,
    const std::string& uid,
    const std::string& run_id,
    const std::string& id,
    const RoguelikeOper& before) const
{
    if (client != m_client || uid != m_uid || run_id.empty() || run_id != m_run_id) {
        return std::nullopt;
    }
    const auto found = m_receipts.find(id);
    if (found == m_receipts.end() || found->second.id != id ||
        RoguelikeTraining.unique_identity(found->second.name) != id || found->second.elite != before.elite ||
        found->second.level != before.level) {
        return std::nullopt;
    }
    return found->second;
}

bool RecruitmentOriginLedger::confirm(
    const RecruitmentDutyOffer& offer,
    const std::optional<RoguelikeOper>& before,
    bool confirmed,
    int recruit_index)
{
    const auto cap = RoguelikeTraining.maximum_level(offer.name, offer.elite);
    if (!confirmed || m_run_id.empty() || offer.client != m_client || offer.uid != m_uid || offer.run_id != m_run_id ||
        offer.id.empty() || RoguelikeTraining.unique_identity(offer.name) != offer.id || offer.analysis_id == 0 ||
        !offer.price_index || !offer.hope_cost || *offer.hope_cost < 0 || !cap || offer.level <= 0 ||
        offer.level > *cap || recruit_index < 1 || offer.page_index < 0 || offer.observed_at.empty() ||
        (offer.operation != "招募" && offer.operation != "进阶")) {
        return false;
    }
    const auto prior = before ? find(offer.client, offer.uid, offer.run_id, offer.id, *before) : std::nullopt;
    RecruitmentOriginReceipt next {
        .client = offer.client,
        .uid = offer.uid,
        .run_id = offer.run_id,
        .id = offer.id,
        .name = offer.name,
        .elite = offer.elite,
        .level = offer.level,
        .receipt_id = offer.client + "/" + offer.uid + "/" + offer.run_id + "/recruit" + std::to_string(recruit_index) +
                      "/analysis" + std::to_string(offer.analysis_id) + "/card" +
                      std::to_string(offer.observation_index),
        .previous_receipt_id = prior ? prior->receipt_id : std::string {},
        .analysis_id = offer.analysis_id,
        .observation_index = offer.observation_index,
        .price_index = *offer.price_index,
        .page_index = offer.page_index,
        .hope_cost = *offer.hope_cost,
        .operation = offer.operation,
        .observed_at = offer.observed_at,
    };
    if (offer.positive_temporary || offer.observed_temporary == true) {
        next.temporary = true;
        next.binding = "confirmed_positive_temporary";
    }
    else if (offer.operation == "进阶" && before && offer.elite > before->elite && prior) {
        next.ordinary = prior->ordinary;
        next.temporary = prior->temporary;
        next.rule_source = prior->rule_source;
        next.binding = prior->ordinary            ? "confirmed_current_run_non_temporary"
                       : prior->temporary == true ? "confirmed_current_run_temporary"
                                                  : "confirmed_current_run_unknown_origin";
    }
    else if (
        offer.operation == "招募" && !before && offer.ordinary_origin &&
        ((offer.training_binding == "source_conditioned_non_temporary" && *offer.hope_cost > 0 &&
          !offer.temporary_rule_source.empty()) ||
         (offer.training_binding == "observed_non_temporary" && offer.observed_temporary == false))) {
        next.ordinary = true;
        next.temporary = false;
        next.binding = offer.training_binding;
        next.rule_source = offer.temporary_rule_source;
    }
    m_receipts.insert_or_assign(offer.id, std::move(next));
    return true;
}

json::value recruitment_origin_to_json(const RecruitmentOriginReceipt& x)
{
    return json::object { { "client", x.client },
                          { "uid", x.uid },
                          { "run_id", x.run_id },
                          { "operator_id", x.id },
                          { "operator", x.name },
                          { "elite", x.elite },
                          { "level", x.level },
                          { "receipt_id", x.receipt_id },
                          { "previous_receipt_id", x.previous_receipt_id },
                          { "analysis_id", x.analysis_id },
                          { "card_index", x.observation_index },
                          { "price_index", x.price_index },
                          { "page_index", x.page_index },
                          { "hope_cost", x.hope_cost },
                          { "operation", x.operation },
                          { "observed_at", x.observed_at },
                          { "binding", x.binding },
                          { "ordinary_origin", x.ordinary },
                          { "instance_temporary", x.temporary ? json::value(*x.temporary) : json::value(nullptr) },
                          { "temporary_field_is_UI_observation", false },
                          { "rule_source", x.rule_source },
                          { "confirmed", true },
                          { "persisted_to_account_cache", false } };
}

bool BlackFlowRecruitmentDutyCatalog::load(
    const std::filesystem::path& f,
    const std::filesystem::path& t,
    const std::filesystem::path& b)
{
    m_valid = false;
    m_operators = json::object {};
    m_temporary_rule = nullptr;
    m_revision.clear();
    m_public_skill_sha.clear();
    const auto facts = json::open(f), training = json::open(t), battle = json::open(b);
    return facts && training && battle && configure(*facts, *training, *battle);
}

bool BlackFlowRecruitmentDutyCatalog::configure(const json::value& f, const json::value& t, const json::value& b)
{
    m_valid = false;
    m_operators = json::object {};
    m_temporary_rule = nullptr;
    m_revision.clear();
    if (!f.is_object() || !t.is_object() || !b.is_object() || f.get("schema", 0) != 1 ||
        f.get("game_data_revision", "").empty() || f.get("game_data_revision", "") != t.get("game_data_revision", "") ||
        f.get("character_table_sha256", "").empty() ||
        f.get("character_table_sha256", "") != t.get("character_table_sha256", "") ||
        f.get("public_revision", "").empty() || f.get("public_skill_sha256", "").empty() ||
        f.get("public_character_sha256", "").empty()) {
        return false;
    }
    const auto operators = f.find<json::object>("operators"), rules = t.find<json::object>("operators"),
               chars = b.find<json::object>("chars");
    if (!operators || !rules || !chars) {
        return false;
    }
    json::object verified;
    for (const auto& [id, fact] : *operators) {
        const auto rule = rules->find<json::object>(id), properties = chars->find<json::object>(id);
        if (!rule || !properties || fact.get("name", "") != properties->get("name", "") ||
            !fact.contains("persistent_blocker_class") || !fact.at("persistent_blocker_class").is<bool>() ||
            !fact.contains("direct_normal_medic") || !fact.at("direct_normal_medic").is<bool>() ||
            fact.get("profession", "") != properties->get("profession", "") ||
            fact.get("subProfessionId", "") != properties->get("subProfessionId", "") ||
            !fact.contains("base_block_counts") || !rule->contains("base_block_counts") ||
            fact.at("base_block_counts") != rule->at("base_block_counts") || !fact.contains("max_levels") ||
            !rule->contains("max_levels") || fact.at("max_levels") != rule->at("max_levels")) {
            return false;
        }
        const auto slots = fact.find<json::array>("slots"), training_slots = rule->find<json::array>("skills");
        if (!slots || !training_slots || slots->size() != training_slots->size()) {
            return false;
        }
        for (std::size_t i = 0; i < slots->size(); ++i) {
            if (slots->at(i).get("skill_id", "") != training_slots->at(i).get("skill_id", "") ||
                slots->at(i).get("unlock_elite", -1) != training_slots->at(i).get("unlock_elite", -1) ||
                slots->at(i).get("unlock_level", -1) != training_slots->at(i).get("unlock_level", -1)) {
                return false;
            }
        }
        verified[id] = fact;
    }
    m_operators = std::move(verified);
    m_revision = f.get("game_data_revision", "");
    m_public_skill_sha = f.get("public_skill_sha256", "");
    if (const auto temporary = f.find<json::object>("temporary_rule");
        temporary && temporary->get("kind", "") == "temporary_recruit_hope_zero" && temporary->get("topic", -1) == 6 &&
        temporary->get("mode", -1) == 0 && !temporary->get("sha256", "").empty()) {
        m_temporary_rule = *temporary;
    }
    m_valid = !operators->empty();
    return m_valid;
}

std::string BlackFlowRecruitmentDutyCatalog::temporary_rule_source() const
{
    return m_valid ? m_temporary_rule.get("sha256", "") : std::string {};
}

std::array<DutyBound, 2> BlackFlowRecruitmentDutyCatalog::body(const std::string& name, int elite, int level) const
{
    std::array<DutyBound, 2> result;
    const auto id = RoguelikeTraining.unique_identity(name);
    if (!m_valid || id.empty() || !m_operators.contains(id)) {
        return result;
    }
    const auto& fact = m_operators.at(id);
    if (fact.get("name", "") != name) {
        return result;
    }
    result[1] = fact.get("direct_normal_medic", false) ? DutyBound { 1, 1 } : DutyBound { 0, 0 };
    if (!fact.get("persistent_blocker_class", false)) {
        result[0] = { 0, 0 };
    }
    else if (const auto count = RoguelikeTraining.base_block_count(name, elite, level)) {
        result[0] = *count > 0 ? DutyBound { 1, 1 } : DutyBound { 0, 0 };
    }
    return result;
}

json::value BlackFlowRecruitmentDutyCatalog::skill(const std::string& id, int slot, int rank) const
{
    if (!m_valid || slot < 1 || slot > 3 || rank < 1 || rank > 10 || !m_operators.contains(id)) {
        return nullptr;
    }
    const auto& slots = m_operators.at(id).at("slots").as_array();
    if (slots.size() < static_cast<std::size_t>(slot)) {
        return nullptr;
    }
    const auto rows = slots.at(slot - 1).find<json::array>("rows");
    if (!rows || rows->size() < static_cast<std::size_t>(rank)) {
        return nullptr;
    }
    const auto& row = rows->at(rank - 1);
    if (!row.is_object() || row.get("rank", -1) != rank) {
        return nullptr;
    }
    for (const auto key : { "sp_cost", "initial_sp" }) {
        if (!row.contains(key) || !row.at(key).is_number() || !std::isfinite(row.at(key).as_double()) ||
            row.at(key).as_double() < 0) {
            return nullptr;
        }
    }
    if (row.get("sp_type", "").empty() || row.get("skill_type", "").empty() || !row.contains("duration") ||
        !row.at("duration").is_number() || !std::isfinite(row.at("duration").as_double()) ||
        row.at("duration").as_double() < -1) {
        return nullptr;
    }
    auto result = row;
    result["skill_id"] = slots.at(slot - 1).at("skill_id");
    result["model_revision"] = m_revision;
    result["deadline_producer_available"] = false;
    result["used_for_output_winner"] = false;
    return result;
}

json::value
    BlackFlowRecruitmentDutyCatalog::heal_packet(const std::string& name, int elite, int level, int slot, int rank)
        const
{
    const auto id = RoguelikeTraining.unique_identity(name);
    const auto maximum_rank = RoguelikeTraining.maximum_training(name, slot);
    if (id.empty() || !m_valid || !m_operators.contains(id) || !maximum_rank || rank > *maximum_rank || elite < 0 ||
        (elite == 0 && rank > 4) || (elite < 2 && rank > 7) ||
        RoguelikeTraining.skill_availability(name, slot, elite, level) != SkillAvailability::Available) {
        return nullptr;
    }
    const auto row = skill(id, slot, rank);
    if (row.is_null() || !row.contains("heal_atk_fraction") || !row.at("heal_atk_fraction").is_number()) {
        return nullptr;
    }
    const auto& op = m_operators.at(id);
    const auto phases = op.find<json::array>("atk_keyframes"), ranges = op.find<json::array>("ranges");
    const auto trust = op.find<json::array>("trust_atk_bounds"),
               potential = op.find<json::array>("potential_atk_bounds");
    const auto& slots = op.at("slots").as_array();
    if (!phases || !ranges || !trust || !potential || phases->size() <= static_cast<std::size_t>(elite) ||
        ranges->size() <= static_cast<std::size_t>(elite) || trust->size() != 2 || potential->size() != 2 ||
        slots.at(slot - 1).get("heal_kind", "") != "direct_heal_atk_buff_single_target_component") {
        return nullptr;
    }
    if (!phases->at(elite).is_array()) {
        return nullptr;
    }
    const auto keys = &phases->at(elite).as_array();
    if (keys->size() != 2) {
        return nullptr;
    }
    const int first = keys->at(0).get("level", -1), last = keys->at(1).get("level", -1);
    if (first < 1 || last < first || level < first || level > last) {
        return nullptr;
    }
    for (const auto& key : *keys) {
        if (!key.contains("atk") || !key.at("atk").is_number() || !key.contains("attack_time") ||
            !key.at("attack_time").is_number()) {
            return nullptr;
        }
    }
    for (const auto& value : *trust) {
        if (!value.is_number()) {
            return nullptr;
        }
    }
    for (const auto& value : *potential) {
        if (!value.is_number()) {
            return nullptr;
        }
    }
    const double factor = last == first ? 0.0 : static_cast<double>(level - first) / (last - first);
    const double atk = keys->at(0).at("atk").as_double() +
                       factor * (keys->at(1).at("atk").as_double() - keys->at(0).at("atk").as_double());
    const double low = std::floor(atk) + trust->at(0).as_double() + potential->at(0).as_double();
    const double high = std::ceil(atk) + trust->at(1).as_double() + potential->at(1).as_double();
    const double multiplier = 1.0 + row.at("heal_atk_fraction").as_double();
    const double interval =
        keys->at(0).at("attack_time").as_double() +
        factor * (keys->at(1).at("attack_time").as_double() - keys->at(0).at("attack_time").as_double());
    if (!std::isfinite(low) || !std::isfinite(high) || !std::isfinite(interval) || !std::isfinite(multiplier) ||
        low <= 0 || high < low || interval <= 0 || multiplier < 1) {
        return nullptr;
    }
    return json::object {
        { "model_revision", m_revision },
        { "skill_id", row.at("skill_id") },
        { "effective_rank", rank },
        { "base_packet_bounds", json::array { low, high } },
        { "active_packet_bounds", json::array { std::floor(low * multiplier), std::ceil(high * multiplier) } },
        { "range_id", ranges->at(elite) },
        { "subclass", op.at("subProfessionId") },
        { "base_attack_interval", interval },
        { "sp_type", row.at("sp_type") },
        { "sp_gap", row.at("sp_cost").as_double() - row.at("initial_sp").as_double() },
        { "duration", row.at("duration") },
        { "trust_and_potential_known", false },
        { "unmodeled_modifiers",
          json::array { "talents", "equipment", "collections", "actual_patient_coverage", "actual_combat_SP_input" } },
        { "scope", "intrinsic_single_target_heal_action_component_not_current_heal_or_HPS" },
        { "HPS", nullptr }
    };
}

std::optional<int> current_roster_preparation_rank(const std::string& name, const RoguelikeOper& current, int slot)
{
    const auto& prep = current.preparation;
    const auto identity = RoguelikeTraining.unique_identity(name);
    if (identity.empty() || prep.operator_id != identity || slot < 1 || slot > 3 || prep.training_sequence == 0 ||
        prep.selection_sequence < prep.training_sequence || prep.selected_skill != slot) {
        return std::nullopt;
    }
    // These are direct facts about the existing run instance. They do not
    // establish ordinary origin or the training of a future promoted offer.
    return normalize_displayed_skill_level_for_run(
        name,
        slot,
        current.elite,
        current.level,
        prep.displayed_skill_levels[slot - 1]);
}

void evaluate_recruitment_output(
    RecruitmentDutyOffer& offer,
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const BlackFlowAccountProfile* account,
    const std::unordered_map<std::string, RoguelikeOper>& roster,
    const RecruitmentDutyContext& context,
    const RecruitmentOriginLedger* origins)
{
    offer.projected_output = {};
    offer.intrinsic_reference = {};
    offer.intrinsic_reference.objective = PrimaryProjectionObjective::OutsideIntrinsicReference;
    const auto evidence = context.output_evidence;
    if (!evidence || evidence->source.empty() || evidence->client != context.client || evidence->uid != context.uid ||
        evidence->run_id != context.run_id || context.run_id.empty() || offer.client != context.client ||
        offer.uid != context.uid || offer.run_id != context.run_id ||
        offer.id != RoguelikeTraining.unique_identity(offer.name) || !account || account->client() != context.client ||
        account->uid() != context.uid || !offer.ordinary_origin || !offer.account_training_used ||
        offer.effective_rank < 1 || offer.positive_temporary) {
        offer.projected_output.reason = offer.intrinsic_reference.reason =
            "recruit_output_context_rank_or_origin_unknown_not_invented";
        return;
    }
    const auto attrs_for = [&](const std::string& id) {
        const auto found = evidence->attributes.find(id);
        return found == evidence->attributes.end() ? ProjectedOutputAttributes {} : found->second;
    };
    const auto risk_for = [&](const std::string& id) {
        std::vector<std::string> result;
        for (const auto& key : { id, std::string("*") }) {
            const auto found = evidence->known_operator_risks.find(key);
            if (found != evidence->known_operator_risks.end()) {
                result.insert(result.end(), found->second.begin(), found->second.end());
            }
        }
        return result;
    };
    offer.projected_output = catalog.projected_output(
        offer.name,
        offer.elite,
        offer.level,
        offer.planned_skill,
        offer.effective_rank,
        attrs_for(offer.id),
        *evidence);
    offer.intrinsic_reference = catalog.intrinsic_primary(
        offer.name,
        offer.elite,
        offer.level,
        offer.planned_skill,
        offer.effective_rank,
        attrs_for(offer.id),
        evidence->source);
    const auto risks = risk_for(offer.id);
    if (!risks.empty()) {
        offer.intrinsic_reference.qualified = false;
        offer.intrinsic_reference.reason = "known_current_context_differential_risk_vetoes_intrinsic_reference";
        offer.intrinsic_reference.limitations.insert(
            offer.intrinsic_reference.limitations.end(),
            risks.begin(),
            risks.end());
    }
    if (account->skill_level_for_run(offer.name, offer.planned_skill, offer.elite, offer.level) !=
        std::optional<int>(offer.effective_rank)) {
        offer.projected_output.qualified = offer.intrinsic_reference.qualified = false;
        offer.projected_output.reason = offer.intrinsic_reference.reason =
            "offer_rank_not_bound_to_exact_account_training";
        return;
    }
    const auto reference_range = BattleData.get_range(battle::Role::Unknown, offer.name, offer.elite);
    for (auto* output : { &offer.projected_output, &offer.intrinsic_reference }) {
        if (!output->qualified) {
            continue;
        }
        const bool intrinsic = output->objective == PrimaryProjectionObjective::OutsideIntrinsicReference;
        if (reference_range.empty()) {
            output->reason = "reference_footprint_unbound";
            continue;
        }
        bool gain = true;
        for (const auto& [name, old] : roster) {
            const auto id = RoguelikeTraining.unique_identity(name);
            const auto props = BattleData.find_first_oper(battle::Role::Unknown, name);
            if (id.empty() || !props || old.elite < 0 || old.elite > 2) {
                output->reason = "roster_identity_or_phase_unknown_not_zero";
                gain = false;
                break;
            }
            if (props->role == battle::Role::Medic && props->sub_role != battle::SubRole::Medic_IncantationMedic) {
                continue;
            }
            const auto range = BattleData.get_range(props->role, name, old.elite);
            if (range.empty()) {
                output->reason = "roster_reference_footprint_unknown";
                gain = false;
                break;
            }
            const bool same_reference =
                range.size() == reference_range.size() && std::ranges::all_of(reference_range, [&](const Point& p) {
                    return std::ranges::find(range, p) != range.end();
                });
            const bool full_coverage = std::ranges::all_of(reference_range, [&](const Point& p) {
                return std::ranges::find(range, p) != range.end();
            });
            if (intrinsic ? !same_reference : !full_coverage) {
                if (intrinsic) {
                    output->limitations.push_back("roster_different_reference_footprint_not_scored_as_zero:" + name);
                }
                continue;
            }
            if (intrinsic && !risk_for(id).empty()) {
                output->reason = "known_incumbent_context_risk_prevents_reference_increment";
                gain = false;
                break;
            }
            const auto& cfg = RoguelikeRecruit.get_oper_info("BlackFlow", name);
            std::array<SkillAvailability, 3> availability;
            for (int slot = 1; slot <= 3; ++slot) {
                availability[slot - 1] = RoguelikeTraining.skill_availability(name, slot, old.elite, old.level);
            }
            const auto plan = resolve_recruitment_skill_plan(cfg.skill, cfg.alternate_skill, availability);
            int rank = -1;
            std::string rank_source = "unknown_origin_or_training";
            const auto observed_rank = current_roster_preparation_rank(name, old, plan.selected);
            if (observed_rank) {
                rank = *observed_rank;
                rank_source = "current_phase_normalized_preparation";
            }
            // Account fallback still requires a confirmed ordinary instance.
            if (rank < 1) {
                const auto origin =
                    origins ? origins->find(context.client, context.uid, context.run_id, id, old) : std::nullopt;
                if (origin && origin->ordinary) {
                    rank = account->skill_level_for_run(name, plan.selected, old.elite, old.level).value_or(-1);
                    if (rank > 0) {
                        rank_source = "confirmed_ordinary_uid_account_training";
                    }
                }
            }
            output->limitations.push_back(
                "incumbent_effective_rank:" + id + ":" + std::to_string(rank) + ":" + rank_source);
            const auto before =
                intrinsic
                    ? catalog.intrinsic_primary(
                          name,
                          old.elite,
                          old.level,
                          plan.selected,
                          rank,
                          attrs_for(id),
                          evidence->source)
                    : catalog
                          .projected_output(name, old.elite, old.level, plan.selected, rank, attrs_for(id), *evidence);
            if (!before.qualified || !projected_output_strictly_better(*output, before)) {
                output->reason = before.qualified ? "no_reference_component_increment_beyond_same_cohort_incumbent"
                                                  : "same_cohort_incumbent_component_unknown_not_zero";
                gain = false;
                break;
            }
        }
        output->marginal_known_positive = gain;
        if (gain) {
            output->reason = intrinsic ? "estimated_intrinsic_reference_increment_not_global_roster_or_run_dominance"
                                       : "bounded_intrinsic_projection_gain_not_global_team_dominance";
        }
    }
}

void evaluate_recruitment_duties(
    RecruitmentDutyOffer& offer,
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const std::unordered_map<std::string, RoguelikeOper>& roster)
{
    offer.capability = catalog.body(offer.name, offer.elite, offer.level);
    const auto old = roster.find(offer.name);
    const bool promotion = offer.operation == "进阶";
    const bool recruit = offer.operation == "招募";
    offer.replacement_valid = promotion ? old != roster.end() : recruit && old == roster.end();
    std::vector<std::array<DutyBound, 2>> others;
    for (const auto& [name, oper] : roster) {
        if (!promotion || name != offer.name) {
            others.push_back(catalog.body(name, oper.elite, oper.level));
        }
    }
    const auto old_body = promotion && old != roster.end()
                              ? catalog.body(offer.name, old->second.elite, old->second.level)
                              : std::array<DutyBound, 2> { DutyBound { 0, 0 }, DutyBound { 0, 0 } };
    for (std::size_t task = 0; task < 2; ++task) {
        offer.marginal[task] = recruitment_duty_detail::replacement_marginal(
            recruitment_duty_detail::presence(others, task),
            old_body[task],
            offer.capability[task]);
    }
}

std::vector<RecruitmentDutyOffer> observe_recruitment_duties(
    const RoguelikeRecruitImageAnalyzer& analyzer,
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const BlackFlowAccountProfile* account,
    const std::unordered_map<std::string, RoguelikeOper>& roster,
    const RecruitmentDutyContext& context)
{
    return observe_recruitment_duties(analyzer, catalog, account, roster, context, nullptr);
}

std::vector<RecruitmentDutyOffer> observe_recruitment_duties(
    const RoguelikeRecruitImageAnalyzer& analyzer,
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const BlackFlowAccountProfile* account,
    const std::unordered_map<std::string, RoguelikeOper>& roster,
    const RecruitmentDutyContext& context,
    const RecruitmentOriginLedger* origins)
{
    std::vector<RecruitmentDutyOffer> result;
    for (const auto& candidate : analyzer.get_candidate_evidence()) {
        RecruitmentDutyOffer offer;
        offer.client = context.client;
        offer.uid = context.uid;
        offer.run_id = context.run_id;
        offer.analysis_id = candidate.analysis_id;
        offer.observation_index = candidate.observation_index;
        offer.page_index = context.page_index;
        offer.name = candidate.name;
        offer.id = RoguelikeTraining.unique_identity(candidate.name);
        offer.elite = candidate.elite;
        offer.level = candidate.level;
        offer.observed_at = context.observed_at;
        offer.observed_temporary = candidate.temporary;
        const auto& configured = RoguelikeRecruit.get_oper_info("BlackFlow", offer.name);
        std::array<SkillAvailability, 3> availability;
        for (int slot = 1; slot <= 3; ++slot) {
            availability[slot - 1] = RoguelikeTraining.skill_availability(offer.name, slot, offer.elite, offer.level);
        }
        offer.skill_plan = resolve_recruitment_skill_plan(configured.skill, configured.alternate_skill, availability);
        offer.planned_skill = offer.skill_plan.selected;
        offer.skill_availability =
            offer.planned_skill > 0 ? availability[offer.planned_skill - 1] : SkillAvailability::Unknown;
        const auto linked = analyzer.resolve_candidate_hope_cost(candidate);
        if (linked) {
            offer.operation = linked->get().operation;
            offer.hope_cost = linked->get().hope_cost;
            offer.price_index = candidate.hope_cost_evidence_index;
        }
        const bool positive_temporary = candidate.temporary == true || candidate.legacy_temporary_flag == true;
        offer.positive_temporary = positive_temporary;
        offer.temporary_rule_source = catalog.temporary_rule_source();
        const bool source_non_temporary = context.theme == "BlackFlow" && context.mode == 0 && linked &&
                                          !offer.temporary_rule_source.empty() && offer.operation == "招募" &&
                                          offer.hope_cost && *offer.hope_cost > 0 && !positive_temporary;
        const bool directly_ordinary_recruit = offer.operation == "招募" && candidate.temporary == false;
        offer.ordinary_origin = !positive_temporary && (directly_ordinary_recruit || source_non_temporary);
        offer.training_binding = positive_temporary          ? "observed_temporary_no_account_training"
                                 : source_non_temporary      ? "source_conditioned_non_temporary"
                                 : directly_ordinary_recruit ? "observed_non_temporary"
                                                             : "unknown_origin";
        if (offer.operation == "进阶" && !positive_temporary && origins) {
            const auto previous = roster.find(offer.name);
            const auto origin =
                previous == roster.end()
                    ? std::nullopt
                    : origins->find(context.client, context.uid, context.run_id, offer.id, previous->second);
            if (origin && offer.elite > previous->second.elite) {
                offer.inherited_origin_receipt = origin->receipt_id;
                offer.ordinary_origin = origin->ordinary;
                offer.temporary_rule_source = origin->rule_source;
                offer.training_binding = origin->ordinary            ? "confirmed_current_run_non_temporary"
                                         : origin->temporary == true ? "confirmed_current_run_temporary"
                                                                     : "confirmed_current_run_unknown_origin";
            }
        }
        if (offer.ordinary_origin && account && account->client() == context.client && account->uid() == context.uid &&
            !context.uid.empty() && !offer.id.empty()) {
            const auto owned = account->find_operator(offer.name);
            if (owned && owned->get().id == offer.id) {
                offer.effective_rank =
                    account->skill_level_for_run(offer.name, offer.planned_skill, offer.elite, offer.level)
                        .value_or(-1);
                offer.account_training_used = offer.effective_rank > 0;
            }
        }
        offer.skill_facts = catalog.skill(offer.id, offer.planned_skill, offer.effective_rank);
        offer.healing_component =
            catalog.heal_packet(offer.name, offer.elite, offer.level, offer.planned_skill, offer.effective_rank);
        evaluate_recruitment_duties(offer, catalog, roster);
        evaluate_recruitment_output(offer, catalog, account, roster, context, origins);
        if (offer.id.empty()) {
            offer.reasons.emplace_back("exact_identity_unknown");
        }
        if (!linked) {
            offer.reasons.emplace_back("same_analysis_price_link_unverified");
        }
        if (!offer.hope_cost) {
            offer.reasons.emplace_back("hope_price_unknown_not_zero");
        }
        if (!offer.replacement_valid) {
            offer.reasons.emplace_back("operation_and_roster_replacement_not_proven");
        }
        if (!offer.ordinary_origin) {
            offer.reasons.emplace_back("origin_unproven_or_temporary_account_training_not_inherited");
        }
        if (!catalog.valid()) {
            offer.reasons.emplace_back("nov1_duty_catalog_unbound");
        }
        if (offer.effective_rank < 0) {
            offer.reasons.emplace_back("effective_planned_skill_rank_unknown_not_maximum");
        }
        const bool affordable = offer.hope_cost && *offer.hope_cost >= 0 &&
                                (*offer.hope_cost == 0 || (context.hope >= 0 && *offer.hope_cost <= context.hope));
        if (!affordable) {
            offer.reasons.emplace_back("hope_budget_insufficient_or_unknown");
        }
        offer.feasible = context.theme == "BlackFlow" && context.mode == 0 && !context.client.empty() &&
                         !context.uid.empty() && candidate.usable_result_index && !offer.id.empty() && linked &&
                         offer.replacement_valid && affordable && catalog.valid();
        result.emplace_back(std::move(offer));
    }
    return result;
}

bool recruitment_duty_strictly_dominates(const RecruitmentDutyOffer& a, const RecruitmentDutyOffer& b)
{
    if (!a.feasible || !b.feasible || a.uid.empty() || a.uid != b.uid || a.client != b.client || !a.hope_cost ||
        !b.hope_cost || *a.hope_cost > *b.hope_cost) {
        return false;
    }
    // Equal presence does not establish equal healing or combat quality.
    // Hope is a nonincrease constraint, never the required strict duty gain.
    bool strict = false;
    bool fills_need = false;
    for (std::size_t task = 0; task < 2; ++task) {
        if (a.marginal[task].lower < b.marginal[task].upper) {
            return false;
        }
        strict = strict || a.marginal[task].lower > b.marginal[task].upper;
        fills_need = fills_need || a.marginal[task].lower > 0;
    }
    return strict && fills_need;
}

RecruitmentDutyDecision choose_recruitment_duty(
    const std::vector<RecruitmentDutyOffer>& offers,
    const std::vector<std::pair<std::string, int>>& eligible,
    const std::string& legacy_name,
    int legacy_page,
    bool fixed_opening)
{
    RecruitmentDutyDecision result;
    if (fixed_opening) {
        result.reason = "fixed_opening_retains_execution_semantics";
        return result;
    }
    const auto legacy_match = [&](const auto& x) {
        return x.name == legacy_name && x.page_index == legacy_page;
    };
    const auto legacy = std::ranges::find_if(offers, legacy_match);
    if (legacy == offers.end() || std::ranges::count_if(offers, legacy_match) != 1) {
        result.reason = "legacy_offer_exact_evidence_missing_or_ambiguous";
        return result;
    }
    std::vector<std::size_t> dominators;
    for (std::size_t i = 0; i < offers.size(); ++i) {
        if (std::ranges::count_if(
                offers,
                [&](const auto& x) { return x.name == offers[i].name && x.page_index == offers[i].page_index; }) == 1 &&
            std::ranges::find(eligible, std::pair { offers[i].name, offers[i].page_index }) != eligible.end() &&
            recruitment_duty_strictly_dominates(offers[i], *legacy)) {
            dominators.push_back(i);
        }
    }
    std::vector<std::size_t> frontier;
    for (auto i : dominators) {
        if (std::ranges::none_of(dominators, [&](auto j) {
                return j != i && recruitment_duty_strictly_dominates(offers[j], offers[i]);
            })) {
            frontier.push_back(i);
        }
    }
    if (frontier.size() == 1) {
        result.selected_offer = frontier[0];
        result.adjusted = true;
        result.reason = "strict_remaining_roster_duty_gain_with_nonincreasing_hope_cost";
    }
    else {
        result.reason = frontier.empty() ? "no_strict_remaining_duty_gain_at_nonincreasing_hope_keep_legacy"
                                         : "multiple_incomparable_duty_dominators_keep_legacy";
        if (frontier.empty() && legacy->marginal[1].lower > 0 && std::ranges::any_of(offers, [&](const auto& x) {
                return x.name != legacy->name && x.marginal[1].lower > 0 && !x.healing_component.is_null() &&
                       !legacy->healing_component.is_null();
            })) {
            result.reason = "intrinsic_heal_packets_known_actual_modifiers_and_patient_task_unproven_keep_legacy";
        }
    }
    if (!result.selected_offer && dominators.empty() && legacy->projected_output.qualified) {
        std::vector<std::size_t> output_candidates;
        const auto output_better = [&](const auto& a, const auto& b) {
            if (!a.feasible || !b.feasible || a.client != b.client || a.uid.empty() || a.uid != b.uid ||
                a.run_id != b.run_id || !a.hope_cost || !b.hope_cost || *a.hope_cost > *b.hope_cost ||
                !a.projected_output.marginal_known_positive) {
                return false;
            }
            for (std::size_t task = 0; task < 2; ++task) {
                if (a.marginal[task].lower < b.marginal[task].upper) {
                    return false;
                }
            }
            return projected_output_strictly_better(a.projected_output, b.projected_output);
        };
        for (std::size_t i = 0; i < offers.size(); ++i) {
            if (std::ranges::count_if(
                    offers,
                    [&](const auto& x) { return x.name == offers[i].name && x.page_index == offers[i].page_index; }) !=
                    1 ||
                std::ranges::find(eligible, std::pair { offers[i].name, offers[i].page_index }) == eligible.end() ||
                !output_better(offers[i], *legacy)) {
                continue;
            }
            output_candidates.push_back(i);
        }
        std::vector<std::size_t> frontier;
        for (const auto i : output_candidates) {
            if (std::ranges::none_of(output_candidates, [&](const auto j) {
                    return i != j && output_better(offers[j], offers[i]);
                })) {
                frontier.push_back(i);
            }
        }
        if (frontier.size() == 1) {
            result.selected_offer = frontier[0];
            result.adjusted = true;
            result.reason = "same_duty_frontier_bounded_primary_gain_nonincreasing_hope_not_team_dominance";
        }
    }
    if (!result.selected_offer && dominators.empty() && legacy->intrinsic_reference.qualified) {
        std::vector<std::size_t> candidates;
        bool unequal_price_gain = false;
        const auto reference_better = [&](const auto& a, const auto& b, bool equal_price) {
            if (!a.feasible || !b.feasible || a.client != b.client || a.uid.empty() || a.uid != b.uid ||
                a.run_id.empty() || a.run_id != b.run_id || !a.hope_cost || !b.hope_cost || a.analysis_id == 0 ||
                b.analysis_id == 0 || !a.price_index || !b.price_index ||
                (equal_price && *a.hope_cost != *b.hope_cost) || !a.ordinary_origin || !b.ordinary_origin ||
                !a.account_training_used || !b.account_training_used || a.positive_temporary || b.positive_temporary ||
                !a.intrinsic_reference.marginal_known_positive ||
                a.intrinsic_reference.objective != PrimaryProjectionObjective::OutsideIntrinsicReference ||
                b.intrinsic_reference.objective != PrimaryProjectionObjective::OutsideIntrinsicReference) {
                return false;
            }
            for (std::size_t task = 0; task < 2; ++task) {
                if (a.marginal[task].lower < b.marginal[task].upper) {
                    return false;
                }
            }
            return projected_output_strictly_better(a.intrinsic_reference, b.intrinsic_reference);
        };
        for (std::size_t i = 0; i < offers.size(); ++i) {
            if (std::ranges::count_if(
                    offers,
                    [&](const auto& x) { return x.name == offers[i].name && x.page_index == offers[i].page_index; }) !=
                    1 ||
                std::ranges::find(eligible, std::pair { offers[i].name, offers[i].page_index }) == eligible.end()) {
                continue;
            }
            if (reference_better(offers[i], *legacy, true)) {
                candidates.push_back(i);
            }
            else if (offers[i].hope_cost != legacy->hope_cost && reference_better(offers[i], *legacy, false)) {
                unequal_price_gain = true;
            }
        }
        std::vector<std::size_t> reference_frontier;
        for (const auto i : candidates) {
            if (std::ranges::none_of(candidates, [&](const auto j) {
                    return i != j && reference_better(offers[j], offers[i], true);
                })) {
                reference_frontier.push_back(i);
            }
        }
        if (reference_frontier.size() == 1) {
            result.selected_offer = reference_frontier[0];
            result.adjusted = true;
            result.intrinsic_reference_used = true;
            result.reason = "estimated_outside_intrinsic_reference_gain_equal_hope_not_current_run_dominance";
        }
        else if (unequal_price_gain) {
            result.reason =
                "reference_gain_known_unequal_hope_needs_gain_per_hope_model_not_universal_nonincrease_rule";
        }
    }
    return result;
}

bool recruitment_duty_refind_matches(const RecruitmentDutyOffer& intended, const RecruitmentDutyOffer& actual)
{
    if (intended.intrinsic_reference_used &&
        (!actual.intrinsic_reference.qualified ||
         intended.intrinsic_reference.objective != actual.intrinsic_reference.objective ||
         intended.intrinsic_reference.marginal_known_positive != actual.intrinsic_reference.marginal_known_positive ||
         intended.intrinsic_reference.evidence_source != actual.intrinsic_reference.evidence_source ||
         intended.intrinsic_reference.range != actual.intrinsic_reference.range ||
         intended.intrinsic_reference.lower != actual.intrinsic_reference.lower ||
         intended.intrinsic_reference.upper != actual.intrinsic_reference.upper ||
         intended.intrinsic_reference.limitations != actual.intrinsic_reference.limitations)) {
        return false;
    }

    if (intended.projected_output.qualified &&
        (!actual.projected_output.qualified ||
         intended.projected_output.marginal_known_positive != actual.projected_output.marginal_known_positive ||
         intended.projected_output.evidence_source != actual.projected_output.evidence_source ||
         intended.projected_output.range != actual.projected_output.range ||
         intended.projected_output.lower != actual.projected_output.lower ||
         intended.projected_output.upper != actual.projected_output.upper)) {
        return false;
    }
    return actual.feasible && intended.client == actual.client && !intended.uid.empty() && intended.uid == actual.uid &&
           intended.run_id == actual.run_id && intended.positive_temporary == actual.positive_temporary &&
           intended.inherited_origin_receipt == actual.inherited_origin_receipt && !intended.id.empty() &&
           intended.id == actual.id && intended.name == actual.name && intended.elite == actual.elite &&
           intended.level == actual.level && intended.planned_skill == actual.planned_skill &&
           intended.hope_cost == actual.hope_cost && intended.operation == actual.operation &&
           intended.training_binding == actual.training_binding && intended.effective_rank == actual.effective_rank;
}

RecruitmentFinalOffer resolve_recruitment_final_offer(
    const RoguelikeRecruitImageAnalyzer& analyzer,
    const std::vector<RecruitmentDutyOffer>& observed,
    const battle::roguelike::Recruitment& selected,
    const std::optional<RecruitmentDutyOffer>& intent)
{
    RecruitmentFinalOffer result;
    const auto same_name = [&](const auto& x) {
        return x.name == selected.name;
    };
    const auto found = std::ranges::find_if(observed, same_name);
    if (found == observed.end() || std::ranges::count_if(observed, same_name) != 1 || found->id.empty() ||
        found->id != RoguelikeTraining.unique_identity(selected.name) || found->analysis_id == 0 ||
        !found->price_index || found->elite != selected.elite || found->level != selected.level ||
        found->hope_cost != selected.hope_cost || (selected.temporary && !found->positive_temporary)) {
        result.reason = "final_card_identity_strength_or_same_analysis_price_not_exact";
        return result;
    }
    if (found->analysis_id != analyzer.get_analysis_id() ||
        found->observation_index >= analyzer.get_candidate_evidence().size()) {
        result.reason = "final_card_analysis_epoch_mismatch";
        return result;
    }
    const auto& raw = analyzer.get_candidate_evidence()[found->observation_index];
    const auto linked = analyzer.resolve_candidate_hope_cost(raw);
    if (!linked || raw.hope_cost_evidence_index != found->price_index || raw.name != found->name ||
        raw.elite != found->elite || raw.level != found->level || linked->get().hope_cost != found->hope_cost ||
        linked->get().operation != found->operation || !raw.usable_result_index ||
        *raw.usable_result_index >= analyzer.get_result().size()) {
        result.reason = "final_card_linked_raw_evidence_mismatch";
        return result;
    }
    const auto& actual = analyzer.get_result()[*raw.usable_result_index];
    if (actual.name != selected.name || actual.rect.x != selected.rect.x || actual.rect.y != selected.rect.y ||
        actual.rect.width != selected.rect.width || actual.rect.height != selected.rect.height) {
        result.reason = "final_card_selected_rectangle_mismatch";
        return result;
    }
    result.observed = *found;
    result.intent_matches = !intent || recruitment_duty_refind_matches(*intent, *found);
    result.reason = result.intent_matches ? "final_card_evidence_matches" : "final_card_differs_from_intended_decision";
    return result;
}

json::value recruitment_duty_to_json(const RecruitmentDutyOffer& x)
{
    json::array capability, marginal, reasons, skill_availability;
    for (const auto value : x.skill_plan.availability) {
        skill_availability.emplace_back(static_cast<int>(value));
    }
    for (std::size_t t = 0; t < 2; ++t) {
        capability.emplace_back(json::array { x.capability[t].lower, x.capability[t].upper });
        marginal.emplace_back(json::array { x.marginal[t].lower, x.marginal[t].upper });
    }
    for (const auto& reason : x.reasons) {
        reasons.emplace_back(reason);
    }
    return json::object {
        { "client", x.client },
        { "uid", x.uid },
        { "run_id", x.run_id },
        { "analysis_id", x.analysis_id },
        { "observation_index", x.observation_index },
        { "price_index", x.price_index ? json::value(*x.price_index) : json::value(nullptr) },
        { "page_index", x.page_index },
        { "name", x.name },
        { "operator_id", x.id },
        { "elite", x.elite },
        { "level", x.level },
        { "planned_skill", x.planned_skill },
        { "configured_preferred_skill", x.skill_plan.preferred },
        { "configured_alternate_skill", x.skill_plan.alternate },
        { "planned_skill_reason", x.skill_plan.reason },
        { "planned_skill_availability", skill_availability },
        { "planned_skill_is_UI_observation", false },
        { "actual_selected_skill", nullptr },
        { "effective_rank", x.effective_rank },
        { "skill_availability", static_cast<int>(x.skill_availability) },
        { "skill_facts", x.skill_facts },
        { "projected_output", projected_output_to_json(x.projected_output) },
        { "outside_intrinsic_reference", projected_output_to_json(x.intrinsic_reference) },
        { "intrinsic_reference_used_for_this_selection", x.intrinsic_reference_used },
        { "intrinsic_healing_component", x.healing_component },
        { "hope_cost", x.hope_cost ? json::value(*x.hope_cost) : json::value(nullptr) },
        { "operation", x.operation },
        { "observed_temporary", x.observed_temporary ? json::value(*x.observed_temporary) : json::value(nullptr) },
        { "training_binding", x.training_binding },
        { "positive_temporary", x.positive_temporary },
        { "inherited_origin_receipt", x.inherited_origin_receipt },
        { "account_training_used", x.account_training_used },
        { "non_temporary_rule_source_sha256", x.temporary_rule_source },
        { "non_temporary_rule_scope", "Topic6/mode0/recruit/positive_exact_price/no_positive_temporary_conflict" },
        { "non_temporary_rule_applied_to_current_offer", x.training_binding == "source_conditioned_non_temporary" },
        { "non_temporary_in_game_independently_verified", false },
        { "capability_bounds", capability },
        { "marginal_bounds", marginal },
        { "feasible_for_hard_override", x.feasible },
        { "reasons", reasons },
        { "source_time_local", x.observed_at },
        { "full_DPS", nullptr },
        { "HPS", nullptr },
        { "task_scope",
          "current_roster_persistent_base_blocker_presence,direct_normal_medical_presence;not_future_lifetime_or_"
          "battle_survival" }
    };
}
} // namespace asst::blackflow
