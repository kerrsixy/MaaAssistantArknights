#pragma once
#include "Common/AsstTypes.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Task/InterfaceTask.h"
#include "Task/MiniGame/SPA/SPADeploymentPlanCache.h"
#include "Task/MiniGame/SPA/SPADeploymentReview.h"
#include "Task/MiniGame/SPA/SPAEquipmentPolicy.h"
#include "Task/MiniGame/SPA/SPAPreparationDeadline.h"
#include "Task/MiniGame/SPA/SPAPreparationPolicy.h"
#include "Task/MiniGame/SPA/SPAProjection.h"
#include "Task/MiniGame/SPA/SPAReplacementPolicy.h"
#include "Task/MiniGame/SPA/SPAStrategyEligibility.h"
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>

namespace asst::spa
{
class LifecycleTaskPlugin;
class SwapTaskPlugin;
struct BenchCapacityTrade;
struct PrimaryDamageBonuses;
}

namespace asst
{
class StrongholdProtocolTask final : public InterfaceTask
{
public:
    inline static constexpr std::string_view TaskType = "StrongholdProtocol";
    StrongholdProtocolTask(const AsstCallback& callback, Assistant* inst);
    bool set_params(const json::value& params) override;
    bool run() override;

protected:
    bool _run() override;

private:
    bool run_single();
    std::optional<json::object>
        scan_strategy_progress(const std::string& target = {}, std::map<std::string, cv::Mat>* portraits = nullptr);
    int m_run_count = 1;
    json::value m_initial_parameters;
    bool run_session(spa::LifecycleTaskPlugin* lifecycle = nullptr);
    bool m_manage_lifecycle = false;
    bool m_require_fresh_independent = false;
    bool m_lifecycle_battle_seen = false;
    bool m_run_victory_verified = false;
    std::optional<json::object> m_lifecycle_solo_result;
    bool m_prefer_unclaimed = true;
    bool m_schedule_strategy_badges = false;
    bool m_strategy_catalogue_complete = false;
    std::string m_next_strategy_badge_hint;
    std::string m_client_type;
    std::string m_mode = "independent";
    std::string m_requested_strategy_id;
    bool m_current_strategy_inspected = false;
    bool m_temporary_elite_description_verified = false;
    std::set<int> m_preparation_instruction_attempts;
    int m_deferred_opening_gift_scan_attempts = 0;
    int m_equipment_discount_round = -1;
    bool identify_current_strategy(const json::value& state);
    bool handle_temporary_elite_instruction(const json::value& state);
    bool handle_strategy_copy(const json::value& state);
    std::string m_strategy_copy_target;
    bool m_strategy_copy_unverified = false;
    int m_strategy_copy_retry_after_round = -1;
    bool handle_fear_trap_instruction(const json::value& state);
    std::set<spa::BoardCell> m_fear_trap_cells;
    int m_fear_trap_round = -1;
    bool m_fear_trap_unverified = false;
    std::vector<std::string> m_configured_core_pacts;
    std::vector<std::string> m_declared_disabled_pacts;
    std::vector<std::string> m_observed_disabled_pacts;
    void refresh_mode_pact_restrictions();
    bool m_shopping_complete = false;
    bool m_shop_review_after_replacements = false;
    std::optional<std::pair<int, spa::ShopOffer>> m_reviewed_funding_quote;
    bool m_shop_reviewed_last_ordinary_coin = false;
    int m_last_coin_review_after_deployment_round = -1;
    int m_last_economy_round = -1;
    int m_last_observed_automated_battle_round = -1;
    spa::PreparationDeadline m_joint_preparation_deadline;
    bool m_joint_emote_probe = false;
    bool m_joint_commend_teammates = false;
    int m_supply_confirmation_after_round = -1;
    std::optional<int> m_previous_round_life;
    std::set<std::pair<int, std::string>> m_blocked_purchases;
    bool handle_reward_page(int after_round);
    // True means a discovery pass ran; discard the old preparation frame.
    bool refresh_equipment_inventory(const json::value& state);
    bool handle_preparation_summons(const json::value& state);
    std::map<std::string, std::pair<int, Point>> m_observed_bench_summons;
    std::set<std::pair<int, std::string>> m_preparation_summon_attempts;
    std::set<std::pair<int, spa::BoardCell>> m_confirmed_summon_cells;
    int m_equipment_capacity_blocked_round = -1;
    bool m_equipment_inventory_dirty = true;
    std::set<int> m_equipment_inventory_scan_rounds;
    std::vector<spa::InventoryOperatorHint> m_inventory_operator_hints;
    bool handle_promotion_page();
    bool handle_full_field_replacement(const json::value& state);
    spa::ReplacementMember replacement_member(
        const std::string& name,
        const spa::OwnedOperator& owned,
        const spa::PreparationPolicy* context = nullptr) const;
    int replacement_lineup_context_value(const std::vector<spa::ReplacementMember>& lineup) const;
    bool verified_elite_clone_on_bench(const std::string& name, const spa::OwnedOperator& owned) const;
    std::pair<std::vector<spa::ReplacementMember>, std::vector<spa::ReplacementMember>> replacement_inputs() const;
    bool handle_full_bench_sale(
        const json::value& state,
        int funding_deficit = 0,
        const spa::BenchCapacityTrade* capacity_trade = nullptr,
        const spa::ReplacementChoice* replacement_to_enable = nullptr,
        const spa::ShopOffer* funding_offer = nullptr);

    struct BenchFundingPair
    {
        int round;
        int funds;
        std::string primary_pact;
        spa::ShopOffer offer;
        std::string reserved_seller;
        bool partial_sale_verified = false;
    };

    std::optional<BenchFundingPair> m_bench_funding_pair;
    std::optional<std::pair<int, spa::ShopOffer>> m_purchase_reserved_after_sale;
    std::set<std::pair<int, std::string>> m_failed_bench_sales;
    std::string m_unreconciled_sale;
    int m_replacement_bench_space_round = -1;
    bool reconcile_pending_cash_equipment(const json::value& state);
    bool handle_cash_equipment(const json::value& state, bool boss_preparation, int funding_deficit = 0);
    bool handle_delayed_cash_equipment(const json::value& state);
    void observe_delayed_cash_equipment(const json::value& state);
    bool handle_pact_recruitment_equipment(const json::value& state);
    bool handle_persistent_equipment(const json::value& state);
    std::optional<std::string> direct_promotion_recipient() const;
    bool handle_direct_promotion_equipment(const json::value& state);
    bool reconcile_direct_promotion_equipment(const json::value& state);
    std::optional<json::object> m_pending_direct_promotion;
    std::set<int> m_direct_promotion_attempted_rounds;
    std::set<int> m_direct_promotion_observed_rounds;
    int m_spatial_snapshot_round = -1;
    std::vector<std::string> field_added_pacts(const std::string& name) const;
    bool handle_spatial_reorientation(const json::value& state);
    void observe_egir_formation(
        const std::vector<spa::GridPlacementMember>& members,
        bool active,
        int round,
        bool harmony_active);
    std::string m_egir_analysis_context;
    std::string m_advertised_boss_family;
    bool m_egir_chain_validation_mode = false;
    std::string m_egir_plan_context;
    std::optional<spa::DeploymentStep> egir_layout_step(
        const std::vector<spa::GridPlacementMember>& members,
        const std::map<std::string, std::vector<std::pair<int, int>>>& ranges,
        int round,
        bool harmony_active);
    std::optional<spa::DeploymentStep> dynamic_layout_step(
        const std::vector<spa::GridPlacementMember>& members,
        const std::map<std::string, std::vector<std::pair<int, int>>>& ranges,
        const std::vector<std::string>& active_pacts,
        int round);

    struct BossOpeningTarget
    {
        std::vector<spa::BoardCell> cells;
        std::vector<spa::BoardCell> movement_waypoints;
        spa::BossInterception interception;
        std::string source = "unknown";
        bool measured = false;
    };

    BossOpeningTarget boss_opening_target(int round) const;
    spa::PrimaryDamageBonuses observed_primary_damage_bonuses(const std::string& name) const;
    spa::DeploymentUnit describe_deployed_unit(
        const spa::GridPlacementMember& member,
        const std::vector<spa::BoardCell>& range,
        bool boss_round) const;
    void review_boss_opening_coverage(const json::value& state);
    void refresh_spatial_grid_snapshot(
        const json::value& state,
        bool require_fresh_projection,
        bool finalizing_boss_formation);
    cv::Mat m_spatial_grid_reference;
    cv::Mat m_spatial_grid_projection;
    std::string m_spatial_map_id;
    bool m_spatial_boss_phase = false;
    bool m_stop_before_battle = false;
    std::string m_spatial_deferral_context;
    int m_spatial_boss_phase_round = 0;
    std::optional<bool> m_spatial_boss_hidden_variant;
    std::vector<std::pair<int, int>> m_boss_attackable_cells;
    std::string m_boss_attackable_map_id;
    int m_boss_attackable_round = 0;
    std::vector<spa::DeploymentUnit> m_dynamic_layout_target;
    spa::DeploymentCellLedger m_dynamic_layout_expected_cells;
    std::string m_dynamic_layout_context;
    std::string m_failed_layout_context;
    std::map<int, std::string> m_boss_layout_fixed_unit;
    std::set<std::string> m_failed_spatial_actions;
    spa::BossVacantTargetRejections m_boss_vacant_target_rejections;
    std::map<int, int> m_dynamic_layout_attempts;
    std::map<int, int> m_spatial_reconciliation_attempts;
    bool m_dynamic_layout_observed = false;
    bool m_field_checkpoint_loaded = false;
    struct SpatialGridRefresh
    {
        int attempts = 0;
        bool facings_reviewed = false;
    };
    std::map<std::string, SpatialGridRefresh> m_spatial_grid_refresh_attempts;
    std::string m_recent_bench_acquisition;
    std::optional<std::pair<int, std::string>> m_first_healer_recruit;
    std::set<std::pair<int, std::string>> m_spatial_orientation_attempts;
    std::optional<json::object> m_pending_spatial_orientation;
    std::shared_ptr<spa::SwapTaskPlugin> m_pending_orientation_verifier;
    void record_acquired_equipment_hints(const std::string& name, bool elite, int round);
    std::pair<spa::EquipmentAssignmentContext, std::vector<spa::EquipmentRecipient>>
        equipment_assignment_inputs() const;
    std::set<std::string> m_equipment_candidates;
    std::map<std::string, std::vector<std::string>> m_equipped_items;
    std::map<std::string, Point> m_equipment_source_hints;
    Point equipment_source_hint(const std::string& name) const;
    spa::PersistentEquipmentPreparation m_persistent_equipment_preparation;
    std::set<int> m_cash_equipment_attempted_rounds;
    std::optional<json::object> m_pending_cash_equipment;
    int m_cash_equipment_source_verified_round = -1;
    std::set<int> m_passbook_attempted_rounds;
    int m_passbook_source_verified_round = -1;
    std::set<int> m_delayed_cash_equipment_attempted_rounds;
    std::string m_delayed_cash_rejected_recipient;
    std::optional<json::object> m_pending_delayed_cash_equipment;
    std::set<int> m_pact_recruitment_attempted_rounds;
    std::string m_pending_pact_recruitment;
    std::string m_pending_tactical_recruitment;
    int m_pending_tactical_recruitment_round = 0;
    int m_tactical_recruitment_probes = 0;
    bool reconcile_tactical_recruitment(const json::value& state);
    bool reconcile_deferred_opening_gifts(const json::value& state);
    int m_cash_equipment_acquired_round = -1;
    json::array equipment_candidates_snapshot() const;
    json::array equipment_slots_snapshot() const;
    void commit_full_field_replacement(
        const std::string& incoming,
        const std::string& outgoing,
        const Point& previous,
        const Point& actual,
        int round,
        int funds,
        std::optional<char> committed_facing,
        int planned_improvement);

    struct PendingReplacement
    {
        std::shared_ptr<spa::SwapTaskPlugin> verifier;
        std::string incoming;
        std::string outgoing;
        Point previous;
        int incoming_copies = 0;
        int outgoing_copies = 0;
        bool incoming_elite = false;
        bool outgoing_elite = false;
        int last_probe_round = -1;
        bool needs_pose_probe = false;
        std::optional<char> committed_facing;
        int planned_improvement = 0;
        int decision_round = -1;
        int unverified_direction_round = -1;
    };

    std::optional<PendingReplacement> m_pending_field_replacement;
    json::value pending_replacement_snapshot() const;
    int m_replacement_round = -1;
    std::set<std::tuple<std::string, std::string, int, int>> m_failed_replacements;
    std::set<std::tuple<std::string, int, int>> m_unverified_replacement_receivers;
    std::set<std::tuple<std::string, int, int>> m_unverified_direction_receivers;
    std::set<spa::ReplacementLineup> m_visited_replacement_lineups;
    spa::ReplacementHistory m_completed_replacement_exchanges;
    std::set<std::string> m_unavailable_replacement_sources;
    json::array roster_snapshot() const;
    json::array failed_spatial_action_snapshot() const;
    json::array deployment_point_snapshot() const;
    json::array field_position_snapshot() const;
    spa::PreparationPolicy m_policy;
    int m_last_ursus_battle_with_layer_growth = -1;
    std::vector<std::string> m_enabled_core_pacts;
    bool m_dynamic_primary_pact = false;
    std::string m_run_id;
    int m_difficulty = 0;
    int m_expected_boss_round = 0;
    int m_resume_after_round = 1;
    int m_max_actions = 0;
    bool m_decision_only = false;
    bool m_stop_after_deployment = false;
    bool m_verify_layout_identities = false;
    bool m_movement_only = false;
    bool m_round_only = false;
    bool m_auto_run = false;
    bool m_phase_one_game_layout = false;
    bool m_auto_deploy = false;
    char m_auto_deploy_direction = 'E';
    bool m_spending_reviewed = false;
    std::string m_spending_reason;
    spa::StrategyEligibilityContext m_strategy_context;
    std::string m_selected_strategy_id;
    std::vector<spa::StrategyCandidate> m_strategy_candidates;
    std::vector<json::object> m_movement_plan;
    std::map<std::string, json::object> m_deployment_targets;
    // Same-name initial copies can occupy different lanes before promotion.
    std::multimap<std::string, std::pair<int, int>> m_auto_deploy_positions;
    // Row counts are valid only for the exact physical field snapshot that
    // produced them. Changes need a fresh snapshot or a verified same-cell
    // exchange; an unreconciled move/swap/promotion invalidates the comparison.
    std::multimap<std::string, std::pair<int, int>> m_growth_population_poses;
    std::map<std::string, int> m_row_growth_populations;
    std::vector<spa::GridPlacementMember> m_population_grid_members;
    std::set<std::pair<int, int>> m_verified_field_pose_points;
    // Last successful direction input, not a claim of pixel-verified facing.
    std::map<std::pair<int, int>, char> m_committed_facings;
    std::set<std::pair<int, int>> m_auto_deploy_used_cells;
    std::set<std::string> m_auto_deploy_blocked_names;
    int m_auto_deploy_suppressed_round = -1;
    // Last authoritative deployment counter. The B-server occasionally drops
    // the digit while keeping the preparation HUD; retaining the last value
    // lets phase one continue without treating that transient OCR gap as a
    // reason to stop before Ready.
    std::optional<int> m_last_remaining_deployment;
    int m_swap_deploy_round = -1;

    struct PendingPromotionReward
    {
        std::string source;
        std::string recruitment_pact;
        std::string name;
        std::vector<std::string> offers;
        int round = -1;
        int slot = -1;
        bool confirmation_issued = false;
        bool short_title_confirmed = false;
    };

    std::optional<PendingPromotionReward> m_pending_promotion_reward;
    std::string m_pending_promotion;
    bool m_bench_recount_pending = false;
    int m_promotion_retry_count = 0;
    std::set<int> m_basic_upgrade_done_rounds;
    // Dynamic planning upgrades at most once per preparation round.  The
    // observed shop level is authoritative on the next loop; this guard only
    // prevents a stale frame from paying the same upgrade twice.
    std::set<int> m_dynamic_upgrade_done_rounds;
    std::map<int, int> m_strategy_refresh_attempts;
    std::set<int> m_strategy_refresh_blocked_rounds;
    std::map<int, int> m_full_bench_refresh_attempts;
    // Dynamic role order for the selected route: growth engines first, then
    // terminal outputs and finally battle-redeploy helpers. This is rebuilt
    // only when the live primary route changes.
    std::vector<std::string> m_dynamic_roles;
    // Phase-one deployment burst: after a verified placement, reuse the
    // bounded preparation snapshot for the next bench card. SwapTask takes a
    // fresh map frame itself, so repeating the outer full-screen observer here
    // only adds latency. The cache is consumed once and is never used after a
    // failed drag or when the bench is empty.
    std::optional<json::value> m_cached_deployment_observation;
    std::map<std::string, std::pair<int, int>> m_layout_screen_points;
    bool m_layout_supplied = false;
    std::vector<spa::DeploymentUnit> m_layout_units;
    std::vector<spa::DeploymentLane> m_layout_lanes;
    std::vector<spa::BoardCell> m_deployable_cells;
};
}
