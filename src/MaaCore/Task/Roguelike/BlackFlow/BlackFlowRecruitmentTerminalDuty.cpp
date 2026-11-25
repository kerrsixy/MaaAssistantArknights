#include "Task/Roguelike/BlackFlow/BlackFlowRecruitmentTerminalDuty.h"

#include <algorithm>
#include <climits>
#include <functional>
#include <map>

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Miscellaneous/TilePack.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"

namespace asst::blackflow::terminal_duty_detail
{
struct Body
{
    std::string name;
    std::string id;
    int elite = -1;
    int level = -1;
    int skill = -1;
    int rank = -1;
    std::string source;
};

struct Alternative
{
    battle::roguelike::DeployInfoWithRank plan;
    std::vector<std::string> groups;
};

struct Slot
{
    Point location;
    std::vector<Alternative> alternatives;
    int first_rank = INT_MAX;
    bool healing = false;
    bool blocking = false;
    bool attack = false;
};

struct Model
{
    std::vector<Slot> slots;
    std::vector<Body> bodies;
    std::vector<std::vector<bool>> edges;
    std::size_t core_body = 0;
    std::size_t core_slot = 0;
    std::size_t target = 0;
    std::string template_terms;
    std::string roster_terms;
    std::string reason;
    bool valid = false;
};

json::array points(const std::vector<Point>& values)
{
    json::array out;
    for (const auto p : values) {
        out.emplace_back(json::array { p.x, p.y });
    }
    return out;
}

json::array strings(const std::vector<std::string>& values)
{
    json::array out;
    for (const auto& value : values) {
        out.emplace_back(value);
    }
    return out;
}

bool member(const std::string& name, const std::vector<std::string>& wanted)
{
    const auto& groups = RoguelikeRecruit.get_group_names("BlackFlow");
    const auto ids = RoguelikeRecruit.get_group_ids_of_oper("BlackFlow", name);
    return std::ranges::any_of(ids, [&](int i) {
        return i >= 0 && static_cast<std::size_t>(i) < groups.size() &&
               std::ranges::find(wanted, groups[i]) != wanted.end();
    });
}

Body roster_body(const std::string& name, const RoguelikeOper& oper, const TerminalDutyContext& context)
{
    Body out { .name = name, .id = RoguelikeTraining.unique_identity(name), .elite = oper.elite, .level = oper.level };
    const auto& configured = RoguelikeRecruit.get_oper_info("BlackFlow", name);
    std::array<SkillAvailability, 3> availability;
    for (int slot = 1; slot <= 3; ++slot) {
        availability[slot - 1] = RoguelikeTraining.skill_availability(name, slot, oper.elite, oper.level);
    }
    out.skill = resolve_recruitment_skill_plan(configured.skill, configured.alternate_skill, availability).selected;
    const auto& prep = oper.preparation;
    if (out.skill > 0 && prep.operator_id == out.id && prep.training_sequence > 0 &&
        prep.selection_sequence >= prep.training_sequence && prep.selected_skill == out.skill) {
        out.rank = normalize_displayed_skill_level_for_run(
                       name,
                       out.skill,
                       oper.elite,
                       oper.level,
                       prep.displayed_skill_levels[out.skill - 1])
                       .value_or(-1);
        if (out.rank > 0) {
            out.source = "current_phase_normalized_preparation_not_account_max";
        }
    }
    const auto origin = context.origins
                            ? context.origins->find(context.client, context.uid, context.run_id, out.id, oper)
                            : std::nullopt;
    if (out.rank < 1 && out.skill > 0 && origin && origin->ordinary && context.account &&
        context.account->client() == context.client && context.account->uid() == context.uid) {
        out.rank = context.account->skill_level_for_run(name, out.skill, oper.elite, oper.level).value_or(-1);
        if (out.rank > 0) {
            out.source = "confirmed_ordinary_uid_training_capped_to_current_phase";
        }
    }
    return out;
}

Body offer_body(const RecruitmentDutyOffer& offer)
{
    return Body { .name = offer.name,
                  .id = offer.id,
                  .elite = offer.elite,
                  .level = offer.level,
                  .skill = offer.planned_skill,
                  .rank = offer.effective_rank,
                  .source = offer.training_binding };
}

std::optional<bool> admits(
    const Body& body,
    const Alternative& alternative,
    const battle::roguelike::CombatData& stage,
    const TerminalDutyContext& context,
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const TilePack::result_type& geometry)
{
    const auto& plan = alternative.plan;
    if (RoguelikeRecruit.is_oper_excluded("BlackFlow", body.name) || !member(body.name, alternative.groups) ||
        (!plan.operator_names.empty() &&
         std::ranges::find(plan.operator_names, body.name) == plan.operator_names.end()) ||
        std::ranges::find(plan.excluded_operator_names, body.name) != plan.excluded_operator_names.end() ||
        (plan.exclude_starting_core && body.name == context.core)) {
        return false;
    }
    if (body.id.empty() || body.elite < 0 || body.elite > 2 || body.level <= 0) {
        return std::nullopt;
    }
    if (body.elite < plan.minimum_elite) {
        return false;
    }
    const auto props = BattleData.find_first_oper(battle::Role::Unknown, body.name);
    const auto where = geometry.normal_tile_info.find(plan.location);
    if (!props || props->id != body.id || where == geometry.normal_tile_info.end() ||
        stage.blacklist_location.contains(plan.location)) {
        return std::nullopt;
    }
    const auto type = where->second.buildable;
    const bool all_position = props->location_type == battle::LocationType::All &&
                              (type == battle::LocationType::Melee || type == battle::LocationType::Ranged);
    if ((props->location_type != type && type != battle::LocationType::All && !all_position) ||
        where->second.key == TilePack::TileKey::Home || where->second.key == TilePack::TileKey::DeepSea) {
        return false;
    }
    const auto body_duties = catalog.body(body.name, body.elite, body.level);
    if (plan.require_persistent_blocking && body_duties[0].lower < 1) {
        return body_duties[0].upper > 0 ? std::nullopt : std::optional<bool>(false);
    }
    if (!plan.required_range_tiles.empty() && body_duties[1].lower < 1) {
        return body_duties[1].upper > 0 ? std::nullopt : std::optional<bool>(false);
    }
    if (plan.minimum_base_block_count > 0) {
        const auto block = RoguelikeTraining.base_block_count(body.name, body.elite, body.level);
        if (!block) {
            return std::nullopt;
        }
        if (*block < plan.minimum_base_block_count) {
            return false;
        }
    }
    if (!plan.required_attack_tiles.empty() &&
        (props->sub_role == battle::SubRole::Support_Bard ||
         (props->role == battle::Role::Medic && props->sub_role != battle::SubRole::Medic_IncantationMedic))) {
        return false;
    }
    auto range = BattleData.get_range(props->role, body.name, body.elite);
    if (range == BattleDataConfig::EmptyRange) {
        return std::nullopt;
    }
    auto direction = plan.direction;
    if (const auto force = stage.force_deploy_direction.find(plan.location);
        force != stage.force_deploy_direction.end() && force->second.applies_to(props->role, body.name)) {
        direction = force->second.direction;
    }
    if (direction == battle::DeployDirection::None) {
        return std::nullopt;
    }
    for (const auto d : { battle::DeployDirection::Right,
                          battle::DeployDirection::Up,
                          battle::DeployDirection::Left,
                          battle::DeployDirection::Down }) {
        if (d == direction) {
            break;
        }
        for (auto& p : range) {
            p = Point(p.y, -p.x);
        }
    }
    const auto covers = [&](const auto& targets) {
        return std::ranges::all_of(targets, [&](Point p) {
            return std::ranges::find(range, p - plan.location) != range.end();
        });
    };
    if (!covers(plan.required_attack_tiles) || !covers(plan.required_range_tiles)) {
        return false;
    }
    const auto normalized =
        normalize_displayed_skill_level_for_run(body.name, body.skill, body.elite, body.level, body.rank);
    if (body.skill < 1 || body.skill > 3 || body.rank < 1 ||
        RoguelikeTraining.skill_availability(body.name, body.skill, body.elite, body.level) !=
            SkillAvailability::Available ||
        !normalized || *normalized != body.rank || catalog.skill(body.id, body.skill, body.rank).is_null()) {
        return std::nullopt;
    }
    return true;
}

std::optional<std::vector<int>>
    assignment(const Model& model, const std::vector<std::vector<bool>>& edges, bool skip_target)
{
    std::vector<int> owners(model.slots.size(), -1);
    owners[model.core_slot] = static_cast<int>(model.core_body);
    std::function<bool(std::size_t, std::vector<bool>&)> visit = [&](std::size_t body, std::vector<bool>& seen) {
        for (std::size_t slot = 0; slot < owners.size(); ++slot) {
            if (slot == model.core_slot || (skip_target && slot == model.target) || seen[slot] || !edges[body][slot]) {
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
        if (body == model.core_body) {
            continue;
        }
        std::vector<bool> seen(owners.size(), false);
        visit(body, seen);
    }
    for (std::size_t slot = 0; slot < owners.size(); ++slot) {
        if (!(skip_target && slot == model.target) && owners[slot] < 0) {
            return std::nullopt;
        }
    }
    return owners;
}

Model model(
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const std::unordered_map<std::string, RoguelikeOper>& roster,
    const TerminalDutyContext& context)
{
    Model out;
    out.reason = "terminal_scope_or_identity_unknown_keep_legacy";
    if (context.strategy != "ending1" || context.fixed_opening || context.client.empty() || context.uid.empty() ||
        context.run_id.empty() || context.core.empty() || context.hope < 0 || !catalog.valid() ||
        (context.account && (context.account->client() != context.client || context.account->uid() != context.uid))) {
        return out;
    }
    const auto stage = RoguelikeCopilot.get_stage_data(context.stage_name);
    if (!stage || !RoguelikeCopilot.get_stage_variant_keys(context.stage_name).empty() || stage->force_ban_medic ||
        !stage->force_deploy_direction.empty() || !stage->blacklist_location.empty()) {
        out.reason = "terminal_template_missing_or_variant_context_unknown_keep_legacy";
        return out;
    }
    const auto geometry = TilePack::calc(context.stage_name);
    if (geometry.normal_tile_info.empty()) {
        out.reason = "terminal_static_geometry_missing_keep_legacy";
        return out;
    }
    std::map<int, Alternative> alternatives;
    for (const auto& [group, plans] : stage->deploy_plan) {
        for (const auto& plan : plans) {
            if (plan.kill_lower_bound != 0 || plan.kill_upper_bound != 9999 || plan.max_deployments != INT_MAX ||
                !plan.exclusive_locations.empty() ||
                RoguelikeCopilot.maximum_deploy_cost(context.stage_name, plan.plan_index)) {
                out.reason = "terminal_conditional_or_DP_cap_not_proven_during_recruitment_keep_legacy";
                return out;
            }
            const auto [it, inserted] = alternatives.try_emplace(plan.plan_index, Alternative { .plan = plan });
            static_cast<void>(inserted);
            it->second.plan.rank = std::min(it->second.plan.rank, plan.rank);
            it->second.groups.push_back(group);
        }
    }
    json::array terms;
    for (auto& [index, alt] : alternatives) {
        std::ranges::sort(alt.groups);
        const auto& p = alt.plan;
        terms.emplace_back(
            json::object { { "index", index },
                           { "groups", strings(alt.groups) },
                           { "location", json::array { p.location.x, p.location.y } },
                           { "direction", static_cast<int>(p.direction) },
                           { "minelite", p.minimum_elite },
                           { "names", strings(p.operator_names) },
                           { "excluded", strings(p.excluded_operator_names) },
                           { "exclude_core", p.exclude_starting_core },
                           { "attack", points(p.required_attack_tiles) },
                           { "heal", points(p.required_range_tiles) },
                           { "persistent", p.require_persistent_blocking },
                           { "minblock", p.minimum_base_block_count },
                           { "rank", p.rank } });
        const auto existing = std::ranges::find_if(out.slots, [&](const auto& s) { return s.location == p.location; });
        const auto slot_index = static_cast<std::size_t>(existing - out.slots.begin());
        if (existing == out.slots.end()) {
            out.slots.push_back(Slot { .location = p.location });
        }
        auto& slot = out.slots[slot_index];
        slot.first_rank = std::min(slot.first_rank, p.rank);
        slot.healing = slot.healing || !p.required_range_tiles.empty();
        slot.blocking = slot.blocking || p.require_persistent_blocking || p.minimum_base_block_count > 0;
        slot.attack = slot.attack || !p.required_attack_tiles.empty();
        slot.alternatives.push_back(alt);
    }
    out.template_terms = json::value(terms).to_string();
    std::vector<std::string> names;
    for (const auto& [name, oper] : roster) {
        static_cast<void>(oper);
        names.push_back(name);
    }
    std::ranges::sort(names);
    json::array roster_terms;
    for (const auto& name : names) {
        const auto& oper = roster.at(name);
        const auto body = roster_body(name, oper, context);
        if (body.id.empty() || body.elite < 0 || body.level <= 0) {
            out.reason = "terminal_current_roster_identity_or_phase_unknown_keep_legacy";
            return out;
        }
        out.bodies.push_back(body);
        roster_terms.emplace_back(
            json::object { { "name", name },
                           { "id", body.id },
                           { "elite", body.elite },
                           { "level", body.level },
                           { "skill", body.skill },
                           { "rank", body.rank },
                           { "source", body.source },
                           { "training_sequence", oper.preparation.training_sequence },
                           { "selection_sequence", oper.preparation.selection_sequence } });
        std::vector<bool> row;
        for (const auto& slot : out.slots) {
            bool yes = false, unknown = false;
            for (const auto& alt : slot.alternatives) {
                const auto admitted = admits(body, alt, *stage, context, catalog, geometry);
                yes = yes || admitted.value_or(false);
                unknown = unknown || !admitted;
            }
            if (!yes && unknown) {
                out.reason = "terminal_relevant_current_roster_skill_or_geometry_unknown_keep_legacy:" + body.name +
                             ":" + slot.location.to_string();
                return out;
            }
            row.push_back(yes);
        }
        out.edges.push_back(std::move(row));
    }
    out.roster_terms = json::value(roster_terms).to_string();
    const auto core = std::ranges::find_if(out.bodies, [&](const auto& b) { return b.name == context.core; });
    if (core == out.bodies.end()) {
        return out;
    }
    out.core_body = static_cast<std::size_t>(core - out.bodies.begin());
    std::vector<std::size_t> core_slots;
    for (std::size_t i = 0; i < out.slots.size(); ++i) {
        if (out.edges[out.core_body][i]) {
            core_slots.push_back(i);
        }
    }
    if (core_slots.size() != 1 || !std::ranges::any_of(out.slots, &Slot::healing) ||
        !std::ranges::any_of(out.slots, &Slot::blocking)) {
        out.reason = "terminal_core_or_healer_or_blocker_contract_unproven_keep_legacy";
        return out;
    }
    out.core_slot = core_slots[0];
    std::vector<std::size_t> supplemental;
    for (std::size_t i = 0; i < out.slots.size(); ++i) {
        const auto& slot = out.slots[i];
        if (i != out.core_slot && slot.attack && !slot.healing && !slot.blocking &&
            slot.first_rank > out.slots[out.core_slot].first_rank) {
            supplemental.push_back(i);
        }
    }
    if (supplemental.size() != 1) {
        out.reason = "terminal_post_core_output_contract_not_unique_keep_legacy";
        return out;
    }
    out.target = supplemental[0];
    // A larger target count is meaningful here only for nested alternatives
    // of this same physical duty, never for unrelated coverage sets.
    const auto& alternatives_for_target = out.slots[out.target].alternatives;
    for (const auto& a : alternatives_for_target) {
        for (const auto& b : alternatives_for_target) {
            const auto subset = [](const auto& left, const auto& right) {
                return std::ranges::all_of(left, [&](Point p) { return std::ranges::find(right, p) != right.end(); });
            };
            if (!subset(a.plan.required_attack_tiles, b.plan.required_attack_tiles) &&
                !subset(b.plan.required_attack_tiles, a.plan.required_attack_tiles)) {
                out.reason = "terminal_incomparable_same_site_output_requirements_keep_legacy";
                return out;
            }
        }
    }
    if (!assignment(out, out.edges, true)) {
        out.reason = "terminal_core_heal_block_or_prelude_still_missing_keep_legacy";
        return out;
    }
    if (assignment(out, out.edges, false)) {
        out.reason = "terminal_independent_output_already_covered_keep_legacy";
        return out;
    }
    out.valid = true;
    return out;
}

int acquired_body_coverage(
    const Model& m,
    const Body& body,
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const std::unordered_map<std::string, RoguelikeOper>& roster,
    const TerminalDutyContext& context)
{
    const auto stage = RoguelikeCopilot.get_stage_data(context.stage_name);
    if (!stage) {
        return false;
    }
    const auto geometry = TilePack::calc(context.stage_name);
    std::vector<bool> row;
    for (const auto& slot : m.slots) {
        row.push_back(std::ranges::any_of(slot.alternatives, [&](const auto& alt) {
            return admits(body, alt, *stage, context, catalog, geometry).value_or(false);
        }));
    }
    auto edges = m.edges;
    auto after_bodies = m.bodies;
    const auto prior = roster.find(body.name);
    if (prior != roster.end()) {
        const auto held = std::ranges::find_if(m.bodies, [&](const auto& b) { return b.name == body.name; });
        if (held == m.bodies.end()) {
            return false;
        }
        const auto index = static_cast<std::size_t>(held - m.bodies.begin());
        edges[index] = row;
        after_bodies[index] = body;
    }
    else {
        edges.push_back(std::move(row));
        after_bodies.push_back(body);
    }
    if (!edges[m.core_body][m.core_slot]) {
        return 0;
    }
    const auto filled = assignment(m, edges, false);
    if (!filled) {
        return 0;
    }
    const auto& target_body = after_bodies[static_cast<std::size_t>((*filled)[m.target])];
    int required_coverage = 0;
    for (const auto& alternative : m.slots[m.target].alternatives) {
        if (admits(target_body, alternative, *stage, context, catalog, geometry).value_or(false)) {
            required_coverage =
                std::max(required_coverage, static_cast<int>(alternative.plan.required_attack_tiles.size()));
        }
    }
    return required_coverage;
}

int completes(
    const Model& m,
    const RecruitmentDutyOffer& offer,
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const std::unordered_map<std::string, RoguelikeOper>& roster,
    const TerminalDutyContext& context)
{
    if (!offer.feasible || offer.client != context.client || offer.uid != context.uid ||
        offer.run_id != context.run_id || !offer.hope_cost || *offer.hope_cost < 0 || *offer.hope_cost > context.hope ||
        offer.planned_skill < 1 || offer.effective_rank < 1 ||
        offer.skill_availability != SkillAvailability::Available || !offer.ordinary_origin ||
        offer.positive_temporary || offer.observed_temporary == true || !offer.account_training_used ||
        !context.account || context.account->client() != context.client || context.account->uid() != context.uid ||
        context.account->skill_level_for_run(offer.name, offer.planned_skill, offer.elite, offer.level) !=
            std::optional<int>(offer.effective_rank) ||
        offer.id.empty() || offer.id != RoguelikeTraining.unique_identity(offer.name) ||
        (offer.operation != "招募" && offer.operation != "进阶")) {
        return false;
    }
    const auto prior = roster.find(offer.name);
    if ((offer.operation == "进阶" && (prior == roster.end() || !offer.replacement_valid)) ||
        (offer.operation == "招募" && prior != roster.end())) {
        return false;
    }
    if (offer.operation == "招募" &&
        (offer.training_binding != "source_conditioned_non_temporary" || *offer.hope_cost <= 0 ||
         offer.temporary_rule_source.empty() || offer.temporary_rule_source != catalog.temporary_rule_source())) {
        return false;
    }
    if (offer.operation == "进阶") {
        const auto origin =
            context.origins
                ? context.origins->find(context.client, context.uid, context.run_id, offer.id, prior->second)
                : std::nullopt;
        if (!origin || !origin->ordinary || origin->receipt_id != offer.inherited_origin_receipt) {
            return false;
        }
    }
    if (offer.operation == "招募" && std::ranges::any_of(m.bodies, [&](const auto& b) { return b.id == offer.id; })) {
        return false;
    }
    return acquired_body_coverage(m, offer_body(offer), catalog, roster, context);
}
}

namespace asst::blackflow
{
TerminalOutputHopeForecast forecast_terminal_output_hope(
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const std::unordered_map<std::string, RoguelikeOper>& roster,
    const TerminalDutyContext& context,
    const RecruitmentQuoteScope& scope,
    const std::vector<RecruitmentObservedQuote>& quotes)
{
    TerminalOutputHopeForecast result;
    const auto model = terminal_duty_detail::model(catalog, roster, context);
    result.reason = model.reason;
    if (!model.valid || !scope.valid() || scope.client != context.client || scope.uid != context.uid ||
        !context.account || context.account->client() != context.client || context.account->uid() != context.uid) {
        return result;
    }
    result.template_terms = model.template_terms;
    result.roster_terms = model.roster_terms;
    json::array applicable;
    std::size_t rejected = 0;
    for (const auto& q : quotes) {
        const auto& observed = q.offer;
        if (q.non_economic_eligible != std::optional<bool>(true) || !q.scope.valid() ||
            q.scope.client != scope.client || q.scope.uid != scope.uid || q.scope.package != scope.package ||
            q.scope.squad != scope.squad || q.scope.source_cohort != scope.source_cohort ||
            observed.operation != "招募" || !observed.hope_cost || *observed.hope_cost <= 0 || !observed.price_index ||
            !observed.ordinary_origin || observed.positive_temporary || observed.observed_temporary == true ||
            observed.training_binding != "source_conditioned_non_temporary" ||
            observed.temporary_rule_source != catalog.temporary_rule_source() || q.frame_path.empty() ||
            q.observation_id.empty() || observed.id != RoguelikeTraining.unique_identity(observed.name) ||
            observed.elite < 0 || observed.elite > 2 || observed.level <= 0 || roster.contains(observed.name)) {
            ++rejected;
            continue;
        }
        const auto quote_price_context = json::parse(q.scope.price_context);
        const auto current_price_context = json::parse(scope.price_context);
        if (!quote_price_context || !current_price_context ||
            quote_price_context->at("observed_difficulty").as_integer() !=
                current_price_context->at("observed_difficulty").as_integer() ||
            (q.scope.price_context_complete && scope.price_context_complete &&
             quote_price_context->to_string() != current_price_context->to_string())) {
            ++rejected;
            continue;
        }
        // Prefer directly observed prices in the current context over cheaper
        // quotes from a different partial modifier context. Eligibility of that
        // newest quote may remain unknown; never resurrect an older cheap price.
        const bool current_context_observed = std::ranges::any_of(quotes, [&](const auto& other) {
            return other.scope.valid() && other.scope.client == scope.client && other.scope.uid == scope.uid &&
                   other.scope.package == scope.package && other.scope.squad == scope.squad &&
                   other.scope.source_cohort == scope.source_cohort &&
                   other.scope.price_context == scope.price_context && other.offer.id == observed.id &&
                   other.offer.elite == observed.elite && other.offer.level == observed.level &&
                   other.offer.operation == "招募" && other.offer.hope_cost && *other.offer.hope_cost > 0 &&
                   other.offer.ordinary_origin && !other.offer.positive_temporary &&
                   other.offer.observed_temporary != true &&
                   other.offer.training_binding == "source_conditioned_non_temporary" &&
                   other.offer.temporary_rule_source == catalog.temporary_rule_source();
        });
        if (current_context_observed && q.scope.price_context != scope.price_context) {
            ++rejected;
            continue;
        }
        const auto& configured = RoguelikeRecruit.get_oper_info("BlackFlow", observed.name);
        if (configured.account_module) {
            const auto module = context.account->module_evidence(observed.name, configured.account_module->module_id);
            if (!module || !module->verified) {
                ++rejected;
                continue;
            }
        }

        std::array<SkillAvailability, 3> availability;
        for (int i = 1; i <= 3; ++i) {
            availability[i - 1] =
                RoguelikeTraining.skill_availability(observed.name, i, observed.elite, observed.level);
        }
        const auto planned = resolve_recruitment_skill_plan(configured.skill, configured.alternate_skill, availability);
        const auto rank =
            planned.selected > 0
                ? context.account->skill_level_for_run(observed.name, planned.selected, observed.elite, observed.level)
                : std::nullopt;
        if (!rank) {
            ++rejected;
            continue;
        }
        const terminal_duty_detail::Body projected {
            .name = observed.name,
            .id = observed.id,
            .elite = observed.elite,
            .level = observed.level,
            .skill = planned.selected,
            .rank = *rank,
            .source = "UID_training_at_historically_observed_offer_phase_not_current_offer"
        };
        if (terminal_duty_detail::acquired_body_coverage(model, projected, catalog, roster, context) <= 0) {
            ++rejected;
            continue;
        }
        if (!result.expected_hope || *observed.hope_cost < *result.expected_hope) {
            result.expected_hope = *observed.hope_cost;
        }
        applicable.emplace_back(
            json::object { { "observed_quote", recruitment_quote_to_json(q) },
                           { "projected_planned_skill", planned.selected },
                           { "projected_effective_rank", *rank },
                           { "same_observed_partial_price_context", q.scope.price_context == scope.price_context },
                           { "actual_future_offer", false } });
    }
    result.reason = result.expected_hope ? "receipt_conditioned_expected_cost_for_missing_loaded_output"
                                         : "no_qualified_native_observed_future_pool_keep_legacy";
    result.evidence = json::object {
        { "objective", "expected_acquisition_budget_not_global_combat_dominance" },
        { "expected_hope", result.expected_hope ? json::value(*result.expected_hope) : json::value(nullptr) },
        { "native_quote_pool", applicable },
        { "rejected_quote_count", rejected },
        { "current_price_context_complete", scope.price_context_complete },
        { "price_or_future_card_guaranteed", false },
        { "partial_or_changed_modifiers_unresolved", true },
        { "missing_site", json::array { model.slots[model.target].location.x, model.slots[model.target].location.y } },
        { "optional_current_support_value_zero", false },
        { "DPS_HPS_or_survival_proven", false }
    };
    return result;
}

TerminalHopeReservationDecision choose_terminal_hope_reservation(
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const std::unordered_map<std::string, RoguelikeOper>& roster,
    const TerminalDutyContext& context,
    const RecruitmentQuoteScope& scope,
    const std::vector<RecruitmentObservedQuote>& quotes,
    const std::vector<RecruitmentDutyOffer>& current_offers,
    const std::string& selected_name,
    int selected_page,
    const VoucherStashObservation& stash)
{
    TerminalHopeReservationDecision out;
    out.forecast = forecast_terminal_output_hope(catalog, roster, context, scope, quotes);
    out.reason = out.forecast.reason;
    if (!out.forecast.expected_hope || context.hope < 0) {
        return out;
    }
    const auto same = [&](const auto& o) {
        return o.name == selected_name && o.page_index == selected_page;
    };
    const auto found = std::ranges::find_if(current_offers, same);
    if (found == current_offers.end() || std::ranges::count_if(current_offers, same) != 1 || !found->feasible ||
        found->client != context.client || found->uid != context.uid || found->run_id != context.run_id ||
        !found->hope_cost || *found->hope_cost < 0 || *found->hope_cost > context.hope) {
        out.reason = "current_selected_offer_or_budget_unknown_keep_legacy";
        return out;
    }
    const auto model = terminal_duty_detail::model(catalog, roster, context);
    if (!model.valid || model.template_terms != out.forecast.template_terms ||
        model.roster_terms != out.forecast.roster_terms) {
        out.reason = "current_roster_or_template_changed_keep_legacy";
        return out;
    }
    if (std::ranges::any_of(found->marginal, [](const auto& b) { return b.lower > 0; }) ||
        terminal_duty_detail::completes(model, *found, catalog, roster, context) > 0) {
        out.reason = "current_required_gain_precedes_expected_hope_reserve";
        return out;
    }
    if (*found->hope_cost == 0 || context.hope - *found->hope_cost >= *out.forecast.expected_hope) {
        out.reason = "free_choice_or_expected_budget_preserved";
        return out;
    }
    if (!stash.can_store()) {
        out.needs_stash_evidence = true;
        out.reason = "expected_budget_advisory_no_verified_legal_stash_keep_legacy";
        return out;
    }
    out.request_stash = true;
    out.reason = "optional_spend_crosses_expected_output_budget_request_verified_stash";
    return out;
}

TerminalDutyDecision choose_terminal_output_duty(
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const std::unordered_map<std::string, RoguelikeOper>& roster,
    const TerminalDutyContext& context,
    const std::vector<RecruitmentDutyOffer>& offers,
    const std::vector<TerminalDutyEligible>& eligible,
    const std::string& legacy_name,
    int legacy_page)
{
    using namespace terminal_duty_detail;
    TerminalDutyDecision result;
    const auto m = model(catalog, roster, context);
    result.reason = m.reason;
    if (!m.valid) {
        return result;
    }
    const auto legacy_match = [&](const auto& o) {
        return o.name == legacy_name && o.page_index == legacy_page;
    };
    const auto legacy = std::ranges::find_if(offers, legacy_match);
    if (legacy == offers.end() || std::ranges::count_if(offers, legacy_match) != 1 || !legacy->hope_cost ||
        *legacy->hope_cost > context.hope || completes(m, *legacy, catalog, roster, context)) {
        result.reason = "terminal_legacy_missing_or_already_adds_required_contract_keep_legacy";
        return result;
    }
    std::optional<std::size_t> best;
    int best_score = INT_MIN;
    int best_coverage = 0;
    for (std::size_t i = 0; i < offers.size(); ++i) {
        const auto& offer = offers[i];
        const auto key = [&](const auto& e) {
            return e.name == offer.name && e.page == offer.page_index;
        };
        const auto found = std::ranges::find_if(eligible, key);
        if (found == eligible.end() || std::ranges::count_if(eligible, key) != 1 ||
            std::ranges::count_if(offers, [&](const auto& o) {
                return o.name == offer.name && o.page_index == offer.page_index;
            }) != 1) {
            continue;
        }
        const auto coverage = completes(m, offer, catalog, roster, context);
        if (coverage <= 0) {
            continue;
        }
        if (!best || coverage > best_coverage || (coverage == best_coverage && found->existing_score > best_score)) {
            best = i;
            best_score = found->existing_score;
            best_coverage = coverage;
        }
    }
    if (!best) {
        result.reason = "terminal_no_affordable_proven_independent_output_candidate_keep_legacy";
        return result;
    }
    result.selected_offer = best;
    result.reason = "ending1_complete_missing_independent_output_before_covered_role_promotion";
    result.intent = TerminalDutyIntent { .chosen = offers[*best],
                                         .legacy = *legacy,
                                         .client = context.client,
                                         .uid = context.uid,
                                         .run_id = context.run_id,
                                         .stage_name = context.stage_name,
                                         .template_terms = m.template_terms,
                                         .roster_terms = m.roster_terms };
    result.evidence = json::object { { "scope", "configured_ending1_static_contract_projection_not_DPS_or_survival" },
                                     { "stage", context.stage_name },
                                     { "merged_slots", m.slots.size() },
                                     { "existing_independent_assignments", m.slots.size() - 1 },
                                     { "selected_required_output_tiles", best_coverage },
                                     { "missing_output_site",
                                       json::array { m.slots[m.target].location.x, m.slots[m.target].location.y } },
                                     { "legacy", legacy_name },
                                     { "selected", offers[*best].name },
                                     { "hope", context.hope },
                                     { "legacy_hope_cost", *legacy->hope_cost },
                                     { "selected_hope_cost", *offers[*best].hope_cost },
                                     { "hope_cost_delta", *offers[*best].hope_cost - *legacy->hope_cost },
                                     { "hope_remaining", context.hope - *offers[*best].hope_cost },
                                     { "tie_break", "existing_eligible_priority_only_after_strict_new_contract_gain" },
                                     { "battle_DP_and_HPS_verified", false },
                                     { "future_enemy_DEF_observed", false } };
    return result;
}

bool terminal_output_refind_matches(
    const TerminalDutyIntent& intended,
    const RecruitmentDutyOffer& actual,
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const std::unordered_map<std::string, RoguelikeOper>& roster,
    const TerminalDutyContext& context,
    std::string* reason)
{
    const auto fail = [&](const std::string& why) {
        if (reason) {
            *reason = why;
        }
        return false;
    };
    if (context.client != intended.client || context.uid != intended.uid || context.run_id != intended.run_id ||
        context.stage_name != intended.stage_name || !recruitment_duty_refind_matches(intended.chosen, actual)) {
        return fail("terminal_refind_identity_run_or_exact_offer_changed");
    }
    const auto current = terminal_duty_detail::model(catalog, roster, context);
    if (!current.valid || current.template_terms != intended.template_terms ||
        current.roster_terms != intended.roster_terms) {
        return fail("terminal_refind_template_roster_or_remaining_contract_changed");
    }
    const std::vector<RecruitmentDutyOffer> actual_set { intended.legacy, actual };
    const std::vector<TerminalDutyEligible> keys { { actual.name, actual.page_index, 1 } };
    const auto repeated = choose_terminal_output_duty(
        catalog,
        roster,
        context,
        actual_set,
        keys,
        intended.legacy.name,
        intended.legacy.page_index);
    if (!repeated.selected_offer || *repeated.selected_offer != 1) {
        return fail("terminal_refind_current_budget_or_gain_no_longer_proven");
    }
    if (reason) {
        *reason = "terminal_refind_same_scope_current_contract_gain_recomputed";
    }
    return true;
}
}
