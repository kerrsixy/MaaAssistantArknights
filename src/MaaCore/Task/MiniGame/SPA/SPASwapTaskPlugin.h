#pragma once

#include "MaaUtils/NoWarningCV.hpp"
#include "SPABoardGeometry.h"
#include "SPAConsumablePolicy.h"
#include "SPAControlEvidence.h"
#include "SPAPlacementPolicy.h"
#include "SPAPreparationEvidence.h"
#include "SPAProjection.h"
#include "SPASelectedPanelEvidence.h"
#include "Task/AbstractTaskPlugin.h"
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace asst::spa
{
class SwapTaskPlugin final : public AbstractTaskPlugin
{
public:
    using AbstractTaskPlugin::AbstractTaskPlugin;
    bool configure(const json::object& options);
    bool verify(AsstMsg msg, const json::value& details) const override;

    const std::string& selected_name() const { return m_name; }

    bool population_consumed() const { return m_population_consumed; }

    int remaining_deployment() const { return m_remaining; }

    const std::string& displaced_name() const { return m_displaced_name; }

    Point target_point() const { return m_target; }

    Point drag_target_point() const { return m_drag_target; }

    bool field_pose_verified() const { return m_field_pose_verified; }

    bool reorientation_started() const { return m_reorientation_started; }

    bool spatial_hold_deferred() const { return m_spatial_hold_deferred; }

    bool spatial_receiver_failed() const { return m_spatial_receiver_failed; }

    bool spatial_drop_returned_without_direction() const { return m_spatial_drop_returned_without_direction; }

    bool spatial_preview_failed() const { return m_spatial_preview_failed; }

    bool relocation_applied() const { return m_relocation_applied; }

    const std::string& verified_map_id() const { return m_verified_map_id; }

    void set_spatial_target_callback(std::function<void(const Point&)> callback)
    {
        m_spatial_target_callback = std::move(callback);
    }

    void set_spatial_hold_guard(std::function<bool()> guard) { m_spatial_hold_guard = std::move(guard); }

    bool set_spatial_reconciliation_context(int round, int funds, int remaining);

    bool set_registered_grid_context(const cv::Mat& reference, const cv::Mat& projection, const std::string& map_id);

    bool inspect_equipment_inventory(int round, int funds, int remaining, std::vector<InventoryOperatorHint>& hints);

    // Empty means a source/target preview failed, false means a drop was
    // issued but not yet confirmed, true means the field title was read.
    std::optional<bool> deploy_preparation_summon(
        const std::string& name,
        const Point& bench_hint,
        const std::vector<std::pair<int, int>>& targets,
        int round,
        int funds,
        int remaining,
        std::optional<std::pair<int, int>>& verified_cell);

    void use_bench_identity_hints(std::vector<InventoryOperatorHint>& hints) { m_bench_identity_hints = &hints; }

    const std::vector<std::pair<std::string, Point>>& inspected_equipment() const { return m_inspected_equipment; }

    const std::vector<std::pair<std::string, Point>>& inspected_summons() const { return m_inspected_summons; }

    const std::vector<PlacementMember>& reoriented_field_members() const { return m_reoriented_field_members; }

    // Retry only observation/known modal cancellation, never the held move.
    bool reconcile_in_place_orientation();

    // A registered subset; callers must match its identities and cardinality
    // to the actual field population before using whole-field row counts.
    const std::vector<GridPlacementMember>& observed_grid_members() const { return m_observed_grid_members; }

    const cv::Mat& observed_grid_projection() const { return m_observed_grid_projection; }

    const cv::Mat& observed_grid_reference() const { return m_observed_grid_reference; }

    const std::vector<std::pair<int, int>>& observed_boss_attackable_cells() const
    {
        return m_observed_boss_attackable_cells;
    }

    const std::vector<PlacementMember>& observed_field_members() const { return m_observed_field_members; }

    bool observe_grid_snapshot(
        const cv::Mat& reference,
        const cv::Mat& grid_to_reference,
        const std::vector<PlacementMember>& members,
        int round,
        int funds,
        int remaining);

    // Observe a held field operator, then cancel the hold. No deployment,
    // sale or direction is committed, including after a cold resume.
    bool probe_registered_grid(
        const std::vector<PlacementMember>& members,
        int round,
        int funds,
        int remaining,
        bool verify_all_identities);

    // Inspect only a previously verified field location after synthesis.
    // This never drags, sells or changes the selected unit's direction.
    bool revalidate_promoted_pose(
        const std::string& name,
        const Point& point,
        int round,
        std::optional<bool> expected_elite = std::nullopt);

    std::optional<char> committed_facing() const
    {
        if (!m_direction_committed) {
            return std::nullopt;
        }
        return m_direction.x > 0 ? 'E' : m_direction.x < 0 ? 'W' : m_direction.y > 0 ? 'S' : 'N';
    }

    std::optional<Point> rejected_drop_target() const { return m_rejected_drop_target; }

    bool replacement_committed() const { return m_replacement_committed; }

    bool replacement_target_unavailable() const { return m_replacement_target_unavailable; }

    bool replacement_can_reconcile() const { return m_replacement_postconditions_ready; }

    bool replacement_original_preserved() const { return m_replacement_original_preserved; }

    bool bench_source_search_exhausted() const { return m_bench_source_search_exhausted; }

    bool sale_committed() const { return m_sale_committed; }

    int sale_credit() const { return m_sale_credit; }

    bool equipment_committed() const { return m_equipment_committed; }

    bool equipment_contact_attempted() const { return m_equipment_contact_attempted; }

    int equipment_cash_credit() const { return m_equipment_cash_credit; }

    bool cash_input_chain_verified() const { return m_cash_input_chain_verified; }

    int equipment_next_round_credit_quote() const { return m_equipment_next_round_credit_quote; }

    bool equipment_consumption_verified() const { return m_equipment_consumption_verified; }

    const std::optional<PromotionConsumableQuote>& promotion_consumable_effect() const
    {
        return m_promotion_consumable_quote;
    }

    // Observation-only receipt: the parent must reconcile physical inventory
    // before updating forms, copies, equipment slots or a promotion reward.
    bool promotion_consumable_pending() const
    {
        return m_promotion_consumable && (m_equipment_contact_attempted || m_equipment_committed);
    }

    bool projection_attached_verified() const { return m_projection_attached_verified; }

    bool recipient_elite_transition_observed() const { return m_recipient_elite_transition_observed; }

    bool equipment_slots_mismatch() const { return m_equipment_slots_mismatch; }

    const std::string& equipment_stage() const { return m_equipment_stage; }

    std::optional<int> equipment_source_x() const
    {
        return m_equipment_source_index ? std::optional<int>(m_source.x) : std::nullopt;
    }

    bool set_reconciliation_context(int round, int funds);
    bool reconcile_replacement();

protected:
    bool _run() override;
    bool on_run_fails() override;

private:
    void observe_boss_attackable_region(const cv::Mat& image, const cv::Mat& grid_to_image);
    std::vector<std::pair<int, int>> m_observed_boss_attackable_cells;
    std::vector<std::pair<std::string, Point>> m_inspected_equipment;
    std::vector<std::pair<std::string, Point>> m_inspected_summons;
    std::vector<InventoryOperatorHint>* m_bench_identity_hints = nullptr;
    BoardRegistration preserve_registered_board(
        const cv::Mat& image,
        BoardRegistration board,
        bool melee_only,
        const std::vector<PlacementMember>& held_members) const;
    bool m_replacement_above_blocking_device = false;
    cv::Mat m_registered_grid_reference;
    cv::Mat m_registered_grid_projection;
    std::string m_registered_grid_map_id;
    bool preparation_matches();
    bool preparation_matches(const cv::Mat& image);
    bool restore_preparation();
    bool current_identity(const std::string& name, cv::Mat* verified_panel = nullptr);
    bool verify_replacement_bench_identity(const std::string& name, int& probes);
    std::optional<std::string> m_last_panel_identity;
    std::optional<std::string> selected_identity_any(const Point& point);
    bool current_equipment(const std::string& name);
    std::optional<std::vector<std::string>> selected_equipment_names(size_t expected_count);
    bool selected_identity(const Point& point, const std::string& name, cv::Mat* verified_panel = nullptr);
    bool selected_field_identity(const Point& point, const std::string& name, cv::Mat& verified_panel);
    std::optional<std::string> bench_identity(
        const Point& point,
        bool allow_sale_head_retry = false,
        const std::string& replacement_bench_name = {},
        Point* verified_contact = nullptr);
    bool reconcile_deployed_target();
    bool observe_deployed_pose();
    bool close_selection(cv::Mat* clean_frame = nullptr, const cv::Mat& initial_frame = {});
    bool ensure_expanded_shop(cv::Mat* already_visible_frame = nullptr);
    bool ensure_collapsed_shop(cv::Mat* already_visible_frame = nullptr);
    bool capture_collapsed_preparation(cv::Mat& image, const cv::Mat& selected_frame = {});
    bool ensure_shop_visibility(
        bool expanded,
        cv::Mat* already_visible_frame,
        const cv::Mat& initial_frame = {},
        bool* toggle_input_issued = nullptr);
    bool reacquire_cash_equipment_source(const Point& verified_source);
    std::optional<int> read_selected_cash_equipment_credit(const cv::Mat& frame) const;
    bool held_drag();
    bool select_direction(const Point& center);
    bool drag_direction_only(const Point& center);
    bool reorient_in_place();
    int m_reorient_col = -1;
    int m_reorient_row = -1;
    bool m_reorientation_started = false;
    bool m_spatial_receiver_failed = false;
    bool m_spatial_drop_returned_without_direction = false;
    bool m_spatial_preview_failed = false;
    bool m_relocate_field = false;
    bool m_grid_probe_only = false;
    bool m_relocation_applied = false;
    std::string m_relocation_displaced;
    std::string m_expected_map_id;
    std::string m_verified_map_id;
    int m_origin_col = -1;
    int m_origin_row = -1;
    std::optional<cv::Point> m_reorientation_destination;
    std::function<void(const Point&)> m_spatial_target_callback;
    std::function<bool()> m_spatial_hold_guard;
    bool m_spatial_hold_deferred = false;
    std::vector<PlacementMember> m_reoriented_field_members;
    std::function<bool()> m_reorientation_reconcile;
    cv::Mat m_observed_grid_projection;
    cv::Mat m_observed_grid_reference;
    std::vector<PlacementMember> m_observed_field_members;
    Point m_source;
    Point m_target;
    Point m_drag_target;
    std::optional<Point> m_settled_held_target;
    std::optional<Point> m_rejected_drop_target;
    std::string m_name;
    std::string m_inspect_tab = "特质";
    std::string m_replaced;
    std::string m_pact_recruitment_pact;
    int m_pact_recruitment_shop_level = 0;
    bool m_promotion_consumable = false;
    bool m_direct_projection = false;
    bool m_allow_delayed_projection = true;
    int m_consumable_free_bench_slots = -1;
    std::optional<PromotionConsumableQuote> m_promotion_consumable_quote;
    bool m_projection_attached_verified = false;
    bool m_recipient_elite_transition_observed = false;
    bool m_withdraw_only = false;
    bool m_deploy_only = false;
    bool m_replace_only = false;
    bool m_replacement_reconcile_only = false;
    bool m_replacement_committed = false;
    bool m_replacement_target_unavailable = false;
    bool m_replacement_postconditions_ready = false;
    bool m_replacement_original_preserved = false;
    bool m_fast_mode = false;
    bool m_best_effort_deployment = false;
    bool m_prefer_safe_target = false;
    bool m_role_aware_deployment = false;
    std::string m_primary_pact;
    bool m_primary_active = false;
    std::vector<std::string> m_active_placement_pacts;
    std::vector<PlacementMember> m_placement_members;
    std::vector<cv::Point> m_verified_preview_feet;
    std::map<char, int> m_replacement_growth_bonuses;
    Point m_safe_target { 680, 360 };
    std::vector<Point> m_excluded_deploy_points;
    bool m_find_bench_source = false;
    bool m_bench_source_search_exhausted = false;
    bool m_recent_acquisition_source = false;
    bool m_deployment_hint_verified = false;
    bool m_direction_preview_verified = false;
    bool m_move_only = false;
    bool m_finalize_direction = false;
    Point m_drag_origin;
    bool m_face_only = false;
    bool m_preserve_population = false;
    Point m_swap_origin;
    bool m_equip_only = false;
    bool m_find_equipment_source = false;
    bool m_equipment_committed = false;
    std::string m_identity_tag_pact;
    bool m_equipment_contact_attempted = false;
    int m_equipment_cash_credit = 0;
    int m_minimum_cash_credit = 1;
    int m_equipment_next_round_credit_quote = 0;
    bool m_equipment_consumption_verified = false;
    std::optional<size_t> m_equipment_source_index;
    std::vector<cv::Point> m_cash_source_inventory;
    bool m_cash_input_chain_verified = false;
    std::vector<std::string> m_expected_equipment;
    bool m_equipment_slots_mismatch = false;
    std::string m_equipment_stage = "not_started";
    cv::Mat m_equipment_reference;
    cv::Mat m_held_board;
    cv::Mat m_deployment_grid_to_screen;
    cv::Mat m_growth_grid_to_screen;
    std::vector<GridPlacementMember> m_prior_grid_members;
    std::vector<GridPlacementMember> m_observed_grid_members;
    bool m_field_pose_verified = false;
    bool m_direction_committed = false;
    bool m_spatial_direction_attempted = false;
    int m_preferred_equipment_x = -1;
    bool m_inspect_only = false;
    bool m_terrain_probe_only = false;
    int m_probe_drag_pixels = 0;
    bool m_sell_only = false;
    bool m_sale_committed = false;
    bool m_strategy_only = false;
    int m_layers_before = -1;
    bool m_target_elite = false;
    bool m_verify_source_form = false;
    bool selected_form_matches(const cv::Mat& image, const std::string& name);
    std::map<std::string, int> m_pact_baselines;
    int m_sale_credit = 0;
    int m_minimum_sale_credit = 1;
    Point m_direction = Point(120, 0);
    int m_round = 0;
    int m_funds = 0;
    int m_remaining = 0;
    PreparationHudEvidence m_preparation_hud;
    ShopToggleEvidence m_shop_toggle_evidence;
    SelectedPanelEvidence m_selected_panel_evidence;
    int m_preparation_hud_cache_hits = 0;
    int m_preparation_hud_fields_reused = 0;
    int m_preparation_hud_ocr_checks = 0;
    bool m_population_consumed = false;
    std::string m_displaced_name;
};
}
