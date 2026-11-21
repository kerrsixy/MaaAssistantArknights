#pragma once

#include "SPADeploymentReview.h"
#include "SPAFormation.h"
#include "SPAPlacementPolicy.h"
#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace asst::spa
{
struct ReplacementMember
{
    std::string name;
    PlacementRole role = PlacementRole::Unknown;
    std::vector<std::string> pacts;
    int value = 0;
    int x = -1;
    int y = -1;
    bool replaceable = false;
    bool accepts_direct_healing = true;
    bool provides_regeneration = false;
    bool route_membership_registered = false;
    std::vector<std::string> terrain_routes;
    int egir_material_tier = 0;
    // A dollkeeper's substitute cannot secure a ground lane. Its normal
    // body still uses ground deployment, including off-route relay cells.
    bool reliable_blocker = true;
    bool strategy_core = false;
    std::map<std::string, size_t> terrain_route_progress;
};

inline bool replacement_blocks_ground(const ReplacementMember& member)
{
    return member.role == PlacementRole::Frontline && member.reliable_blocker;
}

// Cells come from a camera-registered field snapshot. These memberships are
// predicted terrain routes, not observations of a particular enemy's path.
inline bool register_replacement_routes(
    std::vector<ReplacementMember>& field,
    const std::vector<GridPlacementMember>& grid,
    const std::vector<DeploymentLane>& lanes)
{
    for (auto& unit : field) {
        unit.route_membership_registered = false;
        unit.terrain_routes.clear();
        unit.terrain_route_progress.clear();
    }
    if (field.empty() || field.size() != grid.size() || lanes.empty()) {
        return false;
    }
    std::set<BoardCell> cells;
    std::set<std::string> route_ids;
    for (const auto& lane : lanes) {
        if (lane.id.empty() || lane.path.size() < 2 || !route_ids.insert(lane.id).second) {
            return false;
        }
    }
    for (size_t index = 0; index < grid.size(); ++index) {
        if (grid[index].name != field[index].name || grid[index].col < 0 || grid[index].col > 10 ||
            grid[index].row < 0 || grid[index].row > 4 || !cells.emplace(grid[index].col, grid[index].row).second) {
            return false;
        }
    }
    for (size_t index = 0; index < grid.size(); ++index) {
        auto& unit = field[index];
        unit.route_membership_registered = true;
        for (const auto& lane : lanes) {
            const auto step =
                std::find(lane.path.begin(), lane.path.end(), BoardCell { grid[index].col, grid[index].row });
            if (step != lane.path.end()) {
                unit.terrain_routes.push_back(lane.id);
                unit.terrain_route_progress.emplace(lane.id, static_cast<size_t>(step - lane.path.begin()));
            }
        }
    }
    return true;
}

inline bool screened_replacement_cell(const ReplacementMember& outgoing, const std::vector<ReplacementMember>& field)
{
    if (!outgoing.route_membership_registered) {
        return !replacement_blocks_ground(outgoing);
    }
    for (const auto& lane : outgoing.terrain_routes) {
        const auto target = outgoing.terrain_route_progress.find(lane);
        if (target == outgoing.terrain_route_progress.end()) {
            return false;
        }
        const bool covered = std::any_of(field.begin(), field.end(), [&](const auto& blocker) {
            const auto step = blocker.terrain_route_progress.find(lane);
            return replacement_blocks_ground(blocker) && blocker.route_membership_registered &&
                   step != blocker.terrain_route_progress.end() && step->second < target->second;
        });
        if (!covered) {
            return false;
        }
    }
    return true;
}

struct ReplacementChoice
{
    bool found = false;
    std::string incoming;
    std::string outgoing;
    int x = -1;
    int y = -1;
    int improvement = 0;
    bool restores_strategy_core = false;
};

using ReplacementLineup = std::vector<std::string>;

inline ReplacementLineup replacement_lineup_identity(const std::vector<ReplacementMember>& field)
{
    ReplacementLineup names;
    names.reserve(field.size());
    for (const auto& unit : field) {
        names.push_back(unit.name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

// A missing green preview is a round-local observation failure, not a
// permanent terrain prohibition. Source movement and facing changes alone
// do not alter the vacant receiver's neighborhood.
inline std::optional<std::string> spatial_preview_failure_key(
    const std::string& map_context,
    int round,
    const DeploymentStep& step,
    const std::vector<GridPlacementMember>& field,
    const std::map<std::string, bool>& forms)
{
    if (map_context.empty() || round < 1 || step.kind != DeploymentStepKind::Move || !forms.contains(step.unit) ||
        step.to.first < 0 || step.to.first > 10 || step.to.second < 0 || step.to.second > 4) {
        return std::nullopt;
    }
    int sources = 0;
    std::set<std::string> neighbors;
    for (const auto& member : field) {
        if (member.name == step.unit) {
            if (BoardCell(member.col, member.row) != step.from) {
                return std::nullopt;
            }
            ++sources;
            continue;
        }
        if (BoardCell(member.col, member.row) == step.to) {
            return std::nullopt;
        }
        if (std::abs(member.col - step.to.first) <= 1 && std::abs(member.row - step.to.second) <= 1) {
            if (!forms.contains(member.name)) {
                return std::nullopt;
            }
            neighbors.insert(
                member.name + ":" + std::to_string(member.col) + "." + std::to_string(member.row) +
                (forms.at(member.name) ? ":elite" : ":initial"));
        }
    }
    if (sources != 1) {
        return std::nullopt;
    }
    std::string key = "spatial-preview-v1:" + map_context + ":" + std::to_string(round) + ":" + step.unit +
                      (forms.at(step.unit) ? ":elite:" : ":initial:") + std::to_string(step.to.first) + "." +
                      std::to_string(step.to.second);
    for (const auto& neighbor : neighbors) {
        key += "|" + neighbor;
    }
    return key;
}

// Validate every input against its projected pre-input field. This is a
// reachability filter only; runtime evidence must still verify each step.
inline bool spatial_sequence_admissible(
    const DeploymentSequence& sequence,
    std::vector<GridPlacementMember> field,
    const std::function<bool(const DeploymentStep&, const std::vector<GridPlacementMember>&)>& accepts)
{
    if (!sequence.complete || !accepts) {
        return false;
    }
    for (const auto& step : sequence.steps) {
        auto source = std::find_if(field.begin(), field.end(), [&](const auto& member) {
            return member.name == step.unit && BoardCell(member.col, member.row) == step.from;
        });
        if (source == field.end() || !accepts(step, field)) {
            return false;
        }
        if (step.kind == DeploymentStepKind::Swap) {
            auto other = std::find_if(field.begin(), field.end(), [&](const auto& member) {
                return member.name == step.displaced && BoardCell(member.col, member.row) == step.to;
            });
            if (other == field.end() || other == source) {
                return false;
            }
            other->col = step.from.first;
            other->row = step.from.second;
            other->facing = '\0';
        }
        else if (step.kind == DeploymentStepKind::Move && std::any_of(field.begin(), field.end(), [&](const auto& m) {
                     return BoardCell(m.col, m.row) == step.to;
                 })) {
            return false;
        }
        else if (step.kind == DeploymentStepKind::Face && step.from != step.to) {
            return false;
        }
        source->col = step.to.first;
        source->row = step.to.second;
        source->facing = step.facing;
    }
    return true;
}

// A distant recruit does not change the held source or target's local
// occlusion. Reconsider a failed spatial input only after a relevant change.
inline std::optional<std::string> spatial_action_failure_key(
    const std::string& map_context,
    const DeploymentStep& step,
    const std::vector<GridPlacementMember>& field,
    const std::map<std::string, bool>& forms)
{
    const auto valid = [](BoardCell cell) {
        return cell.first >= 0 && cell.first <= 10 && cell.second >= 0 && cell.second <= 4;
    };
    if (map_context.empty() || step.unit.empty() || !valid(step.from) || !valid(step.to) ||
        std::string("ENSW").find(step.facing) == std::string::npos) {
        return std::nullopt;
    }
    int sources = 0;
    std::set<std::string> neighbors;
    for (const auto& member : field) {
        sources += member.name == step.unit && BoardCell(member.col, member.row) == step.from;
        const auto near_endpoint = [&](BoardCell cell) {
            return std::abs(member.col - cell.first) <= 1 && std::abs(member.row - cell.second) <= 1;
        };
        if (!near_endpoint(step.from) && !near_endpoint(step.to)) {
            continue;
        }
        const auto form = forms.find(member.name);
        if (form == forms.end()) {
            return std::nullopt;
        }
        neighbors.insert(
            member.name + ":" + std::to_string(member.col) + "." + std::to_string(member.row) +
            (form->second ? ":elite:" : ":initial:") + (member.facing ? std::string(1, member.facing) : "unknown"));
    }
    if (sources != 1) {
        return std::nullopt;
    }
    auto key = "spatial-local-v2:" + map_context + ":" + std::to_string(static_cast<int>(step.kind)) + ":" + step.unit +
               ":" + std::to_string(step.from.first) + "." + std::to_string(step.from.second) + ">" +
               std::to_string(step.to.first) + "." + std::to_string(step.to.second) + ":" + step.facing + ":" +
               step.displaced;
    for (const auto& member : neighbors) {
        key += "|" + member;
    }
    return key;
}

// Failure to establish the receiver happens before the requested facing is
// entered. Changing E to S/N cannot repair that same camera/identity stage.
inline std::optional<std::string> spatial_receiver_failure_key(
    const std::string& map_context,
    const DeploymentStep& step,
    const std::vector<GridPlacementMember>& field,
    const std::map<std::string, bool>& forms)
{
    auto independent = step;
    independent.facing = 'E';
    auto key = spatial_action_failure_key(map_context, independent, field, forms);
    if (key) {
        *key = "spatial-receiver-v1:" + key->substr(std::string("spatial-local-v2:").size());
    }
    return key;
}

// A release onto a vacant tile can return directly to the unchanged board.
// Remember this only for that operator form, map phase and pair of cells.
inline std::optional<std::string>
    spatial_vacant_drop_failure_key(const std::string& map_context, const DeploymentStep& step, bool elite)
{
    if (map_context.empty() || step.kind != DeploymentStepKind::Move || step.from == step.to) {
        return std::nullopt;
    }
    return "spatial-drop-v1:" + map_context + ":" + step.unit + (elite ? ":elite:" : ":initial:") +
           std::to_string(step.from.first) + "." + std::to_string(step.from.second) + ">" +
           std::to_string(step.to.first) + "." + std::to_string(step.to.second);
}

inline std::optional<std::string> replacement_target_failure_key(
    const std::string& map_id,
    const std::string& incoming,
    bool incoming_elite,
    int incoming_copies,
    BoardCell target,
    const std::vector<GridPlacementMember>& field,
    const std::map<std::string, bool>& forms)
{
    if (map_id.empty() || incoming.empty() || incoming_copies < 1 || field.empty()) {
        return std::nullopt;
    }
    bool occupied = false;
    std::set<std::string> neighbors;
    for (const auto& member : field) {
        if (member.col == target.first && member.row == target.second) {
            occupied = true;
        }
        if (std::abs(member.col - target.first) > 1 || std::abs(member.row - target.second) > 1) {
            continue;
        }
        const auto form = forms.find(member.name);
        if (form == forms.end()) {
            return std::nullopt;
        }
        neighbors.insert(
            member.name + ":" + std::to_string(member.col) + "." + std::to_string(member.row) +
            (form->second ? ":elite" : ":initial"));
    }
    if (!occupied) {
        return std::nullopt;
    }
    auto key = "replacement-target:" + map_id + ":" + incoming + (incoming_elite ? ":elite:" : ":initial:") +
               std::to_string(incoming_copies) + ":" + std::to_string(target.first) + "." +
               std::to_string(target.second);
    for (const auto& member : neighbors) {
        key += "|" + member;
    }
    return key;
}

// There must be no same-name copy on the bench before the swap, otherwise
// observing that name there afterwards cannot prove this copy moved out.
// A single elite is identifiable; mixed elite/initial inventories are not.
inline bool replacement_outgoing_identity_trackable(bool elite, int owned_copies, int field_copies)
{
    return owned_copies > 0 && field_copies == owned_copies && (!elite || owned_copies == 1);
}

inline int replacement_primary_members(const std::vector<ReplacementMember>& field, const std::string& primary)
{
    std::set<std::string> distinct;
    for (const auto& member : field) {
        if (std::find(member.pacts.begin(), member.pacts.end(), primary) != member.pacts.end()) {
            distinct.insert(member.name);
        }
    }
    return static_cast<int>(distinct.size());
}

inline int replacement_lineup_value(const std::vector<ReplacementMember>& field, const std::string& primary)
{
    std::map<std::string, int> distinct_values;
    int egir_material = 0;
    int blockers = 0, ranged = 0, healers = 0;
    for (const auto& member : field) {
        distinct_values[member.name] = std::max(distinct_values[member.name], member.value);
        if (primary == "阿戈尔" && member.egir_material_tier >= 1 && member.egir_material_tier <= 6 &&
            std::find(member.pacts.begin(), member.pacts.end(), primary) != member.pacts.end()) {
            // Every physical Egir body can extend consumption, including a
            // newly recruited distinct relay. Crediting only duplicates
            // wrongly favored a spare Skadi over the first dollkeeper.
            // Distinct-name activation is still counted separately below.
            // This is a placement preference, not an observed layer payout.
            egir_material += member.egir_material_tier * 350;
        }
        blockers += replacement_blocks_ground(member);
        ranged += member.role == PlacementRole::Ranged;
        healers += member.role == PlacementRole::Healer;
    }
    const bool regeneration = std::any_of(field.begin(), field.end(), [](const auto& member) {
        return member.role == PlacementRole::Healer && member.provides_regeneration;
    });
    const int healing_recipients = static_cast<int>(std::count_if(field.begin(), field.end(), [&](const auto& member) {
        return member.role != PlacementRole::Healer && (member.accepts_direct_healing || regeneration);
    }));
    // Sustain is more valuable when many allies can use it. This remains a
    // composition score, not a mandatory healer rule or a claim of coverage.
    const int sustain = healers > 0 && healing_recipients > 0 ? 700 + healing_recipients * 400 : 0;
    int value = std::min(blockers, 2) * 800 + std::min(ranged, 2) * 900 + sustain;
    for (const auto& [name, strength] : distinct_values) {
        value += strength;
    }
    const bool harmony = std::any_of(field.begin(), field.end(), [](const auto& unit) {
        return std::find(unit.pacts.begin(), unit.pacts.end(), "调和") != unit.pacts.end();
    });
    const int distinct_primary = replacement_primary_members(field, primary);
    const int primary_members = distinct_primary + (harmony ? 1 : 0);
    static const auto rules = pact_requirements();
    const auto rule = std::find_if(rules.begin(), rules.end(), [&](const auto& item) { return item.name == primary; });
    // The additional core effect requires literal distinct members. Harmony
    // contributes to base activation, never to this six-member condition.
    // This is a composition preference, not a damage or survival forecast;
    // a sufficiently valuable role repair may still outweigh the milestone.
    const int advanced =
        rule != rules.end() && rule->core && rule->advanced_members > 0 && distinct_primary >= rule->advanced_members
            ? 2400
            : 0;
    return value + primary_members * 400 + (primary_members >= 3 ? 5000 + egir_material : 0) + advanced;
}

inline int replacement_support_pact_value(
    const std::vector<ReplacementMember>& field,
    const std::vector<std::string>& supports,
    const std::vector<std::string>& disabled)
{
    int value = 0;
    for (const auto& rule : pact_requirements()) {
        if (rule.core || rule.bench_counts ||
            std::find(supports.begin(), supports.end(), rule.name) == supports.end() ||
            std::find(disabled.begin(), disabled.end(), rule.name) != disabled.end()) {
            continue;
        }
        const int distinct = replacement_primary_members(field, rule.name);
        value += std::min(distinct, rule.members) * 400;
        if (distinct >= rule.members) {
            value += 1600;
        }
    }
    return value;
}

// Compare the resulting lineups, rather than selling a low-tier unit merely
// because a higher-tier one exists. Keep an activated core and the last known
// blocker of each lane. Unknown poses may contribute to value but are never
// selected as executable outgoing targets.
inline ReplacementChoice choose_replacement(
    const std::string& primary,
    const std::vector<ReplacementMember>& field,
    const std::vector<ReplacementMember>& bench,
    const std::set<std::tuple<std::string, std::string, int, int>>& blocked = {},
    const std::function<int(const std::vector<ReplacementMember>&)>& evaluate = {},
    const std::set<ReplacementLineup>& visited_lineups = {},
    const std::vector<std::string>& disabled_pacts = {})
{
    ReplacementChoice best;
    if (primary.empty() || field.empty() || field.size() > 16 || bench.size() > 10) {
        return best;
    }
    // Context-dependent growth/support values must be recomputed for each
    // complete lineup. Reusing a benched helper's current-context score can
    // make A -> B and B -> A both look profitable without any new evidence.
    const auto value = [&](const auto& lineup) {
        return evaluate ? evaluate(lineup) : replacement_lineup_value(lineup, primary);
    };
    const int baseline = value(field);
    int best_rank = 0;
    const int strategy_cores = static_cast<int>(
        std::count_if(field.begin(), field.end(), [](const auto& unit) { return unit.strategy_core; }));
    const auto activation_members = [&](const auto& lineup) {
        const bool harmony = std::any_of(lineup.begin(), lineup.end(), [](const auto& unit) {
            return std::find(unit.pacts.begin(), unit.pacts.end(), "调和") != unit.pacts.end();
        });
        return replacement_primary_members(lineup, primary) + (harmony ? 1 : 0);
    };
    const int members = activation_members(field);
    const auto blockers = [](const auto& lineup, bool lower) {
        return std::count_if(lineup.begin(), lineup.end(), [&](const auto& unit) {
            return replacement_blocks_ground(unit) && unit.y >= 0 && (unit.y > 335) == lower;
        });
    };
    const auto upper = blockers(field, false), lower = blockers(field, true);
    const bool routes_registered =
        std::all_of(field.begin(), field.end(), [](const auto& unit) { return unit.route_membership_registered; });
    std::set<std::string> intercepted_routes;
    for (const auto& unit : field) {
        if (replacement_blocks_ground(unit) && unit.route_membership_registered) {
            intercepted_routes.insert(unit.terrain_routes.begin(), unit.terrain_routes.end());
        }
    }
    for (const auto& incoming : bench) {
        if (incoming.name.empty()) {
            continue;
        }
        for (size_t index = 0; index < field.size(); ++index) {
            const auto& outgoing = field[index];
            if (!outgoing.replaceable || (outgoing.strategy_core && strategy_cores <= 1) ||
                outgoing.name == incoming.name || outgoing.x < 180 || outgoing.x >= 1000 || outgoing.y < 160 ||
                outgoing.y >= 495 || blocked.contains({ incoming.name, outgoing.name, outgoing.x, outgoing.y })) {
                continue;
            }
            if (needs_screened_position(incoming.name) && !screened_replacement_cell(outgoing, field)) {
                continue;
            }
            // A bench guard cannot inherit an arbitrary rear support tile.
            // Require a registered interception cell before attempting the
            // drag; the held green gate still verifies actual buildability.
            if (replacement_blocks_ground(incoming) && !replacement_blocks_ground(outgoing) &&
                (!outgoing.route_membership_registered || outgoing.terrain_routes.empty())) {
                continue;
            }
            auto proposed = field;
            proposed[index] = incoming;
            if (!visited_lineups.empty() && visited_lineups.contains(replacement_lineup_identity(proposed))) {
                continue;
            }
            proposed[index].x = outgoing.x;
            proposed[index].y = outgoing.y;
            proposed[index].route_membership_registered = outgoing.route_membership_registered;
            proposed[index].terrain_routes = outgoing.terrain_routes;
            proposed[index].terrain_route_progress = outgoing.terrain_route_progress;
            const bool opens_route =
                std::any_of(intercepted_routes.begin(), intercepted_routes.end(), [&](const auto& route) {
                    return std::none_of(proposed.begin(), proposed.end(), [&](const auto& unit) {
                        return replacement_blocks_ground(unit) && unit.route_membership_registered &&
                               std::find(unit.terrain_routes.begin(), unit.terrain_routes.end(), route) !=
                                   unit.terrain_routes.end();
                    });
                });
            if ((members >= 3 && activation_members(proposed) < 3) || opens_route ||
                (!routes_registered &&
                 ((upper > 0 && blockers(proposed, false) == 0) || (lower > 0 && blockers(proposed, true) == 0)))) {
                continue;
            }
            const int improvement = value(proposed) - baseline;
            const bool restores_core = incoming.strategy_core && strategy_cores == 0;
            const bool same_auxiliary_role =
                incoming.role == outgoing.role && incoming.role != PlacementRole::Unknown &&
                std::ranges::any_of(incoming.pacts, [&](const auto& pact) {
                    return pact != primary && std::ranges::find(disabled_pacts, pact) == disabled_pacts.end() &&
                           std::ranges::find(outgoing.pacts, pact) != outgoing.pacts.end();
                });
            // A same-role exchange sharing a live pact preserves the old
            // slot's job. Apply this preference only after proving a net
            // lineup improvement, so a sideways exchange cannot cycle.
            const int rank = improvement + (same_auxiliary_role ? 800 : 0);
            // Restore a strategy foundation before optimizing faction bonuses.
            // Route coverage and active-pact constraints above still apply.
            if ((restores_core || improvement > 100) &&
                (!best.found || (restores_core && !best.restores_strategy_core) ||
                 (restores_core == best.restores_strategy_core && rank > best_rank))) {
                best = { true, incoming.name, outgoing.name, outgoing.x, outgoing.y, improvement, restores_core };
                best_rank = rank;
            }
        }
    }
    return best;
}
}
