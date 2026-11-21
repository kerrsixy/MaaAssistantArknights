#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace asst::spa
{
// Return original slot indices so a fresh badge scan after panel dismissal
// can recover the same slot even when the probing order was different.
inline std::vector<std::size_t>
    bench_source_probe_order(const std::vector<int>& source_x, std::optional<int> preferred_x = std::nullopt)
{
    std::vector<std::size_t> order(source_x.size());
    std::iota(order.begin(), order.end(), 0);
    if (preferred_x) {
        std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
            return std::abs(source_x[left] - *preferred_x) < std::abs(source_x[right] - *preferred_x);
        });
    }
    return order;
}

enum class PlacementRole
{
    Frontline,
    Ranged,
    Healer,
    Unknown
};

// Dismissing a panel can add/remove a noisy badge candidate without moving
// the requested inventory slot. Reacquire that unique occupied position;
// an equal candidate count or an old vector index is not an identity proof.
inline std::optional<std::size_t>
    reacquire_bench_source(const std::vector<std::pair<int, int>>& candidates, std::pair<int, int> expected)
{
    std::optional<std::size_t> result;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const auto dx = static_cast<long long>(candidates[index].first) - expected.first;
        const auto dy = static_cast<long long>(candidates[index].second) - expected.second;
        if (std::abs(dx) > 24 || std::abs(dy) > 24 || dx * dx + dy * dy > 24 * 24) {
            continue;
        }
        if (result) {
            return std::nullopt;
        }
        result = index;
    }
    return result;
}

struct PlacementMember
{
    int x;
    int y;
    PlacementRole role;
    std::string name;
    char facing = '\0';
    bool accepts_direct_healing = true;
};

inline bool held_cell_has_member(int x, int y, const std::vector<PlacementMember>& placed)
{
    return std::any_of(placed.begin(), placed.end(), [&](const auto& member) {
        const int dx = member.x - x, dy = member.y - y;
        return dx * dx + dy * dy < 2500;
    });
}

// Preferences are constructed from a registered board, not an assumed pixel
// tile size. They influence scoring without creating additional legal cells.
struct PlacementPreference
{
    int x;
    int y;
    char facing;
    int bonus;
    bool healing_coverage = false;
};

struct GridPlacementCell
{
    int col;
    int row;
    int screen_x;
    int screen_y;
};

struct GridPlacementMember
{
    int col;
    int row;
    PlacementRole role;
    std::string name;
    char facing = '\0';
    bool accepts_direct_healing = true;
};

inline bool needs_screened_position(std::string_view name)
{
    return name == "Miss.Christine" || name == "灰喉" || name == "矩";
}

// Paths are ordered from spawn to home. Screen x/y alone cannot establish
// which unit is encountered first on a bent route.
inline bool support_precedes_frontline(
    std::pair<int, int> cell,
    const std::vector<GridPlacementMember>& placed,
    const std::vector<std::vector<std::pair<int, int>>>& paths,
    bool require_cover = false)
{
    for (const auto& path : paths) {
        const auto target = std::find(path.begin(), path.end(), cell);
        if (target == path.end()) {
            continue;
        }
        auto contact = path.end();
        for (const auto& unit : placed) {
            if (unit.role == PlacementRole::Frontline) {
                const auto found = std::find(path.begin(), path.end(), std::pair { unit.col, unit.row });
                if (found < contact) {
                    contact = found;
                }
            }
        }
        if ((contact == path.end() && require_cover) || (contact != path.end() && target < contact)) {
            return true;
        }
    }
    return false;
}

// Local ranges use the game's y-up convention; board and screen rows increase downward.
inline std::pair<int, int> rotate_range_offset(int x, int y, char facing)
{
    switch (facing) {
    case 'W':
        return { -x, y };
    case 'N':
        return { -y, -x };
    case 'S':
        return { y, x };
    default:
        return { x, -y };
    }
}

inline bool grid_range_covers(
    int source_col,
    int source_row,
    char facing,
    int target_col,
    int target_row,
    const std::vector<std::pair<int, int>>& attack_range)
{
    if (facing != 'E' && facing != 'W' && facing != 'N' && facing != 'S') {
        return false;
    }
    return std::any_of(attack_range.begin(), attack_range.end(), [&](const auto& offset) {
        const auto [dx, dy] = rotate_range_offset(offset.first, offset.second, facing);
        return source_col + dx == target_col && source_row + dy == target_row;
    });
}

// Score the other direction as well: a blocker should stand where existing
// ranged allies can attack its blocked enemies. The blocker's own facing
// does not change an existing ally's range. Unknown ally facings give no
// coverage, and the caller still supplies only legal ground-route targets.
inline std::vector<PlacementPreference> frontline_support_preferences(
    const std::vector<GridPlacementCell>& legal,
    const std::vector<GridPlacementMember>& placed,
    const std::map<std::string, std::vector<std::pair<int, int>>>& ranges)
{
    std::vector<PlacementPreference> preferences;
    for (const auto& cell : legal) {
        int score = 0;
        for (const auto& member : placed) {
            const auto range = ranges.find(member.name);
            if (member.role == PlacementRole::Ranged && range != ranges.end() &&
                grid_range_covers(member.col, member.row, member.facing, cell.col, cell.row, range->second)) {
                score += 3000;
            }
        }
        if (score > 0) {
            for (const char facing : { 'E', 'N', 'S', 'W' }) {
                preferences.push_back({ cell.screen_x, cell.screen_y, facing, score });
            }
        }
    }
    return preferences;
}

inline std::vector<PlacementPreference> range_placement_preferences(
    PlacementRole role,
    const std::vector<GridPlacementCell>& legal,
    const std::vector<GridPlacementMember>& placed,
    const std::vector<std::pair<int, int>>& attack_range,
    bool provides_regeneration = false)
{
    std::vector<PlacementPreference> preferences;
    if ((role != PlacementRole::Healer && role != PlacementRole::Ranged) || attack_range.empty()) {
        return preferences;
    }
    for (const auto& cell : legal) {
        for (const char facing : { 'E', 'N', 'S', 'W' }) {
            int score = 0;
            for (const auto& member : placed) {
                if (role == PlacementRole::Healer && !member.accepts_direct_healing && !provides_regeneration) {
                    continue;
                }
                if (role == PlacementRole::Ranged && member.role != PlacementRole::Frontline) {
                    continue;
                }
                const bool covered =
                    grid_range_covers(cell.col, cell.row, facing, member.col, member.row, attack_range);
                if (covered) {
                    score += member.role == PlacementRole::Frontline ? 3000 : 400;
                }
            }
            if (score > 0) {
                preferences.push_back({ cell.screen_x, cell.screen_y, facing, score, role == PlacementRole::Healer });
            }
        }
    }
    return preferences;
}

// Coordinates belong to the held-card camera. Only live legal candidates are
// accepted. This fallback arranges support behind blockers; it does not claim
// that unobserved enemy routes or attack ranges have been verified.
inline std::optional<std::pair<int, int>> choose_role_placement(
    PlacementRole role,
    const std::vector<std::pair<int, int>>& legal,
    const std::vector<PlacementMember>& placed,
    const std::vector<std::pair<int, int>>& attack_range = {},
    char* direction = nullptr,
    const std::vector<PlacementPreference>& preferences = {},
    bool registered_range_scoring = false,
    bool provides_regeneration = false)
{
    if (legal.empty()) {
        return std::nullopt;
    }
    int min_x = legal.front().first, max_x = min_x;
    int min_y = legal.front().second, max_y = min_y;
    for (const auto& [x, y] : legal) {
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
    }
    const int middle_y = (min_y + max_y) / 2;
    int upper = 0, lower = 0;
    for (const auto& unit : placed) {
        if (unit.role == role || (role == PlacementRole::Frontline && unit.role == PlacementRole::Frontline)) {
            (unit.y <= middle_y ? upper : lower)++;
        }
    }
    const int target_y = role == PlacementRole::Healer ? middle_y : upper <= lower ? min_y : max_y;
    int target_x = role == PlacementRole::Frontline ? max_x : std::max(min_x, max_x - 160);
    if (role != PlacementRole::Frontline) {
        const PlacementMember* anchor = nullptr;
        for (const auto& unit : placed) {
            if (unit.role == PlacementRole::Frontline &&
                (!anchor || std::abs(unit.y - target_y) < std::abs(anchor->y - target_y))) {
                anchor = &unit;
            }
        }
        if (anchor) {
            target_x = std::max(min_x, anchor->x - 160);
        }
    }
    std::optional<std::pair<int, int>> best;
    std::pair<int, int> best_score;
    char best_direction = 'E';
    for (const auto& point : legal) {
        if (std::any_of(placed.begin(), placed.end(), [&](const auto& unit) {
                return std::abs(unit.x - point.first) < 40 && std::abs(unit.y - point.second) < 40;
            })) {
            continue;
        }
        for (const char facing : { 'E', 'N', 'S', 'W' }) {
            const bool spatial_facing =
                std::any_of(preferences.begin(), preferences.end(), [&](const auto& preference) {
                    return preference.x == point.first && preference.y == point.second && preference.facing == facing &&
                           preference.bonus > 0;
                });
            if ((role == PlacementRole::Frontline || attack_range.empty()) && facing != 'E' && !spatial_facing) {
                continue;
            }
            int score = -std::abs(point.first - target_x) - 2 * std::abs(point.second - target_y);
            int healing = 0;
            score += facing == 'E' ? 10 : 0;
            for (const auto& preference : preferences) {
                if (point.first == preference.x && point.second == preference.y && facing == preference.facing) {
                    (role == PlacementRole::Healer && preference.healing_coverage ? healing : score) +=
                        preference.bonus;
                }
            }
            // A measured legal interior point is not a registered tile. Use
            // tolerant projected range coverage only as a placement score;
            // the accepted position/facing are reconciled by the executor.
            for (const auto& unit : placed) {
                if (registered_range_scoring || role == PlacementRole::Frontline) {
                    continue;
                }
                if (role != PlacementRole::Healer && unit.role != PlacementRole::Frontline) {
                    continue;
                }
                if (role == PlacementRole::Healer && !unit.accepts_direct_healing && !provides_regeneration) {
                    continue;
                }
                bool covers = false;
                for (const auto& [dx, dy] : attack_range) {
                    const auto [rx, ry] = rotate_range_offset(dx, dy, facing);
                    covers = covers || (std::abs(point.first + rx * 80 - unit.x) < 40 &&
                                        std::abs(point.second + ry * 70 - unit.y) < 35);
                }
                if (covers) {
                    if (role == PlacementRole::Healer) {
                        healing += unit.role == PlacementRole::Frontline ? 3000 : 400;
                    }
                    else {
                        score += 1500;
                    }
                }
            }
            const std::pair ranked { healing, score };
            if (!best || ranked > best_score) {
                best = point;
                best_score = ranked;
                best_direction = facing;
            }
        }
    }
    if (best && direction) {
        *direction = best_direction;
    }
    return best;
}
}
