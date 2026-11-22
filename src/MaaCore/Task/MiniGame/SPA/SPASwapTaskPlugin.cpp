#include "SPASwapTaskPlugin.h"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/TemplResource.h"
#include "MaaUtils/ImageIo.h"
#include "SPABattleRole.h"
#include "SPABenchHitbox.h"
#include "SPABenchSalePolicy.h"
#include "SPABoardGeometry.h"
#include "SPACashEquipmentEvidence.h"
#include "SPACombatStats.h"
#include "SPAEngagementPolicy.h"
#include "SPAEquipmentPolicy.h"
#include "SPAGreenCellTarget.h"
#include "SPAGrowth.h"
#include "SPAImageEvidence.h"
#include "SPAProjection.h"
#include "SPASkillPolicy.h"
#include "SPASpatialGrowthPolicy.h"
#include "SPASummonPolicy.h"
#include "SPATerrainModel.h"
#include "Vision/Battle/BattlefieldClassifier.h"
#include <algorithm>
#include <chrono>
#include <cmath>

#include "Controller/Controller.h"
#include "SPAAcquisition.h"
#include "SPANumberOcr.h"
#include "SPAObservation.h"
#include "Vision/Matcher.h"
#include "Vision/OCRer.h"

namespace
{

// During B-server deployment the map is covered by a large translucent green
// legal-cell overlay.  OCR can briefly lose the "出售/攻击范围" labels while
// the operator is settling, so use this visual signal only as a placement
// fallback.  Normal preparation has only a few blue hints and stays far below
// this coverage threshold.
bool deployment_surface_visible(const cv::Mat& image)
{
    if (image.empty() || image.cols != 1280 || image.rows != 720) {
        return false;
    }
    const cv::Rect board(180, 140, 820, 420);
    cv::Mat hsv;
    cv::cvtColor(image(board), hsv, cv::COLOR_BGR2HSV);
    cv::Mat green;
    cv::inRange(hsv, cv::Scalar(35, 50, 80), cv::Scalar(95, 255, 255), green);
    return static_cast<double>(cv::countNonZero(green)) / static_cast<double>(green.total()) >= 0.08;
}

// Empty bench slots are dark blue panels.  Do not open an operator panel at
// every geometric slot: first require a visible sprite in the slot ROI.  The
// identity OCR remains the final authority after this cheap visual gate.
bool bench_sprite_visible(const cv::Mat& image, const asst::Point& center)
{
    return asst::spa::bench_operator_visible(image, cv::Point(center.x, center.y));
}

// The direction diamond is sometimes too faint for the geometric detector
// while its modal hint remains visible.  OCR on this small, stable area is a
// cheap second signal and prevents identity probing from looping on the same
// modal frame.
bool direction_modal_visible(const cv::Mat& image)
{
    if (image.empty() || image.cols != 1280 || image.rows != 720) {
        return false;
    }
    if (const auto center = asst::spa::direction_selection_center(image);
        center && asst::spa::direction_cancel_target(image, *center)) {
        return true;
    }
    asst::OCRer ocr(image, asst::Rect(350, 70, 360, 260));
    const auto texts = ocr.analyze();
    if (!texts) {
        return false;
    }
    for (const auto& text : *texts) {
        if (text.score >= 0.75 && text.text.find("点击取消") != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool reread_single_operator_title(
    const cv::Mat& image,
    const std::vector<asst::spa::TextObservation>& original,
    const std::string& expected)
{
    if (!asst::spa::operator_title_contains_rank_artifact(original, expected) &&
        !asst::spa::single_operator_title_confidence(original, expected)) {
        return false;
    }
    asst::OCRer exact(image, asst::Rect(6, 173, 39, 41));
    const auto glyph = exact.analyze();
    return glyph && glyph->size() == 1 && glyph->front().text == expected && glyph->front().score >= .90 &&
           glyph->front().rect.x < 40 && glyph->front().rect.y >= 175 && glyph->front().rect.y <= 207;
}

// Discover bench centres from the tier badges themselves.  The bench row is
// right-aligned and its first occupied slot changes as the shop is purchased,
// so a fixed x=220+N*80 probe is inherently wrong.  Returning badge-derived
// centres also makes the empty-slot filter independent of map/camera layout.
std::vector<asst::Point> detect_bench_centres(const cv::Mat& image, bool include_christine = false)
{
    std::vector<asst::Point> result;
    for (const auto& center : asst::spa::bench_source_centres(image, include_christine)) {
        result.emplace_back(center.x, center.y);
    }
    return result;
}

} // namespace

bool asst::spa::SwapTaskPlugin::configure(const json::object& options)
{
    m_population_consumed = false;
    m_sale_committed = false;
    m_observed_sale_credit.reset();
    m_equipment_committed = false;
    m_identity_tag_pact = options.get("identity_tag_pact", std::string());
    m_equipment_contact_attempted = false;
    m_equipment_cash_credit = 0;
    m_minimum_cash_credit = options.get("minimum_cash_credit", 1);
    if (m_minimum_cash_credit < 1 || m_minimum_cash_credit > 999) {
        return false;
    }
    m_equipment_next_round_credit_quote = 0;
    m_equipment_consumption_verified = false;
    m_direct_projection = options.get("direct_projection", false);
    m_allow_delayed_projection = options.get("allow_delayed_projection", true);
    m_promotion_consumable = options.get("strategy_core_consumable", false) || m_direct_projection;
    m_consumable_free_bench_slots = options.get("consumable_free_bench_slots", -1);
    m_promotion_consumable_quote.reset();
    m_projection_attached_verified = false;
    m_recipient_elite_transition_observed = false;
    m_equipment_source_index.reset();
    m_cash_source_inventory.clear();
    m_cash_input_chain_verified = false;
    m_expected_equipment.clear();
    m_equipment_slots_mismatch = false;
    m_equipment_stage = "not_started";
    m_equipment_reference.release();
    m_displaced_name.clear();
    m_strategy_only = options.get("strategy_only", false);
    m_strategy_target_on_field = options.get("strategy_target_on_field", false);
    m_strategy_copy_contact_attempted = false;
    m_layers_before = options.get("layers_before", -1);
    m_target_elite = options.get("target_elite", false);
    m_verify_source_form = options.get("verify_source_form", false);
    m_pact_baselines.clear();
    m_inspect_tab = options.get("inspect_tab", std::string("特质"));
    if (m_inspect_tab != "特质" && m_inspect_tab != "技能" && m_inspect_tab != "特性" && m_inspect_tab != "天赋") {
        return false;
    }
    m_inspect_only = options.get("inspect_only", false);
    m_terrain_probe_only = options.get("terrain_probe_only", false);
    m_probe_drag_pixels = options.get("probe_drag_pixels", 0);
    if (m_probe_drag_pixels < 0 || m_probe_drag_pixels > 32 || (!m_terrain_probe_only && m_probe_drag_pixels != 0)) {
        return false;
    }
    m_sell_only = options.get("sell_only", false);
    m_sale_credit = options.get("sale_credit", -1);
    m_minimum_sale_credit = options.get("minimum_sale_credit", 1);
    m_equip_only = options.get("equip_only", false);
    m_find_equipment_source = options.get("find_equipment_source", false);
    m_preferred_equipment = { options.get("preferred_equipment_x", -1), options.get("preferred_equipment_y", -1) };
    if (m_preferred_equipment.x < -1 || m_preferred_equipment.x >= 1000 || m_preferred_equipment.y < -1 ||
        m_preferred_equipment.y >= 720 || (m_preferred_equipment.x == -1 && m_preferred_equipment.y >= 0)) {
        return false;
    }
    m_face_only = options.get("face_only", false);
    m_bench_source_search_exhausted = false;
    m_joint_deployment = options.get("joint_deployment", false);
    m_joint_named_bench_slot.reset();
    m_joint_named_bench_sprite.clear();
    m_preserve_population = options.get("preserve_population", false);
    m_reorient_col = options.get("reorient_col", -1);
    m_reorient_row = options.get("reorient_row", -1);
    m_reorientation_started = false;
    m_spatial_hold_guard = {};
    m_spatial_hold_deferred = false;
    m_spatial_receiver_failed = false;
    m_spatial_drop_returned_without_direction = false;
    m_spatial_preview_failed = false;
    m_spatial_held_registration_failed = false;
    m_relocate_field = options.get("relocate_field", false);
    m_grid_probe_only = false;
    m_relocation_applied = false;
    m_relocation_displaced = options.get("relocation_displaced", std::string());
    m_expected_map_id = options.get("expected_map_id", std::string());
    m_verified_map_id.clear();
    m_origin_col = options.get("origin_col", m_reorient_col);
    m_origin_row = options.get("origin_row", m_reorient_row);
    m_reorientation_destination.reset();
    m_spatial_target_callback = {};
    m_reoriented_field_members.clear();
    m_reorientation_reconcile = {};
    m_swap_origin = Point(options.get("swap_origin_x", -1), options.get("swap_origin_y", -1));
    const auto direction = options.get("direction", std::string("E"));
    if (direction == "E") {
        m_direction = Point(120, 0);
    }
    else if (direction == "W") {
        m_direction = Point(-120, 0);
    }
    else if (direction == "N") {
        m_direction = Point(0, -120);
    }
    else if (direction == "S") {
        m_direction = Point(0, 120);
    }
    else {
        return false;
    }
    m_deploy_only = options.get("deploy_only", false);
    m_replace_only = options.get("replace_only", false);
    m_replacement_reconcile_only = options.get("replacement_reconcile_only", false);
    m_replacement_postconditions_ready = false;
    m_replacement_original_preserved = false;
    m_replacement_committed = false;
    m_replacement_target_unavailable = false;
    m_replacement_above_blocking_device = false;
    m_registered_replacement_receiver.reset();
    m_field_pose_verified = false;
    m_direction_committed = false;
    m_preparation_hud.clear();
    m_selected_panel_evidence.clear();
    m_preparation_hud_cache_hits = 0;
    m_preparation_hud_fields_reused = 0;
    m_preparation_hud_ocr_checks = 0;
    m_settled_held_target.reset();
    m_rejected_drop_target.reset();
    m_held_board.release();
    m_deployment_grid_to_screen.release();
    m_growth_grid_to_screen.release();
    m_prior_grid_members.clear();
    m_observed_grid_members.clear();
    m_observed_grid_projection.release();
    m_observed_grid_reference.release();
    m_observed_boss_attackable_cells.clear();
    m_observed_field_members.clear();
    m_registered_grid_reference.release();
    m_registered_grid_projection.release();
    m_registered_grid_map_id.clear();
    if (const auto prior = options.find<json::object>("registered_grid")) {
        const auto values = prior->find<json::array>("projection");
        const auto file = prior->get("reference_file", std::string());
        const auto map_id = prior->get("map_id", std::string());
        if (!values || values->size() != 9 || file.empty() || !utils::path(file).is_absolute()) {
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
        if (!set_registered_grid_context(MAA_NS::imread(utils::path(file)), projection, map_id)) {
            return false;
        }
    }
    m_equipment_reference.release();
    m_fast_mode = options.get("fast_mode", false);
    m_best_effort_deployment = options.get("best_effort_deployment", false);
    m_prefer_safe_target = options.get("prefer_safe_target", false);
    m_role_aware_deployment = options.get("role_aware_deployment", false);
    m_primary_pact = options.get("primary_pact", std::string());
    m_primary_active = options.get("primary_active", false);
    m_active_placement_pacts.clear();
    if (const auto pacts = options.find<json::array>("active_placement_pacts")) {
        for (const auto& pact : *pacts) {
            if (pact.is_string()) {
                m_active_placement_pacts.push_back(pact.as_string());
            }
        }
    }
    m_placement_members.clear();
    m_verified_preview_feet.clear();
    m_replacement_growth_bonuses.clear();
    if (const auto bonuses = options.find<json::array>("replacement_growth_bonuses")) {
        if ((!m_replace_only && !bonuses->empty()) || bonuses->size() > 4) {
            return false;
        }
        for (const auto& item : *bonuses) {
            const auto bonus_direction = item.get("direction", std::string());
            const int bonus = item.get("bonus", -1);
            if (bonus_direction.size() != 1 || std::string("ENSW").find(bonus_direction.front()) == std::string::npos ||
                bonus < 0 || bonus > 100'000 ||
                !m_replacement_growth_bonuses.emplace(bonus_direction.front(), bonus).second) {
                return false;
            }
        }
    }
    if (const auto placed = options.find<json::array>("placement_units")) {
        for (const auto& item : *placed) {
            if (item.contains("field_pose_verified") && !item.get("field_pose_verified", false)) {
                continue;
            }
            if (item.get("field_pose_verified", false)) {
                m_verified_preview_feet.emplace_back(item.get("x", -1), item.get("y", -1));
            }
            const auto name = item.get("name", std::string());
            const auto placement_role = operator_placement_role(name);
            const auto props = BattleData.find_first_oper(battle::Role::Unknown, name, true);
            const auto facing = item.get("committed_facing", std::string());
            m_placement_members.push_back(
                { item.get("x", -1),
                  item.get("y", -1),
                  placement_role,
                  name,
                  facing.size() == 1 && std::string("ENSW").find(facing.front()) != std::string::npos ? facing.front()
                                                                                                      : '\0',
                  !props || accepts_direct_healing(props->sub_role) });
        }
    }
    m_safe_target = Point(options.get("safe_target_x", 680), options.get("safe_target_y", 360));
    m_excluded_deploy_points.clear();
    if (const auto points = options.find<json::array>("excluded_deploy_points")) {
        for (const auto& point : *points) {
            const int x = point.get("x", -1), y = point.get("y", -1);
            if (x < 180 || x >= 1000 || y < 140 || y >= 620 || m_excluded_deploy_points.size() >= 100) {
                return false;
            }
            m_excluded_deploy_points.emplace_back(x, y);
        }
    }
    m_find_bench_source = options.get("find_bench_source", false);
    m_recent_acquisition_source = options.get("recent_acquisition_source", false);
    if (m_find_bench_source && !m_deploy_only && !m_sell_only) {
        return false;
    }
    m_move_only = options.get("move_only", false);
    m_finalize_direction = options.get("finalize_direction", false);
    if (m_finalize_direction && !m_move_only) {
        return false;
    }
    m_drag_origin = Point(options.get("drag_origin_x", -1), options.get("drag_origin_y", -1));
    m_withdraw_only = options.get("withdraw_only", false);
    m_source = Point(options.get("source_x", -1), options.get("source_y", -1));
    m_target = Point(options.get("target_x", -1), options.get("target_y", -1));
    m_drag_target = Point(options.get("drag_target_x", m_target.x), options.get("drag_target_y", m_target.y));
    m_name = options.get("name", std::string());
    if (m_verify_source_form &&
        (!m_find_bench_source || !(m_deploy_only || m_sell_only) || m_name.empty() || m_name == "*")) {
        return false;
    }
    m_replaced = options.get("replaced", std::string());
    // Direct projection is a separately reserved single-body transaction.
    // Random phone grants still require physical inventory reconciliation.
    if (m_direct_projection && (m_name != "博士投影" || m_target_elite || !season_combat_stats(m_replaced, false) ||
                                !season_combat_stats(m_replaced, true))) {
        return false;
    }
    if (m_promotion_consumable && (!m_equip_only || !m_find_equipment_source ||
                                   (!m_direct_projection && !strategy_core_consumable_supported(
                                                                options.get("strategy_id", std::string()),
                                                                m_replaced,
                                                                m_name,
                                                                m_target_elite)))) {
        return false;
    }
    if (m_promotion_consumable && m_name == "简易通讯机" &&
        (m_consumable_free_bench_slots < 1 || m_consumable_free_bench_slots > 10)) {
        return false;
    }
    m_pact_recruitment_pact = options.get("pact_recruitment_pact", std::string());
    m_pact_recruitment_shop_level = options.get("pact_recruitment_shop_level", 0);
    if (!m_pact_recruitment_pact.empty() && (m_name != "寻呼模块" || !m_equip_only || !m_find_equipment_source ||
                                             m_pact_recruitment_shop_level < 1 || m_pact_recruitment_shop_level > 6 ||
                                             !safe_pact_recruitment_recipient(
                                                 m_replaced,
                                                 m_pact_recruitment_pact,
                                                 options.get("target_equipment_known", false),
                                                 {}))) {
        return false;
    }
    if ((m_reorient_col != -1 || m_reorient_row != -1) &&
        (!m_face_only || !m_preserve_population || m_reorient_col < 0 || m_reorient_col > 10 || m_reorient_row < 0 ||
         m_reorient_row > 4 || m_source != m_target || !m_replaced.empty())) {
        return false;
    }
    if (m_relocate_field &&
        (!m_face_only || !m_preserve_population || m_reorient_col < 0 || m_origin_col < 0 || m_origin_col > 10 ||
         m_origin_row < 0 || m_origin_row > 4 || m_expected_map_id.empty() || m_relocation_displaced == m_name)) {
        return false;
    }
    if (m_find_equipment_source) {
        if (!m_equip_only || m_replaced.empty()) {
            return false;
        }
        const auto profile =
            std::find_if(equipment_profiles().begin(), equipment_profiles().end(), [&](const auto& item) {
                return item.name == m_name;
            });
        const auto expected = options.find<json::array>("expected_equipment");
        const bool pact_recruitment = m_name == "寻呼模块" && !m_pact_recruitment_pact.empty();
        const bool cash = is_immediate_cash_equipment(m_name) || is_deferred_cash_equipment(m_name);
        if (profile == equipment_profiles().end() ||
            (profile->consumable && !pact_recruitment && !cash && m_identity_tag_pact.empty() &&
             !m_promotion_consumable) ||
            !expected || expected->size() >= static_cast<size_t>(equipment_capacity(m_replaced, m_target_elite)) ||
            (pact_recruitment && !expected->empty()) || !options.get("target_equipment_known", false)) {
            return false;
        }
        for (const auto& item : *expected) {
            if (!item.is_string() || !equipment_title_matches(item.as_string(), item.as_string()) ||
                item.as_string() == m_name ||
                std::find(m_expected_equipment.begin(), m_expected_equipment.end(), item.as_string()) !=
                    m_expected_equipment.end()) {
                return false;
            }
            m_expected_equipment.push_back(item.as_string());
        }
    }
    if (!m_identity_tag_pact.empty() &&
        (m_name != "随身身份牌" || !m_find_equipment_source || !m_equip_only ||
         !safe_identity_tag_recipient(m_replaced, m_identity_tag_pact, m_target_elite, m_expected_equipment))) {
        return false;
    }
    m_round = options.get("round", -1);
    m_funds = options.get("funds", -1);
    m_remaining = options.get("remaining_deployment", -1);
    if (m_replacement_reconcile_only && !m_replace_only) {
        return false;
    }
    if (m_replace_only &&
        (!m_deploy_only || !m_find_bench_source || m_name.empty() || m_name == "*" || m_replaced.empty() ||
         m_name == m_replaced || m_remaining != 0 || !options.get("outgoing_all_copies_deployed", false))) {
        return false;
    }
    const auto in_field = [](const Point& p) {
        return p.x >= 180 && p.x < 1000 && p.y >= 140 && p.y < 510;
    };
    if (m_strategy_only && m_replaced == "幻画为真") {
        const auto items = options.find<json::array>("expected_equipment");
        if (!items || !options.get("target_equipment_known", false) ||
            items->size() > static_cast<size_t>(equipment_capacity(m_name, m_target_elite))) {
            return false;
        }
        for (const auto& item : *items) {
            if (!item.is_string() || !equipment_title_matches(item.as_string(), item.as_string())) {
                return false;
            }
            m_expected_equipment.push_back(item.as_string());
        }
        std::sort(m_expected_equipment.begin(), m_expected_equipment.end());
        return in_field(m_target) && m_drag_target == m_target && m_source.x >= 1190 && m_source.x < 1270 &&
               m_source.y >= 200 && m_source.y < 280 && m_round >= 1 && m_round <= 20 && m_funds >= 0 &&
               m_funds <= 999 && m_remaining >= 0 && m_remaining <= 8 && !m_face_only && !m_move_only &&
               !m_deploy_only && !m_sell_only && !m_inspect_only && !m_terrain_probe_only && !m_equip_only &&
               !m_withdraw_only && operator_pacts().contains(m_name);
    }
    if (m_strategy_only && m_replaced == "全场聚焦") {
        return can_queue_temporary_elite(
                   m_funds,
                   true,
                   true,
                   m_target_elite,
                   m_strategy_target_on_field ? in_field(m_target) : m_target.y >= 140 && m_target.y < 420,
                   !m_strategy_target_on_field && deployment_source_is_bench(m_target.x, m_target.y)) &&
               in_field(m_target) && in_field(m_drag_target) && m_source.x >= 1190 && m_source.x < 1270 &&
               m_source.y >= 200 && m_source.y < 280 && m_round >= 1 && m_round <= 20 && m_remaining >= 0 &&
               m_remaining <= 8 && !m_face_only && !m_move_only && !m_deploy_only && !m_sell_only && !m_inspect_only &&
               !m_terrain_probe_only && !m_equip_only && !m_withdraw_only && !m_name.empty();
    }
    if (m_strategy_only) {
        const auto effects = acquired_effects(m_name, m_target_elite);
        if (!effects || effects->empty()) {
            return false;
        }
        if (const auto baselines = options.find<json::array>("pact_baselines")) {
            for (const auto& baseline : *baselines) {
                const auto pact = baseline.find<std::string>("pact");
                const auto value = baseline.find<int>("layers");
                if (!pact || !value || !m_pact_baselines.emplace(*pact, *value).second) {
                    return false;
                }
            }
        }
        if (m_pact_baselines.empty() && effects->size() == 1 && m_layers_before >= 0) {
            m_pact_baselines.emplace(effects->front().target, m_layers_before);
        }
        if (m_pact_baselines.size() != effects->size()) {
            return false;
        }
        for (const auto& effect : *effects) {
            const auto baseline = m_pact_baselines.find(effect.target);
            if (effect.kind != AcquiredEffectKind::Layers || effect.requires_observed_amount || effect.amount <= 0 ||
                baseline == m_pact_baselines.end() || baseline->second < 0 || baseline->second > 999 - effect.amount) {
                return false;
            }
        }
        return m_replaced == "御守之力" && in_field(m_target) && in_field(m_drag_target) && m_source.x >= 1190 &&
               m_source.x < 1270 && m_source.y >= 200 && m_source.y < 280 && m_round >= 1 && m_round <= 20 &&
               m_funds >= 0 && m_funds <= 999 && m_remaining >= 0 && m_remaining <= 9 && !m_face_only && !m_move_only &&
               !m_deploy_only && !m_sell_only && !m_inspect_only && !m_terrain_probe_only && !m_equip_only &&
               !m_withdraw_only;
    }
    if (m_preserve_population && (!m_face_only || (!m_replaced.empty() && !in_field(m_swap_origin)))) {
        return false;
    }
    if (m_sell_only && ((!m_find_bench_source && m_source.y < 420) || m_sale_credit < 0 || m_sale_credit > 99 ||
                        m_funds > 999 - m_sale_credit || m_minimum_sale_credit < 1 || m_minimum_sale_credit > 99)) {
        return false;
    }
    const bool destination_valid =
        (m_withdraw_only || m_inspect_only || m_terrain_probe_only || m_sell_only) ||
        (in_field(m_target) && (m_face_only || m_source != m_target) &&
         (m_move_only || m_deploy_only || m_face_only || (!m_replaced.empty() && m_name != m_replaced)));
    const bool source_valid =
        m_find_equipment_source ||
        ((m_deploy_only || m_sell_only) ? (m_find_bench_source || deployment_source_is_bench(m_source.x, m_source.y))
                                        : in_field(m_source));
    return (!m_move_only || (m_source.y < 420 && m_target.y < 420 && in_field(m_drag_origin))) && source_valid &&
           !m_name.empty() && (m_name != "*" || m_inspect_only || m_deploy_only) && destination_valid &&
           (m_withdraw_only || m_inspect_only || m_terrain_probe_only || m_sell_only || in_field(m_drag_target) ||
            (m_deploy_only && m_drag_target.x >= 180 && m_drag_target.x < 1000 && m_drag_target.y >= 140 &&
             m_drag_target.y < 620)) &&
           (static_cast<int>(m_move_only) + static_cast<int>(m_withdraw_only) + static_cast<int>(m_deploy_only) +
                static_cast<int>(m_face_only) + static_cast<int>(m_equip_only) + static_cast<int>(m_inspect_only) +
                static_cast<int>(m_terrain_probe_only) + static_cast<int>(m_sell_only) <=
            1) &&
           m_round >= 1 && m_round <= 20 && m_funds >= 0 && m_funds <= 999 && m_remaining >= 0 && m_remaining <= 9;
}

bool asst::spa::SwapTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    return msg == AsstMsg::SubTaskStart && details.get("subtask", std::string()) == "ProcessTask" &&
           details.get("details", "task", std::string()) == "MiniGame@SPA@SwapOperator";
}

std::optional<int> asst::spa::SwapTaskPlugin::read_selected_cash_equipment_credit(const cv::Mat& frame) const
{
    const bool passbook = m_name == "定期存折";
    OCRer effect(frame, passbook ? Rect(0, 164, 450, 536) : Rect(0, 350, 450, 350));
    const auto texts = effect.analyze();
    if (!texts) {
        return std::nullopt;
    }
    std::string description;
    std::vector<TextObservation> observations;
    for (const auto& text : *texts) {
        if (text.score < .85) {
            continue;
        }
        if (passbook) {
            observations.push_back({ text.text, text.score, text.rect.x, text.rect.y });
        }
        else {
            description += text.text;
        }
    }
    if (!passbook) {
        return equipment_cash_gain(m_name, description);
    }
    if (operator_panel_identity(observations, { m_name }) != m_name) {
        return std::nullopt;
    }
    std::erase_if(observations, [](const auto& text) { return text.y < 350; });
    std::sort(observations.begin(), observations.end(), [](const auto& left, const auto& right) {
        return std::tie(left.y, left.x) < std::tie(right.y, right.x);
    });
    for (const auto& text : observations) {
        description += text.text;
    }
    std::erase_if(description, [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; });
    constexpr std::string_view destruction = "装备时销毁";
    const auto start = description.find(destruction);
    const auto suffix = description.find("持有时每回合+1", start);
    const auto end = description.find(')', suffix);
    const auto full_width_end = description.find("）", suffix);
    if (start == std::string::npos || suffix == std::string::npos ||
        (end == std::string::npos && full_width_end == std::string::npos) ||
        description.find(destruction, start + destruction.size()) != std::string::npos) {
        return std::nullopt;
    }
    const auto after_end = full_width_end < end ? full_width_end + std::string_view("）").size() : end + 1;
    // The title and complete effect above were read from this one capture.
    constexpr std::uint64_t selected_panel_frame = 1;
    const auto quote = cash_equipment_redemption_quote(
        { m_name, m_name, description.substr(start, after_end - start), m_round, selected_panel_frame, true, true },
        m_round,
        selected_panel_frame);
    return quote ? std::optional(quote->amount) : std::nullopt;
}

bool asst::spa::SwapTaskPlugin::reacquire_cash_equipment_source(const Point& verified_source)
{
    m_equipment_stage = "cash_source_reacquisition";
    const auto frame = ctrler()->get_image();
    const auto candidates = equipment_inventory_candidates(frame);
    const cv::Point anchor(verified_source.x, verified_source.y);
    const auto source = stable_equipment_source(anchor, candidates);
    const auto reject = [&](const std::string& reason, const std::vector<cv::Point>& refreshed) {
        json::array points;
        for (const auto& point : refreshed) {
            points.emplace_back(json::object { { "x", point.x }, { "y", point.y } });
        }
        auto info = basic_info_with_what("SPACashEquipmentSourceDeferred");
        info["details"] = json::object { { "equipment", m_name },
                                         { "round", m_round },
                                         { "source_x", anchor.x },
                                         { "source_y", anchor.y },
                                         { "candidates", std::move(points) },
                                         { "reason", reason },
                                         { "contact_release_attempted", false } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        save_evidence(ctrler()->get_image(), utils::path("debug/SPA/cash_source_reacquisition"), false);
        return false;
    };
    if (!source) {
        return reject("No unique safe candidate near the verified cash source", candidates);
    }
    m_equipment_stage = "cash_source_title_recheck";
    const auto point = candidates[*source];
    const auto title = bench_identity(Point(point.x, point.y));
    if (!title || !equipment_title_matches(m_name, *title)) {
        close_selection();
        return reject("The reacquired cash source title did not match", candidates);
    }
    const auto credit = read_selected_cash_equipment_credit(ctrler()->get_image());
    if (!credit || *credit != m_equipment_cash_credit) {
        close_selection();
        return reject("The reacquired cash source effect did not match its quote", candidates);
    }
    if (!close_selection() || !ensure_expanded_shop()) {
        return false;
    }
    m_equipment_stage = "cash_source_reacquisition";
    m_cash_source_inventory = equipment_inventory_candidates(ctrler()->get_image());
    // Keep the original anchor through title inspection. Small successive
    // camera shifts must not accumulate into permission to drag another slot.
    const auto final_source = stable_equipment_source(anchor, m_cash_source_inventory);
    if (!final_source) {
        return reject("The verified cash source moved or became ambiguous", m_cash_source_inventory);
    }
    m_equipment_source_index = *final_source;
    const auto settled = m_cash_source_inventory[*final_source];
    m_source = Point(settled.x, settled.y);
    auto info = basic_info_with_what("SPACashEquipmentSourceReacquired");
    info["details"] = json::object { { "equipment", m_name },
                                     { "round", m_round },
                                     { "source_x", m_source.x },
                                     { "source_y", m_source.y },
                                     { "candidate_count", m_cash_source_inventory.size() },
                                     { "source_title_verified", true },
                                     { "cash_gain_quote", *credit },
                                     { "contact_release_attempted", false } };
    callback(AsstMsg::SubTaskExtraInfo, info);
    return true;
}

bool asst::spa::SwapTaskPlugin::on_run_fails()
{
    if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
        process->override_next("MiniGame@SPA@SwapOperator", {});
    }
    return false;
}

bool asst::spa::SwapTaskPlugin::preparation_matches()
{
    return preparation_matches(ctrler()->get_image());
}

bool asst::spa::SwapTaskPlugin::preparation_matches(const cv::Mat& image)
{
    if (image.empty() || image.cols != 1280 || image.rows != 720) {
        return false;
    }
    std::array<bool, 4> reused {};
    if (m_fast_mode) {
        for (size_t index = 0; index < reused.size(); ++index) {
            reused[index] = m_preparation_hud.matches_region(index, image, m_round, m_funds, m_remaining);
            m_preparation_hud_fields_reused += reused[index] ? 1 : 0;
        }
    }
    if (std::all_of(reused.begin(), reused.end(), [](bool matched) { return matched; })) {
        ++m_preparation_hud_cache_hits;
        return true;
    }
    ++m_preparation_hud_ocr_checks;
    std::vector<TextObservation> texts;
    const auto read_hud = [&](const Rect& region) {
        OCRer ocr(image, region);
        const auto recognized = ocr.analyze();
        if (!recognized) {
            return false;
        }
        for (const auto& text : *recognized) {
            texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
        }
        return true;
    };
    // Named deployment revisits this gate several times per unit. The map
    // and shop descriptions supply none of the required round/budget/HUD
    // facts and were repeatedly adding full-screen detection latency.
    if (m_fast_mode) {
        // Round and funds are read numerically below. Pact icons and layer
        // captions across the top are unrelated to transaction completion.
        if ((!reused[2] && !read_hud(Rect(1100, 70, 180, 85))) ||
            (!reused[3] && !read_hud(Rect(1090, 350, 190, 165)))) {
            return false;
        }
    }
    else if (!read_hud(Rect(0, 0, 1280, 720))) {
        return false;
    }
    const auto page = observe_page(texts, {});
    const auto round = reused[0] ? std::optional(m_round) : preparation_round_number(image, m_round);
    const auto funds = reused[1] ? std::optional(m_funds) : shop_number(image, Rect(1180, 510, 45, 40));
    const auto remaining_badge = reused[3] ? std::optional(m_remaining) : remaining_deployment_number(image, texts);
    bool ready_text_ok = reused[2];
    for (const auto& text : texts) {
        if (text.score >= 0.9 && text.text == "准备就绪" && text.x > 1100 && text.y < 150) {
            ready_text_ok = true;
        }
    }
    const bool remaining_ok = remaining_badge == m_remaining;
    const bool funds_ok = funds == m_funds || (m_funds == 0 && (page.page == Page::Preparation || ready_text_ok));
    // Expanded-hand cards can produce unrelated OCR numerals and mark the
    // page contradictory even after the game has already returned to normal
    // preparation.  The dedicated round/funds/remaining fields are the
    // authoritative reconciliation values here.
    const bool verified =
        (page.page == Page::Preparation || ready_text_ok) && round == m_round && funds_ok && remaining_ok;
    if (verified && m_fast_mode) {
        m_preparation_hud.remember_verified(
            image,
            m_round,
            m_funds,
            m_remaining,
            observe_deployment_counter(texts).label_y,
            funds == m_funds);
    }
    return verified;
}

bool asst::spa::SwapTaskPlugin::restore_preparation()
{
    // Both shop layouts have a label-anchored population reader. Expanding
    // the shop cannot repair a real population/funds mismatch and used to
    // turn failed reconciliation into unnecessary UI input.
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (preparation_matches()) {
            return true;
        }
        if (attempt < 2 && !sleep(250)) {
            return false;
        }
    }
    return false;
}

std::optional<std::string> asst::spa::SwapTaskPlugin::bench_identity(
    const Point& point,
    bool allow_sale_head_retry,
    const std::string& replacement_bench_name,
    Point* verified_contact,
    cv::Mat* verified_panel)
{
    m_selected_panel_evidence.clear();
    if (verified_panel) {
        verified_panel->release();
    }
    Point target = point;
    cv::Mat retry_reference;
    bool probe_shoulder_on_miss = false;
    if (overlaps_expanded_shop_controls(cv::Point(point.x, point.y))) {
        const auto frame = ctrler()->get_image();
        const auto head = exposed_bench_head(frame, cv::Point(point.x, point.y));
        if (!head || overlaps_expanded_shop_controls(*head) || !bench_inspection_panel_absent(frame)) {
            return std::nullopt;
        }
        target = Point(head->x, head->y);
        if (verified_contact || !replacement_bench_name.empty()) {
            retry_reference = frame;
            probe_shoulder_on_miss = true;
        }
    }
    // Badge detection has already found an occupied bench hitbox. A different
    // readable name is a conclusive miss; probing that same card three times
    // with full preparation OCR only delays the exchange and shifts the shop.
    if (target == point && allow_sale_head_retry && m_sell_only && m_find_bench_source && point.y == 450 &&
        point.x >= 940 && point.x <= 975) {
        retry_reference = ctrler()->get_image();
        if (!bench_inspection_panel_absent(retry_reference) ||
            !exposed_bench_head(retry_reference, cv::Point(point.x, point.y))) {
            retry_reference.release();
        }
    }
    static const auto known_names = [] {
        std::vector<std::string> names;
        for (const auto& [name, pacts] : operator_pacts()) {
            names.push_back(name);
        }
        for (const auto& profile : equipment_profiles()) {
            names.push_back(profile.name);
            if (!profile.advanced_effect.empty()) {
                names.push_back("进阶" + profile.name);
            }
        }
        for (const auto& summon : SummonRules) {
            names.emplace_back(summon.name);
        }
        return names;
    }();
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!ctrler()->click(target) || !sleep(180)) {
            return std::nullopt;
        }
        cv::Mat image;
        bool readable_title_seen = false;
        std::optional<double> previous_single_name;
        int frame_limit = 2;
        for (int frame = 0; frame < frame_limit; ++frame) {
            image = ctrler()->get_image();
            OCRer name(image, frame == 2 ? Rect(0, 165, 350, 75) : Rect(0, 170, 340, 65));
            if (const auto texts = name.analyze()) {
                std::vector<TextObservation> observations;
                for (const auto& text : *texts) {
                    observations.push_back({ text.text, text.score, text.rect.x, text.rect.y });
                    readable_title_seen = readable_title_seen || text.score >= .8;
                }
                auto identity = operator_panel_identity(observations, known_names);
                const auto& expected = replacement_bench_name.empty() ? m_name : replacement_bench_name;
                const auto single_name = single_operator_title_confidence(observations, expected);
                // Keep the same selected panel open. A one-character name
                // needs a second exact read, including one near the normal
                // threshold, before it can authorize any action.
                const bool confirmed_single_name = repeated_single_operator_title(previous_single_name, single_name);
                if (!identity &&
                    (confirmed_single_name || reread_single_operator_title(image, observations, expected))) {
                    identity = expected;
                }
                previous_single_name = single_name;
                if (identity) {
                    m_selected_panel_evidence.remember_recognized_title(image);
                    if (*identity == m_name && !selected_form_matches(image, *identity)) {
                        return std::nullopt;
                    }
                    if (*identity == m_name && verified_contact) {
                        *verified_contact = target;
                    }
                    if (verified_panel) {
                        *verified_panel = image;
                    }
                    LogInfo << "SPA bench identity" << *identity << target.x << target.y;
                    if (frame == 2) {
                        auto evidence = basic_info_with_what("SPAReplacementBenchTitleSettled");
                        evidence["details"] =
                            json::object { { "round", m_round },           { "expected_name", expected },
                                           { "observed_name", *identity }, { "source_x", target.x },
                                           { "source_y", target.y },       { "selection_repeated", false } };
                        callback(AsstMsg::SubTaskExtraInfo, evidence);
                    }
                    return identity;
                }
                // A partial title grants one more fresh read of this same
                // selected panel. Verification still requires the normal
                // confidence threshold and no competing recognized identity.
                if (single_name || operator_title_contains_rank_artifact(observations, expected) ||
                    bench_replacement_title_needs_settle(observations, replacement_bench_name)) {
                    frame_limit = 3;
                }
            }
            if (frame + 1 < frame_limit && !sleep(frame == 0 ? 120 : 220)) {
                return std::nullopt;
            }
        }
        // A recognised different name/form above is conclusive. Only a first
        // click that left the panel absent permits one current-frame probe of
        // the same slot. Equipment selection never opts in.
        const cv::Point source(point.x, point.y);
        if (attempt != 0) {
            return std::nullopt;
        }
        const auto retry = probe_shoulder_on_miss
                               ? bench_shoulder_retry_target(retry_reference, image, source, readable_title_seen)
                               : bench_head_retry_target(retry_reference, image, source, readable_title_seen);
        if (!retry || (probe_shoulder_on_miss && !preparation_matches(image))) {
            return std::nullopt;
        }
        target = Point(retry->x, retry->y);
        auto info = basic_info_with_what(
            probe_shoulder_on_miss ? "SPABenchShoulderInspectionRetry" : "SPABenchSaleHeadInspectionRetry");
        info["details"] = json::object { { "name", m_name },
                                         { "round", m_round },
                                         { "source_x", point.x },
                                         { "source_y", point.y },
                                         { "retry_x", target.x },
                                         { "retry_y", target.y },
                                         { "same_slot_badge_verified", true },
                                         { "first_click_panel_absent", true },
                                         { "identity_verified", false },
                                         { "sale_committed", false } };
        callback(AsstMsg::SubTaskExtraInfo, info);
    }
    return std::nullopt;
}

bool asst::spa::SwapTaskPlugin::selected_identity(const Point& point, const std::string& name, cv::Mat* verified_panel)
{
    // The sprite foot is not always the hitbox center on B-server maps, and
    // camera/animation settling can move the clickable area by a few pixels.
    // Only a small vertical hitbox correction is permitted. A readable wrong
    // identity disproves this source and must not launch a neighboring search.
    const std::initializer_list<Point> deploy_offsets = { Point(0, 0), Point(0, -15), Point(0, 15) };
    const std::initializer_list<Point> normal_offsets = { Point(0, 0), Point(0, -20) };
    const auto& offsets = m_deploy_only ? deploy_offsets : normal_offsets;
    for (const auto& offset : offsets) {
        const Point candidate(point.x + offset.x, point.y + offset.y);
        // Deployment is a best-effort phase-one action.  Keep an identity
        // miss short so one stale/occluded bench sprite cannot consume the
        // whole run timeout or reopen the same card indefinitely.
        if (!ctrler()->click(candidate) ||
            !sleep(
                (m_deploy_only || (m_fast_mode && (m_find_equipment_source || m_replace_only || m_find_bench_source ||
                                                   (m_face_only && m_preserve_population))))
                    ? 180
                    : 700)) {
            return false;
        }
        if (current_identity(name, verified_panel)) {
            return true;
        }
        const bool wrong_identity = m_last_panel_identity && *m_last_panel_identity != name;
        if (!close_selection() || wrong_identity) {
            return false;
        }
    }
    return false;
}

bool asst::spa::SwapTaskPlugin::selected_field_identity(
    const Point& point,
    const std::string& name,
    cv::Mat& verified_panel)
{
    if (!selected_identity(point, name, &verified_panel)) {
        return false;
    }
    const auto& retreat = TemplResource::get_instance().get_templ("MiniGame@SPA@RetreatSelected.png");
    if (selected_retreat_control(verified_panel, retreat)) {
        return true;
    }
    // The button can settle after the title. Recheck the title on that fresh
    // frame too, so a control from a different panel cannot prove this unit.
    return current_identity(name, &verified_panel) && selected_retreat_control(verified_panel, retreat).has_value();
}

std::optional<std::string> asst::spa::SwapTaskPlugin::selected_identity_any(const Point& point)
{
    // A deploy onto an occupied tile swaps the field operator back to the
    // bench.  Read that identity before starting the held drag so the caller
    // can update its roster instead of marking both operators as deployed.
    // A deployment target is normally an empty cell.  Only call this probe
    // when the cheap occupied-cell gate below says that a swap is plausible.
    // Stay inside the registered cell's hitbox neighborhood. Searching a full
    // tile above it could return a different operator as the displaced unit.
    const std::initializer_list<Point> offsets = { Point(0, 0), Point(0, -20) };
    for (const auto& offset : offsets) {
        const Point candidate(point.x + offset.x, point.y + offset.y);
        if (!ctrler()->click(candidate) || !sleep(260)) {
            continue;
        }
        OCRer ocr(ctrler()->get_image(), Rect(0, 80, 350, 230));
        if (const auto recognized = ocr.analyze()) {
            for (const auto& text : *recognized) {
                if (text.score >= 0.9 && text.rect.x < 30 && text.rect.y >= 175 && text.rect.y <= 210 &&
                    operator_pacts().contains(text.text)) {
                    const auto name = text.text;
                    // This is only an observation probe.  Dismiss the panel
                    // before the real source drag; no map click is used as a
                    // placement action here.
                    return close_selection() ? std::optional(name) : std::nullopt;
                }
            }
        }
        if (!close_selection()) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool asst::spa::SwapTaskPlugin::observe_deployed_pose(const cv::Mat& preparation_frame)
{
    m_field_pose_verified = false;
    if (m_held_board.empty() || m_name.empty() || m_name == "*") {
        return false;
    }
    cv::Mat collapsed;
    if (!capture_collapsed_preparation(collapsed, preparation_frame) &&
        (preparation_frame.empty() || !capture_collapsed_preparation(collapsed))) {
        return false;
    }
    const auto held_target = m_settled_held_target.value_or(m_drag_target);
    const cv::Point receiver(held_target.x, held_target.y);
    // The new unit's confirmed screen pose and retained grid must use the
    // same camera fit. No registration is shared across different frames.
    auto camera_fit = estimate_camera_projection(m_held_board, collapsed, true);
    const auto project_target = [&] {
        return m_deployment_grid_to_screen.empty()
                   ? registered_deployment_pose(camera_fit, receiver)
                   : registered_deployment_cell_pose(camera_fit, m_deployment_grid_to_screen, receiver);
    };
    auto projected = project_target();
    const bool used_post_drop_frame = !preparation_frame.empty() && collapsed.data == preparation_frame.data;
    if (!projected && used_post_drop_frame && capture_collapsed_preparation(collapsed)) {
        camera_fit = estimate_camera_projection(m_held_board, collapsed, true);
        projected = project_target();
    }
    if (!projected) {
        LogWarn << "SPA deployed pose camera registration is unknown" << m_name;
        return false;
    }
    const Point target(projected->x, projected->y);
    m_field_pose_verified = selected_identity(target, m_name) && close_selection();
    if (m_field_pose_verified) {
        m_target = target;
        if (!m_growth_grid_to_screen.empty()) {
            std::vector<cv::Point2f> grid;
            cv::perspectiveTransform(
                std::vector<cv::Point2f> { { static_cast<float>(held_target.x), static_cast<float>(held_target.y) } },
                grid,
                m_growth_grid_to_screen.inv());
            if (!grid.empty() && std::isfinite(grid.front().x) && std::isfinite(grid.front().y) &&
                grid.front().x >= -.2 && grid.front().x <= 10.2 && grid.front().y >= -.2 && grid.front().y <= 4.2) {
                const int col = static_cast<int>(std::lround(grid.front().x));
                const int row = static_cast<int>(std::lround(grid.front().y));
                if (std::abs(grid.front().x - col) <= .2 && std::abs(grid.front().y - row) <= .2) {
                    auto members = m_prior_grid_members;
                    members.push_back({ col, row, operator_placement_role(m_name), m_name });
                    if (same_row_growth_populations(members)) {
                        m_observed_grid_members = std::move(members);
                    }
                }
            }
        }
    }
    auto pose = basic_info_with_what("SPADeployedPoseObserved");
    pose["details"] = json::object { { "name", m_name },
                                     { "round", m_round },
                                     { "held_x", m_drag_target.x },
                                     { "held_y", m_drag_target.y },
                                     { "field_x", target.x },
                                     { "field_y", target.y },
                                     { "coordinate_frame", "collapsed" },
                                     { "pose_from_registered_cell_center", !m_deployment_grid_to_screen.empty() },
                                     { "target_identity_verified", m_field_pose_verified } };
    pose["details"]["grid_member_coordinates_verified"] = !m_observed_grid_members.empty();
    pose["details"]["post_drop_preparation_frame_reused"] =
        used_post_drop_frame && collapsed.data == preparation_frame.data;
    if (!m_observed_grid_members.empty()) {
        if (const auto projection = registered_grid_projection(camera_fit, m_growth_grid_to_screen)) {
            m_observed_grid_projection = projection->clone();
            m_observed_grid_reference = collapsed.clone();
        }
    }
    pose["details"]["grid_reference_retained"] = !m_observed_grid_reference.empty();
    pose["details"]["grid_member_count"] = m_observed_grid_members.size();
    callback(AsstMsg::SubTaskExtraInfo, pose);
    save_evidence(collapsed, utils::path("debug/SPA/post_deployment_map"), false);
    return m_field_pose_verified;
}

void asst::spa::SwapTaskPlugin::observe_boss_attackable_region(const cv::Mat& image, const cv::Mat& grid_to_image)
{
    const auto bar = preparation_boss_health_bar(image);
    const auto region = bar ? boss_attackable_region(image, *bar) : std::nullopt;
    if (!region) {
        return;
    }
    const auto cells = project_boss_attackable_cells(*region, grid_to_image);
    if (cells.empty()) {
        return;
    }
    m_observed_boss_attackable_cells = cells;
    json::array observed;
    for (const auto& cell : cells) {
        observed.emplace_back(json::object { { "col", cell.first }, { "row", cell.second } });
    }
    auto evidence = basic_info_with_what("SPABossAttackableRegionObserved");
    evidence["details"] = json::object { { "round", m_round },
                                         { "cells", std::move(observed) },
                                         { "source", "visible_red_diagonal_hatching_on_registered_grid" },
                                         { "any_cell_overlap_sufficient", true },
                                         { "occluded_area_inferred", false },
                                         { "physical_hit_verified", false } };
    callback(AsstMsg::SubTaskExtraInfo, evidence);
    save_evidence(image, utils::path("debug/SPA/boss_attackable_region"), false);
}

void asst::spa::SwapTaskPlugin::observe_clean_field_facings(
    const cv::Mat& image,
    std::vector<PlacementMember>& members,
    std::vector<GridPlacementMember>& grid,
    const std::string& source)
{
    json::array recovered;
    // registered_grid_members preserves the order of the verified field poses.
    for (std::size_t index = 0; index < members.size(); ++index) {
        auto& member = members[index];
        if (member.facing) {
            continue;
        }
        if (const auto facing = deployed_facing(image, cv::Point(member.x, member.y))) {
            member.facing = *facing;
            grid[index].facing = *facing;
            recovered.emplace_back(
                json::object { { "name", member.name },
                               { "facing", std::string(1, *facing) },
                               { "x", member.x },
                               { "y", member.y } });
        }
    }
    if (!recovered.empty()) {
        auto evidence = basic_info_with_what("SPAFieldFacingObserved");
        evidence["details"] = json::object { { "round", m_round },
                                             { "members", std::move(recovered) },
                                             { "source", source },
                                             { "extra_direction_input_issued", false } };
        callback(AsstMsg::SubTaskExtraInfo, evidence);
    }
}

bool asst::spa::SwapTaskPlugin::observe_grid_snapshot(
    const cv::Mat& reference,
    const cv::Mat& grid_to_reference,
    const std::vector<PlacementMember>& members,
    int round,
    int funds,
    int remaining)
{
    m_observed_grid_members.clear();
    m_observed_grid_reference.release();
    m_observed_grid_projection.release();
    m_observed_field_members.clear();
    if (reference.empty() || grid_to_reference.empty() || round < 1 || round > 20 || funds < 0 || funds > 999 ||
        remaining < 0 || remaining > 7 || members.size() != static_cast<std::size_t>(8 - remaining)) {
        return false;
    }
    m_round = round;
    m_funds = funds;
    m_remaining = remaining;
    m_fast_mode = true;
    m_best_effort_deployment = true;
    cv::Mat current;
    if (!capture_collapsed_preparation(current)) {
        return false;
    }
    std::vector<cv::Point> points;
    for (const auto& member : members) {
        points.emplace_back(member.x, member.y);
    }
    // The collapsed-field ledger must remain aligned with this reference.
    // Large camera changes need explicit identity/pose reconstruction, not
    // a guessed assignment of newly observed survivors to old logical cells.
    const auto camera_fit = estimate_camera_projection(reference, current, true);
    const auto projected = registered_field_points(camera_fit, points);
    if (!projected || projected->size() != members.size()) {
        return false;
    }
    auto aligned = members;
    for (std::size_t index = 0; index < members.size(); ++index) {
        if (cv::norm(projected->at(index) - points[index]) > 12) {
            return false;
        }
        aligned[index].x = projected->at(index).x;
        aligned[index].y = projected->at(index).y;
    }
    const auto projection = registered_grid_projection(camera_fit, grid_to_reference);
    auto mapped = projection ? registered_grid_members(*projection, aligned) : std::nullopt;
    if (!mapped) {
        save_evidence(current, utils::path("debug/SPA/grid_snapshot_unverified"), false);
        return false;
    }
    observe_clean_field_facings(current, aligned, *mapped, "registered_clean_chevron");
    m_observed_grid_members = std::move(*mapped);
    m_observed_field_members = std::move(aligned);
    m_observed_grid_projection = projection->clone();
    m_observed_grid_reference = current.clone();
    observe_boss_attackable_region(current, *projection);
    auto evidence = basic_info_with_what("SPARegisteredFormationObserved");
    evidence["details"] = json::object { { "round", round },
                                         { "member_count", m_observed_grid_members.size() },
                                         { "remaining_deployment", remaining },
                                         { "grid_coordinates_verified", true },
                                         { "identities_from_prior_field_ledger", true },
                                         { "fresh_identity_probes", false },
                                         { "board_changed", false } };
    callback(AsstMsg::SubTaskExtraInfo, evidence);
    return true;
}

bool asst::spa::SwapTaskPlugin::revalidate_promoted_pose(
    const std::string& name,
    const Point& point,
    int round,
    std::optional<bool> expected_elite)
{
    if (name.empty() || point.x < 180 || point.x >= 1000 || point.y < 140 || point.y >= 510) {
        return false;
    }
    m_name = name;
    m_round = round;
    m_fast_mode = true;
    m_best_effort_deployment = true;
    if (!close_selection() || !ensure_collapsed_shop()) {
        return false;
    }
    // The selected panel can appear after the first capture at the exact
    // prior footpoint. Re-read an unknown title without clicking again.
    if (!ctrler()->click(point) || !sleep(250)) {
        return false;
    }
    bool identity_verified = current_identity(name);
    const bool retry_title = !identity_verified && !m_last_panel_identity && sleep(200);
    if (retry_title) {
        identity_verified = current_identity(name);
    }
    // The retreat control distinguishes a field selection from a bench card.
    std::optional<RetreatControlMatch> retreat;
    std::optional<OperatorCardObservation> card;
    if (identity_verified) {
        const auto selected_image = ctrler()->get_image();
        retreat = selected_retreat_control(
            selected_image,
            TemplResource::get_instance().get_templ("MiniGame@SPA@RetreatSelected.png"));
        save_evidence(selected_image, utils::path("debug/SPA/promoted_pose_probe"), false);
        if (expected_elite) {
            card = selected_operator_card(selected_image, name);
        }
    }
    const bool form_verified = expected_elite && card && card->name == name && card->elite == *expected_elite;
    const bool verified = identity_verified && retreat.has_value() && (!expected_elite || form_verified);
    const bool restored = close_selection() && ensure_collapsed_shop();
    auto evidence = basic_info_with_what("SPAPromotedPoseProbe");
    evidence["details"] = json::object { { "name", name },
                                         { "round", round },
                                         { "x", point.x },
                                         { "y", point.y },
                                         { "identity_and_field_control_verified", verified },
                                         { "identity_verified", identity_verified },
                                         { "selected_title_retried", retry_title },
                                         { "form_verification_required", expected_elite.has_value() },
                                         { "form_verified", form_verified },
                                         { "training_level", card ? card->training_level : 0 },
                                         { "elite_form_observed", card && card->elite },
                                         { "retreat_score", retreat ? retreat->score : 0 },
                                         { "retreat_scale", retreat ? retreat->scale : 0 },
                                         { "preparation_restored", restored },
                                         { "coordinate_frame", "collapsed" },
                                         { "board_changed", false } };
    callback(AsstMsg::SubTaskExtraInfo, evidence);
    return verified && restored;
}

bool asst::spa::SwapTaskPlugin::probe_registered_grid(
    const std::vector<PlacementMember>& members,
    int round,
    int funds,
    int remaining,
    bool verify_all_identities)
{
    if (members.empty() || members.size() > 8 || remaining < 0 || remaining > 7 ||
        members.size() != static_cast<std::size_t>(8 - remaining) || round < 1 || round > 20 || funds < 0 ||
        funds > 999) {
        return false;
    }
    m_grid_probe_only = true;
    m_relocate_field = false;
    m_fast_mode = true;
    m_best_effort_deployment = true;
    m_face_only = true;
    m_preserve_population = true;
    m_round = round;
    m_funds = funds;
    m_remaining = remaining;
    m_placement_members = members;
    if (!close_selection() || !ensure_collapsed_shop() || !preparation_matches()) {
        return false;
    }
    if (verify_all_identities) {
        for (const auto& member : members) {
            cv::Mat selected, clean;
            if (!selected_field_identity(Point(member.x, member.y), member.name, selected) ||
                !capture_collapsed_preparation(clean, selected)) {
                return false;
            }
        }
    }
    m_name = members.front().name;
    m_source = Point(members.front().x, members.front().y);
    m_target = m_source;
    static_cast<void>(reorient_in_place());
    const bool complete = m_field_pose_verified && m_observed_grid_members.size() == members.size() &&
                          !m_observed_grid_projection.empty() && !m_observed_grid_reference.empty();
    auto receipt = basic_info_with_what("SPAFieldGridBootstrapObserved");
    receipt["details"] = json::object { { "round", round },
                                        { "complete", complete },
                                        { "all_member_identities_rechecked", verify_all_identities },
                                        { "member_count", members.size() },
                                        { "map_id", m_verified_map_id },
                                        { "held_input_started", m_reorientation_started },
                                        { "cancellation_verified", complete },
                                        { "direction_input_committed", false },
                                        { "board_changed", false } };
    callback(AsstMsg::SubTaskExtraInfo, receipt);
    return complete;
}

bool asst::spa::SwapTaskPlugin::reconcile_deployed_target()
{
    const auto frame = ctrler()->get_image();
    if (direction_modal_visible(frame)) {
        save_evidence(frame, utils::path("debug/SPA/deploy_direction_uncommitted"), false);
        LogWarn << "SPA deployment direction remains open before identity reconciliation";
        return false;
    }
    // Use the held tile's registered camera transform. An ambiguous transform
    // defers reconciliation; guessed offsets and whole-field probing cannot
    // establish which tile received this deployment.
    return observe_deployed_pose();
}

bool asst::spa::SwapTaskPlugin::selected_form_matches(const cv::Mat& image, const std::string& name)
{
    if (!m_verify_source_form) {
        return true;
    }
    const auto card = selected_operator_card(image);
    const bool matched = card && card->name == name && card->elite == m_target_elite;
    auto evidence = basic_info_with_what("SPASelectedOperatorFormObserved");
    evidence["details"] = json::object { { "name", name },
                                         { "round", m_round },
                                         { "form_known", card.has_value() },
                                         { "training_level", card ? card->training_level : 0 },
                                         { "elite_observed", card && card->elite },
                                         { "expected_elite", m_target_elite },
                                         { "form_matches", matched },
                                         { "board_changed", false } };
    callback(AsstMsg::SubTaskExtraInfo, evidence);
    return matched;
}

bool asst::spa::SwapTaskPlugin::current_identity(const std::string& name, cv::Mat* verified_panel)
{
    m_last_panel_identity.reset();
    m_selected_panel_evidence.clear();
    if (verified_panel) {
        verified_panel->release();
    }
    auto image = ctrler()->get_image();
    const Rect roi = m_fast_mode ? Rect(0, 165, 350, 75) : Rect(0, 80, 350, 230);
    const auto read_title = [&](const cv::Mat& frame) {
        OCRer ocr(frame, roi);
        std::vector<TextObservation> tokens;
        if (const auto recognized = ocr.analyze()) {
            for (const auto& text : *recognized) {
                tokens.push_back({ text.text, text.score, text.rect.x, text.rect.y });
            }
        }
        return tokens;
    };
    const auto texts = read_title(image);
    std::vector<std::string> names;
    for (const auto& [known, pacts] : operator_pacts()) {
        names.push_back(known);
    }
    m_last_panel_identity = operator_panel_identity(texts, names);
    const bool expected_operator = operator_pacts().contains(name);
    if (!m_last_panel_identity && expected_operator && reread_single_operator_title(image, texts, name)) {
        m_last_panel_identity = name;
    }
    if (!m_last_panel_identity && expected_operator && m_replace_only) {
        const auto first = single_operator_title_confidence(texts, name);
        if (first && sleep(120)) {
            // Re-read the selected panel before offsetting the click. The
            // one-character title and the panel must both still match.
            m_selected_panel_evidence.remember_recognized_title(image);
            const auto settled = ctrler()->get_image();
            if (m_selected_panel_evidence.consume_unchanged(settled) &&
                repeated_single_operator_title(first, single_operator_title_confidence(read_title(settled), name))) {
                image = settled;
                m_last_panel_identity = name;
            }
        }
    }
    const auto remember_verified_panel = [&] {
        if (verified_panel) {
            *verified_panel = image;
        }
        return true;
    };
    if (m_last_panel_identity) {
        m_selected_panel_evidence.remember_recognized_title(image);
        if ((m_inspect_only || m_deploy_only) && name == "*") {
            m_name = *m_last_panel_identity;
            return remember_verified_panel();
        }
        if (*m_last_panel_identity == name) {
            return selected_form_matches(image, name) && remember_verified_panel();
        }
    }
    if (m_find_equipment_source && name == m_name) {
        for (const auto& text : texts) {
            if (text.score >= .9 && text.x >= 0 && text.x < 200 && text.y >= 170 && text.y <= 240 &&
                equipment_title_matches(name, text.text)) {
                return remember_verified_panel();
            }
        }
    }
    LogWarn << "SPA selected identity did not match" << name << m_last_panel_identity.value_or("unknown");
    return false;
}

bool asst::spa::SwapTaskPlugin::current_equipment(const std::string& name)
{
    OCRer ocr(ctrler()->get_image(), Rect(0, 500, 450, 220));
    const auto recognized = ocr.analyze();
    if (recognized) {
        for (const auto& text : *recognized) {
            if ((text.text == name || equipment_title_matches(name, text.text)) && text.score >= 0.85) {
                return true;
            }
        }
    }
    LogError << "SPA equipped item was not verified on the intended operator" << name;
    return false;
}

std::optional<std::vector<std::string>> asst::spa::SwapTaskPlugin::selected_equipment_names(
    size_t expected_count,
    const std::string& equipment_owner)
{
    const auto& identity = equipment_owner.empty() ? m_replaced : equipment_owner;
    std::vector<std::string> result;
    // Ordinary recipients expose two slots in the initial panel. A mech's
    // third through sixth expected item must be located explicitly, never
    // inferred from capacity or from successful input alone.
    const int page_limit = expected_count > 2 ? 6 : 1;
    for (int page = 0; page < page_limit; ++page) {
        const auto frame = ctrler()->get_image();
        // Include the fixed recipient title: a genuinely empty lower panel
        // produces no OCR tokens, whereas this same-frame identity anchor
        // distinguishes it from an unreadable/closed panel.
        OCRer ocr(frame, Rect(0, 165, 350, 555));
        const auto texts = ocr.analyze();
        if (!texts) {
            // An OCR failure is not an observed empty equipment list.
            return std::nullopt;
        }
        std::vector<TextObservation> panel_texts;
        for (const auto& text : *texts) {
            panel_texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
        }
        auto page_items = equipment_panel_items(panel_texts, identity);
        if (!page_items) {
            // Resize for full-panel OCR can make a clear title miss the
            // confidence threshold. Read the title crop from this same frame.
            OCRer title(frame, Rect(0, 165, 350, 75));
            if (const auto reread = title.analyze()) {
                std::vector<TextObservation> title_texts;
                for (const auto& text : *reread) {
                    title_texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
                }
                page_items = equipment_panel_items(panel_texts, identity, title_texts);
                if (page_items) {
                    LogTrace << "SPA equipment panel identity recovered from title crop" << m_replaced << m_round;
                }
            }
        }
        if (!page_items) {
            return std::nullopt;
        }
        for (const auto& name : *page_items) {
            if (std::find(result.begin(), result.end(), name) == result.end()) {
                result.push_back(name);
            }
        }
        if (page_limit > 1) {
            save_evidence(frame, utils::path("debug/SPA/equipment_panel_page"), false);
        }
        if (result.size() >= expected_count || page + 1 == page_limit) {
            break;
        }
        // Scroll only the inspected equipment column. If the phase or
        // recipient changes, defer the transaction instead of swiping a
        // battle/map surface. Partial reads remain mismatches to the ledger.
        if (!preparation_matches() || !ctrler()->swipe(Point(240, 675), Point(240, 525), 300) || !sleep(180)) {
            return std::nullopt;
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool asst::spa::SwapTaskPlugin::close_selection(cv::Mat* clean_frame, const cv::Mat& initial_frame)
{
    if (clean_frame) {
        clean_frame->release();
    }
    // The B-server keeps the selected operator panel open while still
    // showing the preparation HUD.  A plain preparation OCR check therefore
    // cannot prove that the map is clean. Clicking an empty, non-deployable
    // map area closes the panel without changing the roster; only then is the
    // normal preparation reconciliation authoritative.
    const auto image = initial_frame.empty() ? ctrler()->get_image() : initial_frame;
    if (m_fast_mode && (m_best_effort_deployment || m_find_equipment_source || m_find_bench_source)) {
        // Preparation callers have a verified HUD snapshot. Dismiss only a
        // visible left panel here; sale/replacement/deployment still perform
        // their own identity and transaction reconciliation before success.
        auto frame = image;
        bool input_issued = false;
        for (int attempt = 0; attempt < 4 && !frame.empty(); ++attempt) {
            // bench_identity/current_identity just read this panel. A fresh
            // exact pixel match avoids reading its title again merely to
            // close it. Evidence is consumed before any click; the following
            // frame still verifies that the panel has actually disappeared.
            bool selected_panel = m_selected_panel_evidence.consume_unchanged(frame);
            if (selected_panel) {
                LogTrace << "SPA selected panel pixels reused for close";
            }
            else {
                OCRer panel(frame, Rect(0, 165, 350, 75));
                if (const auto recognized = panel.analyze()) {
                    for (const auto& text : *recognized) {
                        if (text.score < 0.85 || text.rect.x >= 40 || text.rect.y < 170 || text.rect.y > 215) {
                            continue;
                        }
                        selected_panel =
                            selected_panel || operator_pacts().contains(text.text) ||
                            std::any_of(
                                equipment_profiles().begin(),
                                equipment_profiles().end(),
                                [&](const auto& item) { return equipment_title_matches(item.name, text.text); });
                    }
                }
            }
            if (!selected_panel) {
                // Unknown prototype names still expose the fixed range label.
                // Equipment panels use their title; they have no range label.
                OCRer range(frame, Rect(110, 285, 110, 40));
                if (const auto recognized = range.analyze()) {
                    selected_panel = std::any_of(recognized->begin(), recognized->end(), [](const auto& text) {
                        return text.score >= .85 && text.text == "攻击范围";
                    });
                }
            }
            if (!selected_panel) {
                if (clean_frame) {
                    *clean_frame = frame;
                }
                return true;
            }
            // Map point (1080,300) can contain an interactive roadblock. It
            // opened another detail panel instead of dismissing the source.
            // This gap is above the battlefield, clear of the HUD buttons.
            if (!input_issued) {
                if (!ctrler()->click(Point(1040, 80))) {
                    return false;
                }
                input_issued = true;
            }
            if (!sleep(250)) {
                return false;
            }
            frame = ctrler()->get_image();
        }
        return false;
    }
    if (!image.empty()) {
        OCRer ocr(image);
        bool placement_panel = false;
        if (const auto recognized = ocr.analyze()) {
            bool has_range = false;
            bool has_sale = false;
            for (const auto& text : *recognized) {
                if (text.score < 0.85) {
                    continue;
                }
                has_range = has_range || text.text == "攻击范围";
                has_sale = has_sale || text.text == "出售";
            }
            placement_panel = has_range && has_sale;
        }
        if (placement_panel && (!ctrler()->click(Point(1040, 80)) || !sleep(300))) {
            return false;
        }
    }
    return restore_preparation();
}

bool asst::spa::SwapTaskPlugin::ensure_expanded_shop(cv::Mat* already_visible_frame)
{
    return ensure_shop_visibility(true, already_visible_frame);
}

bool asst::spa::SwapTaskPlugin::ensure_collapsed_shop(cv::Mat* already_visible_frame)
{
    return ensure_shop_visibility(false, already_visible_frame);
}

bool asst::spa::SwapTaskPlugin::capture_collapsed_preparation(cv::Mat& image, const cv::Mat& selected_frame)
{
    image.release();
    cv::Mat clean;
    if (!close_selection(&clean, selected_frame) || !ensure_shop_visibility(false, &image, clean)) {
        return false;
    }
    // A toggle can change the label before the camera settles. Keep the
    // post-toggle HUD check before capturing the field that will be used.
    if (image.empty()) {
        if (!preparation_matches()) {
            return false;
        }
        image = ctrler()->get_image();
    }
    if (!preparation_matches(image)) {
        image.release();
        return false;
    }
    return true;
}

bool asst::spa::SwapTaskPlugin::ensure_shop_visibility(
    bool expanded,
    cv::Mat* already_visible_frame,
    const cv::Mat& initial_frame,
    bool* toggle_input_issued)
{
    if (already_visible_frame) {
        already_visible_frame->release();
    }
    if (toggle_input_issued) {
        *toggle_input_issued = false;
    }
    bool input_issued = false;
    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto image = attempt == 0 && !initial_frame.empty() ? initial_frame : ctrler()->get_image();
        const auto publish_frame = [&] {
            // After a toggle, the layout label can settle ahead of the
            // camera. Geometry callers keep their existing follow-up frame.
            if (already_visible_frame && attempt == 0) {
                *already_visible_frame = image;
            }
        };
        if (m_shop_toggle_evidence.observe_unchanged(image) == expanded) {
            publish_frame();
            return true;
        }
        m_shop_toggle_evidence.clear();
        OCRer controls(image, Rect(1080, 620, 200, 100));
        std::optional<std::pair<bool, Rect>> control;
        if (const auto texts = controls.analyze()) {
            for (const auto& text : *texts) {
                if (const auto observed = observe_shop_toggle({ text.text, text.score, text.rect.x, text.rect.y })) {
                    if (control) {
                        return false;
                    }
                    control = std::pair { *observed, text.rect };
                }
            }
        }
        if (control && control->first == expanded) {
            m_shop_toggle_evidence.remember(image, expanded);
            publish_frame();
            return true;
        }
        if (control && !input_issued) {
            if (!ctrler()->click(control->second)) {
                return false;
            }
            input_issued = true;
            if (toggle_input_issued) {
                *toggle_input_issued = true;
            }
        }
        // After one click, only observe. A delayed frame must never toggle
        // the shop back before the first transition becomes visible.
        if (attempt < 3 && !sleep(250)) {
            return false;
        }
    }
    return false;
}

bool asst::spa::SwapTaskPlugin::set_reconciliation_context(int round, int funds)
{
    if (!m_replacement_postconditions_ready || round < m_round || round > 20 || funds < 0 || funds > 999) {
        return false;
    }
    m_round = round;
    m_funds = funds;
    m_field_pose_verified = false;
    return true;
}

bool asst::spa::SwapTaskPlugin::reconcile_replacement()
{
    // Verification-only retry: never repeats the held drag or direction
    // input. Those inputs already produced an identified incoming operator
    // and a reconciled unchanged field population in this transaction.
    if (!m_replace_only || !m_replacement_committed || !m_replacement_postconditions_ready || !close_selection() ||
        !restore_preparation()) {
        return false;
    }
    if (!m_field_pose_verified) {
        if (!ensure_collapsed_shop() || !selected_identity(m_target, m_name) || !close_selection() ||
            !preparation_matches()) {
            return false;
        }
        m_field_pose_verified = true;
    }
    int probes = 0;
    if (!verify_replacement_bench_identity(m_replaced, probes)) {
        save_evidence(ctrler()->get_image(), utils::path("debug/SPA/replacement_unverified"), false);
        return false;
    }
    m_displaced_name = m_replaced;
    auto replaced = basic_info_with_what("SPAReplacementVerified");
    replaced["details"] = json::object { { "incoming", m_name },
                                         { "outgoing", m_replaced },
                                         { "round", m_round },
                                         { "remaining_deployment", m_remaining },
                                         { "population_preserved", true },
                                         { "incoming_placement_identity_verified", true },
                                         { "outgoing_bench_identity_verified", true },
                                         { "outgoing_bench_probes", probes },
                                         { "hud_cache_hits", m_preparation_hud_cache_hits },
                                         { "hud_fields_reused", m_preparation_hud_fields_reused },
                                         { "hud_ocr_checks", m_preparation_hud_ocr_checks },
                                         { "source_slot_probed_first", true },
                                         { "held_target_x", m_drag_target.x },
                                         { "held_target_y", m_drag_target.y } };
    callback(AsstMsg::SubTaskExtraInfo, replaced);
    return true;
}

bool asst::spa::SwapTaskPlugin::verify_replacement_bench_identity(const std::string& name, int& probes)
{
    cv::Mat bench;
    if (!ensure_expanded_shop(&bench)) {
        return false;
    }
    if (bench.empty()) {
        bench = ctrler()->get_image();
    }
    const bool find_christine = name == "Miss.Christine";
    const auto candidates = detect_bench_centres(bench, find_christine);
    std::vector<int> candidate_x;
    for (const auto& candidate : candidates) {
        candidate_x.push_back(candidate.x);
    }
    for (const auto index : bench_source_probe_order(candidate_x, m_source.x)) {
        const auto& candidate = candidates[index];
        const bool compact_sprite =
            find_christine && bench_christine_sprite_visible(bench, cv::Point(candidate.x, candidate.y));
        if (!compact_sprite && !bench_sprite_visible(bench, candidate)) {
            continue;
        }
        // The initial observation already verified the expanded layout.
        // A previous identity probe may have collapsed it.
        if (probes > 0 && !ensure_expanded_shop()) {
            return false;
        }
        ++probes;
        const auto hitbox = compact_sprite ? Point(candidate.x, candidate.y + 27) : candidate;
        const bool matched = bench_identity(hitbox, false, name) == name;
        if (!close_selection()) {
            return false;
        }
        if (matched) {
            // Only the first probe uses the exact certified expanded frame.
            // Later panel transitions can move the bench before its next click.
            if (probes == 1 && m_bench_identity_hints) {
                remember_inventory_operator_title(
                    *m_bench_identity_hints,
                    bench,
                    { candidate.x, candidate.y },
                    name,
                    true);
            }
            return true;
        }
    }
    return false;
}

bool asst::spa::SwapTaskPlugin::set_registered_grid_context(
    const cv::Mat& reference,
    const cv::Mat& projection,
    const std::string& map_id)
{
    if (reference.type() != CV_8UC3 || reference.size() != cv::Size(1280, 720) || projection.type() != CV_64FC1 ||
        projection.rows != 3 || projection.cols != 3 || !cv::checkRange(projection) ||
        std::abs(cv::determinant(projection)) < 1e-8 || !terrain_layout_model(map_id)) {
        return false;
    }
    m_registered_grid_reference = reference.clone();
    m_registered_grid_projection = projection.clone();
    m_registered_grid_map_id = map_id;
    return true;
}

asst::spa::BoardRegistration asst::spa::SwapTaskPlugin::preserve_registered_board(
    const cv::Mat& image,
    BoardRegistration board,
    bool melee_only,
    const std::vector<PlacementMember>& held_members) const
{
    if (m_registered_grid_reference.empty() || m_registered_grid_projection.empty() ||
        (board.verified && board.map_id != m_registered_grid_map_id) || held_members.empty()) {
        return board;
    }
    auto projected = reproject_preparation_board(
        m_registered_grid_reference,
        image,
        m_registered_grid_projection,
        m_registered_grid_map_id,
        melee_only);
    if (!projected.verified || !registered_grid_members(projected.grid_to_screen, held_members, FieldView::Held)) {
        return board;
    }
    // Keep the independent colour-fit scores for diagnostics. Map identity
    // here comes from the verified reference and camera transform instead.
    projected.score = board.score;
    projected.alternative_score = board.alternative_score;
    return projected;
}

std::optional<std::vector<std::pair<std::string, bool>>> asst::spa::SwapTaskPlugin::inspect_bench_operator_forms(
    int round,
    int funds,
    int remaining,
    const std::map<std::string, bool>& unchanged_forms,
    const std::map<std::string, std::vector<std::string>>& expected_equipment)
{
    m_round = round;
    m_funds = funds;
    m_remaining = remaining;
    m_fast_mode = true;
    m_best_effort_deployment = true;
    cv::Mat frame;
    if (!close_selection() || !ensure_expanded_shop(&frame)) {
        return std::nullopt;
    }
    if (frame.empty()) {
        frame = ctrler()->get_image();
    }
    if (!preparation_matches(frame)) {
        return std::nullopt;
    }
    const auto centres = detect_bench_centres(frame, true);
    std::vector<std::pair<std::string, bool>> operators;
    const auto catalogue = operator_pacts();
    for (const auto& centre : centres) {
        if (need_exit()) {
            return std::nullopt;
        }
        cv::Mat panel;
        const auto name = bench_identity(Point(centre.x, centre.y + 30), false, {}, nullptr, &panel);
        std::optional<bool> elite;
        if (name && catalogue.contains(*name)) {
            const auto unchanged = unchanged_forms.find(*name);
            if (unchanged != unchanged_forms.end()) {
                elite = unchanged->second;
            }
            else if (const auto card = selected_operator_card(panel, *name)) {
                elite = card->elite;
            }
        }
        if (elite) {
            const auto items = expected_equipment.find(*name);
            if (items != expected_equipment.end() &&
                selected_equipment_names(items->second.size(), *name) != items->second) {
                elite.reset();
            }
        }
        if (!close_selection(&frame, panel) || !preparation_matches(frame)) {
            return std::nullopt;
        }
        if (elite) {
            operators.emplace_back(*name, *elite);
            continue;
        }
        const bool non_operator =
            name && !catalogue.contains(*name) &&
            (summon_rule(*name) || std::ranges::any_of(equipment_profiles(), [&](const auto& profile) {
                 return equipment_title_matches(profile.name, *name);
             }));
        if (!non_operator) {
            save_evidence(panel, utils::path("debug/SPA/bench_form_unverified"), false);
            LogWarn << "SPA initial/elite form unverified during bench reconciliation" << round << centre.x << centre.y
                    << name.value_or("");
            return std::nullopt;
        }
    }
    if (detect_bench_centres(frame, true) != centres || operators.size() > 10) {
        return std::nullopt;
    }
    return operators;
}

std::optional<bool> asst::spa::SwapTaskPlugin::inspect_field_operator_form(
    const std::string& name,
    const Point& point,
    int round,
    int funds,
    int remaining,
    std::optional<bool> unchanged_form)
{
    m_round = round;
    m_funds = funds;
    m_remaining = remaining;
    m_fast_mode = true;
    m_best_effort_deployment = true;
    cv::Mat field, panel;
    if (!capture_collapsed_preparation(field)) {
        return std::nullopt;
    }
    std::optional<bool> elite;
    if (selected_field_identity(point, name, panel)) {
        if (unchanged_form) {
            elite = unchanged_form;
        }
        else if (const auto card = selected_operator_card(panel, name)) {
            elite = card->elite;
        }
    }
    return capture_collapsed_preparation(field, panel) ? elite : std::nullopt;
}

bool asst::spa::SwapTaskPlugin::inspect_equipment_inventory(
    int round,
    int funds,
    int remaining,
    std::vector<InventoryOperatorHint>& hints,
    const std::function<bool()>& may_start_probe)
{
    const auto started = std::chrono::steady_clock::now();
    m_inspected_equipment.clear();
    m_inspected_summons.clear();
    if (round < 1 || round > 20 || funds < 0 || funds > 999 || remaining < 0 || remaining > 8) {
        return false;
    }
    m_round = round;
    m_funds = funds;
    m_remaining = remaining;
    m_fast_mode = true;
    m_best_effort_deployment = true;
    const auto restore_inventory_frame = [&](cv::Mat& frame) {
        cv::Mat clean;
        if (!close_selection(&clean) || !ensure_shop_visibility(true, &frame, clean)) {
            return false;
        }
        if (frame.empty()) {
            frame = ctrler()->get_image();
        }
        return !frame.empty();
    };
    cv::Mat inventory_frame;
    if (!may_start_probe() || !restore_inventory_frame(inventory_frame) || !preparation_matches(inventory_frame)) {
        return false;
    }
    save_evidence(inventory_frame, utils::path("debug/SPA/equipment_inventory_discovery"), false);
    const auto read_freeze = [](const cv::Mat& image) -> std::optional<bool> {
        OCRer ocr(image, Rect(1010, 420, 110, 70));
        if (const auto texts = ocr.analyze()) {
            for (const auto& text : *texts) {
                if (const auto frozen = observe_shop_freeze({ text.text, text.score, text.rect.x, text.rect.y })) {
                    return frozen;
                }
            }
        }
        return std::nullopt;
    };
    const auto freeze_before = read_freeze(inventory_frame);
    auto freeze_after = freeze_before;
    bool unknown_probe_input = false;
    bool ui_side_effect_observed = false;
    const auto original = equipment_inventory_candidates(inventory_frame);
    auto fresh_frame = inventory_frame;
    auto fresh = original;
    bool complete = original.size() <= 20;
    bool deadline_deferred = false;
    bool expanded_frame_restored = true;
    int candidate_frames = 1;
    int fresh_title_probes = 0;
    int operator_hints_reused = 0;
    json::array observations;
    for (std::size_t index = 0; index < original.size() && index < 20 && !need_exit(); ++index) {
        // Visual hints do not issue input. The current frame was captured
        // after opening the shop or closing the preceding title probe.
        const auto source = std::min_element(fresh.begin(), fresh.end(), [&](const auto& a, const auto& b) {
            return cv::norm(a - original[index]) < cv::norm(b - original[index]);
        });
        if (source == fresh.end() || cv::norm(*source - original[index]) > 25 ||
            !equipment_inventory_probe_safe(*source)) {
            complete = false;
            continue;
        }
        if (const auto hint = inventory_operator_visual_hint(hints, fresh_frame, *source)) {
            ++operator_hints_reused;
            observations.emplace_back(
                json::object { { "x", source->x },
                               { "y", source->y },
                               { "title_hint", hint->first },
                               { "item_kind", "operator_visual_hint" },
                               { "visual_similarity", hint->second },
                               { "fresh_identity_verified", false },
                               { "input_issued", false } });
            continue;
        }
        if (!may_start_probe()) {
            complete = false;
            deadline_deferred = true;
            break;
        }
        ++fresh_title_probes;
        const auto identity = bench_identity(Point(source->x, source->y));
        std::string equipment;
        if (identity) {
            for (const auto& profile : equipment_profiles()) {
                if (equipment_title_matches(profile.name, *identity)) {
                    equipment = profile.name;
                    break;
                }
            }
        }
        std::string item_kind = "unknown";
        if (!equipment.empty()) {
            item_kind = "equipment";
            m_inspected_equipment.emplace_back(equipment, Point(source->x, source->y));
        }
        else if (identity && operator_pacts().contains(*identity)) {
            item_kind = "operator";
            remember_inventory_operator_title(hints, fresh_frame, *source, *identity, true);
        }
        else if (identity && (summon_rule(*identity) || BattleData.find_first_oper(battle::Role::Drone, *identity))) {
            item_kind = "summon";
            m_inspected_summons.emplace_back(*identity, Point(source->x, source->y));
        }
        else {
            complete = false;
        }
        observations.emplace_back(
            json::object { { "x", source->x },
                           { "y", source->y },
                           { "title", identity.value_or("") },
                           { "item_kind", item_kind },
                           { "fresh_identity_verified", identity.has_value() },
                           { "equipment", equipment } });
        // Every probe is a title read in the inventory region. No dragging,
        // sale, card purchase or equipment replacement is authorized here.
        expanded_frame_restored = restore_inventory_frame(fresh_frame);
        if (!expanded_frame_restored) {
            complete = false;
            break;
        }
        if (index + 1 < original.size() && index + 1 < 20) {
            fresh = equipment_inventory_candidates(fresh_frame);
            ++candidate_frames;
        }
        if (!identity) {
            // Small summons and empty-looking sprites may lack a readable
            // title. Continue only if the same preparation and freeze state
            // are re-established, so one unread item cannot hide all later slots.
            unknown_probe_input = true;
            freeze_after = read_freeze(fresh_frame);
            ui_side_effect_observed = freeze_before && freeze_after && *freeze_before != *freeze_after;
            if (!freeze_before || !freeze_after || ui_side_effect_observed || !preparation_matches(fresh_frame)) {
                break;
            }
        }
    }
    const bool restored = (expanded_frame_restored || restore_inventory_frame(fresh_frame)) &&
                          preparation_matches(fresh_frame);
    if (restored && !unknown_probe_input) {
        freeze_after = read_freeze(fresh_frame);
        ui_side_effect_observed = freeze_before && freeze_after && *freeze_before != *freeze_after;
    }
    if (ui_side_effect_observed) {
        complete = false;
        save_evidence(ctrler()->get_image(), utils::path("debug/SPA/inventory_ui_side_effect"), false);
        LogWarn << "SPA inventory title probe changed shop freeze state; fresh observation required";
    }
    if (need_exit()) {
        complete = false;
    }
    const auto scan_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    auto receipt = basic_info_with_what("SPAEquipmentInventoryInspected");
    receipt["details"] =
        json::object { { "round", round },
                       { "detected_candidates_inspected", complete && restored },
                       { "inventory_complete", false },
                       { "preparation_restored", restored },
                       { "candidate_count", original.size() },
                       { "fresh_title_probes", fresh_title_probes },
                       { "deadline_deferred", deadline_deferred },
                       { "scan_ms", scan_ms },
                       { "candidate_frames", candidate_frames },
                       { "operator_visual_hints_reused", operator_hints_reused },
                       { "all_titles_freshly_verified", operator_hints_reused == 0 && complete && restored },
                       { "equipment_slots_observed", m_inspected_equipment.size() },
                       { "summon_slots_observed", m_inspected_summons.size() },
                       { "hud_cache_hits", m_preparation_hud_cache_hits },
                       { "hud_fields_reused", m_preparation_hud_fields_reused },
                       { "hud_ocr_checks", m_preparation_hud_ocr_checks },
                       { "item_quantities_inferred", false },
                       { "inventory_mutation_requested", false },
                       { "inventory_mutation_known", false },
                       { "unknown_probe_input", unknown_probe_input },
                       { "ui_side_effect_observed", ui_side_effect_observed },
                       { "shop_freeze_before_known", freeze_before.has_value() },
                       { "shop_freeze_before", freeze_before.value_or(false) },
                       { "shop_freeze_after_known", freeze_after.has_value() },
                       { "shop_freeze_after", freeze_after.value_or(false) },
                       { "fresh_observation_required", fresh_title_probes > 0 },
                       { "observations", std::move(observations) } };
    callback(AsstMsg::SubTaskExtraInfo, receipt);
    return complete && restored;
}

std::optional<bool> asst::spa::SwapTaskPlugin::deploy_preparation_summon(
    const std::string& name,
    const Point& bench_hint,
    const std::vector<SummonTarget>& targets,
    int round,
    int funds,
    int remaining,
    std::optional<SummonTarget>& verified_target)
{
    verified_target.reset();
    if (deployable_summon_owner(name).empty() || targets.empty() || m_registered_grid_reference.empty() ||
        !deployment_source_is_bench(bench_hint.x, bench_hint.y) || round < 1 || funds < 0 || remaining < 0 ||
        remaining > 8) {
        return std::nullopt;
    }
    m_round = round;
    m_funds = funds;
    m_remaining = remaining;
    m_fast_mode = true;
    if (!close_selection() || !ensure_expanded_shop() || !preparation_matches()) {
        return std::nullopt;
    }
    const auto identity = bench_identity(bench_hint);
    if (!close_selection() || !ensure_expanded_shop() || !identity || *identity != name || !preparation_matches()) {
        return std::nullopt;
    }
    const auto send = [&](InputEvent::Type type, const Point& point = Point()) {
        InputEvent event;
        event.type = type;
        event.point = point;
        return ctrler()->inject_input_event(event);
    };
    if (!send(InputEvent::Type::TOUCH_DOWN, bench_hint)) {
        return std::nullopt;
    }
    const auto finish = [&] {
        const bool released = send(InputEvent::Type::TOUCH_UP);
        return send(InputEvent::Type::COMMIT) && released;
    };
    const auto cancel = [&] {
        send(InputEvent::Type::TOUCH_MOVE, Point(20, 300));
        send(InputEvent::Type::COMMIT);
        finish();
    };
    if (!send(InputEvent::Type::COMMIT) || !sleep(250) || !send(InputEvent::Type::TOUCH_MOVE, Point(120, 560)) ||
        !send(InputEvent::Type::COMMIT) || !sleep(200)) {
        cancel();
        return std::nullopt;
    }
    const auto held = ctrler()->get_image();
    OCRer panel(held, Rect(0, 163, 340, 78));
    const auto held_names = panel.analyze();
    if (!held_names || !std::any_of(held_names->begin(), held_names->end(), [&](const auto& text) {
            return text.rect.x < 45 && text.score >= .88 && text.text == name;
        })) {
        cancel();
        return std::nullopt;
    }
    // Restricted summons may expose only one or two legal tiles. Register
    // the terrain independently, then let the same-cell green check decide
    // legality even when every projected tile center is occluded.
    const auto grid = registered_grid_projection(m_registered_grid_reference, held, m_registered_grid_projection);
    const bool boss_phase = preparation_boss_health_bar(held).has_value();
    std::optional<SummonTarget> chosen;
    std::optional<cv::Point> destination;
    if (grid) {
        for (const auto& target : targets) {
            const auto& [col, row] = target.cell;
            const auto interior = registered_green_cell_target(
                held,
                *grid,
                cv::Point(col, row),
                true,
                terrain_allows_deployment(
                    m_registered_grid_map_id,
                    target.cell,
                    deployable_summon_owner(name) != "温蒂",
                    boss_phase),
                true);
            if (interior) {
                chosen = target;
                destination = interior;
                break;
            }
        }
    }
    if (!chosen) {
        json::array considered;
        for (const auto& [cell, facing] : targets) {
            considered.emplace_back(
                json::object { { "col", cell.first },
                               { "row", cell.second },
                               { "planned_facing", facing ? std::string(1, facing) : "" } });
        }
        auto evidence = basic_info_with_what("SPASummonHeldTargetUnavailable");
        evidence["details"] = json::object { { "round", round },
                                             { "name", name },
                                             { "map_id", m_registered_grid_map_id },
                                             { "held_identity_verified", true },
                                             { "held_grid_verified", grid.has_value() },
                                             { "candidate_cells", std::move(considered) },
                                             { "drop_attempted", false } };
        save_evidence(held, utils::path("debug/SPA/summon_held_target_unavailable"), false);
        callback(AsstMsg::SubTaskExtraInfo, evidence);
    }
    if (!destination || !chosen || need_exit() ||
        !send(InputEvent::Type::TOUCH_MOVE, Point(destination->x, destination->y)) || !send(InputEvent::Type::COMMIT) ||
        !sleep(130)) {
        cancel();
        return std::nullopt;
    }
    const bool issued = finish();
    if (!issued || !sleep(450)) {
        return false;
    }
    const auto direction_frame = ctrler()->get_image();
    if (direction_modal_visible(direction_frame)) {
        const auto center = direction_selection_center(direction_frame);
        if (!center || chosen->facing == '\0' || cv::norm(*center - *destination) > 70) {
            save_evidence(direction_frame, utils::path("debug/SPA/summon_direction_unverified"), false);
            cancel_direction_modal(direction_frame);
            return false;
        }
        const auto [dx, dy] = growth_facing_step(chosen->facing);
        m_direction = Point(dx * 120, dy * 120);
        if (!drag_direction_only(Point(center->x, center->y))) {
            return false;
        }
        const auto directed = ctrler()->get_image();
        if (direction_modal_visible(directed)) {
            save_evidence(directed, utils::path("debug/SPA/summon_direction_unverified"), false);
            cancel_direction_modal(directed);
            return false;
        }
    }
    else {
        chosen->facing = '\0'; // A missing modal does not establish the placed heading.
    }
    if (!close_selection() || !ensure_collapsed_shop() || !preparation_matches()) {
        return false;
    }
    // The drop ends the green placement preview. Recover only the grid
    // geometry on the clean frame, then read the title at the placed cell.
    const auto after = ctrler()->get_image();
    if (direction_modal_visible(after)) {
        save_evidence(after, utils::path("debug/SPA/summon_direction_unverified"), false);
        cancel_direction_modal(after);
        return false;
    }
    const auto projected = registered_grid_projection(m_registered_grid_reference, after, m_registered_grid_projection);
    if (!projected) {
        save_evidence(after, utils::path("debug/SPA/summon_field_grid_unverified"), false);
        return false;
    }
    std::vector<cv::Point2f> point;
    cv::perspectiveTransform(
        std::vector<cv::Point2f> {
            { static_cast<float>(chosen->cell.first), static_cast<float>(chosen->cell.second) } },
        point,
        *projected);
    if (point.size() != 1 || !std::isfinite(point.front().x) || !std::isfinite(point.front().y) ||
        point.front().x < 180 || point.front().x >= 1050 || point.front().y < 130 || point.front().y >= 520) {
        return false;
    }
    const auto placed = bench_identity(Point(cvRound(point.front().x), cvRound(point.front().y) - 14));
    const bool preparation_restored = close_selection() && preparation_matches();
    if (placed && *placed == name && preparation_restored) {
        verified_target = chosen;
        save_evidence(after, utils::path("debug/SPA/summon_on_field"), false);
        return true;
    }
    save_evidence(after, utils::path("debug/SPA/summon_field_title_unverified"), false);
    return false;
}

bool asst::spa::SwapTaskPlugin::held_drag()
{
    const auto send = [&](InputEvent::Type type, const Point& point = Point()) {
        InputEvent event;
        event.type = type;
        event.point = point;
        return ctrler()->inject_input_event(event);
    };
    m_deployment_hint_verified = false;
    bool ok = send(InputEvent::Type::TOUCH_DOWN, m_source) && send(InputEvent::Type::COMMIT) && sleep(300);
    LogInfo << "SPA held drag begin" << m_source.x << m_source.y << "to" << m_drag_target.x << m_drag_target.y << ok;
    bool picked_named_joint_slot = false;
    if (ok) {
        const auto source_frame = ctrler()->get_image();
        save_evidence(source_frame, utils::path("debug/SPA/held_swap_source"), false);
        if (m_joint_named_bench_slot) {
            const auto match = inventory_operator_visual_hint(
                m_joint_named_bench_sprite, source_frame, *m_joint_named_bench_slot);
            picked_named_joint_slot = match && match->first == m_name;
        }
    }

    // Keep the contact held while moving the card to the far-left staging
    // area.  The game fades the operator detail panel there and exposes the
    // complete board/green legal-cell overlay.  Releasing at this point would
    // cancel the drag, so all map reacquisition happens before the final move.
    if (ok && (m_deploy_only || m_find_equipment_source)) {
        const Point staging(120, 580);
        for (int step = 1; ok && step <= 8; ++step) {
            const Point point(
                m_source.x + (staging.x - m_source.x) * step / 8,
                m_source.y + (staging.y - m_source.y) * step / 8);
            ok = send(InputEvent::Type::TOUCH_MOVE, point) && send(InputEvent::Type::COMMIT) && sleep(35);
        }
        if (ok) {
            auto map_image = ctrler()->get_image();
            m_held_board = map_image.clone();
            save_evidence(map_image, utils::path("debug/SPA/held_drag_map"), false);
            if (m_deploy_only && m_name != "*") {
                // Opening and closing a source panel can change the bench
                // camera. Verify the actually held identity from this frame,
                // rather than trusting the earlier source coordinate alone.
                bool held_identity = false;
                bool joined_title_fragments = false;
                bool conflicting_title = false;
                OCRer held_name(map_image, Rect(0, 160, 340, 65));
                if (const auto names = held_name.analyze()) {
                    std::vector<TextObservation> titles;
                    for (const auto& text : *names) {
                        titles.push_back({ text.text, text.score, text.rect.x, text.rect.y });
                        conflicting_title |= text.score >= .85 && text.rect.x < 40 && text.text != m_name &&
                                             operator_pacts().contains(text.text);
                    }
                    held_identity = std::any_of(names->begin(), names->end(), [&](const auto& text) {
                        return text.score >= .85 && text.rect.x < 40 && text.text == m_name;
                    });
                    if (!held_identity && !conflicting_title) {
                        joined_title_fragments = operator_panel_identity(titles, { m_name }) == m_name;
                        held_identity =
                            joined_title_fragments || reread_single_operator_title(map_image, titles, m_name);
                    }
                }
                // Joint portraits can cover the held title. Require a newly
                // read bench title, same-slot sprite at touch-down, and no
                // conflicting title. The legal green drop is verified below.
                const bool joint_identity = m_joint_deployment && !conflicting_title && picked_named_joint_slot;
                const bool identity_verified = !conflicting_title && (held_identity || joint_identity);
                auto identity = basic_info_with_what("SPAHeldOperatorIdentity");
                identity["details"] =
                    json::object { { "name", m_name },
                                   { "verified", identity_verified },
                                   { "joined_title_fragments", joined_title_fragments },
                                   { "source",
                                     identity_verified ? (held_identity ? "held_title" : "joint_bench_touch_continuity")
                                                       : "unverified" },
                                   { "round", m_round },
                                   { "drop_issued", false } };
                callback(AsstMsg::SubTaskExtraInfo, identity);
                if (!identity_verified) {
                    send(InputEvent::Type::TOUCH_MOVE, Point(20, 300));
                    send(InputEvent::Type::COMMIT);
                    send(InputEvent::Type::TOUCH_UP);
                    send(InputEvent::Type::COMMIT);
                    return false;
                }
            }
            // Identify the held operator before choosing its combat role.
            // The fast bench path intentionally avoids opening every card.
            if (m_name == "*") {
                OCRer held_name(map_image, Rect(0, 160, 340, 65));
                if (const auto names = held_name.analyze()) {
                    for (const auto& text : *names) {
                        if (text.score >= 0.85 && text.rect.x < 40 &&
                            BattleData.get_first_role(text.text) != battle::Role::Unknown) {
                            m_name = text.text;
                            break;
                        }
                    }
                }
            }
            // Spatial member coordinates come from the collapsed-map ledger.
            // Register them into this held frame before range/lane scoring.
            const bool needs_equipment_projection =
                !m_placement_members.empty() || m_replace_only || m_find_equipment_source;
            const auto equipment_projection =
                !m_equipment_reference.empty() && needs_equipment_projection
                    ? estimate_camera_projection(m_equipment_reference, map_image, true)
                    : CameraProjection {};
            if (!m_equipment_reference.empty() && !m_placement_members.empty()) {
                std::vector<PlacementMember> registered_members;
                for (auto member : m_placement_members) {
                    if (const auto point = project_supported_point(
                            equipment_projection,
                            cv::Point2f(static_cast<float>(member.x), static_cast<float>(member.y)))) {
                        member.x = cvRound(point->x);
                        member.y = cvRound(point->y);
                        registered_members.push_back(std::move(member));
                    }
                }
                m_placement_members = std::move(registered_members);
            }
            // Reacquire legal interiors from this held-camera frame. A fixed
            // screen lattice crosses tile seams when the shop/camera changes.
            const auto held_role = BattleData.get_first_role(m_name);
            const auto held_location = season_location_type(m_name, BattleData.get_location_type(held_role, m_name));
            const bool frontline = operator_placement_role(m_name) == PlacementRole::Frontline;
            std::vector<cv::Point> occluded;
            // Cancelled green inputs are exclusions, not on-field sprites.
            // Only registered actor poses can hide terrain during board fit.
            for (const auto& point : m_placement_members) {
                occluded.emplace_back(point.x, point.y);
            }
            BoardRegistration board;
            std::vector<Point> candidates;
            std::vector<cv::Point> measured;
            bool paired_target_verified = false;
            if (m_replace_only || m_find_equipment_source) {
                if (!m_equipment_reference.empty()) {
                    const auto paired = equipment_target_preview(
                        equipment_projection,
                        m_equipment_reference,
                        map_image,
                        cv::Point(m_target.x, m_target.y),
                        m_verified_preview_feet,
                        m_replace_only && m_replacement_above_blocking_device,
                        m_registered_replacement_receiver);
                    auto evidence = basic_info_with_what("SPAPairedRecipientPreview");
                    evidence["details"] =
                        json::object { { "verified", paired.verified },
                                       { "matches", paired.matches },
                                       { "inliers", paired.inliers },
                                       { "added_green", paired.added_green },
                                       { "left_green", paired.left_green },
                                       { "right_green", paired.right_green },
                                       { "target_x", paired.target.x },
                                       { "target_y", paired.target.y },
                                       { "equipment", m_find_equipment_source },
                                       { "replacement_above_blocking_device", m_replacement_above_blocking_device },
                                       { "verified_blocking_device_occlusion", paired.blocking_device_assisted },
                                       { "verified_lower_body_occlusion", paired.occlusion_assisted },
                                       { "verified_registered_receiver_overlay", paired.registered_cell_assisted } };
                    callback(AsstMsg::SubTaskExtraInfo, evidence);
                    if (paired.verified) {
                        measured.push_back(paired.target);
                        paired_target_verified = true;
                    }
                }
                if (measured.empty()) {
                    std::vector<cv::Point> occupied { cv::Point(m_drag_target.x, m_drag_target.y) };
                    for (const auto& member : m_placement_members) {
                        occupied.emplace_back(member.x, member.y);
                    }
                    auto aligned = align_occupied_preview(map_image, occupied);
                    auto target = cv::Point(m_drag_target.x, m_drag_target.y) + aligned.shift;
                    int preview_rechecks = 0;
                    // Sprite animations can obscure the perimeter for one frame.
                    // Hold at staging and retry without relaxing either check.
                    while ((!aligned.verified || !occupied_swap_hint(map_image, target)) && preview_rechecks < 3) {
                        if (!sleep(180)) {
                            ok = false;
                            break;
                        }
                        ++preview_rechecks;
                        map_image = ctrler()->get_image();
                        aligned = align_occupied_preview(map_image, occupied);
                        target = cv::Point(m_drag_target.x, m_drag_target.y) + aligned.shift;
                    }
                    if (preview_rechecks > 0) {
                        save_evidence(map_image, utils::path("debug/SPA/held_swap_preview_recheck"), false);
                    }
                    if (ok && aligned.verified && occupied_swap_hint(map_image, target)) {
                        measured.push_back(target);
                    }
                    auto alignment = basic_info_with_what("SPAOccupiedBoardRegistration");
                    alignment["details"] = json::object {
                        { "verified", aligned.verified },        { "matched_cells", aligned.matched_cells },
                        { "total_cells", aligned.total_cells },  { "shift_x", aligned.shift.x },
                        { "shift_y", aligned.shift.y },          { "target_hint", !measured.empty() },
                        { "preview_rechecks", preview_rechecks }
                    };
                    callback(AsstMsg::SubTaskExtraInfo, alignment);
                }
            }
            else {
                board = register_preparation_board(map_image, held_location == battle::LocationType::Melee, occluded);
                board = preserve_registered_board(
                    map_image,
                    std::move(board),
                    held_location == battle::LocationType::Melee,
                    m_placement_members);
            }
            if (!m_replace_only && !m_find_equipment_source && board.grid_verified) {
                const auto terrain = terrain_layout_model(board.map_id, board.boss_phase_visible);
                const auto active = terrain && m_round >= 1 && m_round <= 3
                                        ? active_preparation_lanes(*terrain, m_round)
                                        : std::vector<DeploymentLane>();
                for (const auto& cell : board.legal_cells) {
                    const bool active_route =
                        active.empty() || std::any_of(active.begin(), active.end(), [&](const auto& lane) {
                            return std::find(lane.path.begin(), lane.path.end(), BoardCell { cell.col, cell.row }) !=
                                   lane.path.end();
                        });
                    if (!frontline || !board.verified || (cell.on_ground_route && active_route)) {
                        measured.push_back(cell.center);
                    }
                }
            }
            else if (!m_replace_only && !m_find_equipment_source) {
                for (const auto& cell : detect_held_deployment_cells(map_image)) {
                    measured.push_back(cell.center);
                }
            }
            for (const auto& center : measured) {
                const bool used = std::any_of(
                    m_excluded_deploy_points.begin(),
                    m_excluded_deploy_points.end(),
                    [&](const Point& point) { return cv::norm(cv::Point(point.x, point.y) - center) < 50; });
                const bool occupied = held_cell_has_member(center.x, center.y, m_placement_members);
                if ((!used && !occupied) || m_replace_only || m_find_equipment_source) {
                    candidates.emplace_back(center.x, center.y);
                }
            }
            auto geometry = basic_info_with_what("SPABoardRegistration");
            if (board.verified) {
                m_verified_map_id = board.map_id;
            }
            geometry["details"] =
                json::object { { "verified", board.verified },
                               { "grid_verified", board.grid_verified },
                               { "map_id", board.map_id },
                               { "map_identity_from_registered_reference", board.identity_from_reference },
                               { "score", board.score },
                               { "alternative_score", board.alternative_score },
                               { "frontline_route_filter", board.verified && frontline },
                               { "candidates", candidates.size() } };
            callback(AsstMsg::SubTaskExtraInfo, geometry);
            if (board.grid_verified) {
                m_deployment_grid_to_screen = board.grid_to_screen.clone();
            }
            if (candidates.empty()) {
                // Release back over the staging bench, never at an unverified
                // field pixel. The preparation driver can reobserve/resume.
                ok = false;
                m_replacement_target_unavailable = m_replace_only;
            }
            else {
                bool role_target_selected = false;
                if (m_role_aware_deployment) {
                    const auto role = BattleData.get_first_role(m_name);
                    const auto placement_role = operator_placement_role(m_name);
                    const auto props = BattleData.find_first_oper(role, m_name, true);
                    const bool regeneration = props && props->sub_role == battle::SubRole::Support_Bard;
                    std::vector<std::pair<int, int>> legal;
                    std::vector<std::pair<int, int>> range;
                    if (const auto stats = season_combat_stats(m_name, m_target_elite)) {
                        range.assign(stats->basic_range.begin(), stats->basic_range.end());
                    }
                    else {
                        for (const auto& point : BattleData.get_range(role, m_name, m_target_elite ? 2 : 1)) {
                            range.emplace_back(point.x, point.y);
                        }
                    }
                    char facing = 'E';
                    std::vector<PlacementPreference> preferences;
                    std::vector<GridPlacementMember> grid_members;
                    std::size_t frontline_support_count = 0;
                    if (board.grid_verified && !board.grid_to_screen.empty()) {
                        const auto inverse = board.grid_to_screen.inv();
                        for (const auto& member : m_placement_members) {
                            std::vector<cv::Point2f> grid;
                            cv::perspectiveTransform(
                                std::vector<cv::Point2f> {
                                    { static_cast<float>(member.x), static_cast<float>(member.y) } },
                                grid,
                                inverse);
                            if (grid.empty() || !std::isfinite(grid.front().x) || !std::isfinite(grid.front().y) ||
                                std::abs(grid.front().x) > 100 || std::abs(grid.front().y) > 100) {
                                continue;
                            }
                            const int col = static_cast<int>(std::lround(grid.front().x));
                            const int row = static_cast<int>(std::lround(grid.front().y));
                            if (std::abs(grid.front().x - col) > .35 || std::abs(grid.front().y - row) > .35) {
                                continue;
                            }
                            grid_members.push_back(
                                { col, row, member.role, member.name, member.facing, member.accepts_direct_healing });
                        }
                    }
                    if (board.grid_verified && grid_members.size() == m_placement_members.size()) {
                        m_growth_grid_to_screen = board.grid_to_screen.clone();
                        m_prior_grid_members = grid_members;
                        std::vector<GridPlacementCell> cells;
                        for (const auto& cell : board.legal_cells) {
                            cells.push_back({ cell.col, cell.row, cell.center.x, cell.center.y });
                        }
                        preferences = spatial_growth_preferences(
                            m_name,
                            cells,
                            grid_members,
                            m_active_placement_pacts,
                            m_primary_pact);
                        if (placement_role == PlacementRole::Frontline) {
                            std::map<std::string, std::vector<std::pair<int, int>>> support_ranges;
                            for (const auto& member : grid_members) {
                                if (member.role != PlacementRole::Ranged) {
                                    continue;
                                }
                                const auto support_role = BattleData.get_first_role(member.name);
                                // The ordinary range is a conservative
                                // support hint; no active skill expansion or
                                // unobserved elite form is assumed here.
                                if (const auto stats = season_combat_stats(member.name, false)) {
                                    support_ranges[member.name].assign(
                                        stats->basic_range.begin(),
                                        stats->basic_range.end());
                                }
                                else {
                                    for (const auto& point : BattleData.get_range(support_role, member.name, 1)) {
                                        support_ranges[member.name].emplace_back(point.x, point.y);
                                    }
                                }
                            }
                            const auto support = frontline_support_preferences(cells, grid_members, support_ranges);
                            frontline_support_count = support.size();
                            preferences.insert(preferences.end(), support.begin(), support.end());
                        }
                    }
                    // Keep ranged/support bodies behind registered contact
                    // points before considering range or layer bonuses.
                    const bool needs_cover = needs_screened_position(m_name) || placement_role == PlacementRole::Healer;
                    if ((!m_replace_only || needs_cover) && board.grid_verified &&
                        grid_members.size() == m_placement_members.size() &&
                        (placement_role == PlacementRole::Ranged || placement_role == PlacementRole::Healer)) {
                        if (const auto terrain = terrain_layout_model(board.map_id, board.boss_phase_visible)) {
                            std::vector<std::vector<std::pair<int, int>>> paths;
                            for (const auto& lane : active_preparation_lanes(*terrain, m_round)) {
                                paths.push_back(lane.path);
                            }
                            std::vector<Point> safe;
                            for (const auto& candidate : candidates) {
                                const auto cell = std::find_if(
                                    board.legal_cells.begin(),
                                    board.legal_cells.end(),
                                    [&](const auto& entry) {
                                        return entry.center.x == candidate.x && entry.center.y == candidate.y;
                                    });
                                if (cell != board.legal_cells.end() && !support_precedes_frontline(
                                                                           { cell->col, cell->row },
                                                                           grid_members,
                                                                           paths,
                                                                           needs_cover)) {
                                    safe.push_back(candidate);
                                }
                            }
                            if (needs_cover && safe.empty()) {
                                m_replacement_target_unavailable = m_replace_only;
                                send(InputEvent::Type::TOUCH_MOVE, Point(20, 300));
                                send(InputEvent::Type::COMMIT);
                                send(InputEvent::Type::TOUCH_UP);
                                send(InputEvent::Type::COMMIT);
                                return false;
                            }
                            if (!safe.empty()) {
                                candidates = std::move(safe);
                            }
                        }
                    }
                    const auto growth_preference_count = preferences.size() - frontline_support_count;
                    // Blend the registered fixed-cell swap's directional
                    // growth bonuses with normal attack/healing coverage.
                    if (m_replace_only && candidates.size() == 1) {
                        for (const auto& [direction, bonus] : m_replacement_growth_bonuses) {
                            preferences.push_back({ candidates.front().x, candidates.front().y, direction, bonus });
                        }
                    }
                    const bool registered_range_scoring =
                        board.grid_verified && !m_placement_members.empty() &&
                        grid_members.size() == m_placement_members.size() && !range.empty() &&
                        (placement_role == PlacementRole::Healer || placement_role == PlacementRole::Ranged);
                    if (registered_range_scoring) {
                        std::vector<GridPlacementCell> grid_cells;
                        for (const auto& cell : board.legal_cells) {
                            grid_cells.push_back({ cell.col, cell.row, cell.center.x, cell.center.y });
                        }
                        const auto coverage =
                            range_placement_preferences(placement_role, grid_cells, grid_members, range, regeneration);
                        preferences.insert(preferences.end(), coverage.begin(), coverage.end());
                    }
                    // The support safety filter changes candidates above.
                    // Rebuild the actual selection domain, otherwise its
                    // earlier copy still admits an exposed rear-line cell.
                    for (const auto& point : candidates) {
                        legal.emplace_back(point.x, point.y);
                    }
                    std::size_t engagement_preferences = 0;
                    if (!m_replace_only && board.grid_verified) {
                        if (const auto terrain = terrain_layout_model(board.map_id, board.boss_phase_visible)) {
                            std::vector<GridPlacementCell> cells;
                            for (const auto& cell : board.legal_cells) {
                                if (std::find(legal.begin(), legal.end(), std::pair { cell.center.x, cell.center.y }) !=
                                    legal.end()) {
                                    cells.push_back({ cell.col, cell.row, cell.center.x, cell.center.y });
                                }
                            }
                            const auto engagement =
                                engagement_placement_preferences(placement_role, cells, terrain->lanes, range);
                            engagement_preferences = engagement.size();
                            preferences.insert(preferences.end(), engagement.begin(), engagement.end());
                        }
                    }
                    const bool ranged_output =
                        placement_role == PlacementRole::Ranged && role != battle::Role::Medic && !range.empty();
                    if (board.grid_verified && grid_members.size() == m_placement_members.size() &&
                        placement_role != PlacementRole::Healer &&
                        (ranged_output || uses_attack_approach_geometry(m_name))) {
                        if (const auto terrain = terrain_layout_model(board.map_id, board.boss_phase_visible)) {
                            const auto lanes = active_preparation_lanes(*terrain, m_round);
                            std::set<std::pair<int, int>> covered_positions;
                            for (const auto& cell : board.legal_cells) {
                                if (std::find(legal.begin(), legal.end(), std::pair { cell.center.x, cell.center.y }) ==
                                    legal.end()) {
                                    continue;
                                }
                                for (const char direction : { 'E', 'N', 'S', 'W' }) {
                                    const int value = attack_approach_value(
                                        { cell.col, cell.row },
                                        direction,
                                        grid_members,
                                        lanes,
                                        range,
                                        frontline);
                                    if (value > 0) {
                                        preferences.push_back({ cell.center.x, cell.center.y, direction, value });
                                        covered_positions.emplace(cell.center.x, cell.center.y);
                                    }
                                }
                            }
                            if (ranged_output && !covered_positions.empty()) {
                                std::erase_if(legal, [&](const auto& point) {
                                    return !covered_positions.contains(point);
                                });
                            }
                        }
                    }
                    if (const auto chosen = choose_role_placement(
                            placement_role,
                            legal,
                            m_placement_members,
                            range,
                            &facing,
                            preferences,
                            registered_range_scoring,
                            regeneration)) {
                        m_drag_target = Point(chosen->first, chosen->second);
                        m_direction = facing == 'E'   ? Point(120, 0)
                                      : facing == 'W' ? Point(-120, 0)
                                      : facing == 'S' ? Point(0, 120)
                                                      : Point(0, -120);
                        role_target_selected = true;
                        auto planned = basic_info_with_what("SPADeploymentRoleTarget");
                        planned["details"] =
                            json::object { { "name", m_name },
                                           { "role", static_cast<int>(placement_role) },
                                           { "x", chosen->first },
                                           { "y", chosen->second },
                                           { "direction", std::string(1, facing) },
                                           { "range_cells", range.size() },
                                           { "spatial_growth_preferences", growth_preference_count },
                                           { "frontline_support_preferences", frontline_support_count },
                                           { "engagement_preferences", engagement_preferences },
                                           { "replacement_growth_directions", m_replacement_growth_bonuses.size() },
                                           { "registered_range_scoring", registered_range_scoring },
                                           { "registered_member_count", grid_members.size() },
                                           { "legal_candidates", legal.size() },
                                           { "on_ground_route", board.verified && frontline },
                                           { "route_coverage_verified", false },
                                           { "map_registration_verified", board.verified } };
                        planned["details"]["grid_registration_verified"] = board.grid_verified;
                        if (placement_role == PlacementRole::Healer && registered_range_scoring) {
                            planned["details"]["healing_coverage_score"] = std::accumulate(
                                preferences.begin(),
                                preferences.end(),
                                0,
                                [&](int total, const auto& preference) {
                                    return total + (preference.healing_coverage && preference.x == chosen->first &&
                                                            preference.y == chosen->second &&
                                                            preference.facing == facing
                                                        ? preference.bonus
                                                        : 0);
                                });
                        }
                        callback(AsstMsg::SubTaskExtraInfo, planned);
                    }
                }
                if (!role_target_selected) {
                    const auto preference = m_prefer_safe_target ? m_safe_target : m_drag_target;
                    std::sort(candidates.begin(), candidates.end(), [&](const Point& a, const Point& b) {
                        return std::abs(a.x - preference.x) + std::abs(a.y - preference.y) <
                               std::abs(b.x - preference.x) + std::abs(b.y - preference.y);
                    });
                    m_drag_target = candidates.front();
                }
                auto annotated = map_image.clone();
                if (board.verified) {
                    for (const auto& cell : board.legal_cells) {
                        const auto color = cell.on_ground_route ? cv::Scalar(0, 255, 255) : cv::Scalar(255, 120, 0);
                        cv::circle(annotated, cell.center, 13, color, 2);
                        cv::putText(
                            annotated,
                            std::to_string(cell.row) + "," + std::to_string(cell.col),
                            cell.center + cv::Point(-16, -15),
                            cv::FONT_HERSHEY_SIMPLEX,
                            .4,
                            color,
                            1);
                    }
                }
                for (size_t index = 0; index < candidates.size(); ++index) {
                    const cv::Point point(candidates[index].x, candidates[index].y);
                    cv::circle(annotated, point, 8, cv::Scalar(0, 0, 255), 2);
                    cv::putText(
                        annotated,
                        std::to_string(index),
                        point + cv::Point(9, -7),
                        cv::FONT_HERSHEY_SIMPLEX,
                        0.45,
                        cv::Scalar(255, 255, 255),
                        1);
                }
                cv::circle(annotated, cv::Point(m_drag_target.x, m_drag_target.y), 16, cv::Scalar(0, 255, 255), 3);
                save_evidence(annotated, utils::path("debug/SPA/held_placement_candidates"), false);
            }
            const bool hint = (m_replace_only || m_find_equipment_source)
                                  ? (paired_target_verified ||
                                     occupied_swap_hint(map_image, cv::Point(m_drag_target.x, m_drag_target.y)))
                                  : green_deployment_hint(map_image, cv::Point(m_drag_target.x, m_drag_target.y));
            auto preflight = basic_info_with_what("SPADeployMapReacquired");
            preflight["details"] = json::object { { "name", m_name },
                                                  { "staging_x", staging.x },
                                                  { "staging_y", staging.y },
                                                  { "green_hint", hint },
                                                  { "candidate_count", candidates.size() },
                                                  { "drag_target_x", m_drag_target.x },
                                                  { "drag_target_y", m_drag_target.y } };
            callback(AsstMsg::SubTaskExtraInfo, preflight);
            m_deployment_hint_verified = hint;
        }
        // Continue the same held contact from staging to the final cell.
        for (int step = 1; ok && step <= 16; ++step) {
            const Point point(
                staging.x + (m_drag_target.x - staging.x) * step / 16,
                staging.y + (m_drag_target.y - staging.y) * step / 16);
            ok = send(InputEvent::Type::TOUCH_MOVE, point) && send(InputEvent::Type::COMMIT) && sleep(35);
        }
    }
    else {
        for (int step = 1; ok && step <= 24; ++step) {
            const Point point(
                m_source.x + (m_drag_target.x - m_source.x) * step / 24,
                m_source.y + (m_drag_target.y - m_source.y) * step / 24);
            ok = send(InputEvent::Type::TOUCH_MOVE, point) && send(InputEvent::Type::COMMIT) && sleep(35);
            if (ok && m_move_only && step == 6) {
                const auto hint_image = ctrler()->get_image();
                const bool hint = green_deployment_hint(hint_image, cv::Point(m_drag_target.x, m_drag_target.y));
                save_evidence(hint_image, utils::path("debug/SPA/move_target_hint"));
                auto preflight = basic_info_with_what("SPAMoveTargetHint");
                preflight["details"] = json::object { { "name", m_name },
                                                      { "green_hint", hint },
                                                      { "drag_target_x", m_drag_target.x },
                                                      { "drag_target_y", m_drag_target.y } };
                callback(AsstMsg::SubTaskExtraInfo, preflight);
                // Inventory badges and other sprites can cover a legal target.
                // Release only at the requested target and reconcile the game's accepted state below.
            }
            if (ok && m_deploy_only && step == 6) {
                const auto hint_image = ctrler()->get_image();
                bool hint = green_deployment_hint(hint_image, cv::Point(m_drag_target.x, m_drag_target.y));
                // During a held drag the game replaces the normal board with the
                // green legal-cell overlay; blue markers from the preparation
                // frame are no longer available.  If the preselected point was
                // shifted by the camera, probe the bounded grid against the live
                // green overlay and retarget the same drag instead of releasing
                // and reopening the same operator panel.
                if (!hint) {
                    std::vector<Point> candidates;
                    // The lower edge is the bench/sell strip.  It is also tinted
                    // green while a card is held, so sampling it can produce a
                    // false legal-cell hit.  Keep the re-acquisition grid inside
                    // the map playfield; the placement diamond and population
                    // reconciliation remain authoritative after release.
                    for (int y = 200; y <= 440; y += 80) {
                        for (int x = 440; x <= 960; x += 80) {
                            candidates.emplace_back(x, y);
                        }
                    }
                    std::sort(candidates.begin(), candidates.end(), [&](const Point& lhs, const Point& rhs) {
                        const auto left = std::abs(lhs.x - m_drag_target.x) + std::abs(lhs.y - m_drag_target.y);
                        const auto right = std::abs(rhs.x - m_drag_target.x) + std::abs(rhs.y - m_drag_target.y);
                        return left < right;
                    });
                    for (const auto& candidate : candidates) {
                        if (green_deployment_hint(hint_image, cv::Point(candidate.x, candidate.y))) {
                            m_drag_target = candidate;
                            hint = true;
                            break;
                        }
                    }
                }
                save_evidence(hint_image, utils::path("debug/SPA/deploy_target_hint"));
                auto preflight = basic_info_with_what("SPADeployTargetHint");
                preflight["details"] = json::object { { "name", m_name },
                                                      { "green_hint", hint },
                                                      { "drag_target_x", m_drag_target.x },
                                                      { "drag_target_y", m_drag_target.y } };
                callback(AsstMsg::SubTaskExtraInfo, preflight);
                // Finish the path before release; never drop at an unintended intermediate tile.
                // The released game's placement stage is authoritative when a sprite occludes the hint.
                m_deployment_hint_verified = hint;
            }
            if (ok && step == 12) {
                save_evidence(ctrler()->get_image(), utils::path("debug/SPA/mid_drag"), false);
            }
        }
    }
    bool cancelled_outside_targets = false;
    if (!ok && (m_find_equipment_source || m_replace_only)) {
        // The staging bench may itself contain an eligible operator. Releasing
        // a consumable there could use it despite a failed map preflight.
        // Move away from the field, inventory and trash before cancellation.
        const bool moved_to_cancel_point = send(InputEvent::Type::TOUCH_MOVE, Point(20, 300));
        const bool cancel_position_committed = send(InputEvent::Type::COMMIT);
        cancelled_outside_targets = moved_to_cancel_point && cancel_position_committed;
    }
    if (ok && m_deploy_only) {
        // Capture the actual final held frame, not just the staging overlay.
        // Camera motion between these frames can invalidate a green target.
        ok = sleep(120);
        if (ok) {
            save_evidence(ctrler()->get_image(), utils::path("debug/SPA/held_drop_preview"), false);
        }
    }
    // Always release the contact, including cancellation and failed movement.
    const bool released = send(InputEvent::Type::TOUCH_UP);
    const bool committed = send(InputEvent::Type::COMMIT);
    if (m_equip_only) {
        // An acknowledged release away from all targets leaves the item in
        // inventory. Failure to acknowledge any part is still ambiguous.
        m_equipment_contact_attempted = !(cancelled_outside_targets && released && committed);
    }
    LogInfo << "SPA held drag end" << ok << released << committed << cancelled_outside_targets;
    return ok && released && committed;
}

bool asst::spa::SwapTaskPlugin::select_direction(const Point& center)
{
    const auto send = [&](InputEvent::Type type, const Point& point = Point()) {
        InputEvent event;
        event.type = type;
        event.point = point;
        return ctrler()->inject_input_event(event);
    };
    m_direction_preview_verified = false;
    bool ok = send(InputEvent::Type::TOUCH_DOWN, center) && send(InputEvent::Type::COMMIT) && sleep(100);
    cv::Mat preview_image;
    if (ok) {
        const Point destination(
            std::clamp(center.x + m_direction.x, 0, 1279),
            std::clamp(center.y + m_direction.y, 0, 719));
        ok = send(InputEvent::Type::TOUCH_MOVE, destination) && send(InputEvent::Type::COMMIT) && sleep(400);
        if (ok) {
            preview_image = ctrler()->get_image();
            BattlefieldClassifier classifier(preview_image);
            classifier.set_object_of_interest({ false, true });
            classifier.set_base_point(center);
            const auto result = classifier.analyze();
            const auto expected = m_direction.x > 0   ? battle::DeployDirection::Right
                                  : m_direction.x < 0 ? battle::DeployDirection::Left
                                  : m_direction.y > 0 ? battle::DeployDirection::Down
                                                      : battle::DeployDirection::Up;
            float score = 0.0F;
            std::string model_direction;
            if (result) {
                score = result->deploy_direction.score;
                switch (result->deploy_direction.direction) {
                case battle::DeployDirection::Right:
                    model_direction = "E";
                    break;
                case battle::DeployDirection::Left:
                    model_direction = "W";
                    break;
                case battle::DeployDirection::Down:
                    model_direction = "S";
                    break;
                case battle::DeployDirection::Up:
                    model_direction = "N";
                    break;
                default:
                    break;
                }
                // The bundled classifier is calibrated below 0.85 on the
                // MuMu B-server overlay; require a clear class preference but
                // keep the stronger post-release marker check below.
                m_direction_preview_verified = score >= 0.65F && result->deploy_direction.direction == expected;
            }
            save_evidence(preview_image, utils::path("debug/SPA/direction_preview"));
            auto preview_info = basic_info_with_what("SPADirectionPreviewCaptured");
            preview_info["details"] = json::object { { "name", m_name },
                                                     { "round", m_round },
                                                     { "captured", true },
                                                     { "direction_preview_verified", m_direction_preview_verified },
                                                     { "model_direction", model_direction },
                                                     { "model_score", score } };
            callback(AsstMsg::SubTaskExtraInfo, preview_info);
        }
    }
    // Releasing the drag only opens the in-game direction diamond.  It does
    // not commit the facing; the corresponding arrow must be tapped next.
    const bool released = send(InputEvent::Type::TOUCH_UP);
    const bool committed = send(InputEvent::Type::COMMIT);
    if (!ok || !released || !committed || !sleep(250)) {
        return false;
    }
    const auto direction_image = ctrler()->get_image();
    const auto diamond = direction_selection_center(direction_image);
    if (!diamond) {
        LogError << "SPA direction diamond was not detected after drag release";
        save_evidence(direction_image, utils::path("debug/SPA/direction_diamond_missing"), false);
        return false;
    }
    const bool horizontal = std::abs(m_direction.x) > std::abs(m_direction.y);
    // The visible chevrons sit slightly above/left of the geometric diamond
    // intersection on this B-server layout; use the measured hitbox centers,
    // while still deriving the base from the detected diamond.
    const Point arrow(
        diamond->x + (m_direction.x > 0   ? 85
                      : m_direction.x < 0 ? -85
                                          : -12),
        horizontal ? diamond->y - 24 : diamond->y + (m_direction.y > 0 ? 122 : -122));
    save_evidence(direction_image, utils::path("debug/SPA/direction_diamond"), false);
    if (!ctrler()->click(arrow) || !sleep(900)) {
        LogError << "SPA direction arrow click failed" << arrow.x << arrow.y;
        return false;
    }
    save_evidence(ctrler()->get_image(), utils::path("debug/SPA/direction_committed"), false);
    return true;
}

bool asst::spa::SwapTaskPlugin::cancel_direction_modal(const cv::Mat& image)
{
    const auto center = direction_selection_center(image);
    const auto cancel = center ? direction_cancel_target(image, *center) : std::nullopt;
    if (!cancel || !ctrler()->click(Point(cancel->x, cancel->y)) || !sleep(350)) {
        return false;
    }

    const auto settled = ctrler()->get_image();
    const bool cleared = !settled.empty() && !direction_modal_visible(settled);
    auto evidence = basic_info_with_what("SPADirectionModalCancellationObserved");
    evidence["details"] =
        json::object { { "round", m_round }, { "name", m_name },           { "x", cancel->x },
                       { "y", cancel->y },   { "modal_cleared", cleared }, { "field_outcome_verified", false } };
    callback(AsstMsg::SubTaskExtraInfo, evidence);
    return cleared;
}

// The B-server direction diamond is confirmed by a short held drag from its
// centre toward the desired facing.  A simple tap on the apparent arrow does
// nothing (the arrows are visual guidance, not independent buttons).
bool asst::spa::SwapTaskPlugin::drag_direction_only(const Point& center)
{
    const auto send = [&](InputEvent::Type type, const Point& point = Point()) {
        InputEvent event;
        event.type = type;
        event.point = point;
        return ctrler()->inject_input_event(event);
    };
    const Point destination(
        std::clamp(center.x + m_direction.x, 0, 1279),
        std::clamp(center.y + m_direction.y, 0, 719));
    bool ok = send(InputEvent::Type::TOUCH_DOWN, center) && send(InputEvent::Type::COMMIT) && sleep(100);
    if (ok) {
        // A single jump move is ignored intermittently by the B-server.  Use
        // the same continuous path as a real swipe so the direction control
        // receives enough motion events to commit the facing.
        for (int step = 1; ok && step <= 8; ++step) {
            const Point point(
                center.x + (destination.x - center.x) * step / 8,
                center.y + (destination.y - center.y) * step / 8);
            ok = send(InputEvent::Type::TOUCH_MOVE, point) && send(InputEvent::Type::COMMIT) && sleep(60);
        }
    }
    const bool released = send(InputEvent::Type::TOUCH_UP);
    const bool committed = send(InputEvent::Type::COMMIT);
    if (!ok || !released || !committed || !sleep(m_fast_mode ? 500 : 900)) {
        if (!need_exit()) {
            cancel_direction_modal(ctrler()->get_image());
        }
        return false;
    }

    // On some B-server maps the drag opens the direction diamond but does not
    // commit the facing.  Leaving that diamond open makes the caller probe
    // operator identities against a modal overlay and can loop for minutes.
    // If the diamond is still visible, click the corresponding arrow using
    // the same geometry as the verified select_direction path, then allow the
    // placement animation to settle.  When no diamond remains, the original
    // drag was already accepted and no extra map click is issued.
    const auto after_drag = ctrler()->get_image();
    const bool modal_after_drag = direction_modal_visible(after_drag);
    if (modal_after_drag) {
        save_evidence(after_drag, utils::path("debug/SPA/direction_after_uncommitted_drag"), false);
    }
    if (m_best_effort_deployment && !modal_after_drag) {
        // The game may already have committed the facing and returned to the
        // preparation HUD while the translucent diamond detector still sees
        // a stale contour.  The HUD is authoritative for phase one: when the
        // round, ready button and remaining-deployment counter are all back,
        // accept the direct held swipe and skip the unreliable arrow fallback.
        // Read only the three HUD labels on this same post-drag frame. The
        // former broad ROI ended at y430 and excluded the population label.
        // Do not compare population with m_remaining: deploying consumes it.
        const auto has_label = [&](const Rect& roi, const std::string& label) {
            OCRer label_ocr(after_drag, roi);
            if (const auto labels = label_ocr.analyze()) {
                return std::ranges::any_of(*labels, [&](const auto& text) {
                    return text.text.find(label) != std::string::npos;
                });
            }
            return false;
        };
        if (has_label(Rect(430, 0, 125, 60), "回合") && has_label(Rect(1100, 70, 180, 85), "准备就绪") &&
            has_label(Rect(1090, 350, 190, 165), "剩余可放置角色")) {
            m_direction_committed = sleep(250);
            return m_direction_committed;
        }
    }
    // A selected operator's white attack-range outline is not a direction
    // modal. Never issue arrow clicks without the cancel-control evidence.
    if (!modal_after_drag) {
        if (direction_selection_center(after_drag)) {
            save_evidence(after_drag, utils::path("debug/SPA/nonmodal_direction_outline"), false);
            auto ignored = basic_info_with_what("SPANonmodalDirectionOutlineIgnored");
            ignored["details"] = json::object { { "round", m_round },
                                                { "name", m_name },
                                                { "arrow_clicks_issued", 0 },
                                                { "reason", "White outline has no direction cancel control" } };
            callback(AsstMsg::SubTaskExtraInfo, ignored);
        }
        if (m_joint_deployment && m_deploy_only && !m_replace_only && !m_preserve_population) {
            // The round may already have applied this deployment. Read the
            // resulting slot count in the caller instead of requiring the
            // pre-drop count, and keep the facing unverified.
            return true;
        }
        if (!close_selection() || !preparation_matches()) {
            return false;
        }
        m_direction_committed = sleep(m_fast_mode ? 300 : 800);
        return m_direction_committed;
    }
    auto diamond = direction_selection_center(after_drag);
    // OCR catches the modal when the white diamond is washed out by a map
    // effect or animation.  Use the intended target as the arrow base in
    // that case; the follow-up modal check still decides whether it worked.
    if (!diamond) {
        diamond = cv::Point(center.x, center.y);
    }
    if (diamond) {
        const bool horizontal = std::abs(m_direction.x) > std::abs(m_direction.y);
        const Point primary_arrow(
            diamond->x + (m_direction.x > 0   ? 85
                          : m_direction.x < 0 ? -85
                                              : -12),
            horizontal ? diamond->y - 24 : diamond->y + (m_direction.y > 0 ? 122 : -122));
        // The arrow is rendered on the geometric side on some maps and
        // slightly above/left on others.  Try the calibrated point first,
        // then the side-centre point while the diamond is still modal.  Each
        // attempt is verified by requiring the diamond to disappear.
        std::vector<Point> arrows { primary_arrow };
        if (horizontal) {
            arrows.emplace_back(diamond->x + (m_direction.x > 0 ? 85 : -85), diamond->y);
            arrows.emplace_back(diamond->x + (m_direction.x > 0 ? 105 : -105), diamond->y);
        }
        else {
            arrows.emplace_back(diamond->x, diamond->y + (m_direction.y > 0 ? 105 : -105));
        }
        bool direction_committed = false;
        for (const auto& arrow : arrows) {
            if (!ctrler()->click(arrow) || !sleep(m_fast_mode ? 400 : 650)) {
                continue;
            }
            const auto settled = ctrler()->get_image();
            if (!direction_modal_visible(settled)) {
                direction_committed = true;
                break;
            }
        }
        if (!direction_committed) {
            LogError << "SPA direction arrow fallback failed" << primary_arrow.x << primary_arrow.y;
            const auto unsettled = ctrler()->get_image();
            save_evidence(unsettled, utils::path("debug/SPA/direction_after_failed_arrows"), false);
            cancel_direction_modal(unsettled);
            return false;
        }
    }
    // Give the placement animation/camera one settled frame before identity
    // reconciliation.  Without this guard the next OCR could see the old
    // direction overlay and spend the bounded identity probes on stale tiles.
    m_direction_committed = sleep(m_fast_mode ? 300 : 800);
    return m_direction_committed;
}

bool asst::spa::SwapTaskPlugin::reorient_in_place()
{
    m_spatial_direction_attempted = false;
    cv::Mat reference;
    if (m_placement_members.empty() || m_placement_members.size() > 8 || !capture_collapsed_preparation(reference)) {
        return false;
    }
    const auto source_member =
        std::find_if(m_placement_members.begin(), m_placement_members.end(), [&](const auto& member) {
            return member.name == m_name && member.x == m_source.x && member.y == m_source.y;
        });
    if (source_member == m_placement_members.end()) {
        return false;
    }
    const auto source_index = static_cast<std::size_t>(std::distance(m_placement_members.begin(), source_member));
    std::optional<std::size_t> displaced_index;
    if (!m_relocation_displaced.empty()) {
        const auto displaced =
            std::find_if(m_placement_members.begin(), m_placement_members.end(), [&](const auto& member) {
                return member.name == m_relocation_displaced;
            });
        if (displaced == m_placement_members.end() || displaced == source_member ||
            std::find_if(std::next(displaced), m_placement_members.end(), [&](const auto& member) {
                return member.name == m_relocation_displaced;
            }) != m_placement_members.end()) {
            return false;
        }
        displaced_index = static_cast<std::size_t>(std::distance(m_placement_members.begin(), displaced));
    }
    std::vector<cv::Point> previous;
    for (const auto& member : m_placement_members) {
        previous.emplace_back(member.x, member.y);
    }
    cv::Mat clean, selected;
    std::optional<std::vector<cv::Point>> clean_points;
    if (m_fast_mode && !m_grid_probe_only && previous.size() == m_verified_preview_feet.size() &&
        previous.size() == static_cast<size_t>(8 - m_remaining) && !m_registered_grid_reference.empty() &&
        m_registered_grid_map_id == m_expected_map_id) {
        const auto fit = estimate_camera_projection(m_registered_grid_reference, reference, true);
        const auto current_points = registered_field_points(fit, previous);
        const auto projection = registered_grid_projection(fit, m_registered_grid_projection);
        if (current_points && projection) {
            auto current_members = m_placement_members;
            for (size_t index = 0; index < current_members.size(); ++index) {
                current_members[index].x = current_points->at(index).x;
                current_members[index].y = current_points->at(index).y;
            }
            const auto cells = registered_grid_members(*projection, current_members);
            const auto at_target = [&](const auto& member) {
                return member.col == m_reorient_col && member.row == m_reorient_row;
            };
            const bool target_matches =
                cells && (displaced_index    ? at_target(cells->at(*displaced_index))
                          : m_relocate_field ? std::none_of(cells->begin(), cells->end(), at_target)
                                             : at_target(cells->at(source_index)));
            if (target_matches && cells->at(source_index).col == m_origin_col &&
                cells->at(source_index).row == m_origin_row &&
                std::equal(
                    previous.begin(),
                    previous.end(),
                    current_points->begin(),
                    [](const auto& before, const auto& now) { return cv::norm(before - now) <= 12; })) {
                // The held title must still match before any release on the
                // target cell. The final identity is checked after release.
                clean = reference;
                clean_points = current_points;
            }
        }
    }
    const bool reused_source_pose = clean_points.has_value();
    if (!reused_source_pose &&
        (!selected_field_identity(m_source, m_name, selected) || !capture_collapsed_preparation(clean, selected))) {
        return false;
    }
    if (displaced_index) {
        // Keep the targeted receiver proof for an occupied drop. Its panel
        // can change the camera, so re-register feet after closing it.
        const auto receiver = clean_points ? clean_points->at(*displaced_index) : previous.at(*displaced_index);
        if (!selected_field_identity(Point(receiver.x, receiver.y), m_relocation_displaced, selected) ||
            !capture_collapsed_preparation(clean, selected)) {
            return false;
        }
        clean_points.reset();
    }
    if (!clean_points) {
        clean_points = registered_field_points(reference, clean, previous);
    }
    if (!clean_points) {
        return false;
    }
    m_source = Point(clean_points->at(source_index).x, clean_points->at(source_index).y);
    m_reorientation_reconcile = [this, clean, clean_points, source_index, displaced_index, reused_source_pose]() {
        cv::Mat field;
        if (!capture_collapsed_preparation(field)) {
            return false;
        }
        auto expected = *clean_points;
        if (m_reorientation_destination) {
            expected[source_index] = *m_reorientation_destination;
            if (displaced_index) {
                expected[*displaced_index] = clean_points->at(source_index);
            }
        }
        std::optional<std::vector<cv::Point>> points;
        bool identified = false;
        for (int candidate = 0; candidate < (m_relocate_field && m_reorientation_destination ? 2 : 1); ++candidate) {
            if (candidate == 1) {
                // A cancelled or uncommitted direction can leave the actor
                // at its original cell. Observe both possible outcomes; do
                // not repeat the drag to make an assumed outcome come true.
                expected = *clean_points;
                if (field.empty() && !capture_collapsed_preparation(field)) {
                    return false;
                }
            }
            points = registered_field_points(clean, field, expected);
            if (!points) {
                continue;
            }
            const auto proves = [&](std::size_t index, const std::string& name) {
                const auto point = points->at(index);
                cv::Mat panel;
                const bool verified = selected_field_identity(Point(point.x, point.y), name, panel);
                return capture_collapsed_preparation(field, panel) && verified;
            };
            identified =
                proves(source_index, m_name) && (!displaced_index || proves(*displaced_index, m_relocation_displaced));
            if (identified) {
                m_relocation_applied = m_relocate_field && m_reorientation_destination.has_value() && candidate == 0;
                break;
            }
        }
        if (!identified) {
            return false;
        }
        if (m_relocate_field && !m_relocation_applied) {
            m_direction_committed = false;
        }
        points = registered_field_points(clean, field, expected);
        if (!points) {
            return false;
        }
        m_reoriented_field_members = m_placement_members;
        for (std::size_t index = 0; index < points->size(); ++index) {
            auto& member = m_reoriented_field_members[index];
            member.x = points->at(index).x;
            member.y = points->at(index).y;
        }
        if (!m_grid_probe_only) {
            if (const auto facing = committed_facing()) {
                m_reoriented_field_members[source_index].facing = *facing;
            }
            else if (m_spatial_direction_attempted || m_relocation_applied) {
                m_reoriented_field_members[source_index].facing = '\0';
            }
        }
        if (m_relocation_applied && displaced_index) {
            // A swap can change the other operator's facing. Clear the
            // prior value before looking for its new chevron.
            m_reoriented_field_members[*displaced_index].facing = '\0';
        }
        m_target = Point(points->at(source_index).x, points->at(source_index).y);
        if (!m_held_board.empty() && !m_growth_grid_to_screen.empty()) {
            const auto projection = registered_grid_projection(m_held_board, field, m_growth_grid_to_screen);
            auto grid = projection ? registered_grid_members(*projection, m_reoriented_field_members) : std::nullopt;
            if (grid) {
                // This clean frame already verified the relocated identities
                // and grid. Reuse it before asking for a separate scan.
                observe_clean_field_facings(field, m_reoriented_field_members, *grid, "reconciled_clean_chevron");
                m_observed_grid_projection = projection->clone();
                m_observed_grid_reference = field.clone();
                m_observed_grid_members = std::move(*grid);
                m_observed_field_members = m_reoriented_field_members;
            }
        }
        // A verified field move can be reconciled before the logical grid
        // is readable. The controller rebuilds that grid separately.
        m_field_pose_verified = true;
        auto verified = basic_info_with_what(
            m_grid_probe_only       ? "SPAFieldGridProbeVerified"
            : m_relocation_applied  ? "SPAFieldRelocationVerified"
            : m_direction_committed ? "SPAInPlaceOrientationVerified"
                                    : "SPAFieldPoseRestored");
        verified["details"] = json::object { { "name", m_name },
                                             { "round", m_round },
                                             { "population_preserved", true },
                                             { "funds_preserved", true },
                                             { "source_identity_and_field_control_verified", true },
                                             { "source_panel_probed_before_hold", !reused_source_pose },
                                             { "receiver_panel_probed_before_hold", displaced_index.has_value() },
                                             { "field_coordinates_registered", points->size() },
                                             { "logical_grid_verified",
                                               m_observed_grid_members.size() == m_reoriented_field_members.size() },
                                             { "hud_cache_hits", m_preparation_hud_cache_hits },
                                             { "hud_fields_reused", m_preparation_hud_fields_reused },
                                             { "hud_ocr_checks", m_preparation_hud_ocr_checks },
                                             { "relocation_applied", m_relocation_applied },
                                             { "displaced", m_relocation_displaced },
                                             { "direction_input_committed", m_direction_committed },
                                             { "facing_pixel_verified", false },
                                             { "target_x", m_target.x },
                                             { "target_y", m_target.y } };
        callback(AsstMsg::SubTaskExtraInfo, verified);
        return true;
    };
    const auto send = [&](InputEvent::Type type, const Point& point = Point()) {
        InputEvent event;
        event.type = type;
        event.point = point;
        return ctrler()->inject_input_event(event);
    };
    if (m_spatial_hold_guard && !m_spatial_hold_guard()) {
        // The source inspection has already restored the clean preparation
        // view. There is no held input or placement dialog to reconcile.
        m_spatial_hold_deferred = true;
        return false;
    }
    m_reorientation_started = true;
    bool held = send(InputEvent::Type::TOUCH_DOWN, m_source) && send(InputEvent::Type::COMMIT);
    const auto release = [&]() {
        const bool up = send(InputEvent::Type::TOUCH_UP);
        const bool committed = send(InputEvent::Type::COMMIT);
        held = false;
        return up && committed;
    };
    const auto cancel_held = [&]() {
        if (held) {
            send(InputEvent::Type::TOUCH_MOVE, Point(20, 300));
            send(InputEvent::Type::COMMIT);
        }
        release();
        sleep(300);
        reconcile_in_place_orientation();
        return false;
    };
    if (!held || !sleep(200)) {
        return cancel_held();
    }
    const Point staging(120, 580);
    for (int step = 1; step <= 8; ++step) {
        const Point point(
            m_source.x + (staging.x - m_source.x) * step / 8,
            m_source.y + (staging.y - m_source.y) * step / 8);
        if (!send(InputEvent::Type::TOUCH_MOVE, point) || !send(InputEvent::Type::COMMIT) || !sleep(35)) {
            return cancel_held();
        }
    }
    auto map = ctrler()->get_image();
    save_evidence(clean, utils::path("debug/SPA/reorientation_clean_map"), false);
    for (int frame = 0;; ++frame) {
        OCRer identity(map, Rect(0, 160, 340, 65));
        std::vector<TextObservation> titles;
        if (const auto names = identity.analyze()) {
            for (const auto& name : *names) {
                titles.push_back({ name.text, name.score, name.rect.x, name.rect.y });
            }
        }
        save_evidence(map, utils::path("debug/SPA/reorientation_held_map"), false);
        if (operator_panel_identity(titles, { m_name }) == m_name ||
            reread_single_operator_title(map, titles, m_name)) {
            break;
        }
        // The translucent held title can settle after the registered field
        // card was picked up. Recheck one frame while keeping the same hold.
        if (frame != 0 || !bench_replacement_title_needs_settle(titles, m_name) || !sleep(120)) {
            return cancel_held();
        }
        auto retry = basic_info_with_what("SPASpatialHeldTitleResampled");
        retry["details"] = json::object { { "round", m_round }, { "name", m_name }, { "new_input_issued", false } };
        callback(AsstMsg::SubTaskExtraInfo, retry);
        map = ctrler()->get_image();
    }
    auto held_points = registered_field_points(clean, map, *clean_points, FieldView::Held);
    if (!held_points && !m_registered_grid_reference.empty() && !m_registered_grid_projection.empty()) {
        const auto clean_grid =
            registered_grid_projection(m_registered_grid_reference, clean, m_registered_grid_projection);
        if (clean_grid) {
            BoardCell source_cell { m_origin_col, m_origin_row };
            if (m_grid_probe_only) {
                auto identified = m_placement_members;
                for (size_t index = 0; index < identified.size(); ++index) {
                    identified[index].x = clean_points->at(index).x;
                    identified[index].y = clean_points->at(index).y;
                }
                const auto cells = registered_grid_members(*clean_grid, identified);
                if (cells) {
                    source_cell = { cells->at(source_index).col, cells->at(source_index).row };
                }
            }
            held_points =
                registered_held_field_points(clean, map, *clean_points, *clean_grid, source_index, source_cell);
            if (held_points) {
                auto source = basic_info_with_what("SPAHeldSourceGridRecovered");
                source["details"] = json::object { { "round", m_round },
                                                   { "name", m_name },
                                                   { "source_identity_verified_before_hold", !reused_source_pose },
                                                   { "held_title_verified", true },
                                                   { "other_members_feature_registered", clean_points->size() - 1 },
                                                   { "col", source_cell.first },
                                                   { "row", source_cell.second } };
                callback(AsstMsg::SubTaskExtraInfo, source);
            }
        }
    }
    if (!held_points) {
        m_spatial_held_registration_failed = true;
        const auto fit = estimate_camera_projection(clean, map, true);
        auto sources = json::array();
        for (const auto& point : *clean_points) {
            const auto mapped = project_supported_point(fit, cv::Point2f(point));
            sources.emplace_back(
                json::object { { "x", point.x },
                               { "y", point.y },
                               { "supported", mapped.has_value() },
                               { "mapped_x", mapped ? mapped->x : -1 },
                               { "mapped_y", mapped ? mapped->y : -1 } });
        }
        auto receipt = basic_info_with_what("SPASpatialHeldFrameObserved");
        receipt["details"] = json::object { { "round", m_round },
                                            { "grid_probe_only", m_grid_probe_only },
                                            { "points_registered", false },
                                            { "member_count", clean_points->size() },
                                            { "camera_estimated", fit.estimated },
                                            { "camera_matches", fit.matches },
                                            { "camera_inliers", fit.inliers },
                                            { "camera_error", fit.median_error },
                                            { "camera_sources", sources } };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
        return cancel_held();
    }
    auto held_members = m_placement_members;
    for (std::size_t index = 0; index < held_points->size(); ++index) {
        held_members[index].x = held_points->at(index).x;
        held_members[index].y = held_points->at(index).y;
    }
    const auto role = BattleData.get_first_role(m_name);
    auto board = register_preparation_board(
        map,
        BattleData.get_location_type(role, m_name) == battle::LocationType::Melee,
        *held_points);
    board = preserve_registered_board(
        map,
        std::move(board),
        BattleData.get_location_type(role, m_name) == battle::LocationType::Melee,
        held_members);
    auto member_projection = json::array();
    if (!board.grid_to_screen.empty()) {
        for (const auto& member : held_members) {
            std::vector<cv::Point2f> cell;
            cv::perspectiveTransform(
                std::vector<cv::Point2f> { { static_cast<float>(member.x), static_cast<float>(member.y) } },
                cell,
                board.grid_to_screen.inv());
            member_projection.emplace_back(
                json::object { { "name", member.name },
                               { "x", member.x },
                               { "y", member.y },
                               { "col", cell.front().x },
                               { "row", cell.front().y } });
        }
    }
    const auto grid = board.grid_verified ? registered_grid_members(board.grid_to_screen, held_members, FieldView::Held)
                                          : std::nullopt;
    auto receipt = basic_info_with_what("SPASpatialHeldFrameObserved");
    receipt["details"] = json::object { { "round", m_round },
                                        { "grid_probe_only", m_grid_probe_only },
                                        { "points_registered", true },
                                        { "member_count", held_members.size() },
                                        { "map_id", board.map_id },
                                        { "map_verified", board.verified },
                                        { "map_identity_from_registered_reference", board.identity_from_reference },
                                        { "map_score", board.score },
                                        { "grid_verified", board.grid_verified },
                                        { "members_mapped", grid.has_value() } };
    receipt["details"]["member_projection"] = std::move(member_projection);
    callback(AsstMsg::SubTaskExtraInfo, receipt);
    if (grid) {
        // Retain the certified grid even when a later target gate cancels
        // the gesture. Reconciliation can then restore the unchanged field
        // without discarding its population geometry and stopping planning.
        m_held_board = map.clone();
        m_growth_grid_to_screen = board.grid_to_screen.clone();
        m_verified_map_id = board.verified ? board.map_id : std::string();
        observe_boss_attackable_region(map, board.grid_to_screen);
        json::array recovered;
        for (std::size_t index = 0; index < held_members.size(); ++index) {
            if (m_placement_members[index].facing != '\0') {
                continue;
            }
            const auto& member = held_members[index];
            if (const auto facing = deployed_facing(map, cv::Point(member.x, member.y))) {
                m_placement_members[index].facing = *facing;
                recovered.emplace_back(
                    json::object { { "name", member.name },
                                   { "col", grid->at(index).col },
                                   { "row", grid->at(index).row },
                                   { "facing", std::string(1, *facing) } });
            }
        }
        if (!recovered.empty()) {
            auto facing = basic_info_with_what("SPAFieldFacingObserved");
            facing["details"] = json::object { { "round", m_round },
                                               { "members", std::move(recovered) },
                                               { "source", "registered_held_chevron" },
                                               { "extra_direction_input_issued", false } };
            callback(AsstMsg::SubTaskExtraInfo, facing);
        }
    }
    if (m_grid_probe_only) {
        if (grid) {
            m_held_board = map.clone();
            m_growth_grid_to_screen = board.grid_to_screen.clone();
            m_verified_map_id = board.verified ? board.map_id : std::string();
        }
        return cancel_held();
    }
    if (!grid || grid->at(source_index).col != m_origin_col || grid->at(source_index).row != m_origin_row ||
        (m_relocate_field && (!board.verified || board.map_id != m_expected_map_id))) {
        return cancel_held();
    }
    const auto occupant = std::find_if(grid->begin(), grid->end(), [&](const auto& member) {
        return member.col == m_reorient_col && member.row == m_reorient_row;
    });
    if ((!m_relocate_field && (m_reorient_col != m_origin_col || m_reorient_row != m_origin_row)) ||
        (m_relocate_field &&
         ((m_reorient_col == m_origin_col && m_reorient_row == m_origin_row) ||
          (m_relocation_displaced.empty() ? occupant != grid->end()
                                          : occupant == grid->end() || occupant->name != m_relocation_displaced)))) {
        return cancel_held();
    }
    if (m_relocate_field) {
        const auto terrain = terrain_layout_model(board.map_id, board.boss_phase_visible);
        const BoardCell destination { m_reorient_col, m_reorient_row };
        const BoardCell origin { m_origin_col, m_origin_row };
        const auto accepts = [&](const PlacementMember& member, const BoardCell& target) {
            const auto role = BattleData.get_first_role(member.name);
            const bool melee = BattleData.get_location_type(role, member.name) == battle::LocationType::Melee;
            return terrain_allows_deployment(board.map_id, target, melee, board.boss_phase_visible);
        };
        if (!terrain || !accepts(m_placement_members[source_index], destination) ||
            (displaced_index && !accepts(m_placement_members[*displaced_index], origin))) {
            return cancel_held();
        }
    }
    std::vector<cv::Point2f> target;
    cv::perspectiveTransform(
        std::vector<cv::Point2f> { { static_cast<float>(m_reorient_col), static_cast<float>(m_reorient_row) } },
        target,
        board.grid_to_screen);
    if (target.size() != 1 || !std::isfinite(target.front().x) || !std::isfinite(target.front().y)) {
        return cancel_held();
    }
    cv::Point target_cell(cvRound(target.front().x), cvRound(target.front().y));
    if (m_relocate_field && occupant == grid->end() && !green_deployment_hint(map, target_cell)) {
        // A registered foot anchor can touch a fence/raised-tile edge. Keep
        // the intended logical cell and the original green threshold; only
        // choose an interior input after terrain and vacancy were verified.
        const auto interior = registered_green_cell_target(
            map,
            board.grid_to_screen,
            { m_reorient_col, m_reorient_row },
            board.verified && board.grid_verified,
            terrain_allows_deployment(
                board.map_id,
                { m_reorient_col, m_reorient_row },
                BattleData.get_location_type(role, m_name) == battle::LocationType::Melee,
                board.boss_phase_visible),
            true);
        if (interior) {
            auto adjusted = basic_info_with_what("SPASpatialGreenInteriorTarget");
            adjusted["details"] = json::object { { "round", m_round },
                                                 { "name", m_name },
                                                 { "col", m_reorient_col },
                                                 { "row", m_reorient_row },
                                                 { "projected_x", target_cell.x },
                                                 { "projected_y", target_cell.y },
                                                 { "input_x", interior->x },
                                                 { "input_y", interior->y },
                                                 { "same_cell_patch_verified", true },
                                                 { "terrain_and_vacancy_checked", true },
                                                 { "deployment_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, adjusted);
            target_cell = *interior;
        }
    }
    // An occupied destination includes the source itself for an in-place
    // turn. Sprites can hide its green preview. The occupant identity and
    // logical cell were checked above; the exact receiver is still checked
    // before direction commit. A vacant cell always needs a green preview.
    if (occupant == grid->end() && !green_deployment_hint(map, target_cell)) {
        bool mire_preview_verified = false;
        const auto& terrains = board.boss_phase_visible ? BossBoardTerrains : BoardTerrains;
        const auto terrain =
            std::find_if(terrains.begin(), terrains.end(), [&](const auto& item) { return item.id == board.map_id; });
        if (board.verified && terrain != terrains.end() && terrain->rows[m_reorient_row][m_reorient_col] == 'M') {
            const auto clean_target = registered_deployment_pose(map, clean, target_cell);
            if (clean_target) {
                const auto paired = equipment_target_preview(clean, map, *clean_target);
                mire_preview_verified = paired.verified;
                if (mire_preview_verified) {
                    auto evidence = basic_info_with_what("SPAMireDeploymentPreviewVerified");
                    evidence["details"] = json::object { { "round", m_round },
                                                         { "name", m_name },
                                                         { "col", m_reorient_col },
                                                         { "row", m_reorient_row },
                                                         { "added_green", paired.added_green },
                                                         { "left_green", paired.left_green },
                                                         { "right_green", paired.right_green },
                                                         { "terrain_and_vacancy_checked", true },
                                                         { "debuff_removed", false } };
                    callback(AsstMsg::SubTaskExtraInfo, evidence);
                }
            }
        }
        if (board.verified &&
            (mire_preview_verified || registered_added_green_preview(clean, map, target_cell, *held_points))) {
            auto preview = basic_info_with_what("SPASpatialShadowPreviewVerified");
            preview["details"] = json::object { { "round", m_round },
                                                { "name", m_name },
                                                { "col", m_reorient_col },
                                                { "row", m_reorient_row },
                                                { "terrain_and_vacancy_checked", true },
                                                { "new_green_overlay_registered", true } };
            callback(AsstMsg::SubTaskExtraInfo, preview);
        }
        else {
            auto rejected = basic_info_with_what("SPASpatialTargetRejected");
            rejected["details"] = json::object { { "round", m_round },
                                                 { "name", m_name },
                                                 { "reason", "vacant_target_has_no_green_preview" },
                                                 { "col", m_reorient_col },
                                                 { "row", m_reorient_row },
                                                 { "x", target_cell.x },
                                                 { "y", target_cell.y } };
            callback(AsstMsg::SubTaskExtraInfo, rejected);
            m_spatial_preview_failed = true;
            return cancel_held();
        }
    }
    if (m_relocate_field) {
        auto destination = registered_deployment_pose(map, clean, target_cell);
        auto journal_target = registered_deployment_pose(map, reference, target_cell);
        if (occupant == grid->end() && (!destination || !journal_target)) {
            const BoardCell cell { m_reorient_col, m_reorient_row };
            destination = registered_green_grid_cell_pose(map, clean, board.grid_to_screen, cell);
            journal_target = registered_green_grid_cell_pose(map, reference, board.grid_to_screen, cell);
        }
        if (!destination || !journal_target) {
            auto rejected = basic_info_with_what("SPASpatialTargetRejected");
            rejected["details"] = json::object { { "round", m_round },
                                                 { "name", m_name },
                                                 { "reason", "target_pose_not_registered" },
                                                 { "col", m_reorient_col },
                                                 { "row", m_reorient_row } };
            callback(AsstMsg::SubTaskExtraInfo, rejected);
            return cancel_held();
        }
        m_reorientation_destination = *destination;
        if (m_spatial_target_callback) {
            m_spatial_target_callback(Point(journal_target->x, journal_target->y));
        }
    }
    m_held_board = map.clone();
    m_growth_grid_to_screen = board.grid_to_screen.clone();
    m_verified_map_id = board.verified ? board.map_id : std::string();
    for (int step = 1; step <= 16; ++step) {
        const Point point(
            staging.x + (target_cell.x - staging.x) * step / 16,
            staging.y + (target_cell.y - staging.y) * step / 16);
        if (!send(InputEvent::Type::TOUCH_MOVE, point) || !send(InputEvent::Type::COMMIT) || !sleep(35)) {
            return cancel_held();
        }
    }
    if (!release() || !sleep(350)) {
        return false;
    }
    auto placement = ctrler()->get_image();
    bool modal_visible = direction_modal_visible(placement);
    if (!modal_visible && sleep(350)) {
        placement = ctrler()->get_image();
        modal_visible = direction_modal_visible(placement);
    }
    save_evidence(placement, utils::path("debug/SPA/spatial_direction_preview"), false);
    std::optional<cv::Mat> placement_grid;
    bool placement_registration_attempted = false;
    const auto project_placement_grid = [&]() -> const std::optional<cv::Mat>& {
        if (!placement_registration_attempted) {
            placement_registration_attempted = true;
            placement_grid = registered_grid_projection(map, placement, board.grid_to_screen);
        }
        return placement_grid;
    };
    const auto center = modal_visible ? direction_selection_center(placement) : std::nullopt;
    auto foot =
        center ? registered_deployment_pose(placement, map, cv::Point(center->x, center->y + 20)) : std::nullopt;
    bool green_grid_receiver = false;
    bool occupied_grid_receiver = false;
    if (!foot && center && m_relocate_field && occupant == grid->end()) {
        // The UI diamond's center is an approximate hint, not a floor feature.
        // Snap only after the complete camera fit maps that hint back to the
        // exact legal, vacant, actually-green destination already checked above.
        if (const auto& grid_to_placement = project_placement_grid()) {
            std::vector<cv::Point2f> cell;
            cv::perspectiveTransform(
                std::vector<cv::Point2f> { { static_cast<float>(center->x), static_cast<float>(center->y + 20) } },
                cell,
                grid_to_placement->inv());
            if (cell.size() == 1 && std::isfinite(cell.front().x) && std::isfinite(cell.front().y) &&
                std::abs(cell.front().x - m_reorient_col) <= .35 && std::abs(cell.front().y - m_reorient_row) <= .35) {
                foot =
                    registered_green_grid_cell_pose(map, map, board.grid_to_screen, { m_reorient_col, m_reorient_row });
                green_grid_receiver = foot.has_value();
            }
        }
    }
    if (!foot && center && board.verified && grid && occupant != grid->end()) {
        // All held field members were registered, including this exact
        // occupied target. Its full identity is checked again below. An
        // offset diamond UI point need not itself carry a floor ORB feature.
        foot = registered_grid_receiver_pose(placement, map, board.grid_to_screen, { m_reorient_col, m_reorient_row });
        occupied_grid_receiver = foot.has_value();
    }
    bool same_cell = false;
    std::vector<cv::Point2f> foot_grid;
    if (foot) {
        cv::perspectiveTransform(
            std::vector<cv::Point2f> { cv::Point2f(*foot) },
            foot_grid,
            board.grid_to_screen.inv());
        same_cell = foot_grid.size() == 1 && std::isfinite(foot_grid.front().x) && std::isfinite(foot_grid.front().y) &&
                    std::abs(foot_grid.front().x - m_reorient_col) <= .35 &&
                    std::abs(foot_grid.front().y - m_reorient_row) <= .35;
    }
    const bool identity_verified = same_cell && current_identity(m_name);
    auto receiver = basic_info_with_what("SPASpatialDirectionObserved");
    receiver["details"] = json::object { { "round", m_round },
                                         { "name", m_name },
                                         { "col", m_reorient_col },
                                         { "row", m_reorient_row },
                                         { "direction_modal_visible", modal_visible },
                                         { "center_x", center ? center->x : -1 },
                                         { "center_y", center ? center->y : -1 },
                                         { "foot_x", foot ? foot->x : -1 },
                                         { "foot_y", foot ? foot->y : -1 },
                                         { "grid_col", foot_grid.empty() ? -1 : foot_grid.front().x },
                                         { "grid_row", foot_grid.empty() ? -1 : foot_grid.front().y },
                                         { "same_cell", same_cell },
                                         { "identity_verified", identity_verified } };
    receiver["details"]["receiver_from_registered_green_grid"] = green_grid_receiver;
    receiver["details"]["receiver_from_registered_occupied_grid"] = occupied_grid_receiver;
    callback(AsstMsg::SubTaskExtraInfo, receiver);
    if (!same_cell || !identity_verified) {
        m_spatial_receiver_failed = true;
        m_spatial_drop_returned_without_direction =
            !modal_visible && m_relocate_field && occupant == grid->end() && preparation_matches();
        cancel_direction_modal(placement);
        reconcile_in_place_orientation();
        return false;
    }
    m_spatial_direction_attempted = true;
    const bool directed = drag_direction_only(Point(center->x, center->y));
    return reconcile_in_place_orientation() && directed;
}

bool asst::spa::SwapTaskPlugin::reconcile_in_place_orientation()
{
    if (!m_reorientation_reconcile || need_exit()) {
        return false;
    }
    const auto image = ctrler()->get_image();
    if (direction_modal_visible(image)) {
        if (!cancel_direction_modal(image)) {
            return false;
        }
        m_direction_committed = false;
    }
    return m_reorientation_reconcile();
}

bool asst::spa::SwapTaskPlugin::set_spatial_reconciliation_context(int round, int funds, int remaining)
{
    if (!m_reorientation_reconcile || round < m_round || round > 20 || funds < 0 || funds > 999 ||
        remaining != m_remaining) {
        return false;
    }
    m_round = round;
    m_funds = funds;
    return true;
}

bool asst::spa::SwapTaskPlugin::_run()
{
    LogInfo << "SPA swap request" << m_name << "deploy" << m_deploy_only << "move" << m_move_only << "face"
            << m_face_only << "inspect" << m_inspect_only << "sell" << m_sell_only << "terrain" << m_terrain_probe_only
            << "withdraw" << m_withdraw_only << "equip" << m_equip_only << "strategy" << m_strategy_only << "source"
            << m_source.x << m_source.y << "target" << m_target.x << m_target.y << "round" << m_round;
    if (m_replacement_reconcile_only) {
        // A persisted pending exchange grants no permission to replay input.
        // Re-establish the incoming field identity and unchanged population,
        // then inspect the bench for the outgoing identity only.
        if (!close_selection() || !restore_preparation() || !ensure_collapsed_shop()) {
            return false;
        }
        const bool incoming_verified = selected_identity(m_target, m_name);
        const auto observed = m_last_panel_identity;
        if (!close_selection() || !preparation_matches()) {
            return false;
        }
        if (!incoming_verified) {
            int probes = 0;
            m_replacement_original_preserved =
                observed == m_replaced && verify_replacement_bench_identity(m_name, probes) && preparation_matches();
            return m_replacement_original_preserved;
        }
        m_replacement_committed = true;
        m_replacement_postconditions_ready = true;
        m_field_pose_verified = true;
        return reconcile_replacement();
    }
    if (m_strategy_only && m_replaced == "幻画为真") {
        cv::Mat field, source_panel;
        if (!capture_collapsed_preparation(field) || !active_strategy_ready(field)) {
            return false;
        }
        const auto source_form = selected_field_identity(m_target, m_name, source_panel)
                                     ? selected_operator_card(source_panel, m_name)
                                     : std::nullopt;
        const bool source_verified = source_form && source_form->elite == m_target_elite &&
                                     selected_equipment_names(m_expected_equipment.size(), m_name) == m_expected_equipment;
        if (!close_selection() || !source_verified) {
            return false;
        }
        const Point view(1155, 235);
        if (!ctrler()->click(view) || !sleep(450)) {
            return false;
        }
        std::string description;
        OCRer panel(ctrler()->get_image(), Rect(750, 145, 390, 170));
        if (const auto texts = panel.analyze()) {
            for (const auto& text : *texts) {
                if (text.score >= .85) {
                    description += text.text;
                }
            }
        }
        if (!ctrler()->click(view) || !sleep(350) ||
            description.find("幻画为真") == std::string::npos || description.find("复制体") == std::string::npos ||
            description.find("精锐") == std::string::npos || description.find("装备") == std::string::npos ||
            !capture_collapsed_preparation(field) || !active_strategy_ready(field) ||
            (m_spatial_hold_guard && !m_spatial_hold_guard())) {
            return false;
        }
        m_strategy_copy_contact_attempted = true;
        auto issued = basic_info_with_what("SPAStrategyCopyContactAttempted");
        issued["details"] = json::object { { "target", m_name },
                                            { "round", m_round },
                                            { "source_form_and_equipment_verified", true },
                                            { "effect_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, issued);
        return held_drag();
    }
    if (m_strategy_only && m_replaced == "全场聚焦") {
        cv::Mat board;
        if (m_strategy_target_on_field ? !capture_collapsed_preparation(board) : !restore_preparation()) {
            return false;
        }
        if (!active_strategy_ready(board.empty() ? ctrler()->get_image() : board)) {
            return false;
        }
        cv::Mat target_panel;
        const bool identity_verified = m_strategy_target_on_field
                                           ? selected_field_identity(m_target, m_name, target_panel)
                                           : selected_identity(m_target, m_name, &target_panel);
        const auto card = identity_verified ? selected_operator_card(target_panel, m_name) : std::nullopt;
        if (!close_selection() || !card || card->elite) {
            return false;
        }
        const Point view(1155, 235);
        if (!ctrler()->click(view) || !sleep(600)) {
            return false;
        }
        std::string description;
        OCRer ocr(ctrler()->get_image(), Rect(750, 145, 390, 145));
        if (const auto texts = ocr.analyze()) {
            for (const auto& text : *texts) {
                if (text.score >= 0.85) {
                    description += text.text;
                }
            }
        }
        if (!ctrler()->click(view) || !sleep(500) ||
            description.find("全场聚焦") == std::string::npos || description.find("大嘴莫布") == std::string::npos ||
            description.find("精锐干员") == std::string::npos ||
            description.find("持续至战斗结束") == std::string::npos ||
            (m_strategy_target_on_field ? !capture_collapsed_preparation(board) : !restore_preparation()) ||
            !active_strategy_ready(board.empty() ? ctrler()->get_image() : board) || !held_drag()) {
            return false;
        }
        auto issued = basic_info_with_what("SPAActiveStrategyInputIssued");
        issued["details"] = json::object { { "strategy", m_replaced },
                                           { "target", m_name },
                                           { "round", m_round },
                                           { "effect_verified", false },
                                           { "trigger", "next_battle_start" } };
        callback(AsstMsg::SubTaskExtraInfo, issued);
        if (!sleep(1000)) {
            return false;
        }
        const int before = m_funds;
        const auto after = shop_number(ctrler()->get_image(), Rect(1180, 510, 45, 40));
        if (!after || *after != before - 1) {
            save_evidence(ctrler()->get_image(), utils::path("debug/SPA/temporary_elite_payment_unverified"), false);
            return false;
        }
        m_funds = *after;
        if (!restore_preparation() || !selected_identity(m_target, m_name) || !close_selection()) {
            return false;
        }
        auto queued = basic_info_with_what("SPATemporaryElitePaymentVerified");
        queued["details"] = json::object { { "strategy", m_replaced },   { "target", m_name },
                                           { "round", m_round },         { "funds_before", before },
                                           { "funds_after", m_funds },   { "target_identity_verified", true },
                                           { "effect_verified", false }, { "trigger", "next_battle_start" },
                                           { "expires", "battle_end" } };
        callback(AsstMsg::SubTaskExtraInfo, queued);
        return true;
    }
    if (m_strategy_only) {
        if (!restore_preparation() || !selected_identity(m_target, m_name) || !close_selection()) {
            return false;
        }
        const auto effects = acquired_effects(m_name, m_target_elite);
        if (!effects) {
            return false;
        }
        for (const auto& [pact, baseline] : m_pact_baselines) {
            if (pact_layers(ctrler()->get_image(), pact) != baseline) {
                LogError << "SPA strategy layer baseline does not match:" << pact;
                return false;
            }
        }
        const Point view(1155, 235);
        if (!ctrler()->click(view) || !sleep(600)) {
            return false;
        }
        OCRer description(ctrler()->get_image(), Rect(750, 145, 390, 145));
        const auto recognized = description.analyze();
        std::string text;
        if (recognized) {
            for (const auto& part : *recognized) {
                if (part.score >= 0.85) {
                    text += part.text;
                }
            }
        }
        if (text.find(m_replaced) == std::string::npos || text.find("获得时") == std::string::npos ||
            !ctrler()->click(view) || !sleep(500) || !restore_preparation()) {
            return false;
        }
        if (!held_drag()) {
            return false;
        }
        auto info = basic_info_with_what("SPAActiveStrategyInputIssued");
        info["details"] = json::object { { "strategy", m_replaced },
                                         { "target", m_name },
                                         { "effect_verified", false },
                                         { "round", m_round } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        if (!sleep(1000) || !restore_preparation()) {
            return false;
        }
        json::array verified_effects;
        for (const auto& effect : *effects) {
            const int before = m_pact_baselines.at(effect.target);
            const auto after = pact_layers(ctrler()->get_image(), effect.target);
            if (!after || *after != before + effect.amount) {
                save_evidence(ctrler()->get_image(), utils::path("debug/SPA/strategy_effect_unverified"), false);
                LogError << "SPA active strategy effect was not verified; no retry will be issued";
                return false;
            }
            verified_effects.emplace_back(
                json::object { { "pact", effect.target }, { "layers_before", before }, { "layers_after", *after } });
        }
        info = basic_info_with_what("SPAActiveStrategyEffectVerified");
        info["details"] = json::object { { "strategy", m_replaced },
                                         { "target", m_name },
                                         { "pact_effects", std::move(verified_effects) },
                                         { "cooldown_verified", false } };
        if (effects->size() == 1) {
            const auto& effect = effects->front();
            info["details"]["pact"] = effect.target;
            info["details"]["layers_before"] = m_pact_baselines.at(effect.target);
            info["details"]["layers_after"] = m_pact_baselines.at(effect.target) + effect.amount;
        }
        callback(AsstMsg::SubTaskExtraInfo, info);
        return true;
    }
    if (m_face_only && m_reorient_col >= 0) {
        return reorient_in_place();
    }
    if (m_face_only) {
        const auto placement_image = ctrler()->get_image();
        if (placement_image.empty() || placement_image.cols != 1280 || placement_image.rows != 720 ||
            preparation_round_number(placement_image, m_round) != m_round ||
            shop_number(placement_image, Rect(1180, 510, 45, 40)) != m_funds) {
            return false;
        }
        OCRer ocr(placement_image);
        const auto recognized = ocr.analyze();
        std::vector<TextObservation> texts;
        if (recognized) {
            for (const auto& text : *recognized) {
                texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
            }
        }
        if ((!m_preserve_population && m_remaining <= 0) || !selected_identity(m_source, m_name)) {
            return false;
        }
        // The face action starts from preparation, so the identity click above
        // must open the deployed-operator panel before checking the placement
        // interaction surface (including the B-server “出售” variant).
        const auto selected_image = ctrler()->get_image();
        OCRer selected_ocr(selected_image);
        const auto selected_texts = selected_ocr.analyze();
        std::vector<TextObservation> selected_observations;
        if (selected_texts) {
            for (const auto& text : *selected_texts) {
                selected_observations.push_back({ text.text, text.score, text.rect.x, text.rect.y });
            }
        }
        if (observe_page(selected_observations, {}).page != Page::Placement) {
            return false;
        }
        // Selecting a deployed operator pans the board to make room for the
        // identity panel.  Reuse the visual camera registration instead of
        // sending the pre-panel coordinate (which can now point at empty
        // terrain).  A direction action is rejected if the projected point
        // is not supported by a strong registration.
        const auto projection = estimate_camera_projection(placement_image, selected_image);
        auto projected_source = project_supported_point(
            projection,
            cv::Point2f(static_cast<float>(m_source.x), static_cast<float>(m_source.y)));
        if (!projected_source && projection.estimated && projection.inliers >= 12 && projection.median_error <= 3.0) {
            // The lower-row operator can lie just outside the inlier hull
            // even when the homography itself is strong.  Permit only a
            // bounded raw projection with the same quality thresholds; this
            // is still camera evidence, not an assumed fixed offset.
            std::vector<cv::Point2f> projected;
            cv::perspectiveTransform(
                std::vector<cv::Point2f> {
                    cv::Point2f(static_cast<float>(m_source.x), static_cast<float>(m_source.y)) },
                projected,
                projection.matrix);
            if (projected.size() == 1 && std::isfinite(projected.front().x) && std::isfinite(projected.front().y) &&
                projected.front().x >= 0 && projected.front().x < 1280 && projected.front().y >= 0 &&
                projected.front().y < 720) {
                projected_source = projected.front();
            }
        }
        if (!projected_source) {
            // B-server's selected-operator panel has a stable one-column
            // camera shift for the lower row.  This is input-only fallback:
            // all placement and facing verification below remains mandatory.
            const auto panel_text = selected_observations;
            const bool placement_panel = observe_page(panel_text, {}).page == Page::Placement;
            if (placement_panel && m_source.y >= 400 && m_source.x >= 450) {
                projected_source = cv::Point2f(static_cast<float>(m_source.x - 296), static_cast<float>(m_source.y));
            }
        }
        if (!projected_source) {
            save_evidence(selected_image, utils::path("debug/SPA/direction_camera_unverified"), false);
            LogError << "SPA direction camera projection did not support selected operator";
            return false;
        }
        Point selected_center(cvRound(projected_source->x), cvRound(projected_source->y));
        // The homography point corresponds to the sprite foot/interaction
        // point.  Direction drag must begin on the visible operator body;
        // the verified lower-row panel shifts that point by this bounded
        // body offset.
        if (m_source.y >= 400 && selected_center.x >= 600) {
            selected_center.x -= 92;
            selected_center.y -= 28;
        }
        if (!select_direction(selected_center) || !sleep(1200)) {
            return false;
        }
        if (!m_preserve_population) {
            --m_remaining;
        }
        if (!close_selection()) {
            LogError << "SPA face reconciliation could not restore preparation after direction input";
            return false;
        }
        if (!selected_identity(m_target, m_name)) {
            LogError << "SPA face reconciliation could not reselect target identity";
            return false;
        }
        if (!close_selection()) {
            LogError << "SPA face reconciliation could not close target identity panel";
            return false;
        }
        if (m_preserve_population && !m_replaced.empty() &&
            (!selected_identity(m_swap_origin, m_replaced) || !close_selection())) {
            return false;
        }
        const auto facing_image = ctrler()->get_image();
        std::optional<char> facing;
        float post_release_model_score = 0.0F;
        std::string post_release_model_direction;
        BattlefieldClassifier post_release_classifier(facing_image);
        post_release_classifier.set_object_of_interest({ false, true });
        post_release_classifier.set_base_point(m_target);
        if (const auto result = post_release_classifier.analyze()) {
            post_release_model_score = result->deploy_direction.score;
            switch (result->deploy_direction.direction) {
            case battle::DeployDirection::Right:
                post_release_model_direction = "E";
                break;
            case battle::DeployDirection::Left:
                post_release_model_direction = "W";
                break;
            case battle::DeployDirection::Down:
                post_release_model_direction = "S";
                break;
            case battle::DeployDirection::Up:
                post_release_model_direction = "N";
                break;
            default:
                break;
            }
            const char predicted = post_release_model_direction.empty() ? '?' : post_release_model_direction.front();
            const char expected = m_direction.x > 0 ? 'E' : m_direction.x < 0 ? 'W' : m_direction.y > 0 ? 'S' : 'N';
            if (predicted == expected && post_release_model_score >= 0.55F) {
                facing = predicted;
            }
        }
        auto orientation_info = basic_info_with_what("SPAOrientationReconciliation");
        orientation_info["details"] = json::object { { "name", m_name },
                                                     { "round", m_round },
                                                     { "model_direction", post_release_model_direction },
                                                     { "model_score", post_release_model_score },
                                                     { "marker_direction_known", false } };
        callback(AsstMsg::SubTaskExtraInfo, orientation_info);
        // The interaction point is operator-center-ish, while the orange
        // facing chevron is rendered near the sprite's feet. Probe a small,
        // bounded vertical neighborhood and still require one unique result.
        for (const int dy : { 0, 20, 30, -20 }) {
            const auto candidate = deployed_facing(facing_image, cv::Point(m_target.x, m_target.y + dy));
            if (!candidate) {
                continue;
            }
            if (facing && *facing != *candidate) {
                facing.reset();
                break;
            }
            facing = candidate;
        }
        const char expected = m_direction.x > 0 ? 'E' : m_direction.x < 0 ? 'W' : m_direction.y > 0 ? 'S' : 'N';
        if (facing != expected) {
            save_evidence(ctrler()->get_image(), utils::path("debug/SPA/facing_reconciliation"), false);
            return false;
        }
        auto info = basic_info_with_what("SPADeploymentVerified");
        info["details"] = json::object { { "name", m_name },
                                         { "round", m_round },
                                         { "remaining_deployment", m_remaining },
                                         { "target_identity_verified", true },
                                         { "direction_input_issued", true },
                                         { "direction_verified", true } };
        info["details"]["population_consumed"] = !m_preserve_population;
        callback(AsstMsg::SubTaskExtraInfo, info);
        if (m_preserve_population && !m_replaced.empty()) {
            info = basic_info_with_what("SPASwapVerified");
            info["details"] = json::object { { "name", m_name },
                                             { "replaced", m_replaced },
                                             { "round", m_round },
                                             { "remaining_deployment", m_remaining },
                                             { "target_identity_verified", true },
                                             { "source_identity_verified", true } };
            callback(AsstMsg::SubTaskExtraInfo, info);
        }
        return true;
    }
    const bool wildcard_fast_deploy = m_fast_mode && m_best_effort_deployment && m_deploy_only && m_name == "*";
    if (!wildcard_fast_deploy && !restore_preparation()) {
        return false;
    }
    if (m_deploy_only && !m_replace_only) {
        cv::Mat reference;
        if (!close_selection() || !ensure_collapsed_shop(&reference)) {
            return false;
        }
        m_equipment_reference = (reference.empty() ? ctrler()->get_image() : reference).clone();
        if (!ensure_expanded_shop()) {
            return false;
        }
    }
    if (m_replace_only) {
        // Establish the outgoing identity in a fresh clean field frame before
        // opening any bench panel. Source discovery below restores its own
        // camera and verifies the incoming identity independently.
        cv::Mat reference;
        if (!close_selection() || !ensure_collapsed_shop(&reference)) {
            return false;
        }
        m_equipment_reference = (reference.empty() ? ctrler()->get_image() : reference).clone();
        save_evidence(m_equipment_reference, utils::path("debug/SPA/replacement_reference"), false);
        if (!selected_identity(m_target, m_replaced) || !close_selection() || !ensure_expanded_shop()) {
            return false;
        }
        if (!m_registered_grid_reference.empty()) {
            const auto grid = registered_grid_projection(
                m_registered_grid_reference,
                m_equipment_reference,
                m_registered_grid_projection);
            const std::vector<PlacementMember> target {
                { m_target.x, m_target.y, operator_placement_role(m_replaced), m_replaced },
            };
            const auto cells = grid ? registered_grid_members(*grid, target) : std::nullopt;
            if (cells) {
                const auto& cell = cells->front();
                const bool boss_phase = preparation_boss_health_bar(m_equipment_reference).has_value();
                m_replacement_above_blocking_device =
                    terrain_has_blocking_device_below(m_registered_grid_map_id, { cell.col, cell.row }, boss_phase);
                const auto role = BattleData.get_first_role(m_name);
                const auto location = season_location_type(m_name, BattleData.get_location_type(role, m_name));
                if (terrain_allows_deployment(
                        m_registered_grid_map_id,
                        { cell.col, cell.row },
                        location == battle::LocationType::Melee,
                        boss_phase)) {
                    m_registered_replacement_receiver = RegisteredReplacementReceiver { { cell.col, cell.row }, *grid };
                }
            }
        }
    }
    cv::Mat verified_equipment_source_panel;
    if (m_find_equipment_source) {
        const bool persistent_equipment = m_fast_mode && !is_immediate_cash_equipment(m_name) &&
                                          !is_deferred_cash_equipment(m_name) && !m_promotion_consumable &&
                                          m_pact_recruitment_pact.empty() &&
                                          std::ranges::any_of(equipment_profiles(), [&](const auto& profile) {
                                              return profile.name == m_name && !profile.consumable;
                                          });
        m_equipment_stage = "source_identity";
        cv::Mat inventory_image;
        if (!ensure_expanded_shop(&inventory_image)) {
            return false;
        }
        if (inventory_image.empty()) {
            inventory_image = ctrler()->get_image();
        }
        const auto candidates = equipment_inventory_candidates(inventory_image);
        json::array inventory_points;
        for (const auto& point : candidates) {
            inventory_points.emplace_back(json::object { { "x", point.x }, { "y", point.y } });
        }
        auto inventory_scan = basic_info_with_what("SPAEquipmentInventoryScan");
        inventory_scan["details"] = json::object { { "equipment", m_name },
                                                   { "round", m_round },
                                                   { "candidates", std::move(inventory_points) },
                                                   { "identities_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, inventory_scan);
        if (candidates.empty()) {
            save_evidence(inventory_image, utils::path("debug/SPA/equipment_inventory_empty"), false);
        }
        std::vector<size_t> order;
        for (size_t index = 0; index < candidates.size(); ++index) {
            order.push_back(index);
        }
        if (m_preferred_equipment.x >= 0) {
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return equipment_source_distance(candidates[a], m_preferred_equipment) <
                       equipment_source_distance(candidates[b], m_preferred_equipment);
            });
        }
        if (m_bench_identity_hints) {
            std::stable_partition(order.begin(), order.end(), [&](size_t index) {
                const auto& source = candidates[index];
                return (m_preferred_equipment.x >= 0 &&
                        equipment_source_distance(source, m_preferred_equipment) <= 12 * 12) ||
                       !inventory_operator_visual_hint(*m_bench_identity_hints, inventory_image, source);
            });
        }
        for (size_t probe = 0; probe < order.size() && probe < 10; ++probe) {
            const auto index = order[probe];
            if (need_exit() || !ensure_expanded_shop()) {
                return false;
            }
            const Point candidate(candidates[index].x, candidates[index].y);
            cv::Mat source_panel;
            const auto identity = bench_identity(candidate, false, {}, nullptr, &source_panel);
            if (identity && equipment_title_matches(m_name, *identity)) {
                if (persistent_equipment && !source_panel.empty()) {
                    // Keep the verified source selected until recipient inspection.
                    // Rebind the original source anchor after returning to the shop.
                    m_source = candidate;
                    m_equipment_source_index = index;
                    verified_equipment_source_panel = std::move(source_panel);
                    break;
                }
                if (is_immediate_cash_equipment(m_name)) {
                    const auto credit = read_selected_cash_equipment_credit(ctrler()->get_image());
                    if (!credit || *credit < m_minimum_cash_credit || m_funds > 999 - *credit) {
                        m_equipment_stage = "cash_quote_unusable";
                        close_selection();
                        return false;
                    }
                    m_equipment_cash_credit = *credit;
                }
                else if (is_deferred_cash_equipment(m_name)) {
                    OCRer effect(ctrler()->get_image(), Rect(0, 350, 450, 350));
                    std::string description;
                    if (const auto texts = effect.analyze()) {
                        for (const auto& text : *texts) {
                            if (text.score >= 0.85) {
                                description += text.text;
                            }
                        }
                    }
                    const auto amount = equipment_next_round_cash_gain(m_name, description);
                    if (!amount) {
                        m_equipment_stage = "delayed_cash_quote_unverified";
                        close_selection();
                        return false;
                    }
                    m_equipment_next_round_credit_quote = *amount;
                }
                if (!close_selection() || !ensure_expanded_shop()) {
                    return false;
                }
                if (is_immediate_cash_equipment(m_name)) {
                    if (!reacquire_cash_equipment_source(Point(candidates[index].x, candidates[index].y))) {
                        return false;
                    }
                }
                else {
                    const auto frame = ctrler()->get_image();
                    const auto refreshed = equipment_inventory_candidates(frame);
                    const auto source = stable_equipment_source(candidates[index], refreshed);
                    if (!source) {
                        m_equipment_stage = "equipment_source_anchor_unverified";
                        save_evidence(frame, utils::path("debug/SPA/equipment_source_anchor_unverified"), false);
                        return false;
                    }
                    m_source = Point(refreshed[*source].x, refreshed[*source].y);
                    m_equipment_source_index = *source;
                }
                break;
            }
            if (!close_selection()) {
                return false;
            }
        }
        if (!m_equipment_source_index) {
            return false;
        }
    }
    bool sale_source_panel_open = false;
    if (m_find_bench_source) {
        // The collapsed shop can cover the rightmost bench operator.  Keep
        // the expanded shop for source discovery; the held drag itself moves
        // to the far-left staging area and fades the panel without releasing,
        // so the map is still reacquired before the final drop.
        const bool wildcard_bench_scan = m_fast_mode && m_deploy_only && m_name == "*";
        bool expanded_for_scan = false;
        cv::Mat bench_image;
        // The wildcard path already arrives from the expanded preparation
        // observer. Named scans must first certify the expanded bench layout.
        if (wildcard_bench_scan) {
            bench_image = ctrler()->get_image();
        }
        else {
            if (!ensure_shop_visibility(true, &bench_image, {}, &expanded_for_scan)) {
                return false;
            }
            if (bench_image.empty()) {
                bench_image = ctrler()->get_image();
            }
            if (!preparation_matches(bench_image)) {
                return false;
            }
        }
        if (bench_image.empty()) {
            return false;
        }
        auto shop_state = basic_info_with_what("SPAShopExpandedForDeployment");
        shop_state["details"] = json::object { { "expanded_before_source_scan", expanded_for_scan },
                                               { "rightmost_bench_visible", true },
                                               { "held_drag_reacquisition", true } };
        callback(AsstMsg::SubTaskExtraInfo, shop_state);
        bool found = false;
        bool source_identity_deferred_to_hold = false;
        // On the B-server the purchased operator is rendered as a sprite in
        // the lower map-edge bench strip (around y=450).  The bottom card
        // row at y≈610–680 is still the shop and must never be used as a
        // deployment source.  Probe the bounded bench strip and require the
        // opened panel to identify the requested operator before accepting it.
        // Derive occupied slot centres from the visible tier badges.  Do not
        // probe a geometric grid: empty slots and map props can open the same
        // operator panel and were the cause of the old click/cancel loop.
        // The compact sprite needs the same bench hitbox for deployment and sale.
        const bool find_christine = m_name == "Miss.Christine";
        const auto christine_sprite = [find_christine](const cv::Mat& frame, const Point& point) {
            return find_christine && bench_christine_sprite_visible(frame, cv::Point(point.x, point.y));
        };
        std::vector<Point> bench_candidates = detect_bench_centres(bench_image, find_christine);
        std::vector<int> candidate_x;
        for (const auto& candidate : bench_candidates) {
            candidate_x.push_back(candidate.x);
        }
        // The verified B-server two-card acquisition placed the newer card
        // to the left of the existing right-aligned card. This is a probe
        // preference only; the selected catalogue identity is still required.
        auto probe_order =
            bench_source_probe_order(candidate_x, m_recent_acquisition_source ? std::optional(0) : std::nullopt);
        int matching_bench_hints = 0;
        if (m_bench_identity_hints && m_name != "*") {
            std::vector<int> candidate_priorities(bench_candidates.size(), 1);
            for (size_t index = 0; index < bench_candidates.size(); ++index) {
                const auto& point = bench_candidates[index];
                const auto hint =
                    inventory_operator_visual_hint(*m_bench_identity_hints, bench_image, { point.x, point.y });
                if (hint) {
                    candidate_priorities[index] = hint->first == m_name ? 2 : 0;
                    matching_bench_hints += hint->first == m_name;
                }
            }
            // Hints change probe order only. The exact current title and
            // requested form still have to pass before sale or drop.
            std::stable_sort(probe_order.begin(), probe_order.end(), [&](size_t lhs, size_t rhs) {
                return candidate_priorities[lhs] > candidate_priorities[rhs];
            });
        }
        int source_probes = 0;
        json::array observed_sources;
        std::optional<size_t> named_source_index;
        Point verified_bench_contact;
        // Reuse the verified panel and its first clean frame without input between the reads.
        cv::Mat selected_bench_frame;
        cv::Mat closed_bench_frame;
        for (const auto index : probe_order) {
            const auto& candidate = bench_candidates[index];
            if (++source_probes > 12) {
                break;
            }
            if (need_exit()) {
                return false;
            }
            if (!wildcard_bench_scan && !ensure_shop_visibility(true, nullptr, closed_bench_frame)) {
                return false;
            }
            closed_bench_frame.release();
            if (!deployment_source_is_bench(candidate.x, candidate.y)) {
                continue;
            }
            // Skip empty slots before any click.  This prevents the old
            // click/cancel loop on the leftmost empty bench position.
            const bool compact_sprite = christine_sprite(bench_image, candidate);
            if (!compact_sprite && !bench_sprite_visible(bench_image, candidate)) {
                continue;
            }
            const auto foot =
                verified_bench_grab_point(bench_image, { candidate.x, candidate.y }, { candidate.x, candidate.y }, {});
            if (wildcard_bench_scan) {
                // The phase-one route does not need to identify a particular
                // operator: the observer has already established that a
                // purchased unit exists and the visible tier badge proves
                // this is an occupied bench slot.  Avoid opening/closing a
                // detail panel just to match "*"; the held drag and live
                // remaining-deployment counter are the authoritative checks.
                if (!foot) {
                    continue;
                }
                m_source = Point(foot->x, foot->y);
                found = true;
                break;
            }
            if (m_fast_mode && m_deploy_only && !m_joint_deployment && bench_candidates.size() == 1 &&
                !m_verify_source_form && foot) {
                // With one detected occupied card, opening and closing its
                // panel only changes the camera. The named held-drag path
                // still verifies the actual title before any field drop.
                m_source = Point(foot->x, foot->y);
                source_identity_deferred_to_hold = true;
                found = true;
                break;
            }
            const auto identity = bench_identity(
                compact_sprite ? Point(candidate.x, candidate.y + 27) : candidate,
                m_sell_only,
                {},
                m_deploy_only ? &verified_bench_contact : nullptr,
                &selected_bench_frame);
            if (identity && m_bench_identity_hints && operator_pacts().contains(*identity)) {
                remember_inventory_operator_title(
                    *m_bench_identity_hints,
                    bench_image,
                    { candidate.x, candidate.y },
                    *identity,
                    true);
            }
            observed_sources.emplace_back(
                json::object { { "x", candidate.x }, { "y", candidate.y }, { "name", identity.value_or("") } });
            if (identity == m_name) {
                if (m_sell_only) {
                    // The selected panel already exposes the sale controls. Reopening
                    // from the lower drag point can hit the adjacent freeze control.
                    m_source = candidate;
                    sale_source_panel_open = true;
                    found = true;
                    break;
                }
                // Recheck the grab point after restoring the expanded layout.
                if (m_joint_deployment) {
                    remember_inventory_operator_title(
                        m_joint_named_bench_sprite, bench_image, { candidate.x, candidate.y }, *identity, true);
                }
                named_source_index = index;
                found = true;
                break;
            }
            if (!close_selection(&closed_bench_frame, selected_bench_frame)) {
                return false;
            }
        }
        if (m_bench_identity_hints && m_name != "*") {
            auto timing = basic_info_with_what("SPABenchSourceProbeCount");
            timing["details"] = json::object { { "name", m_name },
                                               { "round", m_round },
                                               { "candidate_probes", source_probes },
                                               { "matching_visual_hints", matching_bench_hints },
                                               { "source_found", found } };
            callback(AsstMsg::SubTaskExtraInfo, timing);
        }
        if (!found) {
            m_bench_source_search_exhausted = true;
            save_evidence(bench_image, utils::path("debug/SPA/bench_source_unresolved"), false);
            auto unresolved = basic_info_with_what("SPABenchSourceUnresolved");
            unresolved["details"] = json::object { { "name", m_name },
                                                   { "round", m_round },
                                                   { "candidate_count", bench_candidates.size() },
                                                   { "candidate_probes", source_probes },
                                                   { "observed_sources", std::move(observed_sources) },
                                                   { "held_input_started", false },
                                                   { "roster_absence_proven", false } };
            callback(AsstMsg::SubTaskExtraInfo, unresolved);
            return false;
        }
        // Wildcard phase-one deployment only uses the tier-badge frame and
        // never opens an operator panel. Avoid another full-frame OCR pass;
        // named swaps still require the strict close/reconciliation path.
        if (!sale_source_panel_open && !source_identity_deferred_to_hold && !wildcard_bench_scan &&
            !close_selection(&closed_bench_frame, selected_bench_frame)) {
            return false;
        }
        if (named_source_index) {
            // Opening an identity panel collapses the shop and shifts the
            // bench. Restore its original layout before reusing any grab
            // position. No roster mutation occurred, so slot order is stable.
            if (!ensure_shop_visibility(true, &bench_image, closed_bench_frame)) {
                return false;
            }
            if (bench_image.empty()) {
                bench_image = ctrler()->get_image();
            }
            const auto refreshed = detect_bench_centres(bench_image, find_christine);
            const auto expected = bench_candidates[*named_source_index];
            std::vector<std::pair<int, int>> fresh_centres;
            for (const auto& candidate : refreshed) {
                fresh_centres.emplace_back(candidate.x, candidate.y);
            }
            const auto reacquired = reacquire_bench_source(fresh_centres, { expected.x, expected.y });
            if (!reacquired || (!bench_sprite_visible(bench_image, refreshed[*reacquired]) &&
                                !christine_sprite(bench_image, refreshed[*reacquired]))) {
                save_evidence(bench_image, utils::path("debug/SPA/bench_source_refresh_rejected"), false);
                auto rejected = basic_info_with_what("SPABenchSourceRefreshRejected");
                rejected["details"] = json::object { { "name", m_name },
                                                     { "round", m_round },
                                                     { "expected_x", expected.x },
                                                     { "expected_y", expected.y },
                                                     { "candidates_before", bench_candidates.size() },
                                                     { "candidates_after", refreshed.size() },
                                                     { "unique_slot_reacquired", reacquired.has_value() },
                                                     { "input_committed", false } };
                callback(AsstMsg::SubTaskExtraInfo, rejected);
                return false;
            }
            const auto& point = refreshed[*reacquired];
            const auto grab = verified_bench_grab_point(
                bench_image,
                { point.x, point.y },
                { expected.x, expected.y },
                cv::Point(verified_bench_contact.x, verified_bench_contact.y));
            if (!grab) {
                return false;
            }
            m_source = Point(grab->x, grab->y);
            if (m_joint_deployment && !m_joint_named_bench_sprite.empty()) {
                m_joint_named_bench_slot.emplace(point.x, point.y);
            }
            bench_candidates = refreshed;
        }
        auto source = basic_info_with_what("SPABenchSourceVerified");
        json::array candidate_centres;
        for (const auto& candidate : bench_candidates) {
            candidate_centres.emplace_back(json::object { { "x", candidate.x }, { "y", candidate.y } });
        }
        source["details"] = json::object { { "name", m_name },
                                           { "identity_deferred_to_hold", source_identity_deferred_to_hold },
                                           { "source_x", m_source.x },
                                           { "source_y", m_source.y },
                                           { "candidate_count", static_cast<int>(bench_candidates.size()) },
                                           { "candidate_probes", source_probes },
                                           { "recent_acquisition_priority", m_recent_acquisition_source },
                                           { "candidate_centres", std::move(candidate_centres) },
                                           { "round", m_round } };
        callback(AsstMsg::SubTaskExtraInfo, source);
    }
    const auto preparation_image = m_inspect_only ? ctrler()->get_image() : cv::Mat();
    // A bench slot rotates as the hand is consumed, so the hand is identified by the placement card
    // rather than by a side panel that only shop cards open.
    if (!m_deploy_only && verified_equipment_source_panel.empty() &&
        !(sale_source_panel_open ? current_identity(m_name) : selected_identity(m_source, m_name))) {
        return false;
    }
    if (m_terrain_probe_only) {
        if (!close_selection() ||
            !save_evidence(ctrler()->get_image(), utils::path("debug/SPA/terrain_probe_before"))) {
            return false;
        }
        InputEvent down;
        down.type = InputEvent::Type::TOUCH_DOWN;
        down.point = m_source;
        InputEvent commit;
        commit.type = InputEvent::Type::COMMIT;
        bool held = ctrler()->inject_input_event(down) && ctrler()->inject_input_event(commit) && sleep(900);
        if (held && m_probe_drag_pixels > 0) {
            InputEvent move;
            move.type = InputEvent::Type::TOUCH_MOVE;
            move.point = Point(m_source.x + m_probe_drag_pixels, m_source.y);
            held = ctrler()->inject_input_event(move) && ctrler()->inject_input_event(commit) && sleep(900);
        }
        const bool saved = held && save_evidence(ctrler()->get_image(), utils::path("debug/SPA/terrain_probe_held"));
        // Release even after a failed capture. A bounded drag is an explicit research option.
        InputEvent up;
        up.type = InputEvent::Type::TOUCH_UP;
        const bool released = ctrler()->inject_input_event(up);
        const bool committed = ctrler()->inject_input_event(commit);
        if (!saved || !released || !committed || !sleep(700) || !restore_preparation() ||
            !selected_identity(m_source, m_name) || !close_selection()) {
            return false;
        }
        auto probe = basic_info_with_what("SPATerrainProbeCaptured");
        probe["details"] = json::object { { "name", m_name },
                                          { "source_x", m_source.x },
                                          { "source_y", m_source.y },
                                          { "round", m_round },
                                          { "funds", m_funds },
                                          { "remaining_deployment", m_remaining },
                                          { "source_identity_verified_after", true },
                                          { "touch_move_issued", m_probe_drag_pixels > 0 },
                                          { "probe_drag_pixels", m_probe_drag_pixels },
                                          { "deployment_cells_verified", false },
                                          { "direction_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, probe);
        return true;
    }
    if (m_sell_only) {
        const auto sale_image = ctrler()->get_image();
        OCRer ocr(sale_image);
        const auto recognized = ocr.analyze();
        std::optional<Rect> sell;
        std::vector<TextObservation> quote_texts;
        if (recognized) {
            for (const auto& text : *recognized) {
                quote_texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
                if (text.text == "出售" && text.score >= 0.9 && text.rect.x >= 600 && text.rect.x < 900 &&
                    text.rect.y >= 320 && text.rect.y < 470) {
                    if (sell) {
                        return false;
                    }
                    sell = text.rect;
                }
            }
        }
        if (!sell) {
            save_evidence(sale_image, utils::path("debug/SPA/sale_control_unresolved"), false);
            auto unresolved = basic_info_with_what("SPASaleControlUnresolved");
            unresolved["details"] = json::object { { "name", m_name },
                                                   { "round", m_round },
                                                   { "source_x", m_source.x },
                                                   { "source_y", m_source.y },
                                                   { "input_committed", false } };
            callback(AsstMsg::SubTaskExtraInfo, unresolved);
            return false;
        }
        auto quote = sale_credit_from_panel(quote_texts, sell->x, sell->y);
        if (!quote) {
            if (const auto local = sale_credit_number(sale_image, sell->x, sell->y)) {
                quote = local->first;
                auto quoted = basic_info_with_what("SPASaleQuoteObserved");
                quoted["details"] = json::object { { "name", m_name },
                                                   { "credit", *quote },
                                                   { "confidence", local->second },
                                                   { "source", "signed_sale_char_roi" },
                                                   { "sale_input_issued", false } };
                callback(AsstMsg::SubTaskExtraInfo, quoted);
            }
        }
        if (quote && m_funds <= 999 - *quote && current_identity(m_name) && selected_form_matches(sale_image, m_name)) {
            m_observed_sale_credit = quote;
        }
        if (!m_observed_sale_credit || *m_observed_sale_credit < m_minimum_sale_credit ||
            (m_sale_credit > 0 && m_sale_credit != *m_observed_sale_credit)) {
            save_evidence(sale_image, utils::path("debug/SPA/sale_quote_unverified"), false);
            return false;
        }
        m_sale_credit = *quote;
        if (!ctrler()->click(*sell)) {
            return false;
        }
        m_sale_committed = true;
        if (!sleep(1200)) {
            return false;
        }
        auto info = basic_info_with_what("SPASaleInputIssued");
        info["details"] = json::object { { "name", m_name },
                                         { "funds_before", m_funds },
                                         { "quoted_credit", *quote },
                                         { "credit_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        const auto after = shop_number(ctrler()->get_image(), Rect(1180, 510, 45, 40));
        if (!after || *after != m_funds + m_sale_credit) {
            save_evidence(ctrler()->get_image(), utils::path("debug/SPA/sale_reconciliation"), false);
            LogError << "SPA sale credit was not verified; no retry will be issued";
            return false;
        }
        const int before = m_funds;
        m_funds = *after;
        if (!restore_preparation()) {
            return false;
        }
        info = basic_info_with_what("SPASaleVerified");
        info["details"] = json::object { { "name", m_name },
                                         { "round", m_round },
                                         { "funds_before", before },
                                         { "funds_after", *after },
                                         { "credit", m_sale_credit },
                                         { "remaining_deployment", m_remaining },
                                         { "equipment_returned_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        return true;
    }
    if (m_inspect_only) {
        OCRer tabs(ctrler()->get_image(), Rect(0, 350, 450, 45));
        const auto recognized_tabs = tabs.analyze();
        std::optional<Rect> tab;
        if (recognized_tabs) {
            for (const auto& text : *recognized_tabs) {
                if (text.text == m_inspect_tab && text.score >= 0.9) {
                    tab = text.rect;
                }
            }
        }
        if (!tab || !ctrler()->click(*tab) || !sleep(700) || !current_identity(m_name)) {
            return false;
        }
        const auto image = ctrler()->get_image();
        OCRer ocr(image);
        const auto recognized = ocr.analyze();
        if (!recognized) {
            return false;
        }
        json::array raw;
        for (const auto& text : *recognized) {
            if (text.rect.x < 450) {
                raw.emplace_back(
                    json::object { { "text", text.text },
                                   { "score", text.score },
                                   { "x", text.rect.x },
                                   { "y", text.rect.y } });
            }
        }
        save_evidence(ctrler()->get_image(), utils::path("debug/SPA/unit_inspection"), false);
        const auto projection = estimate_camera_projection(preparation_image, image);
        const auto projected_source = project_supported_point(
            projection,
            cv::Point2f(static_cast<float>(m_source.x), static_cast<float>(m_source.y)));
        json::array matrix;
        if (projection.estimated) {
            for (int row = 0; row < 3; ++row) {
                json::array values;
                for (int col = 0; col < 3; ++col) {
                    values.emplace_back(projection.matrix.at<double>(row, col));
                }
                matrix.emplace_back(std::move(values));
            }
        }
        auto registration = basic_info_with_what("SPACameraRegistration");
        registration["details"] = json::object { { "name", m_name },
                                                 { "estimated", projection.estimated },
                                                 { "matches", projection.matches },
                                                 { "inliers", projection.inliers },
                                                 { "median_error", projection.median_error },
                                                 { "matrix", std::move(matrix) },
                                                 { "source_supported", projected_source.has_value() },
                                                 { "ground_plane_verified", false },
                                                 { "placement_authorized", false } };
        if (projected_source) {
            registration["details"]["selected_x"] = projected_source->x;
            registration["details"]["selected_y"] = projected_source->y;
        }
        callback(AsstMsg::SubTaskExtraInfo, registration);
        auto info = basic_info_with_what("SPAUnitInspection");
        info["details"] = json::object {
            { "name", m_name },
            { "round", m_round },
            { "facing_known", deployed_facing(preparation_image, cv::Point(m_source.x, m_source.y)).has_value() },
            { "facing",
              std::string(1, deployed_facing(preparation_image, cv::Point(m_source.x, m_source.y)).value_or('?')) },
            { "equipment_slots_complete", false },
            { "texts", std::move(raw) }
        };
        callback(AsstMsg::SubTaskExtraInfo, info);
        return close_selection();
    }
    if (m_withdraw_only) {
        const auto retreat = selected_retreat_control(
            ctrler()->get_image(),
            TemplResource::get_instance().get_templ("MiniGame@SPA@RetreatSelected.png"));
        if (!retreat ||
            !ctrler()->click(Rect(retreat->rect.x, retreat->rect.y, retreat->rect.width, retreat->rect.height)) ||
            !sleep(1200)) {
            return false;
        }
        ++m_remaining;
        if (!close_selection() || !preparation_matches()) {
            auto pending = basic_info_with_what("SPAWithdrawalReconciliationRequired");
            pending["details"] = json::object { { "name", m_name },
                                                { "round", m_round },
                                                { "withdrawal_input_issued", true },
                                                { "expected_remaining_deployment", m_remaining },
                                                { "resumable", true } };
            callback(AsstMsg::SubTaskExtraInfo, pending);
            return false;
        }
        auto info = basic_info_with_what("SPAWithdrawalVerified");
        info["details"] = json::object { { "name", m_name },
                                         { "round", m_round },
                                         { "remaining_deployment", m_remaining },
                                         { "funds_preserved", true },
                                         { "population_change_verified", true } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        return true;
    }
    if (m_deploy_only) {
        LogInfo << "SPA deploy branch entered" << m_remaining;
        if (m_remaining <= 0 && !m_replace_only) {
            LogError << "SPA deploy has no remaining population";
            return false;
        }
        if (!wildcard_fast_deploy && !close_selection()) {
            LogError << "SPA deploy could not settle the preparation page";
            return false;
        }
        if (!m_replace_only && !m_preserve_population && m_target.x >= 180 && m_target.x < 1000 && m_target.y >= 140 &&
            m_target.y < 510) {
            // Do not click/OCR every candidate cell.  The target is chosen
            // from the normal preparation map; most targets are empty, and
            // probing them opened a full operator panel repeatedly (about
            // 17 seconds in the previous trace).  A cheap visual occupancy
            // gate keeps the identity probe only for real swaps.
            const auto occupancy_image = ctrler()->get_image();
            const bool target_occupied = occupied_deployment_cell(occupancy_image, cv::Point(m_target.x, m_target.y));
            auto occupancy_info = basic_info_with_what("SPATargetOccupancyGate");
            occupancy_info["details"] = json::object { { "name", m_name },
                                                       { "target_x", m_target.x },
                                                       { "target_y", m_target.y },
                                                       { "occupied", target_occupied } };
            callback(AsstMsg::SubTaskExtraInfo, occupancy_info);
            if (target_occupied) {
                if (const auto displaced = selected_identity_any(m_target); displaced && *displaced != m_name) {
                    m_displaced_name = *displaced;
                    auto swap_probe = basic_info_with_what("SPADisplacedTargetDetected");
                    swap_probe["details"] = json::object { { "name", m_name },
                                                           { "displaced", m_displaced_name },
                                                           { "target_x", m_target.x },
                                                           { "target_y", m_target.y },
                                                           { "population_preserved", true } };
                    callback(AsstMsg::SubTaskExtraInfo, swap_probe);
                }
            }
        }
        if (!held_drag()) {
            LogError << "SPA deploy drag input was not delivered";
            return false;
        }
        if (m_replace_only) {
            // A valid drop may already exchange the units before direction
            // reconciliation. Do not report a failed postcondition as an
            // untouched board or replay another exchange against stale poses.
            m_replacement_committed = true;
        }
        if (!sleep(m_fast_mode ? 350 : 1200)) {
            return false;
        }
        const auto settled_image = ctrler()->get_image();
        save_evidence(settled_image, utils::path("debug/SPA/deploy_settled"), false);
        std::vector<TextObservation> texts;
        const bool has_replacement_preview = m_replace_only && m_deployment_hint_verified && !m_held_board.empty();
        const auto replacement_modal_visible = [&] {
            if (!has_replacement_preview) {
                return false;
            }
            const auto center = direction_selection_center(settled_image);
            return center && direction_cancel_target(settled_image, *center);
        };
        // On a full board only the occupied receivers turn green. The
        // diamond and cancel control identify the modal without registering
        // the receiver twice. Its identity and cell are checked before input.
        const bool visual_placement = deployment_surface_visible(settled_image) || replacement_modal_visible();
        if (!(m_best_effort_deployment && visual_placement)) {
            OCRer ocr(settled_image);
            if (const auto recognized = ocr.analyze()) {
                for (const auto& text : *recognized) {
                    texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
                }
            }
        }
        // This season may drop the operator straight onto the field when the target tile is
        // legal.  That path still needs the same identity and facing reconciliation as the
        // placement-dialog path; the population counter alone cannot prove that the intended
        // operator occupied the requested tile or that its attack direction is usable.  Reuse
        // the settled full-frame OCR above; the previous duplicate full OCR added another
        // 0.9--1.2 seconds to every deployment without changing the page decision.
        auto settled = observe_page(texts, {});
        if (settled.page != Page::Placement && visual_placement && !settled.contradictory) {
            // Keep the normal OCR result for diagnostics, but promote this
            // frame to placement when the live green-cell surface is present.
            // The subsequent identity, direction and post-release checks are
            // still mandatory before consuming population.
            settled.page = Page::Placement;
        }
        LogInfo << "SPA deploy settled page" << static_cast<int>(settled.page) << "visual" << visual_placement
                << "contradictory" << settled.contradictory;
        if (settled.page == Page::Preparation && !settled.contradictory && settled.remaining_deployment &&
            *settled.remaining_deployment == m_remaining - 1) {
            m_population_consumed = true;
            m_remaining = *settled.remaining_deployment;
            if (!m_best_effort_deployment && (!reconcile_deployed_target() || !close_selection())) {
                save_evidence(ctrler()->get_image(), utils::path("debug/SPA/deployment_identity_unverified"), false);
                LogError << "SPA direct deployment changed population but target identity was not verified";
                return false;
            }
            if (m_best_effort_deployment && !wildcard_fast_deploy && !close_selection()) {
                return false;
            }
            const auto facing = deployed_facing(ctrler()->get_image(), cv::Point(m_target.x, m_target.y));
            const char expected = m_direction.x > 0 ? 'E' : m_direction.x < 0 ? 'W' : m_direction.y > 0 ? 'S' : 'N';
            if (facing != expected) {
                save_evidence(
                    ctrler()->get_image(),
                    utils::path("debug/SPA/direct_deployment_facing_unverified"),
                    false);
                LogWarn
                    << "SPA direct deployment facing marker was not visually readable; using committed direction input"
                    << expected;
            }
            auto info = basic_info_with_what("SPADeploymentVerified");
            info["details"] = json::object { { "name", m_name },
                                             { "round", m_round },
                                             { "remaining_deployment", m_remaining },
                                             { "target_identity_verified", !m_best_effort_deployment },
                                             { "population_consumed", true },
                                             { "direction_input_issued", false },
                                             { "direction_verified", true } };
            info["details"]["displaced"] = m_displaced_name;
            callback(AsstMsg::SubTaskExtraInfo, info);
            return true;
        }
        if (settled.page != Page::Placement || !current_identity(m_name)) {
            save_evidence(ctrler()->get_image(), utils::path("debug/SPA/uncertain_deployment"), false);
            LogError << "SPA deploy did not consume population and did not reach a placement state";
            return false;
        }
        auto info = basic_info_with_what("SPAPlacementPending");
        info["details"] = json::object { { "name", m_name },
                                         { "round", m_round },
                                         { "deployment_verified", false },
                                         { "game_placement_accepted", true },
                                         { "green_hint_verified", m_deployment_hint_verified },
                                         { "direction_required", true } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        // The placement drag has already opened the live direction diamond.
        // The B-server confirms the facing with a short drag from the
        // diamond centre toward the requested direction; a tap on the
        // visible arrow is only a visual cue and does not commit placement.
        const auto placement_image = ctrler()->get_image();
        std::optional<cv::Point> center;
        if (has_replacement_preview) {
            center = replacement_direction_center(
                placement_image,
                m_held_board,
                cv::Point(m_drag_target.x, m_drag_target.y));
        }
        else if (!m_replace_only) {
            center = direction_selection_center(placement_image);
        }
        if (!center) {
            save_evidence(placement_image, utils::path("debug/SPA/deploy_direction_diamond_missing"), false);
            return false;
        }
        if (!m_replace_only && !m_held_board.empty()) {
            // Elevated tile faces can pass the green-mask test but hit the
            // occupied tile above them. The game diamond identifies the actual
            // receiver before facing confirmation commits that unintended swap.
            const auto foot =
                registered_deployment_pose(placement_image, m_held_board, cv::Point(center->x, center->y + 20));
            if (foot) {
                m_settled_held_target = Point(foot->x, foot->y);
                const auto occupied =
                    std::find_if(m_placement_members.begin(), m_placement_members.end(), [&](const auto& member) {
                        return held_cell_has_member(foot->x, foot->y, { member });
                    });
                if (occupied != m_placement_members.end()) {
                    const auto cancel = direction_cancel_target(placement_image, *center);
                    bool cancelled = cancel && ctrler()->click(Point(cancel->x, cancel->y)) && sleep(350) &&
                                     close_selection() && ensure_collapsed_shop() && preparation_matches();
                    if (cancelled) {
                        const auto field = ctrler()->get_image();
                        const auto previous =
                            registered_deployment_pose(m_held_board, field, cv::Point(occupied->x, occupied->y));
                        cancelled = previous && selected_identity(Point(previous->x, previous->y), occupied->name) &&
                                    close_selection() && preparation_matches();
                    }
                    if (cancelled) {
                        m_rejected_drop_target = m_drag_target;
                    }
                    auto collision = basic_info_with_what("SPAOccupiedDropCancelled");
                    collision["details"] = json::object { { "name", m_name },
                                                          { "outgoing", occupied->name },
                                                          { "round", m_round },
                                                          { "input_x", m_drag_target.x },
                                                          { "input_y", m_drag_target.y },
                                                          { "receiver_x", foot->x },
                                                          { "receiver_y", foot->y },
                                                          { "cancelled_and_original_identity_verified", cancelled },
                                                          { "population_preserved", cancelled },
                                                          { "retry_another_cell", cancelled },
                                                          { "deployed", false } };
                    callback(AsstMsg::SubTaskExtraInfo, collision);
                    return false;
                }
            }
        }
        auto direction_info = basic_info_with_what("SPADirectionDragInputIssued");
        direction_info["details"] = json::object { { "name", m_name },
                                                   { "round", m_round },
                                                   { "center_x", center->x },
                                                   { "center_y", center->y },
                                                   { "drag_target_x", std::clamp(center->x + m_direction.x, 0, 1279) },
                                                   { "drag_target_y", std::clamp(center->y + m_direction.y, 0, 719) },
                                                   { "direction",
                                                     m_direction.x > 0   ? "E"
                                                     : m_direction.x < 0 ? "W"
                                                     : m_direction.y > 0 ? "S"
                                                                         : "N" } };
        callback(AsstMsg::SubTaskExtraInfo, direction_info);
        save_evidence(placement_image, utils::path("debug/SPA/deploy_direction_diamond"), false);
        if (!drag_direction_only(Point(center->x, center->y))) {
            LogError << "SPA deployment direction drag failed" << center->x << center->y;
            return false;
        }
        // The placement animation can take several seconds on the B-server;
        // polling the authoritative preparation counters prevents a transient
        // "点击取消" frame from being reported as a deployment failure.  A
        // tile occupied by an existing operator performs a swap and keeps the
        // population counter unchanged; a free tile consumes one slot.
        const int remaining_before = m_remaining;
        const bool joint_new_unit = m_joint_deployment && !m_replace_only && !m_preserve_population;
        const auto expected_post_drop_count = [remaining_before, joint_new_unit](int count) {
            return count == remaining_before - 1 || (!joint_new_unit && count == remaining_before);
        };
        std::optional<int> observed_remaining;
        cv::Mat settled_preparation;
        for (int attempt = 0; attempt < (joint_new_unit ? 4 : 12) && settled_preparation.empty(); ++attempt) {
            if (attempt > 0 && !sleep(m_fast_mode ? 160 : 450)) {
                return false;
            }
            const auto reconciliation_image = ctrler()->get_image();
            if (m_fast_mode) {
                // The compact HUD counter is the authoritative population
                // signal. Read the ready label from its top HUD ROI; the
                // previous bottom ROI could never see y=103 and forced a
                // full-screen OCR on every successful card.
                OCRer hud(reconciliation_image, Rect(1090, 80, 190, 430));
                bool ready = false;
                std::vector<TextObservation> hud_texts;
                if (const auto recognized_hud = hud.analyze()) {
                    for (const auto& text : *recognized_hud) {
                        hud_texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
                        if (text.score >= 0.85 && text.text == "准备就绪") {
                            ready = true;
                        }
                    }
                }
                const auto badge = remaining_deployment_number(reconciliation_image, hud_texts);
                if (ready && badge && expected_post_drop_count(*badge) &&
                    (!joint_new_unit || preparation_round_number(reconciliation_image, m_round) == m_round)) {
                    observed_remaining = *badge;
                    settled_preparation = reconciliation_image;
                    break;
                }
            }
            OCRer preparation_ocr(reconciliation_image);
            std::vector<TextObservation> preparation_texts;
            if (const auto preparation_recognized = preparation_ocr.analyze()) {
                for (const auto& text : *preparation_recognized) {
                    preparation_texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
                }
            }
            const auto page = observe_page(preparation_texts, {});
            const auto count = remaining_deployment_number(reconciliation_image, preparation_texts);
            if (joint_new_unit && (page.page == Page::Battle || page.page == Page::Result)) {
                break;
            }
            if (page.page == Page::Preparation && count && expected_post_drop_count(*count) &&
                (!joint_new_unit || preparation_round_number(reconciliation_image, m_round) == m_round)) {
                observed_remaining = *count;
                settled_preparation = reconciliation_image;
            }
        }
        if (settled_preparation.empty() || !observed_remaining) {
            save_evidence(ctrler()->get_image(), utils::path("debug/SPA/deployment_reconciliation"), false);
            return false;
        }
        m_population_consumed = *observed_remaining == remaining_before - 1;
        m_remaining = *observed_remaining;
        observe_deployed_pose(settled_preparation);
        if (m_replace_only) {
            m_replacement_committed = true;
            if (m_population_consumed || m_remaining != remaining_before) {
                return false;
            }
            m_replacement_postconditions_ready = true;
            return reconcile_replacement();
        }
        if (m_best_effort_deployment) {
            if (!m_population_consumed) {
                auto unresolved = basic_info_with_what("SPADeploymentReconciliationRequired");
                unresolved["details"] = json::object { { "name", m_name },
                                                       { "round", m_round },
                                                       { "remaining_deployment", m_remaining },
                                                       { "population_consumed", false },
                                                       { "field_identity_verified", m_field_pose_verified },
                                                       { "deployed", false },
                                                       { "resumable", true } };
                callback(AsstMsg::SubTaskExtraInfo, unresolved);
                return false;
            }
            // A verified pose already closed and checked the selection panel.
            // Retry the cleanup only if pose verification did not finish it.
            if (!wildcard_fast_deploy && !m_field_pose_verified && !close_selection()) {
                save_evidence(ctrler()->get_image(), utils::path("debug/SPA/deployment_reconciliation"), false);
                return false;
            }
            info = basic_info_with_what("SPADeploymentVerified");
            info["details"] =
                json::object { { "name", m_name },
                               { "round", m_round },
                               { "remaining_deployment", m_remaining },
                               { "target_identity_verified", m_field_pose_verified },
                               { "field_pose_verified", m_field_pose_verified },
                               { "population_consumed", m_population_consumed },
                               { "direction_input_issued", true },
                               { "direction_verified", !joint_new_unit || m_direction_committed },
                               { "validation",
                                 m_field_pose_verified ? "registered_field_identity"
                                 : joint_new_unit ? "same_round_slot_count"
                                                  : "page_and_direction_state" } };
            info["details"]["displaced"] = m_displaced_name;
            callback(AsstMsg::SubTaskExtraInfo, info);
            return true;
        }
        if (!reconcile_deployed_target() || !close_selection()) {
            const auto unsettled = ctrler()->get_image();
            save_evidence(unsettled, utils::path("debug/SPA/deployment_reconciliation"), false);
            cancel_direction_modal(unsettled);
            return false;
        }
        const auto facing = deployed_facing(ctrler()->get_image(), cv::Point(m_target.x, m_target.y));
        const char expected = m_direction.x > 0 ? 'E' : m_direction.x < 0 ? 'W' : m_direction.y > 0 ? 'S' : 'N';
        if (facing != expected) {
            save_evidence(ctrler()->get_image(), utils::path("debug/SPA/deployment_facing_unverified"), false);
            LogWarn << "SPA deployment facing marker was not visually readable; using committed direction input"
                    << expected;
        }
        info = basic_info_with_what("SPADeploymentVerified");
        info["details"] = json::object { { "name", m_name },
                                         { "round", m_round },
                                         { "remaining_deployment", m_remaining },
                                         { "target_identity_verified", true },
                                         { "population_consumed", m_population_consumed },
                                         { "direction_input_issued", true },
                                         { "direction_verified", true } };
        info["details"]["displaced"] = m_displaced_name;
        callback(AsstMsg::SubTaskExtraInfo, info);
        return true;
    }
    if (m_move_only) {
        if (!close_selection() || !held_drag() || !sleep(1200)) {
            return false;
        }
        OCRer placement(ctrler()->get_image());
        std::vector<TextObservation> texts;
        if (const auto recognized = placement.analyze()) {
            for (const auto& text : *recognized) {
                texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
            }
        }
        if (observe_page(texts, {}).page == Page::Placement && current_identity(m_name)) {
            auto pending = basic_info_with_what("SPAMoveDirectionPending");
            pending["details"] = json::object { { "name", m_name },
                                                { "round", m_round },
                                                { "remaining_deployment", m_remaining },
                                                { "target_identity_verified", false },
                                                { "direction_required", true } };
            callback(AsstMsg::SubTaskExtraInfo, pending);
            if (!m_finalize_direction) {
                return true;
            }
            const auto center = direction_selection_center(ctrler()->get_image());
            if (!center || !select_direction(Point(center->x, center->y)) || !sleep(1200)) {
                return false;
            }
        }
        if (!restore_preparation() || !selected_identity(m_target, m_name) || !close_selection()) {
            return false;
        }
        auto moved = basic_info_with_what("SPAMoveVerified");
        moved["details"] = json::object { { "name", m_name },
                                          { "round", m_round },
                                          { "remaining_deployment", m_remaining },
                                          { "target_identity_verified", true },
                                          { "direction_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, moved);
        return true;
    }
    if (m_find_equipment_source) {
        m_equipment_stage = "recipient_identity";
    }
    cv::Mat clean_equipment_frame;
    cv::Mat collapsed_equipment_frame;
    if (!close_selection(&clean_equipment_frame, verified_equipment_source_panel) ||
        (m_find_equipment_source &&
         !ensure_shop_visibility(false, &collapsed_equipment_frame, clean_equipment_frame))) {
        return false;
    }
    if (m_find_equipment_source) {
        m_equipment_reference =
            (collapsed_equipment_frame.empty() ? ctrler()->get_image() : collapsed_equipment_frame).clone();
        save_evidence(m_equipment_reference, utils::path("debug/SPA/equipment_reference"), false);
    }
    if (!selected_identity(m_target, m_replaced)) {
        return false;
    }
    if (m_promotion_consumable) {
        const auto card = selected_operator_card(ctrler()->get_image());
        if (!card || card->name != m_replaced || card->elite) {
            close_selection();
            return false;
        }
    }
    if (m_find_equipment_source) {
        m_equipment_stage = "existing_equipment";
        // More than the base capacity requires live form evidence. A stale
        // elite flag must not make an ordinary prototype lose existing gear.
        if (equipment_capacity(m_replaced, m_target_elite) > equipment_capacity(m_replaced, false)) {
            const auto card = selected_operator_card(ctrler()->get_image());
            if (!card || card->name != m_replaced || card->elite != m_target_elite) {
                close_selection();
                return false;
            }
        }
        auto expected = m_expected_equipment;
        std::sort(expected.begin(), expected.end());
        const auto observed = selected_equipment_names(expected.size());
        save_evidence(ctrler()->get_image(), utils::path("debug/SPA/equipment_before"), false);
        if (!observed || *observed != expected) {
            m_equipment_slots_mismatch = true;
            close_selection();
            return false;
        }
    }
    if (!close_selection()) {
        return false;
    }
    if (m_equip_only) {
        if (m_find_equipment_source) {
            m_equipment_stage = "source_reacquisition";
            // Target inspection pans the board. Restore the inventory camera
            // and find the same item slot again before beginning the contact.
            if (!ensure_expanded_shop() || !m_equipment_source_index) {
                return false;
            }
            if (is_immediate_cash_equipment(m_name)) {
                if (!reacquire_cash_equipment_source(m_source)) {
                    return false;
                }
            }
            else {
                const auto sources = equipment_inventory_candidates(ctrler()->get_image());
                const auto source = stable_equipment_source({ m_source.x, m_source.y }, sources);
                if (!source) {
                    return false;
                }
                m_source = Point(sources[*source].x, sources[*source].y);
                m_equipment_source_index = *source;
            }
        }
        const bool id_tag = m_name == "随身身份牌";
        std::vector<cv::Point> delayed_cash_inventory;
        std::vector<cv::Point> promotion_consumable_inventory;
        if (m_promotion_consumable) {
            // Do not reuse a source index or assume every tier-II phone is
            // normal quality. Both the live name and complete effect matter.
            m_equipment_stage = "promotion_consumable_source_recheck";
            const auto title = bench_identity(m_source);
            if (!title || !equipment_title_matches(m_name, *title)) {
                return false;
            }
            const auto effect_image = ctrler()->get_image();
            save_evidence(effect_image, utils::path("debug/SPA/promotion_consumable_quote"), false);
            OCRer effect(effect_image, Rect(0, 350, 450, 350));
            std::string description;
            if (const auto texts = effect.analyze()) {
                for (const auto& text : *texts) {
                    if (text.score >= .85) {
                        description += text.text;
                    }
                }
            }
            m_promotion_consumable_quote = promotion_consumable_quote(m_name, description);
            if (!m_promotion_consumable_quote ||
                (m_direct_projection &&
                 m_promotion_consumable_quote->effect != PromotionConsumableEffect::DirectPromotion) ||
                (m_direct_projection && !m_allow_delayed_projection &&
                 m_promotion_consumable_quote->timing == PromotionConsumableTiming::NextRoundStart) ||
                (m_promotion_consumable_quote->effect == PromotionConsumableEffect::FirstPactRecruitment &&
                 m_consumable_free_bench_slots < m_promotion_consumable_quote->initial_grants) ||
                !close_selection() || !ensure_expanded_shop()) {
                return false;
            }
            promotion_consumable_inventory = equipment_inventory_candidates(ctrler()->get_image());
            const auto same_source = std::find_if(
                promotion_consumable_inventory.begin(),
                promotion_consumable_inventory.end(),
                [&](const auto& point) { return cv::norm(point - cv::Point(m_source.x, m_source.y)) < 15; });
            const auto local_sources = std::count_if(
                promotion_consumable_inventory.begin(),
                promotion_consumable_inventory.end(),
                [&](const auto& point) { return cv::norm(point - cv::Point(m_source.x, m_source.y)) < 15; });
            if (same_source == promotion_consumable_inventory.end() || local_sources != 1) {
                return false;
            }
            m_source = Point(same_source->x, same_source->y);
            auto pending = basic_info_with_what("SPAPromotionConsumableContactPending");
            pending["details"] =
                json::object { { "equipment", m_name },
                               { "operator", m_replaced },
                               { "round", m_round },
                               { "initial_grants_quote", m_promotion_consumable_quote->initial_grants },
                               { "effect_timing",
                                 m_promotion_consumable_quote->timing == PromotionConsumableTiming::NextRoundStart
                                     ? "next_round_start"
                                     : "on_equip" },
                               { "recipient_initial_form_observed", true },
                               { "source_x", m_source.x },
                               { "source_y", m_source.y },
                               { "input_committed", false },
                               { "roster_change_verified", false },
                               { "equipment_replay_allowed", false } };
            callback(AsstMsg::SubTaskExtraInfo, pending);
        }
        if (m_find_equipment_source && is_deferred_cash_equipment(m_name)) {
            // Recipient inspection can pan the board. Re-read this source
            // after inventory reacquisition, rather than authorizing a
            // destructive contact from an old index alone.
            m_equipment_stage = "delayed_cash_source_recheck";
            const auto title = bench_identity(m_source);
            if (!title || !equipment_title_matches(m_name, *title)) {
                return false;
            }
            OCRer effect(ctrler()->get_image(), Rect(0, 350, 450, 350));
            std::string description;
            if (const auto texts = effect.analyze()) {
                for (const auto& text : *texts) {
                    if (text.score >= .85) {
                        description += text.text;
                    }
                }
            }
            const auto quote = equipment_next_round_cash_gain(m_name, description);
            if (!quote || !close_selection() || !ensure_expanded_shop()) {
                return false;
            }
            m_equipment_next_round_credit_quote = *quote;
            delayed_cash_inventory = equipment_inventory_candidates(ctrler()->get_image());
            const auto reacquired = stable_equipment_source({ m_source.x, m_source.y }, delayed_cash_inventory);
            if (!reacquired) {
                return false;
            }
            const auto point = delayed_cash_inventory[*reacquired];
            m_source = Point(point.x, point.y);
        }
        std::optional<int> defense_before;
        std::optional<int> unyielding_before;
        std::optional<int> pact_before;
        std::optional<int> tag_quote;
        std::vector<cv::Point> tag_inventory;
        if (id_tag && !m_identity_tag_pact.empty()) {
            m_equipment_stage = "identity_tag_source_recheck";
            const auto title = bench_identity(m_source);
            if (!title || !equipment_title_matches(m_name, *title)) {
                return false;
            }
            std::string description;
            OCRer effects(ctrler()->get_image(), Rect(0, 350, 450, 350));
            if (const auto texts = effects.analyze()) {
                for (const auto& text : *texts) {
                    if (text.score >= .85) {
                        description += text.text;
                    }
                }
            }
            tag_quote = identity_tag_growth_quote(description);
            if (!tag_quote || !close_selection() || !ensure_expanded_shop()) {
                return false;
            }
            const auto before = ctrler()->get_image();
            tag_inventory = equipment_inventory_candidates(before);
            const auto source_matches = [&](const auto& point) {
                return cv::norm(point - cv::Point(m_source.x, m_source.y)) < 15;
            };
            const auto slot = std::find_if(tag_inventory.begin(), tag_inventory.end(), source_matches);
            pact_before = pact_layers(before, m_identity_tag_pact);
            if (slot == tag_inventory.end() ||
                std::count_if(tag_inventory.begin(), tag_inventory.end(), source_matches) != 1 || !pact_before ||
                *pact_before < 0 || *pact_before + *tag_quote > 999) {
                return false;
            }
            m_source = Point(slot->x, slot->y);
        }
        else if (id_tag) {
            // This first consumable verification covers the reviewed dual-pact target.
            if (m_replaced != "摆渡人") {
                return false;
            }
            defense_before = pact_layers(ctrler()->get_image(), "坚守");
            unyielding_before = pact_layers(ctrler()->get_image(), "不屈");
            if (!defense_before || !unyielding_before || *defense_before > 996 || *unyielding_before > 996) {
                return false;
            }
        }
        if (m_find_equipment_source) {
            m_equipment_stage = "held_target";
        }
        if (m_find_equipment_source && is_immediate_cash_equipment(m_name)) {
            // The source title/effect and recipient identity were rechecked
            // above. Only this one subsequent contact may explain the quote.
            m_cash_input_chain_verified = true;
        }
        if (!held_drag()) {
            return false;
        }
        m_equipment_committed = true;
        if (m_find_equipment_source) {
            m_equipment_stage = "effect_verification";
        }
        if (!sleep(1200)) {
            return false;
        }
        if (m_promotion_consumable) {
            // A phone grants bodies directly; it is not the pager's free
            // choice shop. Leave any synthesis reward overlay untouched for
            // the parent's complete physical-inventory reconciliation.
            const auto after = ctrler()->get_image();
            const bool source_slot_removed = inventory_slot_removal_observed(
                promotion_consumable_inventory,
                equipment_inventory_candidates(after),
                { m_source.x, m_source.y });
            const auto& quote = *m_promotion_consumable_quote;
            // A normal projection leaves the inventory because it is now
            // equipped. That is not evidence it has been destroyed yet.
            m_equipment_consumption_verified =
                quote.timing == PromotionConsumableTiming::OnEquip && source_slot_removed;
            const bool direct_promotion = quote.effect == PromotionConsumableEffect::DirectPromotion;
            if (direct_promotion && restore_preparation() && close_selection() && ensure_collapsed_shop() &&
                selected_identity(m_target, m_replaced)) {
                const auto card = selected_operator_card(ctrler()->get_image());
                if (card && card->name == m_replaced) {
                    m_recipient_elite_transition_observed =
                        quote.timing == PromotionConsumableTiming::OnEquip && card->elite;
                    if (quote.timing == PromotionConsumableTiming::NextRoundStart && !card->elite) {
                        auto expected = m_expected_equipment;
                        expected.push_back(m_name);
                        std::sort(expected.begin(), expected.end());
                        const auto observed = selected_equipment_names(expected.size());
                        m_projection_attached_verified = observed && *observed == expected;
                    }
                }
                close_selection();
            }
            m_equipment_stage = "physical_inventory_reconciliation_pending";
            auto receipt = basic_info_with_what("SPAPromotionConsumableObserved");
            receipt["details"] = json::object {
                { "equipment", m_name },
                { "operator", m_replaced },
                { "round", m_round },
                { "input_committed", true },
                { "inventory_source_slot_removed", source_slot_removed },
                { "consumption_verified", m_equipment_consumption_verified },
                { "initial_grants_quote", quote.initial_grants },
                { "effect_timing",
                  quote.timing == PromotionConsumableTiming::NextRoundStart ? "next_round_start" : "on_equip" },
                { "projection_attached_verified", m_projection_attached_verified },
                { "recipient_elite_transition_observed", m_recipient_elite_transition_observed },
                { "roster_change_verified", false },
                { "first_pact_verified", false },
                { "inventory_complete", false },
                { "promotion_reward_verified", false },
                { "equipment_effect_pending", true },
                { "equipment_replay_allowed", false }
            };
            callback(AsstMsg::SubTaskExtraInfo, receipt);
            save_evidence(ctrler()->get_image(), utils::path("debug/SPA/promotion_consumable_pending"), false);
            // Success means that a bounded observation completed, not that
            // the parent may add copies or mark the recipient as elite.
            return m_equipment_consumption_verified || m_projection_attached_verified ||
                   m_recipient_elite_transition_observed;
        }
        if (m_find_equipment_source && is_deferred_cash_equipment(m_name)) {
            // A deferred quote is not an immediate cash receipt. Never add it
            // to this round's wallet or replay an uncertain consumed source.
            if (!restore_preparation() || !ensure_expanded_shop()) {
                return false;
            }
            const auto after = ctrler()->get_image();
            m_equipment_consumption_verified = inventory_slot_removal_observed(
                delayed_cash_inventory,
                equipment_inventory_candidates(after),
                { m_source.x, m_source.y });
            m_equipment_stage =
                m_equipment_consumption_verified ? "next_round_credit_pending" : "consumption_unverified";
            auto receipt = basic_info_with_what("SPADelayedCashConsumableObserved");
            receipt["details"] = json::object { { "equipment", m_name },
                                                { "operator", m_replaced },
                                                { "round", m_round },
                                                { "funds", m_funds },
                                                { "funds_unchanged", true },
                                                { "remaining_deployment", m_remaining },
                                                { "input_committed", true },
                                                { "consumption_verified", m_equipment_consumption_verified },
                                                { "next_round_credit_quote", m_equipment_next_round_credit_quote },
                                                { "next_round_credit_verified", false },
                                                { "equipment_replay_allowed", false } };
            callback(AsstMsg::SubTaskExtraInfo, receipt);
            save_evidence(after, utils::path("debug/SPA/delayed_cash_consumption"), false);
            return m_equipment_consumption_verified;
        }
        if (m_find_equipment_source && m_name == "寻呼模块" && !m_pact_recruitment_pact.empty()) {
            // A consumed pager creates a temporary free shop. Do not inspect
            // it as persistent gear or issue the contact again on a delayed
            // animation. The parent selects and reconciles the actual card.
            for (int attempt = 0; attempt < 4; ++attempt) {
                const auto offered = ctrler()->get_image();
                OCRer offer_ocr(offered, Rect(680, 625, 440, 75));
                std::set<std::string> names;
                if (const auto texts = offer_ocr.analyze()) {
                    for (const auto& text : *texts) {
                        if (text.score < .85 || text.rect.y < 625 || text.rect.y > 700 ||
                            !pact_recruitment_offer_eligible(
                                text.text,
                                m_pact_recruitment_pact,
                                m_pact_recruitment_shop_level)) {
                            continue;
                        }
                        const int slot = text.rect.x >= 1000 ? 0 : text.rect.x >= 840 ? 1 : 2;
                        if (shop_number(offered, Rect(1038 - slot * 162, 510, 24, 30)) == 0) {
                            names.insert(text.text);
                        }
                    }
                }
                if (!names.empty() && preparation_matches()) {
                    m_equipment_stage = "recruitment_offer_verified";
                    auto receipt = basic_info_with_what("SPAPactRecruitmentOfferVerified");
                    receipt["details"] =
                        json::object { { "equipment", m_name },
                                       { "operator", m_replaced },
                                       { "pact", m_pact_recruitment_pact },
                                       { "round", m_round },
                                       { "shop_level", m_pact_recruitment_shop_level },
                                       { "free_candidates", std::vector<std::string>(names.begin(), names.end()) },
                                       { "recruited_operator_verified", false },
                                       { "equipment_replay_allowed", false } };
                    callback(AsstMsg::SubTaskExtraInfo, receipt);
                    save_evidence(offered, utils::path("debug/SPA/pact_recruitment_offer"), false);
                    return true;
                }
                if (attempt < 3 && !sleep(250)) {
                    return false;
                }
            }
            save_evidence(ctrler()->get_image(), utils::path("debug/SPA/pact_recruitment_unverified"), false);
            return false;
        }
        if (m_find_equipment_source && is_immediate_cash_equipment(m_name)) {
            // Cash changes the HUD funds, so the old preparation snapshot
            // cannot be matched until the new wallet has been verified.
            if (!ensure_expanded_shop()) {
                return false;
            }
            const auto after_frame = ctrler()->get_image();
            m_equipment_consumption_verified = inventory_slot_removal_observed(
                m_cash_source_inventory,
                equipment_inventory_candidates(after_frame),
                { m_source.x, m_source.y });
            const auto after = shop_number(after_frame, Rect(1180, 510, 45, 40));
            const auto observed_round = preparation_round_number(after_frame, m_round);
            // This immediate wallet read precedes all other transactions.
            // Cash credit is distinct from visually observing item removal;
            // unrelated animated badges must not veto an exact cash receipt.
            if (!after || !observed_round ||
                !cash_effect_verified(
                    m_equipment_contact_attempted,
                    m_cash_input_chain_verified,
                    m_round,
                    *observed_round,
                    m_funds,
                    m_equipment_cash_credit,
                    *after,
                    m_name)) {
                save_evidence(ctrler()->get_image(), utils::path("debug/SPA/cash_equipment_unreconciled"), false);
                return false;
            }
            const int before = m_funds;
            m_funds = *after;
            if (!restore_preparation()) {
                return false;
            }
            auto info = basic_info_with_what("SPACashEquipmentEffectVerified");
            info["details"] = json::object { { "equipment", m_name },
                                             { "operator", m_replaced },
                                             { "round", m_round },
                                             { "funds_before", before },
                                             { "funds_after", *after },
                                             { "cash_gain", m_equipment_cash_credit },
                                             { "remaining_deployment", m_remaining },
                                             { "inventory_complete", false },
                                             { "contact_release_attempted", m_equipment_contact_attempted },
                                             { "source_and_target_verified", m_cash_input_chain_verified },
                                             { "inventory_removal_observed", m_equipment_consumption_verified },
                                             { "cash_effect_verified", true },
                                             { "effect_verified", true } };
            callback(AsstMsg::SubTaskExtraInfo, info);
            m_equipment_stage = "verified";
            return true;
        }
        if (id_tag) {
            auto info = basic_info_with_what("SPAConsumableInputIssued");
            info["details"] =
                json::object { { "equipment", m_name }, { "operator", m_replaced }, { "effect_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, info);
            if (!restore_preparation()) {
                return false;
            }
            if (pact_before && tag_quote) {
                if (!ensure_expanded_shop()) {
                    return false;
                }
                const auto after = ctrler()->get_image();
                const auto layers = pact_layers(after, m_identity_tag_pact);
                m_equipment_consumption_verified = inventory_slot_removal_observed(
                    tag_inventory,
                    equipment_inventory_candidates(after),
                    { m_source.x, m_source.y });
                const bool verified = layers && *layers == *pact_before + *tag_quote;
                info =
                    basic_info_with_what(verified ? "SPAConsumableEffectVerified" : "SPAConsumableEffectUnconfirmed");
                info["details"] = json::object { { "equipment", m_name },
                                                 { "operator", m_replaced },
                                                 { "round", m_round },
                                                 { "pact", m_identity_tag_pact },
                                                 { "layers_before", *pact_before },
                                                 { "layers_after", layers.value_or(-1) },
                                                 { "quoted_gain", *tag_quote },
                                                 { "inventory_consumption_verified", m_equipment_consumption_verified },
                                                 { "effect_verified", verified } };
                callback(AsstMsg::SubTaskExtraInfo, info);
                m_equipment_stage = verified ? "verified" : "layer_delta_unverified";
                if (!verified) {
                    save_evidence(after, utils::path("debug/SPA/consumable_effect_unverified"), false);
                }
                return verified;
            }
            const auto defense_after = pact_layers(ctrler()->get_image(), "坚守");
            const auto unyielding_after = pact_layers(ctrler()->get_image(), "不屈");
            if (!defense_after || !unyielding_after || *defense_after != *defense_before + 3 ||
                *unyielding_after != *unyielding_before + 3) {
                save_evidence(ctrler()->get_image(), utils::path("debug/SPA/consumable_effect_unverified"), false);
                return false;
            }
            info = basic_info_with_what("SPAConsumableEffectVerified");
            info["details"] = json::object { { "equipment", m_name },
                                             { "operator", m_replaced },
                                             { "round", m_round },
                                             { "effect_verified", true },
                                             { "defense_before", *defense_before },
                                             { "defense_after", *defense_after },
                                             { "unyielding_before", *unyielding_before },
                                             { "unyielding_after", *unyielding_after },
                                             { "inventory_consumption_verified", false } };
            callback(AsstMsg::SubTaskExtraInfo, info);
            return true;
        }
        if (m_find_equipment_source) {
            m_equipment_stage = "equipped_identity";
        }
        if ((m_find_equipment_source && (!close_selection() || !ensure_collapsed_shop())) || !restore_preparation() ||
            !selected_identity(m_target, m_replaced) || (!m_find_equipment_source && !current_equipment(m_name))) {
            save_evidence(ctrler()->get_image(), utils::path("debug/SPA/gear_inspection_required"), false);
            return false;
        }
        if (m_find_equipment_source) {
            m_equipment_stage = "complete_equipment_list";
            auto expected = m_expected_equipment;
            expected.push_back(m_name);
            std::sort(expected.begin(), expected.end());
            const auto observed = selected_equipment_names(expected.size());
            if (!observed || *observed != expected) {
                m_equipment_slots_mismatch = true;
                save_evidence(ctrler()->get_image(), utils::path("debug/SPA/equipment_slots_unreconciled"), false);
                return false;
            }
        }
        save_evidence(ctrler()->get_image(), utils::path("debug/SPA/equipment_after"), false);
        m_equipment_stage = "verified";
        auto info = basic_info_with_what("SPAGearEquippedVerified");
        info["details"] =
            json::object { { "equipment", m_name },
                           { "operator", m_replaced },
                           { "round", m_round },
                           { "equipped_verified", true },
                           { "source_panel_reused_before_recipient", !verified_equipment_source_panel.empty() } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        return close_selection();
    }
    save_evidence(ctrler()->get_image(), utils::path("debug/SPA/before_swap"), true);
    if (!held_drag() || !sleep(1200)) {
        return false;
    }
    OCRer placement(ctrler()->get_image());
    if (const auto recognized = placement.analyze()) {
        std::vector<TextObservation> texts;
        for (const auto& text : *recognized) {
            texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
        }
        if (observe_page(texts, {}).page == Page::Placement && current_identity(m_name)) {
            auto info = basic_info_with_what("SPASwapDirectionPending");
            info["details"] = json::object { { "name", m_name },         { "replaced", m_replaced },
                                             { "round", m_round },       { "remaining_deployment", m_remaining },
                                             { "source_x", m_source.x }, { "source_y", m_source.y },
                                             { "target_x", m_target.x }, { "target_y", m_target.y },
                                             { "swap_verified", false }, { "direction_required", true } };
            callback(AsstMsg::SubTaskExtraInfo, info);
            return true;
        }
    }
    if (!preparation_matches() || !selected_identity(m_target, m_name) || !close_selection() ||
        !selected_identity(m_source, m_replaced) || !close_selection()) {
        save_evidence(ctrler()->get_image(), utils::path("debug/SPA/uncertain_swap"), false);
        return false;
    }
    auto info = basic_info_with_what("SPASwapVerified");
    info["details"] = json::object { { "name", m_name },
                                     { "replaced", m_replaced },
                                     { "round", m_round },
                                     { "remaining_deployment", m_remaining },
                                     { "target_identity_verified", true } };
    info["details"]["source_identity_verified"] = true;
    callback(AsstMsg::SubTaskExtraInfo, info);
    return true;
}
