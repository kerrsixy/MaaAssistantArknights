#include "StrongholdProtocolTask.h"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Controller/Controller.h"
#include "MaaUtils/ImageIo.h"
#include "MaaUtils/Uuid.h"
#include "Task/MiniGame/SPA/SPAAcquisition.h"
#include "Task/MiniGame/SPA/SPABattleRole.h"
#include "Task/MiniGame/SPA/SPABenchSalePolicy.h"
#include "Task/MiniGame/SPA/SPABossPolicy.h"
#include "Task/MiniGame/SPA/SPABountyPolicy.h"
#include "Task/MiniGame/SPA/SPACashEquipmentEvidence.h"
#include "Task/MiniGame/SPA/SPACombatStats.h"
#include "Task/MiniGame/SPA/SPADifficultyPolicy.h"
#include "Task/MiniGame/SPA/SPADifficultyTaskPlugin.h"
#include "Task/MiniGame/SPA/SPAEconomyPolicy.h"
#include "Task/MiniGame/SPA/SPAEngagementPolicy.h"
#include "Task/MiniGame/SPA/SPAEquipmentPolicy.h"
#include "Task/MiniGame/SPA/SPAFormation.h"
#include "Task/MiniGame/SPA/SPAFormationSelectTaskPlugin.h"
#include "Task/MiniGame/SPA/SPAGrowth.h"
#include "Task/MiniGame/SPA/SPAImageEvidence.h"
#include "Task/MiniGame/SPA/SPALifecycleTaskPlugin.h"
#include "Task/MiniGame/SPA/SPAModePolicy.h"
#include "Task/MiniGame/SPA/SPANumberOcr.h"
#include "Task/MiniGame/SPA/SPAObservation.h"
#include "Task/MiniGame/SPA/SPAObserverTaskPlugin.h"
#include "Task/MiniGame/SPA/SPAOperatorCatalogue.h"
#include "Task/MiniGame/SPA/SPAPlanner.h"
#include "Task/MiniGame/SPA/SPAProjection.h"
#include "Task/MiniGame/SPA/SPAPromotionPosePolicy.h"
#include "Task/MiniGame/SPA/SPAReplacementPolicy.h"
#include "Task/MiniGame/SPA/SPARoundTaskPlugin.h"
#include "Task/MiniGame/SPA/SPARouteProfile.h"
#include "Task/MiniGame/SPA/SPARunBatch.h"
#include "Task/MiniGame/SPA/SPASetupTaskPlugin.h"
#include "Task/MiniGame/SPA/SPAShopTaskPlugin.h"
#include "Task/MiniGame/SPA/SPASpatialGrowthPolicy.h"
#include "Task/MiniGame/SPA/SPAStrategyCardTaskPlugin.h"
#include "Task/MiniGame/SPA/SPAStrategyCatalogue.h"
#include "Task/MiniGame/SPA/SPAStrategyCore.h"
#include "Task/MiniGame/SPA/SPAStrategyReportTaskPlugin.h"
#include "Task/MiniGame/SPA/SPASwapTaskPlugin.h"
#include "Task/MiniGame/SPA/SPATerrainModel.h"
#include "Vision/OCRer.h"
#include <algorithm>
#include <array>
#include <regex>
#include <set>
#include <tuple>

asst::StrongholdProtocolTask::StrongholdProtocolTask(const AsstCallback& callback, Assistant* inst) :
    InterfaceTask(callback, inst, TaskType)
{
    set_retry_times(0);
}

void asst::StrongholdProtocolTask::refresh_mode_pact_restrictions()
{
    m_policy.disabled_pacts =
        spa::effective_mode_pact_bans(m_mode, m_difficulty, m_declared_disabled_pacts, m_observed_disabled_pacts);
    m_strategy_context.disabled_pacts = m_policy.disabled_pacts;
    m_enabled_core_pacts = m_configured_core_pacts;
    std::erase_if(m_enabled_core_pacts, [&](const auto& pact) {
        return std::find(m_policy.disabled_pacts.begin(), m_policy.disabled_pacts.end(), pact) !=
               m_policy.disabled_pacts.end();
    });
    m_policy.enabled_core_pacts = m_enabled_core_pacts;
    if (std::find(m_policy.disabled_pacts.begin(), m_policy.disabled_pacts.end(), m_policy.primary_pact) !=
            m_policy.disabled_pacts.end() &&
        m_dynamic_primary_pact) {
        const auto previous = m_policy.primary_pact;
        m_policy.primary_pact = "auto";
        m_policy.primary_locked_after_activation = false;
        m_policy.carries.clear();
        m_policy.support_pacts.clear();
        auto receipt = basic_info_with_what("SPAPrimaryPactFallback");
        receipt["details"] = json::object { { "requested_primary_pact", previous },
                                            { "fallback", "auto" },
                                            { "reason", "Primary faction is banned in this simulation" } };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
    }
}

bool asst::StrongholdProtocolTask::set_params(const json::value& params)
{
    if (m_running) {
        return false;
    }
    const auto count = params.find<int>("run_count");
    if ((params.contains("run_count") && !count) || !spa::valid_run_count(count.value_or(1))) {
        return false;
    }
    m_run_count = count.value_or(1);
    if (m_run_count > 1 && (!params.get("manage_lifecycle", false) || !params.get("auto_run", false) ||
                            params.get("decision_only", false) || params.get("stop_before_battle", false) ||
                            params.get("stop_after_deployment", false) || params.get("movement_only", false) ||
                            params.get("round_only", false))) {
        return false;
    }
    m_initial_parameters = params;
    m_mode = params.get("mode", std::string("independent"));
    if (m_running || (m_mode != "independent" && m_mode != "quick" && m_mode != "joint")) {
        return false;
    }
    m_run_id = params.get("run_id", std::string());
    m_difficulty = params.get("difficulty", 0);
    m_expected_boss_round = params.get("expected_boss_round", m_mode == "quick" ? 9 : 14);
    m_resume_after_round = params.get("resume_after_round", 1);
    if (m_resume_after_round < 1 || m_resume_after_round > 20) {
        return false;
    }
    if (m_expected_boss_round != 0 && (m_expected_boss_round < 2 || m_expected_boss_round > 20)) {
        return false;
    }
    m_auto_run = params.get("auto_run", false);
    m_manage_lifecycle = params.get("manage_lifecycle", false);
    m_require_fresh_independent = params.get("require_fresh_independent", false);
    m_stop_before_battle = params.get("stop_before_battle", false);
    m_spatial_deferral_context.clear();
    m_lifecycle_battle_seen = false;
    m_lifecycle_solo_result.reset();
    m_pending_promotion_reward.reset();
    m_advertised_boss_family.clear();
    if (params.get("boss_advertisement_verified", false)) {
        const auto family = params.get("boss_family_name", std::string());
        if (!spa::known_boss_family(family)) {
            return false;
        }
        m_advertised_boss_family = family;
    }
    m_shopping_complete = false;
    m_last_coin_review_after_deployment_round = -1;
    m_last_economy_round = -1;
    m_joint_preparation_deadline = {};
    m_supply_confirmation_after_round = params.get("supply_confirmation_after_round", -1);
    m_previous_round_life.reset();
    m_replacement_round = -1;
    m_failed_replacements.clear();
    m_visited_replacement_lineups.clear();
    m_unavailable_replacement_sources.clear();
    m_pending_field_replacement.reset();
    m_blocked_purchases.clear();
    m_purchase_reserved_after_sale.reset();
    m_prefer_unclaimed = params.get("prefer_unclaimed_strategy", false);
    m_current_strategy_inspected = false;
    m_preparation_instruction_attempts.clear();
    m_fear_trap_cells.clear();
    m_fear_trap_round = -1;
    m_fear_trap_unverified = false;
    m_equipment_discount_round = -1;
    m_requested_strategy_id = params.get("strategy_id", std::string());
    const auto schedule_badges = params.find<bool>("schedule_strategy_badges");
    if (params.contains("schedule_strategy_badges") && !schedule_badges) {
        return false;
    }
    m_schedule_strategy_badges = schedule_badges.value_or(false);
    if (m_schedule_strategy_badges &&
        (m_mode != "independent" || !m_auto_run || !m_manage_lifecycle || !m_require_fresh_independent ||
         m_resume_after_round != 1 || m_difficulty < 1 || m_difficulty > 20 || !m_requested_strategy_id.empty() ||
         params.get("assisted_run", false) || params.get("decision_only", false) || m_stop_before_battle ||
         params.get("stop_after_deployment", false) || params.get("movement_only", false) ||
         params.get("round_only", false))) {
        return false;
    }
    m_client_type = params.get("client_type", std::string("Bilibili"));
    m_auto_deploy = params.get("auto_deploy", false);
    const auto auto_direction = params.get("auto_deploy_direction", std::string("E"));
    if (auto_direction.size() == 1) {
        m_auto_deploy_direction = auto_direction.front();
    }
    m_max_actions = params.get("max_actions", m_auto_run ? 300 : 10);
    m_decision_only = params.get("decision_only", false);
    m_stop_after_deployment = params.get("stop_after_deployment", false);
    m_policy = {};
    m_equipment_candidates.clear();
    m_equipped_items.clear();
    m_equipment_source_hints.clear();
    m_equipment_inventory_dirty = true;
    m_equipment_inventory_scan_rounds.clear();
    m_observed_bench_summons.clear();
    m_preparation_summon_attempts.clear();
    m_confirmed_summon_cells.clear();
    m_equipment_capacity_blocked_round = -1;
    m_inventory_operator_hints.clear();
    m_persistent_equipment_attempts.clear();
    m_cash_equipment_attempted_rounds.clear();
    const int cash_attempt = params.get("cash_equipment_attempted_round", -1);
    if (cash_attempt != -1 && (cash_attempt < 1 || cash_attempt > 20)) {
        return false;
    }
    if (cash_attempt >= 1) {
        m_cash_equipment_attempted_rounds.insert(cash_attempt);
    }
    m_pending_cash_equipment.reset();
    m_cash_equipment_source_verified_round = -1;
    m_passbook_attempted_rounds.clear();
    m_passbook_source_verified_round = -1;
    const int passbook_attempt = params.get("passbook_attempted_round", -1);
    if (passbook_attempt != -1 && (passbook_attempt < 1 || passbook_attempt > 20)) {
        return false;
    }
    if (passbook_attempt >= 1) {
        m_passbook_attempted_rounds.insert(passbook_attempt);
    }
    m_pending_direct_promotion.reset();
    m_direct_promotion_attempted_rounds.clear();
    m_direct_promotion_observed_rounds.clear();
    m_spatial_snapshot_round = -1;
    if (params.contains("direct_promotion_pending") && !params.at("direct_promotion_pending").is_null()) {
        const auto pending = params.find<json::object>("direct_promotion_pending");
        if (!pending) {
            return false;
        }
        // Preserve unknown contact; recovery only observes the actual form.
        // Neither a stale slot nor a missing source licenses another input.
        m_pending_direct_promotion = *pending;
        (*m_pending_direct_promotion)["restored_from_checkpoint"] = true;
        (*m_pending_direct_promotion)["equipment_replay_allowed"] = false;
    }
    if (params.contains("cash_equipment_pending") && !params.at("cash_equipment_pending").is_null()) {
        const auto pending = params.find<json::object>("cash_equipment_pending");
        if (!pending) {
            return false;
        }
        // Preserve even an incomplete pending record. Unknown contact or
        // consumption cannot be converted into permission to equip again.
        m_pending_cash_equipment = spa::restored_cash_pending(*pending);
    }
    m_delayed_cash_equipment_attempted_rounds.clear();
    m_delayed_cash_rejected_recipient.clear();
    m_pending_delayed_cash_equipment.reset();
    const int delayed_cash_attempt = params.get("delayed_cash_attempted_round", -1);
    if (delayed_cash_attempt != -1 && (delayed_cash_attempt < 1 || delayed_cash_attempt > 20)) {
        return false;
    }
    if (delayed_cash_attempt >= 1) {
        m_delayed_cash_equipment_attempted_rounds.insert(delayed_cash_attempt);
    }
    if (const auto pending = params.find<json::object>("delayed_cash_equipment_pending")) {
        const int source_round = pending->get("source_round", -1);
        const int quote = pending->get("next_round_credit_quote", -1);
        const auto equipment = pending->get("equipment", std::string());
        const bool valid_quote = quote == 2 || (spa::is_recurring_cash_equipment(equipment) ? quote == 1 : quote == 4);
        if (source_round < 1 || source_round > 20 || (quote != 0 && !valid_quote) ||
            !spa::is_deferred_cash_equipment(equipment) ||
            !spa::operator_pacts().contains(pending->get("operator", std::string())) ||
            (pending->get("consumption_verified", false) &&
             (!pending->get("input_committed", false) || !valid_quote))) {
            return false;
        }
        m_pending_delayed_cash_equipment = *pending;
        m_delayed_cash_equipment_attempted_rounds.insert(source_round);
    }
    m_pact_recruitment_attempted_rounds.clear();
    m_pending_pact_recruitment = params.get("pact_recruitment_pending", std::string());
    const auto recruitment_pacts = spa::pact_requirements();
    if (!m_pending_pact_recruitment.empty() &&
        std::none_of(recruitment_pacts.begin(), recruitment_pacts.end(), [&](const auto& pact) {
            return pact.name == m_pending_pact_recruitment;
        })) {
        return false;
    }
    m_cash_equipment_acquired_round = params.get("cash_equipment_acquired_round", -1);
    if (m_cash_equipment_acquired_round < -1 || m_cash_equipment_acquired_round > 20) {
        return false;
    }
    if (const auto hints = params.find<json::array>("equipment_candidates")) {
        for (const auto& hint : *hints) {
            if (!hint.is_string()) {
                return false;
            }
            const auto name = hint.as_string();
            if (std::none_of(spa::equipment_profiles().begin(), spa::equipment_profiles().end(), [&](const auto& item) {
                    return item.name == name;
                })) {
                return false;
            }
            m_equipment_candidates.insert(name);
        }
    }
    m_basic_upgrade_done_rounds.clear();
    m_dynamic_upgrade_done_rounds.clear();
    m_strategy_refresh_attempts.clear();
    m_full_bench_refresh_attempts.clear();
    m_strategy_refresh_blocked_rounds.clear();
    m_pending_promotion = params.get("promotion_pending", std::string());
    if ((params.contains("bench_recount_pending") && !params.find<bool>("bench_recount_pending")) ||
        (params.contains("tactical_elite_purchase_pending") && !params.find<bool>("tactical_elite_purchase_pending"))) {
        return false;
    }
    m_bench_recount_pending = params.get("bench_recount_pending", false);
    m_policy.tactical_elite_purchase_pending = params.get("tactical_elite_purchase_pending", false);
    if (!m_pending_promotion.empty() && !m_pending_pact_recruitment.empty()) {
        return false;
    }
    m_promotion_retry_count = 0;
    m_policy.primary_pact = params.get("primary_pact", std::string());
    m_policy.requested_primary_pact = params.get("requested_primary_pact", std::string("auto"));
    // An explicit primary_pact is normally a hard route for deterministic
    // tests.  Callers that only want to express an opening preference can set
    // dynamic_primary_pact=true; the live shop is then allowed to pivot until
    // the game-side faction activation is verified.  "auto" keeps the
    // backwards-compatible all-candidate mode.
    m_dynamic_primary_pact = m_policy.primary_pact == "auto" || params.get("dynamic_primary_pact", false);
    m_policy.bench_occupied = params.get("bench_occupied", -1);
    m_policy.primary_deadline_round = params.get("primary_deadline_round", 6);
    m_policy.shop_level = params.get("shop_level", 0);
    m_policy.allow_refresh = params.get("allow_refresh", true);
    m_policy.derive_support_pacts = params.get("derive_support_pacts", true);
    m_policy.dynamic_primary_pact = m_dynamic_primary_pact;
    // A resumed native invocation starts from the checkpointed preparation
    // round. This prevents the first observation from treating a persisted
    // same-round deployment suppression as stale and clearing it immediately.
    m_policy.round = m_resume_after_round;
    m_policy.minimum_recruit_price = params.get("minimum_recruit_price", 2);
    m_policy.basic_mode = params.get("basic_mode", false);
    m_policy.dynamic_decision_mode = params.get("dynamic_decision_mode", false);
    m_egir_chain_validation_mode = params.get("egir_chain_validation_mode", true);
    // A fresh autonomous run has no owned roster yet.  Some phase-one
    // launchers used to carry the setup-screen placeholder (three support
    // cards) into this field, which made the deployment gate fire before the
    // first shop purchase.  That produced repeated 3-4 second failed drags
    // and made an otherwise cheap preparation step look like a 20-second
    // planner decision.  With an empty persisted roster the authoritative
    // starting bench is zero; resumed runs with a real roster keep the value
    // supplied by the checkpoint.
    if (m_auto_run && m_policy.basic_mode && m_policy.owned.empty() && m_resume_after_round == 1 &&
        m_policy.bench_occupied == 3) {
        m_policy.bench_occupied = 0;
    }
    m_policy.basic_upgrade_rounds.clear();
    if (const auto upgrade_rounds = params.find<json::array>("basic_upgrade_rounds")) {
        for (const auto& value : *upgrade_rounds) {
            if (!value.is_number() || value.as_integer() < 1 || value.as_integer() > 20) {
                return false;
            }
            m_policy.basic_upgrade_rounds.emplace_back(value.as_integer());
        }
    }
    m_basic_upgrade_done_rounds.clear();
    if (const auto synthesis = params.find<json::object>("promotion_copies")) {
        for (const auto& [name, value] : *synthesis) {
            if (!value.is_number() || value.as_integer() < 2 || value.as_integer() > 6) {
                return false;
            }
            m_policy.promotion_copies.emplace(name, value.as_integer());
        }
    }
    const auto rules = spa::pact_requirements();
    if (m_policy.requested_primary_pact != "auto" && !m_policy.requested_primary_pact.empty()) {
        if (std::none_of(rules.begin(), rules.end(), [&](const auto& rule) {
                return rule.core && rule.name == m_policy.requested_primary_pact;
            })) {
            return false;
        }
        m_policy.primary_pact = m_policy.requested_primary_pact;
    }
    if (m_run_id.empty() || !spa::valid_difficulty_request(m_difficulty, m_mode == "joint" ? 4 : 20) ||
        m_max_actions < 1 || m_max_actions > (m_auto_run ? 300 : 30) || m_policy.bench_occupied < 0 ||
        m_policy.bench_occupied > 10 || m_policy.minimum_recruit_price < 1 || m_policy.minimum_recruit_price > 6 ||
        m_policy.primary_deadline_round < 3 || m_policy.primary_deadline_round > 7 || m_policy.shop_level < 0 ||
        m_policy.shop_level > 6 ||
        (m_auto_deploy && std::string("ESWN").find(m_auto_deploy_direction) == std::string::npos) ||
        (!m_dynamic_primary_pact && std::none_of(rules.begin(), rules.end(), [&](const auto& r) {
            return r.name == m_policy.primary_pact && r.core;
        }))) {
        return false;
    }
    const auto enabled = params.find<json::array>("enabled_core_pacts");
    bool primary_enabled = false;
    if (!enabled && !m_dynamic_primary_pact) {
        return false;
    }
    m_enabled_core_pacts.clear();
    if (enabled && !enabled->empty()) {
        for (const auto& value : *enabled) {
            if (!value.is_string() || std::none_of(rules.begin(), rules.end(), [&](const auto& r) {
                    return r.core && r.name == value.as_string();
                })) {
                return false;
            }
            primary_enabled = primary_enabled || m_dynamic_primary_pact || value.as_string() == m_policy.primary_pact;
            m_enabled_core_pacts.emplace_back(value.as_string());
        }
    }
    else if (m_dynamic_primary_pact) {
        // Auto mode is a live faction search.  An omitted allow-list means
        // all core factions are candidates; the later disabled-pact filter
        // still removes opening-banned factions before formation evaluation.
        for (const auto& rule : rules) {
            if (rule.core) {
                m_enabled_core_pacts.emplace_back(rule.name);
            }
        }
        primary_enabled = true;
    }
    else {
        return false;
    }
    if (!primary_enabled) {
        return false;
    }
    const auto carries = params.find<json::array>("carries");
    const auto roster = params.find<json::array>("roster");
    const auto catalog = spa::operator_pacts();
    if (((!carries || carries->empty()) && !m_dynamic_primary_pact) || !roster) {
        return false;
    }
    if (carries) {
        for (const auto& value : *carries) {
            if (!value.is_string()) {
                return false;
            }
            const auto name = value.as_string();
            const auto found = catalog.find(name);
            if (found == catalog.end() ||
                (!m_dynamic_primary_pact &&
                 std::find(found->second.begin(), found->second.end(), m_policy.primary_pact) == found->second.end())) {
                return false;
            }
            m_policy.carries.emplace_back(name);
        }
    }
    for (const auto& unit : *roster) {
        if (!unit.is_object()) {
            return false;
        }
        const auto name = unit.find<std::string>("name");
        spa::OwnedOperator owned;
        if (!name || !catalog.contains(*name) || !spa::restore_owned_operator(unit, owned)) {
            return false;
        }
        const auto counts = spa::owned_form_counts(owned);
        const auto synthesis = m_policy.promotion_copies.find(*name);
        const int required =
            synthesis == m_policy.promotion_copies.end() ? spa::promotion_copies_required(*name) : synthesis->second;
        if ((counts && counts->initial >= required && m_pending_promotion != *name) ||
            !m_policy.owned.emplace(*name, std::move(owned)).second) {
            return false;
        }
    }
    if (const auto slots = params.find<json::array>("equipment_slots")) {
        for (const auto& entry : *slots) {
            const auto name = entry.find<std::string>("name");
            const auto items = entry.find<json::array>("items");
            if (!name || !items || items->size() > 2 || !m_policy.owned.contains(*name) ||
                m_equipped_items.contains(*name)) {
                return false;
            }
            std::vector<std::string> verified;
            for (const auto& item : *items) {
                if (!item.is_string() || !spa::equipment_title_matches(item.as_string(), item.as_string()) ||
                    std::find(verified.begin(), verified.end(), item.as_string()) != verified.end()) {
                    return false;
                }
                verified.push_back(item.as_string());
            }
            m_equipped_items.emplace(*name, std::move(verified));
        }
    }
    m_verify_layout_identities = params.get("verify_layout_identities", false);
    m_layout_screen_points.clear();
    m_layout_supplied = false;
    m_layout_units.clear();
    m_layout_lanes.clear();
    m_deployable_cells.clear();
    const auto read_cell = [](const json::value& value) -> std::optional<spa::BoardCell> {
        const auto col = value.find<int>("col");
        const auto row = value.find<int>("row");
        if (!col || !row || *col < -40 || *col > 40 || *row < -40 || *row > 40) {
            return std::nullopt;
        }
        return spa::BoardCell { *col, *row };
    };
    if (const auto layout = params.find<json::object>("layout")) {
        m_layout_supplied = true;
        const auto units = layout->find<json::array>("units");
        const auto lanes = layout->find<json::array>("lanes");
        const auto cells = layout->find<json::array>("deployable_cells");
        if (!units || !lanes || !cells || units->size() > 20 || lanes->size() > 8 || cells->size() > 400) {
            return false;
        }
        for (const auto& entry : *units) {
            spa::DeploymentUnit unit;
            unit.id = entry.get("name", std::string());
            const auto position = read_cell(entry);
            const auto facing = entry.get("facing", std::string());
            const auto range = entry.find<json::array>("range");
            const auto owned = m_policy.owned.find(unit.id);
            if (!position || facing.size() != 1 || owned == m_policy.owned.end() || !owned->second.deployed ||
                (range && range->size() > 100)) {
                return false;
            }
            if (m_verify_layout_identities) {
                const auto x = entry.find<int>("screen_x");
                const auto y = entry.find<int>("screen_y");
                if (!x || !y || *x < 180 || *x >= 1000 || *y < 140 || *y >= 420 ||
                    !m_layout_screen_points.emplace(unit.id, std::pair { *x, *y }).second) {
                    return false;
                }
            }
            unit.cell = *position;
            unit.facing = facing[0];
            unit.block_count = entry.get("block_count", 0);
            unit.frontline = entry.get("frontline", false);
            unit.damage_output = entry.get("damage_output", false);
            unit.healer = entry.get("healer", false);
            unit.accepts_direct_healing = entry.get("accepts_direct_healing", true);
            unit.provides_regeneration = entry.get("provides_regeneration", false);
            unit.range_known = entry.get("range_known", false);
            if (range) {
                for (const auto& offset : *range) {
                    const auto cell = read_cell(offset);
                    if (!cell) {
                        return false;
                    }
                    unit.local_range.emplace_back(*cell);
                }
            }
            if (const auto placements = entry.find<json::array>("placement_cells")) {
                if (placements->size() > 400) {
                    return false;
                }
                for (const auto& point : *placements) {
                    const auto cell = read_cell(point);
                    if (!cell) {
                        return false;
                    }
                    unit.placement_cells.emplace_back(*cell);
                }
            }
            m_layout_units.emplace_back(std::move(unit));
        }
        for (const auto& entry : *lanes) {
            spa::DeploymentLane lane;
            lane.id = entry.get("name", std::string());
            lane.minimum_engagement_column = entry.get("minimum_engagement_col", 0);
            lane.precontact_cells = entry.get("precontact_cells", 2);
            const auto path = entry.find<json::array>("path");
            if (!path || path->size() > 400) {
                return false;
            }
            for (const auto& point : *path) {
                const auto cell = read_cell(point);
                if (!cell) {
                    return false;
                }
                lane.path.emplace_back(*cell);
            }
            m_layout_lanes.emplace_back(std::move(lane));
        }
        for (const auto& point : *cells) {
            const auto cell = read_cell(point);
            if (!cell) {
                return false;
            }
            m_deployable_cells.emplace_back(*cell);
        }
    }
    m_movement_only = params.get("movement_only", false);
    m_phase_one_game_layout = m_policy.basic_mode && m_auto_run && !m_movement_only && !m_verify_layout_identities;
    m_round_only = params.get("round_only", false);
    m_spending_reviewed = params.get("spending_reviewed", false);
    m_spending_reason = params.get("spending_reason", std::string());
    m_last_remaining_deployment.reset();
    if (const auto remaining = params.find<int>("remaining_deployment");
        remaining && *remaining >= 0 && *remaining <= 8) {
        m_last_remaining_deployment = *remaining;
    }
    m_strategy_context = {};
    m_strategy_context.initial_brief = params.get("initial_brief", std::string());
    m_selected_strategy_id = params.get("selected_strategy_id", std::string());
    const auto read_strings = [](const json::value& value, const char* key) {
        std::vector<std::string> result;
        if (const auto values = value.find<json::array>(key)) {
            for (const auto& entry : *values) {
                if (entry.is_string()) {
                    result.emplace_back(entry.as_string());
                }
            }
        }
        return result;
    };
    m_declared_disabled_pacts = read_strings(params, "disabled_pacts");
    m_policy.occupied_strategy_ids =
        m_mode == "joint" ? read_strings(params, "occupied_strategy_ids") : std::vector<std::string> {};
    m_observed_disabled_pacts.clear();
    m_configured_core_pacts = m_enabled_core_pacts;
    refresh_mode_pact_restrictions();
    if (m_enabled_core_pacts.empty()) {
        return false;
    }
    if (!m_dynamic_primary_pact && std::find(
                                       m_strategy_context.disabled_pacts.begin(),
                                       m_strategy_context.disabled_pacts.end(),
                                       m_policy.primary_pact) != m_strategy_context.disabled_pacts.end()) {
        return false;
    }
    if (const auto supports = params.find<json::array>("support_pacts")) {
        for (const auto& value : *supports) {
            if (!value.is_string()) {
                return false;
            }
            const auto name = value.as_string();
            if (std::none_of(
                    rules.begin(),
                    rules.end(),
                    [&](const auto& rule) { return rule.name == name && !rule.core && rule.members >= 2; }) ||
                std::find(m_strategy_context.disabled_pacts.begin(), m_strategy_context.disabled_pacts.end(), name) !=
                    m_strategy_context.disabled_pacts.end() ||
                std::find(m_policy.support_pacts.begin(), m_policy.support_pacts.end(), name) !=
                    m_policy.support_pacts.end()) {
                return false;
            }
            m_policy.support_pacts.emplace_back(name);
        }
    }
    m_strategy_context.disabled_strategy_ids = read_strings(params, "disabled_strategy_ids");
    m_strategy_context.active_pacts = read_strings(params, "active_pacts");
    m_strategy_context.core_layer_operator_verified = params.get("core_layer_operator_verified", false);
    m_strategy_context.damage_carry_verified = params.get("damage_carry_verified", false);
    m_strategy_context.opening_selection = params.get("opening_strategy_selection", false);
    m_strategy_candidates.clear();
    if (const auto candidates = params.find<json::array>("strategy_candidates")) {
        for (const auto& entry : *candidates) {
            if (!entry.is_object()) {
                return false;
            }
            spa::StrategyCandidate candidate;
            candidate.id = entry.get("id", std::string());
            candidate.name = entry.get("name", std::string());
            candidate.primary_pacts = read_strings(entry, "primary_pacts");
            candidate.required_pacts = read_strings(entry, "required_pacts");
            candidate.forbidden_bans = read_strings(entry, "forbidden_bans");
            candidate.required_initial_terms = read_strings(entry, "required_initial_terms");
            candidate.observed_support = entry.get("observed_support", 0);
            candidate.badge_verified = entry.get("badge_verified", false);
            candidate.layer_sources = read_strings(entry, "layer_sources");
            candidate.layer_cap_per_round = entry.get("layer_cap_per_round", 0);
            candidate.layer_sources_verified = entry.get("layer_sources_verified", false);
            candidate.requires_core_roles = entry.get("requires_core_roles", false);
            candidate.requires_active_primary_pact = entry.get("requires_active_primary_pact", true);
            if (candidate.id.empty() || candidate.name.empty() || candidate.observed_support < 0) {
                return false;
            }
            m_strategy_candidates.emplace_back(std::move(candidate));
        }
    }
    if (m_spending_reason.size() > 512) {
        return false;
    }
    if ((m_auto_run && (m_round_only || m_decision_only || m_movement_only || m_stop_after_deployment)) ||
        (m_round_only && (m_decision_only || m_movement_only))) {
        return false;
    }
    m_deployment_targets.clear();
    m_auto_deploy_positions.clear();
    m_growth_population_poses.clear();
    m_row_growth_populations.clear();
    m_population_grid_members.clear();
    m_verified_field_pose_points.clear();
    m_committed_facings.clear();
    m_spatial_orientation_attempts.clear();
    m_spatial_grid_reference.release();
    m_spatial_grid_projection.release();
    m_spatial_map_id.clear();
    if (const auto prior = params.find<json::object>("registered_grid")) {
        const auto values = prior->find<json::array>("projection");
        const auto file = prior->get("reference_file", std::string());
        const auto map_id = prior->get("map_id", std::string());
        if (!values || values->size() != 9 || file.empty() || !utils::path(file).is_absolute() ||
            !spa::terrain_layout_model(map_id)) {
            return false;
        }
        cv::Mat projection(3, 3, CV_64F);
        for (size_t index = 0; index < 9; ++index) {
            if (!values->at(index).is_number()) {
                return false;
            }
            projection.at<double>(static_cast<int>(index / 3), static_cast<int>(index % 3)) =
                values->at(index).as_double();
        }
        const auto reference = MAA_NS::imread(utils::path(file));
        if (reference.type() != CV_8UC3 || reference.size() != cv::Size(1280, 720) || !cv::checkRange(projection) ||
            std::abs(cv::determinant(projection)) < 1e-8) {
            return false;
        }
        m_spatial_grid_reference = reference;
        m_spatial_grid_projection = projection;
        m_spatial_map_id = map_id;
    }
    m_spatial_boss_phase = false;
    m_spatial_boss_phase_round = 0;
    m_boss_attackable_cells.clear();
    m_boss_attackable_map_id.clear();
    m_boss_attackable_round = 0;
    m_dynamic_layout_target.clear();
    m_dynamic_layout_expected_cells.clear();
    m_dynamic_layout_context.clear();
    m_failed_spatial_actions.clear();
    if (const auto failed = params.find<json::array>("failed_spatial_actions")) {
        if (failed->size() > 512) {
            return false;
        }
        for (const auto& key : *failed) {
            if (!key.is_string() || key.as_string().empty() || key.as_string().size() > 8192) {
                return false;
            }
            m_failed_spatial_actions.insert(key.as_string());
        }
    }
    m_egir_analysis_context.clear();
    m_egir_plan_context.clear();
    m_failed_layout_context.clear();
    m_dynamic_layout_attempts.clear();
    m_spatial_reconciliation_attempts.clear();
    m_dynamic_layout_observed = false;
    m_field_checkpoint_loaded = false;
    m_spatial_grid_refresh_attempts.clear();
    m_recent_bench_acquisition.clear();
    m_pending_spatial_orientation.reset();
    m_pending_orientation_verifier.reset();
    if (const auto pending = params.find<json::object>("pending_orientation")) {
        const auto name = pending->get("name", std::string());
        if (name.empty() || !m_policy.owned.contains(name) || pending->get("x", -1) < 180 ||
            pending->get("x", -1) >= 1000 || pending->get("y", -1) < 140 || pending->get("y", -1) >= 510 ||
            pending->get("remaining_deployment", -1) < 0 || pending->get("remaining_deployment", -1) > 8) {
            return false;
        }
        m_pending_spatial_orientation = *pending;
    }
    m_auto_deploy_used_cells.clear();
    if (const auto points = params.find<json::array>("used_deploy_points")) {
        if (points->size() > 100) {
            return false;
        }
        for (const auto& point : *points) {
            const int x = point.get("x", -1), y = point.get("y", -1);
            if (x < 180 || x >= 1000 || y < 140 || y >= 620) {
                return false;
            }
            m_auto_deploy_used_cells.emplace(x, y);
            const auto name = point.get("name", std::string());
            if (const auto unit = m_policy.owned.find(name); unit != m_policy.owned.end() && unit->second.deployed) {
                m_auto_deploy_positions.emplace(name, std::pair { x, y });
            }
        }
    }
    if (const auto positions = params.find<json::array>("field_positions")) {
        m_field_checkpoint_loaded = !positions->empty();
        if (positions->size() > 16) {
            return false;
        }
        m_auto_deploy_positions.clear();
        for (const auto& point : *positions) {
            const auto name = point.get("name", std::string());
            const int x = point.get("x", -1), y = point.get("y", -1);
            const auto owned = m_policy.owned.find(name);
            if (owned == m_policy.owned.end() || !owned->second.deployed || x < 180 || x >= 1000 || y < 140 ||
                y >= 510) {
                return false;
            }
            m_auto_deploy_positions.emplace(name, std::pair { x, y });
            if (point.get("field_pose_verified", false) &&
                point.get("coordinate_frame", std::string()) == "collapsed") {
                m_verified_field_pose_points.emplace(x, y);
                const auto facing = point.get("committed_facing", std::string());
                if (facing.size() == 1 && std::string("ENSW").find(facing[0]) != std::string::npos) {
                    m_committed_facings[{ x, y }] = facing[0];
                }
            }
        }
    }
    m_auto_deploy_blocked_names.clear();
    if (const auto saved = params.find<json::object>("pending_replacement")) {
        const auto incoming = saved->get("incoming", std::string());
        const auto outgoing = saved->get("outgoing", std::string());
        const int x = saved->get("x", -1), y = saved->get("y", -1);
        if (incoming == outgoing || !m_policy.owned.contains(incoming) || !m_policy.owned.contains(outgoing) ||
            x < 180 || x >= 1000 || y < 140 || y >= 495) {
            return false;
        }
        const auto& source = m_policy.owned.at(incoming);
        const auto& target = m_policy.owned.at(outgoing);
        const bool incoming_has_bench_copy =
            !source.deployed ||
            (!source.elite && source.copies > static_cast<int>(m_auto_deploy_positions.count(incoming)));
        if (source.copies <= 0 || target.copies <= 0 || !incoming_has_bench_copy || !target.deployed) {
            return false;
        }
        m_pending_field_replacement =
            PendingReplacement { nullptr,       incoming,     outgoing,     Point(x, y), source.copies,
                                 target.copies, source.elite, target.elite, -1,          true };
        const auto saved_facing = saved->get("committed_facing", std::string());
        if (saved_facing.size() == 1 && std::string("ENSW").find(saved_facing[0]) != std::string::npos) {
            m_pending_field_replacement->committed_facing = saved_facing[0];
        }
    }
    if (const auto blocked = params.find<json::array>("deployment_blocked_names")) {
        const int blocked_round = params.get("deployment_blocked_round", m_resume_after_round);
        if (blocked_round == m_resume_after_round) {
            for (const auto& value : *blocked) {
                if (value.is_string() && (!value.as_string().empty())) {
                    m_auto_deploy_blocked_names.insert(value.as_string());
                }
            }
        }
    }
    m_auto_deploy_suppressed_round = -1;
    m_swap_deploy_round = -1;
    if (const auto targets = params.find<json::array>("deployment_targets")) {
        if (targets->size() > 8 || m_round_only || m_movement_only) {
            return false;
        }
        for (const auto& target : *targets) {
            const auto name = target.get("name", std::string());
            const auto direction = target.get("direction", std::string());
            if (!catalog.contains(name) || direction.size() != 1 ||
                std::string("ESWN").find(direction) == std::string::npos) {
                return false;
            }
            for (const auto axis : { "target_x", "target_y", "drag_target_x", "drag_target_y" }) {
                const auto value = target.find<int>(axis);
                const bool vertical = std::string(axis).ends_with("_y");
                if (!value || *value < (vertical ? 140 : 180) ||
                    *value >= (vertical ? (std::string(axis).starts_with("drag_") ? 620 : 420) : 1000)) {
                    return false;
                }
            }
            for (const auto& [existing, point] : m_deployment_targets) {
                if (point.get("target_x", -1) == target.get("target_x", -2) &&
                    point.get("target_y", -1) == target.get("target_y", -2)) {
                    return false;
                }
            }
            if (!m_deployment_targets.emplace(name, target.as_object()).second) {
                return false;
            }
        }
    }
    if (m_stop_after_deployment && m_deployment_targets.empty()) {
        return false;
    }
    if (m_auto_deploy && !m_deployment_targets.empty()) {
        // Automatic cell discovery and hand-authored targets are separate
        // operating modes.  Mixing them would make target ownership and
        // rollback ambiguous after a failed drag.
        return false;
    }
    m_movement_plan.clear();
    const auto plan = params.find<json::array>("movement_plan");
    if ((m_movement_only && (m_decision_only || !plan || plan->empty() || plan->size() > 20)) ||
        (!m_movement_only && plan && !plan->empty())) {
        return false;
    }
    if (m_movement_only) {
        std::map<std::string, spa::BoardCell> positions;
        for (const auto& unit : m_layout_units) {
            if (!positions.emplace(unit.id, unit.cell).second) {
                return false;
            }
        }
        for (const auto& entry : *plan) {
            const auto name = entry.get("name", std::string());
            const auto found = positions.find(name);
            const auto direction = entry.get("direction", std::string());
            if (found == positions.end() || direction.size() != 1 ||
                std::string("ESWN").find(direction) == std::string::npos) {
                return false;
            }
            for (const auto field : { "source_x",
                                      "source_y",
                                      "target_x",
                                      "target_y",
                                      "drag_target_x",
                                      "drag_target_y",
                                      "drag_origin_x",
                                      "drag_origin_y",
                                      "from_col",
                                      "from_row",
                                      "to_col",
                                      "to_row" }) {
                if (!entry.find<int>(field)) {
                    return false;
                }
            }
            const auto valid_point = [&](const char* x_key, const char* y_key, int bottom) {
                const int x = entry.get(x_key, -1);
                const int y = entry.get(y_key, -1);
                return x >= 180 && x < 1000 && y >= 140 && y < bottom;
            };
            if (!valid_point("source_x", "source_y", 420) || !valid_point("target_x", "target_y", 420) ||
                !valid_point("drag_origin_x", "drag_origin_y", 510) ||
                !valid_point("drag_target_x", "drag_target_y", 510)) {
                return false;
            }
            const spa::BoardCell from { entry.get("from_col", -100), entry.get("from_row", -100) };
            const spa::BoardCell to { entry.get("to_col", -100), entry.get("to_row", -100) };
            if (found->second != from || from == to || to.first < -40 || to.first > 40 || to.second < -40 ||
                to.second > 40 ||
                std::any_of(positions.begin(), positions.end(), [&](const auto& unit) { return unit.second == to; })) {
                return false;
            }
            found->second = to;
            m_movement_plan.emplace_back(
                json::object { { "name", name },
                               { "direction", direction },
                               { "source_x", entry.get("source_x", -1) },
                               { "source_y", entry.get("source_y", -1) },
                               { "target_x", entry.get("target_x", -1) },
                               { "target_y", entry.get("target_y", -1) },
                               { "drag_target_x", entry.get("drag_target_x", -1) },
                               { "drag_target_y", entry.get("drag_target_y", -1) },
                               { "drag_origin_x", entry.get("drag_origin_x", -1) },
                               { "drag_origin_y", entry.get("drag_origin_y", -1) },
                               { "to_col", to.first },
                               { "to_row", to.second } });
        }
    }
    return true;
}

bool asst::StrongholdProtocolTask::handle_reward_page(int after_round)
{
    const auto image = ctrler()->get_image();
    if (image.empty() || image.cols != 1280 || image.rows != 720 || after_round < 1 || after_round > 20) {
        return false;
    }
    OCRer ocr(image);
    const auto recognized = ocr.analyze();
    if (!recognized) {
        return false;
    }
    std::vector<spa::TextObservation> texts;
    texts.reserve(recognized->size());
    for (const auto& text : *recognized) {
        texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
    }
    const auto page = spa::observe_page(texts, {});
    if (page.contradictory || (page.page != spa::Page::EquipmentSupply && page.page != spa::Page::BountySelection &&
                               page.page != spa::Page::TacticalDecision)) {
        return false;
    }

    if (page.page == spa::Page::TacticalDecision) {
        if (m_mode == "joint" && spa::supply_own_turn(texts) != true) {
            auto waiting = basic_info_with_what("SPAJointTacticalWaiting");
            waiting["details"] = json::object { { "own_turn_verified", false }, { "input_issued", false } };
            callback(AsstMsg::SubTaskExtraInfo, waiting);
            return false;
        }
        const auto offers = spa::observe_tactical_offers(texts);
        std::vector<spa::RosterUnit> roster;
        for (const auto& [name, unit] : m_policy.owned) {
            roster.push_back({ name, unit.deployed, field_added_pacts(name), unit.elite });
        }
        const auto formation = spa::evaluate_formation(
            roster,
            spa::operator_pacts(),
            spa::pact_requirements(),
            spa::enabled_formation_pacts(m_enabled_core_pacts, m_policy.disabled_pacts));
        int selected = spa::choose_tactical_adjustment(
            offers,
            m_policy.primary_pact,
            formation.complete ? formation.active_pacts : std::vector<std::string> {},
            m_policy.bench_occupied);
        if (selected < 0) {
            // This verified page grants a free friendly adjustment. If all
            // three titles/descriptions are readable, an unmodeled effect
            // must not trap the same game in an endless observation loop.
            const bool complete =
                offers.size() == 3 && std::all_of(offers.begin(), offers.end(), [](const auto& offer) {
                    return offer.slot >= 0 && offer.slot <= 2 && !offer.title.empty() && !offer.description.empty();
                });
            if (!complete) {
                LogWarn << "SPA tactical adjustment cards are incomplete";
                return false;
            }
            // Do not confirm an incomplete quote whose effect changes the
            // next purchase's form. Other readable cards remain usable.
            const auto alternative = std::find_if(offers.begin(), offers.end(), [](const auto& offer) {
                return offer.title != "升华" || spa::tactical_elite_purchase(offer);
            });
            if (alternative == offers.end()) {
                return false;
            }
            selected = static_cast<int>(alternative - offers.begin());
            auto fallback = basic_info_with_what("SPATacticalModelFallback");
            fallback["details"] = json::object { { "title", offers[selected].title },
                                                 { "effect_value_known", false },
                                                 { "page_and_all_card_texts_verified", true },
                                                 { "paid_purchase", false } };
            callback(AsstMsg::SubTaskExtraInfo, fallback);
        }
        const auto& choice = offers[selected];
        for (bool confirm : { false, true }) {
            spa::ShopTaskPlugin action(m_callback, m_inst, TaskType);
            action.set_fast_mode(true);
            action.set_retry_times(0);
            action.set_task_id(m_task_id);
            action.set_task_ptr(this);
            if (!action.configure_tactical(choice.title, confirm, choice.slot) || !action.run()) {
                return false;
            }
        }
        if (spa::tactical_elite_purchase(choice)) {
            m_policy.tactical_elite_purchase_pending = true;
        }
        auto receipt = basic_info_with_what("SPAAutomationRewardSelected");
        receipt["details"] =
            json::object { { "after_round", after_round },
                           { "choice_kind", "tactical_decision" },
                           { "selection", choice.title },
                           { "description", choice.description },
                           { "slot", choice.slot },
                           { "returned_to_preparation", true },
                           { "run_id", m_run_id },
                           { "roster", roster_snapshot() },
                           { "tactical_elite_purchase_pending", m_policy.tactical_elite_purchase_pending },
                           { "effect_amount_verified", false },
                           { "run_victory_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        return true;
    }

    if (page.page == spa::Page::EquipmentSupply) {
        if (m_supply_confirmation_after_round == after_round) {
            return false;
        }
        // Card slots distinguish identical free rewards. Acquiring the best
        // available free card is separate from deciding whether to equip it.
        std::vector<spa::EquipmentProfile> offers;
        if (m_mode == "joint" && spa::supply_own_turn(texts) != true) {
            auto waiting = basic_info_with_what("SPAJointSupplyWaiting");
            waiting["details"] = json::object { { "own_turn_verified", false }, { "input_issued", false } };
            callback(AsstMsg::SubTaskExtraInfo, waiting);
            return false;
        }
        const auto supply_cards = spa::observe_supply_offers(texts);
        for (const auto& card : supply_cards) {
            const auto profile =
                std::find_if(spa::equipment_profiles().begin(), spa::equipment_profiles().end(), [&](const auto& item) {
                    return item.name == card.title;
                });
            if (profile != spa::equipment_profiles().end()) {
                auto reward = *profile;
                // The supply page explicitly grants a free choice. Catalogue
                // shop prices must not make every reward unaffordable against
                // the intentionally zero selection budget.
                reward.price = 0;
                offers.emplace_back(std::move(reward));
            }
        }
        if (offers.empty()) {
            auto blocked = basic_info_with_what("SPAAutomationRewardBlocked");
            blocked["details"] =
                json::object { { "after_round", after_round },
                               { "choice_kind", "equipment_supply" },
                               { "reason", "No reviewed equipment title was recognized in a card slot" },
                               { "run_victory_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, blocked);
            return false;
        }

        bool target_deployed = false;
        bool target_elite = false;
        bool healer_covers = false;
        bool target_is_carry = false;
        for (const auto& [name, unit] : m_policy.owned) {
            if (unit.deployed) {
                target_deployed = true;
                target_elite = target_elite || unit.elite;
            }
            if (unit.deployed && std::find_if(m_layout_units.begin(), m_layout_units.end(), [&](const auto& layout) {
                                     return layout.id == name && layout.healer;
                                 }) != m_layout_units.end()) {
                healer_covers = true;
            }
            if (unit.deployed &&
                std::find(m_policy.carries.begin(), m_policy.carries.end(), name) != m_policy.carries.end()) {
                target_is_carry = true;
            }
        }
        bool primary_active = std::find(
                                  m_strategy_context.active_pacts.begin(),
                                  m_strategy_context.active_pacts.end(),
                                  m_policy.primary_pact) != m_strategy_context.active_pacts.end();
        if (!primary_active) {
            std::vector<spa::RosterUnit> roster;
            roster.reserve(m_policy.owned.size());
            for (const auto& [name, unit] : m_policy.owned) {
                roster.push_back({ name, unit.deployed, field_added_pacts(name), unit.elite });
            }
            const auto formation = spa::evaluate_formation(
                roster,
                spa::operator_pacts(),
                spa::pact_requirements(),
                spa::enabled_formation_pacts(m_enabled_core_pacts, m_policy.disabled_pacts));
            primary_active =
                formation.complete &&
                std::find(formation.active_pacts.begin(), formation.active_pacts.end(), m_policy.primary_pact) !=
                    formation.active_pacts.end();
        }
        const int next_round = after_round + 1;
        const int boss_round = m_expected_boss_round > 0 ? m_expected_boss_round : 20;
        spa::EquipmentContext equipment_context {
            0, next_round, boss_round, target_deployed, target_elite, healer_covers, primary_active, target_is_carry
        };
        equipment_context.free_supply = true;
        const auto equipment = spa::choose_equipment(equipment_context, offers);
        if (!equipment.buy) {
            auto blocked = basic_info_with_what("SPAAutomationRewardBlocked");
            blocked["details"] =
                json::object { { "after_round", after_round },         { "choice_kind", "equipment_supply" },
                               { "reason", equipment.reason },         { "primary_active", primary_active },
                               { "target_deployed", target_deployed }, { "healer_covers", healer_covers },
                               { "target_is_carry", target_is_carry }, { "run_victory_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, blocked);
            return false;
        }
        const auto selected =
            std::find_if(offers.begin(), offers.end(), [&](const auto& offer) { return offer.name == equipment.name; });
        if (selected == offers.end()) {
            return false;
        }
        const auto selected_card = std::find_if(supply_cards.begin(), supply_cards.end(), [&](const auto& card) {
            return card.title == selected->name;
        });
        if (selected_card == supply_cards.end()) {
            return false;
        }
        spa::ShopTaskPlugin supply(m_callback, m_inst, TaskType);
        supply.set_fast_mode(m_policy.basic_mode || m_policy.dynamic_decision_mode);
        supply.set_retry_times(0);
        supply.set_task_id(m_task_id);
        supply.set_task_ptr(this);
        const bool supply_returned = supply.configure_supply(selected->name, selected_card->slot) && supply.run();
        if (supply.supply_confirmation_issued()) {
            m_supply_confirmation_after_round = after_round;
            m_equipment_candidates.insert(selected->name);
            m_equipment_inventory_dirty = true;
            auto pending = basic_info_with_what("SPASupplySelectionPending");
            pending["details"] = json::object { { "run_id", m_run_id },
                                                { "roster", roster_snapshot() },
                                                { "equipment_candidates", equipment_candidates_snapshot() },
                                                { "supply_confirmation_after_round", after_round },
                                                { "selection", selected->name },
                                                { "confirmation_issued", true },
                                                { "inventory_verified", false },
                                                { "returned_to_preparation", supply_returned } };
            callback(AsstMsg::SubTaskExtraInfo, pending);
        }
        if (!supply_returned) {
            return false;
        }
        m_equipment_candidates.insert(selected->name);
        if (selected->name == "盟约之币") {
            m_cash_equipment_acquired_round = after_round;
        }
        auto confirmed = basic_info_with_what("SPAAutomationRewardSelected");
        confirmed["details"] = json::object { { "after_round", after_round },
                                              { "choice_kind", "equipment_supply" },
                                              { "selection", selected->name },
                                              { "selection_slot", selected_card->slot },
                                              { "score", equipment.score },
                                              { "reason", equipment.reason },
                                              { "primary_active", primary_active },
                                              { "target_deployed", target_deployed },
                                              { "healer_covers", healer_covers },
                                              { "target_is_carry", target_is_carry },
                                              { "returned_to_preparation", true },
                                              { "inventory_verified", false },
                                              { "run_victory_verified", false },
                                              { "run_id", m_run_id },
                                              { "roster", roster_snapshot() },
                                              { "equipment_candidates", equipment_candidates_snapshot() },
                                              { "cash_equipment_acquired_round", m_cash_equipment_acquired_round } };
        callback(AsstMsg::SubTaskExtraInfo, confirmed);
        return true;
    }

    // Bounty cards do not expose a stable shared title catalogue.  Build a
    // card-local description from OCR and use a unique, nontrivial fragment
    // as the identity token.  The plugin verifies the token on both the
    // selection and confirmation pages, then verifies the preparation return.
    struct BountyCard
    {
        int left;
        int right;
        std::vector<std::pair<Rect, std::string>> parts;
    };

    std::array<BountyCard, 3> cards { { { 170, 470, {} }, { 470, 820, {} }, { 820, 1150, {} } } };
    for (const auto& text : *recognized) {
        if (text.score < 0.85 || text.rect.x < 170 || text.rect.x >= 1150 || text.rect.y < 330 || text.rect.y >= 430 ||
            text.text.size() < 2) {
            continue;
        }
        for (auto& card : cards) {
            if (text.rect.x >= card.left && text.rect.x < card.right) {
                card.parts.emplace_back(text.rect, text.text);
                break;
            }
        }
    }
    std::string token;
    std::vector<spa::BountyOffer> bounty_offers;
    const auto bounty_card_texts = spa::observe_tactical_offers(texts);
    for (const auto& card : cards) {
        if (card.parts.empty()) {
            continue;
        }
        auto parts = card.parts;
        std::sort(parts.begin(), parts.end(), [](const auto& lhs, const auto& rhs) {
            return std::tie(lhs.first.y, lhs.first.x) < std::tie(rhs.first.y, rhs.first.x);
        });
        std::string joined;
        for (const auto& part : parts) {
            joined += part.second;
        }
        if (joined.size() >= 2) {
            const auto& title = bounty_card_texts[&card - cards.data()].title;
            const int difficulty = spa::bounty_difficulty_from_title(title);
            bounty_offers.push_back(
                { title.empty() ? joined : title,
                  difficulty,
                  spa::bounty_requires_anti_air(title, joined),
                  static_cast<int>(&card - cards.data()),
                  spa::bounty_reward(joined).value_or(-1),
                  joined });
        }
    }
    // Owning a ranged operator alone does not prove air-lane coverage or
    // sufficient DPS. Prefer a ground bounty until that assessment is wired.
    const int selected_bounty = spa::choose_bounty(bounty_offers, false);
    if (selected_bounty >= 0) {
        json::array candidates;
        for (const auto& offer : bounty_offers) {
            candidates.emplace_back(
                json::object { { "title", offer.token },
                               { "description", offer.description },
                               { "difficulty", offer.difficulty },
                               { "reward", offer.reward },
                               { "flying", offer.flying },
                               { "slot", offer.slot } });
        }
        auto review = basic_info_with_what("SPABountyRiskReview");
        review["details"] = json::object { { "after_round", after_round },
                                           { "offers", std::move(candidates) },
                                           { "selected_slot", bounty_offers[selected_bounty].slot },
                                           { "primary_pact", m_policy.primary_pact },
                                           { "layers",
                                             m_policy.observed_pact_layers.contains(m_policy.primary_pact)
                                                 ? m_policy.observed_pact_layers.at(m_policy.primary_pact)
                                                 : -1 },
                                           { "combat_model_verified", false },
                                           { "reason",
                                             "Prefer known lower difficulty; use lowest reward only as conservative "
                                             "fallback for unmodeled enemies" } };
        callback(AsstMsg::SubTaskExtraInfo, review);
    }
    if (selected_bounty >= 0) {
        token = bounty_offers[selected_bounty].token;
    }
    if (token.empty()) {
        auto blocked = basic_info_with_what("SPAAutomationRewardBlocked");
        blocked["details"] = json::object { { "after_round", after_round },
                                            { "choice_kind", "bounty_selection" },
                                            { "reason", "No unique bounty description token was recognized" },
                                            { "run_victory_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, blocked);
        return false;
    }
    spa::ShopTaskPlugin select(m_callback, m_inst, TaskType);
    select.set_fast_mode(m_policy.basic_mode || m_policy.dynamic_decision_mode);
    select.set_retry_times(0);
    select.set_task_id(m_task_id);
    select.set_task_ptr(this);
    if (!select.configure_bounty(token, false, bounty_offers[selected_bounty].slot) || !select.run()) {
        return false;
    }
    spa::ShopTaskPlugin confirm(m_callback, m_inst, TaskType);
    confirm.set_fast_mode(m_policy.basic_mode || m_policy.dynamic_decision_mode);
    confirm.set_retry_times(0);
    confirm.set_task_id(m_task_id);
    confirm.set_task_ptr(this);
    if (!confirm.configure_bounty(token, true, bounty_offers[selected_bounty].slot) || !confirm.run()) {
        return false;
    }
    auto confirmed = basic_info_with_what("SPAAutomationRewardSelected");
    confirmed["details"] = json::object { { "after_round", after_round },
                                          { "choice_kind", "bounty_selection" },
                                          { "selection_token", token },
                                          { "returned_to_preparation", true },
                                          { "run_victory_verified", false } };
    callback(AsstMsg::SubTaskExtraInfo, confirmed);
    return true;
}

bool asst::StrongholdProtocolTask::handle_full_bench_sale(
    const json::value& state,
    int funding_deficit,
    const spa::BenchCapacityTrade* capacity_trade)
{
    const int remaining_deployment = state.get("remaining_deployment", -1);
    if (!m_policy.dynamic_decision_mode || m_decision_only || (m_policy.bench_occupied < 10 && funding_deficit <= 0) ||
        !m_unreconciled_sale.empty() || m_bench_recount_pending || !state.get("remaining_deployment_known", false) ||
        remaining_deployment < 0 || remaining_deployment > 8 || (remaining_deployment != 0 && funding_deficit <= 0) ||
        !state.get("funds_known", false)) {
        return false;
    }
    const int round = state.get("round", -1);
    if (std::count_if(m_failed_bench_sales.begin(), m_failed_bench_sales.end(), [&](const auto& attempt) {
            return attempt.first == round;
        }) >= 2) {
        return false;
    }
    auto candidates = m_policy;
    for (const auto& [failed_round, name] : m_failed_bench_sales) {
        if (failed_round == round) {
            candidates.owned.erase(name);
        }
    }
    // A committed swap preserves the physical bench population. Its two
    // identities remain reserved until verification, but unrelated bench
    // operators can still be identity-checked and sold. Blocking every sale
    // here stranded an entire round's funds while one swap awaited proof.
    std::vector<std::string> protected_names;
    if (m_pending_field_replacement) {
        protected_names = { m_pending_field_replacement->incoming, m_pending_field_replacement->outgoing };
    }
    if (m_pending_direct_promotion) {
        protected_names.push_back(m_pending_direct_promotion->get("operator", std::string()));
    }
    const auto selected =
        capacity_trade && std::find(protected_names.begin(), protected_names.end(), capacity_trade->spare_initial) ==
                              protected_names.end()
            ? std::optional(capacity_trade->spare_initial)
            : spa::choose_full_bench_sale(candidates, protected_names, funding_deficit);
    if (!selected) {
        return false;
    }
    std::optional<spa::OwnedOperator> remaining_copies;
    if (capacity_trade) {
        const auto found = candidates.owned.find(*selected);
        if (*selected != capacity_trade->spare_initial || found == candidates.owned.end()) {
            return false;
        }
        const auto forms = spa::owned_form_counts(found->second);
        if (!forms || forms->initial < 1) {
            return false;
        }
        if (found->second.deployed) {
            remaining_copies = found->second;
            if (forms->initial < 2 ||
                !spa::set_owned_form_counts(*remaining_copies, { forms->initial - 1, forms->elite })) {
                return false;
            }
        }
        else if (forms->initial != 1 || forms->elite != 0) {
            return false;
        }
    }
    m_failed_bench_sales.emplace(round, *selected);
    spa::SwapTaskPlugin sale(m_callback, m_inst, TaskType);
    sale.use_bench_identity_hints(m_inventory_operator_hints);
    sale.set_retry_times(0);
    sale.set_task_id(m_task_id);
    sale.set_task_ptr(this);
    json::object options { { "name", *selected },
                           { "sell_only", true },
                           { "find_bench_source", true },
                           { "fast_mode", true },
                           { "sale_credit", 0 },
                           { "minimum_sale_credit", std::max(1, funding_deficit) },
                           { "round", round },
                           { "funds", state.get("funds", -1) },
                           { "remaining_deployment", remaining_deployment } };
    if (capacity_trade) {
        options["verify_source_form"] = true;
        options["target_elite"] = false;
    }
    if (!sale.configure(options) || !sale.run()) {
        if (sale.sale_committed()) {
            // No second sale or new copy of this identity may obscure a
            // committed transaction before the roster has been reconstructed.
            m_unreconciled_sale = *selected;
            m_auto_deploy_blocked_names.insert(*selected);
        }
        auto deferred = basic_info_with_what("SPABenchSaleDeferred");
        deferred["details"] = json::object { { "round", round },
                                             { "name", *selected },
                                             { "protected_replacement_names", protected_names },
                                             { "input_committed", sale.sale_committed() },
                                             { "continue_current_run", true } };
        callback(AsstMsg::SubTaskExtraInfo, deferred);
        return true;
    }
    if (remaining_copies) {
        m_policy.owned.at(*selected) = *remaining_copies;
    }
    else {
        m_policy.owned.erase(*selected);
        m_policy.released_transitions.erase(*selected);
    }
    if (capacity_trade) {
        m_purchase_reserved_after_sale = std::pair(round, capacity_trade->purchase);
    }
    // Selling returns attached gear to the inventory. Preserve names as
    // discovery hints, not verified item counts: identical gear may merge.
    if (const auto slots = m_equipped_items.find(*selected); slots != m_equipped_items.end()) {
        m_equipment_candidates.insert(slots->second.begin(), slots->second.end());
        if (!slots->second.empty()) {
            m_equipment_inventory_dirty = true;
            m_equipment_inventory_scan_rounds.erase(round);
        }
    }
    m_equipped_items.erase(*selected);
    --m_policy.bench_occupied;
    m_shopping_complete = false;
    m_spending_reviewed = false;
    m_spending_reason.clear();
    auto receipt = basic_info_with_what("SPAAutomationSaleReconciled");
    receipt["details"] =
        json::object { { "run_id", m_run_id },
                       { "round", round },
                       { "name", *selected },
                       { "funding_deficit", funding_deficit },
                       { "spare_initial_form_verified", remaining_copies.has_value() },
                       { "reserved_shop_offer", capacity_trade ? capacity_trade->purchase.name : std::string() },
                       { "protected_replacement_names", protected_names },
                       { "credit", sale.sale_credit() },
                       { "funds", state.get("funds", 0) + sale.sale_credit() },
                       { "bench_occupied", m_policy.bench_occupied },
                       { "roster", roster_snapshot() },
                       { "equipment_slots", equipment_slots_snapshot() },
                       { "equipment_candidates", equipment_candidates_snapshot() },
                       { "inventory_complete", false },
                       { "used_deploy_points", deployment_point_snapshot() },
                       { "field_positions", field_position_snapshot() },
                       { "remaining_deployment", remaining_deployment },
                       { "run_victory_verified", false } };
    callback(AsstMsg::SubTaskExtraInfo, receipt);
    return true;
}

asst::spa::ReplacementMember asst::StrongholdProtocolTask::replacement_member(
    const std::string& name,
    const spa::OwnedOperator& owned,
    const spa::PreparationPolicy* context) const
{
    const auto& policy = context ? *context : m_policy;
    const auto pacts = spa::operator_pacts();
    const auto route = spa::route_profiles().find(policy.primary_pact);
    spa::ReplacementMember result;
    result.name = name;
    result.strategy_core = spa::is_strategy_core(policy.selected_strategy_id, name);
    if (const auto found = pacts.find(name); found != pacts.end()) {
        result.pacts = found->second;
    }
    result.role = spa::operator_placement_role(name);
    if (const auto props = BattleData.find_first_oper(battle::Role::Unknown, name, true)) {
        result.accepts_direct_healing = spa::accepts_direct_healing(props->sub_role);
        result.provides_regeneration = props->sub_role == battle::SubRole::Support_Bard;
        result.reliable_blocker = props->sub_role != battle::SubRole::Special_DollKeeper &&
                                  spa::default_skill_holds_position(name, owned.elite);
    }
    const auto fact = std::find_if(spa::OperatorCatalogue.begin(), spa::OperatorCatalogue.end(), [&](const auto& item) {
        return item.name == name;
    });
    result.value = (fact == spa::OperatorCatalogue.end() ? 0 : fact->tier * 120) + (owned.elite ? 350 : 0);
    result.egir_material_tier = fact == spa::OperatorCatalogue.end() ? 0 : fact->tier;
    if (policy.primary_locked_after_activation && spa::belongs_to_planned_lineup(name, policy, pacts)) {
        result.value += 1200;
    }
    if (spa::is_primary_growth_engine(name, policy.primary_pact)) {
        result.value += 900;
    }
    result.value += spa::primary_growth_support_value(name, policy, pacts);
    if (route != spa::route_profiles().end() &&
        std::find(route->second.output_candidates.begin(), route->second.output_candidates.end(), name) !=
            route->second.output_candidates.end()) {
        result.value += 1200;
    }
    return result;
}

int asst::StrongholdProtocolTask::replacement_lineup_context_value(
    const std::vector<spa::ReplacementMember>& lineup) const
{
    auto context = m_policy;
    context.support_pacts = spa::planning_support_pacts(context, {}, spa::operator_pacts());
    for (const auto& unit : lineup) {
        if (!context.owned.contains(unit.name)) {
            // A shop offer is an initial body in this counterfactual only.
            // It must participate in pact/growth scoring before purchase.
            context.owned.emplace(unit.name, spa::OwnedOperator { 1, false, true });
        }
    }
    for (auto& [name, owned] : context.owned) {
        owned.deployed = std::any_of(lineup.begin(), lineup.end(), [&](const auto& unit) { return unit.name == name; });
    }
    auto scored = lineup;
    for (auto& unit : scored) {
        const auto owned = context.owned.find(unit.name);
        if (owned == context.owned.end()) {
            return 0;
        }
        unit.value = replacement_member(unit.name, owned->second, &context).value;
    }
    int value = spa::replacement_lineup_value(scored, context.primary_pact) +
                spa::replacement_support_pact_value(scored, context.support_pacts, context.disabled_pacts);
    if (context.primary_pact == "乌萨斯" && context.round < m_expected_boss_round &&
        spa::replacement_primary_members(scored, context.primary_pact) >= 3) {
        const int population = static_cast<int>(std::count_if(scored.begin(), scored.end(), [&](const auto& unit) {
            return std::find(unit.pacts.begin(), unit.pacts.end(), context.primary_pact) != unit.pacts.end();
        }));
        std::vector<spa::GrowthSource> sources;
        sources.reserve(scored.size());
        for (const auto& unit : scored) {
            const auto forms = spa::owned_form_counts(context.owned.at(unit.name));
            sources.push_back({ unit.name, forms && forms->initial == 0 && forms->elite > 0, true, population });
        }
        const auto cycle = spa::assess_ursus_layer_cycle(sources);
        // Preserve a viable injection/conversion pair when its immediate
        // combat roles and lane coverage still permit this replacement.
        value += cycle.converted_from_opening * 100;
    }
    return value;
}

std::pair<std::vector<asst::spa::ReplacementMember>, std::vector<asst::spa::ReplacementMember>>
    asst::StrongholdProtocolTask::replacement_inputs() const
{
    std::vector<spa::ReplacementMember> field, bench;
    for (const auto& [name, owned] : m_policy.owned) {
        if (owned.copies <= 0) {
            continue;
        }
        const auto count = m_auto_deploy_positions.count(name);
        if (owned.deployed) {
            if (count == 0) {
                // A promoted unit may still be deployed but its surviving
                // position is unknown. Protect it and retain its pact/role.
                field.push_back(replacement_member(name, owned));
            }
            else {
                const auto [begin, end] = m_auto_deploy_positions.equal_range(name);
                for (auto item = begin; item != end; ++item) {
                    auto placed = replacement_member(name, owned);
                    placed.x = item->second.first;
                    placed.y = item->second.second;
                    // All owned copies are accounted for on the field, so
                    // the outgoing name cannot already be present on bench.
                    placed.replaceable = spa::replacement_outgoing_identity_trackable(
                                             owned.elite,
                                             owned.copies,
                                             static_cast<int>(count)) &&
                                         m_verified_field_pose_points.contains(item->second) &&
                                         (!m_pending_direct_promotion ||
                                          m_pending_direct_promotion->get("operator", std::string()) != name);
                    field.push_back(std::move(placed));
                }
            }
        }
        if ((!owned.deployed || (!owned.elite && owned.copies > static_cast<int>(count) && count > 0)) &&
            spa::owned_form_counts(owned)) {
            bench.push_back(replacement_member(name, owned));
        }
    }
    const auto terrain = spa::terrain_layout_model(m_spatial_map_id, m_spatial_boss_phase);
    if (terrain && m_growth_population_poses == m_auto_deploy_positions &&
        m_population_grid_members.size() == field.size() && !m_spatial_grid_projection.empty() && !field.empty() &&
        std::all_of(field.begin(), field.end(), [&](const auto& unit) {
            return m_verified_field_pose_points.contains({ unit.x, unit.y });
        })) {
        std::vector<spa::PlacementMember> placed;
        for (const auto& unit : field) {
            placed.push_back({ unit.x, unit.y, unit.role, unit.name });
        }
        // Stored positions and projection share the certified collapsed
        // frame. The current shop may be expanded, so mixing a fresh shop
        // camera with these collapsed coordinates would invent route cells.
        const auto grid = spa::registered_grid_members(m_spatial_grid_projection, placed);
        if (grid) {
            spa::register_replacement_routes(field, *grid, spa::active_preparation_lanes(*terrain, m_policy.round));
        }
    }
    return { std::move(field), std::move(bench) };
}

bool asst::StrongholdProtocolTask::handle_full_field_replacement(const json::value& state)
{
    if (m_pending_spatial_orientation || !m_policy.dynamic_decision_mode || m_policy.primary_pact.empty() ||
        m_policy.primary_pact == "auto" || m_policy.bench_occupied <= 0 ||
        !state.get("remaining_deployment_known", false) || state.get("remaining_deployment", -1) != 0) {
        return false;
    }
    const int round = state.get("round", -1);
    if (m_pending_field_replacement) {
        auto& pending = *m_pending_field_replacement;
        // A pending transaction must not synthesize/buy either identity
        // before its delayed bench proof, otherwise that proof is ambiguous.
        m_blocked_purchases.emplace(round, pending.incoming);
        m_blocked_purchases.emplace(round, pending.outgoing);
        if (pending.last_probe_round == round) {
            return false;
        }
        pending.last_probe_round = round;
        const auto incoming = m_policy.owned.find(pending.incoming);
        const auto outgoing = m_policy.owned.find(pending.outgoing);
        if (incoming == m_policy.owned.end() || outgoing == m_policy.owned.end() ||
            incoming->second.copies != pending.incoming_copies || outgoing->second.copies != pending.outgoing_copies ||
            incoming->second.elite != pending.incoming_elite || outgoing->second.elite != pending.outgoing_elite) {
            return false;
        }
        if (pending.needs_pose_probe) {
            pending.verifier = std::make_shared<spa::SwapTaskPlugin>(m_callback, m_inst, TaskType);
            pending.verifier->set_task_id(m_task_id);
            pending.verifier->set_task_ptr(this);
            pending.verifier->set_retry_times(0);
            if (!pending.verifier->configure(
                    json::object { { "name", pending.incoming },
                                   { "replaced", pending.outgoing },
                                   { "deploy_only", true },
                                   { "replace_only", true },
                                   { "replacement_reconcile_only", true },
                                   { "find_bench_source", true },
                                   { "outgoing_all_copies_deployed", true },
                                   { "target_x", pending.previous.x },
                                   { "target_y", pending.previous.y },
                                   { "drag_target_x", pending.previous.x },
                                   { "drag_target_y", pending.previous.y },
                                   { "fast_mode", true },
                                   { "best_effort_deployment", true },
                                   { "round", round },
                                   { "funds", state.get("funds", -1) },
                                   { "remaining_deployment", 0 } })) {
                return false;
            }
        }
        else if (!pending.verifier || !pending.verifier->set_reconciliation_context(round, state.get("funds", -1))) {
            return false;
        }
        auto probe = basic_info_with_what("SPAReplacementVerificationResumed");
        probe["details"] = json::object { { "round", round },
                                          { "incoming", pending.incoming },
                                          { "outgoing", pending.outgoing },
                                          { "drag_repeated", false } };
        callback(AsstMsg::SubTaskExtraInfo, probe);
        if (!(pending.needs_pose_probe ? pending.verifier->run() : pending.verifier->reconcile_replacement())) {
            return false;
        }
        if (pending.verifier->replacement_original_preserved()) {
            auto unchanged = basic_info_with_what("SPAReplacementOriginalPreserved");
            unchanged["details"] = json::object { { "run_id", m_run_id },
                                                  { "round", round },
                                                  { "incoming", pending.incoming },
                                                  { "outgoing", pending.outgoing },
                                                  { "roster", roster_snapshot() },
                                                  { "field_positions", field_position_snapshot() },
                                                  { "incoming_bench_identity_verified", true },
                                                  { "outgoing_field_identity_verified", true },
                                                  { "drag_repeated", false },
                                                  { "pending_replacement", nullptr } };
            m_pending_field_replacement.reset();
            callback(AsstMsg::SubTaskExtraInfo, unchanged);
            return true;
        }
        commit_full_field_replacement(
            pending.incoming,
            pending.outgoing,
            pending.previous,
            pending.verifier->field_pose_verified() ? pending.verifier->target_point()
                                                    : pending.verifier->drag_target_point(),
            round,
            pending.committed_facing ? pending.committed_facing : pending.verifier->committed_facing());
        m_pending_field_replacement.reset();
        return true;
    }
    if (m_field_checkpoint_loaded || m_policy.bench_occupied >= 10) {
        // A full hand cannot receive the outgoing actor. Resolve a pending
        // committed exchange above, but free a bench slot before a new one.
        // The ordinary full-bench sale step follows this function.
        return false;
    }
    // A failed exchange can require source/panel recovery. Bound that work
    // within a preparation phase, rather than probing every bench/field pair.
    if (m_failed_replacements.size() >= 3) {
        return false;
    }
    auto [field, bench] = replacement_inputs();
    std::erase_if(bench, [&](const auto& member) { return m_unavailable_replacement_sources.contains(member.name); });
    if (field.size() != 8) {
        LogInfo << "SPA replacement deferred for incomplete field model" << round << field.size() << bench.size();
        return false;
    }
    // Repositioning is handled by the layout executor. A replacement must
    // advance the lineup; changing camera coordinates must not permit a cycle.
    m_visited_replacement_lineups.insert(spa::replacement_lineup_identity(field));
    const auto failure_key = [&](const spa::ReplacementChoice& candidate) -> std::string {
        if (m_spatial_map_id.empty() || m_growth_population_poses != m_auto_deploy_positions ||
            m_spatial_grid_projection.empty()) {
            return {};
        }
        std::vector<spa::PlacementMember> placed;
        std::optional<std::size_t> target_index;
        std::map<std::string, bool> forms;
        for (const auto& member : field) {
            if (member.name == candidate.outgoing && member.x == candidate.x && member.y == candidate.y) {
                target_index = placed.size();
            }
            placed.push_back({ member.x, member.y, member.role, member.name });
            forms[member.name] = m_policy.owned.at(member.name).elite;
        }
        const auto grid = spa::registered_grid_members(m_spatial_grid_projection, placed);
        if (!target_index || !grid) {
            return {};
        }
        const auto& actor = m_policy.owned.at(candidate.incoming);
        const auto& target = grid->at(*target_index);
        return spa::replacement_target_failure_key(
                   m_spatial_map_id + (m_spatial_boss_phase ? "#boss" : "#normal"),
                   candidate.incoming,
                   actor.elite,
                   actor.copies,
                   { target.col, target.row },
                   *grid,
                   forms)
            .value_or("");
    };
    auto blocked = m_failed_replacements;
    const auto evaluate = [this](const auto& lineup) {
        return replacement_lineup_context_value(lineup);
    };
    auto choice = spa::choose_replacement(
        m_policy.primary_pact,
        field,
        bench,
        blocked,
        evaluate,
        m_visited_replacement_lineups,
        m_policy.disabled_pacts);
    std::string target_failure_key;
    while (choice.found) {
        target_failure_key = failure_key(choice);
        if (target_failure_key.empty() || !m_failed_spatial_actions.contains(target_failure_key)) {
            break;
        }
        // No input: the same source form and local target geometry already
        // failed its held preview. A new round alone is not new evidence.
        blocked.emplace(choice.incoming, choice.outgoing, choice.x, choice.y);
        choice = spa::choose_replacement(
            m_policy.primary_pact,
            field,
            bench,
            blocked,
            evaluate,
            m_visited_replacement_lineups,
            m_policy.disabled_pacts);
    }
    if (!choice.found) {
        return false;
    }
    m_failed_replacements.emplace(choice.incoming, choice.outgoing, choice.x, choice.y);
    auto decision = basic_info_with_what("SPAReplacementDecision");
    decision["details"] = json::object {
        { "round", round },
        { "incoming", choice.incoming },
        { "outgoing", choice.outgoing },
        { "improvement", choice.improvement },
        { "value_context", "each_prospective_full_lineup" },
        { "restores_strategy_core", choice.restores_strategy_core },
        { "held_target_x", choice.x },
        { "held_target_y", choice.y },
        { "primary_pact", m_policy.primary_pact },
        { "route_membership_registered",
          std::all_of(field.begin(), field.end(), [](const auto& unit) { return unit.route_membership_registered; }) },
        { "enemy_paths_observed", false },
        { "outgoing_committed_facing_known", m_committed_facings.contains({ choice.x, choice.y }) },
        { "executed", false }
    };
    callback(AsstMsg::SubTaskExtraInfo, decision);
    const char inherited_direction = m_committed_facings.contains({ choice.x, choice.y })
                                         ? m_committed_facings.at({ choice.x, choice.y })
                                         : m_auto_deploy_direction;
    char incoming_direction = inherited_direction;
    bool growth_geometry_known = false;
    json::array replacement_growth_bonuses;
    if (m_growth_population_poses == m_auto_deploy_positions && m_population_grid_members.size() == 8 &&
        std::all_of(m_auto_deploy_positions.begin(), m_auto_deploy_positions.end(), [&](const auto& placed) {
            return m_auto_deploy_positions.count(placed.first) == 1 &&
                   m_verified_field_pose_points.contains(placed.second);
        })) {
        std::vector<spa::GridPlacementMember> others;
        std::vector<spa::RosterUnit> prospective;
        std::optional<spa::GridPlacementCell> target;
        for (auto member : m_population_grid_members) {
            if (member.name == choice.outgoing) {
                target = spa::GridPlacementCell { member.col, member.row, choice.x, choice.y };
                prospective.push_back({ choice.incoming, true, {}, m_policy.owned.at(choice.incoming).elite });
                continue;
            }
            const auto placed = m_auto_deploy_positions.find(member.name);
            if (placed == m_auto_deploy_positions.end()) {
                others.clear();
                break;
            }
            const auto facing = m_committed_facings.find(placed->second);
            member.facing = facing == m_committed_facings.end() ? '\0' : facing->second;
            prospective.push_back({ member.name, true, {}, m_policy.owned.at(member.name).elite });
            others.push_back(std::move(member));
        }
        if (target && others.size() == 7) {
            const auto formation = spa::evaluate_formation(
                prospective,
                spa::operator_pacts(),
                spa::pact_requirements(),
                spa::enabled_formation_pacts(m_enabled_core_pacts, m_policy.disabled_pacts));
            if (formation.complete) {
                incoming_direction = spa::replacement_growth_facing(
                    choice.incoming,
                    *target,
                    others,
                    formation.active_pacts,
                    m_policy.primary_pact,
                    inherited_direction);
                std::map<char, int> bonuses;
                for (const auto& preference : spa::spatial_growth_preferences(
                         choice.incoming,
                         { *target },
                         others,
                         formation.active_pacts,
                         m_policy.primary_pact)) {
                    bonuses[preference.facing] += preference.bonus;
                }
                const bool directional =
                    bonuses.size() != 4 || std::any_of(bonuses.begin(), bonuses.end(), [&](const auto& value) {
                        return value.second != bonuses.begin()->second;
                    });
                if (directional) {
                    for (const auto& [direction, bonus] : bonuses) {
                        replacement_growth_bonuses.emplace_back(
                            json::object { { "direction", std::string(1, direction) }, { "bonus", bonus } });
                    }
                }
                growth_geometry_known = true;
            }
        }
    }
    auto facing_plan = basic_info_with_what("SPAReplacementGrowthFacing");
    facing_plan["details"] = json::object { { "incoming", choice.incoming },
                                            { "round", round },
                                            { "inherited_direction", std::string(1, inherited_direction) },
                                            { "preferred_growth_direction", std::string(1, incoming_direction) },
                                            { "registered_formation_known", growth_geometry_known },
                                            { "direction_committed", false } };
    callback(AsstMsg::SubTaskExtraInfo, facing_plan);
    json::object options { { "name", choice.incoming },
                           { "replaced", choice.outgoing },
                           { "target_elite", m_policy.owned.at(choice.incoming).elite },
                           { "deploy_only", true },
                           { "replace_only", true },
                           { "find_bench_source", true },
                           { "recent_acquisition_source", m_recent_bench_acquisition == choice.incoming },
                           { "outgoing_all_copies_deployed", true },
                           { "fast_mode", true },
                           { "best_effort_deployment", true },
                           { "role_aware_deployment", true },
                           { "target_x", choice.x },
                           { "target_y", choice.y },
                           { "drag_target_x", choice.x },
                           { "drag_target_y", choice.y },
                           { "direction", std::string(1, incoming_direction) },
                           { "replacement_growth_bonuses", std::move(replacement_growth_bonuses) },
                           { "round", round },
                           { "funds", state.get("funds", -1) },
                           { "remaining_deployment", 0 },
                           { "excluded_deploy_points", deployment_point_snapshot() } };
    json::array placed;
    for (const auto& unit : field) {
        if (unit.x >= 0 && (unit.x != choice.x || unit.y != choice.y)) {
            placed.emplace_back(
                json::object { { "name", unit.name },
                               { "x", unit.x },
                               { "y", unit.y },
                               { "field_pose_verified", m_verified_field_pose_points.contains({ unit.x, unit.y }) } });
        }
    }
    options["placement_units"] = std::move(placed);
    const auto incoming_forms = spa::owned_form_counts(m_policy.owned.at(choice.incoming));
    options["verify_source_form"] = incoming_forms && incoming_forms->initial > 0 && incoming_forms->elite > 0;
    auto exchange = std::make_shared<spa::SwapTaskPlugin>(m_callback, m_inst, TaskType);
    exchange->use_bench_identity_hints(m_inventory_operator_hints);
    exchange->set_retry_times(0);
    exchange->set_task_id(m_task_id);
    exchange->set_task_ptr(this);
    const bool configured = exchange->configure(options);
    if (configured && !target_failure_key.empty() && m_auto_deploy_positions.count(choice.outgoing) == 1 &&
        std::any_of(m_population_grid_members.begin(), m_population_grid_members.end(), [&](const auto& member) {
            return member.name == choice.outgoing && spa::terrain_has_blocking_device_below(
                                                         m_spatial_map_id,
                                                         { member.col, member.row },
                                                         m_spatial_boss_phase);
        })) {
        exchange->set_registered_grid_context(m_spatial_grid_reference, m_spatial_grid_projection, m_spatial_map_id);
    }
    if (!configured || !exchange->run()) {
        const bool drop_issued = exchange->replacement_committed();
        if (!drop_issued && exchange->replacement_target_unavailable() && !target_failure_key.empty()) {
            m_failed_spatial_actions.insert(target_failure_key);
        }
        if (!drop_issued && exchange->bench_source_search_exhausted()) {
            // Changing the outgoing target cannot reveal a source that the
            // same visible bench scan just failed to identify. Try other
            // sources now; only a fresh copy of this unit or a new round
            // warrants repeating its unrecognized bench scan.
            m_unavailable_replacement_sources.insert(choice.incoming);
        }
        if (drop_issued) {
            const auto& incoming = m_policy.owned.at(choice.incoming);
            const auto& outgoing = m_policy.owned.at(choice.outgoing);
            m_pending_field_replacement =
                PendingReplacement { exchange,        choice.incoming, choice.outgoing, Point(choice.x, choice.y),
                                     incoming.copies, outgoing.copies, incoming.elite,  outgoing.elite };
            m_pending_field_replacement->committed_facing = exchange->committed_facing();
            // A drop can be issued before the normal verifier has gathered
            // its postconditions. Use the same observation-only route as
            // a persisted exchange, starting with a fresh field identity.
            m_pending_field_replacement->needs_pose_probe = !exchange->replacement_can_reconcile();
        }
        auto recovery = basic_info_with_what("SPAReplacementDeferred");
        recovery["details"] =
            json::object { { "round", round },
                           { "incoming", choice.incoming },
                           { "outgoing", choice.outgoing },
                           { "source_probe_exhausted", exchange->bench_source_search_exhausted() },
                           { "committed_but_unreconciled", drop_issued },
                           { "verification_only_retry_available", m_pending_field_replacement.has_value() },
                           { "run_id", m_run_id },
                           { "roster", roster_snapshot() },
                           { "bench_occupied", m_policy.bench_occupied },
                           { "used_deploy_points", deployment_point_snapshot() },
                           { "field_positions", field_position_snapshot() },
                           { "pending_replacement", pending_replacement_snapshot() },
                           { "failed_spatial_actions", failed_spatial_action_snapshot() },
                           { "target_preview_unavailable", exchange->replacement_target_unavailable() },
                           { "continue_current_run", true } };
        callback(AsstMsg::SubTaskExtraInfo, recovery);
        return true;
    }
    commit_full_field_replacement(
        choice.incoming,
        choice.outgoing,
        Point(choice.x, choice.y),
        exchange->field_pose_verified() ? exchange->target_point() : exchange->drag_target_point(),
        round,
        exchange->committed_facing());
    return true;
}

void asst::StrongholdProtocolTask::commit_full_field_replacement(
    const std::string& incoming,
    const std::string& outgoing,
    const Point& previous,
    const Point& actual,
    int round,
    std::optional<char> committed_facing)
{
    auto grid_members = m_population_grid_members;
    const bool grid_was_known = m_growth_population_poses == m_auto_deploy_positions && grid_members.size() == 8 &&
                                m_auto_deploy_positions.count(outgoing) == 1;
    m_failed_replacements.erase({ incoming, outgoing, previous.x, previous.y });
    std::erase_if(m_auto_deploy_positions, [&](const auto& unit) {
        return std::abs(unit.second.first - previous.x) < 40 && std::abs(unit.second.second - previous.y) < 40;
    });
    // A replacement preserves occupancy. The held-input exclusion ledger
    // must not receive coordinates measured in the collapsed-map frame.
    m_auto_deploy_positions.emplace(incoming, std::pair { actual.x, actual.y });
    std::erase_if(m_verified_field_pose_points, [&](const auto& point) {
        return std::abs(point.first - previous.x) < 40 && std::abs(point.second - previous.y) < 40;
    });
    m_verified_field_pose_points.emplace(actual.x, actual.y);
    std::erase_if(m_committed_facings, [&](const auto& entry) {
        return std::abs(entry.first.first - previous.x) < 40 && std::abs(entry.first.second - previous.y) < 40;
    });
    if (committed_facing) {
        m_committed_facings[{ actual.x, actual.y }] = *committed_facing;
    }
    m_policy.owned.at(incoming).deployed = true;
    m_policy.owned.at(outgoing).deployed = m_auto_deploy_positions.contains(outgoing);
    m_policy.released_transitions.erase(incoming);
    if (!m_policy.owned.at(outgoing).deployed) {
        m_policy.released_transitions.insert(outgoing);
    }
    m_growth_population_poses.clear();
    m_row_growth_populations.clear();
    m_population_grid_members.clear();
    if (grid_was_known) {
        int changed = 0;
        for (auto& member : grid_members) {
            if (member.name == outgoing) {
                member.name = incoming;
                member.role = spa::operator_placement_role(incoming);
                member.facing = committed_facing.value_or('\0');
                ++changed;
            }
        }
        if (const auto populations = spa::same_row_growth_populations(grid_members);
            changed == 1 && populations && m_auto_deploy_positions.size() == 8 &&
            std::all_of(m_auto_deploy_positions.begin(), m_auto_deploy_positions.end(), [&](const auto& item) {
                return m_verified_field_pose_points.contains(item.second);
            })) {
            m_population_grid_members = std::move(grid_members);
            m_row_growth_populations = *populations;
            m_growth_population_poses = m_auto_deploy_positions;
        }
    }
    m_last_remaining_deployment = 0;
    m_shopping_complete = false;
    m_spending_reviewed = false;
    m_spending_reason.clear();
    auto receipt = basic_info_with_what("SPAReplacementReconciled");
    receipt["details"] =
        json::object { { "run_id", m_run_id },
                       { "round", round },
                       { "incoming", incoming },
                       { "outgoing", outgoing },
                       { "outgoing_available_for_sale_review", m_policy.released_transitions.contains(outgoing) },
                       { "bench_occupied", m_policy.bench_occupied },
                       { "remaining_deployment", 0 },
                       { "roster", roster_snapshot() },
                       { "used_deploy_points", deployment_point_snapshot() },
                       { "field_positions", field_position_snapshot() },
                       { "pending_replacement", nullptr },
                       { "run_victory_verified", false } };
    callback(AsstMsg::SubTaskExtraInfo, receipt);
}

bool asst::StrongholdProtocolTask::handle_promotion_page()
{
    const bool source_known = !m_pending_promotion.empty();
    const bool pact_recruitment = !m_pending_pact_recruitment.empty();
    const auto image = ctrler()->get_image();
    if (image.empty() || image.cols != 1280 || image.rows != 720) {
        return false;
    }
    if (m_pending_promotion_reward && (m_pending_promotion_reward->source != m_pending_promotion ||
                                       m_pending_promotion_reward->recruitment_pact != m_pending_pact_recruitment ||
                                       m_pending_promotion_reward->round != m_policy.round)) {
        m_pending_promotion_reward.reset();
    }
    const auto catalog = spa::operator_pacts();
    if (!m_pending_promotion_reward) {
        OCRer ocr(image, Rect(630, 495, 480, 214));
        const auto recognized = ocr.analyze();
        if (!recognized) {
            return false;
        }
        std::set<int> free_price_slots;
        for (int slot = 0; slot < 3; ++slot) {
            if (spa::shop_number(image, Rect(1038 - slot * 162, 510, 24, 30)) == 0) {
                free_price_slots.insert(slot);
            }
        }
        // Numeric OCR on the pale free badges is less stable than normal gold
        // prices. Count an explicit zero token in each card's price band as a
        // fallback, while keeping the x bands disjoint.
        for (const auto& text : *recognized) {
            if (text.score < 0.5 || text.text != "0" || text.rect.y < 500 || text.rect.y >= 545) {
                continue;
            }
            const bool in_left = text.rect.x >= 680 && text.rect.x < 760;
            const bool in_middle = text.rect.x >= 840 && text.rect.x < 920;
            const bool in_right = text.rect.x >= 1000 && text.rect.x < 1080;
            if (in_left) {
                free_price_slots.insert(2);
            }
            if (in_middle) {
                free_price_slots.insert(1);
            }
            if (in_right) {
                free_price_slots.insert(0);
            }
        }
        // The synthesis page is a three-card free shop. Ordinary preparation
        // cards can contain the same operator names, so require at least two
        // independently read zero-price badges before treating the page as a
        // promotion transition.
        if (free_price_slots.size() < (pact_recruitment ? 1U : 2U)) {
            return false;
        }

        struct Candidate
        {
            std::string name;
            Rect text;
        };

        std::vector<Candidate> candidates;
        for (const auto& text : *recognized) {
            if (text.score < 0.85 || text.rect.y < 625 || text.rect.y > 700 || text.rect.x < 400 ||
                text.rect.x >= 1120 || text.text == m_pending_promotion || !catalog.contains(text.text)) {
                continue;
            }
            const int slot = static_cast<int>(std::lround((995 - text.rect.x - text.rect.width / 2) / 162.0));
            if (!free_price_slots.contains(slot) ||
                (pact_recruitment &&
                 !spa::pact_recruitment_offer_eligible(text.text, m_pending_pact_recruitment, m_policy.shop_level))) {
                continue;
            }
            if (std::none_of(candidates.begin(), candidates.end(), [&](const Candidate& item) {
                    return item.name == text.text;
                })) {
                candidates.push_back({ text.text, text.rect });
            }
        }
        if (candidates.empty()) {
            return false;
        }

        // Phase one keeps its deterministic choice. Dynamic mode compares only
        // the actually observed free cards, without buying or refreshing first.
        std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            return a.text.x < b.text.x;
        });
        std::vector<std::string> reward_names;
        for (const auto& candidate : candidates) {
            reward_names.push_back(candidate.name);
        }
        const auto ranking = spa::rank_promotion_rewards(m_policy, reward_names, catalog);
        if (ranking.empty()) {
            return false;
        }
        const auto chosen = std::find_if(candidates.begin(), candidates.end(), [&](const auto& candidate) {
            return candidate.name == ranking.front().name;
        });
        if (chosen == candidates.end()) {
            return false;
        }
        const auto selected = *chosen;
        auto reviewed_policy = m_policy;
        if (!spa::reconcile_promotion_reward(reviewed_policy, m_pending_promotion, selected.name)) {
            LogWarn << "SPA promotion deferred for unknown operator form counts" << m_pending_promotion
                    << selected.name;
            return false;
        }
        json::array reward_ranking;
        for (const auto& candidate : ranking) {
            reward_ranking.emplace_back(json::object { { "name", candidate.name }, { "score", candidate.score } });
        }
        const int center_x = selected.text.x + selected.text.width / 2;
        const int selected_slot = std::clamp(static_cast<int>(std::lround((995 - center_x) / 162.0)), 0, 5);
        auto issued = basic_info_with_what("SPAPromotionSelectionIssued");
        issued["details"] = json::object { { "source", m_pending_promotion },
                                           { "pact_recruitment", pact_recruitment },
                                           { "recruitment_pact", m_pending_pact_recruitment },
                                           { "source_known", source_known },
                                           { "name", selected.name },
                                           { "choice_count", static_cast<int>(candidates.size()) },
                                           { "free_selection", true },
                                           { "run_victory_verified", false },
                                           { "dynamic_decision_mode", m_policy.dynamic_decision_mode },
                                           { "ranking", std::move(reward_ranking) } };
        callback(AsstMsg::SubTaskExtraInfo, issued);
        // A random point in the wide card rectangle can hit its left boundary
        // instead of selecting it. Use the observed name's column center.
        m_pending_promotion_reward = PendingPromotionReward { m_pending_promotion, m_pending_pact_recruitment,
                                                              selected.name,       std::move(reward_names),
                                                              m_policy.round,      selected_slot };
        if (!ctrler()->click(Point(center_x, 600)) || !sleep(300)) {
            return false;
        }
    }
    auto& pending = *m_pending_promotion_reward;
    const auto& selected_name = pending.name;
    const int selected_slot = pending.slot;
    const bool chain_expected = spa::promotion_reward_starts_chain(m_policy, selected_name);
    auto policy_after_reward = m_policy;
    if (!spa::reconcile_promotion_reward(policy_after_reward, m_pending_promotion, selected_name)) {
        LogWarn << "SPA pending promotion has unknown operator form counts" << m_pending_promotion << selected_name;
        return false;
    }
    bool restored = false;
    bool chain_observed = false;
    auto& confirmation_clicked = pending.confirmation_issued;
    auto& short_title_confirmed = pending.short_title_confirmed;
    std::optional<spa::TextObservation> previous_short_confirmation;
    for (int attempt = 0; attempt < 4 && !need_exit(); ++attempt) {
        const auto after_image = ctrler()->get_image();
        OCRer::ResultsVec after_recognized;
        const auto read = [&](const Rect& roi) {
            OCRer ocr(after_image, roi);
            if (const auto detected = ocr.analyze()) {
                after_recognized.insert(after_recognized.end(), detected->begin(), detected->end());
            }
        };
        if (confirmation_clicked) {
            read(Rect(0, 0, 1280, 720));
        }
        else {
            read(Rect(0, 170, 230, 155));
            read(Rect(620, 620, 485, 88));
        }
        std::vector<spa::TextObservation> texts;
        for (const auto& text : after_recognized) {
            texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
        }
        std::set<int> remaining_free_slots;
        if (confirmation_clicked) {
            for (int slot = 0; slot < 3; ++slot) {
                if (spa::shop_number(after_image, Rect(1038 - slot * 162, 510, 24, 30)) == 0) {
                    remaining_free_slots.insert(slot);
                }
            }
        }
        bool promotion_confirmation = false;
        auto confirm = spa::promotion_confirmation_control(texts, selected_name, selected_slot);
        if (!confirmation_clicked && !confirm && source_known && !pact_recruitment) {
            // The same one-character title and button must survive two fresh
            // captures, with the free shop and selected slot still visible.
            const auto short_title = spa::promotion_short_title_confirmation(texts, selected_name, selected_slot);
            if (short_title && previous_short_confirmation &&
                std::abs(short_title->x - previous_short_confirmation->x) <= 8 &&
                std::abs(short_title->y - previous_short_confirmation->y) <= 8) {
                confirm = short_title;
                short_title_confirmed = true;
            }
            previous_short_confirmation = short_title;
        }
        if (!confirmation_clicked && confirm) {
            for (const auto& text : after_recognized) {
                if (text.rect.x == confirm->x && text.rect.y == confirm->y && text.text == confirm->text) {
                    promotion_confirmation = true;
                    confirmation_clicked = true;
                    if (!ctrler()->click(text.rect) || !sleep(350)) {
                        return false;
                    }
                    break;
                }
            }
        }
        if (promotion_confirmation) {
            continue;
        }
        if (!confirmation_clicked) {
            if (attempt == 1 && !spa::purchase_confirmation_control(texts, selected_slot)) {
                // A missed first tap can be retried only on the exact same
                // still-visible reservation. Never rank the other cards.
                for (const auto& text : after_recognized) {
                    const int center = text.rect.x + text.rect.width / 2;
                    if (text.text == selected_name && text.score >= .90 && text.rect.y >= 625 &&
                        static_cast<int>(std::lround((995 - center) / 162.0)) == selected_slot &&
                        spa::shop_number(after_image, Rect(1038 - selected_slot * 162, 510, 24, 30)) == 0) {
                        if (!ctrler()->click(Point(center, 600))) {
                            return false;
                        }
                        break;
                    }
                }
            }
            if (!sleep(150)) {
                return false;
            }
            continue;
        }
        const auto page = spa::observe_page(texts, {});
        std::vector<std::string> next_reward_names;
        {
            for (const auto& text : after_recognized) {
                if (text.score >= .85 && text.rect.y >= 625 && text.rect.y <= 700 && text.rect.x >= 400 &&
                    text.rect.x < 1120 && catalog.contains(text.text)) {
                    next_reward_names.push_back(text.text);
                }
            }
        }
        chain_observed = spa::promotion_reward_chain_observed(
            chain_expected,
            confirmation_clicked,
            static_cast<int>(remaining_free_slots.size()),
            pending.offers,
            next_reward_names);
        if (chain_observed) {
            restored = true;
            break;
        }
        // The preparation HUD is also visible while a free card is merely
        // highlighted. Require the temporary shop to disappear after confirmation.
        if (page.page == spa::Page::Preparation && !spa::purchase_confirmation_control(texts, selected_slot) &&
            remaining_free_slots.size() < (pact_recruitment ? 1U : 2U)) {
            restored = true;
            break;
        }
        // Never promote a blind card tap to a confirmation receipt. In the
        // failed R4 run that tap merely opened the panel, then suppressed the
        // real visible purchase control on every subsequent frame.
        // Confirmation is issued once for this selected card. An old label
        // during animation, or an unrelated confirmation on an unknown page,
        // must not trigger another click. Observe the shop transition only.
        if (!sleep(400)) {
            return false;
        }
    }
    if (!restored) {
        return false;
    }

    const bool reward_already_owned =
        m_policy.owned.contains(selected_name) && m_policy.owned.at(selected_name).copies > 0;
    m_policy = std::move(policy_after_reward);
    if (source_known) {
        const auto source_forms = spa::owned_form_counts(m_policy.owned.at(m_pending_promotion));
        if (source_forms && source_forms->initial == 0 && source_forms->elite == 1) {
            if (const auto slots = m_equipped_items.find(m_pending_promotion); slots != m_equipped_items.end()) {
                m_equipment_candidates.insert(slots->second.begin(), slots->second.end());
                if (!slots->second.empty()) {
                    m_equipment_inventory_dirty = true;
                    m_equipment_inventory_scan_rounds.erase(m_policy.round);
                }
            }
            m_equipped_items[m_pending_promotion].clear();
        }
        else {
            // Another same-name body survives. Name-level equipment metadata
            // cannot tell which items returned and which remain equipped.
            m_equipped_items.erase(m_pending_promotion);
            m_equipment_inventory_dirty = true;
            m_equipment_inventory_scan_rounds.erase(m_policy.round);
        }
        // Do not retain the consumed copies as phantom field positions.
        // The surviving elite remains protected until its pose is observed.
        const auto [begin, end] = m_auto_deploy_positions.equal_range(m_pending_promotion);
        std::vector<Point> prior_verified_positions;
        for (auto position = begin; position != end; ++position) {
            if (m_verified_field_pose_points.contains(position->second)) {
                prior_verified_positions.emplace_back(position->second.first, position->second.second);
            }
            m_verified_field_pose_points.erase(position->second);
            m_committed_facings.erase(position->second);
        }
        m_auto_deploy_positions.erase(m_pending_promotion);
        if (!chain_observed && m_policy.owned.at(m_pending_promotion).deployed) {
            std::vector<std::pair<int, int>> prior_points;
            std::vector<std::pair<int, int>> confirmed_positions;
            spa::SwapTaskPlugin probe(m_callback, m_inst, TaskType);
            probe.set_task_id(m_task_id);
            for (const auto& point : prior_verified_positions) {
                prior_points.emplace_back(point.x, point.y);
                if (probe.revalidate_promoted_pose(m_pending_promotion, point, m_policy.round, true)) {
                    confirmed_positions.emplace_back(point.x, point.y);
                }
            }
            // Existing elite bodies survive a later synthesis. Preserve every
            // independently verified elite pose within the exact physical
            // count; do not guess the locations of unseen bodies or initials.
            const auto retained =
                source_forms ? spa::retain_promoted_elite_poses(*source_forms, prior_points, confirmed_positions)
                             : std::vector<std::pair<int, int>> {};
            for (const auto& point : retained) {
                m_auto_deploy_positions.emplace(m_pending_promotion, point);
                m_verified_field_pose_points.insert(point);
            }
            auto recovered = basic_info_with_what("SPAPromotedFieldPosesReconciled");
            recovered["details"] = json::object { { "name", m_pending_promotion },
                                                  { "round", m_policy.round },
                                                  { "initial_copies", source_forms ? source_forms->initial : -1 },
                                                  { "elite_copies", source_forms ? source_forms->elite : -1 },
                                                  { "prior_positions", prior_points.size() },
                                                  { "positive_probes", confirmed_positions.size() },
                                                  { "retained_positions", retained.size() },
                                                  { "inventory_complete", false },
                                                  { "directions_retained", false } };
            callback(AsstMsg::SubTaskExtraInfo, recovered);
        }
    }
    if (!reward_already_owned) {
        m_equipped_items[selected_name] = {};
    }
    if (source_known) {
        record_acquired_equipment_hints(m_pending_promotion, true, m_policy.round);
    }
    record_acquired_equipment_hints(selected_name, false, m_policy.round);
    m_recent_bench_acquisition = selected_name;
    m_unavailable_replacement_sources.erase(selected_name);
    m_bench_recount_pending = true;
    auto confirmed = basic_info_with_what("SPAPromotionReconciled");
    confirmed["details"] =
        json::object { { "source", m_pending_promotion },
                       { "pact_recruitment", pact_recruitment },
                       { "recruitment_pact", m_pending_pact_recruitment },
                       { "pact_recruitment_pending", std::string() },
                       { "shop_level", m_policy.shop_level },
                       { "resume_after_round", m_policy.round },
                       { "source_known", source_known },
                       { "name", selected_name },
                       { "elite", false },
                       { "source_elite", source_known },
                       { "run_id", m_run_id },
                       { "round", m_policy.round },
                       { "roster", roster_snapshot() },
                       { "used_deploy_points", deployment_point_snapshot() },
                       { "field_positions", field_position_snapshot() },
                       { "equipment_slots", equipment_slots_snapshot() },
                       { "equipment_candidates", equipment_candidates_snapshot() },
                       { "cash_equipment_acquired_round", m_cash_equipment_acquired_round },
                       { "bench_occupied", m_policy.bench_occupied },
                       { "cash_equipment_pending",
                         m_pending_cash_equipment ? json::value(*m_pending_cash_equipment) : json::value(nullptr) },
                       { "bench_recount_pending", true },
                       { "bench_population_verified", false },
                       { "returned_to_preparation", !chain_observed },
                       { "short_title_confirmed_on_two_frames", short_title_confirmed },
                       { "promotion_pending", chain_observed ? selected_name : std::string() },
                       { "next_reward_verified", chain_observed },
                       { "run_victory_verified", false } };
    callback(AsstMsg::SubTaskExtraInfo, confirmed);
    m_pending_pact_recruitment.clear();
    m_pending_promotion = chain_observed ? selected_name : std::string();
    m_pending_promotion_reward.reset();
    return true;
}

bool asst::StrongholdProtocolTask::run()
{
    // A batch owns no single-game evidence scope. Each child wraps all its
    // plugins with its own scope, including observations and settlement.
    // Re-entering the same configured game creates a new execution identity.
    const auto original_callback = m_callback;

    struct RestoreRunState
    {
        bool& running;
        AsstCallback& callback;
        AsstCallback previous_callback;

        ~RestoreRunState()
        {
            running = false;
            callback = std::move(previous_callback);
        }
    } restore { m_running, m_callback, original_callback };

    const bool single_game = m_run_count == 1;
    m_run_victory_verified = false;
    static const std::string host_session_id = MaaNS::make_uuid();
    const std::string execution_id = single_game ? MaaNS::make_uuid() : std::string();
    if (single_game) {
        m_callback = [original_callback,
                      run_id = m_run_id,
                      execution_id](AsstMsg msg, const json::value& info, Assistant* inst) {
            if (!original_callback) {
                return;
            }
            auto scoped = info;
            scoped["run_id"] = run_id;
            scoped["run_execution_id"] = execution_id;
            scoped["host_session_id"] = host_session_id;
            original_callback(msg, scoped, inst);
        };
        auto started = basic_info_with_what("SPARunExecutionStarted");
        started["details"] = json::object { { "run_id", m_run_id },
                                            { "mode", m_mode },
                                            { "difficulty", m_difficulty },
                                            { "task_id", m_task_id },
                                            { "assisted_run", m_initial_parameters.get("assisted_run", false) } };
        callback(AsstMsg::SubTaskExtraInfo, started);
    }
    m_running = true;
    const bool success = AbstractTask::run();
    m_running = false;
    if (single_game) {
        auto finished = basic_info_with_what("SPARunExecutionFinished");
        finished["details"] = json::object { { "run_id", m_run_id },
                                             { "mode", m_mode },
                                             { "difficulty", m_difficulty },
                                             { "task_id", m_task_id },
                                             { "runner_success", success },
                                             { "stop_requested", need_exit() },
                                             { "assisted_run", m_initial_parameters.get("assisted_run", false) },
                                             { "run_victory_verified", m_run_victory_verified } };
        callback(AsstMsg::SubTaskExtraInfo, finished);
    }
    return success;
}

bool asst::StrongholdProtocolTask::_run()
{
    if (m_run_count == 1) {
        return run_single();
    }
    auto parameters = m_initial_parameters;
    const auto batch_id = m_run_id.empty() ? "spa-batch-" + std::to_string(m_task_id) : m_run_id;
    for (int completed = 0; completed < m_run_count && !need_exit(); ++completed) {
        parameters["run_count"] = 1;
        parameters["run_id"] = batch_id + "-" + std::to_string(completed + 1);
        bool returned = false;
        bool completed_result = false;
        const auto forward = [&](AsstMsg msg, const json::value& info, Assistant* inst) {
            if (info.get("what", std::string()) == "SPALifecycleCompleted") {
                if (const auto receipt = info.find<json::object>("details")) {
                    returned = spa::settled_run_returned(*receipt);
                    completed_result = receipt->get("result_completed", false);
                }
            }
            if (m_callback) {
                auto event = info;
                event["batch_run_id"] = parameters.get("run_id", std::string());
                m_callback(msg, event, inst);
            }
        };
        StrongholdProtocolTask game(forward, m_inst);
        game.set_task_id(m_task_id);
        const bool executed = game.set_params(parameters) && game.run();
        if (!executed || !returned || need_exit()) {
            auto stopped = basic_info_with_what("SPABatchStopped");
            stopped["details"] = json::object { { "settled_runs", completed },
                                                { "requested_runs", m_run_count },
                                                { "return_home_verified", returned },
                                                { "new_run_started", false } };
            callback(AsstMsg::SubTaskExtraInfo, stopped);
            return false;
        }
        auto progress = basic_info_with_what("SPABatchProgress");
        progress["details"] = json::object { { "settled_runs", completed + 1 },
                                             { "requested_runs", m_run_count },
                                             { "result_completed", completed_result },
                                             { "run_victory_verified", game.m_run_victory_verified },
                                             { "return_home_verified", true } };
        callback(AsstMsg::SubTaskExtraInfo, progress);
        parameters = spa::fresh_run_parameters(m_initial_parameters);
    }
    return !need_exit();
}

std::optional<json::object> asst::StrongholdProtocolTask::scan_strategy_progress(const std::string& target)
{
    spa::ObserverTaskPlugin observer(m_callback, m_inst, TaskType);
    observer.set_retry_times(0);
    observer.set_task_id(m_task_id);
    const auto lobby_verified = [&] {
        if (!observer.observe_frame(ctrler()->get_image())) {
            return false;
        }
        const auto& observation = observer.last_observation();
        return observation.get("page", std::string()) == "lobby" && !observation.get("contradictory", true);
    };
    if (!lobby_verified()) {
        return std::nullopt;
    }
    std::optional<json::object> result;
    const auto forward = [&](AsstMsg msg, const json::value& info, Assistant* inst) {
        if (info.get("what", std::string()) == "SPAStrategyReportScan") {
            result = info.find<json::object>("details");
        }
        if (m_callback) {
            m_callback(msg, info, inst);
        }
    };
    const auto run_report_task = [&](const std::string& name, bool scan = false) {
        ProcessTask task(forward, m_inst, TaskType);
        task.set_task_id(m_task_id);
        task.set_retry_times(0);
        task.set_tasks({ name });
        task.override_next("MiniGame@SPA@Observe", {});
        if (scan) {
            const auto plugin = task.register_plugin<spa::StrategyReportTaskPlugin>();
            plugin->set_target(target);
            plugin->set_retry_times(0);
        }
        return task.run();
    };
    const bool scanned = run_report_task("MiniGame@SPA@OpenReport") &&
                         run_report_task("MiniGame@SPA@OpenStrategyReport") &&
                         run_report_task("MiniGame@SPA@ScanStrategyReport", true);
    // ReportBack positively recognizes the tab before any click. Only a
    // freshly reobserved lobby can authorize a subsequent simulation.
    if (!need_exit() && !lobby_verified()) {
        static_cast<void>(run_report_task("MiniGame@SPA@ReportBack"));
    }
    if (!scanned || need_exit() || !lobby_verified()) {
        return std::nullopt;
    }
    return result;
}

bool asst::StrongholdProtocolTask::identify_current_strategy(const json::value& state)
{
    if (!m_selected_strategy_id.empty() || m_current_strategy_inspected ||
        state.get("page", std::string()) != "preparation" || !state.get("round_known", false) ||
        (m_mode == "joint" && state.get("preparation_seconds_remaining", 0) < 35)) {
        return false;
    }
    const auto texts = state.find<json::array>("texts");
    if (!texts || std::none_of(texts->begin(), texts->end(), [](const auto& item) {
            const int x = item.get("x", -1), y = item.get("y", -1);
            return item.get("score", 0.) >= .88 && item.get("text", std::string()) == "查看" && x >= 1120 && x < 1190 &&
                   y >= 195 && y < 270;
        })) {
        return false;
    }
    m_current_strategy_inspected = true;
    const Point view(1155, 235);
    if (!ctrler()->click(view) || !sleep(400)) {
        return true;
    }
    const auto frame = ctrler()->get_image();
    OCRer panel(frame, Rect(755, 154, 375, 105));
    const auto read = panel.analyze();
    std::string strategy;
    if (read) {
        for (const auto& identity : spa::strategy_identities()) {
            const bool leader = std::any_of(read->begin(), read->end(), [&](const auto& item) {
                return item.score >= .90 && item.rect.y < 198 && item.text == identity.leader;
            });
            const bool title = std::any_of(read->begin(), read->end(), [&](const auto& item) {
                return item.score >= .90 && item.rect.y >= 198 &&
                       (item.text == identity.name || item.text == "【" + identity.name + "】");
            });
            if (leader && title) {
                if (!strategy.empty()) {
                    strategy.clear();
                    break;
                }
                strategy = identity.band_id;
            }
        }
    }
    const bool panel_observed = read && !read->empty();
    const bool dismissed = panel_observed && ctrler()->click(view) && sleep(300);
    if (!strategy.empty() && dismissed) {
        m_selected_strategy_id = strategy;
    }
    auto evidence = basic_info_with_what("SPACurrentStrategyInspected");
    evidence["details"] =
        json::object { { "round", state.get("round", -1) },  { "strategy_id", m_selected_strategy_id },
                       { "panel_observed", panel_observed }, { "leader_and_title_verified", !strategy.empty() },
                       { "panel_close_issued", dismissed },  { "strategy_use_issued", false } };
    callback(AsstMsg::SubTaskExtraInfo, evidence);
    return true;
}

bool asst::StrongholdProtocolTask::handle_temporary_elite_instruction(const json::value& state)
{
    const int round = state.get("round", -1), funds = state.get("funds", -1);
    if (m_selected_strategy_id != "band_evolve" || round < 1 || funds < 1 || !state.get("funds_known", false) ||
        !state.get("remaining_deployment_known", false) || m_preparation_instruction_attempts.contains(round) ||
        m_pending_spatial_orientation || m_pending_field_replacement || m_field_checkpoint_loaded ||
        (m_mode == "joint" && state.get("preparation_seconds_remaining", 0) < 25)) {
        return false;
    }
    std::string target;
    std::pair<int, int> cell;
    int best_value = 0;
    for (const auto& [name, point] : m_auto_deploy_positions) {
        const auto owned = m_policy.owned.find(name);
        if (owned == m_policy.owned.end() || owned->second.elite || m_auto_deploy_positions.count(name) != 1 ||
            !m_verified_field_pose_points.contains(point) || point.second >= 420) {
            continue;
        }
        const auto initial = spa::season_combat_stats(name, false);
        const auto elite = spa::season_combat_stats(name, true);
        if (!initial || !elite) {
            continue;
        }
        const bool carry = std::find(m_policy.carries.begin(), m_policy.carries.end(), name) != m_policy.carries.end();
        const int value = (carry ? 1500 : 0) + elite->attack - initial->attack + (elite->max_hp - initial->max_hp) / 5;
        if (value > best_value) {
            target = name;
            cell = point;
            best_value = value;
        }
    }
    if (target.empty() || !spa::active_strategy_ready(ctrler()->get_image())) {
        return false;
    }
    m_preparation_instruction_attempts.insert(round);
    spa::SwapTaskPlugin instruction(m_callback, m_inst, TaskType);
    instruction.set_retry_times(0);
    instruction.set_task_id(m_task_id);
    instruction.set_task_ptr(this);
    if (instruction.configure(
            json::object { { "name", target },
                           { "replaced", "全场聚焦" },
                           { "strategy_only", true },
                           { "source_x", 1230 },
                           { "source_y", 235 },
                           { "target_x", cell.first },
                           { "target_y", cell.second },
                           { "drag_target_x", cell.first },
                           { "drag_target_y", cell.second },
                           { "target_elite", false },
                           { "round", round },
                           { "funds", funds },
                           { "remaining_deployment", state.get("remaining_deployment", -1) } })) {
        static_cast<void>(instruction.run());
    }
    return true;
}

bool asst::StrongholdProtocolTask::run_single()
{
    if (!m_manage_lifecycle) {
        return run_session();
    }
    const auto progress_record = [](const json::object& report,
                                    const std::string& id) -> std::optional<spa::StrategyProgress> {
        const auto entries = report.find<json::array>("strategies");
        if (id.empty() || !entries || !spa::strategy_unlock_rule(id)) {
            return std::nullopt;
        }
        std::optional<spa::StrategyProgress> result;
        for (const auto& entry : *entries) {
            if (entry.get("band_id", std::string()) != id) {
                continue;
            }
            const bool wins_known = entry.get("wins_known", false);
            const auto wins = wins_known ? entry.find<int>("wins") : std::nullopt;
            if (result || (wins_known && (!wins || *wins < 0 || *wins > 6)) || !entry.get("unlocked_verified", false)) {
                return std::nullopt;
            }
            result = spa::StrategyProgress { id,
                                             wins,
                                             true,
                                             entry.get("badge_verified", false),
                                             spa::strategy_available_in_mode(id, "independent") };
        }
        return result;
    };
    std::optional<spa::StrategyProgress> scheduled;
    if (m_schedule_strategy_badges) {
        const auto report = scan_strategy_progress();
        if (report) {
            scheduled = progress_record(*report, report->get("next_independent_strategy", std::string()));
        }
        const bool selected = scheduled && spa::ProgressLedger::can_schedule_solo(*scheduled);
        auto receipt = basic_info_with_what("SPAStrategyBadgeSchedule");
        receipt["details"] =
            json::object { { "report_and_lobby_verified", report.has_value() },
                           { "identified_strategies", report ? report->get("identified_strategies", 0) : 0 },
                           { "all_strategy_badges_verified",
                             report && report->get("all_strategy_badges_verified", false) },
                           { "requested_strategy", selected ? scheduled->band_id : "" },
                           { "report_wins_before", selected ? scheduled->wins.value_or(-1) : -1 },
                           { "simulation_start_issued", false } };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        if (!selected) {
            return report && report->get("all_strategy_badges_verified", false);
        }
        m_requested_strategy_id = scheduled->band_id;
    }
    std::shared_ptr<spa::LifecycleTaskPlugin> lifecycle;
    std::shared_ptr<spa::DifficultyTaskPlugin> start_simulation;
    bool opening_pacts_known = false;
    bool settled_return_verified = false;

    const auto forwarding = [&](AsstMsg msg, const json::value& info, Assistant* inst) {
        if (lifecycle) {
            lifecycle->observe_event(info);
        }
        const auto what = info.get("what", std::string());
        if (what == "SPALifecycleCompleted") {
            if (const auto receipt = info.find<json::object>("details")) {
                settled_return_verified = spa::settled_run_returned(*receipt);
                m_run_victory_verified = spa::verified_solo_victory(*receipt, m_mode, m_difficulty);
            }
        }
        if (what == "SPABossAdvertised" && info.get("details", "boss_advertisement_verified", false)) {
            const auto family = info.get("details", "boss_family_name", std::string());
            if (spa::known_boss_family(family)) {
                m_advertised_boss_family = family;
                if (m_callback) {
                    auto checkpoint = info;
                    checkpoint["details"]["run_id"] = m_run_id;
                    checkpoint["details"]["roster"] = roster_snapshot();
                    m_callback(msg, checkpoint, inst);
                }
                return;
            }
        }
        if (what == "SPAPactAvailabilityScanned" && info.get("details", "after_core_selection_verified", false)) {
            opening_pacts_known = info.get("details", "complete", false);
            const auto details = info.find<json::object>("details");
            if (const auto disabled = details ? details->find<json::array>("disabled_pacts") : std::nullopt) {
                m_observed_disabled_pacts.clear();
                for (const auto& item : *disabled) {
                    if (item.is_string()) {
                        m_observed_disabled_pacts.emplace_back(item.as_string());
                    }
                }
                refresh_mode_pact_restrictions();
            }
        }
        if (what == "SPADifficultySelected") {
            m_difficulty = info.get("details", "difficulty", m_difficulty);
            refresh_mode_pact_restrictions();
            if (start_simulation) {
                start_simulation->set_target(m_difficulty, true, m_mode);
            }
        }
        if (what == "SPAStrategyCardSelected") {
            m_selected_strategy_id = info.get("details", "strategy_id", m_selected_strategy_id);
        }
        if (m_mode == "joint" && what == "SPAStrategyOccupiedObserved" &&
            info.get("details", "occupied_label_verified", false)) {
            const auto id = info.get("details", "strategy_id", std::string());
            if (!id.empty() &&
                std::find(m_policy.occupied_strategy_ids.begin(), m_policy.occupied_strategy_ids.end(), id) ==
                    m_policy.occupied_strategy_ids.end()) {
                m_policy.occupied_strategy_ids.push_back(id);
            }
        }
        if (what == "SPAFormationSelected" && info.get("details", "formation_page_verified", false) &&
            info.get("details", "readiness_committed", false) && m_policy.shop_level == 0) {
            // A confirmed new-run formation starts at book I. Keep that
            // authoritative lifecycle fact when OCR misses the narrow I
            // glyph; never initialize a resumed mid-run shop this way.
            m_policy.shop_level = 1;
        }
        if (what == "SPARunSetupVerified" && info.get("details", "selection_verified", false) &&
            info.get("details", "availability_after_selection_verified", false)) {
            const auto details = info.find<json::object>("details");
            if (const auto disabled = details ? details->find<json::array>("disabled_pacts") : std::nullopt) {
                m_observed_disabled_pacts.clear();
                for (const auto& item : *disabled) {
                    if (item.is_string()) {
                        m_observed_disabled_pacts.emplace_back(item.as_string());
                    }
                }
                refresh_mode_pact_restrictions();
            }
        }
        if (m_callback) {
            m_callback(msg, info, inst);
        }
    };
    ProcessTask flow(forwarding, m_inst, TaskType);
    flow.set_task_id(m_task_id);
    flow.set_task_delay(180).set_tasks({ "MiniGame@SPA@LifecycleDispatch" });
    flow.set_retry_times(2);
    const std::vector<std::string> dispatch { "MiniGame@SPA@LifecycleDispatch" };
    for (const auto* task : { "MiniGame@SPA@Observe",
                              "MiniGame@SPA@ReturnResultHomeClick",
                              "MiniGame@SPA@ReportBack",
                              "MiniGame@SPA@OpenIndependentStart",
                              "MiniGame@SPA@LifecycleOpenJoint",
                              "MiniGame@SPA@ReconnectCurrent" }) {
        flow.override_next(task, dispatch);
    }
    auto choose_difficulty = flow.register_plugin<spa::DifficultyTaskPlugin>();
    start_simulation = flow.register_plugin<spa::DifficultyTaskPlugin>();
    auto setup = flow.register_plugin<spa::SetupTaskPlugin>();
    auto strategy = flow.register_plugin<spa::StrategyCardTaskPlugin>();
    strategy->set_context_provider([&] {
        return spa::OpeningStrategyContext { opening_pacts_known,
                                             m_policy.disabled_pacts,
                                             m_policy.occupied_strategy_ids,
                                             m_mode };
    });
    auto formation = flow.register_plugin<spa::FormationSelectTaskPlugin>();
    const auto opening_core =
        m_policy.primary_pact.empty() ||
                std::find(m_policy.disabled_pacts.begin(), m_policy.disabled_pacts.end(), m_policy.primary_pact) !=
                    m_policy.disabled_pacts.end()
            ? std::string("auto")
            : m_policy.primary_pact;
    if (!choose_difficulty->set_target(m_difficulty, false, m_mode) ||
        !start_simulation->set_target(m_difficulty, true, m_mode) || !setup->configure(opening_core, {}, -1) ||
        !strategy->configure(-1, m_prefer_unclaimed, m_requested_strategy_id) ||
        !formation->configure({ "auto" }, m_dynamic_primary_pact ? "auto" : opening_core)) {
        return false;
    }
    for (const auto& plugin : { std::static_pointer_cast<AbstractTaskPlugin>(choose_difficulty),
                                std::static_pointer_cast<AbstractTaskPlugin>(start_simulation),
                                std::static_pointer_cast<AbstractTaskPlugin>(setup),
                                std::static_pointer_cast<AbstractTaskPlugin>(strategy),
                                std::static_pointer_cast<AbstractTaskPlugin>(formation) }) {
        plugin->set_retry_times(0);
    }
    choose_difficulty->set_recovery_task(dispatch.front());
    start_simulation->set_recovery_task(dispatch.front());
    setup->set_recovery_task(dispatch.front());
    strategy->set_recovery_task(dispatch.front());
    formation->set_recovery_task(dispatch.front());
    lifecycle = flow.register_plugin<spa::LifecycleTaskPlugin>(
        [&] {
            const bool executed = run_session(lifecycle.get());
            lifecycle->note_battle(m_lifecycle_battle_seen);
            if (m_lifecycle_solo_result) {
                lifecycle->note_solo_result(*m_lifecycle_solo_result);
                m_lifecycle_solo_result.reset();
            }
            return executed;
        },
        m_client_type,
        m_mode,
        m_require_fresh_independent && m_mode == "independent" && m_difficulty >= 1 && m_difficulty <= 20
            ? std::optional(m_difficulty)
            : std::nullopt);
    const bool executed = flow.run();
    const bool full_run = m_auto_run && !m_decision_only && !m_stop_before_battle && !m_stop_after_deployment &&
                          !m_movement_only && !m_round_only;
    if (executed && full_run && !settled_return_verified) {
        // An empty recovery branch can complete ProcessTask without finishing
        // the game. Report that interruption instead of a successful UI task.
        auto receipt = basic_info_with_what("SPALifecycleIncomplete");
        receipt["details"] = json::object { { "reason", "Native lifecycle ended without a verified settlement return" },
                                            { "return_home_verified", false },
                                            { "resumable", true },
                                            { "run_abandoned", false },
                                            { "input_issued", false } };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        return false;
    }
    if (scheduled && executed && settled_return_verified && !need_exit()) {
        const bool matching_strategy = m_selected_strategy_id == scheduled->band_id;
        const auto report =
            matching_strategy && m_run_victory_verified ? scan_strategy_progress(scheduled->band_id) : std::nullopt;
        const auto after = report ? progress_record(*report, scheduled->band_id) : std::nullopt;
        const bool credited = after && spa::ProgressLedger::next_report_confirms_win(
                                           *scheduled,
                                           *after,
                                           m_selected_strategy_id,
                                           m_run_victory_verified);
        auto receipt = basic_info_with_what("SPAStrategyBadgeScheduleSettled");
        receipt["details"] =
            json::object { { "requested_strategy", scheduled->band_id },
                           { "actual_strategy", m_selected_strategy_id },
                           { "same_strategy", matching_strategy },
                           { "settlement_victory_verified", m_run_victory_verified },
                           { "report_wins_before", scheduled->wins.value_or(-1) },
                           { "report_wins_after", after ? after->wins.value_or(-1) : -1 },
                           { "report_win_delta_verified", credited },
                           { "three_win_badge_verified", after && spa::ProgressLedger::complete(*after) } };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
    }
    return executed;
}

bool asst::StrongholdProtocolTask::run_session(spa::LifecycleTaskPlugin* lifecycle)
{
    spa::ObserverTaskPlugin observer(m_callback, m_inst, TaskType);
    observer.set_retry_times(0);
    observer.set_task_id(m_task_id);
    const auto catalog = spa::operator_pacts();
    int observation_failures = 0;
    for (int action = 0; action < m_max_actions && !need_exit(); ++action) {
        if (m_pending_spatial_orientation) {
            // A checkpoint can resume while the same-cell direction modal
            // is still open. Cancel only its visually verified control;
            // never repeat the held source movement after a process restart.
            const auto frame = ctrler()->get_image();
            if (const auto center = spa::direction_selection_center(frame)) {
                if (const auto cancel = spa::direction_cancel_target(frame, *center)) {
                    if (!ctrler()->click(Point(cancel->x, cancel->y)) || !sleep(350)) {
                        return false;
                    }
                    auto restored = basic_info_with_what("SPASpatialOrientationModalRecovered");
                    restored["details"] = json::object { { "run_id", m_run_id },
                                                         { "held_input_replayed", false },
                                                         { "direction_committed", false },
                                                         { "field_identity_revalidation_required", true } };
                    callback(AsstMsg::SubTaskExtraInfo, restored);
                    continue;
                }
            }
        }
        // A confirmation modal can survive a previous native round attempt.  It is
        // not a preparation page, so the read-only observer cannot classify it.
        // Any autonomous run may resume this modal: the battle gate has
        // already reviewed spending before clicking Ready, and leaving it
        // open would otherwise make every observation retry forever.
        json::value state_storage;
        bool used_cached_deployment_observation = false;
        std::optional<spa::PreparationDeadline::Clock::time_point> observation_started;
        if (m_pending_cash_equipment && !m_pending_cash_equipment->find<bool>("observation_checked").value_or(false)) {
            m_cached_deployment_observation.reset();
        }
        if (m_policy.basic_mode && m_cached_deployment_observation) {
            state_storage = std::move(*m_cached_deployment_observation);
            m_cached_deployment_observation.reset();
            used_cached_deployment_observation = true;
        }
        if (!used_cached_deployment_observation) {
            // Anchor the frame before capture/OCR, rather than adding their
            // elapsed time back to the game's preparation countdown.
            observation_started = spa::PreparationDeadline::Clock::now();
            if (!observer.run()) {
                ++observation_failures;
                auto recovery = basic_info_with_what("SPAAutomationObservationRetry");
                recovery["details"] = json::object { { "attempt", observation_failures },
                                                     { "max_attempts_before_action_budget", m_max_actions },
                                                     { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, recovery);
                // A transient OCR/transition frame is not a run failure.  Keep the
                // current game open and re-observe; the outer action budget remains
                // the only bounded escape hatch for a genuinely stuck screen.
                if (!sleep(300)) {
                    return false;
                }
                continue;
            }
            state_storage = observer.last_observation();
        }
        observation_failures = 0;
        // The digit beside “剩余可放置角色” is occasionally occluded by
        // the map/shop overlay even though the preparation HUD is stable.
        // Carry forward the last authoritative counter in phase one; a
        // transient OCR gap must not block Ready after the formation is
        // already on the map. New deployments update this value through the
        // reconciled receipt/cached observation below.
        if (state_storage.get("page", std::string()) == "preparation") {
            if (state_storage.get("preparation_boss_bar_visible", false)) {
                m_spatial_boss_phase = true;
                m_spatial_boss_phase_round = state_storage.get("round", 0);
            }
            else if (
                state_storage.get("round_known", false) &&
                state_storage.get("round", 0) != m_spatial_boss_phase_round) {
                m_spatial_boss_phase = false;
            }
            if (state_storage.get("remaining_deployment_known", false)) {
                const int remaining = state_storage.get("remaining_deployment", -1);
                if (remaining >= 0 && remaining <= 8) {
                    m_last_remaining_deployment = remaining;
                }
            }
            else if (m_phase_one_game_layout && m_last_remaining_deployment) {
                state_storage["remaining_deployment"] = *m_last_remaining_deployment;
                state_storage["remaining_deployment_known"] = true;
                state_storage["remaining_deployment_inferred"] = true;
                auto inferred = basic_info_with_what("SPADeploymentCounterInferred");
                inferred["details"] = json::object { { "remaining_deployment", *m_last_remaining_deployment },
                                                     { "source", "last_authoritative_preparation_observation" },
                                                     { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, inferred);
            }
            else if (m_phase_one_game_layout && m_policy.bench_occupied >= 10) {
                // A resumed preparation page can hide the small digit beside
                // the deployment counter behind the full-bench warning.  If
                // the authoritative warning says no further operators can be
                // dispatched and the local checkpoint already has a full
                // bench, treating the remaining slot count as zero is safer
                // than looping through the same OCR frame indefinitely.  This
                // is explicitly marked as inferred; a fresh known counter
                // always takes precedence on the next observation.
                const auto state_texts = state_storage.find<json::array>("texts");
                bool full_bench_warning = false;
                if (state_texts) {
                    for (const auto& item : *state_texts) {
                        const auto value = item.get("text", std::string());
                        if (item.get("score", 0.0) >= 0.85 && (value.find("干员已满") != std::string::npos ||
                                                               value.find("无法继续调度干员") != std::string::npos)) {
                            full_bench_warning = true;
                            break;
                        }
                    }
                }
                if (full_bench_warning) {
                    state_storage["remaining_deployment"] = 0;
                    state_storage["remaining_deployment_known"] = true;
                    state_storage["remaining_deployment_inferred"] = true;
                    auto inferred = basic_info_with_what("SPADeploymentCounterInferred");
                    inferred["details"] = json::object { { "remaining_deployment", 0 },
                                                         { "source", "full_bench_warning_on_resumed_preparation" },
                                                         { "run_victory_verified", false } };
                    callback(AsstMsg::SubTaskExtraInfo, inferred);
                }
            }
        }
        if (m_mode == "joint" && state_storage.get("page", std::string()) == "preparation" &&
            state_storage.get("round_known", false)) {
            const bool read_now =
                observation_started.has_value() && state_storage.get("preparation_seconds_remaining_known", false);
            const auto remaining = m_joint_preparation_deadline.observe_frame(
                state_storage.get("round", -1),
                read_now ? std::optional(state_storage.get("preparation_seconds_remaining", 0)) : std::nullopt,
                observation_started,
                spa::PreparationDeadline::Clock::now());
            state_storage["preparation_seconds_remaining"] = remaining.value_or(0);
            state_storage["preparation_seconds_remaining_known"] = remaining.has_value();
            state_storage["preparation_seconds_from_previous_observation"] = !read_now;
        }
        const auto& state = state_storage;
        // Attribute pending coin credit before purchase-modal recovery, sales,
        // equipment use, or any other operation that can change the wallet.
        // A false return has permanently closed an unresolved attribution
        // window; later transactions may proceed without resolving that coin.
        if (m_auto_run && state.get("page", std::string()) == "preparation" &&
            reconcile_pending_cash_equipment(state)) {
            continue;
        }
        if (state.get("page", std::string()) == "spectating") {
            if (const auto texts = state.find<json::array>("texts")) {
                for (const auto& text : *texts) {
                    if (text.get("text", std::string()) == "返回战场" && text.get("score", 0.0) >= .9 &&
                        text.get("x", -1) >= 0 && text.get("x", -1) < 110 && text.get("y", -1) >= 120 &&
                        text.get("y", -1) < 490) {
                        if (!ctrler()->click(Point(text.get("x", 0) + 20, text.get("y", 0) + 10))) {
                            return false;
                        }
                        break;
                    }
                }
            }
            if (!sleep(300)) {
                return false;
            }
            continue; // A fresh local frame must precede any transaction.
        }
        if (state.get("page", std::string()) == "ready_waiting") {
            if (!sleep(500)) {
                return false;
            }
            continue; // Cancel Ready is never a second confirmation click.
        }
        if (m_auto_run && state.get("page", std::string()) == "ready_confirmation") {
            std::vector<spa::TextObservation> modal_texts;
            if (const auto texts = state.find<json::array>("texts")) {
                for (const auto& text : *texts) {
                    modal_texts.push_back(
                        { text.get("text", std::string()),
                          text.get("score", 0.0),
                          text.get("x", -1),
                          text.get("y", -1) });
                }
            }
            // The observer already recognized this modal from one frame.
            // Do not OCR the same region before every ordinary action.
            const auto control = spa::ready_confirmation_control(modal_texts, m_spending_reviewed);
            if (!control) {
                if (!sleep(300)) {
                    return false;
                }
                continue;
            }
            spa::save_evidence(ctrler()->get_image(), utils::path("debug/SPA/ready_confirmation_resume"), false);
            if (!ctrler()->click(Point(control->x + 20, control->y + 10)) || !sleep(m_spending_reviewed ? 1000 : 300)) {
                continue;
            }
            auto receipt =
                basic_info_with_what(m_spending_reviewed ? "SPARoundReadyConfirmed" : "SPARoundReadyCancelled");
            receipt["details"] = json::object { { "source", "page_observer" },
                                                { "spending_reviewed", m_spending_reviewed },
                                                { "round_known", false },
                                                { "fresh_observation_required", true } };
            callback(AsstMsg::SubTaskExtraInfo, receipt);
            continue;
        }
        if (state.get("page", std::string()) == "preparation" && state.get("round_known", false) &&
            state.get("round", -1) != m_last_economy_round) {
            m_last_economy_round = state.get("round", -1);
            m_shopping_complete = false;
            m_spending_reviewed = false;
            m_spending_reason.clear();
            // The policy round is also updated before deployment planning.
            // Clear transient failures at this authoritative HUD transition,
            // not later by comparing an already-updated policy round.
            m_auto_deploy_blocked_names.clear();
            m_auto_deploy_suppressed_round = -1;
            m_policy.shop_frozen_this_round = false;
            m_policy.last_round_life_loss = -1;
            if (state.get("life_known", false)) {
                const int life = state.get("life", 0);
                if (m_previous_round_life) {
                    m_policy.last_round_life_loss = std::max(0, *m_previous_round_life - life);
                }
                m_previous_round_life = life;
            }
            if (m_policy.last_round_life_loss > 5) {
                auto pressure = basic_info_with_what("SPAEarlyBoardPressure");
                pressure["details"] = json::object { { "round", m_last_economy_round },
                                                     { "previous_life_loss", m_policy.last_round_life_loss },
                                                     { "review_deployment_and_spending", true },
                                                     { "run_abandoned", false } };
                callback(AsstMsg::SubTaskExtraInfo, pressure);
            }
        }
        // A fast purchase can commit the card click while the native task is
        // interrupted or its confirmation OCR misses one frame.  The next
        // observer still classifies the underlying page as preparation, but
        // the live "确认购买" modal must be finished before any new shop
        // decision.  Recover it once from the already-returned text list;
        // otherwise the loop repeatedly reopens the same card and appears to
        // be stuck in preparation.
        if (state.get("page", std::string()) == "preparation" && m_pending_promotion.empty() &&
            m_pending_pact_recruitment.empty()) {
            const auto state_texts = state.find<json::array>("texts");
            std::optional<Rect> stale_purchase_confirm;
            if (state_texts) {
                for (const auto& item : *state_texts) {
                    const auto text = item.get("text", std::string());
                    const auto score = item.get("score", 0.0);
                    const int x = item.get("x", -1);
                    const int y = item.get("y", -1);
                    if (score >= 0.85 && text.find("确认购买") != std::string::npos && x >= 120 && x < 1100 &&
                        y >= 620 && y < 710) {
                        if (stale_purchase_confirm) {
                            stale_purchase_confirm.reset();
                            break;
                        }
                        stale_purchase_confirm = Rect(x, y, item.get("width", 100), item.get("height", 40));
                    }
                }
            }
            if (stale_purchase_confirm) {
                if (!ctrler()->click(*stale_purchase_confirm) || !sleep(m_policy.basic_mode ? 350 : 700)) {
                    return false;
                }
                auto recovered = basic_info_with_what("SPAAutomationPurchaseConfirmationRecovered");
                recovered["details"] = json::object { { "round", state.get("round", -1) },
                                                      { "funds_before", state.get("funds", -1) },
                                                      { "source", "preparation_observation" },
                                                      { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, recovered);
                continue;
            }
        }
        // A guarded held-drag can leave the operator in the placement view
        // after the game rejects the target (for example when a pixel cell
        // was stale).  Treat that view as recoverable: cancel the pending
        // placement and let the next action re-observe preparation.  Waiting
        // on the placement page forever used to make one bad pixel candidate
        // look like a run failure.
        if (state.get("page", std::string()) == "placement") {
            const auto placement_image = ctrler()->get_image();
            OCRer placement_ocr(placement_image);
            std::optional<Rect> cancel;
            bool selected_operator_panel = false;
            if (const auto recognized = placement_ocr.analyze()) {
                bool has_range = false;
                bool has_sale = false;
                for (const auto& text : *recognized) {
                    if (text.score >= 0.9 && text.text == "点击取消") {
                        cancel = text.rect;
                    }
                    has_range = has_range || (text.score >= 0.9 && text.text == "攻击范围");
                    has_sale = has_sale || (text.score >= 0.9 && text.text == "出售");
                }
                selected_operator_panel = has_range && has_sale;
            }
            if (cancel && ctrler()->click(*cancel) && sleep(300)) {
                auto recovered = basic_info_with_what("SPAAutomationPlacementCancelled");
                recovered["details"] = json::object { { "reason", "Recover stale held-drag placement view" },
                                                      { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, recovered);
                continue;
            }
            if (!cancel && selected_operator_panel && ctrler()->click(Point(1040, 80)) && sleep(300)) {
                auto recovered = basic_info_with_what("SPAAutomationPlacementPanelDismissed");
                recovered["details"] =
                    json::object { { "reason", "Dismiss stale selected-operator panel without selling" },
                                   { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, recovered);
                continue;
            }
        }
        // A resumed process can lose the in-memory pending source while the
        // game is already showing the three-card free elite shop.  The page
        // is otherwise OCR-classified as the generic “请选择奖励” bounty
        // overlay.  Detect the promotion window from two independent facts:
        // at least two zero-price card badges and at least two catalog
        // operator names in the card row.  This prevents ordinary bounty
        // choices from being routed through the elite selector.
        bool inferred_promotion_window = false;
        if (m_pending_promotion.empty() && m_pending_pact_recruitment.empty() &&
            (state.get("page", std::string()) == "bounty_selection" ||
             state.get("page", std::string()) == "preparation")) {
            std::set<std::string> card_names;
            std::set<int> possible_free_slots;
            if (const auto state_texts = state.find<json::array>("texts")) {
                for (const auto& item : *state_texts) {
                    const auto name = item.get("text", std::string());
                    const int x = item.get("x", -1);
                    const int y = item.get("y", -1);
                    if (item.get("score", 0.0) >= 0.8 && x >= 400 && x < 1120 && y >= 620 && y <= 700 &&
                        catalog.contains(name)) {
                        card_names.insert(name);
                    }
                    if (item.get("score", 0.0) >= 0.5 && (name == "0" || name == "O") && y >= 500 && y < 545) {
                        if (x >= 680 && x < 760) {
                            possible_free_slots.insert(2);
                        }
                        if (x >= 840 && x < 920) {
                            possible_free_slots.insert(1);
                        }
                        if (x >= 1000 && x < 1080) {
                            possible_free_slots.insert(0);
                        }
                    }
                }
            }
            const bool reward_overlay = state.get("page", std::string()) == "bounty_selection";
            int free_slots = 0;
            // Most ordinary shops have no free cards. Reuse the observer's
            // texts as a cheap prefilter before running three numeric OCRs.
            if (card_names.size() >= 2 && (reward_overlay || possible_free_slots.size() >= 2)) {
                const auto promotion_image = ctrler()->get_image();
                for (int slot = 0; slot < 3; ++slot) {
                    if (spa::shop_number(promotion_image, Rect(1038 - slot * 162, 510, 24, 30)) == 0) {
                        ++free_slots;
                    }
                }
            }
            inferred_promotion_window =
                reward_overlay ? free_slots >= 2 && card_names.size() >= 2 : free_slots == 3 && card_names.size() == 3;
            if (inferred_promotion_window) {
                auto detected = basic_info_with_what("SPAPromotionWindowDetected");
                detected["details"] = json::object { { "source_known", false },
                                                     { "free_price_slots", free_slots },
                                                     { "card_count", static_cast<int>(card_names.size()) },
                                                     { "page", state.get("page", std::string()) },
                                                     { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, detected);
            }
        }
        if (!m_pending_promotion.empty() || !m_pending_pact_recruitment.empty() || m_pending_promotion_reward ||
            inferred_promotion_window) {
            if (handle_promotion_page()) {
                m_promotion_retry_count = 0;
                continue;
            }
            ++m_promotion_retry_count;
            if (m_promotion_retry_count >= 8) {
                if (!m_pending_pact_recruitment.empty()) {
                    // The item contact may already be committed. Preserve the
                    // pending transaction and stop without another use, shop
                    // refresh, guessed acquisition, or abandonment.
                    spa::save_evidence(ctrler()->get_image(), utils::path("debug/SPA/pact_recruitment_pending"), false);
                    LogError << "SPA pact recruitment selection remains unverified" << m_pending_pact_recruitment;
                    if (lifecycle) {
                        lifecycle->note_input_reconciliation("SPAPactRecruitmentDeferred");
                    }
                    return false;
                }
                // Consumed copies cannot be retained as live field poses.
                // Keep the transaction pending rather than clearing it and
                // letting purchases/deployments/battles use the stale roster.
                auto deferred = basic_info_with_what("SPAPromotionDeferred");
                deferred["details"] = json::object { { "source", m_pending_promotion },
                                                     { "retry_count", m_promotion_retry_count },
                                                     { "page", state.get("page", std::string()) },
                                                     { "requires_roster_resync", true },
                                                     { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, deferred);
                if (lifecycle) {
                    lifecycle->note_input_reconciliation("SPAPromotionDeferred");
                }
                spa::save_evidence(ctrler()->get_image(), utils::path("debug/SPA/promotion_pending"), false);
                return false;
            }
            auto promotion_wait = basic_info_with_what("SPAAutomationPromotionRetry");
            promotion_wait["details"] = json::object { { "source", m_pending_promotion },
                                                       { "page", state.get("page", std::string()) },
                                                       { "run_victory_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, promotion_wait);
            if (!sleep(400)) {
                return false;
            }
            continue;
        }
        // A resumed invocation can attach while the previous battle's bounty
        // or free-supply page is still open.  These pages intentionally have
        // no preparation funds/round counters; handle them before the normal
        // preparation gate instead of waiting forever on an impossible OCR
        // state.
        const auto page_name = state.get("page", std::string());
        m_lifecycle_battle_seen = m_lifecycle_battle_seen || page_name == "battle";
        if (page_name == "authentication_expired" || page_name == "data_updated") {
            auto recovery = basic_info_with_what("SPASessionRecoveryRequired");
            recovery["details"] = json::object { { "run_id", m_run_id },
                                                 { "round", m_policy.round },
                                                 { "roster", roster_snapshot() },
                                                 { "bench_occupied", m_policy.bench_occupied },
                                                 { "phase", page_name },
                                                 { "pending_replacement", pending_replacement_snapshot() },
                                                 { "used_deploy_points", deployment_point_snapshot() },
                                                 { "field_positions", field_position_snapshot() },
                                                 { "combat_result", "unverified" },
                                                 { "resumable", true },
                                                 { "run_victory_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, recovery);
            return true;
        }
        if (page_name == "result_summary") {
            return true; // The lifecycle owns the transition to the team result.
        }
        if (page_name == "result" || page_name == "lobby") {
            // Settlement is a terminal checkpoint for this native invocation.
            // The outer phase-one runner performs the separate “返回主页” audit
            // (and can resume/reconnect before that audit).  Treating result as
            // a generic non-preparation page used to spend the action budget
            // on 300 ms stage waits after the run had already ended.
            auto terminal = basic_info_with_what("SPAAutomationResultObserved");
            terminal["details"] =
                json::object { { "page", page_name },
                               { "passed_rounds", state.get("passed_rounds", 0) },
                               { "passed_rounds_known", state.get("passed_rounds_known", false) },
                               { "result_difficulty", state.get("result_difficulty", 0) },
                               { "result_difficulty_known", state.get("result_difficulty_known", false) },
                               { "result_joint", state.get("result_joint", false) },
                               { "result_difficulty_name", state.get("result_difficulty_name", std::string()) },
                               { "result_text_verified", state.get("result_text_verified", false) },
                               { "result_interrupted", state.get("result_interrupted", false) },
                               { "result_completed", state.get("result_completed", false) },
                               { "run_victory_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, terminal);
            return true;
        }
        if (page_name == "bounty_selection" || page_name == "equipment_supply" || page_name == "tactical_decision") {
            const int after_round = std::max(m_policy.round, m_resume_after_round);
            if (handle_reward_page(after_round)) {
                m_resume_after_round = after_round + 1;
                continue;
            }
            auto reward_wait = basic_info_with_what("SPAAutomationRewardRetry");
            reward_wait["details"] = json::object { { "after_round", after_round },
                                                    { "choice_kind", page_name },
                                                    { "run_victory_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, reward_wait);
            if (!sleep(400)) {
                return false;
            }
            continue;
        }
        if (state.get("page", std::string()) != "preparation" || !state.get("funds_known", false) ||
            !state.get("round_known", false) || state.get("contradictory", true)) {
            auto recovery = basic_info_with_what("SPAAutomationStageWait");
            recovery["details"] = json::object { { "page", state.get("page", std::string()) },
                                                 { "funds_known", state.get("funds_known", false) },
                                                 { "round_known", state.get("round_known", false) },
                                                 { "contradictory", state.get("contradictory", true) },
                                                 { "run_victory_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, recovery);
            if (!sleep(300)) {
                return false;
            }
            continue;
        }
        if (m_mode == "joint" && state.get("preparation_seconds_remaining_known", false) &&
            state.get("preparation_seconds_remaining", 0) <= 8) {
            // Stop new transactions, not cancellation or observation of an
            // already issued spatial transaction. Never fall through to new
            // preparation actions even when this reconciliation succeeds.
            if (m_pending_spatial_orientation) {
                static_cast<void>(handle_spatial_reorientation(state));
            }
            auto deadline = basic_info_with_what("SPAJointPreparationDeadline");
            deadline["details"] = json::object { { "round", state.get("round", -1) },
                                                 { "seconds_remaining", state.get("preparation_seconds_remaining", 0) },
                                                 { "funds", state.get("funds", -1) },
                                                 { "new_transaction_issued", false },
                                                 { "funds_carryover_assumed", false } };
            callback(AsstMsg::SubTaskExtraInfo, deadline);
            if (!sleep(400)) {
                return false;
            }
            continue;
        }
        if (m_bench_recount_pending && state.get("remaining_deployment_known", false) &&
            !state.get("remaining_deployment_inferred", false) && m_pending_promotion.empty() &&
            m_pending_pact_recruitment.empty() && !m_pending_field_replacement && !m_pending_spatial_orientation &&
            m_unreconciled_sale.empty()) {
            const int remaining = state.get("remaining_deployment", -1);
            if (const auto bench = spa::bench_population_from_roster(m_policy.owned, 8 - remaining)) {
                m_policy.bench_occupied = *bench;
                m_bench_recount_pending = false;
                auto reconciled = basic_info_with_what("SPABenchPopulationReconciled");
                reconciled["details"] =
                    json::object { { "run_id", m_run_id },
                                   { "round", state.get("round", -1) },
                                   { "roster", roster_snapshot() },
                                   { "bench_occupied", *bench },
                                   { "bench_recount_pending", false },
                                   { "remaining_deployment", remaining },
                                   { "quantity_source", "reconciled_roster_and_fresh_field_counter" },
                                   { "inventory_complete", false },
                                   { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, reconciled);
            }
        }
        const int preparation_round = state.get("round", -1);
        if (m_replacement_round != preparation_round) {
            m_replacement_round = preparation_round;
            m_failed_replacements.clear();
            m_visited_replacement_lineups.clear();
            m_unavailable_replacement_sources.clear();
        }
        const auto texts = state.find<json::array>("texts");
        bool difficulty_verified = false;
        std::vector<spa::ShopOffer> offers;
        std::vector<spa::EquipmentProfile> equipment_offers;
        std::map<std::string, int> equipment_slots;
        auto image = ctrler()->get_image();
        if (!texts) {
            auto recovery = basic_info_with_what("SPAAutomationObservationRetry");
            recovery["details"] = json::object { { "reason", "Preparation OCR text list was not returned" },
                                                 { "run_victory_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, recovery);
            if (!sleep(500)) {
                return false;
            }
            continue;
        }
        json::array shop_scan;
        bool bench_capacity_full = false;
        for (const auto& text : *texts) {
            const auto name = text.get("text", std::string());
            const int x = text.get("x", -1);
            const int y = text.get("y", -1);
            if (m_blocked_purchases.contains({ state.get("round", -1), name }) ||
                (m_pending_direct_promotion && m_pending_direct_promotion->get("operator", std::string()) == name)) {
                continue;
            }
            if (text.get("score", 0.0) < 0.85) {
                continue;
            }
            if (spa::bench_capacity_warning({ name, text.get("score", 0.0), x, y })) {
                bench_capacity_full = true;
            }
            if (x < 300 && y < 70) {
                std::smatch level;
                if (m_mode == "quick" && name == "极速模拟") {
                    // Quick simulation has a named HUD, not a numeric rule
                    // level. It still uses the same preparation executor.
                    m_difficulty = spa::KeepDifficulty;
                    difficulty_verified = true;
                }
                else if (
                    m_mode == "independent" && std::regex_search(name, level, std::regex("规程等级([0-9]{1,2})"))) {
                    const auto digits = level[1].str();
                    const int actual = digits.size() == 1 ? digits[0] - '0' : (digits[0] - '0') * 10 + digits[1] - '0';
                    // A resumed game owns its existing difficulty; configuration
                    // is only a request for the next new simulation.
                    if (actual >= 1 && actual <= 20) {
                        m_difficulty = actual;
                        difficulty_verified = true;
                    }
                }
                else if (m_mode == "joint") {
                    const std::vector<std::string> names { "标准模拟", "险境模拟", "绝境模拟", "终极模拟" };
                    for (int index = 0; index < 4; ++index) {
                        if (name.find(names[index]) != std::string::npos) {
                            m_difficulty = index + 1;
                            difficulty_verified = true;
                        }
                    }
                }
            }
        }
        if (!difficulty_verified) {
            auto recovery = basic_info_with_what("SPAAutomationStageWait");
            recovery["details"] = json::object { { "reason", "Current difficulty label is temporarily unreadable" },
                                                 { "difficulty", m_difficulty },
                                                 { "run_victory_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, recovery);
            if (!sleep(500)) {
                return false;
            }
            continue;
        }

        if (state.get("round_known", false) && state.get("round", -1) >= 1 && state.get("round", -1) <= 20) {
            m_policy.round = state.get("round", m_policy.round);
        }
        observe_delayed_cash_equipment(state);
        if (m_auto_run && identify_current_strategy(state)) {
            continue;
        }
        if (m_pending_spatial_orientation && handle_spatial_reorientation(state)) {
            continue;
        }
        m_policy.observed_pact_layers.clear();
        if (const auto values = state.find<json::object>("pact_layers")) {
            for (const auto& [pact, value] : *values) {
                if (value.is_number() && value.as_integer() >= 0 && value.as_integer() <= 9999) {
                    m_policy.observed_pact_layers.emplace(pact, value.as_integer());
                }
            }
        }
        // Keep the shop stable during purchases. Field discovery and lineup
        // changes are batched after shopping, except when capacity or cash
        // prevents another transaction. An issued replacement is reconciled
        // immediately; it must never be stranded behind this scheduling gate.
        const bool confirmed_full_bench =
            bench_capacity_full || (!m_bench_recount_pending && m_policy.bench_occupied == 10);
        const int available_funds = state.get("funds", -1);
        const int free_field_slots = state.get("remaining_deployment", -1);
        const bool equipment_capacity_blocked = m_equipment_capacity_blocked_round == state.get("round", 0);
        const bool field_finalization = m_shopping_complete || available_funds == 0;
        const bool field_work_due =
            field_finalization || confirmed_full_bench || equipment_capacity_blocked ||
            (m_pending_direct_promotion && !m_direct_promotion_observed_rounds.contains(state.get("round", -1)) &&
             (m_pending_direct_promotion->get("effect_timing", std::string()) == "on_equip" ||
              (m_pending_direct_promotion->get("effect_timing", std::string()) == "next_round_start" &&
               state.get("round", -1) > m_pending_direct_promotion->get("contact_round", -1))));
        auto scheduling = basic_info_with_what("SPAPreparationSchedulingObserved");
        scheduling["details"] = json::object {
            { "run_id", m_run_id },
            { "round", state.get("round", -1) },
            { "funds", available_funds },
            { "phase",
              m_pending_field_replacement  ? "reconcile_field_transaction"
              : field_finalization         ? "finalize_formation"
              : confirmed_full_bench       ? "make_bench_space"
              : equipment_capacity_blocked ? "make_equipment_space"
                                           : "shop" },
            { "free_field_slots", free_field_slots },
            { "free_bench_slots",
              !m_bench_recount_pending && m_policy.bench_occupied >= 0 ? 10 - m_policy.bench_occupied : -1 },
            { "shopping_complete", m_shopping_complete },
            { "bench_capacity_full", confirmed_full_bench },
            { "bench_capacity_source", bench_capacity_full ? "live_warning" : "reconciled_roster" },
            { "equipment_capacity_known", equipment_capacity_blocked },
            { "equipment_capacity_full", equipment_capacity_blocked ? json::value(true) : json::value(nullptr) },
            { "deadline_emergency", nullptr },
            { "preparation_seconds_remaining_known", state.get("preparation_seconds_remaining_known", false) },
            { "field_work_due", field_work_due },
            { "replacement_reconciliation_pending", m_pending_field_replacement.has_value() }
        };
        if (!bench_capacity_full && (m_bench_recount_pending || m_policy.bench_occupied < 0)) {
            scheduling["details"]["bench_capacity_full"] = nullptr;
            scheduling["details"]["bench_capacity_source"] = "unknown";
        }
        if (m_mode != "joint") {
            scheduling["details"]["deadline_emergency"] = false;
        }
        else if (state.get("preparation_seconds_remaining_known", false)) {
            const int remaining = state.get("preparation_seconds_remaining", -1);
            scheduling["details"]["preparation_seconds_remaining"] = remaining;
            scheduling["details"]["deadline_emergency"] = remaining <= 8;
        }
        const bool time_for_board_maintenance = m_mode != "joint" ||
                                                !state.get("preparation_seconds_remaining_known", false) ||
                                                state.get("preparation_seconds_remaining", 0) > 25;
        scheduling["details"]["time_for_board_maintenance"] = time_for_board_maintenance;
        callback(AsstMsg::SubTaskExtraInfo, scheduling);
        const bool boss_preparation =
            preparation_round > 0 && ((m_spatial_boss_phase && m_spatial_boss_phase_round == preparation_round) ||
                                      (m_expected_boss_round > 0 && preparation_round >= m_expected_boss_round));
        if (m_auto_run && field_work_due && time_for_board_maintenance) {
            refresh_spatial_grid_snapshot(
                state,
                (boss_preparation && m_boss_attackable_round != preparation_round) ||
                    (m_pending_direct_promotion && m_spatial_snapshot_round != preparation_round));
        }
        if (m_auto_run && field_work_due && reconcile_direct_promotion_equipment(state)) {
            continue;
        }
        if (m_auto_run && ((field_work_due && time_for_board_maintenance) || m_pending_field_replacement) &&
            handle_full_field_replacement(state)) {
            continue;
        }
        if (field_work_due && refresh_equipment_inventory(state)) {
            continue;
        }
        if (m_auto_run && handle_cash_equipment(state)) {
            continue;
        }
        if (m_auto_run && handle_delayed_cash_equipment(state)) {
            continue;
        }
        if (m_auto_run && handle_pact_recruitment_equipment(state)) {
            continue;
        }
        if (m_auto_run && field_work_due && handle_direct_promotion_equipment(state)) {
            continue;
        }
        if (m_auto_run && handle_persistent_equipment(state)) {
            continue;
        }
        if (!m_unreconciled_sale.empty()) {
            m_blocked_purchases.emplace(state.get("round", -1), m_unreconciled_sale);
        }
        if (m_auto_run) {
            auto coverage = basic_info_with_what("SPAAutomationDeploymentCoverage");
            coverage["details"] =
                json::object { { "round", state.get("round", -1) },
                               { "bench_occupied", m_policy.bench_occupied },
                               { "remaining_deployment", state.get("remaining_deployment", -1) },
                               { "deployment_slots_available", state.get("remaining_deployment", 0) > 0 },
                               { "attempt_required",
                                 m_policy.bench_occupied > 0 && state.get("remaining_deployment", 0) > 0 },
                               { "acceptance_observation_only", true },
                               { "battle_blocked_by_metric", false },
                               { "run_victory_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, coverage);
        }

        // Optional autonomous deployment mode.  The normal preparation frame
        // exposes blue/cyan cell markers before a drag.  Turn the first
        // unoccupied marker into a guarded target for the highest-priority
        // undeployed owned unit; SwapTaskPlugin still requires the live green
        // hint, placement page, target identity, facing marker and population
        // decrement.  A failed candidate is consumed for this observation so
        // the next action can try another visible cell instead of replaying a
        // possibly committed drag.
        if (m_auto_deploy && !m_pending_spatial_orientation && !image.empty()) {
            bool operator_panel = false;
            if (m_policy.basic_mode && texts) {
                // The observer already scanned this exact preparation frame;
                // reuse its text list instead of running a second full OCR.
                bool has_range = false;
                bool has_sale = false;
                for (const auto& text : *texts) {
                    if (text.get("score", 0.0) < 0.85) {
                        continue;
                    }
                    const auto value = text.get("text", std::string());
                    has_range = has_range || value == "攻击范围";
                    has_sale = has_sale || value == "出售";
                }
                operator_panel = has_range && has_sale;
            }
            else {
                OCRer panel_ocr(image);
                if (const auto recognized = panel_ocr.analyze()) {
                    bool has_range = false;
                    bool has_sale = false;
                    for (const auto& text : *recognized) {
                        if (text.score < 0.85) {
                            continue;
                        }
                        has_range = has_range || text.text == "攻击范围";
                        has_sale = has_sale || text.text == "出售";
                    }
                    operator_panel = has_range && has_sale;
                }
            }
            if (operator_panel) {
                // This click only dismisses the detail overlay.  Placement is
                // always held-drag based; a map click is never used as a
                // deployment action.
                if (!ctrler()->click(Point(1040, 80)) || !sleep(500)) {
                    return false;
                }
                image = ctrler()->get_image();
            }
        }
        // A non-consuming drag only invalidates the selected card/target.
        // Keep scanning the rest of the bench in this same preparation phase;
        // suppressing the entire round left a full bench beside unused
        // deployment slots and eventually caused avoidable life loss.
        m_policy.selected_strategy_id = m_selected_strategy_id;
        const std::string opening_gift = m_selected_strategy_id == "band_mech" ? "“暴躁铁皮”" : "";
        // A strategy gift is a discovery hint, not a fabricated owned copy.
        // The named deployment must still verify its live bench identity,
        // destination, facing and population before it enters the roster.
        // A reconnect can miss the opening preparation. The physical gift
        // remains on the bench; do not discard its discovery hint at round 3.
        const bool opening_gift_pending = !opening_gift.empty() && !m_policy.owned.contains(opening_gift) &&
                                          !m_auto_deploy_blocked_names.contains(opening_gift);
        if (m_auto_deploy && !m_pending_spatial_orientation && !m_pending_field_replacement &&
            state.get("remaining_deployment_known", false) && state.get("remaining_deployment", 0) > 0 &&
            m_deployment_targets.empty() && field_work_due) {
            const auto deployable = [&](const std::string& name) {
                return !m_auto_deploy_blocked_names.contains(name) && spa::bench_operator_can_deploy(m_policy, name);
            };
            std::optional<std::string> deploy_name;
            if (opening_gift_pending) {
                deploy_name = opening_gift;
            }
            if (!deploy_name && m_policy.primary_pact == "阿戈尔") {
                // A legacy list of possible carries must not leave an actual
                // terminal on the bench while another transition unit fills
                // its slot. The route orders terminal, current output, relay.
                const auto route = spa::route_profiles().find("阿戈尔");
                if (route != spa::route_profiles().end()) {
                    for (const auto& name : route->second.output_candidates) {
                        if (deployable(name)) {
                            deploy_name = name;
                            break;
                        }
                    }
                }
            }
            for (const auto& name : m_policy.carries) {
                if (deploy_name) {
                    break;
                }
                if (deployable(name)) {
                    deploy_name = name;
                    break;
                }
            }
            if (!deploy_name) {
                const auto route = spa::route_profiles().find(m_policy.primary_pact);
                if (route != spa::route_profiles().end()) {
                    for (const auto& name : route->second.output_candidates) {
                        if (deployable(name)) {
                            deploy_name = name;
                            break;
                        }
                    }
                    if (!deploy_name) {
                        for (const auto& name : route->second.growth_engines) {
                            if (deployable(name)) {
                                deploy_name = name;
                                break;
                            }
                        }
                    }
                }
            }
            if (!deploy_name) {
                for (const auto& entry : m_policy.owned) {
                    if (deployable(entry.first)) {
                        deploy_name = entry.first;
                        break;
                    }
                }
            }
            if (!deploy_name && m_policy.basic_mode && m_policy.bench_occupied > 0 &&
                !m_auto_deploy_blocked_names.contains("*")) {
                deploy_name = "*";
            }
            auto candidates = spa::detect_deployment_cells(image);
            // In phase one the held-card map exposes a stable input lattice
            // even when the blue-cell contour is hidden by the selected
            // operator panel.  Prefer the next unused lattice point as a
            // guarded candidate; the SwapTaskPlugin still validates the
            // green preview, direction and population receipt.  This keeps
            // later bench cards deployable instead of treating the same
            // stale blue markers as an exhausted map.
            if (m_policy.basic_mode && deploy_name) {
                const std::array<std::pair<int, int>, 8> safe_inputs { std::pair { 680, 360 }, std::pair { 760, 360 },
                                                                       std::pair { 840, 360 }, std::pair { 920, 360 },
                                                                       std::pair { 680, 440 }, std::pair { 760, 440 },
                                                                       std::pair { 840, 440 }, std::pair { 920, 440 } };
                for (const auto& [safe_x, safe_y] : safe_inputs) {
                    if (!m_auto_deploy_used_cells.contains(std::pair { safe_x, safe_y })) {
                        candidates.insert(
                            candidates.begin(),
                            spa::DeploymentCellCandidate { cv::Point(safe_x, safe_y), 1.0 });
                        break;
                    }
                }
            }
            if (deploy_name && !candidates.empty()) {
                const auto unoccupied = std::count_if(candidates.begin(), candidates.end(), [&](const auto& candidate) {
                    return !spa::occupied_deployment_cell(image, candidate.center);
                });
                if (unoccupied > 0) {
                    candidates.erase(
                        std::remove_if(
                            candidates.begin(),
                            candidates.end(),
                            [&](const auto& candidate) {
                                return spa::occupied_deployment_cell(image, candidate.center);
                            }),
                        candidates.end());
                }
                // These points only start the held preview. Final placement
                // uses its registered cells, actual ranges and spatial traits;
                // a fixed 80px offset is not a logical row across cameras.
                const char preferred_direction = m_auto_deploy_direction;
                for (const auto& candidate : candidates) {
                    const auto key = std::pair { candidate.center.x, candidate.center.y };
                    if (m_auto_deploy_used_cells.contains(key)) {
                        continue;
                    }
                    // detect_deployment_cells returns the screen point used
                    // by the held-card green preview.  It is already the
                    // drag target (the accepted B-server sample is
                    // (700,340)); adding an offset moved every automatic
                    // attempt to an illegal tile and caused the false
                    // deployment block seen in round 2.
                    const int drag_x = candidate.center.x;
                    const int drag_y = candidate.center.y;
                    if (drag_x < 180 || drag_x >= 1000 || drag_y < 140 || drag_y >= 620) {
                        continue;
                    }
                    json::object target {
                        { "name", *deploy_name },           { "direction", std::string(1, preferred_direction) },
                        { "target_x", candidate.center.x }, { "target_y", candidate.center.y },
                        { "drag_target_x", drag_x },        { "drag_target_y", drag_y },
                        { "auto_detected", true },          { "candidate_score", candidate.score }
                    };
                    m_deployment_targets.emplace(*deploy_name, std::move(target));
                    auto detected = basic_info_with_what("SPAAutomationDeploymentCandidateDetected");
                    detected["details"] = json::object { { "name", *deploy_name },
                                                         { "target_x", candidate.center.x },
                                                         { "target_y", candidate.center.y },
                                                         { "drag_target_x", drag_x },
                                                         { "drag_target_y", drag_y },
                                                         { "direction", std::string(1, preferred_direction) },
                                                         { "candidate_score", candidate.score },
                                                         { "green_hint_verified", false },
                                                         { "placement_authorized", false },
                                                         { "run_victory_verified", false } };
                    callback(AsstMsg::SubTaskExtraInfo, detected);
                    break;
                }
            }
            if (m_deployment_targets.empty() && deploy_name) {
                auto blocked = basic_info_with_what("SPAAutomationDeploymentBlocked");
                blocked["details"] =
                    json::object { { "reason",
                                     candidates.empty() ? "No blue deployment-cell candidate was detected"
                                                        : "All detected deployment cells were already tried" },
                                   { "name", *deploy_name },
                                   { "remaining_deployment", state.get("remaining_deployment", -1) },
                                   { "placement_authorized", false },
                                   { "battle_authorized", false },
                                   { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, blocked);
                if (!m_policy.basic_mode) {
                    return true;
                }
                // A failed target for this operator must not advance solo
                // combat ahead of another eligible bench candidate.
                m_auto_deploy_blocked_names.insert(*deploy_name);
                auto deferred = basic_info_with_what("SPAAutomationDeploymentDeferred");
                deferred["details"] =
                    json::object { { "name", *deploy_name },
                                   { "round", state.get("round", -1) },
                                   { "reason", "No additional verified deployment cell for this operator" },
                                   { "deployed", false },
                                   { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, deferred);
                if (m_mode != "joint") {
                    m_cached_deployment_observation = state_storage;
                    continue;
                }
            }
        }
        std::vector<std::string> deployment_names;
        for (const auto& [name, target] : m_deployment_targets) {
            deployment_names.emplace_back(name);
        }
        std::optional<std::string> pending;
        if (!m_pending_spatial_orientation && !m_pending_field_replacement &&
            state.get("remaining_deployment_known", false)) {
            pending = spa::choose_pending_deployment(m_policy, state.get("remaining_deployment", -1), deployment_names);
        }
        if (pending) {
            if (m_mode == "joint") {
                const auto remaining = m_joint_preparation_deadline.observe(
                    state.get("round", -1),
                    std::nullopt,
                    spa::PreparationDeadline::Clock::now());
                if (!spa::deployment_fits_preparation(remaining, *pending == "*")) {
                    auto deferred = basic_info_with_what("SPAJointDeploymentDeadline");
                    deferred["details"] = json::object { { "name", *pending },
                                                         { "round", state.get("round", -1) },
                                                         { "seconds_remaining", remaining.value_or(-1) },
                                                         { "new_deployment_issued", false },
                                                         { "bench_preserved", true },
                                                         { "other_preparation_actions_allowed", true } };
                    callback(AsstMsg::SubTaskExtraInfo, deferred);
                    pending.reset();
                }
            }
        }
        if (pending) {
            auto info = basic_info_with_what("SPAAutomationDeploymentDecision");
            info["details"] = json::object { { "name", *pending },
                                             { "decision_only", m_decision_only },
                                             { "round", state.get("round", -1) },
                                             { "run_id", m_run_id } };
            callback(AsstMsg::SubTaskExtraInfo, info);
            if (m_decision_only) {
                return true;
            }
            auto options = m_deployment_targets.at(*pending);
            const bool auto_target = options.get("auto_detected", false);
            const auto auto_cell = std::pair { options.get("target_x", -1), options.get("target_y", -1) };
            options["deploy_only"] = true;
            options["find_bench_source"] = true;
            options["recent_acquisition_source"] = m_recent_bench_acquisition == *pending;
            // Phase one prioritizes a complete, bounded round loop.  The
            // drag, direction commit, page restoration and population state
            // remain authoritative; expensive map-identity reconciliation is
            // reserved for the dynamic phase.
            options["best_effort_deployment"] = m_policy.basic_mode;
            options["fast_mode"] = m_policy.basic_mode;
            bool safe_target_available = true;
            Point safe_target(680, 360);
            if (m_policy.basic_mode) {
                // The held-drag input lattice point (680,360) lands on the
                // rendered middle tile around (600,280) after the map camera
                // projection.  A resumed task has no in-memory used-cell set,
                // so inspect that rendered tile before reusing the safe input.
                // The selected-card camera has a different projection from
                // the settled preparation frame.  Use the run-local receipt
                // set as the authoritative occupancy map for phase one;
                // probing the settled pixels here marked every legal tile as
                // occupied on maps with orange props and prevented the next
                // bench card from being considered.
                const std::array<std::pair<int, int>, 8> safe_inputs { std::pair { 680, 360 }, std::pair { 760, 360 },
                                                                       std::pair { 840, 360 }, std::pair { 920, 360 },
                                                                       std::pair { 680, 440 }, std::pair { 760, 440 },
                                                                       std::pair { 840, 440 }, std::pair { 920, 440 } };
                safe_target_available = false;
                for (const auto& [safe_x, safe_y] : safe_inputs) {
                    if (!m_auto_deploy_used_cells.contains(std::pair { safe_x, safe_y })) {
                        safe_target = Point(safe_x, safe_y);
                        safe_target_available = true;
                        break;
                    }
                }
            }
            options["prefer_safe_target"] =
                m_policy.basic_mode && safe_target_available &&
                !m_auto_deploy_used_cells.contains(std::pair<int, int> { safe_target.x, safe_target.y });
            options["safe_target_x"] = safe_target.x;
            options["safe_target_y"] = safe_target.y;
            options["excluded_deploy_points"] = deployment_point_snapshot();
            options["role_aware_deployment"] = true;
            options["primary_pact"] = m_policy.primary_pact;
            options["primary_active"] = m_policy.primary_locked_after_activation;
            std::vector<spa::RosterUnit> placement_roster;
            for (const auto& [name, owned] : m_policy.owned) {
                if (owned.copies > 0) {
                    placement_roster.push_back(
                        { name, owned.deployed || name == *pending, field_added_pacts(name), owned.elite });
                }
            }
            const auto placement_formation = spa::evaluate_formation(
                placement_roster,
                spa::operator_pacts(),
                spa::pact_requirements(),
                spa::enabled_formation_pacts(m_enabled_core_pacts, m_policy.disabled_pacts));
            options["active_placement_pacts"] = placement_formation.active_pacts;
            json::array placement_units;
            for (const auto& [name, point] : m_auto_deploy_positions) {
                const auto owned = m_policy.owned.find(name);
                if (owned != m_policy.owned.end() && owned->second.deployed) {
                    placement_units.emplace_back(
                        json::object { { "name", name },
                                       { "x", point.first },
                                       { "y", point.second },
                                       { "committed_facing",
                                         m_committed_facings.contains(point)
                                             ? std::string(1, m_committed_facings.at(point))
                                             : std::string() },
                                       { "field_pose_verified", m_verified_field_pose_points.contains(point) } });
                }
            }
            options["placement_units"] = std::move(placement_units);
            // The policy roster can lag behind the live shop after a resumed
            // run.  In phase one, let the bench pixel/identity scan choose
            // the actual occupied card and feed that identity back below;
            // dynamic planning still keeps the requested name for priority.
            if (m_policy.basic_mode && !m_policy.dynamic_decision_mode) {
                options["name"] = "*";
            }
            if (options.get("name", std::string()) != "*" && m_policy.owned.contains(*pending)) {
                const auto& owned = m_policy.owned.at(*pending);
                const auto forms = spa::owned_form_counts(owned);
                if (!forms) {
                    m_auto_deploy_blocked_names.insert(*pending);
                    continue;
                }
                options["target_elite"] = owned.elite;
                options["verify_source_form"] = forms->initial > 0 && forms->elite > 0;
            }
            options["round"] = state.get("round", -1);
            options["funds"] = state.get("funds", -1);
            options["remaining_deployment"] = state.get("remaining_deployment", -1);
            spa::SwapTaskPlugin deploy(m_callback, m_inst, TaskType);
            deploy.use_bench_identity_hints(m_inventory_operator_hints);
            deploy.set_retry_times(0);
            deploy.set_task_id(m_task_id);
            deploy.set_task_ptr(this);
            const bool configured = deploy.configure(options);
            if (configured && !m_field_checkpoint_loaded && m_growth_population_poses == m_auto_deploy_positions &&
                m_population_grid_members.size() == m_auto_deploy_positions.size()) {
                deploy.set_registered_grid_context(
                    m_spatial_grid_reference,
                    m_spatial_grid_projection,
                    m_spatial_map_id);
            }
            if (!configured || !deploy.run()) {
                const auto rejected = deploy.rejected_drop_target();
                if (auto_target) {
                    // A failed guarded drag does not occupy the cell. Keep it
                    // eligible for the next fresh map scan; only successful
                    // placements are recorded in m_auto_deploy_used_cells.
                    m_deployment_targets.erase(*pending);
                    // A cancelled direction preview proved that this green
                    // input point hits an occupied tile. Exclude that input,
                    // then retry the unchanged bench card at another cell.
                    if (rejected) {
                        m_auto_deploy_used_cells.emplace(rejected->x, rejected->y);
                    }
                }
                if (m_policy.basic_mode && !rejected) {
                    // A failed identity/green-cell/direction reconciliation
                    // must not reopen the same bench card forever.  Defer it
                    // for this run checkpoint and let the fixed shop/round
                    // policy continue with other work.
                    m_auto_deploy_blocked_names.insert(*pending);
                    auto deferred = basic_info_with_what("SPAAutomationDeploymentDeferred");
                    deferred["details"] = json::object {
                        { "name", *pending },
                        { "round", state.get("round", -1) },
                        { "reason",
                          "One guarded deployment attempt failed; skip this operator for the current run checkpoint" },
                        { "deployed", false },
                        { "repeat_suppressed", true },
                        { "run_victory_verified", false }
                    };
                    callback(AsstMsg::SubTaskExtraInfo, deferred);
                }
                auto recovery = basic_info_with_what("SPAAutomationDeploymentRetry");
                recovery["details"] = json::object { { "name", *pending },
                                                     { "round", state.get("round", -1) },
                                                     { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, recovery);
                if (!sleep(500)) {
                    return false;
                }
                continue;
            }
            // A verified swap is a committed board change. Reconcile it below
            // and consume its target from the probe set, while leaving the
            // bench count unchanged. Suppressing the card before recording
            // that target made every resumed drag reuse the occupied first tile.
            const auto actual_name = deploy.selected_name().empty() ? *pending : deploy.selected_name();
            if (deploy.remaining_deployment() >= 0 && deploy.remaining_deployment() <= 8) {
                m_last_remaining_deployment = deploy.remaining_deployment();
            }
            if (*pending != "*" && actual_name == *pending) {
                auto pending_unit = m_policy.owned.find(*pending);
                if (pending_unit != m_policy.owned.end()) {
                    pending_unit->second.deployed = true;
                }
            }
            const auto displaced_name = deploy.displaced_name();
            if (!displaced_name.empty() && displaced_name != actual_name) {
                auto displaced = m_policy.owned.find(displaced_name);
                if (displaced == m_policy.owned.end() && spa::operator_pacts().contains(displaced_name)) {
                    m_policy.owned.emplace(displaced_name, spa::OwnedOperator { 1, false, false });
                }
                else if (displaced != m_policy.owned.end()) {
                    displaced->second.deployed = false;
                }
                else {
                    // OCR can read the displaced sprite as a non-catalog
                    // fragment. Keep it out of the planner roster so a
                    // hallucinated name cannot become the next deployment
                    // target on the following loop.
                    m_auto_deploy_blocked_names.insert(displaced_name);
                }
            }
            if (actual_name != *pending || !m_policy.owned.contains(actual_name)) {
                const bool fresh_strategy_gift = m_selected_strategy_id == "band_mech" && actual_name == "“暴躁铁皮”" &&
                                                 state.get("round", -1) == 1 && !m_policy.owned.contains(actual_name) &&
                                                 deploy.field_pose_verified();
                spa::OwnedOperator discovered { 1, false, true };
                if (const auto actual_unit = m_policy.owned.find(actual_name); actual_unit != m_policy.owned.end()) {
                    discovered = actual_unit->second;
                }
                discovered.deployed = true;
                if (spa::operator_pacts().contains(actual_name)) {
                    m_policy.owned[actual_name] = discovered;
                }
                if (fresh_strategy_gift && !m_equipped_items.contains(actual_name)) {
                    // A newly acquired opening gift has the same empty-item
                    // baseline as a fresh purchase. This is not an OCR claim:
                    // equipment use still re-reads and compares the real panel.
                    // Never seed this baseline from a later resumed discovery.
                    m_equipped_items.emplace(actual_name, std::vector<std::string> {});
                    auto baseline = basic_info_with_what("SPAStrategyGiftEquipmentBaseline");
                    baseline["details"] = json::object { { "name", actual_name },
                                                         { "round", 1 },
                                                         { "source", "opening_strategy_acquisition" },
                                                         { "equipment_panel_verified", false },
                                                         { "live_preflight_required", true } };
                    callback(AsstMsg::SubTaskExtraInfo, baseline);
                }
                if (*pending != "*" && actual_name != *pending) {
                    m_auto_deploy_blocked_names.insert(*pending);
                }
            }
            if (auto_target) {
                // SwapTask may retarget the held drag after the source panel
                // fades and the green map is reacquired. Record the actual
                // drag/settled point returned by the plugin; retaining the
                // preflight target makes the next bench card reuse an
                // occupied tile and silently turn deployment into a swap.
                const auto actual_drag = deploy.drag_target_point();
                const auto actual_target = deploy.target_point();
                const std::pair<int, int> actual_drag_cell { actual_drag.x, actual_drag.y };
                if (actual_drag.x >= 180 && actual_drag.x < 1000 && actual_drag.y >= 140 && actual_drag.y < 620) {
                    m_auto_deploy_used_cells.insert(actual_drag_cell);
                }
                else {
                    m_auto_deploy_used_cells.insert(auto_cell);
                }
                if (m_policy.basic_mode && options.get("prefer_safe_target", false) &&
                    !options.get("role_aware_deployment", false)) {
                    // The safe target is the input coordinate actually used
                    // by the held drag.  Record that coordinate, rather than
                    // hard-coding the first slot, so each successful receipt
                    // advances to the next free lattice cell.
                    m_auto_deploy_used_cells.insert(
                        std::pair<int, int> { options.get("safe_target_x", -1), options.get("safe_target_y", -1) });
                }
                m_deployment_targets.erase(*pending);
                const std::pair<int, int> actual_position = deploy.field_pose_verified()
                                                                ? std::pair { actual_target.x, actual_target.y }
                                                                : std::pair { actual_drag.x, actual_drag.y };
                const auto accepted_position = (actual_position.first >= 180 && actual_position.first < 1000 &&
                                                actual_position.second >= 140 && actual_position.second < 510)
                                                   ? actual_position
                                                   : auto_cell;
                // Update the occupied tile, not every instance with the same
                // name. Overwriting by name lost the first copy's lane and
                // made subsequent placement scores cluster around one lane.
                std::erase_if(m_auto_deploy_positions, [&](const auto& placed) {
                    return std::abs(placed.second.first - accepted_position.first) < 40 &&
                           std::abs(placed.second.second - accepted_position.second) < 40;
                });
                std::erase_if(m_verified_field_pose_points, [&](const auto& point) {
                    return std::abs(point.first - accepted_position.first) < 40 &&
                           std::abs(point.second - accepted_position.second) < 40;
                });
                m_auto_deploy_positions.emplace(actual_name, accepted_position);
                std::erase_if(m_committed_facings, [&](const auto& entry) {
                    return std::abs(entry.first.first - accepted_position.first) < 40 &&
                           std::abs(entry.first.second - accepted_position.second) < 40;
                });
                if (deploy.field_pose_verified()) {
                    m_verified_field_pose_points.insert(accepted_position);
                    if (const auto facing = deploy.committed_facing()) {
                        m_committed_facings[accepted_position] = *facing;
                    }
                }
                if (!displaced_name.empty()) {
                    if (auto displaced = m_policy.owned.find(displaced_name); displaced != m_policy.owned.end()) {
                        displaced->second.deployed = m_auto_deploy_positions.contains(displaced_name);
                    }
                }
            }
            if (deploy.population_consumed() && m_policy.bench_occupied > 0) {
                --m_policy.bench_occupied;
            }
            // A swap keeps the bench population unchanged.  The target cell
            // is consumed from m_auto_deploy_used_cells above, so the next
            // loop may try another candidate in the same preparation window.
            // This is necessary when the first geometric probe lands on an
            // already occupied tile: a swap is recoverable, not a reason to
            // stop deploying the remaining bench operators.
            m_growth_population_poses.clear();
            m_row_growth_populations.clear();
            m_population_grid_members.clear();
            const auto& grid_members = deploy.observed_grid_members();
            const int field_population = 8 - deploy.remaining_deployment();
            if (deploy.field_pose_verified() && field_population > 0 &&
                grid_members.size() == static_cast<size_t>(field_population) &&
                m_auto_deploy_positions.size() == grid_members.size() &&
                std::all_of(m_auto_deploy_positions.begin(), m_auto_deploy_positions.end(), [&](const auto& item) {
                    return m_verified_field_pose_points.contains(item.second);
                })) {
                std::multiset<std::string> pose_names, grid_names;
                for (const auto& [name, point] : m_auto_deploy_positions) {
                    pose_names.insert(name);
                }
                for (const auto& member : grid_members) {
                    grid_names.insert(member.name);
                }
                if (const auto populations = spa::same_row_growth_populations(grid_members);
                    populations && pose_names == grid_names) {
                    m_row_growth_populations = *populations;
                    m_population_grid_members = grid_members;
                    m_growth_population_poses = m_auto_deploy_positions;
                    if (!deploy.observed_grid_reference().empty() && !deploy.observed_grid_projection().empty()) {
                        m_spatial_grid_reference = deploy.observed_grid_reference().clone();
                        m_spatial_grid_projection = deploy.observed_grid_projection().clone();
                        if (!deploy.verified_map_id().empty()) {
                            m_spatial_map_id = deploy.verified_map_id();
                        }
                    }
                }
            }
            auto reconciled = basic_info_with_what("SPAAutomationDeploymentReconciled");
            const auto reconciled_drag = deploy.drag_target_point();
            const auto reconciled_target = deploy.target_point();
            reconciled["details"] = json::object { { "run_id", m_run_id },
                                                   { "name", actual_name },
                                                   { "round", state.get("round", -1) },
                                                   { "bench_occupied", m_policy.bench_occupied },
                                                   { "deployed", true },
                                                   { "population_consumed", deploy.population_consumed() },
                                                   { "displaced", displaced_name },
                                                   { "actual_drag_target_x", reconciled_drag.x },
                                                   { "actual_drag_target_y", reconciled_drag.y },
                                                   { "actual_target_x", reconciled_target.x },
                                                   { "actual_target_y", reconciled_target.y },
                                                   { "remaining_deployment", deploy.remaining_deployment() },
                                                   { "funds", state.get("funds", -1) },
                                                   { "roster", roster_snapshot() },
                                                   { "used_deploy_points", deployment_point_snapshot() },
                                                   { "field_positions", field_position_snapshot() },
                                                   { "row_population_snapshot_complete",
                                                     !m_growth_population_poses.empty() &&
                                                         m_growth_population_poses == m_auto_deploy_positions },
                                                   { "requires_live_revalidation", true },
                                                   { "run_victory_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, reconciled);
            if (m_stop_after_deployment) {
                return true;
            }
            if (m_last_coin_review_after_deployment_round == state.get("round", -1) && deploy.population_consumed() &&
                deploy.remaining_deployment() == 0 &&
                (m_mode != "joint" || m_joint_preparation_deadline
                                              .observe(
                                                  m_last_coin_review_after_deployment_round,
                                                  std::nullopt,
                                                  spa::PreparationDeadline::Clock::now())
                                              .value_or(0) >= 30)) {
                m_last_coin_review_after_deployment_round = -1;
                m_shopping_complete = false;
                m_spending_reviewed = false;
                m_spending_reason.clear();
                continue;
            }
            if (m_policy.basic_mode && m_policy.bench_occupied > 0 && state.get("remaining_deployment", 0) > 0) {
                auto burst = state_storage;
                burst["remaining_deployment"] = deploy.remaining_deployment();
                burst["remaining_deployment_known"] = true;
                burst["page"] = "preparation";
                burst["contradictory"] = false;
                m_cached_deployment_observation = std::move(burst);
            }
            continue;
        }
        const bool layout_due = !m_auto_run || m_shopping_complete || state.get("funds", -1) == 0 || m_movement_only ||
                                m_verify_layout_identities;
        if (!layout_due) {
            m_dynamic_layout_observed = false;
        }
        if ((action == 0 || m_auto_run) && layout_due) {
            if (m_movement_only) {
                if (!spa::review_deployment(m_layout_units, m_layout_lanes, m_deployable_cells).complete) {
                    return false;
                }
                size_t completed = 0;
                for (const auto& step : m_movement_plan) {
                    auto options = step;
                    options["move_only"] = true;
                    options["finalize_direction"] = true;
                    options["round"] = state.get("round", -1);
                    options["funds"] = state.get("funds", -1);
                    options["remaining_deployment"] = state.get("remaining_deployment", -1);
                    spa::SwapTaskPlugin move(m_callback, m_inst, TaskType);
                    move.set_retry_times(0);
                    move.set_task_id(m_task_id);
                    move.set_task_ptr(this);
                    if (!move.configure(options) || !move.run()) {
                        auto failed = basic_info_with_what("SPAAutomationMovementPlanStopped");
                        failed["details"] = json::object { { "completed_steps", static_cast<int>(completed) },
                                                           { "failed_unit", step.get("name", std::string()) },
                                                           { "rollback_verified", false } };
                        callback(AsstMsg::SubTaskExtraInfo, failed);
                        return false;
                    }
                    const auto name = step.get("name", std::string());
                    const spa::BoardCell destination { step.get("to_col", -100), step.get("to_row", -100) };
                    for (auto& unit : m_layout_units) {
                        if (unit.id == name) {
                            unit.cell = destination;
                            unit.facing = step.get("direction", std::string())[0];
                            unit.placement_cells.push_back(destination);
                        }
                    }
                    m_layout_screen_points[name] = { step.get("target_x", -1), step.get("target_y", -1) };
                    if (std::find(m_deployable_cells.begin(), m_deployable_cells.end(), destination) ==
                        m_deployable_cells.end()) {
                        m_deployable_cells.push_back(destination);
                    }
                    ++completed;
                    auto progress = basic_info_with_what("SPAAutomationMovementPlanProgress");
                    progress["details"] = json::object { { "completed_steps", static_cast<int>(completed) },
                                                         { "name", name },
                                                         { "target_identity_verified", true },
                                                         { "direction_verified", false } };
                    callback(AsstMsg::SubTaskExtraInfo, progress);
                }
            }
            if (m_verify_layout_identities) {
                if (m_layout_units.empty()) {
                    return false;
                }
                json::array verified;
                for (const auto& unit : m_layout_units) {
                    const auto [x, y] = m_layout_screen_points.at(unit.id);
                    spa::SwapTaskPlugin inspect(m_callback, m_inst, TaskType);
                    inspect.set_retry_times(0);
                    inspect.set_task_id(m_task_id);
                    inspect.set_task_ptr(this);
                    if (!inspect.configure(
                            json::object { { "inspect_only", true },
                                           { "name", unit.id },
                                           { "source_x", x },
                                           { "source_y", y },
                                           { "round", state.get("round", -1) },
                                           { "funds", state.get("funds", -1) },
                                           { "remaining_deployment", state.get("remaining_deployment", -1) } }) ||
                        !inspect.run()) {
                        return false;
                    }
                    verified.emplace_back(
                        json::object { { "name", unit.id },
                                       { "col", unit.cell.first },
                                       { "row", unit.cell.second },
                                       { "screen_x", x },
                                       { "screen_y", y } });
                }
                auto verification = basic_info_with_what("SPAAutomationLayoutIdentitiesVerified");
                verification["details"] = json::object { { "units", std::move(verified) },
                                                         { "screen_identities_verified", true },
                                                         { "grid_projection_verified", false },
                                                         { "directions_verified", false },
                                                         { "battle_authorized", false } };
                callback(AsstMsg::SubTaskExtraInfo, verification);
            }
            // Shop and field replacement decisions can still change this
            // lineup. Reorder it once spending is done, rather than moving
            // the same temporary roster after every individual purchase.
            // Pending spatial transactions are reconciled before this gate.
            if (m_auto_run && time_for_board_maintenance && (m_shopping_complete || state.get("funds", -1) == 0) &&
                handle_spatial_reorientation(state)) {
                continue;
            }
            if (boss_preparation && (m_shopping_complete || state.get("funds", -1) == 0)) {
                const auto region = spa::verified_boss_opening_region(
                    m_spatial_boss_phase && m_spatial_boss_phase_round == preparation_round,
                    m_spatial_map_id,
                    preparation_round,
                    m_boss_attackable_map_id,
                    m_boss_attackable_round,
                    m_boss_attackable_cells);
                const bool coverage_known = !region.empty() && m_dynamic_layout_observed &&
                                            m_growth_population_poses == m_auto_deploy_positions;
                auto objective = basic_info_with_what("SPABossPreparationObjectiveReviewed");
                objective["details"] =
                    json::object { { "round", preparation_round },
                                   { "boss_region_verified", !region.empty() },
                                   { "boss_attackers_known", coverage_known },
                                   { "boss_opening_attackers",
                                     coverage_known ? spa::stationary_boss_attackers(m_layout_units, region) : -1 },
                                   { "boss_opening_basic_attack_weight",
                                     coverage_known ? spa::stationary_boss_attack_weight(m_layout_units, region) : -1 },
                                   { "boss_defeat_verified", false },
                                   { "replacement_reconciliation_pending", m_pending_field_replacement.has_value() } };
                callback(AsstMsg::SubTaskExtraInfo, objective);
            }
            if ((m_shopping_complete || available_funds == 0) && handle_preparation_summons(state)) {
                continue;
            }
            if (m_auto_run && (m_shopping_complete || state.get("funds", -1) == 0) &&
                (handle_temporary_elite_instruction(state) || handle_fear_trap_instruction(state))) {
                continue;
            }
            const bool configured_layout = m_layout_supplied;
            const auto review = spa::review_deployment(m_layout_units, m_layout_lanes, m_deployable_cells);
            json::array lanes;
            for (const auto& lane : review.lanes) {
                json::array healing;
                json::array output;
                json::array problems;
                for (const auto& name : lane.healers) {
                    healing.emplace_back(name);
                }
                for (const auto& name : lane.damage_support) {
                    output.emplace_back(name);
                }
                for (const auto& problem : lane.problems) {
                    problems.emplace_back(problem);
                }
                lanes.emplace_back(
                    json::object { { "lane", lane.lane },
                                   { "blocker", lane.blocker },
                                   { "block_capacity", lane.block_capacity },
                                   { "healers", std::move(healing) },
                                   { "damage_support", std::move(output) },
                                   { "approach_cells_covered", lane.covered_approach_cells },
                                   { "approach_cells_required", lane.required_approach_cells },
                                   { "ready", lane.ready },
                                   { "problems", std::move(problems) } });
            }
            json::array problems;
            for (const auto& problem : review.problems) {
                problems.emplace_back(problem);
            }
            auto layout_info = basic_info_with_what("SPAAutomationLayoutReview");
            layout_info["details"] =
                json::object { { "complete", review.complete },
                               { "static_constraints_met", review.static_constraints_met },
                               { "unique_route_block_capacity", review.unique_route_block_capacity },
                               { "balanced_block_capacity", review.balanced_block_capacity },
                               { "configured_layout", configured_layout },
                               { "phase_one_game_layout", m_phase_one_game_layout },
                               { "lanes", std::move(lanes) },
                               { "problems", std::move(problems) },
                               { "pose_source",
                                 m_dynamic_layout_observed ? "registered_field_grid" : "configured_coordinates" },
                               { "vision_positions_verified", m_dynamic_layout_observed },
                               { "enemy_paths_observed", false },
                               { "routes_from_client_terrain", m_dynamic_layout_observed },
                               { "battle_authorized", false } };
            callback(AsstMsg::SubTaskExtraInfo, layout_info);
            const auto proposal = m_layout_supplied
                                      ? spa::propose_deployment(m_layout_units, m_layout_lanes, m_deployable_cells)
                                      : spa::DeploymentProposal {};
            json::array placements;
            if (proposal.found) {
                for (const auto& unit : proposal.units) {
                    placements.emplace_back(
                        json::object { { "name", unit.id },
                                       { "col", unit.cell.first },
                                       { "row", unit.cell.second },
                                       { "facing", std::string(1, unit.facing) } });
                }
            }
            auto proposal_info = basic_info_with_what("SPAAutomationLayoutProposal");
            proposal_info["details"] = json::object { { "input_complete", proposal.input_complete },
                                                      { "found", proposal.found },
                                                      { "evaluations", proposal.evaluations },
                                                      { "budget_exhausted", proposal.budget_exhausted },
                                                      { "reason", proposal.reason },
                                                      { "placements", std::move(placements) },
                                                      { "vision_positions_verified", false },
                                                      { "movement_sequence_verified", false },
                                                      { "battle_authorized", false } };
            callback(AsstMsg::SubTaskExtraInfo, proposal_info);
            if (proposal.found) {
                const auto sequence = spa::sequence_deployment(m_layout_units, proposal.units);
                json::array steps;
                if (sequence.complete) {
                    for (const auto& step : sequence.steps) {
                        steps.emplace_back(
                            json::object { { "action",
                                             step.kind == spa::DeploymentStepKind::Swap   ? "swap"
                                             : step.kind == spa::DeploymentStepKind::Face ? "face"
                                                                                          : "move" },
                                           { "name", step.unit },
                                           { "displaced", step.displaced },
                                           { "from_col", step.from.first },
                                           { "from_row", step.from.second },
                                           { "to_col", step.to.first },
                                           { "to_row", step.to.second },
                                           { "facing", std::string(1, step.facing) } });
                    }
                }
                auto sequence_info = basic_info_with_what("SPAAutomationMovementSequence");
                sequence_info["details"] = json::object { { "complete", sequence.complete },
                                                          { "steps", std::move(steps) },
                                                          { "explored_states", sequence.explored_states },
                                                          { "budget_exhausted", sequence.budget_exhausted },
                                                          { "reason", sequence.reason },
                                                          { "executed", false },
                                                          { "grid_projection_verified", false },
                                                          { "battle_authorized", false } };
                callback(AsstMsg::SubTaskExtraInfo, sequence_info);
            }
        }
        auto flow = basic_info_with_what("SPAAutomationFlowDiagnostics");
        flow["details"] = json::object {
            { "movement_only", m_movement_only }, { "round_only", m_round_only },
            { "auto_run", m_auto_run },           { "phase_one_game_layout", m_phase_one_game_layout },
            { "funds", state.get("funds", -1) },  { "remaining_deployment", state.get("remaining_deployment", -1) }
        };
        callback(AsstMsg::SubTaskExtraInfo, flow);
        if (m_movement_only) {
            if (!observer.run()) {
                return false;
            }
            const auto final_state = observer.last_observation();
            if (final_state.get("page", std::string()) != "preparation" || !final_state.get("round_known", false) ||
                !final_state.get("funds_known", false) || !final_state.get("remaining_deployment_known", false) ||
                final_state.get("round", -1) != state.get("round", -2) ||
                final_state.get("funds", -1) != state.get("funds", -2) ||
                final_state.get("remaining_deployment", -1) != state.get("remaining_deployment", -2)) {
                return false;
            }
            auto completed = basic_info_with_what("SPAAutomationMovementPlanComplete");
            completed["details"] = json::object { { "steps", static_cast<int>(m_movement_plan.size()) },
                                                  { "directions_verified", false },
                                                  { "battle_authorized", false } };
            callback(AsstMsg::SubTaskExtraInfo, completed);
            return true;
        }
        // Ordinary rounds still clear funds by policy. A reviewed special
        // strategy/equipment spend is the explicit exception: once the caller
        // has supplied a non-empty spending reason, battle may start with the
        // reviewed balance instead of being trapped waiting for zero.
        const bool equipment_discount_window =
            m_auto_run && !m_round_only && !m_decision_only && m_policy.dynamic_decision_mode &&
            m_selected_strategy_id == "band_price" &&
            (m_mode != "joint" || state.get("preparation_seconds_remaining", 0) >= 30);
        const bool review_free_equipment = equipment_discount_window && !m_shopping_complete &&
                                           state.get("funds", -1) == 0 &&
                                           (!m_preparation_instruction_attempts.contains(preparation_round) ||
                                            m_equipment_discount_round == preparation_round);
        if (!review_free_equipment && spa::battle_entry_allowed(
                                          m_round_only,
                                          m_auto_run,
                                          state.get("funds", -1),
                                          m_spending_reviewed,
                                          m_spending_reason)) {
            if (m_stop_before_battle) {
                auto prepared = basic_info_with_what("SPAPreparationCompletedBeforeBattle");
                prepared["details"] = json::object { { "run_id", m_run_id },
                                                     { "round", state.get("round", -1) },
                                                     { "funds", state.get("funds", -1) },
                                                     { "life", state.get("life", -1) },
                                                     { "field_positions", field_position_snapshot() },
                                                     { "roster", roster_snapshot() },
                                                     { "bench_occupied", m_policy.bench_occupied },
                                                     { "equipment_slots", equipment_slots_snapshot() },
                                                     { "equipment_candidates", equipment_candidates_snapshot() },
                                                     { "used_deploy_points", deployment_point_snapshot() },
                                                     { "readiness_input_issued", false },
                                                     { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, prepared);
                return true;
            }
            // Faction activation and named stacker/output cards are progress
            // evidence, not an opening hard gate.  A normal run must be able
            // to spend its early rounds with bridge operators and let later
            // shop levels provide the real layer engine and terminal carry.
            std::vector<spa::RosterUnit> battle_roster;
            battle_roster.reserve(m_policy.owned.size());
            for (const auto& [name, unit] : m_policy.owned) {
                battle_roster.push_back({ name, unit.deployed, field_added_pacts(name), unit.elite });
            }
            const auto battle_formation = spa::evaluate_formation(
                battle_roster,
                spa::operator_pacts(),
                spa::pact_requirements(),
                spa::enabled_formation_pacts(m_enabled_core_pacts, m_policy.disabled_pacts));
            const bool primary_active_for_battle =
                battle_formation.complete &&
                std::find(
                    battle_formation.active_pacts.begin(),
                    battle_formation.active_pacts.end(),
                    m_policy.primary_pact) != battle_formation.active_pacts.end() &&
                (m_strategy_context.active_pacts.empty() ||
                 std::find(
                     m_strategy_context.active_pacts.begin(),
                     m_strategy_context.active_pacts.end(),
                     m_policy.primary_pact) != m_strategy_context.active_pacts.end());
            if (!primary_active_for_battle) {
                auto blocked = basic_info_with_what("SPAAutomationBattleBlocked");
                blocked["details"] =
                    json::object { { "reason",
                                     "Primary faction is not active yet; continue normal early-round development" },
                                   { "primary_pact", m_policy.primary_pact },
                                   { "formation_complete", battle_formation.complete },
                                   { "active_pacts", battle_formation.active_pacts },
                                   { "configured_active_pacts", m_strategy_context.active_pacts },
                                   { "activation_members", battle_formation.activation_members },
                                   { "deployed_roster", roster_snapshot() },
                                   { "battle_authorized", false },
                                   { "hard_gate", false },
                                   { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, blocked);
            }
            std::vector<std::string> deployed_names;
            deployed_names.reserve(battle_roster.size());
            for (const auto& unit : battle_roster) {
                if (unit.deployed) {
                    deployed_names.emplace_back(unit.name);
                }
            }
            const auto primary_roles = spa::assess_primary_roles(m_policy.primary_pact, deployed_names, catalog);
            if (!primary_roles.complete()) {
                json::array growth_engines;
                json::array outputs;
                for (const auto& name : primary_roles.growth_engines) {
                    growth_engines.emplace_back(name);
                }
                for (const auto& name : primary_roles.outputs) {
                    outputs.emplace_back(name);
                }
                auto blocked = basic_info_with_what("SPAAutomationBattleBlocked");
                blocked["details"] =
                    json::object { { "reason", "Primary stacker/output is not ready yet; continue normal development" },
                                   { "primary_pact", m_policy.primary_pact },
                                   { "growth_engine_owned", primary_roles.growth_engine_owned },
                                   { "output_owned", primary_roles.output_owned },
                                   { "growth_engines", std::move(growth_engines) },
                                   { "outputs", std::move(outputs) },
                                   { "activation_members", battle_formation.activation_members },
                                   { "deployed_roster", roster_snapshot() },
                                   { "battle_authorized", false },
                                   { "hard_gate", false },
                                   { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, blocked);
            }
            const bool configured_layout = m_layout_supplied;
            const auto reviewed = configured_layout
                                      ? spa::review_deployment(m_layout_units, m_layout_lanes, m_deployable_cells)
                                      : spa::DeploymentReview {};
            // A setup receipt with a verified formation is already a valid
            // game-side layout.  Static lane review is required only when the
            // caller supplied a coordinate layout for us to validate.
            // Phase one intentionally has no hand-authored route geometry: it
            // must complete a real round using the live formation and reserve
            // static map optimization for phase two.  Do not turn that
            // acceptance boundary into a hard battle block.
            const bool layout_ready =
                m_phase_one_game_layout || !configured_layout || (reviewed.complete && reviewed.static_constraints_met);
            auto gate = basic_info_with_what("SPAAutomationBattleGateDiagnostics");
            gate["details"] =
                json::object { { "layout_ready", layout_ready },
                               { "phase_one_game_layout", m_phase_one_game_layout },
                               { "configured_layout", configured_layout },
                               { "remaining_deployment_known", state.get("remaining_deployment_known", false) },
                               { "funds", state.get("funds", -1) },
                               { "auto_run", m_auto_run },
                               { "round_only", m_round_only },
                               { "spending_reviewed", m_spending_reviewed } };
            callback(AsstMsg::SubTaskExtraInfo, gate);
            if (!layout_ready || !state.get("remaining_deployment_known", false)) {
                auto blocked = basic_info_with_what("SPAAutomationBattleBlocked");
                blocked["details"] =
                    json::object { { "reason",
                                     configured_layout ? "Configured layout coverage is not verified"
                                                       : "Game-side formation layout is not yet observable" },
                                   { "static_constraints_met", reviewed.static_constraints_met },
                                   { "layout_source",
                                     m_phase_one_game_layout
                                         ? "phase_one_game_formation"
                                         : (configured_layout ? "configured_coordinates" : "setup_receipt") },
                                   { "battle_authorized", false } };
                callback(AsstMsg::SubTaskExtraInfo, blocked);
                if (!sleep(500)) {
                    return false;
                }
                continue;
            }
            // A formation/roster receipt is not proof that an operator is
            // currently on a map tile.  Compare the policy's modeled deployed
            // count with the live remaining-deployment counter before ever
            // clicking Ready.  A stale "deployed=true" would otherwise let
            // an empty map consume a real combat round and lose life.  The
            // caller must reconcile the roster through an explicit deploy
            // target (or a fresh live placement observation) before retrying.
            const int observed_deployed = 8 - state.get("remaining_deployment", 8);
            if (m_policy.dynamic_decision_mode && state.get("remaining_deployment_known", false) &&
                m_auto_deploy_positions.size() > static_cast<size_t>(std::max(0, observed_deployed))) {
                auto blocked = basic_info_with_what("SPAAutomationBattleBlocked");
                blocked["details"] = json::object {
                    { "reason", "Field pose ledger exceeds live population; reconcile consumed or displaced copies" },
                    { "round", state.get("round", -1) },
                    { "field_count", m_auto_deploy_positions.size() },
                    { "observed_deployed", observed_deployed },
                    { "battle_authorized", false },
                    { "run_victory_verified", false }
                };
                callback(AsstMsg::SubTaskExtraInfo, blocked);
                return false;
            }
            if (observed_deployed <= 0) {
                auto blocked = basic_info_with_what("SPAAutomationBattleBlocked");
                blocked["details"] =
                    json::object { { "reason", "No operator is currently deployed; do not authorize an empty battle" },
                                   { "observed_deployed", observed_deployed },
                                   { "remaining_deployment", state.get("remaining_deployment", -1) },
                                   { "bench_occupied", m_policy.bench_occupied },
                                   { "battle_authorized", false },
                                   { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, blocked);
                if (!sleep(300)) {
                    return false;
                }
                continue;
            }
            int modeled_deployed = 0;
            for (const auto& [name, unit] : m_policy.owned) {
                if (unit.deployed) {
                    ++modeled_deployed;
                }
            }
            if (observed_deployed < modeled_deployed) {
                auto reconcile = basic_info_with_what("SPAAutomationRosterReconcileRequired");
                reconcile["details"] =
                    json::object { { "reason",
                                     "Live remaining-deployment count contradicts the modeled deployed roster" },
                                   { "observed_deployed", observed_deployed },
                                   { "modeled_deployed", modeled_deployed },
                                   { "remaining_deployment", state.get("remaining_deployment", -1) },
                                   { "deployment_targets_configured", !m_deployment_targets.empty() },
                                   { "battle_authorized", false },
                                   { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, reconcile);
                if (!m_phase_one_game_layout) {
                    // Configured/dynamic runs must not authorize a battle
                    // from a stale roster: the caller supplied an identity
                    // model that can be reconciled explicitly.
                    return true;
                }
                // Phase one is the lifecycle runner.  A resumed game can
                // have a smaller live field than the persisted roster (for
                // example a previous swap receipt marked the displaced unit
                // late).  The live deployment counter is still authoritative
                // for the current round, and the game-side formation is
                // already present, so defer the identity correction and let
                // the round proceed.  This avoids a non-combat stop while
                // keeping the contradiction visible in the audit.
                auto deferred = basic_info_with_what("SPAAutomationRosterReconcileDeferred");
                deferred["details"] =
                    json::object { { "reason",
                                     "Phase-one live formation is usable; defer stale roster identity reconciliation" },
                                   { "observed_deployed", observed_deployed },
                                   { "modeled_deployed", modeled_deployed },
                                   { "remaining_deployment", state.get("remaining_deployment", -1) },
                                   { "battle_authorized", true },
                                   { "identity_reconciled", false },
                                   { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, deferred);
            }
            if (state.get("funds", -1) > 0 && (!m_spending_reviewed || m_spending_reason.empty())) {
                auto blocked = basic_info_with_what("SPAAutomationBattleBlocked");
                blocked["details"] =
                    json::object { { "reason", "Remaining funds require an explicit spending review before battle" },
                                   { "funds", state.get("funds", -1) },
                                   { "spending_reviewed", m_spending_reviewed },
                                   { "battle_authorized", false } };
                callback(AsstMsg::SubTaskExtraInfo, blocked);
                m_spending_reviewed = true;
                m_spending_reason = spa::round_balance_review_reason(m_selected_strategy_id, state.get("funds", -1));
                if (!sleep(250)) {
                    return false;
                }
                continue;
            }
            spa::RoundTaskPlugin round(m_callback, m_inst, TaskType);
            round.set_retry_times(0);
            round.set_task_id(m_task_id);
            round.set_task_ptr(this);
            if (!round.configure(
                    state.get("round", -1),
                    state.get("remaining_deployment", -1),
                    true,
                    true,
                    state.get("funds", -1),
                    m_spending_reviewed,
                    m_spending_reason,
                    state.get("remaining_deployment_inferred", false),
                    m_mode == "joint")) {
                auto blocked = basic_info_with_what("SPAAutomationBattleBlocked");
                blocked["details"] =
                    json::object { { "reason", "Round plugin rejected observed state or spending review" },
                                   { "round", state.get("round", -1) },
                                   { "funds", state.get("funds", -1) },
                                   { "remaining_deployment", state.get("remaining_deployment", -1) },
                                   { "spending_reviewed", m_spending_reviewed },
                                   { "spending_reason_present", !m_spending_reason.empty() },
                                   { "battle_authorized", false } };
                callback(AsstMsg::SubTaskExtraInfo, blocked);
                if (!sleep(250)) {
                    return false;
                }
                continue;
            }
            auto authorized = basic_info_with_what("SPAAutomationBattleAuthorized");
            authorized["details"] =
                json::object { { "round", state.get("round", -1) },
                               { "funds", state.get("funds", -1) },
                               { "remaining_deployment", state.get("remaining_deployment", -1) },
                               { "bench_occupied", m_policy.bench_occupied },
                               { "deployment_deferred_count", static_cast<int>(m_auto_deploy_blocked_names.size()) },
                               { "static_constraints_met", layout_ready },
                               { "layout_source",
                                 m_phase_one_game_layout
                                     ? "phase_one_game_formation"
                                     : (configured_layout ? "configured_coordinates" : "setup_receipt") },
                               { "primary_activation_verified", primary_active_for_battle },
                               { "primary_roles_verified", primary_roles.complete() },
                               { "combat_strength_verified", false },
                               { "battle_authorized", true } };
            callback(AsstMsg::SubTaskExtraInfo, authorized);
            round.track_primary_pact(m_policy.primary_pact);
            const bool round_ok = round.run();
            m_lifecycle_battle_seen = m_lifecycle_battle_seen || round.battle_seen();
            if (round.session_interrupted()) {
                // Emit the actual authentication page so the host reattaches
                // this checkpoint after login, without calling it a battle loss.
                observer.run();
                return true;
            }
            auto result = basic_info_with_what(round_ok ? "SPAAutomationBattleObserved" : "SPAAutomationRoundRecovery");
            result["details"] = json::object { { "round", state.get("round", -1) },
                                               { "battle_started", round.battle_seen() },
                                               { "result_verified", false },
                                               { "next_round_ready", round.next_round() == state.get("round", -1) + 1 },
                                               { "next_round", round.next_round() },
                                               { "battle_outcome", round_ok ? "observed" : "unverified" },
                                               { "primary_pact", m_policy.primary_pact },
                                               { "active_pacts",
                                                 [&] {
                                                     json::array pacts;
                                                     for (const auto& pact : m_strategy_context.active_pacts) {
                                                         pacts.emplace_back(pact);
                                                     }
                                                     return pacts;
                                                 }() },
                                               { "roster", roster_snapshot() },
                                               { "selected_strategy_id", m_selected_strategy_id },
                                               { "strategy_context", m_strategy_context.initial_brief },
                                               { "run_victory_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, result);
            if (round.result_seen()) {
                const auto& visible_pacts = round.latest_pact_layer_snapshot();
                json::object visible_counts;
                int highest_layers = -1;
                for (const auto& [pact, layers] : visible_pacts) {
                    visible_counts[pact] = layers;
                    highest_layers = std::max(highest_layers, layers);
                }
                const bool active_pacts_covered = battle_formation.complete && !battle_formation.active_pacts.empty() &&
                                                  std::all_of(
                                                      battle_formation.active_pacts.begin(),
                                                      battle_formation.active_pacts.end(),
                                                      [&](const auto& pact) { return visible_pacts.contains(pact); });
                auto growth = basic_info_with_what("SPABattleEndingGrowthReview");
                growth["details"] =
                    json::object { { "round", state.get("round", -1) },
                                   { "strategy_id", m_selected_strategy_id },
                                   { "primary_pact", m_policy.primary_pact },
                                   { "battle_sample_pact_layers", std::move(visible_counts) },
                                   { "opening_formation_active_pacts_all_read", active_pacts_covered },
                                   { "highest_observed_layers", highest_layers },
                                   { "needs_growth_review", highest_layers >= 0 && highest_layers < 300 },
                                   { "route_growth_accepted", highest_layers >= 500 },
                                   { "exact_final_layer_count_verified", false },
                                   { "settlement", round.last_result() } };
                callback(AsstMsg::SubTaskExtraInfo, growth);
                auto review = basic_info_with_what("SPAPrimaryPactSettlementReview");
                const auto latest = round.latest_primary_layers();
                const auto preparation_value = m_policy.observed_pact_layers.find(m_policy.primary_pact);
                // A preparation count is only a baseline, even if it was
                // larger than the last witnessed battle sample.
                review["details"] = json::object {
                    { "run_id", m_run_id },
                    { "round", state.get("round", -1) },
                    { "primary_pact", m_policy.primary_pact },
                    { "strategy_id", m_selected_strategy_id },
                    { "preparation_layers_known", preparation_value != m_policy.observed_pact_layers.end() },
                    { "preparation_layers",
                      preparation_value != m_policy.observed_pact_layers.end() ? json::value(preparation_value->second)
                                                                               : json::value(nullptr) },
                    { "battle_sample_layers", latest ? json::value(*latest) : json::value(nullptr) },
                    { "battle_sample_enemies_resolved",
                      round.latest_primary_layer_sample_enemies().has_value()
                          ? json::value(*round.latest_primary_layer_sample_enemies())
                          : json::value(nullptr) },
                    { "observed_growth_band",
                      !latest          ? "unknown"
                      : *latest == 999 ? "at_cap"
                      : *latest >= 500 ? "advanced_sample"
                      : *latest >= 300 ? "baseline_sample"
                                       : "needs_review" },
                    { "final_exact_layer_count_verified", false },
                    { "settlement", round.last_result() },
                    { "roster", roster_snapshot() }
                };
                callback(AsstMsg::SubTaskExtraInfo, review);
                if (m_manage_lifecycle && m_mode != "joint" &&
                    spa::round_settlement_can_be_retained(round.last_result())) {
                    m_lifecycle_solo_result = round.last_result();
                    return true;
                }
                continue;
            }
            if (!m_auto_run || !round_ok) {
                if (m_auto_run && !need_exit()) {
                    // A missed transition or changed HUD is not a combat loss.
                    // Reobserve this same run instead of ending the native task.
                    m_spending_reviewed = false;
                    m_spending_reason.clear();
                    continue;
                }
                return round_ok;
            }
            if (round.next_round() != state.get("round", -1) + 1) {
                // A reward page is a real inter-round state, but it is not a
                // completed transition.  Resolve it through the guarded
                // reward plugins and only then resume the next preparation
                // observation.  Any ambiguous card or failed postcondition
                // stops the run instead of replaying the old ready click.
                if (!handle_reward_page(state.get("round", -1))) {
                    m_spending_reviewed = false;
                    m_spending_reason.clear();
                    continue;
                }
            }
            m_spending_reviewed = false;
            m_spending_reason.clear();
            continue;
        }

        // Every preparation window starts with the shop expanded.  A collapsed
        // shop moves the bench and can cover the rightmost operator, so never
        // treat its “展开” button as a shop offer or spend a refresh before
        // restoring the full purchase/bench view.
        bool shop_was_collapsed = false;
        for (const auto& text : *texts) {
            const auto name = text.get("text", std::string());
            const int x = text.get("x", -1);
            const int y = text.get("y", -1);
            if (text.get("score", 0.0) >= 0.85 && spa::parse_shop_toggle(name) == false && x > 1080 && y > 620) {
                shop_was_collapsed = true;
                break;
            }
        }
        if (shop_was_collapsed) {
            if (!ctrler()->click(Point(1190, 670)) || !sleep(500)) {
                return false;
            }
            auto expanded = basic_info_with_what("SPAShopExpandedForPreparation");
            expanded["details"] = json::object { { "round", state.get("round", -1) },
                                                 { "rightmost_bench_visible", true },
                                                 { "reason", "Preparation shop decisions require expanded hand" } };
            callback(AsstMsg::SubTaskExtraInfo, expanded);
            continue;
        }

        // Deployment, movement and round transitions do not depend on unrelated shop prices.
        for (const auto& text : *texts) {
            const auto name = text.get("text", std::string());
            const int x = text.get("x", -1);
            const int y = text.get("y", -1);
            if (text.get("score", 0.0) < 0.85) {
                continue;
            }
            if (m_blocked_purchases.contains({ state.get("round", -1), name }) ||
                (m_pending_direct_promotion && m_pending_direct_promotion->get("operator", std::string()) == name)) {
                continue;
            }
            if (y >= 630 && y <= 690 && catalog.contains(name)) {
                const auto slot = spa::shop_column_from_right(x);
                if (!slot) {
                    // A transient OCR coordinate on one card must not end a
                    // live run.  Leave this offer unpriced and let the next
                    // observation retry it.
                    continue;
                }
                auto price = spa::shop_number(image, Rect(1050 - *slot * 162, 515, 24, 25));
                // Free reward cards center the zero badge 12 px left of the
                // paid shop badge.  The purchase plugin already uses that
                // geometry for price 0; probe it here as a bounded fallback
                // so phase one buys the free card instead of refreshing past
                // an actionable offer.
                if (!price) {
                    price = spa::shop_number(image, Rect(1038 - *slot * 162, 515, 24, 25));
                }
                if (!price) {
                    shop_scan.emplace_back(
                        json::object { { "name", name },
                                       { "x", x },
                                       { "y", y },
                                       { "score", text.get("score", 0.0) },
                                       { "slot", *slot },
                                       { "price", nullptr },
                                       { "price_unknown", true },
                                       { "catalog_match", true } });
                    continue;
                }
                offers.push_back({ name, *price, *slot });
                shop_scan.emplace_back(
                    json::object { { "name", name },
                                   { "x", x },
                                   { "y", y },
                                   { "score", text.get("score", 0.0) },
                                   { "slot", *slot },
                                   { "price", *price },
                                   { "catalog_match", true } });
            }
            else if (y >= 630 && y <= 690 && !name.empty()) {
                const auto& profiles = spa::equipment_profiles();
                const auto profile =
                    std::find_if(profiles.begin(), profiles.end(), [&](const auto& item) { return item.name == name; });
                json::object scanned { { "name", name },
                                       { "x", x },
                                       { "y", y },
                                       { "score", text.get("score", 0.0) },
                                       { "slot", nullptr },
                                       { "price", nullptr },
                                       { "catalog_match", profile != profiles.end() },
                                       { "is_equipment", profile != profiles.end() } };
                if (profile != profiles.end()) {
                    scanned["price_unknown"] = true;
                    if (const auto slot = spa::shop_column_from_right(x)) {
                        scanned["slot"] = *slot;
                        if (const auto price = spa::equipment_shop_price(image, *slot)) {
                            scanned["price"] = *price;
                            scanned["price_unknown"] = false;
                            auto priced = *profile;
                            priced.price = *price;
                            if (!equipment_capacity_blocked) {
                                equipment_offers.push_back(priced);
                            }
                            equipment_slots.emplace(name, *slot);
                        }
                    }
                }
                // Equipment used to be priced for the policy but always
                // logged as unknown. Preserve the actual same-frame quote
                // so a skipped purchase can be distinguished from OCR loss.
                shop_scan.emplace_back(std::move(scanned));
            }
        }
        std::vector<spa::ShopOffer> capacity_blocked_offers;
        if (confirmed_full_bench) {
            // Preserve the visible quotes to compare purchases that justify a sale.
            // They cannot go through the normal purchase path until a slot is freed.
            capacity_blocked_offers.swap(offers);
            m_policy.bench_occupied = 10;
        }
        auto shop_scan_info = basic_info_with_what("SPAAutomationShopScan");
        shop_scan_info["details"] = json::object { { "round", state.get("round", -1) },
                                                   { "funds", state.get("funds", -1) },
                                                   { "offers", std::move(shop_scan) },
                                                   { "offer_count", static_cast<int>(offers.size()) },
                                                   { "bench_capacity_full", confirmed_full_bench },
                                                   { "run_victory_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, shop_scan_info);
        const int funds = state.get("funds", -1);
        const int round = state.get("round", -1);
        m_strategy_context.round = round;
        m_policy.round = round;
        m_policy.current_life = state.get("life_known", false) ? state.get("life", -1) : -1;
        m_policy.boss_preparation = boss_preparation;
        m_policy.allow_freeze = !boss_preparation;
        // Freeze persists in the game UI across observations and may carry
        // into the next round.  The observer's generic preparation payload
        // intentionally does not infer this state, so read the explicit
        // toggle here.  Never click a visible "解冻/已冻结" button as if it
        // were the normal "冻结" action.
        for (const auto& text : *texts) {
            const auto name = text.get("text", std::string());
            const int x = text.get("x", -1);
            const int y = text.get("y", -1);
            if (const auto frozen = spa::observe_shop_freeze({ name, text.get("score", 0.0), x, y })) {
                m_policy.shop_frozen_this_round = *frozen;
                break;
            }
        }
        m_policy.free_refreshes = state.get("free_refreshes", 0);
        m_policy.deployed_population =
            state.get("remaining_deployment_known", false) ? 8 - state.get("remaining_deployment", 8) : -1;
        int blockers = 0, ranged = 0, healers = 0;
        for (const auto& [name, unit] : m_policy.owned) {
            if (!unit.deployed) {
                continue;
            }
            const auto role = spa::operator_placement_role(name);
            const int count = std::max(1, static_cast<int>(m_auto_deploy_positions.count(name)));
            blockers += role == spa::PlacementRole::Frontline ? count : 0;
            ranged += role == spa::PlacementRole::Ranged ? count : 0;
            healers += role == spa::PlacementRole::Healer ? count : 0;
        }
        m_policy.early_frontline_count = blockers;
        m_policy.early_board_ready = blockers >= 2 && m_policy.deployed_population >= 4;
        m_policy.early_board_scores.clear();
        for (const auto& [name, pacts] : catalog) {
            const auto role = spa::operator_placement_role(name);
            int score = 100;
            if (role == spa::PlacementRole::Frontline) {
                score = blockers < 2 ? 1700 : 500;
            }
            if (role == spa::PlacementRole::Ranged) {
                score = ranged < 2 ? 1600 : 600;
            }
            if (role == spa::PlacementRole::Healer) {
                score = healers == 0 && blockers >= 2 ? 900 : 250;
            }
            m_policy.early_board_scores.emplace(name, score);
        }
        if (m_policy.free_refreshes < 0) {
            return false;
        }
        // A shop-level badge can be occluded by the expanded shop/map overlay.
        // Zero here means “not observed”, never book I. Preserve the last
        // authoritative level so dynamic route scoring cannot unlock a
        // higher-tier window merely because one OCR frame missed LEVEL.
        const int observed_shop_level = state.get("shop_level", 0);
        // Shop levels cannot decrease within a run. OCR can read II as I;
        // that must not overwrite an already reconciled paid upgrade.
        if (observed_shop_level >= m_policy.shop_level && observed_shop_level >= 1 && observed_shop_level <= 6) {
            m_policy.shop_level = observed_shop_level;
        }
        m_policy.enabled_core_pacts = m_enabled_core_pacts;
        m_policy.disabled_pacts = m_strategy_context.disabled_pacts;
        m_policy.dynamic_primary_pact = m_dynamic_primary_pact;
        // Phase one uses a small, explicit upgrade schedule so available
        // funds are not silently discarded while the controller is waiting
        // for a card or a deployment retry.  The displayed shop level and
        // displayed upgrade price are independent observations.
        const bool scheduled_upgrade =
            std::find(m_policy.basic_upgrade_rounds.begin(), m_policy.basic_upgrade_rounds.end(), round) !=
            m_policy.basic_upgrade_rounds.end();
        const bool no_affordable_operator = std::none_of(offers.begin(), offers.end(), [&](const auto& offer) {
            return offer.price >= 0 && offer.price <= funds;
        });
        if (m_policy.basic_mode && !m_policy.dynamic_decision_mode && state.get("upgrade_price_known", false) &&
            m_policy.shop_level < 6 &&
            (scheduled_upgrade || no_affordable_operator || state.get("upgrade_price", -1) == 0) &&
            !m_basic_upgrade_done_rounds.contains(round) && funds >= state.get("upgrade_price", -1)) {
            const int upgrade_price = state.get("upgrade_price", -1);
            spa::ShopTaskPlugin upgrade(m_callback, m_inst, TaskType);
            upgrade.set_fast_mode(m_policy.basic_mode || m_policy.dynamic_decision_mode);
            upgrade.set_retry_times(0);
            upgrade.set_task_id(m_task_id);
            upgrade.set_task_ptr(this);
            if (!upgrade.configure_upgrade(upgrade_price, funds, round) || !upgrade.run()) {
                auto failed = basic_info_with_what("SPAShopUpgradeBlocked");
                failed["details"] = json::object { { "round", round },
                                                   { "price", upgrade_price },
                                                   { "funds", funds },
                                                   { "reason", "Fixed phase-one upgrade was not reconciled" },
                                                   { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, failed);
                auto deferred = basic_info_with_what("SPAShopUpgradeDeferred");
                deferred["details"] = json::object {
                    { "round", round },
                    { "price", upgrade_price },
                    { "funds", funds },
                    { "reason", "Upgrade is optional for this preparation; continue with the current roster" },
                    { "battle_started", false },
                    { "run_victory_verified", false }
                };
                callback(AsstMsg::SubTaskExtraInfo, deferred);
                m_basic_upgrade_done_rounds.insert(round);
                m_spending_reviewed = false;
                m_spending_reason.clear();
                continue;
            }
            m_basic_upgrade_done_rounds.insert(round);
            if (m_policy.shop_level >= 1 && m_policy.shop_level < 6) {
                ++m_policy.shop_level;
            }
            auto upgraded = basic_info_with_what("SPAShopUpgradeReconciled");
            upgraded["details"] = json::object { { "round", round },
                                                 { "price", upgrade_price },
                                                 { "funds_before", funds },
                                                 { "schedule", "phase1_fixed" },
                                                 { "shop_level_after", m_policy.shop_level },
                                                 { "run_victory_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, upgraded);
            continue;
        }
        // Activation is current state, not a permanent route lock. The
        // selector protects observed layer investment while evaluating a
        // fully funded alternative growth package.
        std::vector<spa::RosterUnit> pre_dynamic_roster;
        pre_dynamic_roster.reserve(m_policy.owned.size());
        for (const auto& [name, unit] : m_policy.owned) {
            pre_dynamic_roster.push_back({ name, unit.deployed, field_added_pacts(name), unit.elite });
        }
        const auto pre_dynamic_formation = spa::evaluate_formation(
            pre_dynamic_roster,
            catalog,
            spa::pact_requirements(),
            spa::enabled_formation_pacts(m_enabled_core_pacts, m_policy.disabled_pacts));
        const bool modeled_primary_is_active =
            pre_dynamic_formation.complete && std::find(
                                                  pre_dynamic_formation.active_pacts.begin(),
                                                  pre_dynamic_formation.active_pacts.end(),
                                                  m_policy.primary_pact) != pre_dynamic_formation.active_pacts.end();
        const bool current_primary_is_active =
            !m_policy.primary_pact.empty() && (std::find(
                                                   m_strategy_context.active_pacts.begin(),
                                                   m_strategy_context.active_pacts.end(),
                                                   m_policy.primary_pact) != m_strategy_context.active_pacts.end() ||
                                               modeled_primary_is_active);
        m_policy.primary_locked_after_activation = current_primary_is_active;
        if (m_dynamic_primary_pact) {
            m_policy.selected_strategy_id = m_selected_strategy_id;
            const auto dynamic = spa::choose_dynamic_primary_pact(m_policy, offers, catalog, funds);
            if (dynamic.valid && dynamic.primary_pact != m_policy.primary_pact) {
                const auto previous = m_policy.primary_pact;
                m_policy.primary_pact = dynamic.primary_pact;
                m_policy.primary_locked_after_activation =
                    std::find(
                        pre_dynamic_formation.active_pacts.begin(),
                        pre_dynamic_formation.active_pacts.end(),
                        m_policy.primary_pact) != pre_dynamic_formation.active_pacts.end();
                std::vector<std::string> retained_carries;
                for (const auto& carry : m_policy.carries) {
                    const auto found = catalog.find(carry);
                    if (found != catalog.end() &&
                        std::find(found->second.begin(), found->second.end(), m_policy.primary_pact) !=
                            found->second.end()) {
                        retained_carries.emplace_back(carry);
                    }
                }
                m_policy.carries = std::move(retained_carries);
                json::array ranking;
                for (const auto& signal : dynamic.ranking) {
                    ranking.emplace_back(
                        json::object { { "pact", signal.pact },
                                       { "required_members", signal.required_members },
                                       { "distinct_members", signal.distinct_members },
                                       { "reachable_members", signal.reachable_members },
                                       { "deployed_members", signal.deployed_members },
                                       { "shop_members", signal.shop_members },
                                       { "minimum_shop_level", signal.minimum_shop_level },
                                       { "growth_engine_owned", signal.growth_engine_owned },
                                       { "growth_engine_eligible", signal.growth_engine_eligible },
                                       { "growth_engine_offers", signal.growth_engine_offers },
                                       { "output_owned", signal.output_owned },
                                       { "output_offers", signal.output_offers },
                                       { "score", signal.score },
                                       { "inferred_contention", signal.inferred_contention },
                                       { "activation_ready", signal.activation_ready },
                                       { "book_window_met", signal.book_window_met },
                                       { "role_signal", signal.role_signal },
                                       { "viable", signal.viable },
                                       { "native_growth_available", signal.native_growth_available } });
                }
                auto selected = basic_info_with_what("SPAAutomationPrimaryPactSelected");
                selected["details"] = json::object { { "previous_primary_pact", previous },
                                                     { "primary_pact", m_policy.primary_pact },
                                                     { "score", dynamic.score },
                                                     { "reason", dynamic.reason },
                                                     { "round", round },
                                                     { "shop_level", m_policy.shop_level },
                                                     { "locked_after_activation", current_primary_is_active },
                                                     { "ranking", std::move(ranking) },
                                                     { "selected_strategy_id", m_selected_strategy_id },
                                                     { "guide_restricted", dynamic.guide_restricted },
                                                     { "noncore_guide_pending", dynamic.noncore_guide_pending } };
                callback(AsstMsg::SubTaskExtraInfo, selected);
            }
        }
        if (m_policy.dynamic_decision_mode && !m_policy.primary_pact.empty() && m_policy.primary_pact != "auto") {
            // Route profiles provide role hints rather than a fixed graduate
            // roster. Keep the current route's stackers ahead of terminal
            // outputs so the dynamic deploy loop can activate growth before
            // spending remaining slots on damage; battle-redeploy helpers are
            // last because they are a phase-two combat action, not a shop
            // purchase requirement. Disabled pacts and unknown catalogue
            // names are filtered before they can influence buying/deployment.
            std::vector<std::string> roles;
            if (const auto route = spa::route_profiles().find(m_policy.primary_pact);
                route != spa::route_profiles().end()) {
                const auto append_roles = [&](const std::vector<std::string>& names) {
                    for (const auto& name : names) {
                        if (!catalog.contains(name) || std::find(roles.begin(), roles.end(), name) != roles.end()) {
                            continue;
                        }
                        roles.emplace_back(name);
                    }
                };
                append_roles(route->second.growth_engines);
                append_roles(route->second.output_candidates);
                append_roles(route->second.battle_redeploy_units);
            }
            if (roles != m_dynamic_roles) {
                m_dynamic_roles = roles;
                // Stackers remain deployment candidates, but must not be
                // mislabeled as terminal output for buying or freezing.
                const auto route = spa::route_profiles().find(m_policy.primary_pact);
                m_policy.carries =
                    route == spa::route_profiles().end() ? std::vector<std::string>() : route->second.output_candidates;
                json::array role_json;
                for (const auto& name : roles) {
                    role_json.emplace_back(name);
                }
                auto role_info = basic_info_with_what("SPAAutomationRolePlan");
                role_info["details"] = json::object { { "primary_pact", m_policy.primary_pact },
                                                      { "roles", std::move(role_json) },
                                                      { "growth_first", true },
                                                      { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, role_info);
            }
        }
        std::vector<spa::RosterUnit> modeled_strategy_roster;
        modeled_strategy_roster.reserve(m_policy.owned.size());
        for (const auto& [name, unit] : m_policy.owned) {
            modeled_strategy_roster.push_back({ name, unit.deployed, field_added_pacts(name), unit.elite });
        }
        const auto modeled_strategy_formation = spa::evaluate_formation(
            modeled_strategy_roster,
            catalog,
            spa::pact_requirements(),
            spa::enabled_formation_pacts(m_enabled_core_pacts, m_policy.disabled_pacts));
        // A modeled pact name is usable only when the complete formation
        // observation is valid.  Merely seeing same-faction operators (or a
        // configured core pact) must never be promoted to an active pact.
        const auto current_strategy_context = spa::strategy_context_for_formation(
            m_strategy_context,
            modeled_strategy_formation.complete,
            modeled_strategy_formation.active_pacts);
        if (!m_strategy_candidates.empty()) {
            const auto selection = spa::choose_strategy_with_fallback(m_strategy_candidates, current_strategy_context);
            auto strategy_info = basic_info_with_what("SPAStrategySelection");
            strategy_info["details"] =
                json::object { { "found", selection.found },
                               { "round", round },
                               { "initial_brief", m_strategy_context.initial_brief },
                               { "disabled_pacts", m_strategy_context.disabled_pacts },
                               { "active_pacts", current_strategy_context.active_pacts },
                               { "modeled_active_pacts", modeled_strategy_formation.active_pacts },
                               { "disabled_strategy_ids", m_strategy_context.disabled_strategy_ids } };
            if (selection.found) {
                strategy_info["details"]["strategy_id"] = selection.candidate.id;
                strategy_info["details"]["strategy_name"] = selection.candidate.name;
                strategy_info["details"]["eligible"] = selection.eligibility.eligible;
                strategy_info["details"]["unclaimed_priority"] = selection.eligibility.unclaimed_priority;
                strategy_info["details"]["score"] = selection.eligibility.score;
                strategy_info["details"]["reasons"] = selection.eligibility.reasons;
            }
            callback(AsstMsg::SubTaskExtraInfo, strategy_info);
        }

        // Compute the current offers before both upgrade and purchase policy.
        m_policy.full_field_offer_improvements.clear();
        if (m_policy.dynamic_decision_mode && m_policy.deployed_population == 8 && !m_pending_field_replacement) {
            const auto [field, bench] = replacement_inputs();
            if (field.size() == 8) {
                const auto& visible_operators = confirmed_full_bench ? capacity_blocked_offers : offers;
                for (const auto& offer : visible_operators) {
                    const auto incoming = replacement_member(offer.name, spa::OwnedOperator { 1, false, false });
                    const auto exchange =
                        m_failed_replacements.size() < 3
                            ? spa::choose_replacement(
                                  m_policy.primary_pact,
                                  field,
                                  { incoming },
                                  m_failed_replacements,
                                  [this](const auto& lineup) { return replacement_lineup_context_value(lineup); },
                                  m_visited_replacement_lineups,
                                  m_policy.disabled_pacts)
                            : spa::ReplacementChoice {};
                    m_policy.full_field_offer_improvements[offer.name] = exchange.found ? exchange.improvement : 0;
                }
            }
        }

        int primary_members = 0;
        std::vector<spa::GrowthSource> sources;
        const int field_population =
            state.get("remaining_deployment_known", false) ? 8 - state.get("remaining_deployment", 8) : -1;
        const bool field_poses_complete =
            field_population >= 0 && field_population <= 8 &&
            m_auto_deploy_positions.size() == static_cast<size_t>(field_population) &&
            std::all_of(m_auto_deploy_positions.begin(), m_auto_deploy_positions.end(), [&](const auto& item) {
                return m_verified_field_pose_points.contains(item.second);
            });
        if (confirmed_full_bench && funds > 0 && field_poses_complete && field_population == 8 &&
            !m_field_checkpoint_loaded && !m_pending_spatial_orientation && !m_bench_recount_pending &&
            !m_purchase_reserved_after_sale && m_auto_run &&
            (m_mode != "joint" || state.get("preparation_seconds_remaining", 0) >= 45)) {
            std::map<std::string, int> field_copies;
            for (const auto& [name, position] : m_auto_deploy_positions) {
                ++field_copies[name];
            }
            std::vector<std::string> reserved_names;
            for (const auto& [failed_round, name] : m_failed_bench_sales) {
                if (failed_round == round) {
                    reserved_names.push_back(name);
                }
            }
            if (m_pending_direct_promotion) {
                reserved_names.push_back(m_pending_direct_promotion->get("operator", std::string()));
            }
            if (m_pending_field_replacement) {
                reserved_names.push_back(m_pending_field_replacement->incoming);
                reserved_names.push_back(m_pending_field_replacement->outgoing);
            }
            const auto trade = spa::choose_bench_capacity_trade(
                m_policy,
                funds,
                capacity_blocked_offers,
                field_copies,
                reserved_names);
            if (trade && handle_full_bench_sale(state, 0, &*trade)) {
                continue;
            }
            if (m_policy.primary_locked_after_activation && m_policy.allow_refresh &&
                funds > m_policy.minimum_recruit_price && handle_full_bench_sale(state)) {
                continue;
            }
        }
        std::optional<int> equipped_population;
        if (field_poses_complete) {
            const auto [equipment_context, recipients] = equipment_assignment_inputs();
            if (recipients.size() == static_cast<size_t>(field_population) &&
                std::all_of(recipients.begin(), recipients.end(), [](const auto& target) {
                    return target.slots_known;
                })) {
                equipped_population =
                    static_cast<int>(std::count_if(recipients.begin(), recipients.end(), [](const auto& target) {
                        return !target.equipped.empty();
                    }));
            }
        }
        std::optional<int> ursus_population;
        if (m_policy.primary_pact == "乌萨斯" && field_poses_complete) {
            ursus_population = static_cast<int>(
                std::count_if(m_auto_deploy_positions.begin(), m_auto_deploy_positions.end(), [&](const auto& member) {
                    const auto traits = catalog.find(member.first);
                    return traits != catalog.end() &&
                           std::find(traits->second.begin(), traits->second.end(), "乌萨斯") != traits->second.end();
                }));
        }
        json::object population_counts;
        for (const auto& [name, unit] : m_policy.owned) {
            if (unit.copies > 0 || unit.elite) {
                spa::GrowthSource source { name, unit.elite, unit.deployed };
                if (m_policy.primary_pact == "乌萨斯" && (name == "烈夏" || name == "折桠" || name == "乌啾")) {
                    const auto forms = spa::owned_form_counts(unit);
                    source.elite = forms && forms->initial == 0 && forms->elite > 0;
                    if (name == "乌啾" && unit.deployed) {
                        source.eligible_population = ursus_population;
                    }
                }
                if (unit.deployed && field_poses_complete && !(unit.elite && unit.copies > 1)) {
                    if (name == "涤火杰西卡") {
                        source.eligible_population = equipped_population;
                    }
                    else if (name == "罗比菈塔" && m_growth_population_poses == m_auto_deploy_positions) {
                        if (const auto count = m_row_growth_populations.find(name);
                            count != m_row_growth_populations.end()) {
                            source.eligible_population = count->second;
                        }
                    }
                }
                if (source.eligible_population) {
                    population_counts.emplace(name, *source.eligible_population);
                }
                sources.push_back(std::move(source));
            }
            if (!unit.deployed) {
                continue;
            }
            const auto found = catalog.find(name);
            if (found != catalog.end() &&
                std::find(found->second.begin(), found->second.end(), m_policy.primary_pact) != found->second.end()) {
                ++primary_members;
            }
        }
        std::vector<spa::RosterUnit> modeled_roster;
        modeled_roster.reserve(m_policy.owned.size());
        for (const auto& [name, unit] : m_policy.owned) {
            modeled_roster.push_back({ name, unit.deployed, field_added_pacts(name), unit.elite });
        }
        const auto modeled_formation = spa::evaluate_formation(
            modeled_roster,
            catalog,
            spa::pact_requirements(),
            spa::enabled_formation_pacts(m_enabled_core_pacts, m_policy.disabled_pacts));
        int primary_activation_required = 0;
        const auto& pact_rules = spa::pact_requirements();
        if (const auto requirement = std::find_if(
                pact_rules.begin(),
                pact_rules.end(),
                [&](const auto& item) { return item.name == m_policy.primary_pact; });
            requirement != pact_rules.end()) {
            primary_activation_required = requirement->members;
        }
        const bool primary_active_by_formation =
            modeled_formation.complete && std::find(
                                              modeled_formation.active_pacts.begin(),
                                              modeled_formation.active_pacts.end(),
                                              m_policy.primary_pact) != modeled_formation.active_pacts.end();
        const bool external_activation_matches = m_strategy_context.active_pacts.empty() ||
                                                 std::find(
                                                     m_strategy_context.active_pacts.begin(),
                                                     m_strategy_context.active_pacts.end(),
                                                     m_policy.primary_pact) != m_strategy_context.active_pacts.end();
        // An empty external list means “derive from the verified roster”; a
        // non-empty list is an additional game-side assertion that must match.
        const bool primary_active_verified = primary_active_by_formation && external_activation_matches;
        const auto growth = spa::project_primary_growth(
            m_policy.primary_pact,
            round,
            m_expected_boss_round,
            primary_active_verified,
            sources);
        json::array engines;
        for (const auto& name : growth.engine_names) {
            engines.emplace_back(name);
        }
        // Layer OCR is advisory and must never block the preparation loop.
        // The observer has already spent a full OCR pass for this frame; doing
        // another full-image detection here can stall on animated shop frames
        // and prevents the actual buy/refresh decision from being reached.
        // Reuse same-frame anchored digits from that pass. Missing digits
        // stay unknown and never become an inferred zero or an extra OCR pass.
        std::optional<int> layers;
        if (const auto current = m_policy.observed_pact_layers.find(m_policy.primary_pact);
            current != m_policy.observed_pact_layers.end()) {
            layers = current->second;
        }
        auto assessment = basic_info_with_what("SPAAutomationGrowthAssessment");
        assessment["details"] = json::object {
            { "primary_pact", m_policy.primary_pact },
            { "deployed_primary_members", primary_members },
            { "primary_active_by_formation", primary_active_by_formation },
            { "external_activation_matches", external_activation_matches },
            { "primary_active_verified", primary_active_verified },
            { "primary_layers_known", layers.has_value() },
            { "primary_layers", layers.value_or(0) },
            { "expected_boss_round", m_expected_boss_round },
            { "boss_round_verified", false },
            { "scenario_valid", growth.valid },
            { "preparation_ends", growth.preparation_ends },
            { "battles_before_boss", growth.completed_battles_before_boss },
            { "fixed_phase_additions_if_eligible", growth.fixed_phase_additions },
            { "conditional_base_cap_additions", growth.conditional_cap_additions },
            { "conditional_preparation_cap_additions", growth.conditional_preparation_cap_additions },
            { "population_phase_additions_if_unchanged", growth.population_phase_additions_if_unchanged },
            { "verified_population_counts", std::move(population_counts) },
            { "field_poses_complete", field_poses_complete },
            { "conversion_sources", growth.conversion_sources },
            { "sale_price_sources", growth.sale_price_sources },
            { "temporary_sources", growth.temporary_sources },
            { "ursus_opening_temporary_layers_per_battle", growth.ursus_cycle.opening_temporary_layers },
            { "ursus_conversion_capacity_per_preparation", growth.ursus_cycle.conversion_capacity },
            { "ursus_conversion_population_unknown", growth.ursus_cycle.conversion_population_unknown },
            { "ursus_converted_opening_layers_if_unchanged", growth.ursus_converted_opening_layers_if_unchanged },
            { "unmodeled_sources", growth.unmodeled_sources },
            { "engine_names", std::move(engines) },
            { "forecast_is_scenario", true },
            { "upper_bound_proven", false },
            { "other_growth_sources_accounted", false },
            { "combat_strength_verified", false }
        };
        callback(AsstMsg::SubTaskExtraInfo, assessment);
        // This horizon is a planning scenario, never a verified future wave.
        // Use the current reconciled field and same-frame layer observation;
        // a checkpoint or pending mutation cannot justify a new reservation.
        m_policy.growth_primary_active_verified = primary_active_verified && field_poses_complete &&
                                                  !m_field_checkpoint_loaded && !m_bench_recount_pending &&
                                                  !m_pending_field_replacement && !m_pending_spatial_orientation;
        m_policy.growth_scenario_horizon_round = m_expected_boss_round;
        // Banking a spare coin is a purchase of an intentionally idle body,
        // not a deployment or an assumed future credit. Keep it out of an
        // incomplete/recovered board and out of a presently visible boss round.
        const auto balance_retention = spa::round_balance_retention(m_selected_strategy_id, funds);
        const bool ordinary_balance_rule_known = balance_retention && !balance_retention->carries;
        m_policy.economy_scenario_horizon_round = m_expected_boss_round;
        m_policy.one_coin_bank_allowed = ordinary_balance_rule_known && m_selected_strategy_id != "band_substitute" &&
                                         field_poses_complete && field_population == 8 && !m_field_checkpoint_loaded &&
                                         !m_bench_recount_pending && !m_pending_spatial_orientation &&
                                         state.get("free_refreshes_known", false) && m_failed_replacements.empty() &&
                                         !m_pending_field_replacement && m_pending_promotion.empty() &&
                                         m_pending_pact_recruitment.empty() && m_unreconciled_sale.empty() &&
                                         !state.get("preparation_boss_bar_visible", false) &&
                                         !(m_spatial_boss_phase && m_spatial_boss_phase_round == round);
        if (state.get("upgrade_price_known", false)) {
            if (const auto reservation = spa::fixed_growth_purchase_reservation(
                    m_policy,
                    funds,
                    state.get("upgrade_price", -1),
                    offers,
                    catalog)) {
                auto budget = basic_info_with_what("SPAFixedGrowthPurchaseReservation");
                budget["details"] =
                    json::object { { "run_id", m_run_id },
                                   { "round", round },
                                   { "name", reservation->name },
                                   { "price", reservation->price },
                                   { "current_layers", reservation->current_layers },
                                   { "scenario_horizon_round", reservation->scenario_horizon_round },
                                   { "fixed_gain_per_trigger", reservation->fixed_gain_per_trigger },
                                   { "remaining_phase_triggers", reservation->remaining_phase_triggers },
                                   { "fixed_layer_additions_if_retained",
                                     reservation->fixed_layer_additions_if_retained },
                                   { "placement_required", reservation->placement_required },
                                   { "reserve_before_upgrade", reservation->reserve_before_upgrade },
                                   { "forecast_is_scenario", true },
                                   { "boss_round_verified", false },
                                   { "combat_strength_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, budget);
            }
        }
        // The live roster and the current quote determine whether to open the
        // next card tier, chase a promotion, or use this round's funds first.
        if (m_policy.dynamic_decision_mode && state.get("upgrade_price_known", false) &&
            !m_purchase_reserved_after_sale && !m_dynamic_upgrade_done_rounds.contains(round)) {
            const int upgrade_price = state.get("upgrade_price", -1);
            const int current_level = m_policy.shop_level;
            const int target_level = spa::dynamic_target_shop_level(m_policy, catalog);
            std::optional<spa::ShopUpgradeHold> shop_hold;
            const bool upgrade_window_needed =
                spa::should_upgrade_dynamic_shop(m_policy, funds, upgrade_price, offers, catalog, &shop_hold);
            if (shop_hold) {
                if (shop_hold->next_round_upgrade_price) {
                    m_policy.shop_discount_wait_level = current_level;
                    m_policy.shop_discount_wait_round = round;
                }
                auto deferred = basic_info_with_what("SPAShopUpgradeTimingReviewed");
                deferred["details"] =
                    json::object { { "round", round },
                                   { "primary_pact", m_policy.primary_pact },
                                   { "shop_level", current_level },
                                   { "funds", funds },
                                   { "observed_upgrade_price", upgrade_price },
                                   { "estimated_next_round_upgrade_price",
                                     shop_hold->next_round_upgrade_price.value_or(-1) },
                                   { "promotion_target", shop_hold->promotion_target },
                                   { "promotion_recruit_budget", shop_hold->promotion_recruit_budget },
                                   { "promotion_reward_tier", std::min(6, current_level + 1) },
                                   { "reason", shop_hold->reason } };
                callback(AsstMsg::SubTaskExtraInfo, deferred);
            }
            if (upgrade_window_needed) {
                spa::ShopTaskPlugin upgrade(m_callback, m_inst, TaskType);
                upgrade.set_fast_mode(m_policy.basic_mode || m_policy.dynamic_decision_mode);
                upgrade.set_retry_times(0);
                upgrade.set_task_id(m_task_id);
                upgrade.set_task_ptr(this);
                if (!upgrade.configure_upgrade(upgrade_price, funds, round) || !upgrade.run()) {
                    auto failed = basic_info_with_what("SPAShopUpgradeBlocked");
                    failed["details"] = json::object { { "round", round },
                                                       { "price", upgrade_price },
                                                       { "funds", funds },
                                                       { "target_shop_level", target_level },
                                                       { "reason", "Dynamic target-tier upgrade was not reconciled" },
                                                       { "run_victory_verified", false } };
                    callback(AsstMsg::SubTaskExtraInfo, failed);
                    auto deferred = basic_info_with_what("SPAShopUpgradeDeferred");
                    deferred["details"] =
                        json::object { { "round", round },
                                       { "price", upgrade_price },
                                       { "funds", funds },
                                       { "target_shop_level", target_level },
                                       { "reason", "Dynamic upgrade is optional; continue with the current roster" },
                                       { "battle_started", false },
                                       { "run_victory_verified", false } };
                    callback(AsstMsg::SubTaskExtraInfo, deferred);
                    m_dynamic_upgrade_done_rounds.insert(round);
                    m_spending_reviewed = false;
                    m_spending_reason.clear();
                    continue;
                }
                m_dynamic_upgrade_done_rounds.insert(round);
                m_policy.shop_level = std::min(6, current_level + 1);
                auto upgraded = basic_info_with_what("SPAShopUpgradeReconciled");
                upgraded["details"] = json::object { { "round", round },
                                                     { "price", upgrade_price },
                                                     { "funds_before", funds },
                                                     { "target_shop_level", target_level },
                                                     { "shop_level_before", current_level },
                                                     { "shop_level_after", m_policy.shop_level },
                                                     { "schedule", "dynamic_target_window" },
                                                     { "primary_pact", m_policy.primary_pact },
                                                     { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, upgraded);
                continue;
            }
        }
        auto decision = spa::choose_preparation(m_policy, funds, offers, catalog);
        bool purchase_reserved_after_sale = false;
        if (m_purchase_reserved_after_sale) {
            const auto& [sold_round, reserved] = *m_purchase_reserved_after_sale;
            const auto same_slot = [&](const auto& offer) {
                return offer.right_slot == reserved.right_slot;
            };
            const auto quoted = std::find_if(offers.begin(), offers.end(), same_slot);
            if (sold_round == round && reserved.price <= funds && m_policy.bench_occupied >= 0 &&
                m_policy.bench_occupied < 10 && quoted != offers.end() && quoted->name == reserved.name &&
                quoted->price == reserved.price && std::none_of(std::next(quoted), offers.end(), same_slot)) {
                decision = { spa::PreparationAction::Buy,
                             reserved.name,
                             reserved.price,
                             reserved.right_slot,
                             "Buy the reviewed lineup improvement after a verified bench sale",
                             m_policy.primary_pact };
                purchase_reserved_after_sale = true;
            }
            m_purchase_reserved_after_sale.reset();
        }
        const int bench_before_funding = m_policy.bench_occupied;
        const auto funding = purchase_reserved_after_sale
                                 ? std::nullopt
                                 : spa::choose_funding_purchase(m_policy, funds, offers, catalog);
        // The source panel verifies the actual one- or two-coin credit before use.
        const auto coin_funding = !purchase_reserved_after_sale && m_equipment_candidates.contains("盟约之币") &&
                                          !m_cash_equipment_attempted_rounds.contains(round)
                                      ? spa::choose_funding_purchase(m_policy, funds, offers, catalog, 2)
                                      : std::nullopt;
        const auto& equipment_funding = coin_funding ? coin_funding : funding;
        if (equipment_funding && handle_cash_equipment(state, equipment_funding->deficit)) {
            continue;
        }
        if (funding && handle_full_bench_sale(state, funding->deficit)) {
            const bool sale_verified = m_policy.bench_occupied == bench_before_funding - 1;
            if (sale_verified) {
                m_purchase_reserved_after_sale = std::pair(round, funding->offer);
            }
            auto funded = basic_info_with_what("SPAKeyPurchaseFundingAttempted");
            funded["details"] = json::object { { "round", round },
                                               { "name", funding->offer.name },
                                               { "deficit", funding->deficit },
                                               { "sale_verified", sale_verified },
                                               { "purchase_committed", false },
                                               { "reobserve_shop_before_purchase", true } };
            callback(AsstMsg::SubTaskExtraInfo, funded);
            continue;
        }
        if (decision.action == spa::PreparationAction::Freeze && m_policy.primary_locked_after_activation) {
            int deficit = 0;
            for (const auto& offer : offers) {
                if (offer.price <= funds) {
                    continue;
                }
                // Only fund a card whose own isolated decision warrants
                // preservation. A cheap faction filler is not a sale goal.
                const auto isolated = spa::choose_preparation(m_policy, funds, { offer }, catalog);
                if (isolated.action == spa::PreparationAction::Freeze) {
                    const int shortfall = offer.price - funds;
                    deficit = deficit == 0 ? shortfall : std::min(deficit, shortfall);
                }
            }
            if (deficit > 0 && handle_full_bench_sale(state, deficit)) {
                continue;
            }
        }
        if (!m_policy.full_field_offer_improvements.empty()) {
            json::object improvements;
            for (const auto& [name, value] : m_policy.full_field_offer_improvements) {
                improvements[name] = value;
            }
            auto fit = basic_info_with_what("SPAFullFieldOfferFit");
            fit["details"] = json::object { { "round", round },
                                            { "improvements", std::move(improvements) },
                                            { "fit_is_planning_only", true },
                                            { "purchase_committed", false } };
            callback(AsstMsg::SubTaskExtraInfo, fit);
        }
        const bool before_refresh = decision.action == spa::PreparationAction::Refresh;
        const bool refresh_strands_last_coin = before_refresh && !decision.free_refresh && decision.price > 0 &&
                                               funds - decision.price == 1 && ordinary_balance_rule_known;
        const bool discount_ready = equipment_discount_window && !equipment_offers.empty() &&
                                    !m_preparation_instruction_attempts.contains(round) &&
                                    spa::active_strategy_ready(image);
        const auto prospective_equipment_price = [&](int quoted_price) {
            return discount_ready && quoted_price > 0 ? quoted_price - 1 : quoted_price;
        };
        std::optional<spa::IdentityTagTarget> identity_tag_target;
        if ((decision.action == spa::PreparationAction::Review || decision.action == spa::PreparationAction::Freeze ||
             before_refresh || decision.banked_value_purchase) &&
            (funds > 0 || review_free_equipment) && !equipment_offers.empty()) {
            // Operators remain the first purchase. A reviewed, affordable
            // persistent item is preferable to discarding the final coin.
            const auto [assignment_context, recipients] = equipment_assignment_inputs();
            if (!m_equipment_candidates.contains("随身身份牌") &&
                (m_mode != "joint" || state.get("preparation_seconds_remaining", 0) >= 25)) {
                identity_tag_target = spa::choose_identity_tag_target(
                    m_policy.primary_pact,
                    m_policy.support_pacts,
                    m_policy.disabled_pacts,
                    m_policy.observed_pact_layers,
                    recipients,
                    (ordinary_balance_rule_known && funds == 1) || review_free_equipment);
            }
            const auto pager_target = std::find_if(recipients.begin(), recipients.end(), [&](const auto& target) {
                return spa::safe_pact_recruitment_recipient(
                    target.name,
                    m_policy.primary_pact,
                    target.slots_known,
                    target.equipped);
            });
            const bool pager_usable = m_policy.primary_pact != "auto" && !m_policy.primary_pact.empty() &&
                                      m_policy.bench_occupied <= 8 && pager_target != recipients.end();
            const auto toy_target = std::find_if(recipients.begin(), recipients.end(), [](const auto& target) {
                return target.slots_known &&
                       target.equipped.size() < static_cast<size_t>(spa::equipment_capacity(target.name, target.elite));
            });
            const int equipment_boss_round = state.get("preparation_boss_bar_visible", false) ||
                                                     (m_spatial_boss_phase && m_spatial_boss_phase_round == round)
                                                 ? round
                                             : m_expected_boss_round > 0 ? m_expected_boss_round
                                                                         : 20;
            const bool delayed_cash_ready =
                !m_pending_delayed_cash_equipment && !m_delayed_cash_equipment_attempted_rounds.contains(round);
            const bool hold_one_cash_toy = ordinary_balance_rule_known && funds == 1 &&
                                           m_pending_delayed_cash_equipment &&
                                           m_pending_delayed_cash_equipment->get("source_round", -1) == round &&
                                           m_pending_delayed_cash_equipment->get("consumption_verified", false);
            const auto profitable_toy_use_round = [&](const spa::EquipmentProfile& item,
                                                      int price) -> std::optional<int> {
                if (toy_target == recipients.end() || m_expected_boss_round <= round ||
                    m_equipment_candidates.contains(item.name) ||
                    (!delayed_cash_ready && !(hold_one_cash_toy && item.name == "见钱眼开玩偶"))) {
                    return std::nullopt;
                }
                spa::EquipmentContext context;
                context.funds = funds;
                context.round = delayed_cash_ready ? round : round + 1;
                context.boss_round = equipment_boss_round;
                context.shop_level = m_policy.shop_level;
                context.target_deployed = true;
                auto offer = item;
                offer.price = price;
                return spa::profitable_next_round_cash_offer(context, offer) ? std::optional(context.round)
                                                                             : std::nullopt;
            };
            // Shop quality is not known here, so reserve a future round for
            // normal projection. The actual complete effect is re-read on use.
            const auto projection_target = direct_promotion_recipient();
            const bool projection_usable = projection_target && m_expected_boss_round > round &&
                                           !state.get("preparation_boss_bar_visible", false) &&
                                           !m_equipment_candidates.contains("博士投影") &&
                                           !m_direct_promotion_attempted_rounds.contains(round);
            std::erase_if(equipment_offers, [&](const auto& item) {
                const int quoted_price = prospective_equipment_price(item.price);
                const bool bank_coin = item.name == "盟约之币" && round < equipment_boss_round;
                const bool passbook = item.name == "定期存折" && m_expected_boss_round > 0 && !recipients.empty() &&
                                      !m_equipment_candidates.contains(item.name) &&
                                      spa::profitable_passbook_purchase(round, equipment_boss_round, quoted_price);
                const bool pager = item.name == "寻呼模块" && pager_usable;
                const bool toy = profitable_toy_use_round(item, quoted_price).has_value();
                const bool projection = projection_usable && item.name == "博士投影";
                const bool tag = identity_tag_target && item.name == "随身身份牌" && quoted_price <= 1;
                const bool reviewed_egir_blade = item.name == "阿戈尔重刃" && m_policy.primary_pact == "阿戈尔";
                const bool boss_damage = equipment_boss_round == round && item.role == spa::EquipmentRole::Damage;
                const bool freeze_equipment =
                    assignment_context.kjerag_freeze_growth_active &&
                    (item.name == "谢拉格不融冰" || item.role == spa::EquipmentRole::SkillRecovery);
                const bool uses_expiring_balance =
                    ordinary_balance_rule_known && quoted_price > 0 && quoted_price == funds && !decision.free_refresh;
                return (before_refresh && !refresh_strands_last_coin && !bank_coin && !pager && !reviewed_egir_blade &&
                        !toy && !projection && !passbook && !uses_expiring_balance && !boss_damage &&
                        !freeze_equipment) ||
                       (item.consumable && !bank_coin && !pager && !toy && !projection && !tag && !passbook) ||
                       item.role == spa::EquipmentRole::Unknown ||
                       (!bank_coin && !pager && !toy && !projection && !tag && !passbook &&
                        !spa::choose_equipment_assignment(assignment_context, { item.name }, recipients).found);
            });
            const bool deployed = std::any_of(m_policy.owned.begin(), m_policy.owned.end(), [](const auto& item) {
                return item.second.deployed;
            });
            spa::EquipmentDecision gear;
            int gear_price = 0;
            for (const auto& offer : equipment_offers) {
                spa::EquipmentContext context;
                context.funds = funds;
                context.round = round;
                context.boss_round = equipment_boss_round;
                context.target_deployed = deployed;
                context.shop_level = m_policy.shop_level;
                const auto assignment =
                    spa::choose_equipment_assignment(assignment_context, { offer.name }, recipients);
                if (assignment.found) {
                    const auto target = std::find_if(recipients.begin(), recipients.end(), [&](const auto& member) {
                        return member.name == assignment.recipient;
                    });
                    if (target == recipients.end()) {
                        continue;
                    }
                    context.target_is_carry = target->carry;
                    context.primary_active = target->active_own_pact;
                    context.intentional_redeploy_loop = target->deliberate_redeploy;
                    context.freeze_engine_assignment =
                        assignment_context.kjerag_freeze_growth_active &&
                        (offer.name == "谢拉格不融冰" ||
                         (offer.role == spa::EquipmentRole::SkillRecovery && target->freeze_skill_engine));
                }
                else if (offer.name == "寻呼模块" && pager_usable) {
                    context.primary_active = pager_target->active_own_pact;
                    context.target_is_carry = pager_target->carry;
                }
                else if (spa::is_deferred_cash_equipment(offer.name)) {
                    const auto use_round = profitable_toy_use_round(offer, prospective_equipment_price(offer.price));
                    if (!use_round) {
                        continue;
                    }
                    context.target_deployed = true;
                    context.round = *use_round;
                }
                else if (offer.name == "博士投影" && projection_usable) {
                    context.target_deployed = true;
                    context.direct_promotion_usable = true;
                }
                else if (offer.name == "随身身份牌" && identity_tag_target) {
                    context.target_deployed = true;
                }
                else if (!spa::is_immediate_cash_equipment(offer.name)) {
                    continue;
                }
                auto priced_offer = offer;
                priced_offer.price = prospective_equipment_price(offer.price);
                context.free_supply =
                    priced_offer.price == 0 && (discount_ready || m_equipment_discount_round == round);
                const auto candidate = spa::choose_equipment(context, { priced_offer });
                if (candidate.buy && (!gear.buy || candidate.score > gear.score ||
                                      (candidate.score == gear.score && priced_offer.price < gear_price))) {
                    gear = candidate;
                    gear_price = priced_offer.price;
                }
            }
            if (gear.buy && equipment_slots.contains(gear.name)) {
                const auto priced =
                    std::find_if(equipment_offers.begin(), equipment_offers.end(), [&](const auto& item) {
                        return item.name == gear.name;
                    });
                decision.action = spa::PreparationAction::Buy;
                decision.name = gear.name;
                decision.price = priced->price;
                decision.right_slot = equipment_slots.at(gear.name);
                decision.reason =
                    gear.name == "盟约之币"   ? "Use the verified Pact Coin purchase/redeem value"
                    : gear.name == "定期存折" ? "Buy the passbook while projected round accrual exceeds its price"
                    : spa::is_deferred_cash_equipment(gear.name)
                        ? (delayed_cash_ready ? "Convert this purchase into future income with a verified toy use"
                                              : "Spend the final expiring coin on a second toy for a later preparation")
                    : refresh_strands_last_coin
                        ? "Buy usable equipment before a paid refresh leaves only one ordinary coin"
                    : before_refresh
                        ? "Secure a reviewed output item or targeted pact recruitment before losing it to refresh"
                        : "Use remaining funds for reviewed equipment";
                decision.is_equipment = true;
                decision.banked_value_purchase = false;
            }
        }
        std::string full_bench_refresh_quote_source;
        if (decision.action == spa::PreparationAction::Review && confirmed_full_bench && field_poses_complete &&
            ordinary_balance_rule_known && m_policy.dynamic_decision_mode && m_policy.allow_refresh &&
            !m_policy.shop_frozen_this_round && !m_pending_field_replacement && !m_pending_spatial_orientation &&
            !m_bench_recount_pending && (m_mode != "joint" || state.get("preparation_seconds_remaining", 0) >= 30) &&
            !m_full_bench_refresh_attempts.contains(round)) {
            const bool free_refresh = state.get("free_refreshes_known", false) && m_policy.free_refreshes > 0;
            const bool observed_paid_quote = state.get("paid_refresh_price_known", false);
            const int refresh_price = observed_paid_quote ? state.get("paid_refresh_price", -1) : 1;
            const bool can_pay =
                !free_refresh && refresh_price > 0 && funds >= refresh_price + m_policy.minimum_recruit_price;
            // The whole-screen OCR misses the small price badge on busy
            // boards. Reuse the same crop that the executor verifies again.
            const bool paid_refresh =
                can_pay && (observed_paid_quote || spa::paid_shop_refresh_price(image) == refresh_price);
            if (free_refresh || paid_refresh) {
                m_full_bench_refresh_attempts.insert(round);
                full_bench_refresh_quote_source = free_refresh          ? "free_counter"
                                                  : observed_paid_quote ? "observer"
                                                                        : "price_badge_crop";
                decision = { spa::PreparationAction::Refresh,
                             "刷新",
                             free_refresh ? 0 : refresh_price,
                             -1,
                             "Full bench with funds remaining; inspect one refreshed shop for equipment or trades",
                             m_policy.primary_pact,
                             free_refresh };
            }
        }
        auto info = basic_info_with_what("SPAAutomationDecision");
        info["details"] = json::object {
            { "run_id", m_run_id },
            { "round", round },
            { "funds", funds },
            { "round_cash_retention_known", balance_retention.has_value() },
            { "round_cash_carries", balance_retention && balance_retention->carries },
            { "round_cash_retained_if_ready", balance_retention ? balance_retention->retained : -1 },
            { "next_round_interest_if_ready", balance_retention ? balance_retention->interest : -1 },
            { "primary_pact", m_policy.primary_pact },
            { "selected_pact", decision.selected_pact },
            { "planned_support_pacts", spa::planning_support_pacts(m_policy, offers, catalog) },
            { "dynamic_primary_pact", m_dynamic_primary_pact },
            { "primary_locked_after_activation", m_policy.primary_locked_after_activation },
            { "primary_activation_required", primary_activation_required },
            { "primary_activation_members",
              modeled_formation.activation_members.contains(m_policy.primary_pact)
                  ? modeled_formation.activation_members.at(m_policy.primary_pact)
                  : 0 },
            { "primary_active_by_formation",
              modeled_formation.complete && std::find(
                                                modeled_formation.active_pacts.begin(),
                                                modeled_formation.active_pacts.end(),
                                                m_policy.primary_pact) != modeled_formation.active_pacts.end() },
            { "name", decision.name },
            { "identity_tag_target_pact",
              decision.is_equipment && decision.name == "随身身份牌" && identity_tag_target ? identity_tag_target->pact
                                                                                            : std::string() },
            { "is_equipment", decision.is_equipment },
            { "cost", decision.price },
            { "preparation_growth_score", decision.growth_score },
            { "reason", decision.reason },
            { "decision_only", m_decision_only },
            { "action",
              decision.action == spa::PreparationAction::Buy       ? "buy"
              : decision.action == spa::PreparationAction::Refresh ? "refresh"
              : decision.action == spa::PreparationAction::Freeze  ? "freeze"
                                                                   : "review" },
            { "free_refresh", decision.free_refresh },
            { "full_bench_refresh_quote_source", full_bench_refresh_quote_source },
            { "banked_value_purchase", decision.banked_value_purchase },
            { "banking_board_verified", m_policy.one_coin_bank_allowed },
            { "economy_scenario_horizon_round", m_policy.economy_scenario_horizon_round },
            { "free_refreshes_remaining", m_policy.free_refreshes }
        };
        callback(AsstMsg::SubTaskExtraInfo, info);
        if (m_decision_only) {
            return true;
        }
        if (decision.action == spa::PreparationAction::Review) {
            const auto modeled_current = spa::evaluate_formation(
                modeled_roster,
                catalog,
                spa::pact_requirements(),
                spa::enabled_formation_pacts(m_enabled_core_pacts, m_policy.disabled_pacts));
            const bool primary_active_now =
                modeled_current.complete && std::find(
                                                modeled_current.active_pacts.begin(),
                                                modeled_current.active_pacts.end(),
                                                m_policy.primary_pact) != modeled_current.active_pacts.end();
            // Missing activation is a development observation, never a
            // request to abandon a live game at an arbitrary round number.
            if (!primary_active_now) {
                const auto activation_rule = std::find_if(pact_rules.begin(), pact_rules.end(), [&](const auto& item) {
                    return item.name == m_policy.primary_pact;
                });
                auto reroll = basic_info_with_what("SPAPrimaryFormationPending");
                reroll["details"] =
                    json::object { { "run_id", m_run_id },
                                   { "round", round },
                                   { "primary_pact", m_policy.primary_pact },
                                   { "shop_level", m_policy.shop_level },
                                   { "reason", "Continue developing the current run from its actual shop and board" },
                                   { "formation_complete", modeled_current.complete },
                                   { "activation_required",
                                     activation_rule != pact_rules.end() ? activation_rule->members : 0 },
                                   { "activation_members",
                                     modeled_current.activation_members.contains(m_policy.primary_pact)
                                         ? modeled_current.activation_members.at(m_policy.primary_pact)
                                         : 0 },
                                   { "active_pacts", modeled_current.active_pacts },
                                   { "reopen_required", false },
                                   { "continue_current_run", true },
                                   { "battle_started", false },
                                   { "run_victory_verified", false } };
                callback(AsstMsg::SubTaskExtraInfo, reroll);
            }
            info = basic_info_with_what("SPAAutomationPreparationDeferred");
            info["details"] = json::object { { "run_id", m_run_id },
                                             { "round", round },
                                             { "funds", funds },
                                             { "round_cash_lost_if_ready",
                                               ordinary_balance_rule_known ? funds
                                               : balance_retention         ? 0
                                                                           : -1 },
                                             { "reason", decision.reason },
                                             { "battle_started", false },
                                             { "run_victory_verified", false },
                                             { "continue_current_roster", true } };
            callback(AsstMsg::SubTaskExtraInfo, info);
            // Pass the actual balance rule to the Ready gate.
            m_spending_reviewed = true;
            m_spending_reason = confirmed_full_bench && funds > 0
                                    ? "Full bench has no verified safe trade. " +
                                          spa::round_balance_review_reason(m_selected_strategy_id, funds)
                                    : spa::round_balance_review_reason(m_selected_strategy_id, funds);
            m_shopping_complete = true;
            if (funds == 1 && ordinary_balance_rule_known && m_policy.dynamic_decision_mode && field_population >= 0 &&
                field_population < 8 && round > 0 && round < m_expected_boss_round &&
                state.get("free_refreshes_known", false) &&
                std::any_of(
                    offers.begin(),
                    offers.end(),
                    [&](const auto& offer) {
                        return spa::is_one_coin_bank_offer(offer) && !m_policy.owned.contains(offer.name);
                    })) {
                m_last_coin_review_after_deployment_round = round;
            }
            // A review decision does not mutate the shop, bench, deployment
            // counter, or funds. Reusing this authoritative preparation
            // snapshot on the next iteration avoids a second full-screen OCR
            // pass before the Ready gate (roughly 2--4 seconds on MuMu).
            // The cache is consumed exactly once at the top of the loop and
            // remains limited to the phase-one/basic runner.
            if (m_policy.basic_mode) {
                m_cached_deployment_observation = state_storage;
            }
            continue;
        }
        spa::ShopTaskPlugin shop(m_callback, m_inst, TaskType);
        shop.set_fast_mode(
            (m_policy.basic_mode || m_policy.dynamic_decision_mode) && !(decision.is_equipment && decision.price == 0));
        shop.set_prevalidated_offer(m_policy.basic_mode && decision.action == spa::PreparationAction::Buy);
        shop.set_retry_times(0);
        shop.set_task_id(m_task_id);
        shop.set_task_ptr(this);
        if (spa::prefer_higher_tier_strategy_refresh(
                m_selected_strategy_id,
                m_policy.shop_level,
                funds,
                m_strategy_refresh_attempts[round],
                decision.action == spa::PreparationAction::Refresh && !decision.free_refresh && decision.price == 1) &&
            !m_strategy_refresh_blocked_rounds.contains(round)) {
            // Count attempts conservatively before input. An ambiguous payment
            // is followed by a new observation, never another paid action using
            // this frame or a blind repeat of the command.
            ++m_strategy_refresh_attempts[round];
            const bool verified = shop.configure_strategy_refresh(funds, round, m_policy.shop_level) && shop.run();
            if (!verified) {
                m_strategy_refresh_blocked_rounds.insert(round);
            }
            auto instruction = basic_info_with_what("SPAAutomationStrategyRefresh");
            instruction["details"] = json::object { { "strategy_id", m_selected_strategy_id },
                                                    { "round", round },
                                                    { "attempts_this_round", m_strategy_refresh_attempts[round] },
                                                    { "effect_verified", verified },
                                                    { "ordinary_free_charge_consumed", false },
                                                    { "next_action_requires_observation", true } };
            callback(AsstMsg::SubTaskExtraInfo, instruction);
            continue;
        }
        if (decision.action == spa::PreparationAction::Buy && decision.is_equipment && decision.price > 0 &&
            discount_ready) {
            m_preparation_instruction_attempts.insert(round);
            if (shop.configure_equipment_discount(decision.name, decision.price, funds, round, decision.right_slot) &&
                shop.run()) {
                m_equipment_discount_round = round;
            }
            // Restart the normal shop planner from actual offers; the
            // discounted estimate never authorizes a purchase.
            observer.expect_unchanged_preparation(round);
            continue;
        }
        std::optional<spa::OwnedOperator> purchased_owned;
        const bool purchased_elite = m_policy.tactical_elite_purchase_pending && !decision.is_equipment &&
                                     decision.action == spa::PreparationAction::Buy;
        if (decision.action == spa::PreparationAction::Buy && !decision.is_equipment) {
            purchased_owned =
                m_policy.owned.contains(decision.name) ? m_policy.owned.at(decision.name) : spa::OwnedOperator {};
            if (!spa::add_purchased_operator(*purchased_owned, purchased_elite)) {
                // No input: an ambiguous old mixed-form row needs a physical
                // observation before another copy may change this identity.
                m_blocked_purchases.emplace(round, decision.name);
                LogWarn << "SPA purchase deferred for unknown operator form counts" << decision.name;
                continue;
            }
        }
        const bool configured = decision.action == spa::PreparationAction::Buy
                                    ? shop.configure(decision.name, decision.price, funds, round, decision.right_slot)
                                : decision.action == spa::PreparationAction::Freeze
                                    ? shop.configure_freeze(funds, round)
                                    : shop.configure_refresh(decision.price, funds, round);
        if (!configured || !shop.run()) {
            if (shop.equipment_capacity_rejection_verified() && decision.is_equipment) {
                m_equipment_capacity_blocked_round = round;
                m_equipment_inventory_dirty = true;
                m_equipment_inventory_scan_rounds.erase(round);
                m_cached_deployment_observation.reset();
                continue;
            }
            if (shop.bench_capacity_rejection_verified() && !decision.is_equipment) {
                m_policy.bench_occupied = 10;
                m_bench_recount_pending = false;
                m_cached_deployment_observation.reset();
                continue;
            }
            if (shop.input_reconciliation_required()) {
                // Leave this selected card intact for transaction recovery.
                // Never continue shopping or launch combat through its modal.
                if (lifecycle) {
                    lifecycle->note_input_reconciliation("SPAPurchaseReconciliationRequired");
                }
                return false;
            }
            if (decision.action == spa::PreparationAction::Buy && !need_exit()) {
                m_blocked_purchases.emplace(round, decision.name);
                auto failed = basic_info_with_what("SPAShopPurchaseDeferred");
                failed["details"] = json::object { { "name", decision.name },
                                                   { "round", round },
                                                   { "is_equipment", decision.is_equipment },
                                                   { "payment_verified", false },
                                                   { "resumable", true } };
                callback(AsstMsg::SubTaskExtraInfo, failed);
                continue;
            }
            if (!full_bench_refresh_quote_source.empty() && !need_exit()) {
                // Reobserve the shop and wallet after this one bounded try.
                continue;
            }
            return false;
        }
        if (decision.action == spa::PreparationAction::Buy && decision.is_equipment) {
            const bool discounted_quote_seen = m_equipment_discount_round == round;
            m_equipment_discount_round = -1;
            m_equipment_candidates.insert(decision.name);
            if (decision.name == "盟约之币") {
                m_cash_equipment_acquired_round = round;
            }
            auto receipt = basic_info_with_what("SPAShopEquipmentPurchaseReconciled");
            receipt["details"] = json::object { { "name", decision.name },
                                                { "round", round },
                                                { "funds", funds - decision.price },
                                                { "bench_occupied", m_policy.bench_occupied },
                                                { "equipped_verified", false },
                                                { "after_verified_instruction_quote", discounted_quote_seen },
                                                { "purchase_price", decision.price },
                                                { "run_id", m_run_id },
                                                { "roster", roster_snapshot() },
                                                { "equipment_candidates", equipment_candidates_snapshot() },
                                                { "cash_equipment_acquired_round", m_cash_equipment_acquired_round } };
            callback(AsstMsg::SubTaskExtraInfo, receipt);
            observer.expect_unchanged_preparation(round);
            continue;
        }
        if (decision.action == spa::PreparationAction::Buy) {
            if (!m_policy.owned.contains(decision.name) || m_policy.owned.at(decision.name).copies == 0) {
                // A newly bought unit receives no equipment from another copy.
                m_equipped_items[decision.name] = {};
            }
            m_policy.owned[decision.name] = *purchased_owned;
            m_policy.tactical_elite_purchase_pending = false;
            m_recent_bench_acquisition = decision.name;
            m_unavailable_replacement_sources.erase(decision.name);
            ++m_policy.bench_occupied;
            record_acquired_equipment_hints(decision.name, purchased_elite, round);
            const auto promotion_override = m_policy.promotion_copies.find(decision.name);
            const int promotion_required = promotion_override == m_policy.promotion_copies.end()
                                               ? spa::promotion_copies_required(decision.name)
                                               : promotion_override->second;
            const auto counts = spa::owned_form_counts(m_policy.owned.at(decision.name));
            const bool promotion_pending = !purchased_elite && counts && counts->initial >= promotion_required;
            if (promotion_pending) {
                m_pending_promotion = decision.name;
            }
            // Persistable receipts must precede the next action: a deployment failure
            // must not make the caller replay an already verified purchase.
            auto receipt = basic_info_with_what("SPAAutomationPurchaseReconciled");
            receipt["details"] = json::object { { "run_id", m_run_id },
                                                { "round", round },
                                                { "name", decision.name },
                                                { "banked_value_purchase", decision.banked_value_purchase },
                                                { "funds", funds - decision.price },
                                                { "bench_occupied", m_policy.bench_occupied },
                                                { "roster", roster_snapshot() },
                                                { "promotion_pending", m_pending_promotion },
                                                { "tactical_elite_purchase_pending", false },
                                                { "purchased_elite_from_tactical_decision", purchased_elite },
                                                { "equipment_slots", equipment_slots_snapshot() },
                                                { "equipment_candidates", equipment_candidates_snapshot() },
                                                { "cash_equipment_acquired_round", m_cash_equipment_acquired_round },
                                                { "requires_live_revalidation", true },
                                                { "run_victory_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, receipt);
            if (promotion_pending) {
                auto promotion = basic_info_with_what("SPAPromotionReconciliationRequired");
                promotion["details"] = json::object { { "run_id", m_run_id },
                                                      { "round", round },
                                                      { "name", decision.name },
                                                      { "copies", counts->initial },
                                                      { "required_copies", promotion_required },
                                                      { "temporary_shop_expected", true },
                                                      { "temporary_offer_count", 3 },
                                                      { "temporary_offer_cost", 0 },
                                                      { "battle_started", false },
                                                      { "run_victory_verified", false },
                                                      { "resumable", true } };
                callback(AsstMsg::SubTaskExtraInfo, promotion);
                // The game has now entered the synthesis/temporary-shop
                // state. Keep the same task alive and reconcile its three free
                // elite choices before parsing the next ordinary shop.
                continue;
            }
            observer.expect_unchanged_preparation(round);
        }
        else if (decision.action == spa::PreparationAction::Refresh && decision.free_refresh) {
            if (m_policy.free_refreshes <= 0) {
                return false;
            }
            --m_policy.free_refreshes;
            auto receipt = basic_info_with_what("SPAShopFreeRefreshReconciled");
            receipt["details"] = json::object { { "round", round },
                                                { "funds", funds },
                                                { "free_refreshes_remaining", m_policy.free_refreshes },
                                                { "cost", 0 },
                                                { "shop_changed_verified", true } };
            callback(AsstMsg::SubTaskExtraInfo, receipt);
            observer.expect_unchanged_preparation(round);
        }
        else if (decision.action == spa::PreparationAction::Refresh) {
            observer.expect_unchanged_preparation(round);
        }
        else if (decision.action == spa::PreparationAction::Freeze) {
            m_policy.shop_frozen_this_round = true;
            auto receipt = basic_info_with_what("SPAShopFreezeReconciled");
            receipt["details"] = json::object { { "round", round },
                                                { "funds", funds },
                                                { "shop_frozen", true },
                                                { "refresh_suppressed_next_round", true } };
            callback(AsstMsg::SubTaskExtraInfo, receipt);
        }
    }
    // Reaching the bounded preparation-action budget is a resumable
    // non-terminal state.  Returning false here made the host report a
    // zero-round "failure" even though no battle had started.  Persist the
    // observed page and let the caller resume from the same preparation
    // state; only RoundTaskPlugin/settlement may produce a combat result.
    auto info = basic_info_with_what("SPAAutomationNonTerminalState");
    info["details"] =
        json::object { { "reason", "Preparation action budget reached; resume from the live preparation page" },
                       { "phase", "preparation" },
                       { "battle_started", false },
                       { "combat_result", "not_started" },
                       { "run_victory_verified", false },
                       { "resumable", true } };
    callback(AsstMsg::SubTaskExtraInfo, info);
    return true;
}

json::value asst::StrongholdProtocolTask::pending_replacement_snapshot() const
{
    if (!m_pending_field_replacement) {
        return nullptr;
    }
    const auto& pending = *m_pending_field_replacement;
    return json::object { { "incoming", pending.incoming },
                          { "outgoing", pending.outgoing },
                          { "x", pending.previous.x },
                          { "y", pending.previous.y },
                          { "verification_only", true },
                          { "drag_replay_allowed", false },
                          { "committed_facing",
                            pending.committed_facing ? std::string(1, *pending.committed_facing) : std::string() } };
}

json::array asst::StrongholdProtocolTask::roster_snapshot() const
{
    json::array roster;
    for (const auto& [name, owned] : m_policy.owned) {
        roster.emplace_back(spa::owned_operator_snapshot(name, owned));
    }
    return roster;
}

json::array asst::StrongholdProtocolTask::field_position_snapshot() const
{
    json::array points;
    for (const auto& [name, point] : m_auto_deploy_positions) {
        if (point.first < 180 || point.first >= 1000 || point.second < 140 || point.second >= 510) {
            continue;
        }
        const bool verified = m_verified_field_pose_points.contains(point);
        points.emplace_back(
            json::object { { "name", name },
                           { "x", point.first },
                           { "y", point.second },
                           { "field_pose_verified", verified },
                           { "committed_facing",
                             verified && m_committed_facings.contains(point)
                                 ? std::string(1, m_committed_facings.at(point))
                                 : std::string() },
                           { "facing_pixel_verified", false },
                           { "coordinate_frame", verified ? "collapsed" : "legacy_held" } });
    }
    return points;
}

json::array asst::StrongholdProtocolTask::deployment_point_snapshot() const
{
    json::array points;
    for (const auto& [x, y] : m_auto_deploy_used_cells) {
        json::object point { { "x", x }, { "y", y } };
        for (const auto& [name, position] : m_auto_deploy_positions) {
            if (position == std::pair { x, y }) {
                point["name"] = name;
                break;
            }
        }
        points.emplace_back(std::move(point));
    }
    return points;
}
