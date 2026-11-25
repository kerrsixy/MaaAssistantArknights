#include "BlackFlowHunterTaskPolicy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>

namespace asst::blackflow
{
bool BlackFlowHunterTaskPolicy::load(const json::value& value, std::string* error)
{
    const auto integer_field = [](const json::value& field) {
        if (!field.is_number()) {
            return false;
        }
        const auto number = field.as_double();
        return std::isfinite(number) && std::floor(number) == number && number >= std::numeric_limits<int>::min() &&
               number <= std::numeric_limits<int>::max();
    };
    std::map<int, std::vector<Rule>> parsed;
    const auto fail = [&](const char* text) {
        if (error) {
            *error = text;
        }
        return false;
    };
    if (!value.is_object() || value.get("schema_version", 0) != 1 ||
        value.get("source_revision", std::string()).empty()) {
        return fail("Hunter task facts lack versioned provenance");
    }
    const auto rules = value.find<json::array>("rules");
    if (!rules || rules->empty() || rules->size() > 16) {
        return fail("Hunter task facts have no bounded rule list");
    }
    for (const auto& row : *rules) {
        const auto plan = row.find<int>("plan_index"), min = row.find<int>("min_elite"),
                   max = row.find<int>("max_elite"), skill = row.find<int>("selected_skill");
        const auto point = row.find<json::array>("location");
        const auto cells = row.find<json::array>("base_right_range");
        const auto radius = row.find<double>("self_effect_radius");
        const auto kind = row.get("kind", std::string());
        if (!plan || *plan < 0 || !min || !max || *min < 0 || *max > 2 || *max < *min || !skill || *skill < 1 ||
            *skill > 3 || !point || point->size() != 2 || !integer_field((*point)[0]) || !integer_field((*point)[1]) ||
            !cells || cells->empty() || cells->size() > 128 || (kind == "guard" && !radius) ||
            (radius && (!std::isfinite(*radius) || *radius < 0 || *radius > 5)) ||
            (kind != "medical" && kind != "guard" && kind != "aoe_risk")) {
            return fail("Hunter task rule has invalid phase, geometry or effect bounds");
        }
        Rule rule;
        rule.plan_index = *plan;
        rule.kind = kind == "medical" ? HunterTaskKind::Medical
                    : kind == "guard" ? HunterTaskKind::Guard
                                      : HunterTaskKind::AoeRisk;
        rule.location = { (*point)[0].as_integer(), (*point)[1].as_integer() };
        if (rule.location.x < 0 || rule.location.x >= 10 || rule.location.y < 0 || rule.location.y >= 7) {
            return fail("Hunter task location is outside the verified stage");
        }
        const auto direction = row.get("direction", std::string());
        if (direction != "left" && direction != "right" && direction != "up" && direction != "down") {
            return fail("Hunter task direction is invalid");
        }
        rule.direction = direction == "left"    ? battle::DeployDirection::Left
                         : direction == "right" ? battle::DeployDirection::Right
                         : direction == "up"    ? battle::DeployDirection::Up
                                                : battle::DeployDirection::Down;
        rule.operator_id = row.get("operator_id", std::string());
        rule.min_elite = *min;
        rule.max_elite = *max;
        rule.selected_skill = *skill;
        rule.min_skill_rank = row.get("min_skill_rank", 1);
        rule.self_effect_radius = radius;
        if (rule.operator_id.empty() || rule.min_skill_rank < 1 || rule.min_skill_rank > 10) {
            return fail("Hunter task identity or skill rank is invalid");
        }
        for (const auto& cell : *cells) {
            if (!cell.is_array() || cell.as_array().size() != 2 || !integer_field(cell.at(0)) ||
                !integer_field(cell.at(1))) {
                return fail("Hunter task range grid is invalid");
            }
            rule.base_right_range.emplace_back(cell.at(0).as_integer(), cell.at(1).as_integer());
        }
        const auto found = parsed.find(*plan);
        if (found != parsed.end() &&
            (found->second.front().kind != rule.kind || found->second.front().location != rule.location ||
             found->second.front().direction != rule.direction)) {
            return fail("Hunter task alternatives disagree about role or location");
        }
        parsed[*plan].push_back(std::move(rule));
    }
    m_rules.swap(parsed);
    return true;
}

bool BlackFlowHunterTaskPolicy::matches(const Rule& rule, const HunterTaskCandidate& candidate, bool position) const
{
    if (!candidate.preparation_known || candidate.oper.name.empty() || candidate.operator_id != rule.operator_id ||
        candidate.elite < rule.min_elite || candidate.elite > rule.max_elite ||
        candidate.selected_skill != rule.selected_skill || candidate.skill_rank < rule.min_skill_rank ||
        (position && (candidate.location != rule.location || candidate.direction != rule.direction))) {
        return false;
    }
    auto expected = rule.base_right_range;
    const int rotations = candidate.direction == battle::DeployDirection::Right  ? 0
                          : candidate.direction == battle::DeployDirection::Up   ? 1
                          : candidate.direction == battle::DeployDirection::Left ? 2
                          : candidate.direction == battle::DeployDirection::Down ? 3
                                                                                 : -1;
    if (rotations < 0) {
        return false;
    }
    for (auto& p : expected) {
        for (int i = 0; i < rotations; ++i) {
            p = { p.y, -p.x };
        }
    }
    return expected.size() == candidate.range.size() && std::ranges::all_of(expected, [&](Point p) {
               return std::ranges::find(candidate.range, p) != candidate.range.end();
           });
}

bool BlackFlowHunterTaskPolicy::has_plan(int plan_index) const noexcept
{
    return m_rules.contains(plan_index);
}

HunterTaskKind BlackFlowHunterTaskPolicy::kind(int plan_index) const
{
    return m_rules.at(plan_index).front().kind;
}

Point BlackFlowHunterTaskPolicy::location(int plan_index) const
{
    return m_rules.at(plan_index).front().location;
}

HunterTaskPermit BlackFlowHunterTaskPolicy::assess(
    int plan_index,
    const HunterTaskCandidate& candidate,
    const HunterTaskWorld& world) const
{
    const auto found = m_rules.find(plan_index);
    if (found == m_rules.end() || !world.normal_verified || !world.spawn_contract_verified ||
        !world.first_bait_consumed || !world.bait_removal_verified) {
        return { false, false, "No current scoped hunter-task evidence" };
    }
    const auto rule = std::ranges::find_if(found->second, [&](const Rule& r) { return matches(r, candidate, true); });
    if (rule == found->second.end()) {
        return { false, false, "Unmodeled identity, phase, selected skill, range or location" };
    }
    if (rule->kind == HunterTaskKind::Medical) {
        return { world.guard_present,
                 false,
                 world.guard_present ? "Known nonoffensive healing for upper guard"
                                     : "No actual approved upper guard patient" };
    }
    if (rule->kind == HunterTaskKind::Guard) {
        const bool overlap = !guard_effect_domain_allowed(*rule, candidate, world);
        return { !overlap,
                 false,
                 overlap ? "Guard base/effect domain intersects unresolved hunter domain"
                         : "Versioned guard effect excludes current conservative hunter domain" };
    }
    const bool legacy_spawns_clear = std::ranges::all_of(world.spawns, [](const auto& spawn) {
        return spawn.presence == HunterPresence::Absent && spawn.distinct_empty_frames >= 2;
    });
    const bool fresh_upper_clear =
        world.upper_visible && world.upper_presence == HunterPresence::Absent && world.upper_empty_frames >= 2;
    const bool positive_spawn =
        std::ranges::any_of(world.spawns, [](const auto& spawn) { return spawn.presence == HunterPresence::Present; });
    // The independent scoped detector can cover both legacy patches even when
    // their background scores stay Unknown. Positive foreground evidence still vetoes.
    const bool spawns_clear =
        !positive_spawn && (legacy_spawns_clear || (world.upper_scope_covers_spawn_rois && fresh_upper_clear));
    const bool lower_known = world.lower_visible && world.lower_presence != HunterPresence::Unknown;
    const bool allowed = world.guard_present && spawns_clear && fresh_upper_clear && lower_known;
    return {
        allowed,
        allowed,
        allowed
            ? (world.medical_present
                   ? "Accepted scoped lower/AoE risk with guard and observed medical support; NOT clearance or HPS "
                     "proof"
                   : "Accepted scoped lower/AoE risk with guard; medical recommended, sustain unknown; NOT clearance")
            : "Risk release needs fresh clear upper covering both spawns (or legacy absence), known lower and actual "
              "guard"
    };
}

bool BlackFlowHunterTaskPolicy::guard_effect_domain_allowed(
    const Rule& rule,
    const HunterTaskCandidate& candidate,
    const HunterTaskWorld& world) const
{
    auto uncertain = world.lower_possible_tiles;
    uncertain.insert(
        uncertain.end(),
        world.upper_positive_possible_tiles.begin(),
        world.upper_positive_possible_tiles.end());
    for (std::size_t i = 0; i < world.spawns.size(); ++i) {
        if (world.spawns[i].presence != HunterPresence::Absent || world.spawns[i].distinct_empty_frames < 2) {
            uncertain.push_back(world.spawn_tiles[i]);
        }
    }
    const bool overlap = std::ranges::any_of(uncertain, [&](Point p) {
        return std::ranges::find(candidate.range, p - candidate.location) != candidate.range.end() ||
               std::hypot(p.x - candidate.location.x, p.y - candidate.location.y) <= *rule.self_effect_radius;
    });
    return !overlap;
}

std::vector<HunterGuardTask> BlackFlowHunterTaskPolicy::guard_tasks() const
{
    std::vector<HunterGuardTask> result;
    for (const auto& [index, rules] : m_rules) {
        if (!rules.empty() && rules.front().kind == HunterTaskKind::Guard) {
            result.push_back({ index, rules.front().location, rules.front().direction });
        }
    }
    return result;
}

bool BlackFlowHunterTaskPolicy::guard_identity_required(
    const HunterTaskCandidate& requested,
    const std::vector<HunterTaskCandidate>& projected,
    const std::vector<int>& satisfied_plans,
    const HunterTaskWorld& world) const
{
    for (const auto& [index, rules] : m_rules) {
        if (rules.empty() || rules.front().kind != HunterTaskKind::Guard ||
            std::ranges::find(satisfied_plans, index) != satisfied_plans.end()) {
            continue;
        }
        const bool can_serve =
            std::ranges::any_of(rules, [&](const Rule& rule) { return matches(rule, requested, false); });
        if (!can_serve) {
            continue;
        }
        const bool other_qualified = std::ranges::any_of(projected, [&](const HunterTaskCandidate& other) {
            return world.normal_verified && world.spawn_contract_verified && other.oper != requested.oper &&
                   std::ranges::any_of(rules, [&](const Rule& rule) {
                       return matches(rule, other, true) && guard_effect_domain_allowed(rule, other, world);
                   });
        });
        if (!other_qualified) {
            return true;
        }
    }
    return false;
}

bool BlackFlowHunterTaskPolicy::reserve_guard_identity(const HunterTaskCandidate& candidate) const
{
    if (!candidate.sole_persistent_blocker) {
        return false;
    }
    for (const auto& [index, rules] : m_rules) {
        static_cast<void>(index);
        for (const auto& rule : rules) {
            if (rule.kind == HunterTaskKind::Guard && matches(rule, candidate, false)) {
                return true;
            }
        }
    }
    return false;
}

bool BlackFlowHunterTaskPolicy::skill_known_allowed(const HunterTaskCandidate& candidate) const
{
    for (const auto& [index, rules] : m_rules) {
        static_cast<void>(index);
        for (const auto& rule : rules) {
            if (matches(rule, candidate, true)) {
                return true;
            }
        }
    }
    return false;
}
}
