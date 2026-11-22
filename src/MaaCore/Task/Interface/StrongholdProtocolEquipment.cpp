#include "Config/Miscellaneous/BattleDataConfig.h"
#include "StrongholdProtocolTask.h"
#include "Task/MiniGame/SPA/SPAAcquisition.h"
#include "Task/MiniGame/SPA/SPABattleRole.h"
#include "Task/MiniGame/SPA/SPACashEquipmentEvidence.h"
#include "Task/MiniGame/SPA/SPACombatStats.h"
#include "Task/MiniGame/SPA/SPADirectPromotionPolicy.h"
#include "Task/MiniGame/SPA/SPAFormation.h"
#include "Task/MiniGame/SPA/SPAGrowth.h"
#include "Task/MiniGame/SPA/SPAOperatorCatalogue.h"
#include "Task/MiniGame/SPA/SPAPlanner.h"
#include "Task/MiniGame/SPA/SPAStrategyEligibility.h"
#include "Task/MiniGame/SPA/SPASummonPolicy.h"
#include "Task/MiniGame/SPA/SPASwapTaskPlugin.h"
#include "Task/MiniGame/SPA/SPATerrainModel.h"
#include <algorithm>
#include <iterator>
#include <utility>

bool asst::StrongholdProtocolTask::reconcile_tactical_recruitment(const json::value& state)
{
    const int round = state.get("round", -1);
    const int funds = state.get("funds", -1);
    const int remaining = state.get("remaining_deployment", -1);
    if (round != m_pending_tactical_recruitment_round || funds < 0 || remaining < 0 || remaining > 8 ||
        !state.get("round_known", false) || !state.get("funds_known", false) ||
        !state.get("remaining_deployment_known", false) || state.get("remaining_deployment_inferred", false) ||
        m_pending_field_replacement || m_pending_spatial_orientation || m_bench_recount_pending ||
        !m_pending_promotion.empty() || !m_pending_pact_recruitment.empty()) {
        return false;
    }

    // The confirmed tactic is the only allowed roster-changing input between
    // the prior field/bench ledger and this observation.
    const auto catalogue = spa::operator_pacts();
    spa::ObservedOperatorInventory before;
    std::map<std::string, bool> unchanged_forms;
    int old_bench_count = 0;
    for (const auto& [name, unit] : m_policy.owned) {
        const auto forms = spa::owned_form_counts(unit);
        const auto field_count = static_cast<int>(m_auto_deploy_positions.count(name));
        if (!forms || !catalogue.contains(name) || field_count > forms->initial + forms->elite ||
            unit.deployed != (field_count > 0)) {
            return false;
        }
        before.members.emplace(name, *forms);
        const auto& pacts = catalogue.at(name);
        if ((forms->initial == 0 || forms->elite == 0) &&
            std::ranges::find(pacts, m_pending_tactical_recruitment) == pacts.end()) {
            // This grant cannot affect a different pact's known single form.
            // Its name and physical quantity are still checked in the new scan.
            unchanged_forms.emplace(name, forms->elite > 0);
        }
        old_bench_count += forms->initial + forms->elite - field_count;
    }
    const int observed_field_count = 8 - remaining;
    if (old_bench_count != m_policy.bench_occupied ||
        observed_field_count > static_cast<int>(m_auto_deploy_positions.size()) ||
        !std::ranges::all_of(m_auto_deploy_positions, [&](const auto& entry) {
            return before.members.contains(entry.first) && m_verified_field_pose_points.contains(entry.second);
        })) {
        return false;
    }
    before.complete = true;

    spa::SwapTaskPlugin probe(m_callback, m_inst, TaskType);
    probe.set_retry_times(0);
    probe.set_task_id(m_task_id);
    probe.set_task_ptr(this);
    const auto bench = probe.inspect_bench_operator_forms(round, funds, remaining, unchanged_forms);
    if (!bench) {
        return false;
    }
    spa::ObservedOperatorInventory observed_bench;
    for (const auto& [name, elite] : *bench) {
        auto& forms = observed_bench.members[name];
        ++(elite ? forms.elite : forms.initial);
    }

    auto after = observed_bench;
    decltype(m_auto_deploy_positions) surviving_field;
    const auto compare = [&](bool check_possible_promotions) {
        after = observed_bench;
        surviving_field.clear();
        for (const auto& [name, forms] : before.members) {
            const auto [begin, end] = m_auto_deploy_positions.equal_range(name);
            const auto field_count = static_cast<int>(std::distance(begin, end));
            if (field_count == 0) {
                continue;
            }
            const bool ambiguous_field_forms =
                forms.initial > 0 && forms.elite > 0 && field_count != forms.initial + forms.elite;
            const bool could_merge =
                forms.initial + 1 == spa::promotion_copies_required(name) &&
                std::ranges::find(catalogue.at(name), m_pending_tactical_recruitment) != catalogue.at(name).end();
            auto& count = after.members[name];
            if (m_field_checkpoint_loaded || ambiguous_field_forms || (check_possible_promotions && could_merge)) {
                // Revalidate the clicked field identity after restoring a
                // checkpoint. A one-copy grant cannot change a known single
                // form unless that operator can synthesize.
                const auto unchanged_form = !could_merge && (forms.initial == 0 || forms.elite == 0)
                                                ? std::optional(forms.elite > 0)
                                                : std::nullopt;
                for (auto it = begin; it != end; ++it) {
                    const auto& [x, y] = it->second;
                    if (const auto elite =
                            probe.inspect_field_operator_form(name, Point(x, y), round, funds, remaining, unchanged_form)) {
                        ++(*elite ? count.elite : count.initial);
                        surviving_field.emplace(*it);
                    }
                }
            }
            else {
                const int on_field_initial = forms.elite == 0 ? field_count : forms.initial == 0 ? 0 : forms.initial;
                count.initial += on_field_initial;
                count.elite += field_count - on_field_initial;
                surviving_field.insert(begin, end);
            }
        }
        after.complete = surviving_field.size() == static_cast<std::size_t>(observed_field_count);
        return spa::verify_pact_recruitment_delta(before, after, m_pending_tactical_recruitment, 1);
    };
    auto delta = compare(m_field_checkpoint_loaded);
    if (!delta.verified && !m_field_checkpoint_loaded) {
        delta = compare(true);
    }
    if (!delta.verified || delta.gained_initial_operators.size() != 1) {
        LogWarn << "SPA tactical recruitment roster delta is unverified" << round << m_pending_tactical_recruitment
                << delta.reason;
        return false;
    }
    const auto& name = delta.gained_initial_operators.front();
    const auto facts =
        std::ranges::find_if(spa::OperatorCatalogue, [&](const auto& entry) { return entry.name == name; });
    if (m_policy.shop_level < 1 || facts == spa::OperatorCatalogue.end() || facts->tier > m_policy.shop_level) {
        return false;
    }
    auto reconciled = m_policy.owned;
    if (!spa::set_owned_form_counts(reconciled[name], after.members.at(name))) {
        return false;
    }
    reconciled[name].deployed = surviving_field.contains(name);
    const bool promoted = !delta.promoted_operators.empty();
    m_policy.owned = std::move(reconciled);
    m_policy.round = round;
    m_policy.bench_occupied = static_cast<int>(bench->size());
    m_auto_deploy_positions = std::move(surviving_field);
    m_auto_deploy_used_cells.clear();
    for (const auto& [member, point] : m_auto_deploy_positions) {
        m_auto_deploy_used_cells.insert(point);
        if (promoted && member == name) {
            m_committed_facings.erase(point);
        }
    }
    std::erase_if(m_verified_field_pose_points, [&](const auto& point) {
        return !m_auto_deploy_used_cells.contains(point);
    });
    std::erase_if(m_committed_facings, [&](const auto& entry) {
        return !m_auto_deploy_used_cells.contains(entry.first);
    });
    m_growth_population_poses.clear();
    m_population_grid_members.clear();
    m_row_growth_populations.clear();
    if (promoted) {
        if (const auto equipped = m_equipped_items.find(name); equipped != m_equipped_items.end()) {
            m_equipment_candidates.insert(equipped->second.begin(), equipped->second.end());
            m_equipped_items.erase(equipped);
            m_equipment_inventory_dirty = true;
            m_equipment_inventory_scan_rounds.erase(round);
        }
    }
    else if (!before.members.contains(name)) {
        m_equipped_items[name] = {};
    }
    record_acquired_equipment_hints(name, promoted, round);
    m_recent_bench_acquisition = name;
    m_unavailable_replacement_sources.erase(name);
    const auto pact = std::exchange(m_pending_tactical_recruitment, {});
    m_pending_tactical_recruitment_round = 0;
    auto receipt = basic_info_with_what("SPATacticalRecruitmentReconciled");
    receipt["details"] = json::object { { "run_id", m_run_id },
                                        { "round", round },
                                        { "pact", pact },
                                        { "name", name },
                                        { "automatic_synthesis_observed", promoted },
                                        { "tactical_recruitment_pending", std::string() },
                                        { "tactical_recruitment_round", 0 },
                                        { "bench_population_verified", true },
                                        { "bench_occupied", m_policy.bench_occupied },
                                        { "bench_recount_pending", false },
                                        { "field_population_verified", true },
                                        { "field_positions", field_position_snapshot() },
                                        { "used_deploy_points", deployment_point_snapshot() },
                                        { "roster", roster_snapshot() },
                                        { "equipment_slots", equipment_slots_snapshot() },
                                        { "equipment_candidates", equipment_candidates_snapshot() },
                                        { "run_victory_verified", false } };
    callback(AsstMsg::SubTaskExtraInfo, receipt);
    return true;
}

void asst::StrongholdProtocolTask::record_acquired_equipment_hints(const std::string& name, bool elite, int round)
{
    const auto hints = spa::acquired_equipment_hints(name, elite);
    const auto effects = spa::acquired_effects(name, elite, m_policy.shop_level);
    const bool discovery_required = effects && std::any_of(effects->begin(), effects->end(), [](const auto& effect) {
                                        return effect.kind == spa::AcquiredEffectKind::Equipment && effect.amount > 0;
                                    });
    if (discovery_required) {
        // A random gift invalidates the inventory just as a fixed gift does.
        // Its family is not a verified item name or quantity.
        m_equipment_inventory_dirty = true;
        m_equipment_inventory_scan_rounds.erase(round);
    }
    if (hints.empty() && !discovery_required) {
        return;
    }
    json::array names;
    for (const auto& item : hints) {
        m_equipment_candidates.insert(item);
        names.emplace_back(item);
    }
    auto evidence = basic_info_with_what("SPAAcquiredEquipmentHints");
    evidence["details"] = json::object { { "operator", name },
                                         { "elite", elite },
                                         { "round", round },
                                         { "discovery_required", discovery_required },
                                         { "candidate_names", std::move(names) },
                                         { "inventory_presence_verified", false },
                                         { "inventory_count_inferred", false } };
    callback(AsstMsg::SubTaskExtraInfo, evidence);
}

bool asst::StrongholdProtocolTask::refresh_equipment_inventory(const json::value& state)
{
    if (m_mode == "joint" && state.get("preparation_seconds_remaining_known", false) &&
        state.get("preparation_seconds_remaining", 0) <= 25) {
        return false;
    }
    const int round = state.get("round", -1);
    const bool scheduled_strategy_gift = spa::strategy_grants_preparation_equipment(m_selected_strategy_id, round);
    const bool preparation_gifts = std::any_of(m_policy.owned.begin(), m_policy.owned.end(), [&](const auto& item) {
        if (!item.second.deployed) {
            return false;
        }
        const auto equipped = m_equipped_items.find(item.first);
        return item.first == "酒神" || item.first == "异客" ||
               std::any_of(
                   spa::SummonRules.begin(),
                   spa::SummonRules.end(),
                   [&](const auto& rule) { return rule.preparation_executor && rule.owner == item.first; }) ||
               (item.first == "白铁" && equipped != m_equipped_items.end() && !equipped->second.empty());
    });
    if (!m_auto_run || !m_policy.dynamic_decision_mode || m_decision_only ||
        (!m_equipment_inventory_dirty && !preparation_gifts && !scheduled_strategy_gift && !m_pending_cash_equipment) ||
        m_equipment_inventory_scan_rounds.contains(round) || state.get("page", std::string()) != "preparation" ||
        !state.get("round_known", false) || !state.get("funds_known", false) ||
        !state.get("remaining_deployment_known", false) || m_pending_spatial_orientation ||
        m_pending_field_replacement || m_field_checkpoint_loaded || m_auto_deploy_positions.empty() ||
        (state.get("remaining_deployment", 0) > 0 && m_policy.bench_occupied > 0)) {
        return false;
    }
    m_equipment_inventory_scan_rounds.insert(round);
    spa::SwapTaskPlugin inspect(m_callback, m_inst, TaskType);
    inspect.set_retry_times(0);
    inspect.set_task_id(m_task_id);
    inspect.set_task_ptr(this);
    const bool probes_complete = inspect.inspect_equipment_inventory(
        round,
        state.get("funds", -1),
        state.get("remaining_deployment", -1),
        m_inventory_operator_hints);
    for (const auto& [name, point] : inspect.inspected_equipment()) {
        m_equipment_candidates.insert(name);
        m_equipment_source_hints[name] = point.x;
        if (name == "盟约之币") {
            // This list contains titles actually read during this scan, not
            // cached candidates or the operator visual hints skipped above.
            m_cash_equipment_source_verified_round = round;
        }
        else if (name == "定期存折") {
            m_passbook_source_verified_round = round;
        }
    }
    m_observed_bench_summons.clear();
    for (const auto& [name, point] : inspect.inspected_summons()) {
        if (spa::deployable_summon_owner(name).empty()) {
            continue;
        }
        if (std::count_if(
                inspect.inspected_summons().begin(),
                inspect.inspected_summons().end(),
                [&](const auto& slot) { return slot.first == name; }) == 1) {
            m_observed_bench_summons.emplace(name, std::pair(round, point));
        }
    }
    // A completed discovery pass does not make the inventory dirty merely
    // because it found an item. Acquisitions, returned gear and consumption
    // invalidate it explicitly. Keep presence-only hints and never infer
    // stack quantities or completeness from this scheduling flag.
    m_equipment_inventory_dirty = !probes_complete;
    const bool funding_source_observed =
        (m_cash_equipment_source_verified_round == round && !m_cash_equipment_attempted_rounds.contains(round)) ||
        (m_passbook_source_verified_round == round && !m_passbook_attempted_rounds.contains(round));
    const bool reopen_shopping = m_shopping_complete && state.get("funds", 0) > 0 && funding_source_observed;
    if (reopen_shopping) {
        // Finalization can be the first scan to identify a funding item.
        // Reevaluate the current shop before Ready consumes the round balance.
        m_shopping_complete = false;
        m_spending_reviewed = false;
        m_spending_reason.clear();
    }
    auto receipt = basic_info_with_what("SPAAutomationEquipmentInventoryObserved");
    receipt["details"] =
        json::object { { "run_id", m_run_id },
                       { "round", round },
                       { "scheduled_strategy_equipment_grant", scheduled_strategy_gift },
                       { "detected_candidates_inspected", probes_complete },
                       { "equipment_slots_observed", inspect.inspected_equipment().size() },
                       { "summon_slots_observed", inspect.inspected_summons().size() },
                       { "inventory_complete", false },
                       { "inventory_count_inferred", false },
                       { "preparation_frame_invalidated", true },
                       { "cash_equipment_source_verified_round", m_cash_equipment_source_verified_round },
                       { "shopping_reopened_for_funding_review", reopen_shopping },
                       { "equipment_candidates", equipment_candidates_snapshot() },
                       { "roster", roster_snapshot() } };
    callback(AsstMsg::SubTaskExtraInfo, receipt);
    // Inspection clicks can open panels or change a HUD control even when
    // no item is recognized. Never reuse pre-inspection money/freeze/page.
    return true;
}

bool asst::StrongholdProtocolTask::handle_preparation_summons(const json::value& state)
{
    const int round = state.get("round", -1);
    const int remaining = state.get("remaining_deployment", -1);
    if (round < 1 || remaining < 0 || remaining > 8 || !m_auto_run || m_decision_only || m_stop_before_battle ||
        m_mode == "joint" && state.get("preparation_seconds_remaining", 0) < 30 || m_pending_spatial_orientation ||
        m_pending_field_replacement || m_field_checkpoint_loaded ||
        m_growth_population_poses != m_auto_deploy_positions ||
        m_population_grid_members.size() != static_cast<size_t>(8 - remaining)) {
        return false;
    }
    const auto terrain = spa::terrain_layout_model(m_spatial_map_id, m_spatial_boss_phase);
    if (!terrain || m_observed_bench_summons.empty()) {
        return false;
    }
    for (const auto& [summon, observed] : m_observed_bench_summons) {
        if (observed.first != round || m_preparation_summon_attempts.contains({ round, summon })) {
            continue;
        }
        const auto owner_name = spa::deployable_summon_owner(summon);
        const auto owner =
            std::find_if(m_population_grid_members.begin(), m_population_grid_members.end(), [&](const auto& member) {
                return member.name == owner_name;
            });
        if (owner == m_population_grid_members.end() || m_auto_deploy_positions.count(owner->name) != 1 ||
            !m_verified_field_pose_points.contains(m_auto_deploy_positions.find(owner->name)->second) ||
            !m_policy.owned.contains(owner->name)) {
            continue;
        }
        auto verified_owner = *owner;
        const auto point = m_auto_deploy_positions.find(owner->name)->second;
        if (const auto facing = m_committed_facings.find(point); facing != m_committed_facings.end()) {
            verified_owner.facing = facing->second;
        }
        else {
            verified_owner.facing = '\0';
        }
        const auto stats = spa::season_combat_stats(verified_owner.name, m_policy.owned.at(verified_owner.name).elite);
        if (!stats) {
            continue;
        }
        const std::vector<spa::BoardCell> range(stats->basic_range.begin(), stats->basic_range.end());
        const auto lanes = spa::active_preparation_lanes(*terrain, round);
        auto cells = spa::summon_placement_cells(
            summon,
            verified_owner,
            range,
            terrain->buildable,
            m_population_grid_members,
            lanes,
            true);
        std::erase_if(cells, [&](const auto& cell) { return m_confirmed_summon_cells.contains({ round, cell }); });
        if (owner_name == "白铁") {
            std::map<spa::BoardCell, int> charging_allies;
            for (const auto& ally : m_population_grid_members) {
                const auto props = BattleData.find_first_oper(battle::Role::Unknown, ally.name, true);
                const auto owned = m_policy.owned.find(ally.name);
                if (!props || !spa::has_basic_attack(props->role, props->sub_role) || owned == m_policy.owned.end()) {
                    continue;
                }
                const auto combat = spa::season_combat_stats(ally.name, owned->second.elite);
                if (!combat) {
                    continue;
                }
                const std::vector<spa::BoardCell> ally_range(combat->basic_range.begin(), combat->basic_range.end());
                for (const auto& cell : cells) {
                    if (spa::grid_range_covers(ally.col, ally.row, ally.facing, cell.first, cell.second, ally_range)) {
                        ++charging_allies[cell];
                    }
                }
            }
            std::erase_if(cells, [&](const auto& cell) { return !charging_allies.contains(cell); });
            std::stable_sort(cells.begin(), cells.end(), [&](const auto& left, const auto& right) {
                return charging_allies.at(left) > charging_allies.at(right);
            });
        }
        std::vector<spa::SummonTarget> targets;
        BossOpeningTarget boss_target;
        if (owner_name == "温蒂") {
            boss_target = boss_opening_target(round);
            targets = spa::rank_water_cannon_targets(
                cells,
                verified_owner,
                m_population_grid_members,
                lanes,
                boss_target.cells);
        }
        else {
            for (const auto& cell : cells) {
                targets.push_back({ cell });
            }
        }
        if (targets.empty()) {
            continue;
        }
        spa::SwapTaskPlugin deploy(m_callback, m_inst, TaskType);
        deploy.set_retry_times(0);
        deploy.set_task_id(m_task_id);
        deploy.set_task_ptr(this);
        if (!deploy
                 .set_registered_grid_context(m_spatial_grid_reference, m_spatial_grid_projection, m_spatial_map_id)) {
            continue;
        }
        m_preparation_summon_attempts.emplace(round, summon);
        std::optional<spa::SummonTarget> verified_target;
        const auto placement = deploy.deploy_preparation_summon(
            summon,
            observed.second,
            targets,
            round,
            state.get("funds", -1),
            remaining,
            verified_target);
        if (verified_target) {
            m_confirmed_summon_cells.emplace(round, verified_target->cell);
        }
        auto receipt = basic_info_with_what("SPAPreparationSummonPlacement");
        receipt["details"] = json::object {
            { "name", summon },
            { "owner", verified_owner.name },
            { "owner_facing_known", verified_owner.facing != '\0' },
            { "boss_opening_target_source", boss_target.source },
            { "boss_opening_region_verified", boss_target.measured },
            { "round", round },
            { "candidate_cells", targets.size() },
            { "drop_attempted", placement.has_value() },
            { "field_title_verified", placement.value_or(false) },
            { "placed_col", verified_target ? verified_target->cell.first : -1 },
            { "placed_row", verified_target ? verified_target->cell.second : -1 },
            { "committed_facing",
              verified_target && verified_target->facing ? std::string(1, verified_target->facing) : "" },
            { "deployment_capacity_effect_known", placement.value_or(false) },
            { "ordinary_operator_population_consumed",
              placement.value_or(false) ? json::value(false) : json::value(nullptr) }
        };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        return true; // Always reobserve after opening the bench or field panel.
    }
    return false;
}

json::array asst::StrongholdProtocolTask::equipment_candidates_snapshot() const
{
    json::array result;
    for (const auto& name : m_equipment_candidates) {
        result.emplace_back(name);
    }
    return result;
}

std::vector<std::string> asst::StrongholdProtocolTask::field_added_pacts(const std::string& name) const
{
    const auto owned = m_policy.owned.find(name);
    const auto position = m_auto_deploy_positions.find(name);
    const auto items = m_equipped_items.find(name);
    const auto forms = owned == m_policy.owned.end() ? std::nullopt : spa::owned_form_counts(owned->second);
    // Name-level persisted slots cannot assign a transfer to an arbitrary
    // duplicate or a body that has not been revalidated after resumption.
    if (m_field_checkpoint_loaded || owned == m_policy.owned.end() || !owned->second.deployed || !forms ||
        forms->initial + forms->elite != 1 || m_auto_deploy_positions.count(name) != 1 ||
        position == m_auto_deploy_positions.end() || !m_verified_field_pose_points.contains(position->second) ||
        items == m_equipped_items.end()) {
        return {};
    }
    // A viewport or a restored partial list cannot resolve ambiguous
    // conversions. Until empty-slot completeness is separately observed,
    // require the identity-checked body to have every physical slot accounted.
    const bool complete_slots =
        items->second.size() == static_cast<size_t>(spa::equipment_capacity(name, owned->second.elite));
    return spa::equipment_added_pacts(items->second, complete_slots);
}

json::array asst::StrongholdProtocolTask::equipment_slots_snapshot() const
{
    json::array result;
    for (const auto& [name, items] : m_equipped_items) {
        result.emplace_back(json::object { { "name", name }, { "items", items } });
    }
    return result;
}

std::pair<asst::spa::EquipmentAssignmentContext, std::vector<asst::spa::EquipmentRecipient>>
    asst::StrongholdProtocolTask::equipment_assignment_inputs() const
{
    const auto catalog = spa::operator_pacts();
    std::vector<spa::RosterUnit> roster;
    for (const auto& [name, owned] : m_policy.owned) {
        if (owned.copies > 0) {
            roster.push_back({ name, owned.deployed, field_added_pacts(name), owned.elite });
        }
    }
    const auto formation = spa::evaluate_formation(
        roster,
        catalog,
        spa::pact_requirements(),
        spa::enabled_formation_pacts(m_enabled_core_pacts, m_policy.disabled_pacts));
    spa::EquipmentAssignmentContext context;
    context.growth_battle_available = m_expected_boss_round > 0 && m_policy.round < m_expected_boss_round;
    context.kjerag_freeze_growth_active =
        std::find(formation.active_pacts.begin(), formation.active_pacts.end(), "谢拉格") !=
            formation.active_pacts.end() &&
        std::any_of(roster.begin(), roster.end(), [](const auto& unit) {
            return unit.deployed && spa::is_kjerag_freeze_stacker(unit.name);
        });
    std::vector<spa::EquipmentRecipient> recipients;
    std::set<std::string> special_hammers;
    const int field_frontliners =
        static_cast<int>(std::count_if(m_policy.owned.begin(), m_policy.owned.end(), [&](const auto& member) {
            const auto& [name, owned] = member;
            return owned.deployed && (owned.copies > 0 || owned.elite) &&
                   spa::operator_placement_role(name) == spa::PlacementRole::Frontline;
        }));
    for (const auto& [name, owned] : m_policy.owned) {
        if (!owned.deployed) {
            continue;
        }
        const auto props = BattleData.find_first_oper(battle::Role::Unknown, name, true);
        const bool strategy_mech = name == "“暴躁铁皮”";
        if (!props && !strategy_mech) {
            continue;
        }
        if (props) {
            context.field_has_healer = context.field_has_healer || props->role == battle::Role::Medic ||
                                       props->sub_role == battle::SubRole::Support_Bard;
            context.elemental_source_known = context.elemental_source_known ||
                                             props->sub_role == battle::SubRole::Caster_PrimCaster ||
                                             props->sub_role == battle::SubRole::Support_Ritualist;
        }
        const auto slots = m_equipped_items.find(name);
        if (slots != m_equipped_items.end()) {
            for (const auto& item : slots->second) {
                if (item != "维式重锤" && item.find("维式重锤") != std::string::npos) {
                    special_hammers.insert(item);
                }
            }
        }
        // A spare bench copy does not make the single field recipient
        // ambiguous. Actual target identity and equipment are re-read on use.
        if (m_auto_deploy_positions.count(name) != 1 || slots == m_equipped_items.end() ||
            !m_verified_field_pose_points.contains(m_auto_deploy_positions.find(name)->second)) {
            continue;
        }
        spa::EquipmentRecipient target;
        target.name = name;
        target.elite = owned.elite;
        target.slots_known = true;
        target.equipped = slots->second;
        target.frontline =
            strategy_mech || spa::combat_placement_role(props->role, props->sub_role) == spa::PlacementRole::Frontline;
        target.healer =
            !strategy_mech && spa::combat_placement_role(props->role, props->sub_role) == spa::PlacementRole::Healer;
        target.attacks =
            strategy_mech ||
            (props->sub_role != battle::SubRole::Support_Bard &&
             (props->role != battle::Role::Medic || props->sub_role == battle::SubRole::Medic_IncantationMedic));
        target.carry = strategy_mech || spa::is_primary_output_candidate(name, m_policy.primary_pact) ||
                       std::find(m_policy.carries.begin(), m_policy.carries.end(), name) != m_policy.carries.end();
        target.freeze_skill_engine = context.kjerag_freeze_growth_active && (name == "灵知" || name == "圣聆初雪");
        if (const auto growth = spa::growth_profiles().find(name); growth != spa::growth_profiles().end()) {
            target.skill_growth_engine = growth->second.kind == spa::GrowthKind::CappedConditional &&
                                         growth->second.phase == spa::GrowthPhase::BattleEvent &&
                                         growth->second.normal_description.find("开启技能") != std::string::npos;
        }
        if (const auto pacts = catalog.find(name); pacts != catalog.end()) {
            target.active_own_pact = std::any_of(pacts->second.begin(), pacts->second.end(), [&](const auto& pact) {
                return std::find(formation.active_pacts.begin(), formation.active_pacts.end(), pact) !=
                       formation.active_pacts.end();
            });
        }
        if (const auto stats = spa::season_combat_stats(name, owned.elite)) {
            for (const auto& [x, y] : stats->basic_range) {
                target.max_range_distance = std::max(target.max_range_distance, std::abs(x) + std::abs(y));
            }
        }
        else if (props) {
            for (const auto& cell : BattleData.get_range(props->role, name, owned.elite ? 2 : 1)) {
                target.max_range_distance = std::max(target.max_range_distance, std::abs(cell.x) + std::abs(cell.y));
            }
        }
        // A cooldown helper such as Fartooth is not itself a disposable
        // redeployment engine. Require repeated on-deploy growth, active
        // recipients and other frontliners before recommending a death loop.
        const int other_frontliners = field_frontliners - (target.frontline ? 1 : 0);
        target.deliberate_redeploy = formation.complete && other_frontliners >= 2 &&
                                     !spa::is_primary_output_candidate(name, m_policy.primary_pact) &&
                                     spa::is_redeployment_growth_engine(name, formation.active_pacts, owned.elite);
        recipients.push_back(std::move(target));
    }
    context.special_hammer_types = static_cast<int>(special_hammers.size());
    if (m_policy.primary_pact == "阿戈尔") {
        std::string output;
        for (const std::string name : { "克莱门莎", "乌尔比安", "幽灵鲨", "斯卡蒂", "安哲拉" }) {
            if (m_auto_deploy_positions.count(name) == 1 && m_policy.owned.contains(name) &&
                m_policy.owned.at(name).deployed) {
                output = name;
                break;
            }
        }
        for (auto& target : recipients) {
            target.carry = target.name == output;
        }
    }
    return { std::move(context), std::move(recipients) };
}

bool asst::StrongholdProtocolTask::handle_pact_recruitment_equipment(const json::value& state)
{
    const int round = state.get("round", -1);
    if (!m_policy.dynamic_decision_mode || m_decision_only || !m_equipment_candidates.contains("寻呼模块") ||
        m_policy.shop_level < 1 || m_policy.shop_level > 6 || m_policy.bench_occupied < 0 ||
        m_policy.bench_occupied > 8 || m_auto_deploy_positions.size() < 3 ||
        m_pact_recruitment_attempted_rounds.contains(round) || !m_pending_pact_recruitment.empty() ||
        m_pending_field_replacement || m_pending_spatial_orientation || m_field_checkpoint_loaded ||
        !state.get("funds_known", false) || !state.get("remaining_deployment_known", false)) {
        return false;
    }
    const auto recipients = equipment_assignment_inputs().second;
    const auto recipient = std::find_if(recipients.begin(), recipients.end(), [&](const auto& item) {
        return spa::safe_pact_recruitment_recipient(item.name, m_policy.primary_pact, item.slots_known, item.equipped);
    });
    if (recipient == recipients.end()) {
        return false;
    }
    const auto target = m_auto_deploy_positions.find(recipient->name)->second;
    m_pact_recruitment_attempted_rounds.insert(round);
    // Journal before the contact. A resumed host observes the temporary shop;
    // it never replays a potentially consumed pager from this intent alone.
    m_pending_pact_recruitment = m_policy.primary_pact;
    auto receipt = basic_info_with_what("SPAPactRecruitmentPending");
    receipt["details"] = json::object { { "run_id", m_run_id },
                                        { "round", round },
                                        { "equipment", "寻呼模块" },
                                        { "resume_after_round", round },
                                        { "operator", recipient->name },
                                        { "pact_recruitment_pending", m_pending_pact_recruitment },
                                        { "pact_recruitment_attempted_round", round },
                                        { "shop_level", m_policy.shop_level },
                                        { "roster", roster_snapshot() },
                                        { "field_positions", field_position_snapshot() },
                                        { "equipment_candidates", equipment_candidates_snapshot() },
                                        { "equipment_slots", equipment_slots_snapshot() },
                                        { "bench_occupied", m_policy.bench_occupied },
                                        { "input_committed", false },
                                        { "contact_release_attempted", false },
                                        { "recruited_operator_verified", false },
                                        { "equipment_replay_allowed", false } };
    callback(AsstMsg::SubTaskExtraInfo, receipt);
    spa::SwapTaskPlugin use(m_callback, m_inst, TaskType);
    use.set_retry_times(0);
    use.set_task_id(m_task_id);
    use.set_task_ptr(this);
    const json::object options {
        { "name", "寻呼模块" },
        { "replaced", recipient->name },
        { "equip_only", true },
        { "find_equipment_source", true },
        { "fast_mode", true },
        { "preferred_equipment_x",
          m_equipment_source_hints.contains("寻呼模块") ? m_equipment_source_hints.at("寻呼模块") : -1 },
        { "target_equipment_known", true },
        { "expected_equipment", recipient->equipped },
        { "pact_recruitment_pact", m_policy.primary_pact },
        { "pact_recruitment_shop_level", m_policy.shop_level },
        { "target_x", target.first },
        { "target_y", target.second },
        { "drag_target_x", target.first },
        { "drag_target_y", target.second },
        { "placement_units", field_position_snapshot() },
        { "round", round },
        { "funds", state.get("funds", -1) },
        { "remaining_deployment", state.get("remaining_deployment", -1) }
    };
    const bool offered = use.configure(options) && use.run();
    const bool contact_attempted = use.equipment_contact_attempted() || use.equipment_committed();
    if (contact_attempted) {
        if (use.equipment_committed()) {
            m_equipment_candidates.erase("寻呼模块");
        }
        m_equipment_inventory_dirty = true;
        m_shopping_complete = false;
        m_spending_reviewed = false;
        m_spending_reason.clear();
    }
    else {
        m_pending_pact_recruitment.clear();
    }
    receipt["details"]["input_committed"] = use.equipment_committed();
    receipt["details"]["contact_release_attempted"] = contact_attempted;
    receipt["details"]["offer_verified"] = offered;
    receipt["details"]["verification_stage"] = use.equipment_stage();
    receipt["details"]["pact_recruitment_pending"] = m_pending_pact_recruitment;
    receipt["details"]["equipment_candidates"] = equipment_candidates_snapshot();
    callback(AsstMsg::SubTaskExtraInfo, receipt);
    return true;
}

std::optional<std::string> asst::StrongholdProtocolTask::direct_promotion_recipient() const
{
    if (m_pending_direct_promotion || m_field_checkpoint_loaded || m_pending_field_replacement ||
        m_pending_spatial_orientation || !m_pending_promotion.empty()) {
        return std::nullopt;
    }
    const auto [context, recipients] = equipment_assignment_inputs();
    (void)context;
    std::vector<spa::RosterUnit> roster;
    for (const auto& [name, owned] : m_policy.owned) {
        if (owned.copies > 0) {
            roster.push_back({ name, owned.deployed, field_added_pacts(name), owned.elite });
        }
    }
    const auto formation = spa::evaluate_formation(
        roster,
        spa::operator_pacts(),
        spa::pact_requirements(),
        spa::enabled_formation_pacts(m_enabled_core_pacts, m_policy.disabled_pacts));
    const bool primary_active =
        formation.complete &&
        std::find(formation.active_pacts.begin(), formation.active_pacts.end(), m_policy.primary_pact) !=
            formation.active_pacts.end();
    std::optional<std::string> selected;
    std::tuple<int, int, int> best {};
    for (const auto& target : recipients) {
        const auto& owned = m_policy.owned.at(target.name);
        const auto forms = spa::owned_form_counts(owned);
        int growth_gain = 0;
        if (primary_active && m_expected_boss_round > m_policy.round) {
            const auto initial = spa::project_primary_growth(
                m_policy.primary_pact,
                m_policy.round + 1,
                m_expected_boss_round,
                true,
                { { target.name, false, true } });
            const auto elite = spa::project_primary_growth(
                m_policy.primary_pact,
                m_policy.round + 1,
                m_expected_boss_round,
                true,
                { { target.name, true, true } });
            if (initial.valid && elite.valid) {
                growth_gain = std::max(0, elite.fixed_phase_additions - initial.fixed_phase_additions);
            }
        }
        if (!forms || !spa::direct_promotion_recipient_supported(
                          target.name,
                          forms->initial,
                          forms->elite,
                          owned.deployed,
                          m_auto_deploy_positions.count(target.name) == 1 &&
                              m_verified_field_pose_points.contains(m_auto_deploy_positions.find(target.name)->second),
                          target.slots_known,
                          static_cast<int>(target.equipped.size()),
                          target.carry,
                          growth_gain)) {
            continue;
        }
        const auto initial = *spa::season_combat_stats(target.name, false);
        const auto elite = *spa::season_combat_stats(target.name, true);
        // A ranking of catalogued improvements, not a damage forecast.
        const auto value = std::tuple { growth_gain,
                                        elite.equipment_slots - initial.equipment_slots,
                                        100 * (elite.attack - initial.attack) / std::max(1, initial.attack) +
                                            100 * (elite.max_hp - initial.max_hp) / std::max(1, initial.max_hp) };
        if (!selected || value > best) {
            selected = target.name;
            best = value;
        }
    }
    return selected;
}

bool asst::StrongholdProtocolTask::handle_direct_promotion_equipment(const json::value& state)
{
    const int round = state.get("round", -1);
    if (!m_policy.dynamic_decision_mode || m_decision_only || !state.get("round_known", false) ||
        !state.get("funds_known", false) || !state.get("remaining_deployment_known", false) ||
        state.get("page", std::string()) != "preparation" || !m_equipment_candidates.contains("博士投影") ||
        m_direct_promotion_attempted_rounds.contains(round) ||
        (m_mode == "joint" && state.get("preparation_seconds_remaining_known", false) &&
         state.get("preparation_seconds_remaining", 0) <= 25)) {
        return false;
    }
    const auto recipient = direct_promotion_recipient();
    if (!recipient) {
        return false;
    }
    const auto point = m_auto_deploy_positions.find(*recipient)->second;
    m_direct_promotion_attempted_rounds.insert(round);
    m_pending_direct_promotion =
        json::object { { "operator", *recipient },           { "equipment", "博士投影" },
                       { "contact_round", round },           { "recipient_initial_form_observed", false },
                       { "effect_timing", "unknown" },       { "restored_from_checkpoint", false },
                       { "equipment_replay_allowed", false } };
    const auto persist = [&](const char* what) {
        auto receipt = basic_info_with_what(what);
        receipt["details"] = json::object { { "run_id", m_run_id },
                                            { "round", round },
                                            { "direct_promotion_pending",
                                              m_pending_direct_promotion ? json::value(*m_pending_direct_promotion)
                                                                         : json::value(nullptr) },
                                            { "equipment_candidates", equipment_candidates_snapshot() },
                                            { "equipment_slots", equipment_slots_snapshot() },
                                            { "roster", roster_snapshot() },
                                            { "inventory_complete", false } };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
    };
    // The parent receipt is synchronously journalled before any equipment
    // input. A crash during contact retains an observation-only reservation.
    persist("SPADirectPromotionPending");
    const AsstCallback observe = [&](AsstMsg msg, const json::value& info, Assistant* inst) {
        if (info.get("what", std::string()) == "SPAPromotionConsumableContactPending" && m_pending_direct_promotion) {
            const auto details = info.find<json::object>("details");
            if (details && details->get("equipment", std::string()) == "博士投影" &&
                details->get("operator", std::string()) == *recipient && details->get("round", -1) == round) {
                (*m_pending_direct_promotion)["effect_timing"] = details->get("effect_timing", std::string());
                (*m_pending_direct_promotion)["recipient_initial_form_observed"] =
                    details->get("recipient_initial_form_observed", false);
                persist("SPADirectPromotionPending");
            }
        }
        if (m_callback) {
            m_callback(msg, info, inst);
        }
    };
    spa::SwapTaskPlugin use(observe, m_inst, TaskType);
    use.set_retry_times(0);
    use.set_task_id(m_task_id);
    use.set_task_ptr(this);
    json::array field;
    for (const auto& [name, location] : m_auto_deploy_positions) {
        field.emplace_back(
            json::object { { "name", name },
                           { "x", location.first },
                           { "y", location.second },
                           { "field_pose_verified", m_verified_field_pose_points.contains(location) } });
    }
    const json::object options {
        { "name", "博士投影" },
        { "replaced", *recipient },
        { "equip_only", true },
        { "find_equipment_source", true },
        { "direct_projection", true },
        { "allow_delayed_projection",
          m_expected_boss_round > round && !state.get("preparation_boss_bar_visible", false) },
        { "preferred_equipment_x",
          m_equipment_source_hints.contains("博士投影") ? m_equipment_source_hints.at("博士投影") : -1 },
        { "fast_mode", true },
        { "target_equipment_known", true },
        { "target_elite", false },
        { "expected_equipment", m_equipped_items.at(*recipient) },
        { "target_x", point.first },
        { "target_y", point.second },
        { "drag_target_x", point.first },
        { "drag_target_y", point.second },
        { "placement_units", field },
        { "round", round },
        { "funds", state.get("funds", -1) },
        { "remaining_deployment", state.get("remaining_deployment", -1) }
    };
    const bool observed = use.configure(options) && use.run();
    if (use.equipment_contact_attempted() || use.equipment_committed()) {
        (*m_pending_direct_promotion)["contact_release_attempted"] = true;
        (*m_pending_direct_promotion)["input_committed"] = use.equipment_committed();
        (*m_pending_direct_promotion)["projection_attached_verified"] = use.projection_attached_verified();
        (*m_pending_direct_promotion)["immediate_elite_form_observed"] = use.recipient_elite_transition_observed();
        m_equipment_candidates.erase("博士投影");
        m_equipment_inventory_dirty = true;
        // A delayed item occupies a real slot until the following round.
        // Unknown equipment after an attempted contact must not authorize a
        // second assignment from the pre-contact slot count.
        if (use.projection_attached_verified()) {
            m_equipped_items.at(*recipient).push_back("博士投影");
        }
        else {
            m_equipped_items.erase(*recipient);
        }
    }
    else {
        m_pending_direct_promotion.reset();
    }
    (void)observed;
    persist("SPADirectPromotionObserved");
    return true;
}

bool asst::StrongholdProtocolTask::reconcile_direct_promotion_equipment(const json::value& state)
{
    const int round = state.get("round", -1);
    if (!m_pending_direct_promotion || !state.get("round_known", false) ||
        m_direct_promotion_observed_rounds.contains(round) || m_field_checkpoint_loaded ||
        m_pending_spatial_orientation || m_pending_field_replacement || m_spatial_snapshot_round != round ||
        m_growth_population_poses != m_auto_deploy_positions || m_population_grid_members.empty() ||
        (m_mode == "joint" && state.get("preparation_seconds_remaining_known", false) &&
         state.get("preparation_seconds_remaining", 0) <= 25)) {
        return false;
    }
    const auto& pending = *m_pending_direct_promotion;
    const std::string name = pending.get("operator", std::string());
    const std::string timing = pending.get("effect_timing", std::string());
    if ((timing != "on_equip" && timing != "next_round_start") || !m_policy.owned.contains(name) ||
        m_auto_deploy_positions.count(name) != 1) {
        return false;
    }
    const spa::PromotionConsumableQuote quote { spa::PromotionConsumableEffect::DirectPromotion,
                                                timing == "on_equip" ? spa::PromotionConsumableTiming::OnEquip
                                                                     : spa::PromotionConsumableTiming::NextRoundStart,
                                                0 };
    const auto forms = spa::owned_form_counts(m_policy.owned.at(name));
    const auto point = m_auto_deploy_positions.find(name)->second;
    if (!forms || forms->initial != 1 || forms->elite != 0 || !m_verified_field_pose_points.contains(point) ||
        !pending.get("recipient_initial_form_observed", false) ||
        !spa::promotion_consumable_due(quote, pending.get("contact_round", -1), round)) {
        return false;
    }
    m_direct_promotion_observed_rounds.insert(round);
    spa::SwapTaskPlugin inspect(m_callback, m_inst, TaskType);
    inspect.set_retry_times(0);
    inspect.set_task_id(m_task_id);
    inspect.set_task_ptr(this);
    const bool elite = inspect.revalidate_promoted_pose(name, Point(point.first, point.second), round, true);
    const bool verified = spa::direct_promotion_form_transition(
        quote,
        pending.get("contact_round", -1),
        round,
        true,
        elite,
        elite,
        forms->initial,
        forms->elite);
    const bool restored = pending.get("restored_from_checkpoint", false);
    if (verified) {
        // No three-copy deduction, free reward, or hypothetical bench copy.
        spa::set_owned_form_counts(m_policy.owned.at(name), { 0, 1 });
        m_equipped_items.erase(name);
        m_equipment_inventory_dirty = true;
        m_dynamic_layout_context.clear();
        m_dynamic_layout_target.clear();
        m_pending_direct_promotion.reset();
    }
    auto receipt = basic_info_with_what(verified ? "SPADirectPromotionReconciled" : "SPADirectPromotionObserved");
    receipt["details"] =
        json::object { { "run_id", m_run_id },
                       { "round", round },
                       { "operator", name },
                       { "recipient_elite_verified", verified },
                       { "physical_copy_delta", 0 },
                       { "promotion_reward_created", false },
                       { "direct_effect_causality_verified", verified && !restored },
                       { "direct_promotion_pending",
                         m_pending_direct_promotion ? json::value(*m_pending_direct_promotion) : json::value(nullptr) },
                       { "equipment_slots", equipment_slots_snapshot() },
                       { "roster", roster_snapshot() },
                       { "inventory_complete", false } };
    callback(AsstMsg::SubTaskExtraInfo, receipt);
    return true;
}

bool asst::StrongholdProtocolTask::handle_persistent_equipment(const json::value& state)
{
    const int round = state.get("round", -1);
    if (!m_policy.dynamic_decision_mode || m_decision_only || m_equipment_candidates.empty() ||
        m_pending_field_replacement || m_pending_spatial_orientation || m_field_checkpoint_loaded ||
        !state.get("funds_known", false) || !state.get("remaining_deployment_known", false) ||
        m_auto_deploy_positions.empty() || (state.get("remaining_deployment", 0) > 0 && m_policy.bench_occupied > 0)) {
        return false;
    }
    const bool solo_boss_finalization = m_mode != "joint" && m_spatial_boss_phase &&
                                       m_spatial_boss_phase_round == round &&
                                       (m_shopping_complete || state.get("funds", -1) == 0);
    m_persistent_equipment_preparation.begin_round(round);
    if (!m_persistent_equipment_preparation.within_attempt_budget(solo_boss_finalization)) {
        return false;
    }
    auto [context, recipients] = equipment_assignment_inputs();
    std::vector<std::string> candidates;
    for (const auto& name : m_equipment_candidates) {
        if (m_persistent_equipment_preparation.may_try(name)) {
            candidates.push_back(name);
        }
    }
    auto assignment = spa::choose_equipment_assignment(context, candidates, recipients);
    std::optional<spa::IdentityTagTarget> identity_tag_target;
    if (std::find(candidates.begin(), candidates.end(), "随身身份牌") != candidates.end() &&
        (m_mode != "joint" || state.get("preparation_seconds_remaining", 0) >= 25)) {
        identity_tag_target = spa::choose_identity_tag_target(
            m_policy.primary_pact,
            m_policy.support_pacts,
            m_policy.disabled_pacts,
            m_policy.observed_pact_layers,
            recipients,
            true);
        if (identity_tag_target) {
            assignment = { true, "随身身份牌", identity_tag_target->recipient, 42 };
        }
    }
    if (!assignment.found) {
        return false;
    }
    const bool identity_tag = identity_tag_target.has_value();
    const auto target = m_auto_deploy_positions.find(assignment.recipient)->second;
    json::array field;
    for (const auto& [name, point] : m_auto_deploy_positions) {
        field.emplace_back(
            json::object { { "name", name },
                           { "x", point.first },
                           { "y", point.second },
                           { "field_pose_verified", m_verified_field_pose_points.contains(point) } });
    }
    spa::SwapTaskPlugin equip(m_callback, m_inst, TaskType);
    equip.use_bench_identity_hints(m_inventory_operator_hints);
    equip.set_retry_times(0);
    equip.set_task_id(m_task_id);
    equip.set_task_ptr(this);
    const json::object options { { "name", assignment.equipment },
                                 { "replaced", assignment.recipient },
                                 { "equip_only", true },
                                 { "find_equipment_source", true },
                                 { "preferred_equipment_x",
                                   m_equipment_source_hints.contains(assignment.equipment)
                                       ? m_equipment_source_hints.at(assignment.equipment)
                                       : -1 },
                                 { "fast_mode", true },
                                 { "target_equipment_known", true },
                                 { "target_elite", m_policy.owned.at(assignment.recipient).elite },
                                 { "expected_equipment", m_equipped_items.at(assignment.recipient) },
                                 { "identity_tag_pact", identity_tag ? identity_tag_target->pact : std::string() },
                                 { "target_x", target.first },
                                 { "target_y", target.second },
                                 { "drag_target_x", target.first },
                                 { "drag_target_y", target.second },
                                 { "placement_units", field },
                                 { "round", round },
                                 { "funds", state.get("funds", -1) },
                                 { "remaining_deployment", state.get("remaining_deployment", -1) } };
    const bool verified = equip.configure(options) && equip.run();
    const bool additional_purchased_item_pending = m_persistent_equipment_preparation.record_assignment(
        assignment.equipment,
        verified,
        equip.equipment_committed() || (identity_tag && equip.equipment_contact_attempted()),
        solo_boss_finalization);
    if (const auto source_x = equip.equipment_source_x()) {
        // A priority hint only: every later use still checks the live name.
        m_equipment_source_hints[assignment.equipment] = *source_x;
    }
    if (verified) {
        if (!identity_tag) {
            m_equipped_items.at(assignment.recipient).push_back(assignment.equipment);
        }
        m_equipment_capacity_blocked_round = -1;
        if (!additional_purchased_item_pending) {
            m_equipment_candidates.erase(assignment.equipment);
        }
        m_equipment_source_hints.erase(assignment.equipment);
        m_equipment_inventory_dirty = true;
    }
    else if (identity_tag && equip.equipment_contact_attempted()) {
        // Reinspect the actual source on another preparation after an
        // unconfirmed effect. Retain the last known persistent equipment.
        m_equipment_candidates.erase(assignment.equipment);
        m_equipment_inventory_dirty = true;
    }
    else if (equip.equipment_committed() || equip.equipment_slots_mismatch()) {
        m_equipped_items.erase(assignment.recipient);
        if (equip.equipment_committed()) {
            m_equipment_candidates.erase(assignment.equipment);
            m_equipment_inventory_dirty = true;
        }
    }
    auto receipt =
        basic_info_with_what(verified ? "SPAAutomationEquipmentReconciled" : "SPAEquipmentAssignmentDeferred");
    receipt["details"] =
        json::object { { "run_id", m_run_id },
                       { "round", round },
                       { "equipment", assignment.equipment },
                       { "operator", assignment.recipient },
                       { "identity_tag_pact", identity_tag ? identity_tag_target->pact : std::string() },
                       { "score", assignment.score },
                       { "input_committed", equip.equipment_committed() },
                       { "verification_stage", equip.equipment_stage() },
                       { "equipped_verified", verified && !identity_tag },
                       { "consumable_effect_verified", verified && identity_tag },
                       { "solo_boss_finalization", solo_boss_finalization },
                       { "additional_purchased_item_pending", additional_purchased_item_pending },
                       { "equipment_candidates", equipment_candidates_snapshot() },
                       { "equipment_slots", equipment_slots_snapshot() },
                       { "roster", roster_snapshot() },
                       { "inventory_complete", false } };
    receipt["details"]["redeployment_growth_candidate"] =
        std::any_of(recipients.begin(), recipients.end(), [&](const auto& target) {
            return target.name == assignment.recipient && target.deliberate_redeploy;
        });
    receipt["details"]["redeployment_cycle_verified"] = false;
    callback(AsstMsg::SubTaskExtraInfo, receipt);
    return true;
}

void asst::StrongholdProtocolTask::observe_delayed_cash_equipment(const json::value& state)
{
    if (!m_pending_delayed_cash_equipment || state.get("page", std::string()) != "preparation" ||
        !state.get("round_known", false) || !state.get("funds_known", false)) {
        return;
    }
    auto& pending = *m_pending_delayed_cash_equipment;
    const int source_round = pending.get("source_round", -1);
    const int round = state.get("round", -1);
    if (round <= source_round || pending.get("funds_observed_round", -1) > source_round) {
        return;
    }
    pending["funds_observed_round"] = round;
    pending["observed_funds"] = state.get("funds", -1);
    const auto equipment = pending.get("equipment", std::string());
    // The wallet combines base income, strategy effects and other gifts.
    // Observing it alone does not isolate this item's credited amount.
    auto receipt = basic_info_with_what("SPADelayedCashFundsObserved");
    json::object details { { "run_id", m_run_id },
                           { "round", round },
                           { "source_round", source_round },
                           { "equipment", equipment },
                           { "funds", state.get("funds", -1) },
                           { "next_round_credit_quote", pending.get("next_round_credit_quote", 0) },
                           { "consumption_verified", pending.get("consumption_verified", false) },
                           { "next_round_credit_verified", false },
                           { "income_attribution_known", false },
                           { "recurring_income_effect", spa::is_recurring_cash_equipment(equipment) },
                           { "expected_next_round_observed", round == source_round + 1 },
                           { "delayed_cash_attempted_round", source_round },
                           { "roster", roster_snapshot() } };
    if (pending.get("consumption_verified", false)) {
        m_pending_delayed_cash_equipment.reset();
        details["delayed_cash_equipment_pending"] = nullptr;
    }
    else {
        details["delayed_cash_equipment_pending"] = pending;
        details["equipment_replay_allowed"] = false;
    }
    receipt["details"] = std::move(details);
    callback(AsstMsg::SubTaskExtraInfo, receipt);
}

bool asst::StrongholdProtocolTask::handle_delayed_cash_equipment(const json::value& state)
{
    const int round = state.get("round", -1);
    const std::string equipment = m_equipment_candidates.contains("精打细算玩偶") ? "精打细算玩偶" : "见钱眼开玩偶";
    if (!m_policy.dynamic_decision_mode || m_decision_only || state.get("page", std::string()) != "preparation" ||
        !state.get("round_known", false) || !state.get("funds_known", false) ||
        !state.get("remaining_deployment_known", false) || round < 1 || m_expected_boss_round <= round ||
        state.get("preparation_boss_bar_visible", false) ||
        (m_spatial_boss_phase && m_spatial_boss_phase_round == round) || !m_equipment_candidates.contains(equipment) ||
        m_pending_delayed_cash_equipment || m_delayed_cash_equipment_attempted_rounds.contains(round) ||
        m_auto_deploy_positions.empty() || m_pending_spatial_orientation || m_pending_field_replacement ||
        m_field_checkpoint_loaded) {
        return false;
    }
    const auto recipients = equipment_assignment_inputs().second;
    const auto eligible = [](const auto& target) {
        return target.slots_known &&
               target.equipped.size() < static_cast<size_t>(spa::equipment_capacity(target.name, target.elite));
    };
    auto recipient = std::find_if(recipients.begin(), recipients.end(), [&](const auto& target) {
        return target.name != m_delayed_cash_rejected_recipient && eligible(target);
    });
    if (recipient == recipients.end()) {
        recipient = std::find_if(recipients.begin(), recipients.end(), eligible);
    }
    if (recipient == recipients.end()) {
        return false;
    }
    m_delayed_cash_equipment_attempted_rounds.insert(round);
    m_pending_delayed_cash_equipment = json::object {
        { "equipment", equipment },           { "operator", recipient->name }, { "source_round", round },
        { "next_round_credit_quote", 0 },     { "input_committed", false },    { "consumption_verified", false },
        { "equipment_replay_allowed", false }
    };
    auto receipt = basic_info_with_what("SPADelayedCashEquipmentPending");
    receipt["details"] = json::object { { "run_id", m_run_id },
                                        { "round", round },
                                        { "equipment", equipment },
                                        { "recurring_income_effect", spa::is_recurring_cash_equipment(equipment) },
                                        { "operator", recipient->name },
                                        { "delayed_cash_attempted_round", round },
                                        { "delayed_cash_equipment_pending", *m_pending_delayed_cash_equipment },
                                        { "next_round_credit_verified", false },
                                        { "equipment_replay_allowed", false },
                                        { "equipment_candidates", equipment_candidates_snapshot() },
                                        { "equipment_slots", equipment_slots_snapshot() },
                                        { "roster", roster_snapshot() },
                                        { "field_positions", field_position_snapshot() } };
    // Persist an intent before any contact, including a lost release reply.
    callback(AsstMsg::SubTaskExtraInfo, receipt);
    const auto target = m_auto_deploy_positions.find(recipient->name)->second;
    spa::SwapTaskPlugin use(m_callback, m_inst, TaskType);
    use.set_retry_times(0);
    use.set_task_id(m_task_id);
    use.set_task_ptr(this);
    const json::object options { { "name", equipment },
                                 { "replaced", recipient->name },
                                 { "equip_only", true },
                                 { "find_equipment_source", true },
                                 { "fast_mode", true },
                                 { "preferred_equipment_x",
                                   m_equipment_source_hints.contains(equipment) ? m_equipment_source_hints.at(equipment)
                                                                                : -1 },
                                 { "target_equipment_known", true },
                                 { "target_elite", recipient->elite },
                                 { "expected_equipment", recipient->equipped },
                                 { "target_x", target.first },
                                 { "target_y", target.second },
                                 { "drag_target_x", target.first },
                                 { "drag_target_y", target.second },
                                 { "placement_units", field_position_snapshot() },
                                 { "round", round },
                                 { "funds", state.get("funds", -1) },
                                 { "remaining_deployment", state.get("remaining_deployment", -1) } };
    const bool consumed = use.configure(options) && use.run();
    const bool contacted = use.equipment_contact_attempted() || use.equipment_committed();
    if (contacted) {
        m_delayed_cash_rejected_recipient.clear();
        auto& pending = *m_pending_delayed_cash_equipment;
        pending["next_round_credit_quote"] = use.equipment_next_round_credit_quote();
        pending["input_committed"] = use.equipment_committed();
        pending["contact_release_attempted"] = use.equipment_contact_attempted();
        pending["consumption_verified"] = consumed && use.equipment_consumption_verified();
        m_equipment_candidates.erase(equipment);
        m_equipment_inventory_dirty = true;
    }
    else {
        if (use.equipment_stage() == "held_target") {
            m_delayed_cash_rejected_recipient = recipient->name;
        }
        m_pending_delayed_cash_equipment.reset();
    }
    auto& details = receipt["details"];
    details["input_committed"] = use.equipment_committed();
    details["contact_release_attempted"] = use.equipment_contact_attempted();
    details["consumption_verified"] = consumed && use.equipment_consumption_verified();
    details["next_round_credit_quote"] = use.equipment_next_round_credit_quote();
    details["verification_stage"] = use.equipment_stage();
    details["delayed_cash_equipment_pending"] =
        m_pending_delayed_cash_equipment ? json::value(*m_pending_delayed_cash_equipment) : json::value(nullptr);
    details["equipment_candidates"] = equipment_candidates_snapshot();
    callback(AsstMsg::SubTaskExtraInfo, receipt);
    return true;
}

bool asst::StrongholdProtocolTask::reconcile_pending_cash_equipment(const json::value& state)
{
    const int round = state.get("round", -1);
    if (m_pending_cash_equipment) {
        auto& pending = *m_pending_cash_equipment;
        const auto equipment = pending.get("equipment", std::string("盟约之币"));
        const auto decision = spa::pending_cash_observation(pending, state, need_exit());
        if (decision == spa::CashReconciliation::AlreadyClosed) {
            // An incomplete restored record may not identify its old round.
            // Wait until a later round than this first local observation.
            if (!pending.contains("closed_observation_round") && state.get("round_known", false) && round >= 1 &&
                round <= 20) {
                pending["closed_observation_round"] = round;
            }
            return false;
        }
        if (decision != spa::CashReconciliation::Verified) {
            // Allow one more observation before any other transaction, not
            // another equipment contact. After that, keep the unknown guard;
            // a later shopping balance cannot resolve this old contact.
            const int checks = pending.find<int>("wallet_observations").value_or(0);
            const bool observe_again = decision == spa::CashReconciliation::ObserveAgain;
            pending["wallet_observations"] = checks + 1;
            pending["observation_checked"] = !observe_again;
            auto receipt = basic_info_with_what("SPACashEquipmentDeferred");
            receipt["details"] = json::object { { "run_id", m_run_id },
                                                { "round", round },
                                                { "equipment", equipment },
                                                { "effect_verified", false },
                                                { "verification_stage", "post_contact_unverified" },
                                                { "cash_equipment_pending", pending },
                                                { "roster", roster_snapshot() } };
            callback(AsstMsg::SubTaskExtraInfo, receipt);
            return observe_again;
        }
        auto receipt = basic_info_with_what("SPAAutomationEquipmentReconciled");
        receipt["details"] = json::object { { "run_id", m_run_id },
                                            { "round", round },
                                            { "equipment", equipment },
                                            { "operator", pending.get("operator", std::string()) },
                                            { "funds", state.get("funds", -1) },
                                            { "effect_verified", true },
                                            { "cash_effect_verified", true },
                                            { "contact_release_attempted", true },
                                            { "source_and_target_verified", true },
                                            { "inventory_removal_observed",
                                              pending.find<bool>("inventory_removal_observed").value_or(false) },
                                            { "reconciliation_only", true },
                                            { "cash_equipment_pending", nullptr },
                                            { "roster", roster_snapshot() } };
        m_pending_cash_equipment.reset();
        m_equipment_candidates.erase(equipment);
        receipt["details"]["equipment_candidates"] = equipment_candidates_snapshot();
        m_equipment_inventory_dirty = true;
        m_shopping_complete = false;
        m_spending_reviewed = false;
        m_spending_reason.clear();
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        return true;
    }
    return false;
}

bool asst::StrongholdProtocolTask::handle_cash_equipment(
    const json::value& state,
    bool boss_preparation,
    int funding_deficit)
{
    // Pending contacts are reconciled before every other preparation action.
    // Never attribute a later sale/shop balance to an old equipment contact.
    const int round = state.get("round", -1);
    const bool coin = (boss_preparation || funding_deficit > 0) && funding_deficit <= 2 &&
                      m_equipment_candidates.contains("盟约之币") && !m_cash_equipment_attempted_rounds.contains(round);
    const bool passbook = !coin && (boss_preparation || funding_deficit > 0) &&
                          m_equipment_candidates.contains("定期存折") && !m_passbook_attempted_rounds.contains(round);
    if (!m_policy.dynamic_decision_mode || m_decision_only || !(coin || passbook) || round < 1 || round > 20 ||
        m_pending_field_replacement || !state.get("funds_known", false) ||
        !state.get("remaining_deployment_known", false) || m_auto_deploy_positions.empty()) {
        return false;
    }
    const std::string equipment = coin ? "盟约之币" : "定期存折";
    auto& attempted_rounds = coin ? m_cash_equipment_attempted_rounds : m_passbook_attempted_rounds;
    const char* attempt_key = coin ? "cash_equipment_attempted_round" : "passbook_attempted_round";
    const int source_verified_round = coin ? m_cash_equipment_source_verified_round : m_passbook_source_verified_round;
    if (m_pending_cash_equipment && !spa::cash_new_source_allowed(
                                        *m_pending_cash_equipment,
                                        round,
                                        source_verified_round,
                                        attempted_rounds.contains(round))) {
        return false;
    }
    auto target = m_auto_deploy_positions.end();
    for (auto it = m_auto_deploy_positions.begin(); it != m_auto_deploy_positions.end(); ++it) {
        const auto owned = m_policy.owned.find(it->first);
        if (owned != m_policy.owned.end() && owned->second.deployed && m_auto_deploy_positions.count(it->first) == 1 &&
            m_verified_field_pose_points.contains(it->second)) {
            target = it;
            break;
        }
    }
    if (target == m_auto_deploy_positions.end()) {
        return false;
    }
    attempted_rounds.insert(round);
    spa::SwapTaskPlugin use(m_callback, m_inst, TaskType);
    use.set_retry_times(0);
    use.set_task_id(m_task_id);
    use.set_task_ptr(this);
    json::array field;
    for (const auto& [name, point] : m_auto_deploy_positions) {
        field.emplace_back(
            json::object { { "name", name },
                           { "x", point.first },
                           { "y", point.second },
                           { "field_pose_verified", m_verified_field_pose_points.contains(point) } });
    }
    const json::object options { { "name", equipment },
                                 { "minimum_cash_credit", std::max(1, funding_deficit) },
                                 { "replaced", target->first },
                                 { "equip_only", true },
                                 { "find_equipment_source", true },
                                 { "fast_mode", true },
                                 { "target_x", target->second.first },
                                 { "target_y", target->second.second },
                                 { "drag_target_x", target->second.first },
                                 { "drag_target_y", target->second.second },
                                 { "placement_units", field },
                                 { "round", round },
                                 { "funds", state.get("funds", -1) },
                                 { "remaining_deployment", state.get("remaining_deployment", -1) } };
    // Persist an unknown contact before execution, so a host interruption
    // between release and its receipt cannot silently clear the replay guard.
    const auto closed_contact = m_pending_cash_equipment;
    m_pending_cash_equipment = json::object { { "equipment", equipment },
                                              { "operator", target->first },
                                              { "source_round", round },
                                              { "funds_before", state.get("funds", -1) },
                                              { "cash_gain_quote", 0 },
                                              { "contact_status_known", false },
                                              { "contact_release_attempted", false },
                                              { "source_and_target_verified", false },
                                              { "inventory_removal_observed", false } };
    auto pending_receipt = basic_info_with_what("SPACashEquipmentDeferred");
    pending_receipt["details"] =
        json::object { { "run_id", m_run_id },
                       { "round", round },
                       { "equipment", equipment },
                       { "effect_verified", false },
                       { "verification_stage", "execution_pending" },
                       { attempt_key, round },
                       { "cash_equipment_closed_contact",
                         closed_contact ? json::value(*closed_contact) : json::value(nullptr) },
                       { "closed_contact_effect_verified", false },
                       { "cash_equipment_source_verified_round", m_cash_equipment_source_verified_round },
                       { "cash_equipment_pending", *m_pending_cash_equipment },
                       { "roster", roster_snapshot() } };
    callback(AsstMsg::SubTaskExtraInfo, pending_receipt);
    bool verified = use.configure(options) && use.run();
    int attempts = 1;
    if (!verified && !need_exit() &&
        spa::cash_source_retry_allowed(
            attempts,
            use.equipment_contact_attempted(),
            use.equipment_committed(),
            use.equipment_stage())) {
        auto retry = basic_info_with_what("SPACashEquipmentSourceRetry");
        retry["details"] = json::object { { "round", round },
                                          { "verification_stage", use.equipment_stage() },
                                          { "contact_release_attempted", false },
                                          { "next_attempt", 2 } };
        callback(AsstMsg::SubTaskExtraInfo, retry);
        // configure resets the plugin's cached evidence; run restores and
        // validates the current round/wallet before searching a fresh frame.
        ++attempts;
        verified = use.configure(options) && use.run();
    }
    const bool contact_attempted = use.equipment_contact_attempted() || use.equipment_committed();
    if (!verified && contact_attempted) {
        (*m_pending_cash_equipment)["contact_status_known"] = true;
        (*m_pending_cash_equipment)["contact_release_attempted"] = true;
        (*m_pending_cash_equipment)["input_committed"] = use.equipment_committed();
        (*m_pending_cash_equipment)["source_and_target_verified"] = use.cash_input_chain_verified();
        (*m_pending_cash_equipment)["inventory_removal_observed"] = use.equipment_consumption_verified();
        (*m_pending_cash_equipment)["cash_gain_quote"] = use.equipment_cash_credit();
    }
    else {
        m_pending_cash_equipment = verified ? std::nullopt : closed_contact;
    }
    if (verified || contact_attempted) {
        // A candidate hint is not a physical inventory count. In particular,
        // same-name equipment may have merged into one advanced item.
        m_equipment_candidates.erase(equipment);
        if (coin) {
            m_cash_equipment_acquired_round = -1;
        }
        m_equipment_inventory_dirty = true;
    }
    if (verified) {
        m_shopping_complete = false;
        m_spending_reviewed = false;
        m_spending_reason.clear();
    }
    auto receipt = basic_info_with_what(verified ? "SPAAutomationEquipmentReconciled" : "SPACashEquipmentDeferred");
    receipt["details"] =
        json::object { { "run_id", m_run_id },
                       { "round", round },
                       { "equipment", equipment },
                       { "operator", target->first },
                       { "input_committed", use.equipment_committed() },
                       { "contact_release_attempted", contact_attempted },
                       { "source_and_target_verified", use.cash_input_chain_verified() },
                       { "inventory_removal_observed", use.equipment_consumption_verified() },
                       { "cash_effect_verified", verified },
                       { "attempts", attempts },
                       { attempt_key, round },
                       { "cash_equipment_pending",
                         m_pending_cash_equipment ? json::value(*m_pending_cash_equipment) : json::value(nullptr) },
                       { "verification_stage", use.equipment_stage() },
                       { "effect_verified", verified },
                       { "inventory_complete", false },
                       { "equipment_candidates", equipment_candidates_snapshot() },
                       { "cash_equipment_acquired_round", m_cash_equipment_acquired_round },
                       { "roster", roster_snapshot() } };
    if (verified) {
        receipt["details"]["funds"] = state.get("funds", 0) + use.equipment_cash_credit();
    }
    callback(AsstMsg::SubTaskExtraInfo, receipt);
    return true;
}
