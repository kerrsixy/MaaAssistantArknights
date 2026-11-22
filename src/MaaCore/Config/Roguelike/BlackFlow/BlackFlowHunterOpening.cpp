#include "BlackFlowHunterOpening.h"

#include <algorithm>
#include <set>

bool asst::blackflow::HunterOpening::valid() const
{
    if (bait_plans.empty() || bait_plans.size() > 4 || attempt_limit < 1 || attempt_limit > 4) {
        return false;
    }
    const auto inside = [](Point tile) {
        return tile.x >= 0 && tile.x < 10 && tile.y >= 0 && tile.y < 7;
    };
    std::set<int> indices;
    std::set<Point> coverage;
    for (const auto& plan : bait_plans) {
        if (plan.plan_index < 0 || !indices.insert(plan.plan_index).second || !inside(plan.location) ||
            plan.required_targets.empty()) {
            return false;
        }
        std::set<Point> required, forbidden;
        for (const auto target : plan.required_targets) {
            if ((target != Point(2, 2) && target != Point(4, 2)) || !required.insert(target).second) {
                return false;
            }
            coverage.insert(target);
        }
        for (const auto target : plan.forbidden_targets) {
            if (!inside(target) || required.contains(target) || !forbidden.insert(target).second) {
                return false;
            }
        }
    }
    return coverage.contains(Point(2, 2)) && coverage.contains(Point(4, 2));
}

std::optional<asst::blackflow::HunterOpening>
    asst::blackflow::HunterOpening::parse(const json::value& stage, std::string* error)
{
    const auto fail = [&](const std::string& message) -> std::optional<HunterOpening> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    if (error) {
        error->clear();
    }
    if (!stage.is_object() || stage.contains("hunter_bait_plan") == stage.contains("hunter_bait_plans")) {
        return fail("Exactly one scalar or list hunter opening declaration is required");
    }
    const auto plans = stage.find<json::array>("deploy_plan");
    if (!plans || !stage.get("deploy_plan_only", false) || !stage.get("reserve_deploy_cost", false)) {
        return fail("Hunter opening requires exclusive deployment plans and cost reservation");
    }
    if (stage.contains("retreat_plan") &&
        (!stage.at("retreat_plan").is_array() || !stage.at("retreat_plan").as_array().empty())) {
        return fail("Hunter bait retreat must be owned by the visual controller");
    }
    std::vector<int> indices;
    if (stage.contains("hunter_bait_plan")) {
        const auto index = stage.find<int>("hunter_bait_plan");
        if (!index) {
            return fail("Hunter bait plan index must be an integer");
        }
        indices.push_back(*index);
    }
    else {
        const auto declared = stage.find<json::array>("hunter_bait_plans");
        if (!declared || declared->empty() || declared->size() > 4) {
            return fail("Hunter bait plan list must contain one to four indices");
        }
        for (const auto& value : *declared) {
            if (!value.is<int>()) {
                return fail("Hunter bait plan list contains a non-integer index");
            }
            indices.push_back(value.as_integer());
        }
    }
    HunterOpening opening;
    std::set<int> seen;
    std::set<Point> coverage;
    const auto read_tile = [](const json::value& value) -> std::optional<Point> {
        if (!value.is_array() || value.as_array().size() != 2) {
            return std::nullopt;
        }
        const auto x = value.find<int>(0), y = value.find<int>(1);
        if (!x || !y || *x < 0 || *x >= 10 || *y < 0 || *y >= 7) {
            return std::nullopt;
        }
        return Point(*x, *y);
    };
    for (int index : indices) {
        if (index < 0 || static_cast<std::size_t>(index) >= plans->size() || !seen.insert(index).second) {
            return fail("Hunter bait plan indices must be unique and in range");
        }
        const auto& plan = (*plans)[index];
        const auto position = plan.find("location");
        const auto tile = position ? read_tile(*position) : std::nullopt;
        const auto required = plan.find<json::array>("required_attack_tiles");
        if (!tile || !required || required->empty() || !plan.get("exclude_starting_core", false)) {
            return fail("Each bait needs a valid tile, explicit target coverage and core exclusion");
        }
        HunterBaitPlan bait { .plan_index = index, .location = *tile };
        std::set<Point> unique;
        for (const auto& value : *required) {
            const auto target = read_tile(value);
            if (!target || (*target != Point(2, 2) && *target != Point(4, 2)) || !unique.insert(*target).second) {
                return fail("Hunter bait targets must be distinct native spawn tiles");
            }
            bait.required_targets.push_back(*target);
            coverage.insert(*target);
        }
        if (plan.contains("forbidden_attack_tiles")) {
            const auto forbidden = plan.find<json::array>("forbidden_attack_tiles");
            if (!forbidden) {
                return fail("Forbidden bait attack tiles must be an array");
            }
            unique.clear();
            for (const auto& value : *forbidden) {
                const auto target = read_tile(value);
                if (!target || !unique.insert(*target).second ||
                    std::ranges::find(bait.required_targets, *target) != bait.required_targets.end()) {
                    return fail("Forbidden bait attack tiles are invalid, repeated or contradictory");
                }
                bait.forbidden_targets.push_back(*target);
            }
        }
        opening.bait_plans.push_back(std::move(bait));
    }
    if (!coverage.contains(Point(2, 2)) || !coverage.contains(Point(4, 2))) {
        return fail("Hunter opening plans must collectively cover both mandatory spawns");
    }
    if (stage.contains("hunter_bait_attempt_limit")) {
        const auto limit = stage.find<int>("hunter_bait_attempt_limit");
        if (!limit) {
            return fail("Hunter bait attempt limit must be an integer");
        }
        opening.attempt_limit = *limit;
    }
    else {
        opening.attempt_limit = (*plans)[indices.front()].get("max_deployments", 1);
    }
    if (opening.attempt_limit < 1 || opening.attempt_limit > 4) {
        return fail("Hunter bait attempt limit is outside its supported bounds");
    }
    return opening;
}
