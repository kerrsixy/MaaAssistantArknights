#pragma once

#include <optional>
#include <string>
#include <vector>

#include <meojson/json.hpp>

#include "Common/AsstBattleDef.h"

namespace asst::blackflow
{
struct HunterBaitPlan
{
    int plan_index = -1;
    Point location;
    std::vector<Point> required_targets;
    std::vector<Point> forbidden_targets;
};

// Separate metadata keeps hunter evidence policy out of ordinary deployment layouts.
struct HunterOpening
{
    std::vector<HunterBaitPlan> bait_plans;
    int attempt_limit = 1;

    [[nodiscard]] bool valid() const;
    static std::optional<HunterOpening> parse(const json::value& stage, std::string* error = nullptr);
};
}
