#pragma once

#include "SPADeploymentReview.h"
#include "SPAPlacementPolicy.h"
#include "SPASpatialGrowthPolicy.h"
#include <array>
#include <cstdlib>

namespace asst::spa
{
enum class SummonPlacement
{
    Anywhere,
    OwnerFront,
    OwnerRange,
    SkillTacticalPoint,
    AllyTarget,
    CopiesOperator,
    Generated,
    QueueMarker
};

struct SummonTarget
{
    BoardCell cell;
    char facing = '\0';
};

struct SummonRule
{
    std::string_view name;
    std::string_view owner;
    SummonPlacement placement;
    std::string_view tactical_use;
    bool preparation_executor = false;
};

// Season operator talents and skills plus BattleData token display names.
// A deployable entity is tracked outside the ordinary roster. Capacity and
// the actual availability phase must still be checked in the mode's HUD.
inline constexpr std::array<SummonRule, 24> SummonRules { {
    { "机动盾牌", "涤火杰西卡", SummonPlacement::OwnerFront, "Frontline shield", true },
    { "樱桃三号", "渡桥", SummonPlacement::OwnerRange, "Intercept and create salvage on defeat", true },
    { "狼群", "伺夜", SummonPlacement::OwnerRange, "Intercept; grows from two to three wolf shadows", true },
    { "工程蓄水炮",
      "温蒂",
      SummonPlacement::Anywhere,
      "Block zero; push enemies and support Weedy's skill nearby",
      true },
    { "蓄水炮", "温蒂", SummonPlacement::Anywhere, "Block zero; push enemies and support Weedy's skill nearby", true },
    { "全自动造型仪", "罗比菈塔", SummonPlacement::AllyTarget, "Protect a ground ally with defense and a shield" },
    { "爬行号·防护单元", "凯瑟琳", SummonPlacement::AllyTarget, "Regenerating barrier on an ally" },
    { "幻影", "夜莺", SummonPlacement::Anywhere, "Block zero; attract attacks while losing health" },
    { "风雪之眼", "凛御银灰", SummonPlacement::QueueMarker, "Deployment queue positional bonus" },
    { "重构体", "Mon3tr", SummonPlacement::OwnerRange, "Extend treatment and buff nearby allies" },
    { "指挥中心", "可露希尔", SummonPlacement::SkillTacticalPoint, "Skill-dependent tactical point and reinforcement" },
    { "“打字机”", "鸿雪", SummonPlacement::Anywhere, "Independent damage and defense reduction; nearby is stronger" },
    { "斯卡蒂的海嗣", "浊心斯卡蒂", SummonPlacement::Anywhere, "Extend support, healing and skill range" },
    { "结构性原理", "机械师", SummonPlacement::Anywhere, "Temporary independently redeployable unit" },
    { "流形", "缪尔赛思", SummonPlacement::CopiesOperator, "Select copying target before scoring block or damage" },
    { "铁钳号·原型机",
      "白铁",
      SummonPlacement::Anywhere,
      "Allied attacks charge area damage; skill spends its health",
      true },
    { "“小自在”", "夕", SummonPlacement::Generated, "Automatic temporary interception on a ground enemy target" },
    { "纸偶", "风丸", SummonPlacement::Generated, "Doll substitution or nearby attacking duplicate" },
    { "旧日残影", "黑键", SummonPlacement::Generated, "Skill trap for pull and area damage" },
    { "黄金盟誓", "维娜·维多利亚", SummonPlacement::Generated, "Skill-created melee interception for true damage" },
    { "“耀阳”", "耀骑士临光", SummonPlacement::Generated, "Skill-created interception and true damage" },
    { "本能的召唤", "酒神", SummonPlacement::SkillTacticalPoint, "Skill-only lure prioritizing elites or bosses" },
    { "浮游放逐舱", "克莱门莎", SummonPlacement::Generated, "Skill-created displacement according to enemy weight" },
    { "心烛", "隐德来希", SummonPlacement::Generated, "Links enemy health to damage dealt to the candle" },
} };

inline const SummonRule* summon_rule(std::string_view name)
{
    const auto found =
        std::find_if(SummonRules.begin(), SummonRules.end(), [&](const auto& rule) { return rule.name == name; });
    return found == SummonRules.end() ? nullptr : &*found;
}

inline std::string_view deployable_summon_owner(std::string_view summon)
{
    const auto rule = summon_rule(summon);
    return rule && rule->preparation_executor ? rule->owner : std::string_view {};
}

inline std::vector<BoardCell> summon_placement_cells(
    std::string_view summon,
    const GridPlacementMember& owner,
    const std::vector<BoardCell>& attack_range,
    const std::vector<BoardCell>& buildable,
    const std::vector<GridPlacementMember>& allies,
    const std::vector<DeploymentLane>& lanes,
    bool allow_unknown_facing_for_preview = false)
{
    if (deployable_summon_owner(summon) != owner.name || owner.name.empty()) {
        return {};
    }
    const bool water_cannon = owner.name == "温蒂";
    const bool anywhere = water_cannon || owner.name == "白铁";
    const std::string_view all_facings = "EWNS";
    const bool facing_known = all_facings.find(owner.facing) != std::string_view::npos;
    if (!anywhere && !facing_known && !allow_unknown_facing_for_preview) {
        return {};
    }
    // With no observed facing, these are candidates for the game's held-card
    // preview. Release still requires green inside the selected registered cell.
    const auto facings = facing_known ? std::string_view(&owner.facing, 1) : all_facings;
    std::vector<BoardCell> candidates;
    for (const auto& cell : buildable) {
        if (cell == BoardCell { owner.col, owner.row } ||
            std::any_of(allies.begin(), allies.end(), [&](const auto& ally) {
                return cell == BoardCell { ally.col, ally.row };
            })) {
            continue;
        }
        if (anywhere || std::any_of(facings.begin(), facings.end(), [&](char facing) {
                if (summon == "机动盾牌") {
                    const auto [dx, dy] = growth_facing_step(facing);
                    return cell == BoardCell { owner.col + dx, owner.row + dy };
                }
                return grid_range_covers(owner.col, owner.row, facing, cell.first, cell.second, attack_range);
            })) {
            candidates.push_back(cell);
        }
    }
    const auto lane_score = [&](BoardCell cell) {
        int value = 0;
        for (const auto& lane : lanes) {
            const auto pos = std::find(lane.path.begin(), lane.path.end(), cell);
            if (pos == lane.path.end()) {
                continue;
            }
            // A summon blocks a real approach; favor enemy-side cells over
            // collecting an arbitrary empty spot closer to the blue gate.
            value += 1000 - static_cast<int>(pos - lane.path.begin()) * 10;
        }
        return value;
    };
    std::stable_sort(candidates.begin(), candidates.end(), [&](const auto& lhs, const auto& rhs) {
        return lane_score(lhs) > lane_score(rhs);
    });
    return candidates;
}

inline char white_iron_summon_facing(BoardCell cell, char owner_facing, const std::vector<DeploymentLane>& lanes)
{
    char best = owner_facing;
    int best_score = 0;
    for (const char facing : std::string_view("ENSW")) {
        const auto [dx, dy] = growth_facing_step(facing);
        int score = 0;
        for (const auto& lane : lanes) {
            for (const auto& approach : lane.path) {
                const int col = approach.first - cell.first, row = approach.second - cell.second;
                const int distance = std::abs(col) + std::abs(row);
                if (distance <= 3 && col * dx + row * dy > 0) {
                    score += lane.blocking_weight * (4 - distance);
                }
            }
        }
        if (score > best_score || (score == best_score && facing == owner_facing)) {
            best = facing;
            best_score = score;
        }
    }
    return best;
}

inline std::vector<SummonTarget> rank_water_cannon_targets(
    const std::vector<BoardCell>& candidates,
    const GridPlacementMember& owner,
    const std::vector<GridPlacementMember>& allies,
    const std::vector<DeploymentLane>& lanes,
    const std::vector<BoardCell>& boss_opening_cells = {})
{
    // Token range 3-2 covers its own tile and three tiles in front. Only
    // targets in those three front tiles can be pushed by its basic attack.
    constexpr int forward_range = 3;

    struct Score
    {
        bool reaches_boss = false;
        bool linked_boss_shot = false;
        int boss_coverage = 0;
        bool pushes_back = false;
        bool linked_wave_shot = false;
        int push_back = 0;
        int approach = 0;

        auto operator<=>(const Score&) const = default;
    };

    std::vector<std::pair<SummonTarget, Score>> ranked;
    std::vector<size_t> contact_indices;
    for (const auto& lane : lanes) {
        const auto first_blocker = std::find_if(lane.path.begin(), lane.path.end(), [&](const auto& cell) {
            return std::ranges::any_of(allies, [&](const auto& ally) {
                return ally.role == PlacementRole::Frontline && cell == BoardCell { ally.col, ally.row };
            });
        });
        contact_indices.push_back(static_cast<size_t>(first_blocker - lane.path.begin()));
    }
    for (const auto& cell : candidates) {
        const bool near_owner = std::abs(cell.first - owner.col) + std::abs(cell.second - owner.row) == 1;
        std::pair<SummonTarget, Score> best { { cell, 'E' }, {} };
        for (const char facing : std::string_view("ENSW")) {
            const auto [dx, dy] = growth_facing_step(facing);
            const auto in_range = [&](BoardCell target, int minimum_distance) {
                const int delta_col = target.first - cell.first;
                const int delta_row = target.second - cell.second;
                const int distance = delta_col * dx + delta_row * dy;
                return distance >= minimum_distance && distance <= forward_range && delta_col == dx * distance &&
                       delta_row == dy * distance;
            };
            const int boss_coverage = static_cast<int>(
                std::ranges::count_if(boss_opening_cells, [&](BoardCell target) { return in_range(target, 0); }));
            int push_back = 0;
            int approach = 0;
            for (size_t index = 0; index < lanes.size(); ++index) {
                const auto& lane = lanes[index];
                for (size_t step = 0; step < lane.path.size() && step <= contact_indices[index]; ++step) {
                    const auto [col, row] = lane.path[step];
                    if (!in_range({ col, row }, 1)) {
                        continue;
                    }
                    const int value = lane.blocking_weight * std::max(1, 20 - static_cast<int>(step));
                    approach += value;
                    if ((step > 0 && lane.path[step - 1] == BoardCell { col + dx, row + dy }) ||
                        (step + 1 < lane.path.size() && lane.path[step + 1] == BoardCell { col - dx, row - dy })) {
                        push_back += value;
                    }
                }
            }
            const Score score { boss_coverage > 0, near_owner && boss_coverage > 0, boss_coverage,
                                push_back > 0,     near_owner && approach > 0,      push_back,
                                approach };
            if (score > best.second) {
                best = { { cell, facing }, score };
            }
        }
        // A square away from all useful fire cannot justify a placement
        // even if the held-card preview marks it legal.
        if (best.second.reaches_boss || best.second.approach > 0) {
            ranked.push_back(best);
        }
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        return left.second > right.second;
    });
    std::vector<SummonTarget> result;
    result.reserve(ranked.size());
    for (const auto& [target, _] : ranked) {
        result.push_back(target);
    }
    return result;
}
}
