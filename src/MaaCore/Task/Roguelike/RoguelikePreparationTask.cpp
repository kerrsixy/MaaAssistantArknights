#include "RoguelikePreparationTask.h"

#include "BlackFlow/BlackFlowSession.h"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/BlackFlow/BlackFlowAccountTraining.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <unordered_set>

#include "MaaUtils/ImageIo.h"
#include "MaaUtils/Time.hpp"
#include "RoguelikeOperEvidence.h"
#include "Utils/Logger.hpp"
#include "Utils/WorkingDir.hpp"

json::value asst::commit_preparation_observations(
    RoguelikeStatus& status,
    const cv::Mat& frame,
    const std::vector<PreparationOperatorObservation>& observations,
    PreparationPage page)
{
    const auto sequence = ++status.decision_seq;
    json::array records;
    for (const auto& observed : observations) {
        if (observed.name.empty() ||
            (!RoguelikeTraining.contains_name(observed.name) && !status.opers.contains(observed.name))) {
            continue;
        }
        if (!observed.anchor_verified && !status.opers.contains(observed.name)) {
            continue;
        }
        auto record = observed.to_json();
        const auto identity = RoguelikeTraining.unique_identity(observed.name);
        if (identity.empty() || (!observed.operator_id.empty() && observed.operator_id != identity)) {
            record["applied"] = false;
            record["reason"] = "operator_identity_unresolved";
            records.push_back(std::move(record));
            continue;
        }
        const auto existing = status.opers.find(observed.name);
        const auto selection_conflicts = [&](int elite, int level) {
            if (observed.selected_skill <= 0) {
                return false;
            }
            const auto available =
                RoguelikeTraining.skill_availability(observed.name, observed.selected_skill, elite, level);
            return available == SkillAvailability::Locked || available == SkillAvailability::Absent;
        };
        if (!observed.training_observed) {
            if (existing == status.opers.end() || existing->second.preparation.operator_id != identity) {
                record["applied"] = false;
                record["reason"] = "training_snapshot_required";
            }
            else {
                auto& prepared = existing->second.preparation;
                const bool conflict = selection_conflicts(existing->second.elite, existing->second.level);
                record["phase_vs_selected_skill_conflict"] = conflict;
                prepared.selected_skill = conflict ? -1 : observed.selected_skill;
                prepared.selection_sequence = static_cast<std::uint64_t>(sequence);
                record["applied"] = true;
                record["training_source_sequence"] = prepared.training_sequence;
                record["selection_source_sequence"] = prepared.selection_sequence;
            }
            records.push_back(std::move(record));
            continue;
        }
        const int merged_elite = observed.elite >= 0              ? observed.elite
                                 : existing != status.opers.end() ? existing->second.elite
                                                                  : -1;
        const auto cap = RoguelikeTraining.maximum_level(observed.name, merged_elite);
        const int level = cap && observed.level > 0 && observed.level <= *cap ? observed.level : -1;
        record["level_compatible_with_current_phase"] = level > 0;
        if (!merge_current_run_operator(status.opers, observed.name, observed.elite, level)) {
            continue;
        }
        auto& oper = status.opers.at(observed.name);
        const bool conflict = selection_conflicts(oper.elite, oper.level);
        record["phase_vs_selected_skill_conflict"] = conflict;
        // A new preparation view invalidates the previous selection/training
        // observation even when some of the new fields cannot be read.
        oper.preparation = {
            .operator_id = identity,
            .displayed_skill_levels = observed.displayed_skill_levels,
            .selected_skill = conflict ? -1 : observed.selected_skill,
            .training_sequence = static_cast<std::uint64_t>(sequence),
            .selection_sequence = page == PreparationPage::Skills ? static_cast<std::uint64_t>(sequence) : 0,
        };
        record["applied"] = true;
        record["current_run_elite"] = oper.elite;
        record["current_run_level"] = oper.level;
        record["training_source_sequence"] = oper.preparation.training_sequence;
        record["selection_source_sequence"] = oper.preparation.selection_sequence;
        records.push_back(std::move(record));
    }
    const auto directory = UserDir.get() / utils::path("debug/blackflow/preparation");
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const auto filename = MAA_NS::format_now_for_filename() + "_preparation_" + std::to_string(sequence) + ".png";
    const auto path = directory / utils::path(filename);
    const bool saved = !error && !frame.empty() && MAA_NS::imwrite(path, frame);
    if (!saved) {
        LogWarn << __FUNCTION__ << "Preparation frame evidence could not be persisted" << path;
    }
    return json::object {
        { "run_id", status.run_id },
        { "observation_sequence", sequence },
        { "page", page == PreparationPage::Skills ? "skill_selection" : "formation" },
        { "operators", std::move(records) },
        { "frame_saved", saved },
        { "frame_path", saved ? utils::path_to_utf8_string(path) : std::string {} },
        { "account_strength_used", false },
        { "gameplay_inputs", 0 },
    };
}

int asst::choose_preparation_skill(
    int preferred,
    int alternate,
    const PreparationOperatorObservation& observation,
    const RoguelikeOper* current_run)
{
    const auto selectable = [&](int skill) {
        if (skill <= 0 || skill > static_cast<int>(observation.availability.size())) {
            return false;
        }
        auto availability = observation.availability[skill - 1];
        if (availability == SkillAvailability::Unknown && current_run &&
            !current_run->preparation.operator_id.empty() &&
            current_run->preparation.operator_id == RoguelikeTraining.unique_identity(observation.name)) {
            availability =
                RoguelikeTraining.skill_availability(observation.name, skill, current_run->elite, current_run->level);
        }
        return availability != SkillAvailability::Locked && availability != SkillAvailability::Absent;
    };
    if (selectable(preferred)) {
        return preferred;
    }
    if (selectable(alternate)) {
        return alternate;
    }
    return 0;
}

namespace asst::blackflow::prepared_primary_detail
{
std::optional<std::reference_wrapper<const ConditionalPrimaryModeEstimate>>
    initial_mode(const ConditionalPrimaryEstimate& e)
{
    if (!e.qualified || e.modes.empty() || (e.modes[0].key != "normal" && e.modes[0].key != "ammo_main")) {
        return std::nullopt;
    }
    return std::cref(e.modes[0]);
}

bool rate_better(const ConditionalPrimaryEstimate& a, const ConditionalPrimaryEstimate& b)
{
    const auto am = initial_mode(a), bm = initial_mode(b);
    if (!am || !bm || am->get().variants.empty() || bm->get().variants.empty()) {
        return false;
    }
    bool strict = false;
    for (const auto& av : am->get().variants) {
        for (const auto& bv : bm->get().variants) {
            std::vector<double> points { 0 };
            double maximum = 0;
            for (const auto& sequence : { av.lower, bv.upper }) {
                for (const auto& hit : sequence.hits) {
                    if (!std::isfinite(hit.damage) || hit.damage <= 0) {
                        return false;
                    }
                    points.push_back(0.95 * hit.damage);
                    maximum = std::max(maximum, hit.damage);
                }
            }
            points.push_back(maximum + 1);
            for (double defense : points) {
                const auto ar = physical_sequence_rate(av.lower, defense);
                const auto br = physical_sequence_rate(bv.upper, defense);
                if (!ar || !br || *ar + 1e-9 < *br) {
                    return false;
                }
                strict = strict || *ar > *br + 1e-9;
            }
        }
    }
    return strict;
}
}

namespace asst::blackflow
{
PreparedComponentBatch build_prepared_primary_components(
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const RoguelikeStatus& status,
    const std::string& client,
    const std::string& uid,
    const std::vector<std::string>& selected_names)
{
    PreparedComponentBatch batch { .client = client, .uid = uid, .run_id = status.run_id };
    std::unordered_set<std::string> unique;
    for (const auto& name : selected_names) {
        const auto current = status.opers.find(name);
        if (name.empty() || !unique.emplace(name).second || current == status.opers.end()) {
            continue;
        }
        PreparedPrimaryEntry entry { .name = name, .observed = current->second };
        const auto& p = entry.observed.preparation;
        if (client.empty() || uid.empty() || status.run_id.empty() || p.operator_id.empty() ||
            p.operator_id != RoguelikeTraining.unique_identity(name) || p.training_sequence == 0 ||
            p.selection_sequence < p.training_sequence || p.selected_skill < 1 || p.selected_skill > 3) {
            entry.component.reason = "verified_preparation_identity_phase_or_selection_unavailable";
        }
        else {
            entry.component = catalog.conditional_primary(
                name,
                entry.observed.elite,
                entry.observed.level,
                p.selected_skill,
                p.displayed_skill_levels[p.selected_skill - 1],
                {},
                PrimarySequenceConditions { .target = ConditionalPrimaryTarget::Ground,
                                            .source = "preparation_ground_reference_not_live_enemy" },
                "verified_preparation:" + status.run_id + ":" + std::to_string(p.training_sequence) + ":" +
                    std::to_string(p.selection_sequence));
        }
        batch.entries.push_back(std::move(entry));
    }
    return batch;
}

bool prepared_primary_matches(
    const PreparedComponentBatch& batch,
    const RoguelikeStatus& status,
    const std::string& client,
    const std::string& uid)
{
    if (client.empty() || uid.empty() || status.run_id.empty() || batch.client != client || batch.uid != uid ||
        batch.run_id != status.run_id || batch.entries.empty()) {
        return false;
    }
    for (const auto& entry : batch.entries) {
        const auto current = status.opers.find(entry.name);
        if (current == status.opers.end()) {
            return false;
        }
        const auto& old = entry.observed;
        const auto& now = current->second;
        if (old.elite != now.elite || old.level != now.level ||
            old.preparation.operator_id != now.preparation.operator_id ||
            old.preparation.selected_skill != now.preparation.selected_skill ||
            old.preparation.displayed_skill_levels != now.preparation.displayed_skill_levels ||
            old.preparation.training_sequence != now.preparation.training_sequence ||
            old.preparation.selection_sequence != now.preparation.selection_sequence) {
            return false;
        }
    }
    return true;
}

PreparedPrimaryChoice choose_initial_primary_reference(
    const std::vector<ConditionalPrimaryEstimate>& components,
    const std::vector<std::size_t>& candidates,
    std::optional<std::size_t> legacy)
{
    PreparedPrimaryChoice result { .reason = "initial_ground_reference_incomparable_keep_legacy" };
    if (legacy && (*legacy >= components.size() || std::ranges::find(candidates, *legacy) == candidates.end())) {
        result.reason = "legacy_not_in_qualified_comparison";
        return result;
    }
    std::vector<std::size_t> frontier;
    for (const auto i : candidates) {
        if (i >= components.size() || !components[i].qualified ||
            (legacy && (i == *legacy || !prepared_primary_detail::rate_better(components[i], components[*legacy])))) {
            continue;
        }
        if (std::ranges::none_of(candidates, [&](const auto j) {
                return j < components.size() && j != i &&
                       prepared_primary_detail::rate_better(components[j], components[i]);
            })) {
            frontier.push_back(i);
        }
    }
    if (frontier.size() == 1) {
        result.preferred = frontier[0];
        result.reason = "initial_acquired_ground_main_rate_reference_not_total_DPS_or_survival";
    }
    else if (frontier.size() > 1) {
        result.reason = "multiple_incomparable_reference_preferences_keep_legacy";
    }
    return result;
}

PreparedPrimaryChoice choose_prepared_primary_reference(
    const PreparedComponentBatch& batch,
    const std::vector<std::size_t>& candidates,
    std::size_t legacy)
{
    std::vector<ConditionalPrimaryEstimate> components;
    for (const auto& entry : batch.entries) {
        components.push_back(entry.component);
    }
    return choose_initial_primary_reference(components, candidates, legacy);
}

json::value prepared_primary_to_json(const PreparedComponentBatch& batch)
{
    json::array entries;
    for (const auto& entry : batch.entries) {
        const auto& p = entry.observed.preparation;
        entries.emplace_back(
            json::object { { "name", entry.name },
                           { "operator_id", p.operator_id },
                           { "elite", entry.observed.elite },
                           { "level", entry.observed.level },
                           { "selected_skill", p.selected_skill },
                           { "training_sequence", p.training_sequence },
                           { "selection_sequence", p.selection_sequence },
                           { "displayed_levels", p.displayed_skill_levels },
                           { "component", conditional_primary_to_json(entry.component) } });
    }
    return json::object { { "client", batch.client },
                          { "uid", batch.uid },
                          { "run_id", batch.run_id },
                          { "entries", entries },
                          { "stage_context",
                            batch.stage ? preparation_stage_to_json(*batch.stage) : json::value(nullptr) },
                          { "primary_role_candidates", batch.primary_role_candidates },
                          { "account_mastery_substituted", false },
                          { "training_read_in_battle", false },
                          { "unknown_external_assumed_zero", false },
                          { "objective", "first_skill_mode_acquired_ground_main_rate_reference" } };
}
}

namespace asst::blackflow
{
std::optional<PreparationStageContext> current_preparation_stage(
    const BlackFlowSession& session,
    const std::string& client,
    const std::string& uid,
    const std::string& run_id)
{
    const auto transaction = session.transaction_snapshot();
    const auto& page = session.page_context();
    if (client.empty() || uid.empty() || run_id.empty() || !page || !transaction ||
        page->run_revision != session.run_revision() || page->transaction_id.empty() || page->decision_id.empty() ||
        (page->stage != PageExecutionStage::PendingDispatch && page->stage != PageExecutionStage::Running) ||
        (transaction->stage() != MoveTransactionStage::Committed &&
         transaction->stage() != MoveTransactionStage::PageResolved) ||
        !transaction->preview() || !is_combat_node_type(page->node_type)) {
        return std::nullopt;
    }
    const auto& preview = *transaction->preview();
    const auto& move = transaction->proposal();
    if (!preview.identity_same_frame || preview.identity_frame_key.empty() || preview.stage_title.empty() ||
        preview.identity_client != client || preview.identity_uid != uid || preview.identity_run_id != run_id ||
        !preview.identity_revealed || preview.reachability != PreviewReachability::Reachable ||
        (page->node != move.target && page->node != move.landing) || page->floor != session.run().floor ||
        transaction->map_revision() != session.viewport().map_revision() ||
        transaction->viewport_revision() != session.viewport().viewport_revision()) {
        return std::nullopt;
    }
    return PreparationStageContext { .client = client,
                                     .uid = uid,
                                     .run_id = run_id,
                                     .stage_name = preview.stage_title,
                                     .transaction_id = page->transaction_id,
                                     .action_id = move.action_id,
                                     .frame_key = preview.identity_frame_key,
                                     .frame_path = preview.identity_frame_path,
                                     .frame_saved = preview.identity_frame_saved,
                                     .run_revision = page->run_revision,
                                     .page_revision = page->page_revision,
                                     .map_revision = transaction->map_revision(),
                                     .viewport_revision = transaction->viewport_revision(),
                                     .node = page->node };
}

json::value preparation_stage_to_json(const PreparationStageContext& c)
{
    return json::object { { "client", c.client },
                          { "uid", c.uid },
                          { "run_id", c.run_id },
                          { "stage", c.stage_name },
                          { "transaction_id", c.transaction_id },
                          { "action_id", c.action_id },
                          { "source_frame_key", c.frame_key },
                          { "source_frame_path", c.frame_path },
                          { "source_frame_saved", c.frame_saved },
                          { "run_revision", c.run_revision },
                          { "page_revision", c.page_revision },
                          { "map_revision", c.map_revision },
                          { "viewport_revision", c.viewport_revision },
                          { "node", c.node },
                          { "source", "same_frame_move_preview_in_current_resolved_transaction" } };
}

std::vector<PlannedPrimaryEntry> build_planned_primary_components(
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const RoguelikeStatus& status,
    const std::vector<std::string>& names)
{
    std::vector<PlannedPrimaryEntry> result;
    std::unordered_set<std::string> unique;
    for (const auto& name : names) {
        const auto found = status.opers.find(name);
        if (!unique.emplace(name).second || found == status.opers.end()) {
            continue;
        }
        PlannedPrimaryEntry entry { .name = name, .full_observed = found->second };
        const auto& full = entry.full_observed;
        const auto& p = full.preparation;
        if (p.training_sequence == 0 || p.operator_id.empty() ||
            p.operator_id != RoguelikeTraining.unique_identity(name)) {
            entry.component.reason = "current_Full_identity_or_training_unknown";
        }
        else {
            const auto& configured = RoguelikeRecruit.get_oper_info("BlackFlow", name);
            std::array<SkillAvailability, 3> availability;
            for (int slot = 1; slot <= 3; ++slot) {
                availability[slot - 1] = RoguelikeTraining.skill_availability(name, slot, full.elite, full.level);
            }
            const auto plan =
                resolve_recruitment_skill_plan(configured.skill, configured.alternate_skill, availability);
            entry.planned_skill = plan.selected;
            if (entry.planned_skill >= 1 && entry.planned_skill <= 3) {
                entry.component = catalog.conditional_primary(
                    name,
                    full.elite,
                    full.level,
                    entry.planned_skill,
                    p.displayed_skill_levels[entry.planned_skill - 1],
                    {},
                    PrimarySequenceConditions { .target = ConditionalPrimaryTarget::Ground,
                                                .source = "provisional_formation_ground_reference_not_live_enemy" },
                    "Formation_Full_planned_not_selected:" + status.run_id + ":" + std::to_string(p.training_sequence));
            }
            else {
                entry.component.reason = "planned_skill_not_proven_available";
            }
        }
        result.push_back(std::move(entry));
    }
    return result;
}
}

namespace asst::blackflow::prepared_formation_detail
{
bool member_of(const std::string& name, const std::string& group)
{
    const auto& groups = RoguelikeRecruit.get_group_names("BlackFlow");
    const auto ids = RoguelikeRecruit.get_group_ids_of_oper("BlackFlow", name);
    return std::ranges::any_of(ids, [&](int i) {
        return i >= 0 && static_cast<std::size_t>(i) < groups.size() && groups[i] == group;
    });
}

battle::AttackRange source_range(const PlannedPrimaryEntry& entry, battle::DeployDirection direction)
{
    auto range = BattleData.get_range(battle::Role::Unknown, entry.name, entry.full_observed.elite);
    if (range == BattleDataConfig::EmptyRange) {
        return {};
    }
    for (const auto d : { battle::DeployDirection::Right,
                          battle::DeployDirection::Up,
                          battle::DeployDirection::Left,
                          battle::DeployDirection::Down }) {
        if (d == direction) {
            return range;
        }
        for (auto& point : range) {
            point = Point(point.y, -point.x);
        }
    }
    return {};
}

bool admits(
    const PlannedPrimaryEntry& entry,
    const battle::roguelike::DeployInfoWithRank& plan,
    const std::string& stage,
    const std::string& core,
    const BlackFlowRecruitmentDutyCatalog& catalog)
{
    if (!entry.component.qualified || entry.full_observed.elite < plan.minimum_elite ||
        (!plan.operator_names.empty() &&
         std::ranges::find(plan.operator_names, entry.name) == plan.operator_names.end()) ||
        std::ranges::find(plan.excluded_operator_names, entry.name) != plan.excluded_operator_names.end() ||
        (plan.exclude_starting_core && entry.name == core) ||
        RoguelikeCopilot.maximum_deploy_cost(stage, plan.plan_index)) {
        return false;
    }
    const auto body = catalog.body(entry.name, entry.full_observed.elite, entry.full_observed.level);
    if (plan.require_persistent_blocking && body[0].lower < 1) {
        return false;
    }
    if (plan.minimum_base_block_count > 0) {
        const auto block =
            RoguelikeTraining.base_block_count(entry.name, entry.full_observed.elite, entry.full_observed.level);
        if (!block || *block < plan.minimum_base_block_count) {
            return false;
        }
    }
    const auto props = BattleData.find_first_oper(battle::Role::Unknown, entry.name);
    const auto authored = RoguelikeCopilot.get_stage_data(stage);
    if (!props || !authored || authored->blacklist_location.contains(plan.location)) {
        return false;
    }
    auto direction = plan.direction;
    if (const auto forced = authored->force_deploy_direction.find(plan.location);
        forced != authored->force_deploy_direction.end() && forced->second.applies_to(props->role, entry.name)) {
        direction = forced->second.direction;
    }
    const auto range = source_range(entry, direction);
    if (range.empty()) {
        return false;
    }
    const auto covers = [&](const auto& targets) {
        return std::ranges::all_of(targets, [&](Point p) {
            return std::ranges::find(range, p - plan.location) != range.end();
        });
    };
    return covers(plan.required_attack_tiles) && covers(plan.required_range_tiles);
}

std::unordered_set<std::string> admitted_tasks(
    const PlannedPrimaryEntry& entry,
    const battle::roguelike::CombatData& stage,
    const std::string& core,
    const BlackFlowRecruitmentDutyCatalog& catalog)
{
    std::unordered_set<std::string> result;
    std::vector<battle::roguelike::CombatData> variants { stage };
    for (const auto& [total, key] : RoguelikeCopilot.get_stage_variant_keys(stage.stage_name)) {
        static_cast<void>(total);
        if (const auto variant = RoguelikeCopilot.get_stage_data(key)) {
            variants.push_back(*variant);
        }
    }
    for (const auto& variant : variants) {
        for (const auto& [group, plans] : variant.deploy_plan) {
            if (!member_of(entry.name, group)) {
                continue;
            }
            for (const auto& plan : plans) {
                if (admits(entry, plan, variant.stage_name, core, catalog)) {
                    result.emplace(variant.stage_name + ":" + std::to_string(plan.plan_index));
                }
            }
        }
    }
    return result;
}

bool has_unresolved_authored_duty(
    const PlannedPrimaryEntry& entry,
    const battle::roguelike::CombatData& stage,
    const std::string& core,
    const BlackFlowRecruitmentDutyCatalog& catalog)
{
    std::vector<battle::roguelike::CombatData> variants { stage };
    for (const auto& [total, key] : RoguelikeCopilot.get_stage_variant_keys(stage.stage_name)) {
        static_cast<void>(total);
        if (const auto value = RoguelikeCopilot.get_stage_data(key)) {
            variants.push_back(*value);
        }
    }
    for (const auto& variant : variants) {
        for (const auto& [group, plans] : variant.deploy_plan) {
            if (!member_of(entry.name, group)) {
                continue;
            }
            for (const auto& plan : plans) {
                if (entry.full_observed.elite < plan.minimum_elite ||
                    (!plan.operator_names.empty() &&
                     std::ranges::find(plan.operator_names, entry.name) == plan.operator_names.end()) ||
                    std::ranges::find(plan.excluded_operator_names, entry.name) != plan.excluded_operator_names.end() ||
                    (plan.exclude_starting_core && entry.name == core)) {
                    continue;
                }
                // A not-yet-observed cost cap or another unresolved plan qualification
                // is an obligation to preserve the member, not evidence of no task.
                if (!admits(entry, plan, variant.stage_name, core, catalog)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool preserves_group_needs(const std::vector<std::string>& old_members, const std::vector<std::string>& new_members)
{
    for (const auto& rule : RoguelikeRecruit.get_team_complete_info("BlackFlow")) {
        const auto count = [&](const auto& names) {
            return std::ranges::count_if(names, [&](const auto& name) {
                return std::ranges::any_of(rule.groups, [&](const auto& g) { return member_of(name, g); });
            });
        };
        if (std::min<std::ptrdiff_t>(count(new_members), rule.threshold) <
            std::min<std::ptrdiff_t>(count(old_members), rule.threshold)) {
            return false;
        }
    }
    return true;
}
}

namespace asst::blackflow
{
FormationRoleProposal choose_formation_primary(
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const RoguelikeStatus& status,
    const PreparationStageContext& stage,
    const std::vector<std::string>& all_names,
    const std::vector<std::string>& legacy_members,
    const std::string& core)
{
    FormationRoleProposal result { .stage = stage,
                                   .requested_members = legacy_members,
                                   .full_observations = build_planned_primary_components(catalog, status, all_names),
                                   .reason = "same_duty_reference_no_change" };
    const auto authored = RoguelikeCopilot.get_stage_data(stage.stage_name);
    std::vector<ConditionalPrimaryEstimate> components;
    for (const auto& e : result.full_observations) {
        components.push_back(e.component);
    }
    if (authored && !authored->deploy_plan.empty() && !RoguelikeCopilot.get_hunter_opening(stage.stage_name)) {
        for (std::size_t slot = 0; slot < result.requested_members.size(); ++slot) {
            const auto old = std::ranges::find_if(result.full_observations, [&](const auto& e) {
                return e.name == result.requested_members[slot];
            });
            if (old == result.full_observations.end() || !old->component.qualified) {
                continue;
            }
            const auto legacy = static_cast<std::size_t>(old - result.full_observations.begin());
            const auto required = prepared_formation_detail::admitted_tasks(*old, *authored, core, catalog);
            if (required.empty() ||
                prepared_formation_detail::has_unresolved_authored_duty(*old, *authored, core, catalog)) {
                continue;
            }
            std::vector<std::size_t> candidates { legacy };
            for (std::size_t i = 0; i < result.full_observations.size(); ++i) {
                const auto& next = result.full_observations[i];
                if (i == legacy || !next.component.qualified ||
                    std::ranges::find(result.requested_members, next.name) != result.requested_members.end()) {
                    continue;
                }
                const auto offered = prepared_formation_detail::admitted_tasks(next, *authored, core, catalog);
                if (!std::ranges::all_of(required, [&](const auto& task) { return offered.contains(task); })) {
                    continue;
                }
                const auto old_body = catalog.body(old->name, old->full_observed.elite, old->full_observed.level);
                const auto new_body = catalog.body(next.name, next.full_observed.elite, next.full_observed.level);
                bool no_duty_loss = true;
                for (std::size_t duty = 0; duty < 2; ++duty) {
                    no_duty_loss &= new_body[duty].lower >= old_body[duty].upper;
                }
                auto members = result.requested_members;
                members[slot] = next.name;
                if (no_duty_loss &&
                    prepared_formation_detail::preserves_group_needs(result.requested_members, members)) {
                    candidates.push_back(i);
                }
            }
            const auto choice = choose_initial_primary_reference(components, candidates, legacy);
            if (choice.preferred) {
                result.requested_members[slot] = result.full_observations[*choice.preferred].name;
                result.reason = "same_authored_duty_current_Full_intrinsic_reference_swap";
            }
        }
    }
    else {
        result.reason =
            authored ? "dynamic_hunter_or_generic_task_no_member_swap" : "stage_plan_unavailable_no_member_swap";
    }
    for (const auto& entry : result.full_observations) {
        if (!entry.component.qualified ||
            std::ranges::find(result.requested_members, entry.name) == result.requested_members.end()) {
            continue;
        }
        if (!authored || authored->deploy_plan.empty() ||
            !prepared_formation_detail::admitted_tasks(entry, *authored, core, catalog).empty()) {
            result.primary_candidates.push_back(entry.name);
        }
    }
    return result;
}

bool verify_formation_selected_set(FormationRoleProposal& proposal, const std::vector<std::string>& observed_selected)
{
    auto expected = proposal.requested_members, actual = observed_selected;
    std::ranges::sort(expected);
    std::ranges::sort(actual);
    const bool unique_expected = std::adjacent_find(expected.begin(), expected.end()) == expected.end();
    const bool unique_actual = std::adjacent_find(actual.begin(), actual.end()) == actual.end();
    proposal.selected_set_verified = !expected.empty() && unique_expected && unique_actual && expected == actual;
    return proposal.selected_set_verified;
}

bool formation_proposal_matches_actual(
    const FormationRoleProposal& proposal,
    const PreparationStageContext& current,
    const RoguelikeStatus& status,
    const std::vector<std::string>& actual_members)
{
    if (!proposal.selected_set_verified || proposal.stage != current || proposal.primary_candidates.empty()) {
        return false;
    }
    auto expected = proposal.requested_members, actual = actual_members;
    std::ranges::sort(expected);
    std::ranges::sort(actual);
    if (expected != actual) {
        return false;
    }
    for (const auto& name : proposal.primary_candidates) {
        const auto before =
            std::ranges::find_if(proposal.full_observations, [&](const auto& e) { return e.name == name; });
        const auto now = status.opers.find(name);
        if (before == proposal.full_observations.end() || now == status.opers.end() ||
            before->full_observed.elite != now->second.elite || before->full_observed.level != now->second.level ||
            before->full_observed.preparation.operator_id != now->second.preparation.operator_id ||
            before->planned_skill != now->second.preparation.selected_skill ||
            now->second.preparation.training_sequence == 0 ||
            now->second.preparation.selection_sequence < now->second.preparation.training_sequence) {
            return false;
        }
        const auto rank = normalize_displayed_skill_level_for_run(
            name,
            before->planned_skill,
            now->second.elite,
            now->second.level,
            now->second.preparation.displayed_skill_levels[before->planned_skill - 1]);
        if (!rank || *rank != before->component.effective_rank) {
            return false;
        }
    }
    return true;
}
}
