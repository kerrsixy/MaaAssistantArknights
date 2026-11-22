#pragma once
#include "MaaUtils/NoWarningCV.hpp"
#include "SPAPlacementPolicy.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace asst::spa
{
// Locate the complete white life counter after its unique cyan tower icon.
// Red pending damage and neighboring header ornaments are excluded. This
// provides an OCR crop only; absent or ambiguous evidence stays unknown.
std::optional<cv::Rect> defence_life_number_roi(const cv::Mat& image);
// Same expanded inventory frame: exactly the selected slot disappears and
// every other observed slot remains. Compaction or missing peers is unknown.
bool inventory_slot_removal_observed(
    const std::vector<cv::Point>& before,
    const std::vector<cv::Point>& after,
    cv::Point selected);

// Bench candidates are visual occupancy evidence only. The executor must
// verify the complete operator identity before transferring a card.
bool bench_operator_visible(const cv::Mat& image, cv::Point center);
bool bench_christine_sprite_visible(const cv::Mat& image, cv::Point center);
std::vector<cv::Point> bench_source_centres(const cv::Mat& image, bool include_christine = false);

struct InventoryOperatorHint
{
    std::string title;
    cv::Mat sprite;
};

// Reuse only a previously read operator title as a visual filtering hint for
// inventory discovery. This cannot establish a roster, form, item quantity,
// or authorize a transfer; the executor still reads every used item's name.
bool remember_inventory_operator_title(
    std::vector<InventoryOperatorHint>& hints,
    const cv::Mat& image,
    cv::Point source,
    const std::string& title,
    bool title_verified);
std::optional<std::pair<std::string, double>> inventory_operator_visual_hint(
    const std::vector<InventoryOperatorHint>& hints,
    const cv::Mat& image,
    cv::Point source);
// Compare the full anchored book glyph silhouette. Added II/VI strokes must
// reduce the score; matching a subregion containing one I is insufficient.
bool shop_book_one_glyph_matches(const cv::Mat& glyph, const cv::Mat& reference);

struct CameraProjection
{
    bool estimated = false;
    int matches = 0;
    int inliers = 0;
    double median_error = 0;
    cv::Mat matrix;
    std::vector<cv::Point2f> source_hull;
};

// Centers of the blue/cyan deployment-cell markers visible on the normal
// preparation map.  These are candidate screen points only: a placement is
// still authorized by the held-drag green hint and the post-placement
// identity/facing/population reconciliation.
struct DeploymentCellCandidate
{
    cv::Point center;
    double score = 0.0;
};

// A full field exposes occupied swap cells only, rather than the normal
// buildability mask. Align remembered held-camera poses by a small translation;
// this does not identify a map or establish enemy-route membership.
struct OccupiedPreviewRegistration
{
    bool verified = false;
    cv::Point shift;
    int matched_cells = 0;
    int total_cells = 0;
};

struct EquipmentPreviewEvidence
{
    bool verified = false;
    cv::Point target;
    int matches = 0;
    int inliers = 0;
    int added_green = 0;
    int left_green = 0;
    int right_green = 0;
    bool occlusion_assisted = false;
    bool blocking_device_assisted = false;
};

// The caller must separately establish the recipient's identity in `before`.
// A supported feature projection and newly green pixels on both sides of that
// same recipient provide independent evidence when its sprite hides the tile.
EquipmentPreviewEvidence equipment_target_preview(
    const cv::Mat& before,
    const cv::Mat& held,
    cv::Point recipient,
    const std::vector<cv::Point>& verified_field_feet = {},
    bool replacement_above_blocking_device = false);

bool occupied_swap_hint(const cv::Mat& image, cv::Point center);
// Name rectangle and pixels must belong to one immutable frame. A clipped
// icon or blank background is unknown, not evidence of a banned pact.
std::optional<bool> pact_icon_available(const cv::Mat& image, const cv::Rect& label, bool core);
// Candidate hitboxes from either inventory badge row with shop expanded.
// These require a selected item-name check before any drag/use.
// The lower right bench is occluded by freeze/refresh controls. A badge there
// does not make its derived sprite-foot coordinate an inventory input target.
bool overlaps_expanded_shop_controls(cv::Point point, int top_margin = 0);
bool equipment_inventory_probe_safe(cv::Point point);
std::vector<cv::Point> equipment_inventory_candidates(const cv::Mat& image);

struct RetreatControlMatch
{
    cv::Rect rect;
    double score = 0;
    double scale = 1;
};

// An observation aid; callers must verify the selected operator separately.
std::optional<RetreatControlMatch> selected_retreat_control(const cv::Mat& image, const cv::Mat& reference);
OccupiedPreviewRegistration align_occupied_preview(const cv::Mat& image, const std::vector<cv::Point>& tracked);

// Visual registration only: terrain plane, tile identity and legal placement need separate verification.
std::optional<cv::Point> direction_selection_center(const cv::Mat& image);
std::optional<cv::Point> direction_cancel_target(const cv::Mat& image, cv::Point center);
std::optional<cv::Point>
    replacement_direction_center(const cv::Mat& placement, const cv::Mat& held, cv::Point expected_target);
std::optional<char> deployed_facing(const cv::Mat& image, cv::Point foot);
// Estimate the visible fill of the top battle HUD. No readable fill is
// unknown, including during a defeat animation or when the HUD disappears.
std::optional<int> battle_boss_hp_bar_fill_percent(const cv::Mat& image);
// A single long boss HP bar in the enemy preparation area. This supplies a
// visible phase marker, not a boss identity or a guessed ground coordinate.
std::optional<cv::Rect> preparation_boss_health_bar(const cv::Mat& image);
// Recognized red diagonal hatching above a separately established boss bar.
// Frames with the direction-and-cancel overlay yield no region. Only visible
// red pixels are retained; occluded body pixels are not inferred.
std::optional<cv::Mat> boss_attackable_region(const cv::Mat& image, cv::Rect boss_bar);
std::vector<std::pair<int, int>> project_boss_attackable_cells(const cv::Mat& region, const cv::Mat& grid_to_screen);
// Estimate the five-by-three opening footprint from the visible lower boundary
// when the Boss model occludes the solid hatching. This is a planning cue.
std::vector<std::pair<int, int>>
    estimate_boss_opening_footprint(const cv::Mat& image, cv::Rect boss_bar, const cv::Mat& grid_to_screen);
bool active_strategy_ready(const cv::Mat& image);
bool green_deployment_hint(const cv::Mat& image, cv::Point center);
// Fallback for a separately registered, legal and vacant tile. A supported
// camera transform and newly added green overlay are required; existing dark
// green scenery alone is insufficient. Not an unknown-cell detector.
bool registered_added_green_preview(
    const cv::Mat& before,
    const cv::Mat& held,
    cv::Point held_target,
    const std::vector<cv::Point>& verified_field_feet = {});
bool occupied_deployment_cell(const cv::Mat& image, cv::Point center);
std::vector<DeploymentCellCandidate> detect_deployment_cells(const cv::Mat& image);
// Interior points measured from the current held-card legal overlay. They
// deliberately carry no logical map/tile identity until registration exists.
std::vector<DeploymentCellCandidate> detect_held_deployment_cells(const cv::Mat& image);
CameraProjection estimate_camera_projection(const cv::Mat& before, const cv::Mat& selected, bool board_view = false);
std::optional<cv::Point2f> project_supported_point(const CameraProjection& projection, cv::Point2f point);
// Held input coordinates and clean collapsed-map coordinates are different
// frames. This only registers the point; the executor must still read its name.
std::optional<cv::Point>
    registered_deployment_pose(const cv::Mat& held, const cv::Mat& collapsed, cv::Point held_target);
// The direction diamond supplies an approximate receiver hint. Record the
// registered tile center rather than treating that UI hint as a ground point.
std::optional<cv::Point> registered_deployment_cell_pose(
    const cv::Mat& held,
    const cv::Mat& collapsed,
    const cv::Mat& grid_to_held,
    cv::Point receiver_hint);
// A verified grid and the actual green tile provide independent location
// evidence when an empty tile has no ORB feature at its exact center.
std::optional<cv::Point> registered_green_grid_cell_pose(
    const cv::Mat& held,
    const cv::Mat& after,
    const cv::Mat& grid_to_held,
    std::pair<int, int> cell);
// A direction modal supplies an approximate receiver hint. The caller must
// separately verify the exact expected cell's occupied identity or green
// vacancy before using this grid-based result to authorize facing input.
std::optional<cv::Point> registered_grid_receiver_pose(
    const cv::Mat& placement,
    const cv::Mat& held,
    const cv::Mat& grid_to_held,
    std::pair<int, int> expected_cell);
// Preserve an entire known field through a camera change, or reject the
// whole mapping. No partially transformed roster is returned.
enum class FieldView
{
    Collapsed,
    Held
};
std::optional<std::vector<cv::Point>> registered_field_points(
    const cv::Mat& before,
    const cv::Mat& after,
    const std::vector<cv::Point>& points,
    FieldView view = FieldView::Collapsed);
std::optional<std::vector<cv::Point>> registered_field_points(
    const CameraProjection& fit,
    const std::vector<cv::Point>& points,
    FieldView view = FieldView::Collapsed);
// Only the actor already identified before being held may use its certified
// source grid cell when lifting it removes the source slot's visual features.
// Every other field point retains the ordinary supported-feature requirement.
std::optional<std::vector<cv::Point>> registered_held_field_points(
    const cv::Mat& before,
    const cv::Mat& held,
    const std::vector<cv::Point>& points,
    const cv::Mat& grid_to_before,
    size_t held_source,
    std::pair<int, int> source_cell);
std::optional<cv::Mat>
    registered_grid_projection(const cv::Mat& before, const cv::Mat& after, const cv::Mat& grid_to_before);
std::optional<cv::Mat> registered_grid_projection(const CameraProjection& fit, const cv::Mat& grid_to_before);
// Map previously verified field coordinates to logical cells. Identity and
// population completeness remain obligations of the caller.
std::optional<std::vector<GridPlacementMember>> registered_grid_members(
    const cv::Mat& grid_to_screen,
    const std::vector<PlacementMember>& members,
    FieldView view = FieldView::Collapsed);
}
