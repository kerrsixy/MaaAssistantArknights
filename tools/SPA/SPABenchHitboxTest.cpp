#include "Task/MiniGame/SPA/SPABenchHitbox.h"
#include <cassert>
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

int main()
{
    using namespace asst::spa;
    // Unmodified batch2 R9 source-discovery failure; not a successful retry.
    const auto failed = cv::imread("bench_sale_head_occluded_live.png");
    const auto selected = cv::imread("selected_panel_title_live.png");
    assert(failed.size() == cv::Size(1280, 720));
    assert(selected.size() == failed.size());
    const cv::Point source(964, 450);
    const cv::Point exposed(964, 420);
    assert(bench_inspection_panel_absent(failed));
    assert(!bench_inspection_panel_absent(selected));
    assert(exposed_bench_head(failed, source) == exposed);
    // Identical-frame replay proves the geometry and unchanged-panel gates,
    // not that the live game has accepted any click or sold this operator.
    assert(bench_head_retry_target(failed, failed, source, false) == exposed);
    assert(!bench_head_retry_target(failed, failed, source, true));
    assert(!bench_head_retry_target(failed, selected, source, false));
    assert(!bench_head_retry_target(selected, failed, source, false));

    // A correct or unreadable open panel must never be clicked through.
    auto unreadable_panel = failed.clone();
    selected(cv::Rect(0, 170, 180, 180)).copyTo(unreadable_panel(cv::Rect(0, 170, 180, 180)));
    assert(!bench_head_retry_target(failed, unreadable_panel, source, false));

    // Neighboring operators, a freeze control itself, and non-bench points do
    // not inherit the final slot's visible head or badge.
    for (const auto other : { cv::Point(883, 450),
                              cv::Point(943, 450),
                              cv::Point(970, 450),
                              cv::Point(1040, 450),
                              cv::Point(964, 420),
                              cv::Point(964, 395) }) {
        assert(!bench_head_retry_target(failed, failed, other, false));
    }
    auto no_badge = failed.clone();
    no_badge(cv::Rect(918, 378, 34, 36)).setTo(cv::Scalar(150, 165, 180));
    assert(!bench_head_retry_target(failed, no_badge, source, false));
    auto no_head = failed.clone();
    no_head(cv::Rect(954, 412, 21, 15)).setTo(cv::Scalar(100, 140, 180));
    assert(!bench_head_retry_target(failed, no_head, source, false));

    // Synthetic occlusion uses real freeze-button pixels from the same frame;
    // texture alone cannot make that control an operator's exposed head.
    auto frozen_head = failed.clone();
    failed(cv::Rect(986, 447, 21, 15)).copyTo(frozen_head(cv::Rect(954, 412, 21, 15)));
    assert(!bench_head_retry_target(failed, frozen_head, source, false));

    auto changed_page = failed.clone();
    changed_page(cv::Rect(0, 170, 180, 180)) += cv::Scalar(8, 8, 8);
    assert(!bench_head_retry_target(failed, changed_page, source, false));
    assert(!bench_head_retry_target(cv::Mat(), failed, source, false));
    assert(!bench_head_retry_target(failed, cv::Mat(720, 1280, CV_8UC1), source, false));

    // D4 R14. The old head probe left the panel closed and the normal foot
    // coordinate falls inside the freeze button. The exposed shoulder is
    // eligible for inspection, but no fixture certifies input or identity.
    const auto elite = cv::imread("bench_elite_rightmost_occluded_live.png");
    assert(elite.size() == failed.size());
    const cv::Point elite_slot(963, 450);
    const cv::Point shoulder(947, 446);
    assert(exposed_bench_head(elite, elite_slot) == cv::Point(963, 420));
    assert(exposed_bench_shoulder(elite, elite_slot) == shoulder);
    assert(!overlaps_expanded_shop_controls(shoulder + cv::Point(2, 0)));
    assert(bench_shoulder_retry_target(elite, elite, elite_slot, false) == shoulder);
    assert(!bench_shoulder_retry_target(elite, elite, elite_slot, true));
    assert(!bench_shoulder_retry_target(elite, selected, elite_slot, false));
    assert(!bench_shoulder_retry_target(elite, failed, elite_slot, false));
    assert(!bench_shoulder_retry_target(failed, failed, source, false));
    assert(!verified_bench_grab_point(elite, elite_slot, elite_slot, {}));
    assert(!verified_bench_grab_point(elite, elite_slot, elite_slot, cv::Point(963, 480)));
    assert(verified_bench_grab_point(elite, elite_slot, elite_slot, shoulder) == shoulder);
    assert(verified_bench_grab_point(elite, cv::Point(883, 450), elite_slot, shoulder) == cv::Point(883, 480));
    auto shoulder_hidden = elite.clone();
    elite(cv::Rect(982, 447, 7, 15)).copyTo(shoulder_hidden(cv::Rect(943, 441, 7, 15)));
    assert(!bench_shoulder_retry_target(elite, shoulder_hidden, elite_slot, false));
    assert(!verified_bench_grab_point(shoulder_hidden, elite_slot, elite_slot, shoulder));

    // Christine is a purchasable operator with a low model. The genuine
    // R5/R6 frames reproduce source discovery with and without other units.
    const cv::Point christine(761, 450);
    for (const auto* file : { "bench_christine_with_operators_live.png", "bench_christine_alone_live.png" }) {
        const auto frame = cv::imread(file);
        assert(frame.size() == failed.size());
        const auto old = bench_source_centres(frame);
        const auto revised = bench_source_centres(frame, true);
        assert(std::find(old.begin(), old.end(), christine) == old.end());
        assert(std::count(revised.begin(), revised.end(), christine) == 1);
        assert(revised.size() == old.size() + 1);
        assert(!bench_christine_sprite_visible(frame, cv::Point(842, 450)));
        auto badge_only = frame.clone();
        badge_only(cv::Rect(741, 456, 50, 35)).setTo(cv::Scalar(30, 30, 30));
        const auto no_sprite = bench_source_centres(badge_only, true);
        assert(std::find(no_sprite.begin(), no_sprite.end(), christine) == no_sprite.end());
    }
    assert(bench_source_centres(failed, true) == bench_source_centres(failed));

    // The real round-9 III badge has a bright numeral over 41% of its
    // inner area. The old scan probed only the other three occupied slots.
    const auto tier_three = cv::imread("bench_tier_three_glyph_live.png");
    assert(tier_three.size() == failed.size());
    const cv::Point missing_slot(681, 450);
    const std::vector<cv::Point> other_slots { { 761, 450 }, { 842, 450 }, { 923, 450 } };
    auto expected_slots = other_slots;
    expected_slots.insert(expected_slots.begin(), missing_slot);
    assert(bench_source_centres(tier_three) == expected_slots);

    auto neutral_glyph = tier_three.clone();
    auto glyph = neutral_glyph(cv::Rect(642, 389, 18, 18));
    cv::Mat hsv, turquoise;
    cv::cvtColor(glyph, hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(73, 120, 125), cv::Scalar(111, 255, 255), turquoise);
    glyph.setTo(cv::Scalar(180, 180, 180), turquoise);
    assert(bench_source_centres(neutral_glyph) == other_slots);

    auto absent_sprite = tier_three.clone();
    absent_sprite(cv::Rect(647, 412, 68, 54)).setTo(cv::Scalar(105, 141, 173));
    assert(bench_source_centres(absent_sprite) == other_slots);

    std::cout << "bench sale hitbox and compact operator source frames passed; no live input\n";
}
