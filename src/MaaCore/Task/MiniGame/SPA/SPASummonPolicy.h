#pragma once

#include "SPADeploymentReview.h"
#include "SPAPlacementPolicy.h"
#include "SPASpatialGrowthPolicy.h"
#include <array>

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
            if (water_cannon && pos != lane.path.begin()) {
                const auto approach = *std::prev(pos);
                if (approach.first > cell.first && approach.second == cell.second) {
                    value += 250; // A right-facing cannon pushes this approach away from the gate.
                }
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
}
