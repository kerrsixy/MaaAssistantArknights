#include "Task/MiniGame/SPA/SPAProjection.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <opencv2/imgcodecs.hpp>

int main()
{
    using namespace asst::spa;
    // The last bench slot is visible behind the expanded-shop controls, but
    // its sprite-foot input is intercepted by freeze. Upper inventory slots
    // at the same x remain usable.
    assert(!equipment_inventory_probe_safe({ 963, 466 }));
    assert(!equipment_inventory_probe_safe({ 1050, 460 }));
    assert(!equipment_inventory_probe_safe({ 1190, 460 }));
    assert(equipment_inventory_probe_safe({ 963, 394 }));
    assert(equipment_inventory_probe_safe({ 883, 466 }));
    assert(equipment_inventory_probe_safe({ 873, 394 }));
    assert(equipment_inventory_probe_safe({ 923, 466 }));

    for (const auto* filename : { "inventory_freeze_before_live.png", "inventory_freeze_after_live.png" }) {
        const auto frame = cv::imread(filename);
        assert(!frame.empty());
        const auto candidates = equipment_inventory_candidates(frame);
        const auto contains = [&](cv::Point expected) {
            return std::any_of(candidates.begin(), candidates.end(), [&](auto point) {
                return cv::norm(point - expected) < 5;
            });
        };
        // Real trial2 fixtures preserve the actual raised-row equipment and
        // unobscured bench sprites while removing the proven freeze misclick.
        assert(contains({ 873, 394 }));
        assert(contains({ 640, 465 }));
        assert(contains({ 883, 466 }));
        assert(!contains({ 963, 466 }));
        assert(std::all_of(candidates.begin(), candidates.end(), equipment_inventory_probe_safe));
        std::cout << filename << ": " << candidates.size() << " safe candidates\n";
    }
    const auto city = cv::imread("equipment_inventory_city.png");
    const auto raised = cv::imread("equipment_inventory.png");
    const auto empty = cv::imread("equipment_inventory_empty.png");
    assert(!city.empty() && !raised.empty() && !empty.empty());
    const auto city_candidates = equipment_inventory_candidates(city);
    assert(city_candidates.size() == 2);
    assert(std::any_of(city_candidates.begin(), city_candidates.end(), [](auto point) {
        return cv::norm(point - cv::Point(923, 466)) < 4;
    }));
    const auto raised_candidates = equipment_inventory_candidates(raised);
    assert(std::any_of(raised_candidates.begin(), raised_candidates.end(), [](auto point) {
        return cv::norm(point - cv::Point(760, 400)) < 20;
    }));
    assert(equipment_inventory_candidates(empty).empty());
    std::cout << "Inventory HUD exclusion passed\n";
}
