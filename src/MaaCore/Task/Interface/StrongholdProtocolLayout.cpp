#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Controller/Controller.h"
#include "StrongholdProtocolTask.h"
#include "Task/MiniGame/SPA/SPABattleRole.h"
#include "Task/MiniGame/SPA/SPABossPolicy.h"
#include "Task/MiniGame/SPA/SPACombatStats.h"
#include "Task/MiniGame/SPA/SPAEngagementPolicy.h"
#include "Task/MiniGame/SPA/SPAFormation.h"
#include "Task/MiniGame/SPA/SPAImageEvidence.h"
#include "Task/MiniGame/SPA/SPANumberOcr.h"
#include "Task/MiniGame/SPA/SPAPlanner.h"
#include "Task/MiniGame/SPA/SPAProjection.h"
#include "Task/MiniGame/SPA/SPASpatialActionDeadline.h"
#include "Task/MiniGame/SPA/SPASpatialGrowthPolicy.h"
#include "Task/MiniGame/SPA/SPASummonPolicy.h"
#include "Task/MiniGame/SPA/SPASwapTaskPlugin.h"
#include "Task/MiniGame/SPA/SPATerrainModel.h"
#include "Vision/OCRer.h"
#include <algorithm>
#include <chrono>
#include <iterator>

void
    asst::StrongholdProtocolTask::refresh_spatial_grid_snapshot(const json::value& state, bool require_fresh_projection)
{
    const int round = state.get("round", -1);
    const bool has_unverified_pose =
        std::any_of(m_auto_deploy_positions.begin(), m_auto_deploy_positions.end(), [&](const auto& item) {
            return !m_verified_field_pose_points.contains(item.second);
        });
    const bool bootstrap = m_field_checkpoint_loaded || m_spatial_grid_reference.empty() ||
                           m_spatial_grid_projection.empty() || has_unverified_pose;
    const bool has_unknown_facing =
        std::any_of(m_auto_deploy_positions.begin(), m_auto_deploy_positions.end(), [&](const auto& item) {
            return !m_committed_facings.contains(item.second);
        });
    if (!m_policy.dynamic_decision_mode || state.get("page", std::string()) != "preparation" ||
        !state.get("round_known", false) || !state.get("funds_known", false) ||
        !state.get("remaining_deployment_known", false) || m_pending_spatial_orientation ||
        m_pending_field_replacement || m_auto_deploy_positions.empty() ||
        m_auto_deploy_positions.size() != static_cast<std::size_t>(8 - state.get("remaining_deployment", -1)) ||
        (!require_fresh_projection && !bootstrap && m_growth_population_poses == m_auto_deploy_positions &&
         m_population_grid_members.size() == m_auto_deploy_positions.size() && !has_unknown_facing)) {
        return;
    }
    std::vector<spa::PlacementMember> members;
    std::string bootstrap_failure_key =
        "grid-bootstrap:" + m_spatial_map_id + (m_spatial_boss_phase ? "#boss" : "#normal");
    for (const auto& [name, point] : m_auto_deploy_positions) {
        if (!bootstrap && !m_verified_field_pose_points.contains(point)) {
            return;
        }
        const auto facing = m_committed_facings.find(point);
        members.push_back(
            { point.first,
              point.second,
              spa::operator_placement_role(name),
              name,
              facing == m_committed_facings.end() ? '\0' : facing->second });
        bootstrap_failure_key += "|" + name + ":" + std::to_string(point.first) + "." + std::to_string(point.second) +
                                 (m_policy.owned.at(name).elite ? ":elite" : ":initial") +
                                 (m_verified_field_pose_points.contains(point) ? ":verified" : ":unverified");
    }
    if (bootstrap && m_failed_spatial_actions.contains(bootstrap_failure_key)) {
        return;
    }
    // Several verified replacements can invalidate the grid in one round.
    // Exhausting the old field's budget must not suppress observation of the
    // new physical field. Funds, bench purchases and inventory changes alone
    // do not reset this bounded identity/pose/form context.
    const auto refresh_context = std::to_string(round) + ":" + bootstrap_failure_key;
    auto& refresh_attempt = m_spatial_grid_refresh_attempts[refresh_context];
    if (refresh_attempt >= (bootstrap ? 1 : 2)) {
        return;
    }
    ++refresh_attempt;
    spa::SwapTaskPlugin probe(m_callback, m_inst, TaskType);
    probe.set_retry_times(0);
    probe.set_task_id(m_task_id);
    probe.set_task_ptr(this);
    const bool recheck_identities = m_field_checkpoint_loaded || has_unverified_pose;
    if (!m_spatial_grid_reference.empty() && !m_spatial_grid_projection.empty() && !m_spatial_map_id.empty()) {
        if (!probe.set_registered_grid_context(m_spatial_grid_reference, m_spatial_grid_projection, m_spatial_map_id)) {
            return;
        }
    }
    const bool observed = bootstrap ? probe.probe_registered_grid(
                                          members,
                                          round,
                                          state.get("funds", -1),
                                          state.get("remaining_deployment", -1),
                                          recheck_identities)
                                    : probe.observe_grid_snapshot(
                                          m_spatial_grid_reference,
                                          m_spatial_grid_projection,
                                          members,
                                          round,
                                          state.get("funds", -1),
                                          state.get("remaining_deployment", -1));
    if (bootstrap && probe.field_pose_verified() && probe.reoriented_field_members().size() == members.size()) {
        m_field_checkpoint_loaded = false;
        m_auto_deploy_positions.clear();
        m_verified_field_pose_points.clear();
        m_auto_deploy_used_cells.clear();
        m_committed_facings.clear();
        for (const auto& member : probe.reoriented_field_members()) {
            const std::pair point { member.x, member.y };
            m_auto_deploy_positions.emplace(member.name, point);
            m_verified_field_pose_points.insert(point);
            m_auto_deploy_used_cells.insert(point);
            if (member.facing) {
                m_committed_facings[point] = member.facing;
            }
        }
    }
    const auto& grid = probe.observed_grid_members();
    const auto populations = observed ? spa::same_row_growth_populations(grid) : std::nullopt;
    if (populations && grid.size() == m_auto_deploy_positions.size() &&
        probe.observed_field_members().size() == m_auto_deploy_positions.size()) {
        m_auto_deploy_positions.clear();
        m_verified_field_pose_points.clear();
        m_auto_deploy_used_cells.clear();
        m_committed_facings.clear();
        for (const auto& member : probe.observed_field_members()) {
            const std::pair point { member.x, member.y };
            m_auto_deploy_positions.emplace(member.name, point);
            m_verified_field_pose_points.insert(point);
            m_auto_deploy_used_cells.insert(point);
            if (member.facing != '\0') {
                m_committed_facings.emplace(point, member.facing);
            }
        }
        m_population_grid_members = grid;
        m_growth_population_poses = m_auto_deploy_positions;
        m_row_growth_populations = *populations;
        m_spatial_grid_reference = probe.observed_grid_reference().clone();
        m_spatial_grid_projection = probe.observed_grid_projection().clone();
        if (!probe.verified_map_id().empty()) {
            m_spatial_map_id = probe.verified_map_id();
        }
        if (!probe.observed_boss_attackable_cells().empty()) {
            m_boss_attackable_cells = probe.observed_boss_attackable_cells();
            m_boss_attackable_map_id = m_spatial_map_id;
            m_boss_attackable_round = round;
        }
        m_spatial_snapshot_round = round;
    }
    auto evidence = basic_info_with_what("SPASpatialGridSnapshotRefreshed");
    const bool complete = populations.has_value() && grid.size() == m_auto_deploy_positions.size();
    if (bootstrap && !complete) {
        m_failed_spatial_actions.insert(bootstrap_failure_key);
    }
    evidence["details"] = json::object { { "run_id", m_run_id },
                                         { "round", round },
                                         { "field_context_attempt", refresh_attempt },
                                         { "roster", roster_snapshot() },
                                         { "field_positions", field_position_snapshot() },
                                         { "failed_spatial_actions", failed_spatial_action_snapshot() },
                                         { "field_members", m_auto_deploy_positions.size() },
                                         { "complete", complete },
                                         { "had_unverified_pose", has_unverified_pose },
                                         { "fresh_identity_probes", bootstrap && recheck_identities },
                                         { "bootstrap", bootstrap },
                                         { "board_changed", false },
                                         { "battle_blocked", false } };
    callback(AsstMsg::SubTaskExtraInfo, evidence);
}

asst::spa::DeploymentUnit asst::StrongholdProtocolTask::describe_deployed_unit(
    const spa::GridPlacementMember& member,
    const std::vector<spa::BoardCell>& range,
    bool boss_round) const
{
    spa::DeploymentUnit unit;
    const bool unique = m_auto_deploy_positions.count(member.name) == 1;
    unit.id = unique ? member.name : member.name + "#" + std::to_string(member.col) + "." + std::to_string(member.row);
    unit.cell = { member.col, member.row };
    unit.frontline = member.role == spa::PlacementRole::Frontline;
    // Read the chess form's actual training phase, not the operator's
    // rarity or a universal block-one approximation. Skill/module/pact
    // changes still need their own state before changing this baseline.
    const auto owned = m_policy.owned.find(member.name);
    const bool elite = owned != m_policy.owned.end() && owned->second.elite;
    const auto baseline = spa::season_combat_stats(member.name, elite);
    unit.block_count = baseline ? baseline->block_count : 1;
    if (baseline && baseline->attack_interval > 0) {
        unit.boss_basic_attack_weight = std::max(1, static_cast<int>(baseline->attack / baseline->attack_interval));
    }
    unit.holds_position = spa::default_skill_holds_position(member.name, elite);
    unit.facing = member.facing ? member.facing : 'E';
    unit.facing_known = member.facing != '\0';
    unit.range_known = !range.empty() && (unit.facing_known || boss_round);
    unit.healer = unit.range_known && member.role == spa::PlacementRole::Healer;
    unit.damage_output = unit.range_known && member.role != spa::PlacementRole::Healer;
    if (const auto props = BattleData.find_first_oper(battle::Role::Unknown, member.name, true)) {
        if (props->sub_role == battle::SubRole::Warrior_Liberator) {
            unit.block_count = 0;
        }
        unit.damage_output = unit.damage_output && spa::has_basic_attack(props->role, props->sub_role);
        unit.accepts_direct_healing = spa::accepts_direct_healing(props->sub_role);
        unit.provides_regeneration = props->sub_role == battle::SubRole::Support_Bard;
    }
    unit.local_range = range;
    if (boss_round && unit.range_known) {
        // Compare plausible early boss coverage from the season default,
        // including the time spent on the basic range before activation.
        constexpr double early_boss_window_seconds = 30.0;
        const auto attack = spa::conditional_skill_attack_window(member.name, elite, early_boss_window_seconds);
        if (attack.possible) {
            unit.boss_skill_attack_range.assign(attack.attack_range.begin(), attack.attack_range.end());
            unit.boss_skill_attack_uptime_percent =
                static_cast<int>(attack.active_seconds * 100 / early_boss_window_seconds);
        }
        const auto skill = spa::season_default_skill(member.name, elite);
        if (skill && skill->id == "skchr_shu_3") {
            unit.boss_skill_healing_range.assign(skill->declared_range.begin(), skill->declared_range.end());
        }
    }
    return unit;
}

asst::StrongholdProtocolTask::BossOpeningTarget asst::StrongholdProtocolTask::boss_opening_target(int round) const
{
    BossOpeningTarget target;
    if (!m_spatial_boss_phase || m_spatial_boss_phase_round != round || m_spatial_snapshot_round != round) {
        return target;
    }
    target.cells = spa::verified_boss_opening_region(
        true,
        m_spatial_map_id,
        round,
        m_boss_attackable_map_id,
        m_boss_attackable_round,
        m_boss_attackable_cells);
    target.measured = !target.cells.empty();
    if (target.measured) {
        target.source = "observed_red_hatched_attackable_region";
    }
    else if (const auto bar = spa::preparation_boss_health_bar(m_spatial_grid_reference)) {
        target.cells = spa::estimate_boss_opening_footprint(m_spatial_grid_reference, *bar, m_spatial_grid_projection);
        if (!target.cells.empty()) {
            target.source = "projected_five_by_three_opening_footprint";
        }
    }
    if (m_mode == "independent") {
        const auto terrain = spa::terrain_layout_model(m_spatial_map_id, true);
        if (terrain && !terrain->lanes.empty() && !terrain->lanes.front().path.empty()) {
            const auto boss =
                spa::boss_objective(m_advertised_boss_family, terrain->lanes.front().path.back(), m_difficulty);
            target.movement_waypoints = boss.movement_waypoints;
            if (target.cells.empty()) {
                target.cells = spa::estimate_solo_boss_opening_cells(boss);
                if (!target.cells.empty()) {
                    target.source = boss.stationary_route ? "stationary_footprint_from_registered_solo_spawn"
                                                          : "moving_boss_opening_cell_from_client_route";
                }
            }
        }
    }
    return target;
}

void asst::StrongholdProtocolTask::review_boss_opening_coverage(const json::value& state)
{
    const int round = state.get("round", -1);
    const auto target = boss_opening_target(round);
    const int population =
        state.get("remaining_deployment_known", false) ? 8 - state.get("remaining_deployment", 8) : -1;
    const bool board_known = population > 0 && m_population_grid_members.size() == static_cast<size_t>(population) &&
                             m_population_grid_members.size() == m_auto_deploy_positions.size() &&
                             m_growth_population_poses == m_auto_deploy_positions && !m_pending_spatial_orientation &&
                             !m_pending_field_replacement && !m_field_checkpoint_loaded;
    std::vector<spa::DeploymentUnit> units;
    if (board_known && !target.cells.empty()) {
        for (auto member : m_population_grid_members) {
            if (m_auto_deploy_positions.count(member.name) == 1) {
                const auto facing = m_committed_facings.find(m_auto_deploy_positions.find(member.name)->second);
                member.facing = facing == m_committed_facings.end() ? '\0' : facing->second;
            }
            const auto owned = m_policy.owned.find(member.name);
            if (owned == m_policy.owned.end()) {
                continue;
            }
            const auto form = spa::season_combat_stats(member.name, owned->second.elite);
            std::vector<spa::BoardCell> range;
            if (form) {
                range.assign(form->basic_range.begin(), form->basic_range.end());
            }
            else {
                for (const auto& point : BattleData.get_range(
                         BattleData.get_first_role(member.name),
                         member.name,
                         owned->second.elite ? 2 : 1)) {
                    range.emplace_back(point.x, point.y);
                }
            }
            units.push_back(describe_deployed_unit(member, range, true));
        }
    }
    const bool coverage_available =
        board_known && !target.cells.empty() && units.size() == m_population_grid_members.size();
    const bool coverage_known = coverage_available && std::ranges::all_of(units, [](const auto& unit) {
        return unit.range_known && unit.facing_known;
    });
    const int count = coverage_available ? spa::stationary_boss_attackers(units, target.cells) : -1;
    const int weight = coverage_available ? spa::stationary_boss_attack_weight(units, target.cells) : -1;
    auto objective = basic_info_with_what("SPABossPreparationObjectiveReviewed");
    objective["details"] =
        json::object { { "round", round },
                       { "boss_region_verified", target.measured },
                       { "boss_target_source", target.source },
                       { "registered_board_known", board_known },
                       { "boss_attackers_known", coverage_known && target.measured },
                       { "boss_opening_attackers", target.measured && coverage_known ? count : -1 },
                       { "boss_opening_basic_attack_weight", target.measured && coverage_known ? weight : -1 },
                       { "boss_opening_attackers_lower_bound", target.measured ? count : -1 },
                       { "boss_opening_basic_attack_weight_lower_bound", target.measured ? weight : -1 },
                       { "estimated_boss_opening_attackers", !target.measured && coverage_known ? count : -1 },
                       { "estimated_boss_opening_basic_attack_weight", !target.measured && coverage_known ? weight : -1 },
                       { "estimated_boss_opening_attackers_lower_bound", !target.measured ? count : -1 },
                       { "estimated_boss_opening_basic_attack_weight_lower_bound", !target.measured ? weight : -1 },
                       { "skill_uptime_or_consumption_applied", false },
                       { "boss_defeat_verified", false },
                       { "replacement_reconciliation_pending", m_pending_field_replacement.has_value() } };
    callback(AsstMsg::SubTaskExtraInfo, objective);
}

std::optional<asst::spa::DeploymentStep> asst::StrongholdProtocolTask::dynamic_layout_step(
    const std::vector<spa::GridPlacementMember>& members,
    const std::map<std::string, std::vector<std::pair<int, int>>>& ranges,
    const std::vector<std::string>& active_pacts,
    int round)
{
    const auto planning_started = std::chrono::steady_clock::now();
    const auto terrain = spa::terrain_layout_model(m_spatial_map_id, m_spatial_boss_phase);
    const bool boss_round = m_spatial_boss_phase && m_spatial_boss_phase_round == round;
    const int layout_action_budget = boss_round ? 8 : 3;
    if (m_layout_supplied || !terrain || m_dynamic_layout_attempts[round] >= layout_action_budget) {
        return std::nullopt;
    }
    const int available_input_steps = layout_action_budget - m_dynamic_layout_attempts[round];
    const auto map_context = m_spatial_map_id + (boss_round ? "#boss" : "#normal");
    const auto rejected_vacant_drop = [&](const spa::DeploymentUnit& unit, const spa::BoardCell& cell) {
        if (unit.cell == cell || m_failed_spatial_actions.empty() ||
            std::any_of(members.begin(), members.end(), [&](const auto& member) {
                return spa::BoardCell { member.col, member.row } == cell;
            })) {
            return false;
        }
        const auto owned = m_policy.owned.find(unit.id);
        const auto key = spa::spatial_vacant_drop_failure_key(
            map_context,
            { spa::DeploymentStepKind::Move, unit.id, {}, unit.cell, cell, unit.facing },
            owned != m_policy.owned.end() && owned->second.elite);
        return key && m_failed_spatial_actions.contains(*key);
    };
    std::vector<spa::DeploymentUnit> current;
    std::map<std::string, std::string> names;
    std::map<std::string, spa::PlacementRole> roles;
    std::set<std::string> directions_known;
    for (const auto& member : members) {
        static const std::vector<spa::BoardCell> empty_range;
        const auto range = ranges.find(member.name);
        auto unit = describe_deployed_unit(member, range == ranges.end() ? empty_range : range->second, boss_round);
        const bool healer_already_adjusted = !boss_round && member.role == spa::PlacementRole::Healer &&
                                             m_spatial_orientation_attempts.contains({ round, member.name });
        unit.movable = m_auto_deploy_positions.count(member.name) == 1 && member.facing && !healer_already_adjusted;
        names[unit.id] = member.name;
        roles[unit.id] = member.role;
        if (member.facing) {
            directions_known.insert(unit.id);
        }
        unit.placement_cells = unit.movable ? (unit.frontline ? terrain->ground_route : terrain->buildable)
                                            : std::vector<spa::BoardCell> { unit.cell };
        std::erase_if(unit.placement_cells, [&](const auto& cell) { return rejected_vacant_drop(unit, cell); });
        if (std::find(unit.placement_cells.begin(), unit.placement_cells.end(), unit.cell) ==
            unit.placement_cells.end()) {
            // An existing bad blocker position is observed, not a new legal
            // target. Keeping it lets the search move that unit off a fence.
            unit.placement_cells.push_back(unit.cell);
        }
        current.push_back(std::move(unit));
    }
    m_layout_units = current;
    m_layout_lanes = spa::active_preparation_lanes(*terrain, round);
    m_deployable_cells = terrain->buildable;
    m_dynamic_layout_observed = true;
    const auto boss_mobility = spa::boss_family_mobility(m_advertised_boss_family);
    const auto boss_target = boss_opening_target(round);
    const auto& boss_cells = boss_target.cells;
    const auto& movement_waypoints = boss_target.movement_waypoints;
    std::string boss_context;
    for (const auto& [col, row] : boss_cells) {
        boss_context += "#boss-cell:" + std::to_string(col) + "." + std::to_string(row);
    }
    for (const auto& [col, row] : movement_waypoints) {
        boss_context += "#boss-waypoint:" + std::to_string(col) + "." + std::to_string(row);
    }
    std::string pact_context;
    for (const auto& pact : std::set<std::string>(active_pacts.begin(), active_pacts.end())) {
        pact_context += "#" + pact;
    }
    std::map<std::string, std::string> position_bound_summons;
    std::string summon_context;
    for (const auto& [summon, observed] : m_observed_bench_summons) {
        const auto rule = spa::summon_rule(summon);
        if (observed.first != round || !rule || !rule->preparation_executor ||
            (rule->placement != spa::SummonPlacement::OwnerFront && rule->placement != spa::SummonPlacement::OwnerRange)) {
            continue;
        }
        const std::string owner(rule->owner);
        if (m_auto_deploy_positions.count(owner) == 1) {
            position_bound_summons.emplace(owner, summon);
            summon_context += "#summon:" + summon;
        }
    }
    const auto context = roster_snapshot().to_string() + equipment_slots_snapshot().to_string() + m_spatial_map_id +
                         (m_spatial_boss_phase ? "#boss" : "#normal") + "#round" + std::to_string(round) + "#" +
                         m_policy.primary_pact + pact_context + boss_context + summon_context;
    const auto target_snapshot = [](const std::vector<spa::DeploymentUnit>& units) {
        json::array result;
        for (const auto& unit : units) {
            result.emplace_back(
                json::object { { "name", unit.id },
                               { "col", unit.cell.first },
                               { "row", unit.cell.second },
                               { "direction", unit.movable ? std::string(1, unit.facing) : "" },
                               { "direction_planned", unit.movable } });
        }
        return result;
    };
    if (context != m_dynamic_layout_context) {
        if (!m_dynamic_layout_target.empty()) {
            auto invalidated = basic_info_with_what("SPADynamicLayoutPlanInvalidated");
            invalidated["details"] =
                json::object { { "round", round },
                               { "reason", "Round, roster, equipment, pacts, summon, map or boss region changed" },
                               { "input_issued", false } };
            callback(AsstMsg::SubTaskExtraInfo, invalidated);
        }
        m_dynamic_layout_target.clear();
        m_dynamic_layout_expected_cells.clear();
        m_dynamic_layout_context = context;
        m_failed_layout_context.clear();
    }
    if (m_failed_layout_context == context + std::to_string(round)) {
        return std::nullopt;
    }
    if (!boss_round && !m_dynamic_layout_target.empty()) {
        for (const auto& unit : current) {
            if (roles.at(unit.id) != spa::PlacementRole::Healer ||
                !m_spatial_orientation_attempts.contains({ round, unit.id })) {
                continue;
            }
            const auto goal =
                std::find_if(m_dynamic_layout_target.begin(), m_dynamic_layout_target.end(), [&](const auto& target) {
                    return target.id == unit.id;
                });
            if (goal == m_dynamic_layout_target.end() || goal->cell != unit.cell ||
                (directions_known.contains(unit.id) && goal->facing != unit.facing)) {
                m_dynamic_layout_target.clear();
                m_dynamic_layout_expected_cells.clear();
                break;
            }
            goal->movable = false;
        }
    }
    const auto healing_repair = [&]() -> std::optional<spa::DeploymentStep> {
        for (auto unit : current) {
            if (roles.at(unit.id) != spa::PlacementRole::Healer || m_auto_deploy_positions.count(unit.id) != 1 ||
                m_spatial_orientation_attempts.contains({ round, unit.id })) {
                continue;
            }
            unit.healer = true;
            auto legal = terrain->buildable;
            std::erase_if(legal, [&](const auto& cell) { return rejected_vacant_drop(unit, cell); });
            if (std::find(legal.begin(), legal.end(), unit.cell) == legal.end()) {
                legal.push_back(unit.cell);
            }
            if (auto action = spa::repair_healing_coverage(unit, current, m_layout_lanes, legal)) {
                auto decision = basic_info_with_what("SPAHealingCoverageRepair");
                decision["details"] = json::object { { "round", round },
                                                     { "name", unit.id },
                                                     { "from_col", action->from.first },
                                                     { "from_row", action->from.second },
                                                     { "to_col", action->to.first },
                                                     { "to_row", action->to.second },
                                                     { "planning_ms",
                                                       std::chrono::duration_cast<std::chrono::milliseconds>(
                                                           std::chrono::steady_clock::now() - planning_started)
                                                           .count() } };
                callback(AsstMsg::SubTaskExtraInfo, decision);
                return action;
            }
        }
        return std::nullopt;
    };
    if (!m_dynamic_layout_target.empty()) {
        auto continuation = spa::continue_deployment_plan(
            current,
            m_dynamic_layout_target,
            directions_known,
            m_dynamic_layout_expected_cells,
            true,
            1500);
        auto receipt = basic_info_with_what("SPADynamicLayoutPlanContinued");
        receipt["details"] = json::object {
            { "round", round },
            { "reason", continuation.reason },
            { "target", target_snapshot(m_dynamic_layout_target) },
            { "target_preserved", continuation.kind != spa::DeploymentPlanContinuationKind::Invalidated },
            { "target_complete", continuation.kind == spa::DeploymentPlanContinuationKind::Complete },
            { "planned_facing_repair", continuation.kind == spa::DeploymentPlanContinuationKind::RestoreFacing },
            { "deferred", continuation.kind == spa::DeploymentPlanContinuationKind::Deferred },
            { "input_issued", false },
            { "battle_blocked", false }
        };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        if (continuation.step) {
            const auto expected = spa::deployment_cells_after_step(current, *continuation.step);
            if (!expected) {
                return std::nullopt;
            }
            // A prediction only: the normal Swap executor still checks the
            // source, receiver, direction commit and observed field outcome.
            m_dynamic_layout_expected_cells = *expected;
            return continuation.step;
        }
        if (continuation.kind != spa::DeploymentPlanContinuationKind::Invalidated) {
            // Complete/Deferred finish this planning pass, not preparation.
            // Keep the target so an observation gap cannot launch a new plan.
            return std::nullopt;
        }
        m_dynamic_layout_target.clear();
        m_dynamic_layout_expected_cells.clear();
    }
    std::vector<std::vector<spa::BoardCell>> active_paths;
    for (const auto& lane : m_layout_lanes) {
        active_paths.push_back(lane.path);
    }
    const auto layout_grid = [&](const std::vector<spa::DeploymentUnit>& candidate) {
        std::vector<spa::GridPlacementMember> grid;
        grid.reserve(candidate.size());
        for (const auto& unit : candidate) {
            grid.push_back(
                { unit.cell.first,
                  unit.cell.second,
                  roles.at(unit.id),
                  names.at(unit.id),
                  directions_known.contains(unit.id) ? unit.facing : '\0' });
        }
        return grid;
    };
    const auto placeable_summons = [&](const std::vector<spa::DeploymentUnit>& candidate,
                                       const std::vector<spa::GridPlacementMember>& grid) {
        if (position_bound_summons.empty()) {
            return 0;
        }
        int count = 0;
        for (std::size_t index = 0; index < grid.size(); ++index) {
            const auto& owner = grid[index];
            const auto summon = position_bound_summons.find(owner.name);
            if (summon != position_bound_summons.end() && !spa::summon_placement_cells(
                                                               summon->second,
                                                               owner,
                                                               candidate[index].local_range,
                                                               terrain->buildable,
                                                               grid,
                                                               {})
                                                               .empty()) {
                ++count;
            }
        }
        return count;
    };
    std::set<std::string> cold_sources;
    std::set<std::string> freeze_stackers;
    std::map<std::string, std::vector<spa::BoardCell>> skill_freeze_sources;
    if (!boss_round && round < m_expected_boss_round && m_policy.primary_pact == "谢拉格" &&
        std::find(active_pacts.begin(), active_pacts.end(), "谢拉格") != active_pacts.end()) {
        for (const auto& [id, name] : names) {
            if (spa::is_kjerag_freeze_stacker(name)) {
                freeze_stackers.insert(id);
            }
            const auto equipped = m_equipped_items.find(name);
            const bool carries_ice = m_auto_deploy_positions.count(name) == 1 && equipped != m_equipped_items.end() &&
                                     std::ranges::any_of(equipped->second, spa::is_kjerag_freeze_equipment);
            const bool has_distinct_skill_range = name == "圣聆初雪" || name == "凛御银灰";
            if (carries_ice || (spa::is_kjerag_freeze_provider(name) && !has_distinct_skill_range)) {
                cold_sources.insert(id);
            }
            if (has_distinct_skill_range) {
                const auto owned = m_policy.owned.find(name);
                const auto skill =
                    spa::season_default_skill(name, owned != m_policy.owned.end() && owned->second.elite);
                if (skill && !skill->declared_range.empty()) {
                    skill_freeze_sources[id].assign(skill->declared_range.begin(), skill->declared_range.end());
                }
            }
        }
    }
    const auto trait_preference = [&](const std::vector<spa::DeploymentUnit>& candidate) {
        const auto grid = layout_grid(candidate);
        int engagement = 0;
        for (const auto& unit : candidate) {
            engagement += spa::engagement_position_value(
                roles.at(unit.id),
                unit.cell,
                directions_known.contains(unit.id) ? unit.facing : '\0',
                unit.local_range,
                terrain->lanes);
            const auto& name = names.at(unit.id);
            if (spa::needs_screened_position(name) &&
                spa::support_precedes_frontline(unit.cell, grid, active_paths, true)) {
                engagement -= 5000;
            }
            const bool ranged_output = roles.at(unit.id) == spa::PlacementRole::Ranged && unit.damage_output;
            if (unit.healer || !unit.range_known || (!ranged_output && !spa::uses_attack_approach_geometry(name))) {
                continue;
            }
            engagement += spa::attack_approach_value(
                unit.cell,
                unit.facing,
                grid,
                m_layout_lanes,
                unit.local_range,
                unit.frontline && unit.holds_position);
        }
        const int uncovered = static_cast<int>(spa::uncovered_healable_allies(candidate).size());
        const int freeze_coverage =
            spa::freeze_growth_coverage(candidate, m_layout_lanes, cold_sources, freeze_stackers, skill_freeze_sources);
        return engagement - uncovered * 550 + spa::spatial_formation_value(grid, active_pacts, m_policy.primary_pact) +
               freeze_coverage * 900 + placeable_summons(candidate, grid) * 900;
    };
    std::function<int(const std::vector<spa::DeploymentUnit>&)> preparation_growth;
    if (!boss_round && round < m_expected_boss_round && m_policy.last_round_life_loss == 0 &&
        std::find(active_pacts.begin(), active_pacts.end(), m_policy.primary_pact) != active_pacts.end() &&
        std::any_of(
            members.begin(),
            members.end(),
            [](const auto& member) { return spa::has_preparation_adjacency_growth(member.name); })) {
        preparation_growth = [&](const auto& candidate) {
            return spa::primary_preparation_adjacency_value(layout_grid(candidate), m_policy.primary_pact);
        };
    }
    const auto proposal = spa::propose_deployment(
        current,
        m_layout_lanes,
        terrain->buildable,
        layout_action_budget == 8 ? 4000 : 1200,
        trait_preference,
        boss_cells,
        movement_waypoints,
        preparation_growth,
        available_input_steps);
    json::array movement_targets;
    for (const auto& [col, row] : movement_waypoints) {
        movement_targets.emplace_back(json::object { { "col", col }, { "row", row } });
    }
    auto evidence = basic_info_with_what("SPADynamicLayoutPlanned");
    evidence["details"] = json::object {
        { "round", round },
        { "map_id", m_spatial_map_id },
        { "registered_members", members.size() },
        { "healable_allies_outside_range_before", spa::uncovered_healable_allies(current) },
        { "healable_allies_outside_range_after", spa::uncovered_healable_allies(proposal.units) },
        { "input_complete", proposal.input_complete },
        { "all_static_constraints_met", proposal.found },
        { "improved", proposal.improved },
        { "unblocked_lanes_improvement", proposal.unblocked_lanes_improvement },
        { "score_improvement", proposal.score_improvement },
        { "blocking_capacity_improvement", proposal.blocking_capacity_improvement },
        { "boss_opening_region_verified", boss_target.measured },
        { "boss_opening_region_cells", boss_cells.size() },
        { "boss_opening_target_source", boss_target.source },
        { "boss_opening_footprint_estimated", !boss_target.measured && !boss_cells.empty() },
        { "stationary_boss_region_verified", boss_target.measured && boss_mobility.stationary_route },
        { "stationary_boss_region_cells",
          boss_mobility.stationary_route && boss_target.measured ? boss_cells.size() : 0 },
        { "boss_basic_attackers_before", spa::stationary_boss_attackers(current, boss_cells) },
        { "boss_basic_attackers_after", spa::stationary_boss_attackers(proposal.units, boss_cells) },
        { "boss_coverage_improvement", proposal.boss_coverage_improvement },
        { "boss_estimated_attack_improvement", proposal.boss_estimated_attack_improvement },
        { "boss_supported_attack_improvement", proposal.boss_supported_attack_improvement },
        { "boss_sustained_attack_improvement", proposal.boss_sustained_attack_improvement },
        { "boss_movement_waypoints", std::move(movement_targets) },
        { "boss_movement_coverage_improvement", proposal.boss_movement_coverage_improvement },
        { "boss_basic_attack_weight_before", spa::stationary_boss_attack_weight(current, boss_cells) },
        { "boss_basic_attack_weight_after", spa::stationary_boss_attack_weight(proposal.units, boss_cells) },
        { "boss_damage_verified", false },
        { "spatial_preference_improvement", proposal.preference_improvement },
        { "position_bound_summons_placeable_before", placeable_summons(current, layout_grid(current)) },
        { "position_bound_summons_placeable_after", placeable_summons(proposal.units, layout_grid(proposal.units)) },
        { "freeze_growth_coverage_before",
          spa::freeze_growth_coverage(current, m_layout_lanes, cold_sources, freeze_stackers, skill_freeze_sources) },
        { "freeze_growth_coverage_after",
          spa::freeze_growth_coverage(
              proposal.units,
              m_layout_lanes,
              cold_sources,
              freeze_stackers,
              skill_freeze_sources) },
        { "preparation_growth_priority", static_cast<bool>(preparation_growth) },
        { "available_input_steps", available_input_steps },
        { "preparation_growth_preference_improvement", proposal.preparation_growth_preference_improvement },
        { "evaluations", proposal.evaluations },
        { "planning_ms",
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - planning_started)
              .count() },
        { "routes_from_client_terrain", true },
        { "enemy_paths_observed", false },
        { "block_counts_measured", false },
        { "battle_blocked", false },
        { "target", target_snapshot(proposal.units) }
    };
    callback(AsstMsg::SubTaskExtraInfo, evidence);
    if (!proposal.input_complete || !proposal.improved) {
        return healing_repair();
    }
    const auto sequence = spa::sequence_deployment(current, proposal.units, 1500);
    if (!sequence.complete || sequence.steps.empty()) {
        return std::nullopt;
    }
    if (!spa::deployment_change_worth_input(proposal, sequence)) {
        auto deferred = basic_info_with_what("SPADynamicLayoutMarginalGainDeferred");
        deferred["details"] =
            json::object { { "round", round },
                           { "map_id", m_spatial_map_id },
                           { "spatial_preference_improvement", proposal.preference_improvement },
                           { "preference_input_cost", spa::deployment_preference_input_cost(sequence) },
                           { "planned_steps", sequence.steps.size() },
                           { "input_issued", false },
                           { "applies_to_egir_chain", false } };
        callback(AsstMsg::SubTaskExtraInfo, deferred);
        return std::nullopt;
    }
    const auto expected = spa::deployment_cells_after_step(current, sequence.steps.front());
    if (!expected) {
        return std::nullopt;
    }
    m_dynamic_layout_target = proposal.units;
    m_dynamic_layout_expected_cells = *expected;
    json::array planned_steps;
    for (const auto& step : sequence.steps) {
        planned_steps.emplace_back(
            json::object { { "name", step.unit },
                           { "displaced", step.displaced },
                           { "col", step.to.first },
                           { "row", step.to.second },
                           { "direction", std::string(1, step.facing) } });
    }
    auto plan = basic_info_with_what("SPAFormationPreplanned");
    plan["details"] =
        json::object { { "round", round },
                       { "map_id", m_spatial_map_id },
                       { "target", target_snapshot(m_dynamic_layout_target) },
                       { "steps", std::move(planned_steps) },
                       { "available_input_steps", available_input_steps },
                       { "input_steps_with_facing_repairs", spa::deployment_input_steps_with_facing_repairs(sequence) },
                       { "input_issued", false } };
    callback(AsstMsg::SubTaskExtraInfo, plan);
    return sequence.steps.front();
}

bool asst::StrongholdProtocolTask::handle_spatial_reorientation(const json::value& state)
{
    m_dynamic_layout_observed = false;
    const int round = state.get("round", m_policy.round);
    if (state.get("page", std::string()) != "preparation" || !state.get("round_known", false) ||
        !state.get("funds_known", false) || !state.get("remaining_deployment_known", false)) {
        return false;
    }
    if (m_pending_spatial_orientation) {
        for (const auto& key : { "name", "displaced" }) {
            const auto reserved = m_pending_spatial_orientation->get(key, std::string());
            if (!reserved.empty()) {
                m_blocked_purchases.emplace(round, reserved);
                m_auto_deploy_blocked_names.insert(reserved);
            }
        }
        if (m_spatial_reconciliation_attempts[round] >= 2) {
            return false;
        }
        ++m_spatial_reconciliation_attempts[round];
        std::string identities;
        for (const auto& [name, point] : m_auto_deploy_positions) {
            identities += name + (m_policy.owned.at(name).elite ? "#elite\n" : "#initial\n");
        }
        if (m_pending_spatial_orientation->get("field_signature", identities) != identities) {
            // Another verified acquisition/promotion changed the field.
            // Rebuild affected identities by observation; the old verifier
            // may not relabel the entire new roster from its old picture.
            m_pending_orientation_verifier.reset();
        }
    }
    auto executor = m_pending_orientation_verifier;
    if (m_pending_spatial_orientation && !executor) {
        // After a process/login interruption no held input is replayed. The
        // persisted exact field point and population must be revalidated.
        const auto& pending = *m_pending_spatial_orientation;
        const auto name = pending.get("name", std::string());
        const auto displaced = pending.get("displaced", std::string());
        spa::SwapTaskPlugin probe(m_callback, m_inst, TaskType);
        probe.set_retry_times(0);
        probe.set_task_id(m_task_id);
        probe.set_task_ptr(this);
        std::optional<Point> recovered;
        bool relocated = false;
        for (int outcome = 0; outcome < (pending.get("relocation", false) ? 2 : 1); ++outcome) {
            const bool destination = pending.get("relocation", false) && outcome == 0;
            const Point point(
                pending.get(destination ? "target_x" : "x", -1),
                pending.get(destination ? "target_y" : "y", -1));
            if (!probe.revalidate_promoted_pose(name, point, round)) {
                continue;
            }
            const Point other(
                pending.get(destination ? "x" : "displaced_x", -1),
                pending.get(destination ? "y" : "displaced_y", -1));
            if (!displaced.empty() && !probe.revalidate_promoted_pose(displaced, other, round)) {
                continue;
            }
            recovered = point;
            relocated = destination;
            break;
        }
        if (!recovered) {
            return false;
        }
        const Point original(pending.get("x", -1), pending.get("y", -1));
        const bool population_preserved =
            state.get("remaining_deployment", -1) == pending.get("remaining_deployment", -2);
        m_auto_deploy_positions.erase(name);
        m_auto_deploy_positions.emplace(name, std::pair { recovered->x, recovered->y });
        if (!displaced.empty()) {
            const Point other(
                relocated ? original.x : pending.get("displaced_x", -1),
                relocated ? original.y : pending.get("displaced_y", -1));
            m_auto_deploy_positions.erase(displaced);
            m_auto_deploy_positions.emplace(displaced, std::pair { other.x, other.y });
            m_auto_deploy_blocked_names.erase(displaced);
        }
        // A cold checkpoint has no camera-reference image. Only the probed
        // identities are recovered; do not certify every old field pixel.
        m_verified_field_pose_points.clear();
        m_verified_field_pose_points.emplace(recovered->x, recovered->y);
        m_committed_facings.clear();
        m_growth_population_poses.clear();
        m_population_grid_members.clear();
        m_field_checkpoint_loaded = true;
        m_spatial_grid_reference.release();
        m_spatial_grid_projection.release();
        m_auto_deploy_used_cells.clear();
        for (const auto& [unit, point] : m_auto_deploy_positions) {
            m_auto_deploy_used_cells.insert(point);
        }
        m_auto_deploy_blocked_names.erase(name);
        m_pending_spatial_orientation.reset();
        auto receipt = basic_info_with_what("SPASpatialOrientationReconciled");
        receipt["details"] = json::object { { "run_id", m_run_id },
                                            { "round", round },
                                            { "name", name },
                                            { "roster", roster_snapshot() },
                                            { "field_positions", field_position_snapshot() },
                                            { "pending_orientation", nullptr },
                                            { "direction_input_committed", false },
                                            { "source", "checkpoint_field_probe" },
                                            { "relocation_applied", relocated },
                                            { "whole_field_camera_verified", false },
                                            { "population_preserved", population_preserved } };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        return true;
    }
    if (executor) {
        if (!executor->set_spatial_reconciliation_context(
                round,
                state.get("funds", -1),
                state.get("remaining_deployment", -1))) {
            return false;
        }
        executor->reconcile_in_place_orientation();
    }
    else {
        if (m_field_checkpoint_loaded || !m_policy.dynamic_decision_mode ||
            state.get("page", std::string()) != "preparation" || m_pending_field_replacement ||
            !state.get("funds_known", false) || !state.get("remaining_deployment_known", false) ||
            m_auto_deploy_positions.size() < 2 ||
            m_auto_deploy_positions.size() != static_cast<std::size_t>(8 - state.get("remaining_deployment", -1)) ||
            m_growth_population_poses != m_auto_deploy_positions ||
            m_population_grid_members.size() != m_auto_deploy_positions.size() ||
            std::any_of(m_auto_deploy_positions.begin(), m_auto_deploy_positions.end(), [&](const auto& placed) {
                return !m_verified_field_pose_points.contains(placed.second);
            })) {
            if (m_auto_run && m_policy.dynamic_decision_mode && state.get("page", std::string()) == "preparation") {
                const auto signature = std::to_string(round) + field_position_snapshot().to_string() +
                                       std::to_string(m_population_grid_members.size()) +
                                       std::to_string(m_growth_population_poses == m_auto_deploy_positions) +
                                       std::to_string(m_field_checkpoint_loaded) +
                                       std::to_string(m_pending_field_replacement.has_value());
                if (signature != m_spatial_deferral_context) {
                    m_spatial_deferral_context = signature;
                    auto evidence = basic_info_with_what("SPASpatialPlanningDeferred");
                    evidence["details"] = json::object {
                        { "round", round },
                        { "checkpoint_revalidation_pending", m_field_checkpoint_loaded },
                        { "replacement_reconciliation_pending", m_pending_field_replacement.has_value() },
                        { "field_count", m_auto_deploy_positions.size() },
                        { "registered_member_count", m_population_grid_members.size() },
                        { "population_poses_match", m_growth_population_poses == m_auto_deploy_positions },
                        { "funds_known", state.get("funds_known", false) },
                        { "remaining_deployment_known", state.get("remaining_deployment_known", false) },
                        { "remaining_deployment", state.get("remaining_deployment", -1) },
                        { "input_issued", false }
                    };
                    callback(AsstMsg::SubTaskExtraInfo, evidence);
                }
            }
            return false;
        }
        std::vector<spa::GridPlacementMember> members;
        std::vector<spa::RosterUnit> roster;
        std::map<std::string, std::vector<std::pair<int, int>>> ranges;
        for (auto member : m_population_grid_members) {
            const auto placed = m_auto_deploy_positions.find(member.name);
            if (placed == m_auto_deploy_positions.end()) {
                return false;
            }
            if (m_auto_deploy_positions.count(member.name) == 1) {
                const auto facing = m_committed_facings.find(placed->second);
                member.facing = facing == m_committed_facings.end() ? '\0' : facing->second;
            }
            const auto& owned = m_policy.owned.at(member.name);
            roster.push_back({ member.name, true, field_added_pacts(member.name), owned.elite });
            if (const auto stats = spa::season_combat_stats(member.name, owned.elite)) {
                ranges[member.name].assign(stats->basic_range.begin(), stats->basic_range.end());
            }
            else {
                for (const auto& point :
                     BattleData.get_range(BattleData.get_first_role(member.name), member.name, owned.elite ? 2 : 1)) {
                    ranges[member.name].emplace_back(point.x, point.y);
                }
            }
            members.push_back(std::move(member));
        }
        const auto formation = spa::evaluate_formation(
            roster,
            spa::operator_pacts(),
            spa::pact_requirements(),
            spa::enabled_formation_pacts(m_enabled_core_pacts, m_policy.disabled_pacts));
        bool egir_active = false;
        bool egir_harmony_active = false;
        if (m_policy.primary_pact == "阿戈尔") {
            const bool active = std::find(formation.active_pacts.begin(), formation.active_pacts.end(), "阿戈尔") !=
                                formation.active_pacts.end();
            const bool harmony_active =
                std::find(formation.active_pacts.begin(), formation.active_pacts.end(), "调和") !=
                formation.active_pacts.end();
            observe_egir_formation(members, active, round, harmony_active);
            if (active) {
                egir_active = true;
                egir_harmony_active = harmony_active;
                // Generic all-survivor coverage cannot move intentional
                // consumption bodies. Only the separate Egir planner may
                // propose a chain, through the same verified executor.
                if (!m_layout_supplied) {
                    m_layout_units.clear();
                    m_layout_lanes.clear();
                    m_deployable_cells.clear();
                }
                if (!m_egir_chain_validation_mode) {
                    m_dynamic_layout_target.clear();
                    return false;
                }
            }
        }
        std::optional<spa::SpatialReorientation> plan;
        std::optional<spa::DeploymentStep> layout_step;
        const int layout_action_budget = m_spatial_boss_phase && m_spatial_boss_phase_round == round ? 8 : 3;
        if (egir_active) {
            layout_step = egir_layout_step(members, ranges, round, egir_harmony_active);
        }
        else if (!m_layout_supplied && m_dynamic_layout_attempts[round] < layout_action_budget) {
            // Unknown direction is an observation gap, not an instruction to
            // turn east. New generic plans keep that unit immovable; a cached
            // target can explicitly restore its previously planned facing.
            // Egir has its own chain-facing intent above.
            layout_step = dynamic_layout_step(members, ranges, formation.active_pacts, round);
        }
        std::string spatial_attempt_key;
        std::string spatial_receiver_key;
        std::optional<std::string> spatial_drop_key;
        if (layout_step) {
            const auto member = std::find_if(members.begin(), members.end(), [&](const auto& item) {
                return item.name == layout_step->unit;
            });
            if (member == members.end() || m_auto_deploy_positions.count(member->name) != 1 ||
                (!layout_step->displaced.empty() && m_auto_deploy_positions.count(layout_step->displaced) != 1)) {
                return false;
            }
            std::map<std::string, bool> physical_forms;
            for (const auto& item : members) {
                physical_forms[item.name] = m_policy.owned.at(item.name).elite;
            }
            const auto map_context = m_spatial_map_id + (m_spatial_boss_phase ? "#boss" : "#normal");
            const auto failure_key =
                spa::spatial_action_failure_key(map_context, *layout_step, members, physical_forms);
            if (!failure_key) {
                return false;
            }
            spatial_attempt_key = *failure_key;
            spatial_receiver_key =
                spa::spatial_receiver_failure_key(map_context, *layout_step, members, physical_forms).value_or("");
            spatial_drop_key =
                spa::spatial_vacant_drop_failure_key(map_context, *layout_step, physical_forms.at(member->name));
            if (m_failed_spatial_actions.contains(spatial_attempt_key) ||
                (!spatial_receiver_key.empty() && m_failed_spatial_actions.contains(spatial_receiver_key)) ||
                (spatial_drop_key && m_failed_spatial_actions.contains(*spatial_drop_key))) {
                m_failed_layout_context = m_dynamic_layout_context + std::to_string(round);
                m_dynamic_layout_target.clear();
                return false;
            }
            plan = spa::SpatialReorientation {
                member->name, member->col, member->row, member->facing, layout_step->facing, 0, 0
            };
        }
        else if (
            !egir_active && m_dynamic_layout_attempts[round] < layout_action_budget &&
            m_dynamic_layout_target.empty() && formation.complete &&
            std::count_if(
                m_spatial_orientation_attempts.begin(),
                m_spatial_orientation_attempts.end(),
                [&](const auto& attempt) { return attempt.first == round; }) < 2) {
            auto helpers = members;
            for (auto& member : helpers) {
                if (m_spatial_orientation_attempts.contains({ round, member.name })) {
                    member.facing = '\0';
                }
            }
            plan = spa::choose_spatial_reorientation(helpers, formation.active_pacts, m_policy.primary_pact, ranges);
        }
        if (!plan) {
            return false;
        }
        if (!layout_step) {
            std::map<std::string, bool> forms;
            for (const auto& item : members) {
                forms[item.name] = m_policy.owned.at(item.name).elite;
            }
            const spa::DeploymentStep face {
                spa::DeploymentStepKind::Face, plan->name,     "", { plan->col, plan->row },
                { plan->col, plan->row },      plan->direction
            };
            const auto key = spa::spatial_action_failure_key(
                m_spatial_map_id + (m_spatial_boss_phase ? "#boss" : "#normal"),
                face,
                members,
                forms);
            if (!key || m_failed_spatial_actions.contains(*key)) {
                return false;
            }
            spatial_attempt_key = *key;
            spatial_receiver_key = spa::spatial_receiver_failure_key(
                                       m_spatial_map_id + (m_spatial_boss_phase ? "#boss" : "#normal"),
                                       face,
                                       members,
                                       forms)
                                       .value_or("");
            if (!spatial_receiver_key.empty() && m_failed_spatial_actions.contains(spatial_receiver_key)) {
                return false;
            }
        }
        const auto point = m_auto_deploy_positions.find(plan->name)->second;
        const bool relocation = layout_step && layout_step->kind != spa::DeploymentStepKind::Face;
        const int target_col = layout_step ? layout_step->to.first : plan->col;
        const int target_row = layout_step ? layout_step->to.second : plan->row;
        const auto displaced = layout_step ? layout_step->displaced : std::string();
        const auto spatial_action = !relocation         ? spa::SpatialPreparationAction::Face
                                    : displaced.empty() ? spa::SpatialPreparationAction::Move
                                                        : spa::SpatialPreparationAction::Swap;
        const auto allows_spatial_input =
            [this, round, spatial_action, name = plan->name](spa::SpatialPreparationPhase phase) {
                if (m_mode != "joint") {
                    return true;
                }
                const auto remaining =
                    m_joint_preparation_deadline.observe(round, std::nullopt, spa::PreparationDeadline::Clock::now());
                if (spa::spatial_action_fits_preparation(remaining, spatial_action, phase)) {
                    return true;
                }
                auto deferred = basic_info_with_what("SPAJointSpatialDeadline");
                deferred["details"] =
                    json::object { { "name", name },
                                   { "round", round },
                                   { "phase",
                                     phase == spa::SpatialPreparationPhase::Admission ? "admission" : "before_hold" },
                                   { "seconds_remaining", remaining.value_or(-1) },
                                   { "new_held_input_issued", false },
                                   { "preserves_existing_formation", true } };
                callback(AsstMsg::SubTaskExtraInfo, deferred);
                return false;
            };
        if (!allows_spatial_input(spa::SpatialPreparationPhase::Admission)) {
            m_dynamic_layout_target.clear();
            m_dynamic_layout_expected_cells.clear();
            return false;
        }
        std::optional<std::string> preview_key;
        if (layout_step) {
            std::map<std::string, bool> forms;
            for (const auto& member : members) {
                forms[member.name] = m_policy.owned.at(member.name).elite;
            }
            preview_key = spa::spatial_preview_failure_key(
                m_spatial_map_id + (m_spatial_boss_phase ? "#boss" : "#normal"),
                round,
                *layout_step,
                members,
                forms);
            if (preview_key && m_failed_spatial_actions.contains(*preview_key)) {
                return false;
            }
        }
        auto decision = basic_info_with_what(layout_step ? "SPADynamicLayoutStep" : "SPASpatialOrientationDecision");
        decision["details"] = json::object { { "round", round },
                                             { "name", plan->name },
                                             { "col", plan->col },
                                             { "row", plan->row },
                                             { "target_col", target_col },
                                             { "target_row", target_row },
                                             { "relocation", relocation },
                                             { "displaced", displaced },
                                             { "previous", std::string(1, plan->previous) },
                                             { "direction", std::string(1, plan->direction) },
                                             { "growth_improvement", plan->growth_improvement },
                                             { "combined_improvement", plan->combined_improvement },
                                             { "executed", false } };
        callback(AsstMsg::SubTaskExtraInfo, decision);
        executor = std::make_shared<spa::SwapTaskPlugin>(m_callback, m_inst, TaskType);
        executor->set_retry_times(0);
        executor->set_task_id(m_task_id);
        executor->set_task_ptr(this);
        const json::object options { { "name", plan->name },
                                     { "source_x", point.first },
                                     { "source_y", point.second },
                                     { "target_x", point.first },
                                     { "target_y", point.second },
                                     { "face_only", true },
                                     { "preserve_population", true },
                                     { "reorient_col", target_col },
                                     { "reorient_row", target_row },
                                     { "origin_col", plan->col },
                                     { "origin_row", plan->row },
                                     { "relocate_field", relocation },
                                     { "relocation_displaced", displaced },
                                     { "expected_map_id", m_spatial_map_id },
                                     { "direction", std::string(1, plan->direction) },
                                     { "round", round },
                                     { "funds", state.get("funds", -1) },
                                     { "remaining_deployment", state.get("remaining_deployment", -1) },
                                     { "fast_mode", true },
                                     { "best_effort_deployment", true },
                                     { "placement_units", field_position_snapshot() } };
        m_pending_spatial_orientation = json::object {
            { "name", plan->name },       { "round", round },
            { "x", point.first },         { "y", point.second },
            { "target_x", point.first },  { "target_y", point.second },
            { "target_col", target_col }, { "target_row", target_row },
            { "relocation", relocation }, { "dynamic_layout", layout_step.has_value() },
            { "displaced", displaced },   { "remaining_deployment", state.get("remaining_deployment", -1) }
        };
        if (!spatial_attempt_key.empty()) {
            (*m_pending_spatial_orientation)["attempt_fingerprint"] = spatial_attempt_key;
        }
        if (!spatial_receiver_key.empty()) {
            (*m_pending_spatial_orientation)["receiver_fingerprint"] = spatial_receiver_key;
        }
        if (preview_key) {
            (*m_pending_spatial_orientation)["preview_fingerprint"] = *preview_key;
        }
        if (spatial_drop_key) {
            (*m_pending_spatial_orientation)["drop_fingerprint"] = *spatial_drop_key;
        }
        std::string field_signature;
        for (const auto& [unit, placed] : m_auto_deploy_positions) {
            field_signature += unit + (m_policy.owned.at(unit).elite ? "#elite\n" : "#initial\n");
        }
        (*m_pending_spatial_orientation)["field_signature"] = std::move(field_signature);
        if (!displaced.empty()) {
            const auto other = m_auto_deploy_positions.find(displaced)->second;
            (*m_pending_spatial_orientation)["displaced_x"] = other.first;
            (*m_pending_spatial_orientation)["displaced_y"] = other.second;
        }
        // Journal intent before any held input so a process interruption can
        // resume by observation instead of repeating the rotation.
        auto intent = basic_info_with_what("SPASpatialOrientationPending");
        intent["details"] = json::object { { "run_id", m_run_id },
                                           { "round", round },
                                           { "name", plan->name },
                                           { "roster", roster_snapshot() },
                                           { "field_positions", field_position_snapshot() },
                                           { "pending_orientation", *m_pending_spatial_orientation },
                                           { "phase", "before_held_input" },
                                           { "held_input_will_be_replayed", false } };
        callback(AsstMsg::SubTaskExtraInfo, intent);
        const bool configured = executor->configure(options);
        if (configured) {
            if (m_mode == "joint") {
                executor->set_spatial_hold_guard(
                    [allows_spatial_input] { return allows_spatial_input(spa::SpatialPreparationPhase::BeforeHold); });
            }
            executor->set_registered_grid_context(
                m_spatial_grid_reference,
                m_spatial_grid_projection,
                m_spatial_map_id);
            executor->set_spatial_target_callback([this, round](const Point& destination) {
                if (!m_pending_spatial_orientation) {
                    return;
                }
                (*m_pending_spatial_orientation)["target_x"] = destination.x;
                (*m_pending_spatial_orientation)["target_y"] = destination.y;
                auto journal = basic_info_with_what("SPASpatialOrientationPending");
                journal["details"] = json::object { { "run_id", m_run_id },
                                                    { "round", round },
                                                    { "roster", roster_snapshot() },
                                                    { "field_positions", field_position_snapshot() },
                                                    { "pending_orientation", *m_pending_spatial_orientation },
                                                    { "phase", "before_field_release" },
                                                    { "held_input_will_be_replayed", false } };
                callback(AsstMsg::SubTaskExtraInfo, journal);
            });
        }
        const auto transaction_started = std::chrono::steady_clock::now();
        const bool executed = configured && executor->run();
        const auto transaction_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - transaction_started)
                                        .count();
        if (!executor->observed_boss_attackable_cells().empty()) {
            m_boss_attackable_cells = executor->observed_boss_attackable_cells();
            m_boss_attackable_map_id = m_spatial_map_id;
            m_boss_attackable_round = round;
        }
        const bool deadline_deferred = executor->spatial_hold_deferred();
        if (!deadline_deferred) {
            ++m_dynamic_layout_attempts[round];
            if (executor->reorientation_started()) {
                m_spatial_orientation_attempts.emplace(round, plan->name);
            }
        }
        if (!executor->reorientation_started()) {
            if (!deadline_deferred && !spatial_attempt_key.empty()) {
                m_failed_spatial_actions.insert(spatial_attempt_key);
            }
            if (layout_step) {
                if (!deadline_deferred) {
                    m_failed_layout_context = m_dynamic_layout_context + std::to_string(round);
                }
                m_dynamic_layout_target.clear();
                m_dynamic_layout_expected_cells.clear();
            }
            if (!deadline_deferred) {
                m_spatial_orientation_attempts.emplace(round, plan->name);
            }
            m_pending_spatial_orientation.reset();
            auto deferred = basic_info_with_what("SPASpatialOrientationDeferred");
            deferred["details"] = json::object { { "run_id", m_run_id },
                                                 { "round", round },
                                                 { "name", plan->name },
                                                 { "roster", roster_snapshot() },
                                                 { "field_positions", field_position_snapshot() },
                                                 { "pending_orientation", nullptr },
                                                 { "failed_spatial_actions", failed_spatial_action_snapshot() },
                                                 { "transaction_ms", transaction_ms },
                                                 { "deadline_deferred", deadline_deferred },
                                                 { "held_input_started", false } };
            callback(AsstMsg::SubTaskExtraInfo, deferred);
            return false;
        }
        (*m_pending_spatial_orientation)["transaction_ms"] = transaction_ms;
        m_pending_orientation_verifier = executor;
        m_spatial_reconciliation_attempts[round] = 1;
        static_cast<void>(executed);
    }
    const auto& projected = executor->reoriented_field_members();
    const auto name = m_pending_spatial_orientation->get("name", std::string());
    const int transaction_ms = m_pending_spatial_orientation->get("transaction_ms", -1);
    if (!executor->field_pose_verified() || projected.size() != m_auto_deploy_positions.size()) {
        m_growth_population_poses.clear();
        const std::pair original { m_pending_spatial_orientation->get("x", -1),
                                   m_pending_spatial_orientation->get("y", -1) };
        m_verified_field_pose_points.erase(original);
        if (!m_pending_spatial_orientation->get("displaced", std::string()).empty()) {
            m_verified_field_pose_points.erase(
                { m_pending_spatial_orientation->get("displaced_x", -1),
                  m_pending_spatial_orientation->get("displaced_y", -1) });
        }
        auto pending = basic_info_with_what("SPASpatialOrientationPending");
        pending["details"] = json::object { { "run_id", m_run_id },
                                            { "round", round },
                                            { "name", name },
                                            { "roster", roster_snapshot() },
                                            { "field_positions", field_position_snapshot() },
                                            { "pending_orientation", *m_pending_spatial_orientation },
                                            { "resumable", true },
                                            { "held_input_will_be_replayed", false } };
        callback(AsstMsg::SubTaskExtraInfo, pending);
        return false;
    }
    std::multimap<std::string, std::pair<int, int>> positions;
    std::map<std::pair<int, int>, char> facings;
    for (const auto& member : projected) {
        const std::pair point { member.x, member.y };
        positions.emplace(member.name, point);
        // The executor preserves the old facing after a cancelled drag and
        // clears it after an uncertain direction input. A missing *new*
        // direction commit must not erase its reconciled facing evidence.
        if (member.facing != '\0') {
            facings.emplace(point, member.facing);
        }
    }
    m_auto_deploy_positions = std::move(positions);
    m_growth_population_poses = m_auto_deploy_positions;
    m_committed_facings = std::move(facings);
    if (executor->observed_grid_members().size() == m_auto_deploy_positions.size() &&
        !executor->observed_grid_reference().empty() && !executor->observed_grid_projection().empty()) {
        m_population_grid_members = executor->observed_grid_members();
        m_spatial_grid_reference = executor->observed_grid_reference().clone();
        m_spatial_grid_projection = executor->observed_grid_projection().clone();
        m_row_growth_populations =
            spa::same_row_growth_populations(m_population_grid_members).value_or(std::map<std::string, int>());
        if (!executor->verified_map_id().empty()) {
            m_spatial_map_id = executor->verified_map_id();
        }
    }
    else {
        // Field identities alone do not update the old logical-grid camera.
        // Reacquire it before another front/back or same-row decision.
        m_population_grid_members.clear();
        m_growth_population_poses.clear();
        m_row_growth_populations.clear();
    }
    m_verified_field_pose_points.clear();
    m_auto_deploy_used_cells.clear();
    for (const auto& [unit, point] : m_auto_deploy_positions) {
        m_verified_field_pose_points.insert(point);
        m_auto_deploy_used_cells.insert(point);
    }
    const auto displaced = m_pending_spatial_orientation->get("displaced", std::string());
    const bool relocation = m_pending_spatial_orientation->get("relocation", false);
    if (relocation ? !executor->relocation_applied() : !executor->committed_facing()) {
        if (m_pending_spatial_orientation->get("dynamic_layout", false)) {
            const auto drop = m_pending_spatial_orientation->get("drop_fingerprint", std::string());
            if (executor->spatial_drop_returned_without_direction() && !drop.empty()) {
                m_failed_spatial_actions.insert(drop);
            }
            else {
                m_failed_layout_context = m_dynamic_layout_context + std::to_string(round);
            }
            m_dynamic_layout_target.clear();
            m_dynamic_layout_expected_cells.clear();
        }
        const auto failed_action = m_pending_spatial_orientation->get("attempt_fingerprint", std::string());
        if (!failed_action.empty()) {
            m_failed_spatial_actions.insert(failed_action);
        }
        if (executor->spatial_preview_failed()) {
            const auto preview = m_pending_spatial_orientation->get("preview_fingerprint", std::string());
            if (!preview.empty()) {
                m_failed_spatial_actions.insert(preview);
            }
        }
        if (executor->spatial_receiver_failed()) {
            const auto receiver_failure = m_pending_spatial_orientation->get("receiver_fingerprint", std::string());
            if (!receiver_failure.empty()) {
                m_failed_spatial_actions.insert(receiver_failure);
            }
        }
        m_spatial_orientation_attempts.emplace(round, name);
    }
    m_auto_deploy_blocked_names.erase(name);
    if (!displaced.empty()) {
        m_auto_deploy_blocked_names.erase(displaced);
    }
    m_pending_spatial_orientation.reset();
    m_pending_orientation_verifier.reset();
    auto receipt = basic_info_with_what("SPASpatialOrientationReconciled");
    receipt["details"] =
        json::object { { "run_id", m_run_id },
                       { "round", round },
                       { "name", name },
                       { "relocation_applied", executor->relocation_applied() },
                       { "transaction_ms", transaction_ms },
                       { "receiver_failed_before_direction", executor->spatial_receiver_failed() },
                       { "drop_returned_without_direction", executor->spatial_drop_returned_without_direction() },
                       { "vacant_preview_failed", executor->spatial_preview_failed() },
                       { "displaced", displaced },
                       { "roster", roster_snapshot() },
                       { "field_positions", field_position_snapshot() },
                       { "used_deploy_points", deployment_point_snapshot() },
                       { "pending_orientation", nullptr },
                       { "failed_spatial_actions", failed_spatial_action_snapshot() },
                       { "direction_input_committed", executor->committed_facing().has_value() },
                       { "population_preserved", true },
                       { "facing_pixel_verified", false },
                       { "other_coordinates_from_camera_registration", true } };
    callback(AsstMsg::SubTaskExtraInfo, receipt);
    return true;
}

json::array asst::StrongholdProtocolTask::failed_spatial_action_snapshot() const
{
    json::array result;
    // Preserve a bounded set without allowing a damaged checkpoint or a
    // long development session to grow an unbounded recovery payload.
    const auto first = m_failed_spatial_actions.size() > 512
                           ? std::next(m_failed_spatial_actions.begin(), m_failed_spatial_actions.size() - 512)
                           : m_failed_spatial_actions.begin();
    for (auto it = first; it != m_failed_spatial_actions.end(); ++it) {
        result.emplace_back(*it);
    }
    return result;
}

bool asst::StrongholdProtocolTask::handle_fear_trap_instruction(const json::value& state)
{
    const int round = state.get("round", -1);
    if (m_selected_strategy_id != "band_dance" || round < 1 || !state.get("round_known", false) ||
        m_pending_spatial_orientation || m_pending_field_replacement || m_field_checkpoint_loaded ||
        m_growth_population_poses != m_auto_deploy_positions ||
        m_population_grid_members.size() != m_auto_deploy_positions.size() || m_population_grid_members.empty() ||
        (m_mode == "joint" && state.get("preparation_seconds_remaining", 0) < 15)) {
        return false;
    }
    if (round != m_fear_trap_round) {
        m_fear_trap_round = round;
        m_fear_trap_cells.clear();
        m_fear_trap_unverified = false;
    }
    const int maximum = round >= 12 ? 4 : round >= 8 ? 3 : round >= 4 ? 2 : 1;
    if (m_fear_trap_unverified || m_fear_trap_cells.size() >= static_cast<size_t>(maximum)) {
        return false;
    }
    const auto read_charges = [maximum](const cv::Mat& image) -> std::optional<int> {
        if (image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3) {
            return std::nullopt;
        }
        if (!spa::active_strategy_ready(image)) {
            return 0;
        }
        // On the first three rounds the visible active button has no numeral:
        // there is only one charge, so the game does not draw a badge.
        if (maximum == 1) {
            return 1;
        }
        const auto count = spa::shop_number(image, Rect(1221, 260, 19, 19), .80);
        return count && *count > 0 && *count <= maximum ? count : std::nullopt;
    };
    const auto before = ctrler()->get_image();
    const auto terrain = spa::terrain_layout_model(m_spatial_map_id, m_spatial_boss_phase);
    if (!terrain || !spa::active_strategy_ready(before) || m_spatial_grid_reference.empty()) {
        return false;
    }
    const auto charges_before = read_charges(before);
    if (!charges_before || *charges_before == 0) {
        return false;
    }
    const auto project = [&](const cv::Mat& image) {
        return spa::registered_grid_projection(m_spatial_grid_reference, image, m_spatial_grid_projection);
    };
    const auto normal_grid = project(before);
    if (!normal_grid) {
        return false;
    }
    std::vector<spa::DeploymentUnit> field;
    for (const auto& member : m_population_grid_members) {
        const auto owned = m_policy.owned.find(member.name);
        const auto forms = owned == m_policy.owned.end() ? std::nullopt : spa::owned_form_counts(owned->second);
        const auto stats = forms && (forms->initial == 0 || forms->elite == 0)
                               ? spa::season_combat_stats(member.name, forms->elite > 0)
                               : std::nullopt;
        std::vector<spa::BoardCell> range;
        if (stats) {
            range.assign(stats->basic_range.begin(), stats->basic_range.end());
        }
        auto unit = describe_deployed_unit(member, range, false);
        if (!stats) {
            unit.block_count = 0;
        }
        field.push_back(std::move(unit));
    }
    const auto send = [&](InputEvent::Type type, Point point = {}) {
        InputEvent event;
        event.type = type;
        event.point = point;
        return ctrler()->inject_input_event(event);
    };
    const auto move = [&](Point point) {
        return send(InputEvent::Type::TOUCH_MOVE, point) && send(InputEvent::Type::COMMIT);
    };
    const auto release = [&] {
        const bool up = send(InputEvent::Type::TOUCH_UP);
        return send(InputEvent::Type::COMMIT) && up;
    };
    const Point source(1230, 235);
    bool held = send(InputEvent::Type::TOUCH_DOWN, source) && send(InputEvent::Type::COMMIT) && sleep(280);
    // Stay to the right of the battlefield until the valid-tile overlay is visible.
    for (int step = 1; held && step <= 4; ++step) {
        held = move(Point(source.x - 15 * step, source.y + 27 * step)) && sleep(45);
    }
    const auto preview = held ? ctrler()->get_image() : cv::Mat {};
    const auto held_grid = held ? project(preview) : std::nullopt;
    const auto project_cell = [](spa::BoardCell cell, const cv::Mat& grid) {
        const std::vector<cv::Point2f> logical { { static_cast<float>(cell.first), static_cast<float>(cell.second) } };
        std::vector<cv::Point2f> projected;
        cv::perspectiveTransform(logical, projected, grid);
        return projected.front();
    };
    std::optional<std::pair<spa::BoardCell, Point>> target;
    std::optional<std::pair<spa::BoardCell, Point>> hover_target;
    std::optional<std::tuple<int, int, int, int>> best_score, best_hover_score;
    if (held_grid) {
        const auto lanes = spa::active_preparation_lanes(*terrain, round);
        for (const auto& cell : terrain->ground_route) {
            if (m_confirmed_summon_cells.contains({ round, cell })) {
                continue;
            }
            const auto score = spa::fear_trap_route_priority(cell, lanes, field, m_fear_trap_cells);
            if (!score || (score <= best_score && score <= best_hover_score)) {
                continue;
            }
            const auto point = project_cell(cell, *held_grid);
            const auto previous = project_cell(cell, *normal_grid);
            const cv::Rect2f bounds(180, 145, 820, 355);
            if (!bounds.contains(point) || !bounds.contains(previous)) {
                continue;
            }
            const cv::Point center(point);
            if (score > best_score && spa::green_deployment_hint(preview, center) &&
                !spa::green_deployment_hint(before, cv::Point(previous))) {
                best_score = score;
                target = std::pair(cell, Point(center.x, center.y));
            }
            if (score > best_hover_score &&
                spa::terrain_allows_deployment(m_spatial_map_id, cell, true, m_spatial_boss_phase)) {
                best_hover_score = score;
                hover_target = std::pair(cell, Point(center.x, center.y));
            }
        }
    }
    bool hover_prompt_verified = false;
    if (!target && hover_target) {
        // The instruction can prompt for a tile without drawing the
        // operator deployment overlay. Only probe a registered, empty
        // ground cell when the game explicitly requests a target.
        OCRer prompt(preview, Rect(450, 128, 390, 48));
        const auto labels = prompt.analyze();
        hover_prompt_verified = labels && std::ranges::any_of(*labels, [](const auto& label) {
                                    return label.score >= .85 && label.text == "请指向一格可用的地块";
                                });
        if (hover_prompt_verified) {
            target = hover_target;
        }
    }
    bool moved = held && target.has_value();
    if (moved) {
        const Point staging(source.x - 60, source.y + 108);
        for (int step = 1; moved && step <= 8; ++step) {
            moved = move(Point(
                        staging.x + (target->second.x - staging.x) * step / 8,
                        staging.y + (target->second.y - staging.y) * step / 8)) &&
                    sleep(35);
        }
    }
    bool hovered_tile_verified = false;
    cv::Mat hover;
    if (moved && hover_prompt_verified) {
        hover = sleep(150) ? ctrler()->get_image() : cv::Mat {};
        const auto hovered_grid =
            hover.empty() ? std::nullopt : spa::registered_grid_projection(preview, hover, *held_grid);
        const cv::Point2f cursor(static_cast<float>(target->second.x), static_cast<float>(target->second.y));
        moved = hovered_grid && cv::norm(project_cell(target->first, *hovered_grid) - cursor) <= 8;
        hovered_tile_verified = moved;
    }
    if (!moved) {
        static_cast<void>(move(Point(20, 300)));
    }
    const bool released = release();
    const bool issued = moved && released;
    if (issued) {
        auto input = basic_info_with_what("SPAActiveStrategyInputIssued");
        input["details"] = json::object { { "round", round },
                                          { "strategy", "溃心领域" },
                                          { "col", target->first.first },
                                          { "row", target->first.second },
                                          { "effect_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, input);
    }
    const auto after = sleep(500) ? ctrler()->get_image() : cv::Mat {};
    const auto charges_after = issued ? read_charges(after) : std::nullopt;
    bool verified = false;
    if (charges_after && *charges_after == *charges_before - 1 &&
        spa::preparation_round_number(after, round) == round) {
        OCRer ready(after, Rect(1100, 70, 180, 85));
        const auto labels = ready.analyze();
        verified = labels && std::any_of(labels->begin(), labels->end(), [](const auto& label) {
                       return label.score >= .90 && label.text == "准备就绪";
                   });
    }
    if (verified) {
        m_fear_trap_cells.insert(target->first);
    }
    else {
        m_fear_trap_unverified = true;
    }
    auto evidence = basic_info_with_what("SPAFearTrapPlacement");
    evidence["details"] = json::object { { "round", round },
                                         { "map_id", m_spatial_map_id },
                                         { "charges_before", *charges_before },
                                         { "charges_after", charges_after.value_or(-1) },
                                         { "held_grid_verified", held_grid.has_value() },
                                         { "legal_route_tile", best_score.has_value() || verified },
                                         { "target_col", target ? target->first.first : -1 },
                                         { "target_row", target ? target->first.second : -1 },
                                         { "hover_prompt_verified", hover_prompt_verified },
                                         { "hover_registered", hovered_tile_verified },
                                         { "input_issued", issued },
                                         { "charge_spent_verified", verified } };
    callback(AsstMsg::SubTaskExtraInfo, evidence);
    if (!hover.empty()) {
        spa::save_evidence(hover, utils::path("debug/SPA/fear_trap_hover"), false);
    }
    if (verified) {
        spa::save_evidence(after, utils::path("debug/SPA/fear_trap_placed"), false);
    }
    else {
        spa::save_evidence(preview, utils::path("debug/SPA/fear_trap_held"), false);
        spa::save_evidence(after, utils::path("debug/SPA/fear_trap_unverified"), false);
    }
    return true;
}
