#pragma once

#include "SPADeploymentReview.h"
#include "SPAFormation.h"
#include "SPAGrowth.h"
#include "SPAPlacementPolicy.h"
#include <cstddef>
#include <set>

namespace asst::spa
{
inline std::optional<std::map<std::string, int>>
    same_row_growth_populations(const std::vector<GridPlacementMember>& members)
{
    if (members.empty() || members.size() > 8) {
        return std::nullopt;
    }
    std::vector<std::pair<int, int>> cells;
    for (const auto& member : members) {
        const std::pair cell { member.col, member.row };
        if (member.name.empty() || member.col < 0 || member.col > 10 || member.row < 0 || member.row > 4 ||
            std::find(cells.begin(), cells.end(), cell) != cells.end()) {
            return std::nullopt;
        }
        cells.push_back(cell);
    }
    std::map<std::string, int> result;
    for (const auto& source : members) {
        if (source.name == "罗比菈塔") {
            result[source.name] +=
                static_cast<int>(std::count_if(members.begin(), members.end(), [&](const auto& target) {
                    return source.row == target.row;
                }));
        }
    }
    return result;
}

// A preference is conditional on the registered cells and active pacts. It
// does not prove that a skill, status event or a preparation trigger occurred.
inline int growth_recipient_offset(const std::string& source)
{
    if (source == "青枳" || source == "佩佩") {
        return -1;
    }
    if (source == "空弦" || source == "渡桥" || source == "旅骨" || source == "帕拉斯" || source == "华法琳" ||
        source == "阿米娅" || source == "塞雷娅" || source == "凛御银灰") {
        return 1;
    }
    return 0;
}

inline bool spatial_growth_target(const std::string& source, const std::string& target, const std::string& pact)
{
    if (source == "Mon3tr") {
        const auto profile = growth_profiles().find(target);
        return profile != growth_profiles().end() && profile->second.kind == GrowthKind::CappedConditional &&
               profile->second.phase == GrowthPhase::BattleEvent &&
               std::max(profile->second.normal_cap, profile->second.elite_cap) > 0 &&
               is_primary_growth_engine(target, pact);
    }
    if (growth_recipient_offset(source) == -1) {
        static const auto pacts = operator_pacts();
        const auto member = pacts.find(target);
        return member != pacts.end() &&
               std::find(member->second.begin(), member->second.end(), pact) != member->second.end();
    }
    if (source == "凛御银灰") {
        const auto profile = growth_profiles().find(target);
        return (pact == "谢拉格" || pact == "灵巧") && is_primary_growth_engine(target, pact) &&
               profile != growth_profiles().end() && profile->second.kind == GrowthKind::CappedConditional &&
               profile->second.phase == GrowthPhase::BattleEvent;
    }
    return growth_recipient_offset(source) == 1 && is_front_growth_target(source, target, pact);
}

inline std::pair<int, int> growth_facing_step(char facing)
{
    return facing == 'E'   ? std::pair { 1, 0 }
           : facing == 'W' ? std::pair { -1, 0 }
           : facing == 'S' ? std::pair { 0, 1 }
           : facing == 'N' ? std::pair { 0, -1 }
                           : std::pair { 0, 0 };
}

inline int spatial_growth_pair_value(
    const std::string& source,
    const std::string& target,
    const std::string& pact,
    const std::string& primary)
{
    if (!spatial_growth_target(source, target, pact)) {
        return 0;
    }
    const bool improved_sale_quote = source == "旅骨" && pact == "伊比利亚" && has_iberia_sale_price_trait(target);
    return (pact == primary ? 1200 : 600) + (improved_sale_quote ? 600 : 0);
}

inline bool has_preparation_adjacency_growth(const std::string& name)
{
    const auto formula = growth_profiles().find(name);
    if (formula == growth_profiles().end() || !formula->second.dynamic_recipients ||
        growth_recipient_offset(name) == 0) {
        return false;
    }
    const auto& profile = formula->second;
    return (profile.phase == GrowthPhase::PreparationEnd || profile.phase == GrowthPhase::PreparationStart) &&
           (profile.kind == GrowthKind::FixedPhase || profile.kind == GrowthKind::SalePriceDependent);
}

// Relative priority for the eligible recipient; this is neither a sale
// quote nor a measurement of layers or the next battle's damage.
inline int
    primary_preparation_adjacency_value(const std::vector<GridPlacementMember>& members, const std::string& primary)
{
    if (primary.empty() || !same_row_growth_populations(members)) {
        return 0;
    }
    int result = 0;
    for (const auto& source : members) {
        const auto [dx, dy] = growth_facing_step(source.facing);
        if ((!dx && !dy) || !has_preparation_adjacency_growth(source.name)) {
            continue;
        }
        const int offset = growth_recipient_offset(source.name);
        const auto target = std::find_if(members.begin(), members.end(), [&](const auto& member) {
            return member.col == source.col + offset * dx && member.row == source.row + offset * dy;
        });
        if (target != members.end()) {
            result += spatial_growth_pair_value(source.name, target->name, primary, primary);
        }
    }
    return result;
}

// Count the contact and immediate approach cells monitored by a freeze
// stacker and covered by a different cold source. This is range geometry,
// not a prediction of skill casts, status immunity or actual stack gain.
inline int freeze_growth_coverage(
    const std::vector<DeploymentUnit>& units,
    const std::vector<DeploymentLane>& lanes,
    const std::set<std::string>& cold_sources,
    const std::set<std::string>& stackers)
{
    if (cold_sources.empty() || stackers.empty()) {
        return 0;
    }
    std::set<BoardCell> engagement;
    for (const auto& lane : lanes) {
        const auto contact = std::find_if(lane.path.begin(), lane.path.end(), [&](BoardCell cell) {
            return std::any_of(units.begin(), units.end(), [&](const auto& unit) {
                return unit.cell == cell && unit.frontline && unit.holds_position && !unit.healer &&
                       unit.block_count > 0;
            });
        });
        if (contact == lane.path.end()) {
            continue;
        }
        const auto preceding = std::min<std::ptrdiff_t>(lane.precontact_cells, contact - lane.path.begin());
        engagement.insert(contact - preceding, contact + 1);
    }
    int coverage = 0;
    for (const auto& stacker : units) {
        if (!stackers.contains(stacker.id)) {
            continue;
        }
        for (const auto cell : engagement) {
            if (range_covers(stacker, cell) &&
                std::any_of(units.begin(), units.end(), [&](const auto& source) {
                    return source.id != stacker.id && cold_sources.contains(source.id) && source.damage_output &&
                           range_covers(source, cell);
                })) {
                ++coverage;
            }
        }
    }
    return coverage;
}

// Directed giver/recipient relationships are counted once, including
// duplicate physical operators. This is a placement preference, not a claim
// that the conditional skill/event trigger or layer gain occurred.
inline int spatial_formation_value(
    const std::vector<GridPlacementMember>& members,
    const std::vector<std::string>& active_pacts,
    const std::string& primary)
{
    if (!same_row_growth_populations(members)) {
        return 0;
    }
    std::vector<std::string> active;
    for (const auto& pact : active_pacts) {
        if (std::find(active.begin(), active.end(), pact) == active.end()) {
            active.push_back(pact);
        }
    }
    int value = 0;
    for (const auto& source : members) {
        const auto [dx, dy] = growth_facing_step(source.facing);
        const int offset = growth_recipient_offset(source.name);
        for (const auto& target : members) {
            if (source.name == "罗比菈塔" && source.row == target.row) {
                for (const auto& pact : active) {
                    if (pact == "坚守" || pact == "助力") {
                        value += 600;
                    }
                }
            }
            const bool front_or_back = offset && (dx || dy) && target.col == source.col + offset * dx &&
                                       target.row == source.row + offset * dy;
            const bool aura = source.name == "Mon3tr" &&
                              std::max(std::abs(target.col - source.col), std::abs(target.row - source.row)) == 1;
            if (!front_or_back && !aura) {
                continue;
            }
            for (const auto& pact : active) {
                value += spatial_growth_pair_value(source.name, target.name, pact, primary);
            }
        }
    }
    return value;
}

inline std::vector<PlacementPreference> spatial_growth_preferences(
    const std::string& incoming,
    const std::vector<GridPlacementCell>& legal,
    const std::vector<GridPlacementMember>& placed,
    const std::vector<std::string>& active_pacts,
    const std::string& primary)
{
    const auto recipient_value = [&](const std::string& source, const std::string& target) {
        int value = 0;
        // An active pact is counted once even if a caller repeats its label.
        std::vector<std::string> counted;
        for (const auto& pact : active_pacts) {
            if (std::find(counted.begin(), counted.end(), pact) == counted.end()) {
                counted.push_back(pact);
                value += spatial_growth_pair_value(source, target, pact, primary);
            }
        }
        return value;
    };
    const bool row_layers_active = std::find(active_pacts.begin(), active_pacts.end(), "坚守") != active_pacts.end() ||
                                   std::find(active_pacts.begin(), active_pacts.end(), "助力") != active_pacts.end();
    std::vector<PlacementPreference> result;
    for (const auto& cell : legal) {
        if (std::any_of(placed.begin(), placed.end(), [&](const auto& member) {
                return member.col == cell.col && member.row == cell.row;
            })) {
            continue;
        }
        const int row_members = static_cast<int>(std::count_if(
                                    placed.begin(),
                                    placed.end(),
                                    [&](const auto& member) { return member.row == cell.row; })) +
                                1;
        for (const char facing : { 'E', 'N', 'S', 'W' }) {
            int bonus = 0;
            const auto [dx, dy] = growth_facing_step(facing);
            const int relative = growth_recipient_offset(incoming);
            if (row_layers_active && incoming == "罗比菈塔") {
                bonus += row_members * 600;
            }
            for (const auto& member : placed) {
                const bool neighboring =
                    std::max(std::abs(member.col - cell.col), std::abs(member.row - cell.row)) == 1;
                // Mon3tr raises battle caps in all eight neighboring cells;
                // the aura has no facing, and uncapped/rest-only sources do
                // not gain an artificial per-battle stacking opportunity.
                if (neighboring && incoming == "Mon3tr") {
                    bonus += recipient_value(incoming, member.name);
                }
                if (neighboring && member.name == "Mon3tr") {
                    bonus += recipient_value(member.name, incoming);
                }
                if (relative != 0 && member.col == cell.col + relative * dx && member.row == cell.row + relative * dy) {
                    bonus += recipient_value(incoming, member.name);
                }
                const auto [sx, sy] = growth_facing_step(member.facing);
                const int source_relative = growth_recipient_offset(member.name);
                // Unknown facing must not invent an existing helper's target.
                if ((sx != 0 || sy != 0) && source_relative != 0 && cell.col == member.col + source_relative * sx &&
                    cell.row == member.row + source_relative * sy) {
                    bonus += recipient_value(member.name, incoming);
                }
                if (row_layers_active && member.name == "罗比菈塔" && member.row == cell.row) {
                    bonus += 600;
                }
            }
            if (bonus > 0) {
                result.push_back({ cell.screen_x, cell.screen_y, facing, bonus });
            }
        }
    }
    return result;
}

inline char replacement_growth_facing(
    const std::string& incoming,
    const GridPlacementCell& target,
    const std::vector<GridPlacementMember>& others,
    const std::vector<std::string>& active_pacts,
    const std::string& primary,
    char current)
{
    const auto preferences = spatial_growth_preferences(incoming, { target }, others, active_pacts, primary);
    const auto score = [&](char direction) {
        int result = 0;
        for (const auto& preference : preferences) {
            if (preference.facing == direction) {
                result += preference.bonus;
            }
        }
        return result;
    };
    char selected = current;
    int best = score(current);
    for (const char direction : { 'E', 'N', 'S', 'W' }) {
        if (const int value = score(direction); value > best) {
            best = value;
            selected = direction;
        }
    }
    return selected;
}

struct SpatialReorientation
{
    std::string name;
    int col = 0;
    int row = 0;
    char previous = '\0';
    char direction = '\0';
    int growth_improvement = 0;
    int combined_improvement = 0;
};

// Turn one existing helper only when adjacency improves and the combined
// growth/attack/healing score also improves. Equal scores retain its facing.
inline std::optional<SpatialReorientation> choose_spatial_reorientation(
    const std::vector<GridPlacementMember>& members,
    const std::vector<std::string>& active_pacts,
    const std::string& primary,
    const std::map<std::string, std::vector<std::pair<int, int>>>& ranges)
{
    if (!same_row_growth_populations(members)) {
        return std::nullopt;
    }
    std::optional<SpatialReorientation> best;
    for (const auto& source : members) {
        const auto range = ranges.find(source.name);
        if (growth_recipient_offset(source.name) == 0 || growth_facing_step(source.facing) == std::pair { 0, 0 } ||
            source.role == PlacementRole::Unknown || range == ranges.end() || range->second.empty() ||
            std::count_if(members.begin(), members.end(), [&](const auto& member) {
                return member.name == source.name;
            }) != 1) {
            continue;
        }
        std::vector<GridPlacementMember> others;
        for (const auto& member : members) {
            if (member.col != source.col || member.row != source.row) {
                others.push_back(member);
            }
        }
        const GridPlacementCell cell { source.col, source.row, 0, 0 };
        const auto growth = spatial_growth_preferences(source.name, { cell }, others, active_pacts, primary);
        const auto coverage = range_placement_preferences(
            source.role == PlacementRole::Healer ? PlacementRole::Healer : PlacementRole::Ranged,
            { cell },
            others,
            range->second);
        const auto score = [](const auto& preferences, char direction) {
            int value = 0;
            for (const auto& preference : preferences) {
                if (preference.facing == direction) {
                    value += preference.bonus;
                }
            }
            return value;
        };
        const int current_growth = score(growth, source.facing);
        const int current_score = current_growth + score(coverage, source.facing);
        for (const char direction : { 'E', 'N', 'S', 'W' }) {
            const int growth_gain = score(growth, direction) - current_growth;
            const int improvement = score(growth, direction) + score(coverage, direction) - current_score;
            if (growth_gain > 0 && improvement > 0 && (!best || improvement > best->combined_improvement)) {
                best = SpatialReorientation { source.name, source.col,  source.row, source.facing,
                                              direction,   growth_gain, improvement };
            }
        }
    }
    return best;
}
}
