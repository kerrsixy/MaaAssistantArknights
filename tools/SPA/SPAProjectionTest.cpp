#include "Task/MiniGame/SPA/SPAProjection.h"
#include "Task/MiniGame/SPA/SPABoardGeometry.h"
#include "Task/MiniGame/SPA/SPAPlacementPolicy.h"
#include "Task/MiniGame/SPA/SPAPreparationEvidence.h"
#include "Task/MiniGame/SPA/SPAStrategyEligibility.h"
#include "Task/MiniGame/SPA/SPAStrategyReportScroll.h"
#include <cassert>
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#ifdef _WIN32
#include <crtdbg.h>
#include <cstdlib>
#endif

int main()
{
#ifdef _WIN32
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    using namespace asst::spa;
    const auto report_with_thumb = [](int y) {
        cv::Mat report(720, 1280, CV_8UC3, cv::Scalar(205, 207, 187));
        report(cv::Rect(1258, 109, 7, 580)).setTo(cv::Scalar(157, 158, 144));
        report(cv::Rect(1258, y, 7, 186)).setTo(cv::Scalar(74, 67, 51));
        return report;
    };
    const auto top_thumb = strategy_report_scrollbar(report_with_thumb(109));
    auto animated_report = report_with_thumb(189);
    animated_report(cv::Rect(565, 130, 670, 560)).setTo(cv::Scalar(90, 68, 44));
    const auto next_thumb = strategy_report_scrollbar(animated_report);
    assert(top_thumb && top_thumb->at_top() && !top_thumb->at_bottom());
    assert(next_thumb && next_thumb->top == 189 && strategy_report_scroll_moved(*top_thumb, *next_thumb, false));
    assert(!strategy_report_scroll_moved(*top_thumb, *next_thumb, true));
    assert(same_strategy_report_scrollbar(*next_thumb, *strategy_report_scrollbar(report_with_thumb(188))));
    assert(!same_strategy_report_scrollbar(*top_thumb, *next_thumb));
    // A selected border changed the row detections at scrollbar 342, but
    // the game stayed on the same page across the remaining scan budget.
    const auto middle_thumb = strategy_report_scrollbar(report_with_thumb(342));
    const std::vector<int> unselected_rows { 220, 356, 493 };
    const std::vector<int> selected_rows { 219, 493 };
    assert(!strategy_report_selection_shifted_viewport(middle_thumb, middle_thumb, unselected_rows, selected_rows));
    assert(strategy_report_selection_shifted_viewport(middle_thumb, next_thumb, unselected_rows, selected_rows));
    assert(!strategy_report_selection_shifted_viewport(std::nullopt, middle_thumb, unselected_rows, selected_rows));
    const std::vector<int> shifted_rows { 123, 259, 396, 533 };
    assert(strategy_report_selection_shifted_viewport(std::nullopt, middle_thumb, unselected_rows, shifted_rows));
    assert(!strategy_report_selection_shifted_viewport(std::nullopt, middle_thumb, unselected_rows, {}));
    assert(strategy_report_scroll_destination(*top_thumb, false) - top_thumb->center() < 93);
    const auto bottom_thumb = strategy_report_scrollbar(report_with_thumb(503));
    assert(bottom_thumb && bottom_thumb->at_bottom() && strategy_report_scroll_moved(*bottom_thumb, *top_thumb, true));
    assert(!strategy_report_scrollbar(cv::Mat(720, 1280, CV_8UC3, cv::Scalar(74, 67, 51))));
    auto ambiguous_report = report_with_thumb(109);
    ambiguous_report(cv::Rect(1258, 420, 7, 100)).setTo(cv::Scalar(74, 67, 51));
    assert(!strategy_report_scrollbar(ambiguous_report));
    const std::vector<cv::Point> inventory_before { { 500, 450 }, { 580, 450 }, { 660, 450 } };
    assert(inventory_slot_removal_observed(inventory_before, { { 580, 450 }, { 660, 450 } }, { 500, 450 }));
    assert(!inventory_slot_removal_observed(inventory_before, inventory_before, { 500, 450 }));
    // Losing another item, a compacted inventory, an unobserved source and
    // duplicate peers cannot certify consumption of the selected toy.
    assert(!inventory_slot_removal_observed(inventory_before, { { 500, 450 }, { 660, 450 } }, { 500, 450 }));
    assert(!inventory_slot_removal_observed(inventory_before, { { 500, 450 }, { 580, 450 } }, { 500, 450 }));
    assert(!inventory_slot_removal_observed(inventory_before, { { 580, 450 }, { 660, 450 } }, { 420, 450 }));
    assert(!inventory_slot_removal_observed(inventory_before, { { 580, 450 }, { 580, 450 } }, { 500, 450 }));
    const std::vector<cv::Point> boss_inventory_before {
        { 640, 393 }, { 718, 393 }, { 795, 394 }, { 873, 394 }, { 236, 465 }, { 317, 466 }, { 398, 465 },
        { 479, 466 }, { 559, 466 }, { 640, 466 }, { 721, 466 }, { 802, 466 }, { 883, 466 },
    };
    auto boss_inventory_after = boss_inventory_before;
    boss_inventory_after.emplace_back(758, 487);
    assert(added_equipment_contact_hint(boss_inventory_before, boss_inventory_after) == cv::Point(758, 487));
    assert(!added_equipment_contact_hint(boss_inventory_before, boss_inventory_before));
    boss_inventory_after.back() = cv::Point(965, 487);
    assert(!added_equipment_contact_hint(boss_inventory_before, boss_inventory_after));
    boss_inventory_after.back() = cv::Point(758, 487);
    boss_inventory_after[4].x += 40;
    assert(!added_equipment_contact_hint(boss_inventory_before, boss_inventory_after));
    const auto life_pending = cv::imread("spa-life-boss-pending-header.png");
    const auto life_40 = cv::imread("spa-life-40-header.png");
    const auto life_27 = cv::imread("spa-life-27-header.png");
    assert(!life_pending.empty() && !life_40.empty() && !life_27.empty());
    const auto pending_number = defence_life_number_roi(life_pending);
    const auto forty_number = defence_life_number_roi(life_40);
    const auto twenty_seven_number = defence_life_number_roi(life_27);
    assert(pending_number && pending_number->x <= 830 && pending_number->br().x >= 841 && pending_number->br().x < 855);
    // The whole two-digit glyph group must survive. The original fixed crop
    // cut the battle 9 and could not follow the header's changing layout.
    assert(forty_number && forty_number->width >= 27);
    assert(twenty_seven_number && twenty_seven_number->width >= 25);
    auto no_life_icon = life_pending.clone();
    no_life_icon(cv::Rect(810, 21, 16, 24)).setTo(cv::Scalar());
    assert(!defence_life_number_roi(no_life_icon));
    auto ambiguous_life = life_pending.clone();
    life_pending(cv::Rect(810, 21, 16, 24)).copyTo(ambiguous_life(cv::Rect(755, 21, 16, 24)));
    assert(!defence_life_number_roi(ambiguous_life));
    auto no_life_digits = life_pending.clone();
    no_life_digits(cv::Rect(826, 23, 25, 22)).setTo(cv::Scalar());
    assert(!defence_life_number_roi(no_life_digits));
    assert(!defence_life_number_roi(cv::Mat()));
    const auto battle_boss_hud = cv::imread("spa-boss-battle-hud-live.png");
    assert(battle_boss_hud.size() == cv::Size(332, 240));
    const auto boss_hud_frame = [&](int row) {
        cv::Mat frame = cv::Mat::zeros(720, 1280, CV_8UC3);
        battle_boss_hud(cv::Rect(0, row * 60, 332, 60)).copyTo(frame(cv::Rect(448, 0, 332, 60)));
        return frame;
    };
    assert(battle_boss_hp_bar_fill_percent(boss_hud_frame(0)) == 100);
    assert(battle_boss_hp_bar_fill_percent(boss_hud_frame(1)) == 67);
    assert(battle_boss_hp_bar_fill_percent(boss_hud_frame(2)) == 9);
    assert(!battle_boss_hp_bar_fill_percent(boss_hud_frame(3)));
    auto obscured_boss_hud = boss_hud_frame(1);
    obscured_boss_hud(cv::Rect(597, 30, 170, 4)).setTo(cv::Scalar());
    assert(!battle_boss_hp_bar_fill_percent(obscured_boss_hud));
    auto detached_fill = boss_hud_frame(0);
    detached_fill(cv::Rect(640, 30, 45, 4)).setTo(cv::Scalar());
    assert(!battle_boss_hp_bar_fill_percent(detached_fill));
    assert(!battle_boss_hp_bar_fill_percent(cv::Mat()));
    const auto blue_gate_clean = cv::imread("spa-ghost-blue-gate-empty-clean.png");
    const auto blue_gate_held = cv::imread("spa-ghost-blue-gate-empty-held.png");
    assert(!blue_gate_clean.empty() && !blue_gate_held.empty());
    // Native R9 verified reference recovered from its floating inverse-grid
    // receipt. R10 empty home-adjacent floor is 1.9px outside the ORB hull.
    const cv::Mat blue_gate_grid =
        (cv::Mat_<double>(3, 3) << 78.30822938434558,
         -12.379340346163223,
         245.62782864111358,
         -.0610416444266741,
         57.51153329420079,
         174.10832272269613,
         .000058473256666108635,
         -.019355555347066986,
         1.0);
    assert(!registered_deployment_pose(blue_gate_held, blue_gate_clean, { 467, 438 }));
    const auto green_home_tile =
        registered_green_grid_cell_pose(blue_gate_held, blue_gate_clean, blue_gate_grid, { 3, 4 });
    assert(green_home_tile && cv::norm(*green_home_tile - cv::Point(467, 438)) < 3);
    assert(!registered_green_grid_cell_pose(blue_gate_held, blue_gate_clean, blue_gate_grid, { 3, 3 }));
    assert(!registered_green_grid_cell_pose(blue_gate_clean, blue_gate_clean, blue_gate_grid, { 3, 4 }));
    assert(
        !registered_green_grid_cell_pose(blue_gate_held, cv::Mat::zeros(720, 1280, CV_8UC3), blue_gate_grid, { 3, 4 }));
    assert(!registered_green_grid_cell_pose(blue_gate_held, blue_gate_clean, cv::Mat(), { 3, 4 }));
    assert(!registered_green_grid_cell_pose(blue_gate_held, blue_gate_clean, blue_gate_grid, { 11, 4 }));
    const auto urbi_source_clean = cv::imread("spa-boss-urbi-source-clean.png");
    const auto urbi_source_held = cv::imread("spa-boss-urbi-source-held.png");
    const std::vector<cv::Point> urbi_source_points { { 468, 438 }, { 720, 367 }, { 637, 300 }, { 975, 438 },
                                                      { 722, 438 }, { 552, 438 }, { 807, 438 }, { 637, 438 } };
    assert(!registered_field_points(urbi_source_clean, urbi_source_held, urbi_source_points, FieldView::Held));
    const auto recovered_source = registered_held_field_points(
        urbi_source_clean,
        urbi_source_held,
        urbi_source_points,
        blue_gate_grid,
        0,
        { 3, 4 });
    assert(
        recovered_source && recovered_source->size() == 8 &&
        cv::norm(recovered_source->front() - cv::Point(468, 438)) < 4);
    assert(!registered_held_field_points(
        urbi_source_clean,
        urbi_source_held,
        urbi_source_points,
        blue_gate_grid,
        0,
        { 4, 4 }));
    assert(
        !registered_held_field_points(urbi_source_clean, urbi_source_held, urbi_source_points, cv::Mat(), 0, { 3, 4 }));
    auto invalid_peer = urbi_source_points;
    invalid_peer[1] = { 0, 0 };
    assert(
        !registered_held_field_points(urbi_source_clean, urbi_source_held, invalid_peer, blue_gate_grid, 0, { 3, 4 }));
    const auto mire_clean = cv::imread("spa-boss-mire-target-clean.png");
    const auto mire_held = cv::imread("spa-boss-mire-target-held.png");
    assert(!green_deployment_hint(mire_held, { 803, 367 }));
    const auto mire_paired = equipment_target_preview(mire_clean, mire_held, { 803, 367 });
    assert(
        mire_paired.verified && mire_paired.added_green >= 400 && mire_paired.left_green >= 80 &&
        mire_paired.right_green >= 80);
    assert(!equipment_target_preview(mire_held, mire_held, { 803, 367 }).verified);
    const auto blue_gate_direction = cv::imread("spa-ghost-blue-gate-direction.png");
    const auto grid_receiver =
        registered_grid_receiver_pose(blue_gate_direction, blue_gate_held, blue_gate_grid, { 3, 4 });
    assert(grid_receiver && cv::norm(*grid_receiver - cv::Point(467, 438)) < 3);
    assert(!registered_grid_receiver_pose(blue_gate_direction, blue_gate_held, blue_gate_grid, { 4, 4 }));
    assert(!registered_grid_receiver_pose(blue_gate_held, blue_gate_held, blue_gate_grid, { 3, 4 }));
    assert(!registered_grid_receiver_pose(
        blue_gate_direction,
        cv::Mat::zeros(720, 1280, CV_8UC3),
        blue_gate_grid,
        { 3, 4 }));
    assert(!registered_grid_receiver_pose(blue_gate_direction, blue_gate_held, cv::Mat(), { 3, 4 }));
    auto manual_boss_preview = cv::imread("spa-manual-boss-attackable-preview.png");
    assert(!manual_boss_preview.empty());
    // The original user screenshot is 1282x723; normalize only this fixture
    // to the native 1280x720 input. It is still a manual demonstration.
    cv::resize(manual_boss_preview, manual_boss_preview, cv::Size(1280, 720));
    const auto manual_boss_bar = preparation_boss_health_bar(manual_boss_preview);
    std::cerr << "Manual boss bar recognized: " << manual_boss_bar.has_value() << '\n';
    assert(manual_boss_bar);
    const auto manual_region = boss_attackable_region(manual_boss_preview, *manual_boss_bar);
    assert(manual_region && cv::countNonZero(*manual_region) > 3000);
    assert(cv::countNonZero((*manual_region)(cv::Rect(820, 190, 270, 220))) > 3000);
    const auto cocoon_direction_crop = cv::imread("spa-boss-direction-overlay-live.png");
    assert(cocoon_direction_crop.size() == cv::Size(560, 540));
    cv::Mat cocoon_direction = cv::Mat::zeros(720, 1280, CV_8UC3);
    cocoon_direction_crop.copyTo(cocoon_direction(cv::Rect(645, 135, 560, 540)));
    const auto cocoon_bar = preparation_boss_health_bar(cocoon_direction);
    const auto cocoon_center = direction_selection_center(cocoon_direction);
    assert(cocoon_bar && cocoon_center && direction_cancel_target(cocoon_direction, *cocoon_center));
    assert(!boss_attackable_region(cocoon_direction, *cocoon_bar));

    cv::Mat mirrored_boss;
    cv::flip(manual_boss_preview, mirrored_boss, 1);
    auto mirrored_bar = *manual_boss_bar;
    mirrored_bar.x = 1280 - mirrored_bar.x - mirrored_bar.width;
    const auto detected_mirrored_bar = preparation_boss_health_bar(mirrored_boss);
    assert(detected_mirrored_bar && std::abs(detected_mirrored_bar->x - mirrored_bar.x) < 3);
    const auto mirrored_region = boss_attackable_region(mirrored_boss, mirrored_bar);
    assert(mirrored_region);
    cv::Mat expected_mirror, difference;
    cv::flip(*manual_region, expected_mirror, 1);
    cv::absdiff(*mirrored_region, expected_mirror, difference);
    assert(cv::countNonZero(difference) == 0);
    cv::Mat solid_red = cv::Mat::zeros(720, 1280, CV_8UC3);
    cv::rectangle(solid_red, cv::Rect(850, 190, 220, 210), cv::Scalar(0, 0, 255), cv::FILLED);
    assert(!boss_attackable_region(solid_red, *manual_boss_bar));
    cv::Mat compact_hatching = cv::Mat::zeros(720, 1280, CV_8UC3);
    cv::Mat compact_mask = cv::Mat::zeros(720, 1280, CV_8UC3);
    for (int offset = -70; offset < 150; offset += 14) {
        cv::line(compact_mask, { 900 + offset, 225 }, { 830 + offset, 295 }, cv::Scalar(0, 0, 255), 7);
    }
    compact_mask(cv::Rect(900, 225, 80, 70)).copyTo(compact_hatching(cv::Rect(900, 225, 80, 70)));
    assert(boss_attackable_region(compact_hatching, *manual_boss_bar));
    cv::Mat red_cross = cv::Mat::zeros(720, 1280, CV_8UC3);
    cv::line(red_cross, { 900, 225 }, { 979, 294 }, cv::Scalar(0, 0, 255), 12);
    cv::line(red_cross, { 979, 225 }, { 900, 294 }, cv::Scalar(0, 0, 255), 12);
    assert(!boss_attackable_region(red_cross, *manual_boss_bar));
    const cv::Rect occluded_bar(969, 335, 186, 6);
    const cv::Mat grid_projection = (cv::Mat_<double>(3, 3) << 82, 0, 302, 0, 64, 180, 0, 0, 1);
    cv::Mat occluded_boss(720, 1280, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::rectangle(occluded_boss, occluded_bar, cv::Scalar(38, 31, 213), cv::FILLED);
    assert(estimate_boss_opening_footprint(occluded_boss, occluded_bar, grid_projection).empty());
    for (int x = 974; x < 1152; x += 14) {
        cv::line(occluded_boss, { x, 395 }, { x + 8, 397 }, cv::Scalar(31, 33, 212), 2);
    }
    assert(!boss_attackable_region(occluded_boss, occluded_bar));
    const auto footprint = estimate_boss_opening_footprint(occluded_boss, occluded_bar, grid_projection);
    assert(footprint.size() == 15);
    assert((footprint.front() == std::pair { 7, 1 }));
    assert((footprint.back() == std::pair { 11, 3 }));
    const cv::Mat mirror_screen = (cv::Mat_<double>(3, 3) << -1, 0, 1279, 0, 1, 0, 0, 0, 1);
    cv::flip(occluded_boss, occluded_boss, 1);
    const cv::Rect mirrored_occluded_bar(1280 - occluded_bar.br().x, occluded_bar.y, occluded_bar.width, 6);
    assert(
        estimate_boss_opening_footprint(occluded_boss, mirrored_occluded_bar, mirror_screen * grid_projection) ==
        footprint);
    cv::Mat partial_region = cv::Mat::zeros(720, 1280, CV_8UC1);
    cv::rectangle(partial_region, cv::Rect(795, 255, 30, 20), cv::Scalar(255), cv::FILLED);
    const cv::Mat example_grid = (cv::Mat_<double>(3, 3) << 80, 0, 200, 0, 60, 160, 0, 0, 1);
    const auto partial_cells = project_boss_attackable_cells(partial_region, example_grid);
    assert(std::find(partial_cells.begin(), partial_cells.end(), std::pair(7, 2)) != partial_cells.end());
    assert(std::find(partial_cells.begin(), partial_cells.end(), std::pair(8, 2)) != partial_cells.end());
    assert(std::find(partial_cells.begin(), partial_cells.end(), std::pair(7, 3)) == partial_cells.end());
    assert(project_boss_attackable_cells(partial_region, cv::Mat()).empty());
    const auto inventory_r12 = cv::imread("spa-inventory-operator-hints-r12.png");
    const auto inventory_r13 = cv::imread("spa-inventory-operator-hints-r13.png");
    assert(!inventory_r12.empty() && !inventory_r13.empty());
    std::vector<InventoryOperatorHint> inventory_hints;
    assert(!remember_inventory_operator_title(inventory_hints, inventory_r12, { 277, 465 }, "乌尔比安", false));
    assert(!remember_inventory_operator_title(inventory_hints, inventory_r12, { 756, 393 }, "虹彩活血饮料", true));
    assert(remember_inventory_operator_title(inventory_hints, inventory_r12, { 277, 465 }, "乌尔比安", true));
    assert(!inventory_operator_visual_hint(inventory_hints, inventory_r13, { 358, 466 }));
    assert(remember_inventory_operator_title(inventory_hints, inventory_r12, { 358, 466 }, "安哲拉", true));
    assert(remember_inventory_operator_title(inventory_hints, inventory_r12, { 438, 465 }, "深巡", true));
    const auto urbi_hint = inventory_operator_visual_hint(inventory_hints, inventory_r13, { 277, 465 });
    const auto andreana_hint = inventory_operator_visual_hint(inventory_hints, inventory_r13, { 358, 466 });
    const auto deep_hint = inventory_operator_visual_hint(inventory_hints, inventory_r13, { 438, 465 });
    assert(urbi_hint && urbi_hint->first == "乌尔比安");
    assert(andreana_hint && andreana_hint->first == "安哲拉");
    assert(deep_hint && deep_hint->first == "深巡");
    // The same verified sprite can be learned during bench actions and
    // reused for inventory discovery, or found in the opposite direction.
    std::vector<InventoryOperatorHint> bench_hints;
    assert(remember_inventory_operator_title(bench_hints, inventory_r12, { 277, 450 }, "乌尔比安", true));
    assert(inventory_operator_visual_hint(bench_hints, inventory_r13, { 277, 465 }) == urbi_hint);
    assert(inventory_operator_visual_hint(inventory_hints, inventory_r13, { 277, 450 }) == urbi_hint);
    assert(!inventory_operator_visual_hint(inventory_hints, inventory_r13, { 756, 393 }));
    assert(!inventory_operator_visual_hint(inventory_hints, inventory_r13, { 834, 394 }));
    assert(!inventory_operator_visual_hint(inventory_hints, cv::Mat(), { 277, 465 }));
    assert(!inventory_operator_visual_hint(inventory_hints, inventory_r13, { 277, 100 }));
    auto ambiguous_inventory_hints = inventory_hints;
    ambiguous_inventory_hints.push_back({ "野鬃", inventory_hints.front().sprite.clone() });
    assert(!inventory_operator_visual_hint(ambiguous_inventory_hints, inventory_r13, { 277, 465 }));
    const auto skadi_elite_live = cv::imread("spa-skadi-elite-unknown-facing-live.png");
    assert(!skadi_elite_live.empty());
    // R10 retried E/S/N because promotion cleared the facing ledger. The
    // registered clean frame already exposes an unambiguous east chevron.
    assert(deployed_facing(skadi_elite_live, { 468, 438 }) == 'E');
    const auto elite_bench = cv::imread("spa-bench-elite-shadow-live.png");
    assert(!elite_bench.empty());
    const auto bench_centers = bench_source_centres(elite_bench);
    assert(bench_centers.size() == 5);
    const std::vector<int> expected_bench_x { 600, 681, 761, 842, 923 };
    for (size_t index = 0; index < expected_bench_x.size(); ++index) {
        assert(std::abs(bench_centers[index].x - expected_bench_x[index]) <= 4);
        assert(bench_centers[index].y == 450);
    }
    // A gap between real cards is not itself evidence of another occupant.
    // Reject an empty panel and a textured body whose badge is absent.
    auto empty_bench_gap = elite_bench.clone();
    cv::rectangle(empty_bench_gap, cv::Rect(632, 375, 85, 115), cv::Scalar(48, 42, 35), cv::FILLED);
    assert(bench_source_centres(empty_bench_gap).size() == 4);
    auto missing_badge = elite_bench.clone();
    cv::rectangle(missing_badge, cv::Rect(635, 378, 35, 39), cv::Scalar(48, 42, 35), cv::FILLED);
    const auto no_badge_centers = bench_source_centres(missing_badge);
    assert(std::none_of(no_badge_centers.begin(), no_badge_centers.end(), [](cv::Point center) {
        return std::abs(center.x - 681) < 10;
    }));
    assert(bench_source_centres(cv::Mat()).empty());
    assert(bench_source_centres(cv::Mat::zeros(720, 1280, CV_8UC3)).empty());
    const auto lappland_before = cv::imread("spa-replacement-lappland-occluded-before.png");
    const auto lappland_held = cv::imread("spa-replacement-lappland-occluded-initial.png");
    const auto lappland_recheck = cv::imread("spa-replacement-lappland-occluded-held.png");
    assert(!lappland_before.empty() && !lappland_held.empty());
    const cv::Point lappland_target(478, 236), verified_greyy(475, 300);
    assert(!equipment_target_preview(lappland_before, lappland_held, lappland_target).verified);
    const auto occluded_recipient =
        equipment_target_preview(lappland_before, lappland_held, lappland_target, { verified_greyy });
    std::cerr << "Occluded recipient: " << occluded_recipient.verified << " green=" << occluded_recipient.added_green
              << " sides=" << occluded_recipient.left_green << ',' << occluded_recipient.right_green << '\n';
    assert(occluded_recipient.verified && occluded_recipient.occlusion_assisted);
    assert(occluded_recipient.added_green >= 250 && occluded_recipient.added_green < 400);
    assert(cv::norm(occluded_recipient.target - lappland_target) < 3);
    // The later animation frame exposes fewer than 80 newly-green pixels
    // on the left. It remains unknown even with a verified lower occupant.
    assert(!equipment_target_preview(lappland_before, lappland_recheck, lappland_target, { verified_greyy }).verified);
    assert(!equipment_target_preview(lappland_before, lappland_before, lappland_target, { verified_greyy }).verified);
    assert(!equipment_target_preview(lappland_before, lappland_held, lappland_target, { { 555, 300 } }).verified);
    assert(!equipment_target_preview(lappland_before, lappland_held, { 560, 236 }, { { 556, 300 } }).verified);
    cv::Mat neutral_board, neutral_luma;
    cv::cvtColor(lappland_before, neutral_luma, cv::COLOR_BGR2GRAY);
    cv::cvtColor(neutral_luma, neutral_board, cv::COLOR_GRAY2BGR);
    const RegisteredReplacementReceiver registered { { 6, 1 },
                                                     (cv::Mat_<double>(3, 3) << 80, 0, -2, 0, 60, 176, 0, 0, 1) };
    auto narrow_receiver_overlay = neutral_board.clone();
    const cv::Scalar preview_green(20, 220, 80);
    cv::rectangle(narrow_receiver_overlay, cv::Rect(451, 238, 16, 16), preview_green, cv::FILLED);
    const auto one_side =
        equipment_target_preview(neutral_board, narrow_receiver_overlay, lappland_target, {}, false, registered);
    assert(!one_side.verified && !one_side.registered_cell_assisted);
    auto neighbouring_overlay = narrow_receiver_overlay.clone();
    cv::rectangle(neighbouring_overlay, cv::Rect(521, 219, 7, 14), preview_green, cv::FILLED);
    assert(!equipment_target_preview(neutral_board, neighbouring_overlay, lappland_target, {}, false, registered)
                .verified);
    cv::rectangle(narrow_receiver_overlay, cv::Rect(500, 219, 5, 14), preview_green, cv::FILLED);
    assert(!equipment_target_preview(neutral_board, narrow_receiver_overlay, lappland_target).verified);
    const auto same_cell =
        equipment_target_preview(neutral_board, narrow_receiver_overlay, lappland_target, {}, false, registered);
    assert(same_cell.verified && same_cell.registered_cell_assisted);
    const RegisteredReplacementReceiver wrong_cell { { 7, 1 }, registered.grid_to_before };
    assert(!equipment_target_preview(neutral_board, narrow_receiver_overlay, lappland_target, {}, false, wrong_cell)
                .verified);
    const auto shop_one_reference = cv::imread("spa-shop-book-one-reference.png");
    const auto shop_one_live = cv::imread("spa-shop-book-one-live.png");
    assert(shop_book_one_glyph_matches(shop_one_live, shop_one_reference));
    const auto shop_one_white_live = cv::imread("spa-shop-book-one-white-live.png");
    assert(shop_book_one_glyph_matches(shop_one_white_live, shop_one_reference));
    assert(!shop_book_one_glyph_matches(cv::Mat(), shop_one_reference));
    assert(!shop_book_one_glyph_matches(cv::Mat(50, 48, CV_8UC3, cv::Scalar(200, 200, 200)), shop_one_reference));
    // A second full-height stroke is a different Roman book, not another I
    // location that may be accepted by a partial-template search.
    auto two_strokes = shop_one_reference.clone();
    cv::rectangle(two_strokes, cv::Rect(32, 8, 6, 28), cv::Scalar(0, 0, 0), cv::FILLED);
    assert(!shop_book_one_glyph_matches(two_strokes, shop_one_reference));
    auto header_only = cv::Mat(50, 48, CV_8UC3, cv::Scalar(220, 220, 220));
    header_only.rowRange(0, 6).setTo(cv::Scalar(0, 0, 0));
    assert(!shop_book_one_glyph_matches(header_only, shop_one_reference));
    const auto bison_direction = cv::imread("spa-direction-bison-target.png");
    const auto bison_held = cv::imread("spa-direction-bison-held.png");
    // The inferred foot lies outside the measured feature hull. Keep this
    // rejected rather than weakening registration to force the live move.
    assert(!registered_deployment_pose(bison_direction, bison_held, { 549, 449 }));
    const auto truth_held = cv::imread("spa-truth-deploy-held.png");
    const auto truth_collapsed = cv::imread("spa-truth-deploy-collapsed.png");
    const auto truth_direction = cv::imread("spa-truth-deploy-direction.png");
    const auto truth_grid = register_preparation_board(
        truth_held,
        false,
        { { 468, 440 }, { 471, 368 }, { 553, 439 }, { 554, 370 }, { 639, 449 }, { 728, 371 }, { 639, 300 } });
    const auto truth_center = direction_selection_center(truth_direction);
    std::cerr << "Truth grid: " << truth_grid.grid_verified << " score=" << truth_grid.score
              << " map=" << truth_grid.map_id << " center=" << truth_center.has_value() << '\n';
    assert(truth_grid.grid_verified && truth_center);
    const auto truth_receiver =
        registered_deployment_pose(truth_direction, truth_held, *truth_center + cv::Point(0, 20));
    assert(truth_receiver);
    const auto truth_pose =
        registered_deployment_cell_pose(truth_held, truth_collapsed, truth_grid.grid_to_screen, *truth_receiver);
    assert(truth_pose && cv::norm(*truth_pose - cv::Point(475, 230)) < 8);
    const auto truth_fit = estimate_camera_projection(truth_held, truth_collapsed, true);
    const auto reused_truth_pose = registered_deployment_cell_pose(truth_fit, truth_grid.grid_to_screen, *truth_receiver);
    assert(reused_truth_pose && cv::norm(*reused_truth_pose - *truth_pose) <= 1);
    assert(registered_grid_projection(truth_fit, truth_grid.grid_to_screen));
    assert(!registered_deployment_cell_pose(CameraProjection {}, truth_grid.grid_to_screen, *truth_receiver));
    assert(!registered_deployment_cell_pose(bison_direction, bison_held, truth_grid.grid_to_screen, { 549, 449 }));
    assert(!registered_deployment_cell_pose(truth_held, truth_collapsed, cv::Mat(), *truth_receiver));
    std::vector<cv::Point2f> truth_between_cells;
    cv::perspectiveTransform(
        std::vector<cv::Point2f> { { 4.5f, 1.f } },
        truth_between_cells,
        truth_grid.grid_to_screen);
    assert(!registered_deployment_cell_pose(
        truth_held,
        truth_collapsed,
        truth_grid.grid_to_screen,
        { cvRound(truth_between_cells[0].x), cvRound(truth_between_cells[0].y) }));
    auto preparation_hud_frame = cv::imread("spa-equipment-inventory.png");
    assert(!preparation_hud_frame.empty());
    PreparationHudEvidence hud_evidence;
    assert(!hud_evidence.matches(preparation_hud_frame, 4, 2, 6));
    assert(hud_evidence.remember_verified(preparation_hud_frame, 4, 2, 6));
    assert(hud_evidence.matches(preparation_hud_frame, 4, 2, 6));
    assert(!hud_evidence.matches(preparation_hud_frame, 5, 2, 6));
    assert(!hud_evidence.matches(preparation_hud_frame, 4, 1, 6));
    assert(!hud_evidence.matches(preparation_hud_frame, 4, 2, 5));
    assert(!hud_evidence.matches_region(0, preparation_hud_frame, 5, 2, 6));
    assert(!hud_evidence.matches_region(4, preparation_hud_frame, 4, 2, 6));
    assert(!hud_evidence.matches(cv::Mat(), 4, 2, 6));
    assert(!hud_evidence.matches(cv::Mat::zeros(719, 1280, CV_8UC3), 4, 2, 6));
    auto board_animation = preparation_hud_frame.clone();
    cv::rectangle(board_animation, cv::Rect(600, 200, 30, 30), cv::Scalar(0, 255, 255), -1);
    assert(hud_evidence.matches(board_animation, 4, 2, 6));
    for (const auto& point : { cv::Point(489, 23), cv::Point(1190, 520), cv::Point(1150, 110), cv::Point(1250, 460) }) {
        auto changed_hud = preparation_hud_frame.clone();
        changed_hud.at<cv::Vec3b>(point)[0] ^= 1;
        assert(!hud_evidence.matches(changed_hud, 4, 2, 6));
    }
    const auto saved_hud_frame = preparation_hud_frame.clone();
    auto changed_funds = preparation_hud_frame.clone();
    changed_funds.at<cv::Vec3b>(520, 1190)[0] ^= 1;
    assert(!hud_evidence.matches_region(1, changed_funds, 4, 2, 6));
    assert(hud_evidence.matches_region(0, changed_funds, 4, 2, 6));
    assert(hud_evidence.matches_region(2, changed_funds, 4, 2, 6));
    assert(hud_evidence.matches_region(3, changed_funds, 4, 2, 6));
    preparation_hud_frame.at<cv::Vec3b>(23, 489)[0] ^= 1;
    assert(!hud_evidence.matches(preparation_hud_frame, 4, 2, 6));
    assert(hud_evidence.matches(saved_hud_frame, 4, 2, 6));
    assert(!hud_evidence.remember_verified(saved_hud_frame, 0, 2, 6));
    assert(!hud_evidence.matches(saved_hud_frame, 4, 2, 6));
    assert(hud_evidence.remember_verified(saved_hud_frame, 4, 2, 6));
    hud_evidence.clear();
    assert(!hud_evidence.matches(saved_hud_frame, 4, 2, 6));
    assert(hud_evidence.remember_verified(saved_hud_frame, 4, 2, 6, 385));
    auto animated_refresh = saved_hud_frame.clone();
    animated_refresh.at<cv::Vec3b>(450, 1150)[0] ^= 1;
    assert(hud_evidence.matches_region(3, animated_refresh, 4, 2, 6));
    animated_refresh.at<cv::Vec3b>(392, 1250)[0] ^= 1;
    assert(!hud_evidence.matches_region(3, animated_refresh, 4, 2, 6));
    // A partial update without new counter OCR retains its verified label.
    assert(hud_evidence.remember_verified(saved_hud_frame, 4, 2, 6));
    animated_refresh = saved_hud_frame.clone();
    animated_refresh.at<cv::Vec3b>(450, 1150)[0] ^= 1;
    assert(hud_evidence.matches_region(3, animated_refresh, 4, 2, 6));
    // A fresh collapsed-shop label moves the protected counter region.
    assert(hud_evidence.remember_verified(saved_hud_frame, 4, 2, 6, 465));
    animated_refresh = saved_hud_frame.clone();
    animated_refresh.at<cv::Vec3b>(470, 1250)[0] ^= 1;
    assert(!hud_evidence.matches_region(3, animated_refresh, 4, 2, 6));
    assert(!hud_evidence.remember_verified(saved_hud_frame, 4, 2, 6, 720));
    assert(!hud_evidence.matches_region(0, saved_hud_frame, 4, 2, 6));
    assert(hud_evidence.remember_verified(saved_hud_frame, 4, 0, 6, 385, false));
    assert(hud_evidence.matches_region(0, saved_hud_frame, 4, 0, 6));
    assert(hud_evidence.matches_region(2, saved_hud_frame, 4, 0, 6));
    assert(hud_evidence.matches_region(3, saved_hud_frame, 4, 0, 6));
    assert(!hud_evidence.matches_region(1, saved_hud_frame, 4, 0, 6));
    assert(!hud_evidence.matches(saved_hud_frame, 4, 0, 6));
    const auto industrial_shadow = cv::imread("spa-held-industrial-shadow.png");
    const auto shadow_empty_clean = cv::imread("spa-shadow-empty-cell-clean.png");
    const auto shadow_empty_held = cv::imread("spa-shadow-empty-cell-held.png");
    assert(!shadow_empty_clean.empty() && !shadow_empty_held.empty());
    assert(!green_deployment_hint(shadow_empty_held, cv::Point(881, 300)));
    assert(registered_added_green_preview(shadow_empty_clean, shadow_empty_held, cv::Point(881, 300)));
    assert(!registered_added_green_preview(shadow_empty_held, shadow_empty_held, cv::Point(881, 300)));
    assert(!registered_added_green_preview(shadow_empty_clean, shadow_empty_clean, cv::Point(881, 300)));
    assert(!registered_added_green_preview(cv::Mat(), shadow_empty_held, cv::Point(881, 300)));
    assert(!registered_added_green_preview(shadow_empty_clean, shadow_empty_held, cv::Point(881, 600)));
    const auto meteor_clean = cv::imread("spa-meteor-move-clean-live.png");
    const auto meteor_held = cv::imread("spa-meteor-move-held-live.png");
    const cv::Point meteor_target(475, 301);
    assert(!green_deployment_hint(meteor_held, meteor_target));
    assert(!registered_added_green_preview(meteor_clean, meteor_held, meteor_target));
    assert(registered_added_green_preview(meteor_clean, meteor_held, meteor_target, { { 472, 371 }, { 558, 236 } }));
    assert(!registered_added_green_preview(meteor_clean, meteor_held, meteor_target, { { 558, 236 } }));
    assert(!registered_added_green_preview(meteor_held, meteor_held, meteor_target, { { 472, 371 } }));
    assert(!industrial_shadow.empty());
    const auto industrial_board = register_preparation_board(industrial_shadow, false);
    std::cerr << "Industrial shadow board: " << industrial_board.map_id << " score=" << industrial_board.score
              << " alternative=" << industrial_board.alternative_score << " grid=" << industrial_board.grid_verified
              << '\n';
    assert(industrial_board.verified && industrial_board.grid_verified && industrial_board.map_id == "m12");
    const auto crate_held = cv::imread("spa-held-crate-overlay.png");
    assert(!crate_held.empty());
    const auto crate_board = register_preparation_board(crate_held, false);
    std::cerr << "Crate overlay board: " << crate_board.map_id << " score=" << crate_board.score << '\n';
    assert(crate_board.verified && crate_board.grid_verified && crate_board.map_id == "m04");
    const auto m10_initial = cv::imread("spa-reference-m10-initial.png");
    const auto m10_crowded = cv::imread("spa-reference-m10-crowded.png");
    assert(!m10_initial.empty() && !m10_crowded.empty());
    const auto m10_known = register_preparation_board(m10_initial, true);
    assert(m10_known.verified && m10_known.map_id == "m10");
    const std::vector<PlacementMember> crowded_members { { 719, 234, PlacementRole::Frontline, "Miss.Christine", 'E' },
                                                         { 722, 299, PlacementRole::Ranged, "凯瑟琳", 'E' },
                                                         { 878, 234, PlacementRole::Frontline, "斯卡蒂", 'E' },
                                                         { 557, 234, PlacementRole::Ranged, "深巡", 'E' },
                                                         { 820, 449, PlacementRole::Ranged, "灰喉", 'N' },
                                                         { 888, 300, PlacementRole::Ranged, "瑰盐", 'E' },
                                                         { 805, 299, PlacementRole::Ranged, "白铁", 'E' },
                                                         { 900, 371, PlacementRole::Frontline, "蛇屠箱", 'E' } };
    std::vector<cv::Point> crowded_points;
    for (const auto& member : crowded_members) {
        crowded_points.emplace_back(member.x, member.y);
    }
    const auto crowded_colour = register_preparation_board(m10_crowded, false, crowded_points);
    assert(crowded_colour.grid_verified && !crowded_colour.verified);
    assert(crowded_colour.map_id == "m02");
    const auto crowded_reference =
        reproject_preparation_board(m10_initial, m10_crowded, m10_known.grid_to_screen, "m10", false);
    assert(
        crowded_reference.verified && crowded_reference.identity_from_reference && crowded_reference.map_id == "m10");
    const auto crowded_cells =
        registered_grid_members(crowded_reference.grid_to_screen, crowded_members, FieldView::Held);
    assert(crowded_cells && crowded_cells->size() == 8);
    assert(crowded_cells->at(0).col == 6 && crowded_cells->at(0).row == 1);
    assert(crowded_cells->at(5).col == 8 && crowded_cells->at(5).row == 2);
    assert(std::none_of(crate_board.legal_cells.begin(), crate_board.legal_cells.end(), [](const auto& cell) {
        return cell.row == 1 && (cell.col == 3 || cell.col == 4 || cell.col == 8);
    }));
    const auto occluded_direction = cv::imread("spa-direction-occluded-top.png");
    const auto occluded_held = cv::imread("spa-direction-occluded-top-held.png");
    assert(!occluded_direction.empty() && !occluded_held.empty());
    const auto occluded_center = direction_selection_center(occluded_direction);
    assert(occluded_center && cv::norm(*occluded_center - cv::Point(800, 214)) < 5);
    const auto occluded_foot =
        registered_deployment_pose(occluded_direction, occluded_held, *occluded_center + cv::Point(0, 20));
    assert(occluded_foot && cv::norm(*occluded_foot - cv::Point(797, 235)) < 6);
    const auto occluded_board = register_preparation_board(occluded_held, false);
    const auto occluded_cell = registered_grid_members(
        occluded_board.grid_to_screen,
        { { occluded_foot->x, occluded_foot->y, PlacementRole::Ranged, "卡达", 'E' } },
        FieldView::Held);
    assert(occluded_cell && occluded_cell->size() == 1);
    const cv::Mat snapshot_grid = (cv::Mat_<double>(3, 3) << 80, 0, 300, 0, 60, 170, 0, 0, 1);
    const std::vector<PlacementMember> snapshot_members { { 540, 290, PlacementRole::Ranged, "罗比菈塔", 'E' },
                                                          { 620, 230, PlacementRole::Ranged, "罗比菈塔", 'N' },
                                                          { 700, 290, PlacementRole::Frontline, "泡泡", '\0' } };
    const auto snapshot_cells = registered_grid_members(snapshot_grid, snapshot_members);
    assert(
        snapshot_cells && snapshot_cells->size() == 3 && snapshot_cells->at(0).col == 3 &&
        snapshot_cells->at(0).row == 2 && snapshot_cells->at(1).col == 4 && snapshot_cells->at(1).row == 1);
    assert(snapshot_cells->at(1).facing == 'N' && snapshot_cells->at(2).facing == '\0');
    auto swapped_snapshot = snapshot_members;
    std::swap(swapped_snapshot[0].x, swapped_snapshot[2].x);
    std::swap(swapped_snapshot[0].y, swapped_snapshot[2].y);
    const auto swapped_grid = registered_grid_members(snapshot_grid, swapped_snapshot);
    assert(
        swapped_grid && swapped_grid->at(0).name == "罗比菈塔" && swapped_grid->at(0).col == 5 &&
        swapped_grid->at(2).name == "泡泡" && swapped_grid->at(2).col == 3 && swapped_grid->size() == 3);
    auto moved_snapshot = snapshot_members;
    moved_snapshot[2].x = 780;
    const auto moved_grid = registered_grid_members(snapshot_grid, moved_snapshot);
    assert(moved_grid && moved_grid->at(2).col == 6 && moved_grid->at(0).col == 3 && moved_grid->size() == 3);
    const auto survivors = registered_grid_members(snapshot_grid, { snapshot_members[0], snapshot_members[2] });
    assert(survivors && survivors->size() == 2 && survivors->front().name == "罗比菈塔" && survivors->back().col == 5);
    auto overlapping_cells = snapshot_members;
    overlapping_cells[1].x = 550;
    overlapping_cells[1].y = 295;
    assert(!registered_grid_members(snapshot_grid, overlapping_cells));
    auto between_cells = snapshot_members;
    between_cells.front().x += 30;
    assert(!registered_grid_members(snapshot_grid, between_cells));
    assert(!registered_grid_members(snapshot_grid, {}));
    assert(!registered_grid_members(cv::Mat::zeros(3, 3, CV_64F), snapshot_members));
    assert(!registered_grid_members(snapshot_grid, { { 100, 290, PlacementRole::Ranged, "罗比菈塔", 'E' } }));
    assert(!registered_grid_members(snapshot_grid, { { 540, 290, PlacementRole::Ranged, "", 'E' } }));
    // A held view expands the visible board. Its right edge is valid only
    // in that camera context; the logical grid and duplicate gates stay strict.
    const std::vector<PlacementMember> right_edge { { 1100, 410, PlacementRole::Ranged, "卡达", 'E' } };
    assert(!registered_grid_members(snapshot_grid, right_edge));
    const auto held_right_edge = registered_grid_members(snapshot_grid, right_edge, FieldView::Held);
    assert(held_right_edge && held_right_edge->front().col == 10 && held_right_edge->front().row == 4);
    assert(!registered_grid_members(
        snapshot_grid,
        { { 1180, 410, PlacementRole::Ranged, "卡达", 'E' } },
        FieldView::Held));
    const auto north_healer = cv::imread("spa-replacement-healer-north.png");
    assert(!north_healer.empty());
    const auto previous_healer_facing = deployed_facing(north_healer, { 911, 448 });
    std::cerr << "Previous healer facing: " << previous_healer_facing.value_or('?') << '\n';
    // The north-facing arrow is occluded by this sprite. Keep it unknown;
    // preserving a recorded direction input must not loosen the pixel model.
    assert(!previous_healer_facing);
    // Actual round-5 Chestnut selection: register the same six verified
    // field poses into the held camera before scoring any healing range.
    const auto medic_normal = cv::imread("spa-medic-range-normal.png");
    const auto medic_held = cv::imread("spa-medic-range-held.png");
    assert(!medic_normal.empty() && !medic_held.empty());
    const std::vector<PlacementMember> medic_before {
        { 560, 251, PlacementRole::Ranged, "折光" },        { 980, 438, PlacementRole::Frontline, "拉普兰德" },
        { 719, 234, PlacementRole::Frontline, "拉普兰德" }, { 820, 449, PlacementRole::Ranged, "深靛" },
        { 722, 299, PlacementRole::Unknown, "砾" },         { 799, 234, PlacementRole::Frontline, "野鬃" }
    };
    const auto medic_camera = estimate_camera_projection(medic_normal, medic_held, true);
    assert(medic_camera.estimated);
    std::vector<PlacementMember> medic_held_members;
    std::vector<cv::Point> medic_occluded;
    for (auto member : medic_before) {
        const auto point = project_supported_point(medic_camera, cv::Point2f(member.x, member.y));
        assert(point);
        member.x = static_cast<int>(std::lround(point->x));
        member.y = static_cast<int>(std::lround(point->y));
        medic_held_members.push_back(member);
        medic_occluded.emplace_back(member.x, member.y);
    }
    std::vector<cv::Point> medic_original_points;
    for (const auto& member : medic_before) {
        medic_original_points.emplace_back(member.x, member.y);
    }
    const auto medic_held_points =
        registered_field_points(medic_normal, medic_held, medic_original_points, FieldView::Held);
    assert(medic_held_points && *medic_held_points == medic_occluded);
    const auto medic_board = register_preparation_board(medic_held, false, medic_occluded);
    assert(medic_board.verified && medic_board.map_id == "m05");
    const auto medic_reference_grid = registered_grid_projection(medic_held, medic_normal, medic_board.grid_to_screen);
    assert(medic_reference_grid);
    // Once a summon is released, clean field geometry is still available
    // even though there is no green placement overlay to register as legal.
    const auto clean_geometry = registered_grid_projection(medic_normal, medic_normal, *medic_reference_grid);
    assert(clean_geometry);
    assert(!reproject_preparation_board(medic_normal, medic_normal, *medic_reference_grid, "m05", false).verified);
    const auto carried_board =
        reproject_preparation_board(medic_normal, medic_held, *medic_reference_grid, "m05", false);
    assert(carried_board.verified && carried_board.identity_from_reference && carried_board.map_id == "m05");
    std::vector<cv::Point2f> original_cell, carried_cell;
    cv::perspectiveTransform(std::vector<cv::Point2f> { { 6, 2 } }, original_cell, medic_board.grid_to_screen);
    cv::perspectiveTransform(std::vector<cv::Point2f> { { 6, 2 } }, carried_cell, carried_board.grid_to_screen);
    assert(cv::norm(original_cell.front() - carried_cell.front()) < 4);
    assert(registered_grid_members(carried_board.grid_to_screen, { medic_held_members[4] }, FieldView::Held));
    assert(!reproject_preparation_board(medic_normal, medic_held, *medic_reference_grid, "unknown", false).verified);
    assert(!reproject_preparation_board(medic_normal, medic_held, cv::Mat::zeros(3, 3, CV_64F), "m05", false).verified);
    assert(!reproject_preparation_board(
                cv::Mat::zeros(720, 1280, CV_8UC3),
                medic_held,
                *medic_reference_grid,
                "m05",
                false)
                .verified);
    std::vector<GridPlacementMember> medic_grid_members;
    for (const auto& member : medic_held_members) {
        std::vector<cv::Point2f> grid;
        cv::perspectiveTransform(
            std::vector<cv::Point2f> { { static_cast<float>(member.x), static_cast<float>(member.y) } },
            grid,
            medic_board.grid_to_screen.inv());
        assert(grid.size() == 1);
        const int col = static_cast<int>(std::lround(grid[0].x)), row = static_cast<int>(std::lround(grid[0].y));
        std::cerr << "Medic grid " << member.name << " -> " << grid[0] << '\n';
        if (std::abs(grid[0].x - col) <= .35 && std::abs(grid[0].y - row) <= .35) {
            medic_grid_members.push_back({ col, row, member.role, member.name });
        }
    }
    assert(medic_grid_members.size() == medic_before.size());
    std::vector<GridPlacementCell> medic_grid_legal;
    std::vector<std::pair<int, int>> medic_screen_legal;
    for (const auto& cell : medic_board.legal_cells) {
        medic_grid_legal.push_back({ cell.col, cell.row, cell.center.x, cell.center.y });
        medic_screen_legal.emplace_back(cell.center.x, cell.center.y);
    }
    // BattleData range 3-17, the ordinary Chestnut's actual 18-cell range.
    const std::vector<std::pair<int, int>> chestnut_range { { 1, 2 },  { 2, 2 },  { 3, 2 },  { 0, 1 },  { 1, 1 },
                                                            { 2, 1 },  { 3, 1 },  { 0, 0 },  { 1, 0 },  { 2, 0 },
                                                            { 3, 0 },  { 0, -1 }, { 1, -1 }, { 2, -1 }, { 3, -1 },
                                                            { 1, -2 }, { 2, -2 }, { 3, -2 } };
    const auto medic_preferences =
        range_placement_preferences(PlacementRole::Healer, medic_grid_legal, medic_grid_members, chestnut_range);
    char medic_facing = '?';
    const auto medic_choice = choose_role_placement(
        PlacementRole::Healer,
        medic_screen_legal,
        medic_held_members,
        chestnut_range,
        &medic_facing,
        medic_preferences,
        true);
    assert(medic_choice && medic_facing == 'N');
    const auto medic_selected = std::find_if(medic_grid_legal.begin(), medic_grid_legal.end(), [&](const auto& cell) {
        return cell.screen_x == medic_choice->first && cell.screen_y == medic_choice->second;
    });
    assert(medic_selected != medic_grid_legal.end() && medic_selected->col == 8 && medic_selected->row == 4);
    const auto medic_score =
        std::find_if(medic_preferences.begin(), medic_preferences.end(), [&](const auto& preference) {
            return preference.x == medic_choice->first && preference.y == medic_choice->second &&
                   preference.facing == medic_facing;
        });
    assert(medic_score != medic_preferences.end() && medic_score->bonus >= 9000);
    const auto strategy_full = cv::imread("spa-strategy-grid-full.png");
    const auto strategy_detail = cv::imread("spa-strategy-grid-detail.png");
    assert(!strategy_full.empty() && !strategy_detail.empty());
    const auto [full_x, full_y, full_w, full_h] = opening_strategy_card_rect(0, false);
    const auto [detail_x, detail_y, detail_w, detail_h] = opening_strategy_card_rect(0, true);
    const auto full_card = strategy_full(cv::Rect(full_x + 10, full_y + 8, full_w - 20, full_h - 13));
    const auto detail_card = strategy_detail(cv::Rect(detail_x + 10, detail_y + 8, detail_w - 20, detail_h - 13));
    cv::Mat portrait_match;
    cv::matchTemplate(full_card, detail_card, portrait_match, cv::TM_CCOEFF_NORMED);
    assert(portrait_match.at<float>(0, 0) > .9F);
    const auto [last_x, last_y, last_w, last_h] = opening_strategy_card_rect(15, true);
    assert(last_x + last_w < 970 && last_y + last_h <= 705 && last_h > 60);
    const cv::Mat blank = cv::Mat::zeros(720, 1280, CV_8UC3);
    const auto retreat_reference = cv::imread("spa-retreat-control.png");
    const auto field_selected = cv::imread("spa-field-selected-scaled.png");
    const auto bench_selected = cv::imread("spa-bench-selected.png");
    assert(!retreat_reference.empty() && !field_selected.empty() && !bench_selected.empty());
    const auto retreat_match = selected_retreat_control(field_selected, retreat_reference);
    assert(retreat_match && retreat_match->score >= .92 && retreat_match->scale > 1);
    assert(retreat_match->rect.contains(cv::Point(527, 338)));
    assert(!selected_retreat_control(bench_selected, retreat_reference));
    assert(!selected_retreat_control(blank, retreat_reference));
    assert(!selected_retreat_control(field_selected, cv::Mat()));
    auto duplicate_retreat = field_selected.clone();
    field_selected(retreat_match->rect)
        .copyTo(duplicate_retreat(cv::Rect(800, 170, retreat_match->rect.width, retreat_match->rect.height)));
    assert(!selected_retreat_control(duplicate_retreat, retreat_reference));
    const auto inventory_frame = cv::imread("spa-equipment-inventory.png");
    assert(!inventory_frame.empty());
    const auto equipment_points = equipment_inventory_candidates(inventory_frame);
    assert(std::any_of(equipment_points.begin(), equipment_points.end(), [](auto point) {
        return cv::norm(point - cv::Point(760, 400)) < 20;
    }));
    assert(equipment_inventory_candidates(blank).empty());
    const auto city_inventory = cv::imread("spa-equipment-inventory-city.png");
    const auto empty_inventory = cv::imread("spa-equipment-inventory-empty.png");
    assert(!city_inventory.empty() && !empty_inventory.empty());
    // The city item is on the lower bench row. The other candidate is an
    // operator, so both still need native title checks. Battle scenery near
    // (971,399) must not be mistaken for a recovered inventory source.
    const auto city_equipment_points = equipment_inventory_candidates(city_inventory);
    assert(city_equipment_points.size() == 2);
    assert(std::any_of(city_equipment_points.begin(), city_equipment_points.end(), [](auto point) {
        return cv::norm(point - cv::Point(923, 466)) < 4;
    }));
    assert(std::none_of(city_equipment_points.begin(), city_equipment_points.end(), [](auto point) {
        return cv::norm(point - cv::Point(971, 399)) < 24;
    }));
    assert(std::any_of(equipment_points.begin(), equipment_points.end(), [](auto point) {
        return cv::norm(point - cv::Point(761, 465)) < 4;
    }));
    assert(equipment_inventory_candidates(empty_inventory).empty());
    const auto pact_top = cv::imread("spa-pact-availability-top.png");
    const auto pact_scroll = cv::imread("spa-pact-availability-scroll.png");
    assert(!pact_top.empty() && !pact_scroll.empty());
    for (const int x : { 133, 233, 333 }) {
        assert(pact_icon_available(pact_top, cv::Rect(x - 30, 308, 60, 20), true) == true);
    }
    for (const int x : { 184, 283 }) {
        assert(pact_icon_available(pact_top, cv::Rect(x - 30, 438, 60, 20), true) == true);
    }
    for (const int x : { 564, 665, 765 }) {
        assert(pact_icon_available(pact_top, cv::Rect(x - 30, 308, 60, 20), true) == false);
    }
    for (const int x : { 615, 714 }) {
        assert(pact_icon_available(pact_top, cv::Rect(x - 30, 438, 60, 20), true) == false);
    }
    for (const int x : { 58, 145, 230, 316, 403, 487, 572, 658, 745, 831 }) {
        assert(pact_icon_available(pact_top, cv::Rect(x - 27, 649, 54, 20), false) == true);
        assert(pact_icon_available(pact_scroll, cv::Rect(x - 27, 258, 54, 20), false) == true);
    }
    assert(pact_icon_available(pact_scroll, cv::Rect(31, 370, 54, 20), false) == true);
    for (const int x : { 145, 230, 316, 403, 487 }) {
        assert(pact_icon_available(pact_scroll, cv::Rect(x - 27, 370, 54, 20), false) == false);
    }
    assert(!pact_icon_available(blank, cv::Rect(200, 308, 60, 20), true).has_value());
    assert(!pact_icon_available(pact_scroll, cv::Rect(200, 180, 60, 20), true).has_value());
    const std::vector<cv::Point> full_field_poses { { 721, 234 }, { 724, 368 }, { 727, 437 }, { 797, 235 },
                                                    { 804, 299 }, { 877, 235 }, { 882, 300 }, { 976, 438 } };
    const auto held_full = cv::imread("spa-held-full-field.png");
    const auto unheld_full = cv::imread("spa-unheld-full-field.png");
    assert(!held_full.empty() && !unheld_full.empty());
    const auto occupied_alignment = align_occupied_preview(held_full, full_field_poses);
    std::cerr << "Full field alignment: " << occupied_alignment.matched_cells << "/" << occupied_alignment.total_cells
              << " shift=" << occupied_alignment.shift << '\n';
    assert(occupied_alignment.verified && occupied_alignment.total_cells == 8);
    assert(occupied_swap_hint(held_full, cv::Point(724, 368) + occupied_alignment.shift));
    assert(!align_occupied_preview(unheld_full, full_field_poses).verified);
    assert(!align_occupied_preview(blank, full_field_poses).verified);
    assert(!align_occupied_preview(held_full, { { 724, 368 }, { 797, 235 } }).verified);
    const auto gear_normal = cv::imread("spa-gear-preview-normal.png");
    const auto gear_held = cv::imread("spa-gear-preview-held.png");
    assert(!gear_normal.empty() && !gear_held.empty());
    // The real helmet attempt had only 4/7 visible perimeter matches because
    // sprites hid the other green edges. The original gate stays unchanged.
    assert(!align_occupied_preview(
                gear_held,
                { { 718, 300 }, { 722, 438 }, { 877, 236 }, { 881, 300 }, { 887, 368 }, { 892, 438 }, { 976, 438 } })
                .verified);
    const auto gear_projection = estimate_camera_projection(gear_normal, gear_held, true);
    std::cerr << "Gear camera fit: " << gear_projection.estimated << " median=" << gear_projection.median_error
              << " area=" << cv::contourArea(gear_projection.source_hull)
              << " target_support=" << cv::pointPolygonTest(gear_projection.source_hull, cv::Point2f(800, 185), true)
              << '\n';
    const auto gear_preview = equipment_target_preview(gear_normal, gear_held, { 800, 185 });
    std::cerr << "Gear preview projection: " << gear_preview.inliers << "/" << gear_preview.matches
              << " green=" << gear_preview.added_green << " target=" << gear_preview.target << '\n';
    assert(gear_preview.verified && cv::norm(gear_preview.target - cv::Point(877, 236)) < 20);
    const auto reused_preview = equipment_target_preview(gear_projection, gear_normal, gear_held, { 800, 185 });
    assert(reused_preview.verified && reused_preview.target == gear_preview.target &&
           reused_preview.added_green == gear_preview.added_green &&
           reused_preview.matches == gear_projection.matches && reused_preview.inliers == gear_projection.inliers);
    assert(!equipment_target_preview(CameraProjection {}, gear_normal, gear_held, { 800, 185 }).verified);
    assert(!equipment_target_preview(gear_projection, gear_normal, gear_held, { 500, 185 }).verified);
    const auto collapsed_pose = registered_deployment_pose(gear_held, gear_normal, gear_preview.target);
    assert(collapsed_pose && cv::norm(*collapsed_pose - cv::Point(800, 185)) < 4);
    assert(cv::norm(*collapsed_pose - gear_preview.target) > 50);
    const std::vector<PlacementMember> held_members {
        { gear_preview.target.x, gear_preview.target.y, PlacementRole::Frontline, "烈夏" }
    };
    std::vector<std::pair<int, int>> vacant { { gear_preview.target.x, gear_preview.target.y }, { 797, 236 } };
    std::erase_if(vacant, [&](auto cell) { return held_cell_has_member(cell.first, cell.second, held_members); });
    assert(vacant.size() == 1 && vacant.front() == std::pair(797, 236));
    assert(choose_role_placement(PlacementRole::Ranged, vacant, held_members) == std::optional(std::pair(797, 236)));
    assert(!registered_deployment_pose(blank, gear_normal, gear_preview.target));
    assert(!registered_deployment_pose(gear_held, gear_normal, { 20, 600 }));
    const auto mapped_field = registered_field_points(gear_held, gear_normal, { gear_preview.target });
    assert(mapped_field && mapped_field->size() == 1 && mapped_field->front() == *collapsed_pose);
    assert(!registered_field_points(gear_held, gear_normal, {}));
    assert(!registered_field_points(gear_held, gear_normal, { gear_preview.target, gear_preview.target }));
    assert(!registered_field_points(gear_held, gear_normal, { gear_preview.target, { 20, 600 } }));
    assert(!registered_field_points(blank, gear_normal, { gear_preview.target }));
    const cv::Mat held_grid =
        (cv::Mat_<double>(3, 3) << 80, 0, gear_preview.target.x - 160, 0, 60, gear_preview.target.y - 60, 0, 0, 1);
    const auto collapsed_grid = registered_grid_projection(gear_held, gear_normal, held_grid);
    assert(collapsed_grid);
    const auto reusable_fit = estimate_camera_projection(gear_held, gear_normal, true);
    assert(registered_field_points(reusable_fit, { gear_preview.target }) == mapped_field);
    const auto reused_grid = registered_grid_projection(reusable_fit, held_grid);
    assert(reused_grid && cv::norm(*reused_grid, *collapsed_grid) < 1e-6);
    assert(!registered_field_points(CameraProjection {}, { gear_preview.target }));
    assert(!registered_grid_projection(CameraProjection {}, held_grid));
    std::vector<cv::Point2f> known_cell;
    cv::perspectiveTransform(std::vector<cv::Point2f> { { 2, 1 } }, known_cell, *collapsed_grid);
    assert(known_cell.size() == 1 && cv::norm(known_cell.front() - cv::Point2f(800, 185)) < 4);
    const auto restored_cell = registered_grid_members(
        *collapsed_grid,
        { { collapsed_pose->x, collapsed_pose->y, PlacementRole::Frontline, "烈夏", 'E' } });
    assert(restored_cell && restored_cell->front().col == 2 && restored_cell->front().row == 1);
    assert(!registered_grid_projection(blank, gear_normal, held_grid));
    assert(!registered_grid_projection(gear_held, gear_normal, cv::Mat::zeros(3, 3, CV_64F)));
    assert(!registered_grid_projection(gear_held, gear_normal, cv::Mat::eye(2, 2, CV_64F)));
    const auto occupied_held = cv::imread("spa-occupied-drop-held.png");
    const auto occupied_direction = cv::imread("spa-occupied-drop-direction.png");
    const auto occupied_center = direction_selection_center(occupied_direction);
    assert(occupied_center && cv::norm(*occupied_center - cv::Point(805, 279)) < 4);
    const auto receiver = registered_deployment_pose(
        occupied_direction,
        occupied_held,
        cv::Point(occupied_center->x, occupied_center->y + 20));
    assert(receiver && cv::norm(*receiver - cv::Point(803, 300)) < 12);
    const std::vector<PlacementMember> receiver_members { { 803, 300, PlacementRole::Ranged, "承曦格雷伊" } };
    assert(!held_cell_has_member(824, 353, receiver_members));
    assert(held_cell_has_member(receiver->x, receiver->y, receiver_members));
    const auto cancel = direction_cancel_target(occupied_direction, *occupied_center);
    assert(cancel && cv::norm(*cancel - cv::Point(655, 174)) < 4);
    assert(!direction_cancel_target(occupied_held, *occupied_center));
    assert(!direction_cancel_target(blank, *occupied_center));
    assert(replacement_direction_center(occupied_direction, occupied_held, { 803, 300 }) == occupied_center);
    assert(!replacement_direction_center(occupied_direction, occupied_held, { 824, 353 }));
    assert(!replacement_direction_center(occupied_held, occupied_held, { 803, 300 }));
    assert(!replacement_direction_center(occupied_direction, blank, { 803, 300 }));
    cv::Mat edge_held = cv::Mat::zeros(blank.size(), CV_8UC3);
    cv::RNG edge_rng(977);
    auto field_texture = edge_held(cv::Rect(330, 150, 660, 335));
    edge_rng.fill(field_texture, cv::RNG::UNIFORM, 0, 160);
    const auto draw_replacement_direction = [&](int center_x) {
        auto direction = edge_held.clone();
        cv::polylines(
            direction,
            std::vector<cv::Point> { { center_x, 243 }, { center_x + 224, 429 },
                                     { center_x, 615 }, { center_x - 224, 429 } },
            true,
            cv::Scalar(255, 255, 255),
            3);
        cv::rectangle(direction, cv::Rect(center_x - 173, 305, 46, 38), cv::Scalar(30, 30, 200), cv::FILLED);
        cv::line(direction, { center_x - 163, 324 }, { center_x - 139, 324 }, cv::Scalar(255, 255, 255), 3);
        return direction;
    };
    const auto edge_direction = draw_replacement_direction(1001);
    const auto edge_center = direction_selection_center(edge_direction);
    assert(edge_center && cv::norm(*edge_center - cv::Point(1001, 429)) < 3);
    assert(!registered_deployment_pose(edge_direction, edge_held, *edge_center + cv::Point(0, 20)));
    assert(replacement_direction_center(edge_direction, edge_held, { 977, 438 }) == edge_center);
    assert(!replacement_direction_center(edge_direction, edge_held, { 893, 438 }));
    assert(!replacement_direction_center(edge_direction, edge_held, { 977, 365 }));
    assert(!replacement_direction_center(edge_direction, blank, { 977, 438 }));
    const auto interior_direction = draw_replacement_direction(910);
    const auto interior_center = direction_selection_center(interior_direction);
    assert(interior_center && cv::norm(*interior_center - cv::Point(910, 429)) < 3);
    const cv::Point interior_receiver(888, 438);
    const auto interior_foot = registered_deployment_pose(
        interior_direction, edge_held, *interior_center + cv::Point(0, 20));
    assert(interior_foot && cv::norm(*interior_foot - interior_receiver) > 22);
    assert(replacement_direction_center(interior_direction, edge_held, interior_receiver) == interior_center);
    assert(!replacement_direction_center(interior_direction, edge_held, { 977, 438 }));
    assert(!equipment_target_preview(gear_normal, gear_normal, { 800, 185 }).verified);
    assert(!equipment_target_preview(gear_normal, gear_held, { 500, 185 }).verified);
    assert(!equipment_target_preview(blank, gear_held, { 800, 185 }).verified);
    cv::Mat shifted_full;
    const cv::Mat occupied_translation = (cv::Mat_<double>(2, 3) << 1, 0, 8, 0, 1, 6);
    cv::warpAffine(held_full, shifted_full, occupied_translation, held_full.size());
    const auto shifted_occupied = align_occupied_preview(shifted_full, full_field_poses);
    assert(shifted_occupied.verified);
    assert(occupied_swap_hint(shifted_full, cv::Point(724, 368) + shifted_occupied.shift));
    assert(!register_preparation_board(blank, true).verified);
    assert(!register_preparation_board(blank, true).grid_verified);
    const cv::Mat identity_grid = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat shifted_grid = identity_grid.clone();
    shifted_grid.at<double>(0, 2) = 8;
    assert(board_grid_consensus(identity_grid, { identity_grid, shifted_grid }));
    shifted_grid.at<double>(0, 2) = 20;
    assert(!board_grid_consensus(identity_grid, { identity_grid, shifted_grid }));
    assert(!board_grid_consensus(identity_grid, {}));
    assert(!board_grid_consensus(identity_grid, { cv::Mat() }));
    const auto ambiguous_frame = cv::imread("spa-held-ambiguous-board.png");
    assert(!ambiguous_frame.empty());
    const auto ambiguous_grid = register_preparation_board(ambiguous_frame, true);
    std::cerr << "Ambiguous board: " << ambiguous_grid.map_id << " score=" << ambiguous_grid.score
              << " alternative=" << ambiguous_grid.alternative_score << " grid=" << ambiguous_grid.grid_verified
              << '\n';
    // The previously ambiguous image is m03: its four shooting platforms
    // and three crates are now part of the map model, rather than omitted
    // overlays on otherwise identical ground tiles.
    assert(ambiguous_grid.verified && ambiguous_grid.map_id == "m03");
    assert(ambiguous_grid.grid_verified && !ambiguous_grid.legal_cells.empty());
    assert(std::none_of(ambiguous_grid.legal_cells.begin(), ambiguous_grid.legal_cells.end(), [](const auto& cell) {
        return (cell.row == 2 && (cell.col == 5 || cell.col == 7 || cell.col == 8 || cell.col == 9)) ||
               (cell.row == 3 && (cell.col == 3 || cell.col == 4 || cell.col == 9));
    }));
    const auto fences = cv::imread("spa-held-fence-board.png");
    assert(!fences.empty());
    const auto board = register_preparation_board(fences, true);
    assert(board.verified && board.map_id == "m06");
    assert(board.grid_verified);
    assert(std::any_of(board.legal_cells.begin(), board.legal_cells.end(), [](const auto& cell) {
        return cell.row == 4 && cell.col == 6 && cell.on_ground_route && cell.ground_passable;
    }));
    assert(std::any_of(board.legal_cells.begin(), board.legal_cells.end(), [](const auto& cell) {
        return cell.row == 3 && cell.col == 6 && !cell.on_ground_route && !cell.ground_passable;
    }));
    for (const auto& cell : board.legal_cells) {
        if (cell.on_ground_route) {
            assert(cell.ground_passable);
        }
    }
    // Reproduce the reported open lower lane on the real fence/swamp map.
    // An upper-lane blocker must not attract another guard onto a fence,
    // even when the guard's attack range includes that existing blocker.
    std::vector<std::pair<int, int>> road_cells;
    for (const auto& cell : board.legal_cells) {
        if (cell.on_ground_route) {
            road_cells.emplace_back(cell.center.x, cell.center.y);
        }
    }
    const auto lower_road = choose_role_placement(
        PlacementRole::Frontline,
        road_cells,
        { { 869, 232, PlacementRole::Frontline } },
        { { 1, 0 } });
    assert(lower_road && lower_road->second > 400);
    assert(std::any_of(board.legal_cells.begin(), board.legal_cells.end(), [&](const auto& cell) {
        return cell.on_ground_route && cell.ground_passable &&
               cell.center == cv::Point(lower_road->first, lower_road->second);
    }));
    const auto occupied_board = cv::imread("spa-held-fence-occupied.png");
    const auto occupied_fit = register_preparation_board(
        occupied_board,
        true,
        { { 640, 366 }, { 720, 296 }, { 724, 366 }, { 869, 232 }, { 721, 439 } });
    std::cerr << "Occupied board registration: " << occupied_fit.map_id << " score=" << occupied_fit.score
              << " alternative=" << occupied_fit.alternative_score << '\n';
    assert(occupied_fit.verified && occupied_fit.map_id == "m06");
    const auto right_road = cv::imread("spa-right-road-direction.png");
    const auto right_road_center = direction_selection_center(right_road);
    assert(
        right_road_center && right_road_center->x >= 990 && right_road_center->x <= 1025 &&
        right_road_center->y >= 420 && right_road_center->y <= 445);
    assert(detect_held_deployment_cells(blank).empty());
    const auto ice = cv::imread("spa-held-ice-board.png");
    assert(!ice.empty());
    const auto measured = detect_held_deployment_cells(ice);
    assert(measured.size() >= 12);
    // Manually inspected tile interiors in the actual held-camera frame,
    // including two points not on the former fixed 80px sampling lattice.
    for (const auto expected : { cv::Point(480, 300), cv::Point(640, 365), cv::Point(815, 440) }) {
        assert(std::any_of(measured.begin(), measured.end(), [&](const auto& cell) {
            return cv::norm(cell.center - expected) < 18;
        }));
    }
    for (const auto& cell : measured) {
        assert(cell.center.y < 495 && cell.center.x < 1000);
        assert(green_deployment_hint(ice, cell.center));
    }
    cv::Mat shifted;
    const cv::Mat translation = (cv::Mat_<double>(2, 3) << 1, 0, 20, 0, 1, 10);
    cv::warpAffine(ice, shifted, translation, ice.size());
    const auto shifted_cells = detect_held_deployment_cells(shifted);
    assert(std::any_of(shifted_cells.begin(), shifted_cells.end(), [](const auto& cell) {
        return cv::norm(cell.center - cv::Point(500, 310)) < 18;
    }));
    auto diamond = blank.clone();
    const std::vector<cv::Point> vertices { { 800, 80 }, { 1000, 230 }, { 800, 380 }, { 600, 230 } };
    cv::polylines(diamond, vertices, true, cv::Scalar(255, 255, 255), 3);
    const auto center = direction_selection_center(diamond);
    assert(center && cv::norm(*center - cv::Point(800, 230)) < 4);
    assert(!direction_cancel_target(diamond, *center));
    auto right_diamond = blank.clone();
    const std::vector<cv::Point> right_vertices { { 925, 230 }, { 1150, 430 }, { 925, 630 }, { 700, 430 } };
    cv::polylines(right_diamond, right_vertices, true, cv::Scalar(255, 255, 255), 3);
    const auto right_center = direction_selection_center(right_diamond);
    assert(right_center && cv::norm(*right_center - cv::Point(925, 430)) < 4);
    assert(!direction_selection_center(blank));
    auto lower_diamond = blank.clone();
    const std::vector<cv::Point> lower_vertices { { 650, 330 }, { 850, 510 }, { 650, 690 }, { 450, 510 } };
    cv::polylines(lower_diamond, lower_vertices, true, cv::Scalar(255, 255, 255), 3);
    const auto lower_center = direction_selection_center(lower_diamond);
    assert(lower_center && cv::norm(*lower_center - cv::Point(650, 510)) < 4);
    auto lower_left_held = blank.clone();
    auto lower_left_texture = lower_left_held(cv::Rect(563, 150, 480, 335));
    edge_rng.fill(lower_left_texture, cv::RNG::UNIFORM, 0, 160);
    auto lower_left_diamond = lower_left_held.clone();
    const std::vector<cv::Point> lower_left_vertices { { 557, 244 }, { 778, 429 }, { 538, 660 }, { 322, 429 } };
    cv::polylines(lower_left_diamond, lower_left_vertices, true, cv::Scalar(255, 255, 255), 3);
    const auto lower_left_center = direction_selection_center(lower_left_diamond);
    assert(lower_left_center && cv::norm(*lower_left_center - cv::Point(549, 429)) < 4);
    cv::rectangle(lower_left_diamond, cv::Rect(376, 305, 46, 38), cv::Scalar(30, 30, 200), cv::FILLED);
    cv::line(lower_left_diamond, { 387, 324 }, { 411, 324 }, cv::Scalar(255, 255, 255), 3);
    const cv::Point lower_left_target(552, 437);
    assert(!registered_field_points(lower_left_held, lower_left_diamond, { lower_left_target }, FieldView::Held));
    assert(replacement_direction_center(lower_left_diamond, lower_left_held, lower_left_target) == lower_left_center);
    assert(!replacement_direction_center(lower_left_diamond, lower_left_held, { 474, 437 }));
    auto hint_image = blank.clone();
    cv::rectangle(hint_image, cv::Rect(700, 200, 80, 60), cv::Scalar(30, 210, 50), -1);
    assert(green_deployment_hint(hint_image, cv::Point(740, 230)));
    auto occluded_hint = hint_image.clone();
    cv::rectangle(occluded_hint, cv::Rect(734, 224, 13, 13), cv::Scalar(180, 70, 30), -1);
    assert(green_deployment_hint(occluded_hint, cv::Point(740, 230)));
    assert(!green_deployment_hint(hint_image, cv::Point(695, 230)));

    assert(!green_deployment_hint(hint_image, cv::Point(690, 230)));
    assert(!green_deployment_hint(hint_image, cv::Point(5, 5)));
    assert(!green_deployment_hint(blank, cv::Point(740, 230)));

    auto deployment_map = blank.clone();
    cv::rectangle(deployment_map, cv::Rect(724, 289, 87, 91), cv::Scalar(255, 180, 80), 3);
    cv::rectangle(deployment_map, cv::Rect(280, 195, 64, 45), cv::Scalar(255, 180, 80), 3);
    const auto deployment_cells = detect_deployment_cells(deployment_map);
    assert(deployment_cells.size() == 1);
    assert(cv::norm(deployment_cells.front().center - cv::Point(767, 334)) < 8);
    const auto probe_cells = detect_deployment_cells(blank);
    assert(!probe_cells.empty());
    // Normal-camera fallback points are diagnostic, never legal-cell proof.
    assert(std::all_of(probe_cells.begin(), probe_cells.end(), [](const auto& cell) {
        return cell.score <= 0.1 && cell.center.x >= 180 && cell.center.x < 1000 && cell.center.y < 620;
    }));

    const auto real_overlay = cv::imread("spa-green-hint-overlay.png");
    assert(!real_overlay.empty() && real_overlay.size() == cv::Size(37, 37));
    auto real_hint = blank.clone();
    real_overlay.copyTo(real_hint(cv::Rect(722, 212, 37, 37)));
    assert(green_deployment_hint(real_hint, cv::Point(740, 230)));

    // A promoted live unit lost its committed direction in the ledger.
    // Its held-frame chevron is readable without issuing a Face action.
    const auto unknown_facing_live = cv::imread("spa-held-unknown-facing-live.png");
    assert(!unknown_facing_live.empty());
    assert(deployed_facing(unknown_facing_live, cv::Point(556, 301)) == 'S');
    assert(deployed_facing(unknown_facing_live, cv::Point(637, 301)) == 'E');
    const auto boss_preparation = cv::imread("spa-boss-preparation-pipe-live.png");
    const auto boss_bar = preparation_boss_health_bar(boss_preparation);
    assert(boss_bar && boss_bar->x >= 890 && boss_bar->y >= 350 && boss_bar->width >= 170);
    const auto boss_held = cv::imread("spa-boss-held-pipe-live.png");
    const auto held_boss_bar = preparation_boss_health_bar(boss_held);
    assert(held_boss_bar && held_boss_bar->x >= 950 && held_boss_bar->y >= 420);
    assert(!boss_attackable_region(boss_held, *held_boss_bar));
    // Logical positions recorded by the real round-14 held-frame receipt.
    const auto boss_grid = cv::getPerspectiveTransform(
        std::vector<cv::Point2f> { { 3, 1 }, { 5, 1 }, { 3, 4 }, { 5, 3 } },
        std::vector<cv::Point2f> { { 477, 236 }, { 637, 236 }, { 468, 439 }, { 637, 368 } });
    const auto boss_registered = reproject_preparation_board(boss_held, boss_held, boss_grid, "m12", false);
    assert(boss_registered.verified && boss_registered.grid_verified && boss_registered.boss_phase_visible);
    assert(boss_registered.map_id == "m12" && !boss_registered.legal_cells.empty());
    const auto selected_range = cv::imread("spa-attack-range-outline-live.png");
    const auto selected_range_center = direction_selection_center(selected_range);
    assert(!selected_range_center || !direction_cancel_target(selected_range, *selected_range_center));
    assert(!direction_cancel_target(selected_range, cv::Point(666, 443)));
    const auto ghost_direction = cv::imread("spa-ghost-direction-live.png");
    const auto ghost_direction_center = direction_selection_center(ghost_direction);
    assert(ghost_direction_center && direction_cancel_target(ghost_direction, *ghost_direction_center));
    assert(!preparation_boss_health_bar(unknown_facing_live));
    assert(!preparation_boss_health_bar(blank));
    auto fake_spawns = blank.clone();
    cv::rectangle(fake_spawns, cv::Rect(930, 170, 90, 75), cv::Scalar(0, 0, 255), 3);
    cv::rectangle(fake_spawns, cv::Rect(930, 350, 90, 75), cv::Scalar(0, 0, 255), 3);
    assert(!preparation_boss_health_bar(fake_spawns));
    // Reproduce the wide Quintus bar's measured extent in the expanded
    // preparation camera and its mirror at the opposite screen edge.
    auto wide_preview = blank.clone();
    const cv::Rect wide_bar(867, 366, 407, 6);
    cv::rectangle(wide_preview, wide_bar, cv::Scalar(38, 31, 213), cv::FILLED);
    const auto detected_wide = preparation_boss_health_bar(wide_preview);
    assert(
        detected_wide && std::abs(detected_wide->x - wide_bar.x) <= 1 && std::abs(detected_wide->y - wide_bar.y) <= 1 &&
        detected_wide->size() == wide_bar.size());
    cv::flip(wide_preview, wide_preview, 1);
    const auto mirrored_wide = preparation_boss_health_bar(wide_preview);
    assert(mirrored_wide && std::abs(mirrored_wide->x - (1280 - wide_bar.br().x)) <= 1);
    cv::rectangle(wide_preview, cv::Rect(867, 366, 407, 6), cv::Scalar(38, 31, 213), cv::FILLED);
    assert(!preparation_boss_health_bar(wide_preview));
    auto broad_stripe = blank.clone();
    cv::rectangle(broad_stripe, cv::Rect(650, 366, 600, 6), cv::Scalar(38, 31, 213), cv::FILLED);
    assert(!preparation_boss_health_bar(broad_stripe));

    const auto east_patch = cv::imread("spa-deployed-east.png");
    assert(!east_patch.empty());
    auto deployed = blank.clone();
    east_patch.copyTo(deployed(cv::Rect(700, 300, 101, 101)));
    assert(deployed_facing(deployed, cv::Point(750, 350)) == 'E');
    const auto astesi_patch = cv::imread("spa-deployed-astesi-east.png");
    auto astesi_frame = blank.clone();
    assert(!astesi_patch.empty());
    astesi_patch.copyTo(astesi_frame(cv::Rect(700, 300, 101, 101)));
    assert(deployed_facing(astesi_frame, cv::Point(750, 350)) == 'E');
    const auto ctrail_patch = cv::imread("spa-deployed-ctrail-east.png");
    auto ctrail_frame = blank.clone();
    assert(!ctrail_patch.empty());
    ctrail_patch.copyTo(ctrail_frame(cv::Rect(700, 300, 101, 101)));
    assert(deployed_facing(ctrail_frame, cv::Point(750, 350)) == 'E');
    for (const auto& patch_image : { astesi_patch, ctrail_patch }) {
        for (int turn = 0; turn < 4; ++turn) {
            cv::Mat rotated = patch_image.clone();
            if (turn > 0) {
                cv::rotate(
                    patch_image,
                    rotated,
                    turn == 1   ? cv::ROTATE_90_COUNTERCLOCKWISE
                    : turn == 2 ? cv::ROTATE_180
                                : cv::ROTATE_90_CLOCKWISE);
            }
            auto frame = blank.clone();
            rotated.copyTo(frame(cv::Rect(700, 300, 101, 101)));
            const char expected_facing = turn == 0 ? 'E' : turn == 1 ? 'N' : turn == 2 ? 'W' : 'S';
            assert(deployed_facing(frame, cv::Point(750, 350)) == expected_facing);
            assert(deployed_facing(frame, cv::Point(753, 353)) == expected_facing);
        }
    }
    auto orange_rectangle = blank.clone();
    cv::rectangle(orange_rectangle, cv::Rect(770, 345, 10, 15), cv::Scalar(0, 160, 255), -1);
    assert(!deployed_facing(orange_rectangle, cv::Point(750, 350)));
    assert(!deployed_facing(blank, cv::Point(750, 350)));
    assert(!deployed_facing(deployed, cv::Point(5, 5)));
    for (int turn = 1; turn < 4; ++turn) {
        cv::Mat rotated;
        cv::rotate(
            east_patch,
            rotated,
            turn == 1   ? cv::ROTATE_90_COUNTERCLOCKWISE
            : turn == 2 ? cv::ROTATE_180
                        : cv::ROTATE_90_CLOCKWISE);
        rotated.copyTo(deployed(cv::Rect(700, 300, 101, 101)));
        assert(deployed_facing(deployed, cv::Point(750, 350)) == (turn == 1 ? 'N' : turn == 2 ? 'W' : 'S'));
    }
    const auto ready_strategy_patch = cv::imread("spa-full-focus-ready.png");
    const auto used_strategy_patch = cv::imread("spa-full-focus-used.png");
    auto strategy_frame = blank.clone();
    assert(!ready_strategy_patch.empty() && !used_strategy_patch.empty());
    ready_strategy_patch.copyTo(strategy_frame(cv::Rect(1190, 175, 90, 115)));
    assert(active_strategy_ready(strategy_frame));
    used_strategy_patch.copyTo(strategy_frame(cv::Rect(1190, 175, 90, 115)));
    assert(!active_strategy_ready(strategy_frame));
    assert(!active_strategy_ready(blank));
    assert(!active_strategy_ready(cv::Mat()));
    assert(!estimate_camera_projection(blank, blank).estimated);
    assert(!estimate_camera_projection(cv::Mat(), blank).estimated);
    assert(!estimate_camera_projection(cv::Mat::zeros(10, 10, CV_8UC3), blank).estimated);
    auto source = blank.clone();
    cv::RNG random(731);
    for (int index = 0; index < 1500; ++index) {
        const cv::Point center(random.uniform(460, 950), random.uniform(155, 310));
        const cv::Scalar color(random.uniform(50, 255), random.uniform(50, 255), random.uniform(50, 255));
        cv::circle(source, center, random.uniform(2, 6), color, -1);
    }
    const cv::Mat expected = (cv::Mat_<double>(3, 3) << 1.2, 0.08, -100, 0.03, 1.5, 200, 0, 0, 1);
    cv::Mat selected;
    cv::warpPerspective(source, selected, expected, source.size());
    const auto estimate = estimate_camera_projection(source, selected);
    assert(estimate.estimated && estimate.inliers >= 12);
    const cv::Point2f point(650, 230);
    const auto projected = project_supported_point(estimate, point);
    assert(projected);
    assert(cv::norm(*projected - cv::Point2f(698.4F, 564.5F)) < 4);
    assert(!project_supported_point(estimate, cv::Point2f(10, 10)));
    assert(!project_supported_point(CameraProjection(), point));
}
