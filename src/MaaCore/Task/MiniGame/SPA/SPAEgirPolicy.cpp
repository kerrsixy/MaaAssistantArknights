#include "SPAEgirPolicy.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <tuple>

int asst::spa::egir_output_priority(const std::string& name)
{
    static const std::array<std::string, 6> names { "克莱门莎", "乌尔比安", "幽灵鲨", "斯卡蒂", "安哲拉", "歌蕾蒂娅" };
    const auto found = std::find(names.begin(), names.end(), name);
    return found == names.end() ? 0 : static_cast<int>(names.end() - found);
}

std::optional<std::string> asst::spa::designate_egir_output(std::vector<EgirEntity>& entities)
{
    EgirEntity* selected = nullptr;
    int highest = 0;
    bool ambiguous = false;
    for (auto& unit : entities) {
        if (!unit.egir_member || unit.intent == EgirIntent::ProtectedSupport) {
            continue;
        }
        unit.intent = EgirIntent::Relay;
        const int priority = egir_output_priority(unit.name);
        if (priority == 0 || !unit.base_attack || *unit.base_attack <= 0 || !unit.block_count ||
            *unit.block_count <= 0) {
            continue;
        }
        if (priority > highest) {
            selected = &unit;
            highest = priority;
            ambiguous = false;
        }
        else if (priority == highest) {
            ambiguous = true;
        }
    }
    if (!selected || ambiguous) {
        return std::nullopt;
    }
    selected->intent = EgirIntent::Core;
    return selected->id;
}

asst::spa::EgirConsumptionProjection asst::spa::project_egir_mark_closure(
    const std::vector<EgirEntity>& entities,
    bool pact_active,
    EgirSixMemberRule six_member_rule)
{
    EgirConsumptionProjection result;
    result.active = pact_active;
    if (entities.empty() || entities.size() > 8) {
        result.problems.emplace_back("Missing or excessive physical field entities");
        return result;
    }
    std::set<std::string> ids, names;
    std::map<BoardCell, size_t> cells;
    std::vector<size_t> order;
    for (size_t index = 0; index < entities.size(); ++index) {
        const auto& unit = entities[index];
        if (unit.id.empty() || unit.name.empty() || !ids.insert(unit.id).second ||
            !cells.emplace(unit.cell, index).second || unit.cell.first < 0 || unit.cell.first > 10 ||
            unit.cell.second < 0 || unit.cell.second > 4 || unit.tier < 0 || unit.tier > 6 ||
            (unit.base_attack && (!std::isfinite(*unit.base_attack) || *unit.base_attack < 0)) ||
            (unit.block_count && *unit.block_count < 0) ||
            (unit.hp_before_consumption &&
             (!std::isfinite(*unit.hp_before_consumption) || *unit.hp_before_consumption <= 0)) ||
            (unit.damage_per_mark && (!std::isfinite(*unit.damage_per_mark) || *unit.damage_per_mark < 0))) {
            result.problems.emplace_back("Invalid identity, cell or declared combat observation");
            return result;
        }
        if (unit.egir_member) {
            if (unit.counts_for_literal_egir_milestone) {
                names.insert(unit.name);
            }
            order.push_back(index);
            if (pact_active && (std::string("ENSW").find(unit.facing) == std::string::npos || !unit.facing)) {
                result.problems.emplace_back("Egir facing is unobserved");
                return result;
            }
        }
    }
    result.distinct_egir_members = static_cast<int>(names.size());
    if (pact_active && names.size() >= 6) {
        if (six_member_rule == EgirSixMemberRule::Revive) {
            result.base_six_member_revive_limit = 3;
        }
        else {
            result.six_member_bench_replacement_limit = 3;
        }
    }
    if (!pact_active) {
        result.complete = true;
        result.conditional_unique_target_layers = 0;
        return result;
    }
    // Column then row: printed current-season left/top priority. Do not use
    // an operator-name sort or the older season's bottom-first priority.
    std::sort(order.begin(), order.end(), [&](size_t left, size_t right) {
        return entities[left].cell < entities[right].cell;
    });
    const auto front = [&](size_t index) -> std::optional<size_t> {
        const auto& unit = entities[index];
        auto cell = unit.cell;
        cell.first += unit.facing == 'E' ? 1 : unit.facing == 'W' ? -1 : 0;
        cell.second += unit.facing == 'S' ? 1 : unit.facing == 'N' ? -1 : 0;
        const auto found = cells.find(cell);
        return found == cells.end() ? std::nullopt : std::optional(found->second);
    };
    std::vector<std::set<size_t>> marks(entities.size());
    std::vector<int> received(entities.size());
    for (const size_t source : order) {
        std::deque<size_t> pending;
        if (const auto target = front(source)) {
            pending.push_back(*target);
        }
        while (!pending.empty()) {
            const auto target = pending.front();
            pending.pop_front();
            if (target == source || marks[source].contains(target) || marks[target].contains(source)) {
                continue;
            }
            marks[source].insert(target);
            ++received[target];
            if (entities[target].egir_member) {
                if (const auto next = front(target)) {
                    pending.push_back(*next);
                }
            }
        }
        EgirConsumptionSource report;
        report.source = entities[source].id;
        report.inherited_base_attack = 0;
        report.inherited_block_count = 0;
        for (const size_t target : marks[source]) {
            const auto& victim = entities[target];
            report.marked_targets.push_back(victim.id);
            if (report.inherited_base_attack && victim.base_attack) {
                *report.inherited_base_attack += *victim.base_attack;
            }
            else {
                report.inherited_base_attack.reset();
            }
            if (report.inherited_block_count && victim.block_count) {
                *report.inherited_block_count += *victim.block_count;
            }
            else {
                report.inherited_block_count.reset();
            }
        }
        result.sources.push_back(std::move(report));
    }
    result.conditional_unique_target_layers = 0;
    for (size_t index = 0; index < entities.size(); ++index) {
        if (!received[index]) {
            continue;
        }
        const auto& unit = entities[index];
        const bool protected_target = unit.intent == EgirIntent::Core || unit.intent == EgirIntent::ProtectedSupport;
        EgirConsumptionVictim victim { unit.id, received[index], std::nullopt, protected_target };
        if (unit.hp_before_consumption && unit.damage_per_mark) {
            victim.defeated = *unit.hp_before_consumption <= received[index] * *unit.damage_per_mark;
        }
        if (protected_target) {
            result.problems.emplace_back("Consumption marks a protected core or support entity");
        }
        if (result.conditional_unique_target_layers && unit.tier > 0) {
            *result.conditional_unique_target_layers += unit.tier;
        }
        else {
            result.conditional_unique_target_layers.reset();
        }
        result.victims.push_back(std::move(victim));
    }
    result.complete = true;
    return result;
}

bool asst::spa::append_egir_defended_member(
    std::vector<std::string>& prefix,
    const std::vector<EgirPlacementUnit>& units,
    const std::string& name,
    BoardCell home)
{
    const BoardCell target(home.first + 1 + static_cast<int>(prefix.size()), home.second);
    const auto found = std::find_if(units.begin(), units.end(), [&](const auto& unit) {
        return unit.entity.name == name && unit.entity.egir_member && (!prefix.empty() || unit.eligible_chain_head) &&
               std::find(prefix.begin(), prefix.end(), unit.entity.id) == prefix.end() &&
               std::find(unit.deployment.placement_cells.begin(), unit.deployment.placement_cells.end(), target) !=
                   unit.deployment.placement_cells.end() &&
               (!unit.required_cell || *unit.required_cell == target) &&
               (!unit.required_facing || *unit.required_facing == 'E') &&
               (unit.deployment.movable || (unit.entity.cell == target && unit.entity.facing == 'E'));
    });
    if (found == units.end()) {
        return false;
    }
    prefix.push_back(found->entity.id);
    return true;
}

bool asst::spa::append_egir_defended_name(
    std::vector<std::string>& names,
    const std::vector<EgirPlacementUnit>& units,
    const std::string& name,
    BoardCell home,
    char head_facing)
{
    if (names.size() >= 3 || !head_facing || std::string("ENSW").find(head_facing) == std::string::npos ||
        std::find(names.begin(), names.end(), name) != names.end()) {
        return false;
    }
    auto wanted = names;
    wanted.push_back(name);
    const auto advance = [](BoardCell cell, char facing) {
        cell.first += facing == 'E' ? 1 : facing == 'W' ? -1 : 0;
        cell.second += facing == 'S' ? 1 : facing == 'N' ? -1 : 0;
        return cell;
    };
    std::set<size_t> used;
    std::set<BoardCell> taken;
    std::function<bool(size_t, BoardCell)> visit = [&](size_t offset, BoardCell cell) {
        for (size_t i = 0; i < units.size(); ++i) {
            const auto& unit = units[i];
            if (used.contains(i) || taken.contains(cell) || unit.entity.name != wanted[offset] ||
                !unit.entity.egir_member || unit.entity.intent == EgirIntent::ProtectedSupport ||
                (offset == 0 && !unit.eligible_chain_head) ||
                (offset == 0 && ((!unit.deployment.movable && unit.entity.facing != head_facing) ||
                                 (unit.required_facing && *unit.required_facing != head_facing))) ||
                (!unit.deployment.movable && !unit.entity.facing) ||
                (!unit.deployment.movable && unit.entity.cell != cell) ||
                (unit.required_cell && *unit.required_cell != cell) ||
                std::find(unit.deployment.placement_cells.begin(), unit.deployment.placement_cells.end(), cell) ==
                    unit.deployment.placement_cells.end()) {
                continue;
            }
            if (offset + 1 == wanted.size()) {
                return true;
            }
            used.insert(i);
            taken.insert(cell);
            for (const char direction : std::string("ENSW")) {
                if ((offset == 0 && direction != head_facing) ||
                    (!unit.deployment.movable && direction != unit.entity.facing) ||
                    (unit.required_facing && direction != *unit.required_facing)) {
                    continue;
                }
                if (visit(offset + 1, advance(cell, direction))) {
                    return true;
                }
            }
            taken.erase(cell);
            used.erase(i);
        }
        return false;
    };
    if (!visit(0, advance(home, head_facing))) {
        return false;
    }
    names = std::move(wanted);
    return true;
}

static bool
    egir_boss_pose_reaches(const asst::spa::DeploymentUnit& unit, const asst::spa::EgirFormationContext& context)
{
    const auto overlaps = [&](asst::spa::BoardCell cell) {
        return asst::spa::boss_attack_overlaps_cell(unit, cell);
    };
    if (context.boss_cells.empty()) {
        return false;
    }
    if (context.boss_cells_are_alternatives) {
        return std::ranges::all_of(context.boss_cells, overlaps);
    }
    return std::ranges::any_of(context.boss_cells, overlaps) ||
           (!context.stationary_boss && std::ranges::any_of(context.boss_movement_waypoints, overlaps));
}

static int egir_surviving_attackers_movement_coverage(
    const std::vector<asst::spa::DeploymentUnit>& units,
    const asst::spa::EgirConsumptionProjection& consumption,
    const asst::spa::EgirFormationContext& context)
{
    if (context.stationary_boss || context.boss_cells_are_alternatives) {
        return 0;
    }
    return static_cast<int>(std::ranges::count_if(context.boss_movement_waypoints, [&](asst::spa::BoardCell cell) {
        return std::ranges::any_of(units, [&](const auto& unit) {
            return unit.damage_output && !unit.healer && unit.range_known &&
                   asst::spa::boss_attack_overlaps_cell(unit, cell) &&
                   std::ranges::none_of(consumption.victims, [&](const auto& victim) { return victim.id == unit.id; });
        });
    }));
}

asst::spa::EgirFormationPlan asst::spa::plan_egir_core_chain(
    const std::vector<EgirPlacementUnit>& units,
    const EgirFormationContext& context,
    const std::string& forced_core)
{
    EgirFormationPlan result;
    const bool aim_at_boss =
        context.stationary_boss || context.boss_opening_only || !context.boss_movement_waypoints.empty();
    if (units.empty() || units.size() > 8 || !context.pact_active || context.evaluation_budget <= 0 ||
        !context.chain_facing || std::string("ENSW").find(context.chain_facing) == std::string::npos ||
        (aim_at_boss ? context.boss_cells.empty() : context.lanes.empty())) {
        result.reason = "Missing active pact, field, objective or evaluation budget";
        return result;
    }
    const auto contains = [](const auto& cells, BoardCell cell) {
        return std::find(cells.begin(), cells.end(), cell) != cells.end();
    };
    std::set<std::string> ids;
    std::set<BoardCell> occupied;
    std::optional<size_t> core;
    for (size_t index = 0; index < units.size(); ++index) {
        const auto& unit = units[index];
        if (unit.entity.id.empty() || unit.entity.id != unit.deployment.id ||
            unit.entity.cell != unit.deployment.cell || !ids.insert(unit.entity.id).second ||
            !occupied.insert(unit.entity.cell).second || unit.deployment.placement_cells.empty() ||
            !contains(unit.deployment.placement_cells, unit.entity.cell)) {
            result.reason = "Incomplete physical identity or terrain-compatible placement domain";
            return result;
        }
        for (const auto& cell : unit.deployment.placement_cells) {
            if (cell.first < 0 || cell.first > 10 || cell.second < 0 || cell.second > 4) {
                result.reason = "Placement cell is outside the current field";
                return result;
            }
        }
        if (unit.eligible_chain_head && unit.entity.egir_member && unit.entity.intent != EgirIntent::ProtectedSupport &&
            unit.deployment.damage_output && unit.deployment.range_known &&
            (forced_core.empty() || unit.entity.id == forced_core) &&
            (!core || std::tuple(unit.entity.intent == EgirIntent::Core, unit.core_priority) >
                          std::tuple(units[*core].entity.intent == EgirIntent::Core, units[*core].core_priority))) {
            core = index;
        }
    }
    if (!core) {
        result.reason = "No identified attacking Egir core with a known range";
        return result;
    }
    result.input_complete = true;
    result.core = units[*core].entity.id;
    const bool ordered_head =
        !aim_at_boss &&
        ((!context.defended_chain_prefix.empty() && result.core == context.defended_chain_prefix.front()) ||
         (!context.defended_chain_names.empty() && units[*core].entity.name == context.defended_chain_names.front()));
    using Score = std::tuple<int, int, int, int, double, int, int, double, int, int, int, int, int>;
    std::optional<Score> best;
    std::vector<size_t> chain { *core };
    std::vector<BoardCell> chain_cells;
    std::set<size_t> used { *core };
    const auto can_place = [&](size_t index, BoardCell cell) {
        return contains(units[index].deployment.placement_cells, cell) &&
               (!units[index].required_cell || cell == *units[index].required_cell) &&
               (units[index].deployment.movable || cell == units[index].entity.cell);
    };
    const auto advance = [](BoardCell cell, char facing) {
        cell.first += facing == 'E' ? 1 : facing == 'W' ? -1 : 0;
        cell.second += facing == 'S' ? 1 : facing == 'N' ? -1 : 0;
        return cell;
    };
    const auto between = [](BoardCell from, BoardCell to) {
        return to.first > from.first ? 'E' : to.first < from.first ? 'W' : to.second > from.second ? 'S' : 'N';
    };
    const auto evaluate = [&](BoardCell root) {
        if (result.evaluations >= context.evaluation_budget) {
            result.budget_exhausted = true;
            return;
        }
        ++result.evaluations;
        const int sources = static_cast<int>(
            std::count_if(chain.begin(), chain.end(), [&](size_t index) { return units[index].entity.egir_member; }));
        if (sources < 2) {
            return;
        }
        if (ordered_head &&
            chain.size() < std::max(context.defended_chain_prefix.size(), context.defended_chain_names.size())) {
            return;
        }
        std::vector<DeploymentUnit> candidate;
        for (const auto& unit : units) {
            candidate.push_back(unit.deployment);
        }
        std::set<BoardCell> taken;
        for (size_t offset = 0; offset < chain.size(); ++offset) {
            auto& target = candidate[chain[offset]];
            target.cell = chain_cells[offset];
            if (units[chain[offset]].entity.egir_member) {
                const char facing = offset + 1 < chain.size()
                                        ? between(chain_cells[offset], chain_cells[offset + 1])
                                        : units[chain[offset]].required_facing.value_or(target.facing);
                if ((!target.movable && target.facing != facing) ||
                    (units[chain[offset]].required_facing && *units[chain[offset]].required_facing != facing)) {
                    return;
                }
                target.facing = facing;
            }
            taken.insert(target.cell);
        }
        // Rehouse displaced bodies using their own legal domain. No sale,
        // withdrawal or bench assumption is introduced by a formation plan.
        std::vector<size_t> others;
        for (size_t index = 0; index < units.size(); ++index) {
            if (!used.contains(index)) {
                others.push_back(index);
            }
        }
        std::stable_sort(others.begin(), others.end(), [&](size_t left, size_t right) {
            return std::tuple(
                       units[left].required_cell.has_value(),
                       !candidate[left].movable,
                       candidate[left].healer,
                       aim_at_boss && units[left].preferred_boss_output) >
                   std::tuple(
                       units[right].required_cell.has_value(),
                       !candidate[right].movable,
                       candidate[right].healer,
                       aim_at_boss && units[right].preferred_boss_output);
        });
        for (const size_t index : others) {
            auto& target = candidate[index];
            std::vector<DeploymentUnit> choices;
            for (const auto& cell : target.placement_cells) {
                if (taken.contains(cell) || !can_place(index, cell)) {
                    continue;
                }
                for (const char facing : std::string("ENSW")) {
                    if ((!target.movable && facing != target.facing) ||
                        (units[index].required_facing && facing != *units[index].required_facing)) {
                        continue;
                    }
                    auto choice = target;
                    choice.cell = cell;
                    choice.facing = facing;
                    choices.push_back(std::move(choice));
                }
            }
            if (choices.empty()) {
                return;
            }
            const auto value = [&](const DeploymentUnit& choice) {
                const bool healing = choice.healer && choice.range_known &&
                                     (candidate[*core].accepts_direct_healing || choice.provides_regeneration) &&
                                     range_covers(choice, root);
                const bool boss_coverage = aim_at_boss && units[index].preferred_boss_output && choice.damage_output &&
                                           choice.range_known && egir_boss_pose_reaches(choice, context);
                const auto additional_waypoints =
                    boss_coverage && !context.stationary_boss && !context.boss_cells_are_alternatives
                        ? std::ranges::count_if(
                              context.boss_movement_waypoints,
                              [&](BoardCell cell) {
                                  return boss_attack_overlaps_cell(choice, cell) &&
                                         !boss_attack_overlaps_cell(candidate[*core], cell);
                              })
                        : 0;
                return std::tuple(
                    healing,
                    boss_coverage,
                    additional_waypoints,
                    choice.cell == target.cell,
                    choice.facing == target.facing);
            };
            target = *std::max_element(choices.begin(), choices.end(), [&](const auto& left, const auto& right) {
                return value(left) < value(right);
            });
            taken.insert(target.cell);
        }
        // Keep the core chain's terminal clear. Off-chain sources may feed
        // on explicitly consumable off-chain bodies while still covering
        // enemies; forcing every such source to face empty space can turn a
        // ranged growth/output unit toward the blue gate.
        for (size_t index = 0; index < units.size(); ++index) {
            if (!units[index].entity.egir_member || (used.contains(index) && index != chain.back())) {
                continue;
            }
            auto& target = candidate[index];
            std::optional<char> best_facing;
            std::pair<int, bool> best_value { -1, false };
            for (const char facing : std::string("ENSW")) {
                if ((!target.movable && facing != target.facing) ||
                    (units[index].required_facing && facing != *units[index].required_facing)) {
                    continue;
                }
                const auto next = advance(target.cell, facing);
                const auto victim = std::find_if(candidate.begin(), candidate.end(), [&](const auto& unit) {
                    return unit.cell == next;
                });
                if (victim != candidate.end()) {
                    const auto victim_index = static_cast<size_t>(std::distance(candidate.begin(), victim));
                    const auto& intent = units[victim_index];
                    const bool consumable =
                        intent.entity.intent == EgirIntent::Relay ||
                        (intent.entity.intent == EgirIntent::Material && intent.expendable_material);
                    if (used.contains(index) || used.contains(victim_index) || victim_index == *core || !consumable) {
                        continue;
                    }
                }
                auto choice = target;
                choice.facing = facing;
                int coverage = 0;
                if (choice.damage_output && choice.range_known) {
                    if (aim_at_boss) {
                        for (const auto& cell : context.boss_cells) {
                            coverage += boss_attack_overlaps_cell(choice, cell);
                        }
                    }
                    else {
                        for (const auto& lane : context.lanes) {
                            for (const auto& cell : lane.path) {
                                coverage += range_covers(choice, cell) ? lane.blocking_weight : 0;
                            }
                        }
                    }
                }
                const std::pair value { coverage, facing == target.facing };
                if (!best_facing || value > best_value) {
                    best_facing = facing;
                    best_value = value;
                }
            }
            if (!best_facing) {
                return;
            }
            target.facing = *best_facing;
        }
        std::vector<EgirEntity> entities;
        int moved = 0, rotated = 0, row_triggers = 0, healing = 0;
        for (size_t index = 0; index < units.size(); ++index) {
            auto entity = units[index].entity;
            entity.cell = candidate[index].cell;
            entity.facing = candidate[index].facing;
            if (index == *core) {
                entity.intent = EgirIntent::Core;
            }
            entities.push_back(std::move(entity));
            moved += candidate[index].cell != units[index].deployment.cell;
            rotated += candidate[index].facing != units[index].deployment.facing;
            healing += candidate[index].healer && candidate[index].range_known &&
                       (candidate[*core].accepts_direct_healing || candidate[index].provides_regeneration) &&
                       range_covers(candidate[index], root);
            if (units[index].same_row_recruitment_members > 0) {
                std::set<std::string> row_names;
                for (size_t other = 0; other < units.size(); ++other) {
                    if (candidate[other].cell.second == candidate[index].cell.second) {
                        row_names.insert(units[other].entity.name);
                    }
                }
                row_triggers += row_names.size() >= units[index].same_row_recruitment_members;
            }
        }
        auto projection = project_egir_mark_closure(entities, true, context.six_member_rule);
        if (!projection.complete || !projection.problems.empty()) {
            return;
        }
        const auto source = std::find_if(projection.sources.begin(), projection.sources.end(), [&](const auto& item) {
            return item.source == result.core;
        });
        if (source == projection.sources.end() || source->marked_targets.empty() ||
            (!aim_at_boss && !candidate[*core].holds_position) ||
            candidate[*core].block_count + source->inherited_block_count.value_or(0) <= 0) {
            return;
        }
        int guarded = 0, min_approach = 100, boss_covered = 0;
        for (const auto& lane : context.lanes) {
            const auto at = std::find(lane.path.begin(), lane.path.end(), root);
            guarded += at != lane.path.end();
            int covered = 0;
            if (at != lane.path.end()) {
                for (auto point = lane.path.begin(); point != at; ++point) {
                    covered += range_covers(candidate[*core], *point);
                }
            }
            min_approach = std::min(min_approach, covered);
        }
        for (const auto& cell : context.boss_cells) {
            boss_covered += boss_attack_overlaps_cell(candidate[*core], cell);
        }
        const auto movement_coverage = static_cast<int>(std::count_if(
            context.boss_movement_waypoints.begin(),
            context.boss_movement_waypoints.end(),
            [&](auto cell) { return boss_attack_overlaps_cell(candidate[*core], cell); }));
        if (aim_at_boss ? !egir_boss_pose_reaches(candidate[*core], context) : guarded != context.lanes.size()) {
            return;
        }
        const int distance = std::abs(root.first - context.home.first) + std::abs(root.second - context.home.second);
        int priority_attackers = 0, chain_attack_priority = 0;
        if (aim_at_boss) {
            const auto attacks_boss = [&](size_t index) {
                const auto& unit = candidate[index];
                return unit.damage_output && !unit.healer && unit.range_known && egir_boss_pose_reaches(unit, context);
            };
            for (size_t index = 0; index < units.size(); ++index) {
                if (index == *core) {
                    ++priority_attackers;
                    continue;
                }
                if (units[index].preferred_boss_output && attacks_boss(index) &&
                    std::none_of(projection.victims.begin(), projection.victims.end(), [&](const auto& victim) {
                        return victim.id == candidate[index].id;
                    })) {
                    ++priority_attackers;
                }
            }
            for (size_t offset = 1; offset < chain.size(); ++offset) {
                if (attacks_boss(chain[offset])) {
                    chain_attack_priority += 8 - static_cast<int>(offset);
                }
            }
        }
        int tail_preference = 0;
        if (units[*core].entity.name == "克莱门莎" || units[*core].entity.name == "乌尔比安") {
            for (size_t offset = 0; offset < chain.size(); ++offset) {
                if (units[chain[offset]].entity.name == "幽灵鲨") {
                    tail_preference = static_cast<int>(offset);
                }
            }
        }
        const int surviving_movement_coverage =
            egir_surviving_attackers_movement_coverage(candidate, projection, context);
        const Score score { surviving_movement_coverage,
                            aim_at_boss ? priority_attackers : guarded,
                            movement_coverage,
                            aim_at_boss ? static_cast<int>(boss_covered > 0) : boss_covered,
                            aim_at_boss ? source->inherited_base_attack.value_or(0)
                                        : static_cast<double>(projection.conditional_unique_target_layers.value_or(0)),
                            chain_attack_priority,
                            -distance,
                            aim_at_boss ? static_cast<double>(projection.conditional_unique_target_layers.value_or(0))
                                        : source->inherited_base_attack.value_or(0),
                            min_approach,
                            tail_preference,
                            row_triggers,
                            healing,
                            -moved * 2 - rotated };
        if (!best || score > *best) {
            if (context.target_admissible && !context.target_admissible(candidate)) {
                return;
            }
            best = score;
            result.found = true;
            result.boss_priority_attackers = priority_attackers;
            result.boss_chain_attack_priority = chain_attack_priority;
            result.boss_core_movement_waypoints = movement_coverage;
            result.boss_surviving_attackers_movement_waypoints = surviving_movement_coverage;
            result.units = std::move(candidate);
            result.conditional_consumption = std::move(projection);
            result.changed_units = 0;
            result.chain.clear();
            for (const size_t index : chain) {
                result.chain.push_back(units[index].entity.id);
            }
            for (size_t index = 0; index < units.size(); ++index) {
                result.changed_units += result.units[index].cell != units[index].deployment.cell ||
                                        result.units[index].facing != units[index].deployment.facing;
            }
        }
    };
    std::function<void(BoardCell)> extend = [&](BoardCell root) {
        if (result.budget_exhausted) {
            return;
        }
        evaluate(root);
        std::vector<size_t> choices;
        for (size_t i = 0; i < units.size(); ++i) {
            if (!used.contains(i)) {
                choices.push_back(i);
            }
        }
        std::stable_sort(choices.begin(), choices.end(), [&](size_t left, size_t right) {
            const auto value = [&](size_t i) {
                const auto& unit = units[i];
                const bool last_ghost =
                    (units[*core].entity.name == "克莱门莎" || units[*core].entity.name == "乌尔比安") &&
                    unit.entity.name == "幽灵鲨";
                return std::tuple(unit.entity.egir_member, !last_ghost, unit.entity.base_attack.value_or(0));
            };
            return value(left) > value(right);
        });
        const auto& previous = units[chain.back()];
        const auto straight =
            chain.size() == 1 ? context.chain_facing : between(chain_cells[chain.size() - 2], chain_cells.back());
        std::string directions(1, straight);
        for (const char direction : std::string("ENSW")) {
            if (direction != straight) {
                directions.push_back(direction);
            }
        }
        for (const char direction : directions) {
            if ((chain.size() == 1 && direction != context.chain_facing) ||
                (!previous.deployment.movable && previous.entity.facing != direction) ||
                (previous.required_facing && *previous.required_facing != direction)) {
                continue;
            }
            const auto next = advance(chain_cells.back(), direction);
            if (std::find(chain_cells.begin(), chain_cells.end(), next) != chain_cells.end()) {
                continue;
            }
            for (const size_t index : choices) {
                const auto& unit = units[index];
                if (!can_place(index, next) ||
                    (ordered_head && chain.size() < context.defended_chain_prefix.size() &&
                     unit.entity.id != context.defended_chain_prefix[chain.size()]) ||
                    (ordered_head && chain.size() < context.defended_chain_names.size() &&
                     unit.entity.name != context.defended_chain_names[chain.size()])) {
                    continue;
                }
                const bool relay = unit.entity.egir_member && unit.entity.intent == EgirIntent::Relay;
                const bool material =
                    !unit.entity.egir_member && unit.entity.intent == EgirIntent::Material && unit.expendable_material;
                if (!relay && !material) {
                    continue;
                }
                chain.push_back(index);
                chain_cells.push_back(next);
                used.insert(index);
                if (relay) {
                    extend(root);
                }
                else {
                    evaluate(root);
                }
                used.erase(index);
                chain_cells.pop_back();
                chain.pop_back();
                if (result.budget_exhausted) {
                    break;
                }
            }
            if (result.budget_exhausted) {
                break;
            }
        }
    };
    auto roots = units[*core].deployment.placement_cells;
    std::stable_sort(roots.begin(), roots.end(), [&](BoardCell left, BoardCell right) {
        const auto distance = [&](BoardCell cell) {
            return std::abs(cell.first - context.home.first) + std::abs(cell.second - context.home.second);
        };
        return distance(left) < distance(right);
    });
    for (const auto& root : roots) {
        if (aim_at_boss) {
            auto attacking_pose = units[*core].deployment;
            attacking_pose.cell = root;
            attacking_pose.facing = context.chain_facing;
            if (!egir_boss_pose_reaches(attacking_pose, context)) {
                continue;
            }
        }
        if (ordered_head && root != advance(context.home, context.chain_facing)) {
            continue;
        }
        if (can_place(*core, root)) {
            chain_cells = { root };
            extend(root);
        }
        if (result.budget_exhausted) {
            break;
        }
    }
    result.reason = result.found ? "Provisional directed Egir chain; current-season consumption remains unverified"
                                 : "No compatible protected-core chain found within this placement family";
    return result;
}

asst::spa::EgirFormationPlan
    asst::spa::plan_egir_formation(const std::vector<EgirPlacementUnit>& units, const EgirFormationContext& context)
{
    if (units.empty() || units.size() > 8 || context.evaluation_budget <= 0 || !context.pact_active) {
        return plan_egir_core_chain(units, context, "");
    }
    std::vector<size_t> cores;
    for (size_t index = 0; index < units.size(); ++index) {
        const auto& unit = units[index];
        if (unit.eligible_chain_head && unit.entity.egir_member && unit.entity.intent != EgirIntent::ProtectedSupport &&
            unit.deployment.damage_output && unit.deployment.range_known) {
            cores.push_back(index);
        }
    }
    std::stable_sort(cores.begin(), cores.end(), [&](size_t left, size_t right) {
        return std::tuple(units[left].entity.intent == EgirIntent::Core, units[left].core_priority) >
               std::tuple(units[right].entity.intent == EgirIntent::Core, units[right].core_priority);
    });
    EgirFormationPlan result;
    int evaluated = 0;
    if (context.boss_cells.empty() ||
        (!context.stationary_boss && !context.boss_opening_only && context.boss_movement_waypoints.empty())) {
        for (const auto index : cores) {
            if (!context.defended_chain_prefix.empty() &&
                units[index].entity.id != context.defended_chain_prefix.front()) {
                continue;
            }
            if (!context.defended_chain_names.empty() &&
                units[index].entity.name != context.defended_chain_names.front()) {
                continue;
            }
            if (evaluated >= context.evaluation_budget) {
                break;
            }
            auto candidate_context = context;
            candidate_context.evaluation_budget = std::min(1200, context.evaluation_budget - evaluated);
            auto candidate = plan_egir_core_chain(units, candidate_context, units[index].entity.id);
            evaluated += candidate.evaluations;
            result.input_complete = result.input_complete || candidate.input_complete;
            if (candidate.found) {
                candidate.evaluations = evaluated;
                return candidate;
            }
        }
        result.evaluations = evaluated;
        result.budget_exhausted = evaluated >= context.evaluation_budget;
        result.reason = "No admissible defended Egir core chain within budget";
        return result;
    }
    for (const auto index : cores) {
        const auto& attacker = units[index];
        std::vector<DeploymentUnit> poses;
        for (const auto cell : attacker.deployment.placement_cells) {
            for (const char facing : std::string("ENSW")) {
                if (!attacker.deployment.movable &&
                    (cell != attacker.deployment.cell || facing != attacker.deployment.facing)) {
                    continue;
                }
                auto pose = attacker.deployment;
                pose.cell = cell;
                pose.facing = facing;
                if (egir_boss_pose_reaches(pose, context)) {
                    poses.push_back(std::move(pose));
                }
            }
        }
        std::stable_sort(poses.begin(), poses.end(), [&](const auto& left, const auto& right) {
            const auto change = [&](const auto& pose) {
                return std::pair(pose.cell != attacker.entity.cell, pose.facing != attacker.entity.facing);
            };
            return change(left) < change(right);
        });
        EgirFormationPlan best_direct;
        const auto direct_value = [](const EgirFormationPlan& plan) {
            const auto source = std::find_if(
                plan.conditional_consumption.sources.begin(),
                plan.conditional_consumption.sources.end(),
                [&](const auto& item) { return item.source == plan.core; });
            return std::tuple(
                plan.boss_surviving_attackers_movement_waypoints,
                plan.boss_priority_attackers,
                plan.boss_core_movement_waypoints,
                source == plan.conditional_consumption.sources.end() ? 0.0 : source->inherited_base_attack.value_or(0),
                plan.boss_chain_attack_priority,
                plan.conditional_consumption.conditional_unique_target_layers.value_or(0),
                -plan.changed_units);
        };
        for (const char direction : std::string("EWNS")) {
            const bool can_feed = std::any_of(poses.begin(), poses.end(), [&](const auto& pose) {
                if (pose.facing != direction) {
                    return false;
                }
                const BoardCell next { pose.cell.first + (direction == 'E'   ? 1
                                                          : direction == 'W' ? -1
                                                                             : 0),
                                       pose.cell.second + (direction == 'S'   ? 1
                                                           : direction == 'N' ? -1
                                                                              : 0) };
                return std::any_of(units.begin(), units.end(), [&](const auto& relay) {
                    return relay.entity.id != attacker.entity.id && relay.entity.egir_member &&
                           relay.entity.intent == EgirIntent::Relay &&
                           (relay.deployment.movable || next == relay.entity.cell) &&
                           std::find(
                               relay.deployment.placement_cells.begin(),
                               relay.deployment.placement_cells.end(),
                               next) != relay.deployment.placement_cells.end();
                });
            });
            if (!can_feed || evaluated >= context.evaluation_budget) {
                continue;
            }
            auto direct_context = context;
            direct_context.chain_facing = direction;
            direct_context.evaluation_budget = std::min(1200, context.evaluation_budget - evaluated);
            auto direct = plan_egir_core_chain(units, direct_context, attacker.entity.id);
            evaluated += direct.evaluations;
            result.input_complete = result.input_complete || direct.input_complete;
            if (direct.found && (!best_direct.found || direct_value(direct) > direct_value(best_direct))) {
                best_direct = std::move(direct);
            }
        }
        if (best_direct.found) {
            best_direct.evaluations = evaluated;
            best_direct.budget_exhausted = best_direct.budget_exhausted || evaluated >= context.evaluation_budget;
            return best_direct;
        }
        // Some maps have a forward attack tile with no feed tile ahead.
        // Preserve that attacking terminal and build a separate defended
        // growth chain, rather than turning the terminal toward the gate.
        for (const auto& pose : poses) {
            for (const auto engine_index : cores) {
                if (engine_index == index || evaluated >= context.evaluation_budget) {
                    continue;
                }
                if (!context.defended_chain_prefix.empty() &&
                    attacker.entity.id != context.defended_chain_prefix.front() &&
                    units[engine_index].entity.id != context.defended_chain_prefix.front()) {
                    continue;
                }
                if (!context.defended_chain_names.empty() &&
                    attacker.entity.name != context.defended_chain_names.front() &&
                    units[engine_index].entity.name != context.defended_chain_names.front()) {
                    continue;
                }
                auto working = units;
                working[index].entity.intent = EgirIntent::ProtectedSupport;
                working[index].required_cell = pose.cell;
                working[index].required_facing = pose.facing;
                auto growth_context = context;
                growth_context.stationary_boss = false;
                growth_context.boss_opening_only = false;
                growth_context.boss_movement_waypoints.clear();
                if (!growth_context.defended_chain_prefix.empty() &&
                    growth_context.defended_chain_prefix.front() == attacker.entity.id) {
                    growth_context.defended_chain_prefix.clear();
                }
                if (!growth_context.defended_chain_names.empty() &&
                    growth_context.defended_chain_names.front() == attacker.entity.name) {
                    growth_context.defended_chain_names.clear();
                }
                growth_context.evaluation_budget = std::min(800, context.evaluation_budget - evaluated);
                auto growth = plan_egir_core_chain(working, growth_context, units[engine_index].entity.id);
                evaluated += growth.evaluations;
                result.input_complete = result.input_complete || growth.input_complete;
                if (growth.found) {
                    growth.core = attacker.entity.id;
                    growth.attacking_core_separate_from_chain = true;
                    growth.boss_priority_attackers = 1;
                    growth.boss_core_movement_waypoints =
                        static_cast<int>(std::ranges::count_if(context.boss_movement_waypoints, [&](BoardCell cell) {
                            return boss_attack_overlaps_cell(pose, cell);
                        }));
                    growth.boss_surviving_attackers_movement_waypoints = egir_surviving_attackers_movement_coverage(
                        growth.units,
                        growth.conditional_consumption,
                        context);
                    growth.evaluations = evaluated;
                    growth.reason = "Provisional boss attacker with a separate defended Egir growth chain";
                    return growth;
                }
            }
            if (evaluated >= context.evaluation_budget) {
                break;
            }
        }
    }
    result.evaluations = evaluated;
    result.budget_exhausted = evaluated >= context.evaluation_budget;
    result.reason = "No protected boss-reaching attacker and compatible Egir chain within budget";
    return result;
}
