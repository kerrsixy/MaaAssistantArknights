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
bool BlackFlowRecruitmentDutyCatalog::load(
    const std::filesystem::path& f,
    const std::filesystem::path& t,
    const std::filesystem::path& b)
{
    m_valid = false;
    m_operators = json::object {};
    m_temporary_rule = nullptr;
    m_revision.clear();
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
    std::vector<RecruitmentDutyOffer> result;
    for (const auto& candidate : analyzer.get_candidate_evidence()) {
        RecruitmentDutyOffer offer;
        offer.client = context.client;
        offer.uid = context.uid;
        offer.analysis_id = candidate.analysis_id;
        offer.observation_index = candidate.observation_index;
        offer.page_index = context.page_index;
        offer.name = candidate.name;
        offer.id = RoguelikeTraining.unique_identity(candidate.name);
        offer.elite = candidate.elite;
        offer.level = candidate.level;
        offer.observed_at = context.observed_at;
        offer.observed_temporary = candidate.temporary;
        offer.planned_skill = RoguelikeRecruit.get_oper_info("BlackFlow", offer.name).skill;
        offer.skill_availability =
            RoguelikeTraining.skill_availability(offer.name, offer.planned_skill, offer.elite, offer.level);
        const auto linked = analyzer.resolve_candidate_hope_cost(candidate);
        if (linked) {
            offer.operation = linked->get().operation;
            offer.hope_cost = linked->get().hope_cost;
        }
        const bool positive_temporary = candidate.temporary == true || candidate.legacy_temporary_flag == true;
        offer.temporary_rule_source = catalog.temporary_rule_source();
        const bool source_non_temporary = context.theme == "BlackFlow" && context.mode == 0 && linked &&
                                          !offer.temporary_rule_source.empty() && offer.operation == "招募" &&
                                          offer.hope_cost && *offer.hope_cost > 0 && !positive_temporary;
        offer.ordinary_origin = !positive_temporary && (candidate.temporary == false || source_non_temporary);
        offer.training_binding = positive_temporary             ? "observed_temporary_no_account_training"
                                 : source_non_temporary         ? "source_conditioned_non_temporary"
                                 : candidate.temporary == false ? "observed_non_temporary"
                                                                : "unknown_origin";
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
    return result;
}

bool recruitment_duty_refind_matches(const RecruitmentDutyOffer& intended, const RecruitmentDutyOffer& actual)
{
    return actual.feasible && intended.client == actual.client && !intended.uid.empty() && intended.uid == actual.uid &&
           !intended.id.empty() && intended.id == actual.id && intended.name == actual.name &&
           intended.elite == actual.elite && intended.level == actual.level &&
           intended.planned_skill == actual.planned_skill && intended.hope_cost == actual.hope_cost &&
           intended.operation == actual.operation && intended.training_binding == actual.training_binding &&
           intended.effective_rank == actual.effective_rank;
}

json::value recruitment_duty_to_json(const RecruitmentDutyOffer& x)
{
    json::array capability, marginal, reasons;
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
        { "analysis_id", x.analysis_id },
        { "observation_index", x.observation_index },
        { "page_index", x.page_index },
        { "name", x.name },
        { "operator_id", x.id },
        { "elite", x.elite },
        { "level", x.level },
        { "planned_skill", x.planned_skill },
        { "actual_selected_skill", nullptr },
        { "effective_rank", x.effective_rank },
        { "skill_availability", static_cast<int>(x.skill_availability) },
        { "skill_facts", x.skill_facts },
        { "intrinsic_healing_component", x.healing_component },
        { "hope_cost", x.hope_cost ? json::value(*x.hope_cost) : json::value(nullptr) },
        { "operation", x.operation },
        { "observed_temporary", x.observed_temporary ? json::value(*x.observed_temporary) : json::value(nullptr) },
        { "training_binding", x.training_binding },
        { "account_training_used", x.account_training_used },
        { "non_temporary_rule_source_sha256", x.temporary_rule_source },
        { "non_temporary_rule_scope", "Topic6/mode0/recruit/positive_exact_price/no_positive_temporary_conflict" },
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
}
