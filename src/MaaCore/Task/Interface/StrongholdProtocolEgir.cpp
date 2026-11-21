#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Controller/Controller.h"
#include "StrongholdProtocolTask.h"
#include "Task/MiniGame/SPA/SPABossPolicy.h"
#include "Task/MiniGame/SPA/SPACombatRole.h"
#include "Task/MiniGame/SPA/SPACombatStats.h"
#include "Task/MiniGame/SPA/SPAEgirPolicy.h"
#include "Task/MiniGame/SPA/SPAProjection.h"
#include "Task/MiniGame/SPA/SPAReplacementPolicy.h"
#include "Task/MiniGame/SPA/SPATerrainModel.h"
#include <algorithm>

void asst::StrongholdProtocolTask::observe_egir_formation(
    const std::vector<spa::GridPlacementMember>& members,
    bool active,
    int round,
    bool harmony_active)
{
    const auto context = std::to_string(round) + (active ? ":active:" : ":inactive:") +
                         field_position_snapshot().to_string() + roster_snapshot().to_string() +
                         equipment_slots_snapshot().to_string();
    if (context == m_egir_analysis_context) {
        return;
    }
    m_egir_analysis_context = context;
    const auto catalogue = spa::operator_pacts();
    std::vector<spa::EgirEntity> entities;
    for (const auto& member : members) {
        spa::EgirEntity unit;
        unit.id = member.name + "#" + std::to_string(member.col) + "." + std::to_string(member.row);
        unit.name = member.name;
        unit.cell = { member.col, member.row };
        unit.facing = member.facing;
        const auto pacts = catalogue.find(member.name);
        unit.egir_member = pacts != catalogue.end() &&
                           std::find(pacts->second.begin(), pacts->second.end(), "阿戈尔") != pacts->second.end();
        const auto added = field_added_pacts(member.name);
        unit.egir_member = unit.egir_member || std::find(added.begin(), added.end(), "阿戈尔") != added.end();
        unit.counts_for_literal_egir_milestone = unit.egir_member;
        if (harmony_active && pacts != catalogue.end() &&
            std::find(pacts->second.begin(), pacts->second.end(), "调和") != pacts->second.end()) {
            unit.egir_member = true;
        }
        const auto owned = m_policy.owned.find(member.name);
        const bool elite = owned != m_policy.owned.end() && owned->second.elite;
        const auto record = std::find_if(
            spa::SeasonOperatorCombatCatalogue.begin(),
            spa::SeasonOperatorCombatCatalogue.end(),
            [&](const auto& row) { return row.name == member.name; });
        if (record != spa::SeasonOperatorCombatCatalogue.end()) {
            unit.tier = record->tier;
            const auto stats = elite ? record->elite : record->initial;
            unit.base_attack = stats.attack;
            unit.block_count = stats.block_count;
        }
        unit.intent = unit.egir_member ? spa::EgirIntent::Relay : spa::EgirIntent::Material;
        if ((!unit.egir_member && member.role == spa::PlacementRole::Healer) ||
            (!unit.egir_member &&
             std::find(m_policy.carries.begin(), m_policy.carries.end(), member.name) != m_policy.carries.end())) {
            unit.intent = spa::EgirIntent::ProtectedSupport;
        }
        entities.push_back(std::move(unit));
    }
    const auto output = spa::designate_egir_output(entities);
    const auto six_member_rule = m_selected_strategy_id == "band_egir" ? spa::EgirSixMemberRule::ReplaceFromBench
                                                                       : spa::EgirSixMemberRule::Revive;
    const auto projection = spa::project_egir_mark_closure(entities, active, six_member_rule);
    json::array sources, victims, problems;
    for (const auto& source : projection.sources) {
        json::array targets;
        for (const auto& target : source.marked_targets) {
            targets.emplace_back(target);
        }
        json::object row { { "source", source.source }, { "targets", std::move(targets) } };
        if (source.inherited_base_attack) {
            row["conditional_nominal_base_attack_gain"] = *source.inherited_base_attack;
        }
        if (source.inherited_block_count) {
            row["conditional_nominal_block_gain"] = *source.inherited_block_count;
        }
        sources.emplace_back(std::move(row));
    }
    for (const auto& victim : projection.victims) {
        victims.emplace_back(
            json::object { { "id", victim.id },
                           { "conditional_marks", victim.marks },
                           { "protected_target", victim.protected_target },
                           { "defeat_observed", false } });
    }
    for (const auto& problem : projection.problems) {
        problems.emplace_back(problem);
    }
    auto info = basic_info_with_what("SPAEgirFormationAnalysis");
    info["details"] =
        json::object { { "round", round },
                       { "run_id", m_run_id },
                       { "current_output_known", output.has_value() },
                       { "current_output", output.value_or("") },
                       { "output_role_source", "current_egir_field_priority" },
                       { "input_complete", projection.complete },
                       { "pact_active_by_member_model", active },
                       { "executable_egir_formation_plan_ready", false },
                       { "distinct_egir_members", projection.distinct_egir_members },
                       { "current_season_semantics_live_verified", false },
                       { "stats_source", "season_base_excluding_account_and_combat_buffs" },
                       { "membership_source", "operator_catalogue_verified_equipment_and_harmony" },
                       { "six_member_revive_limit", projection.base_six_member_revive_limit },
                       { "six_member_bench_replacement_limit", projection.six_member_bench_replacement_limit },
                       { "generic_all_survivor_rearrangement_suppressed", active },
                       { "board_changed", false },
                       { "battle_blocked", false },
                       { "conditional_sources", std::move(sources) },
                       { "conditional_victims", std::move(victims) },
                       { "problems", std::move(problems) } };
    if (projection.conditional_unique_target_layers) {
        info["details"]["conditional_unique_target_layers"] = *projection.conditional_unique_target_layers;
    }
    callback(AsstMsg::SubTaskExtraInfo, info);
}

std::optional<asst::spa::DeploymentStep> asst::StrongholdProtocolTask::egir_layout_step(
    const std::vector<spa::GridPlacementMember>& members,
    const std::map<std::string, std::vector<std::pair<int, int>>>& ranges,
    int round,
    bool harmony_active)
{
    const auto frame = ctrler()->get_image();
    const auto visible_bar = spa::preparation_boss_health_bar(frame);
    if (visible_bar) {
        m_spatial_boss_phase = true;
        m_spatial_boss_phase_round = round;
    }
    const bool boss_phase = m_spatial_boss_phase || visible_bar.has_value();
    const auto terrain = spa::terrain_layout_model(m_spatial_map_id, boss_phase);
    if (!m_egir_chain_validation_mode || m_layout_supplied || !terrain || m_dynamic_layout_attempts[round] >= 16) {
        return std::nullopt;
    }
    const auto& board_catalogue = boss_phase ? spa::BossBoardTerrains : spa::BoardTerrains;
    const auto board = std::find_if(board_catalogue.begin(), board_catalogue.end(), [&](const auto& item) {
        return item.id == m_spatial_map_id;
    });
    if (board == board_catalogue.end()) {
        return std::nullopt;
    }
    spa::EgirFormationContext request;
    request.pact_active = true;
    request.lanes = spa::active_preparation_lanes(*terrain, round);
    request.six_member_rule = m_selected_strategy_id == "band_egir" ? spa::EgirSixMemberRule::ReplaceFromBench
                                                                    : spa::EgirSixMemberRule::Revive;
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 11; ++col) {
            if (board->rows[row][col] == 'E') {
                request.home = { col, row };
            }
        }
    }
    spa::BossObjective boss;
    bool boss_bar_verified = false;
    std::string boss_target_source = "unknown";
    if (boss_phase && !visible_bar) {
        return std::nullopt;
    }
    if (visible_bar) {
        const auto projection =
            spa::registered_grid_projection(m_spatial_grid_reference, frame, m_spatial_grid_projection);
        boss_bar_verified = projection.has_value();
        if (projection && m_mode == "independent") {
            boss = spa::boss_objective(m_advertised_boss_family, request.home, m_difficulty, m_spatial_boss_hidden_variant);
        }
        if (projection) {
            if (const auto red_region = spa::boss_attackable_region(frame, *visible_bar)) {
                const auto cells = spa::project_boss_attackable_cells(*red_region, *projection);
                if (!cells.empty()) {
                    m_boss_attackable_cells = cells;
                    m_boss_attackable_map_id = m_spatial_map_id;
                    m_boss_attackable_round = round;
                }
            }
            request.boss_cells = spa::verified_boss_opening_region(
                true,
                m_spatial_map_id,
                round,
                m_boss_attackable_map_id,
                m_boss_attackable_round,
                m_boss_attackable_cells);
            if (!request.boss_cells.empty()) {
                boss_target_source = "observed_red_hatched_attackable_region";
            }
            else {
                request.boss_cells = spa::estimate_boss_opening_footprint(frame, *visible_bar, *projection);
                if (!request.boss_cells.empty()) {
                    boss_target_source = "projected_five_by_three_opening_footprint";
                }
            }
        }
        request.boss_opening_only = !boss.known && !request.boss_cells.empty();
        request.stationary_boss = boss.stationary_route;
        request.boss_movement_waypoints = boss.movement_waypoints;
        if (request.boss_cells.empty()) {
            request.boss_cells = spa::estimate_solo_stationary_boss_footprint(boss);
            if (!request.boss_cells.empty()) {
                boss_target_source = "stationary_footprint_from_registered_solo_spawn";
            }
        }
        if (request.boss_cells.empty() && boss.known && !boss.possible_cells.empty()) {
            request.boss_cells = boss.possible_cells;
            request.boss_cells_are_alternatives = !boss.variant_known;
            boss_target_source = "client_route_translated_from_registered_home";
        }
        if (!boss_bar_verified || request.boss_cells.empty()) {
            m_dynamic_layout_target.clear();
            const auto deferred_context = "unresolved-boss:" + std::to_string(round);
            if (m_egir_plan_context != deferred_context) {
                m_egir_plan_context = deferred_context;
                auto deferred = basic_info_with_what("SPABossLayoutDeferred");
                deferred["details"] = json::object { { "round", round },
                                                     { "reason", "Visible boss bar lacks a registered attack target" },
                                                     { "boss_family_name", m_advertised_boss_family },
                                                     { "board_changed", false },
                                                     { "battle_blocked", false } };
                callback(AsstMsg::SubTaskExtraInfo, deferred);
            }
            return std::nullopt;
        }
    }
    const bool prioritize_boss = request.stationary_boss || request.boss_opening_only;
    json::array conditional_windows;
    const auto catalogue = spa::operator_pacts();
    std::vector<spa::EgirPlacementUnit> inputs;
    std::vector<spa::DeploymentUnit> current;
    for (const auto& member : members) {
        spa::EgirPlacementUnit input;
        auto& entity = input.entity;
        auto& unit = input.deployment;
        const bool unique = m_auto_deploy_positions.count(member.name) == 1;
        entity.id =
            unique ? member.name : member.name + "#" + std::to_string(member.col) + "." + std::to_string(member.row);
        entity.name = member.name;
        entity.cell = { member.col, member.row };
        entity.facing = member.facing;
        const auto pacts = catalogue.find(member.name);
        const auto added = field_added_pacts(member.name);
        const auto has_pact = [&](const std::string& pact) {
            return (pacts != catalogue.end() &&
                    std::find(pacts->second.begin(), pacts->second.end(), pact) != pacts->second.end()) ||
                   std::find(added.begin(), added.end(), pact) != added.end();
        };
        entity.egir_member = has_pact("阿戈尔");
        entity.counts_for_literal_egir_milestone = entity.egir_member;
        entity.egir_member = entity.egir_member || (harmony_active && has_pact("调和"));
        const auto& owned = m_policy.owned.at(member.name);
        if (const auto stats = spa::season_combat_stats(member.name, owned.elite)) {
            entity.base_attack = stats->attack;
            entity.block_count = stats->block_count;
        }
        const auto record = std::find_if(
            spa::SeasonOperatorCombatCatalogue.begin(),
            spa::SeasonOperatorCombatCatalogue.end(),
            [&](const auto& item) { return item.name == member.name; });
        if (record != spa::SeasonOperatorCombatCatalogue.end()) {
            entity.tier = record->tier;
        }
        unit.id = entity.id;
        unit.cell = entity.cell;
        unit.facing = member.facing ? member.facing : 'E';
        unit.movable = unique;
        unit.frontline = member.role == spa::PlacementRole::Frontline;
        unit.healer = member.role == spa::PlacementRole::Healer;
        unit.block_count = entity.block_count.value_or(0);
        unit.holds_position = spa::default_skill_holds_position(member.name, owned.elite);
        if (const auto range = ranges.find(member.name); range != ranges.end()) {
            unit.local_range = range->second;
            unit.range_known = !unit.local_range.empty();
        }
        if (request.stationary_boss) {
            const auto window = spa::conditional_skill_attack_window(member.name, owned.elite, boss.time_limit_seconds);
            if (window.possible) {
                for (const auto& cell : window.attack_range) {
                    if (std::find(unit.local_range.begin(), unit.local_range.end(), cell) == unit.local_range.end()) {
                        unit.local_range.push_back(cell);
                    }
                }
                unit.range_known = !unit.local_range.empty();
                conditional_windows.emplace_back(
                    json::object { { "entity", entity.id },
                                   { "first_activation_seconds", window.first_activation_seconds },
                                   { "active_seconds", window.active_seconds },
                                   { "activation_observed", false },
                                   { "timing_excludes_runtime_modifiers", true } });
            }
        }
        unit.damage_output = unit.range_known && !unit.healer;
        if (const auto props = BattleData.find_first_oper(battle::Role::Unknown, member.name, true)) {
            unit.damage_output = unit.damage_output && spa::has_basic_attack(props->role, props->sub_role);
            unit.accepts_direct_healing = spa::accepts_direct_healing(props->sub_role);
            unit.provides_regeneration = props->sub_role == battle::SubRole::Support_Bard;
        }
        for (const auto& cell : terrain->buildable) {
            // Fence is ground-buildable but not walkable. Mire remains a
            // valid deployment domain; its debuff is not a terrain ban.
            const auto role = BattleData.get_first_role(member.name);
            const bool melee = BattleData.get_location_type(role, member.name) == battle::LocationType::Melee;
            if (spa::terrain_allows_deployment(m_spatial_map_id, cell, melee, boss_phase)) {
                unit.placement_cells.push_back(cell);
            }
        }
        if (std::find(unit.placement_cells.begin(), unit.placement_cells.end(), unit.cell) ==
            unit.placement_cells.end()) {
            unit.placement_cells.push_back(unit.cell);
        }
        const bool protected_pact = std::any_of(m_policy.support_pacts.begin(), m_policy.support_pacts.end(), has_pact);
        const bool carry =
            std::find(m_policy.carries.begin(), m_policy.carries.end(), member.name) != m_policy.carries.end();
        const auto gear = m_equipped_items.find(member.name);
        input.expendable_material = !entity.egir_member && !unit.healer && !carry && !protected_pact &&
                                    m_policy.primary_locked_after_activation && owned.copies == 1 &&
                                    (gear == m_equipped_items.end() || gear->second.empty());
        entity.intent = unit.healer                 ? spa::EgirIntent::ProtectedSupport
                        : entity.egir_member        ? spa::EgirIntent::Relay
                        : input.expendable_material ? spa::EgirIntent::Material
                                                    : spa::EgirIntent::ProtectedSupport;
        // A route-specific role order: bard is protected regeneration, not
        // the attacking terminal merely because it is a high-tier carry.
        input.core_priority = spa::egir_output_priority(member.name);
        input.preferred_boss_output = unit.damage_output && (carry || input.core_priority >= 4);
        input.eligible_chain_head = member.name != "归溟幽灵鲨";
        input.same_row_recruitment_members = member.name == "歌蕾蒂娅" && !owned.elite ? 3 : 0;
        current.push_back(unit);
        inputs.push_back(std::move(input));
    }
    // The output is the head of the chain. Dollkeeper is a relay: its
    // substitute has zero block and must not replace the defended output.
    const auto append_member = [&](const std::string& name) {
        if (!prioritize_boss &&
            std::none_of(
                inputs.begin(),
                inputs.end(),
                [&](const auto& input) { return input.entity.name == name && input.deployment.holds_position; }) &&
            request.defended_chain_names.empty()) {
            return false;
        }
        return spa::append_egir_defended_name(
            request.defended_chain_names,
            inputs,
            name,
            request.home,
            request.chain_facing);
    };
    const bool terminal = append_member("克莱门莎") || append_member("乌尔比安");
    if (terminal || append_member("幽灵鲨") || append_member("斯卡蒂") || append_member("安哲拉") ||
        append_member("歌蕾蒂娅")) {
        append_member("归溟幽灵鲨");
        append_member("斯卡蒂");
    }
    request.target_admissible = [&](const std::vector<spa::DeploymentUnit>& target) {
        auto oriented = current;
        spa::DeploymentSequence sequence;
        for (const auto& member : members) {
            if (member.facing || m_auto_deploy_positions.count(member.name) != 1) {
                continue;
            }
            const auto goal =
                std::find_if(target.begin(), target.end(), [&](const auto& row) { return row.id == member.name; });
            const auto input = std::find_if(inputs.begin(), inputs.end(), [&](const auto& row) {
                return row.entity.id == member.name;
            });
            if (goal != target.end() && input != inputs.end() &&
                (input->entity.egir_member || goal->cell != input->entity.cell || goal->facing != 'E')) {
                sequence.steps.push_back(
                    { spa::DeploymentStepKind::Face,
                      member.name,
                      {},
                      { member.col, member.row },
                      { member.col, member.row },
                      goal->facing });
                for (auto& unit : oriented) {
                    if (unit.id == member.name) {
                        unit.facing = goal->facing;
                    }
                }
            }
        }
        const auto remaining = spa::sequence_deployment(oriented, target, 1500);
        sequence.complete = remaining.complete;
        sequence.steps.insert(sequence.steps.end(), remaining.steps.begin(), remaining.steps.end());
        std::map<std::string, bool> forms;
        for (const auto& member : members) {
            forms[member.name] = m_policy.owned.at(member.name).elite;
        }
        const auto map_context = m_spatial_map_id + (m_spatial_boss_phase ? "#boss" : "#normal");
        return spa::spatial_sequence_admissible(sequence, members, [&](const auto& step, const auto& field) {
            const auto key = spa::spatial_action_failure_key(map_context, step, field, forms);
            const auto receiver = spa::spatial_receiver_failure_key(map_context, step, field, forms);
            const auto preview = spa::spatial_preview_failure_key(map_context, round, step, field, forms);
            const auto drop =
                key ? spa::spatial_vacant_drop_failure_key(map_context, step, forms.at(step.unit)) : std::nullopt;
            return key && receiver && !m_failed_spatial_actions.contains(*key) &&
                   !m_failed_spatial_actions.contains(*receiver) &&
                   (!drop || !m_failed_spatial_actions.contains(*drop)) &&
                   (!preview || !m_failed_spatial_actions.contains(*preview));
        });
    };
    std::set<std::string> field_ids;
    for (const auto& input : inputs) {
        field_ids.insert(input.entity.id);
    }
    std::string field_signature;
    for (const auto& id : field_ids) {
        field_signature += id + "\n";
    }
    std::string objective_context = "#egir-home";
    if (boss_bar_verified) {
        objective_context = "#egir-boss:" + m_advertised_boss_family;
        for (const auto& cell : request.boss_cells) {
            objective_context += ":" + std::to_string(cell.first) + "." + std::to_string(cell.second);
        }
    }
    const auto context = roster_snapshot().to_string() + equipment_slots_snapshot().to_string() + field_signature +
                         m_spatial_map_id + m_selected_strategy_id +
                         (round <= 3 ? "#egir-opening-lower" : "#egir-all-entries") + objective_context +
                         "#rejected:" + std::to_string(m_failed_spatial_actions.size());
    if (context != m_dynamic_layout_context) {
        m_dynamic_layout_context = context;
        m_dynamic_layout_target.clear();
        m_failed_layout_context.clear();
    }
    if (m_failed_layout_context == context + std::to_string(round)) {
        return std::nullopt;
    }
    if (m_dynamic_layout_target.empty() && m_egir_plan_context != context) {
        m_egir_plan_context = context;
        const auto proposal = spa::plan_egir_formation(inputs, request);
        json::array chain;
        json::array boss_cells;
        for (const auto& cell : request.boss_cells) {
            boss_cells.emplace_back(json::object { { "col", cell.first }, { "row", cell.second } });
        }
        for (const auto& id : proposal.chain) {
            chain.emplace_back(id);
        }
        auto receipt = basic_info_with_what("SPAEgirFormationPlanned");
        json::array placements;
        for (const auto& unit : proposal.units) {
            placements.emplace_back(
                json::object { { "id", unit.id },
                               { "col", unit.cell.first },
                               { "row", unit.cell.second },
                               { "facing", std::string(1, unit.facing) } });
        }
        receipt["details"] =
            json::object { { "round", round },
                           { "map_id", m_spatial_map_id },
                           { "core", proposal.core },
                           { "boss_priority_attackers", proposal.boss_priority_attackers },
                           { "boss_chain_attack_priority", proposal.boss_chain_attack_priority },
                           { "boss_movement_waypoints_known", request.boss_movement_waypoints.size() },
                           { "boss_core_movement_waypoints_covered", proposal.boss_core_movement_waypoints },
                           { "attacking_core_separate_from_chain", proposal.attacking_core_separate_from_chain },
                           { "input_complete", proposal.input_complete },
                           { "found", proposal.found },
                           { "chain", std::move(chain) },
                           { "defended_chain_prefix", request.defended_chain_prefix },
                           { "defended_chain_names", request.defended_chain_names },
                           { "placements", std::move(placements) },
                           { "changed_units", proposal.changed_units },
                           { "evaluations", proposal.evaluations },
                           { "budget_exhausted", proposal.budget_exhausted },
                           { "validation_mode", true },
                           { "failed_spatial_inputs_excluded", true },
                           { "current_season_semantics_live_verified", false },
                           { "objective",
                             request.stationary_boss     ? "stationary_boss_attack"
                             : request.boss_opening_only ? "boss_opening_attack"
                                                         : "home_defense" },
                           { "boss_family_name", m_advertised_boss_family },
                           { "catalogue_hidden_variant",
                             m_spatial_boss_hidden_variant ? json::value(*m_spatial_boss_hidden_variant)
                                                           : json::value(nullptr) },
                           { "boss_health_bar_verified", boss_bar_verified },
                           { "boss_variant_verified", boss.variant_known },
                           { "possible_boss_cells", std::move(boss_cells) },
                           { "all_alternative_cells_required", request.boss_cells_are_alternatives },
                           { "boss_target_source", boss_target_source },
                           { "boss_physical_hit_verified", false },
                           { "conditional_skill_windows", std::move(conditional_windows) },
                           { "enemy_paths_observed", false },
                           { "movement_executed", false },
                           { "reason", proposal.reason } };
        const auto core_source = std::find_if(
            proposal.conditional_consumption.sources.begin(),
            proposal.conditional_consumption.sources.end(),
            [&](const auto& source) { return source.source == proposal.core; });
        if (core_source != proposal.conditional_consumption.sources.end() && core_source->inherited_base_attack) {
            receipt["details"]["conditional_core_inherited_base_attack"] = *core_source->inherited_base_attack;
        }
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        if (proposal.found) {
            m_dynamic_layout_target = proposal.units;
        }
    }
    if (m_dynamic_layout_target.empty()) {
        return std::nullopt;
    }
    // Establish only directions needed by this plan. A verified next step
    // reuses the existing journal/held-input/population reconciliation path.
    for (const auto& member : members) {
        if (member.facing || m_auto_deploy_positions.count(member.name) != 1) {
            continue;
        }
        const auto target =
            std::find_if(m_dynamic_layout_target.begin(), m_dynamic_layout_target.end(), [&](const auto& unit) {
                return unit.id == member.name;
            });
        const auto input =
            std::find_if(inputs.begin(), inputs.end(), [&](const auto& unit) { return unit.entity.id == member.name; });
        if (target != m_dynamic_layout_target.end() && input != inputs.end() &&
            (input->entity.egir_member || target->cell != input->entity.cell || target->facing != 'E')) {
            return spa::DeploymentStep { spa::DeploymentStepKind::Face, member.name,   {}, { member.col, member.row },
                                         { member.col, member.row },    target->facing };
        }
    }
    const auto sequence = spa::sequence_deployment(current, m_dynamic_layout_target, 1500);
    if (!sequence.complete) {
        m_failed_layout_context = context + std::to_string(round);
        return std::nullopt;
    }
    // Empty means this exact target is already reached. Keep the target,
    // so unchanged observations do not launch another optimization cycle.
    return sequence.steps.empty() ? std::nullopt : std::optional(sequence.steps.front());
}
