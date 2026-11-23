#pragma once

#include "SPADeploymentReview.h"
#include <functional>
#include <optional>

namespace asst::spa
{
enum class EgirIntent
{
    Core,
    Relay,
    Material,
    ProtectedSupport
};

enum class EgirSixMemberRule
{
    Revive,
    ReplaceFromBench
};

struct EgirEntity
{
    std::string id;
    std::string name;
    BoardCell cell;
    char facing = 0;
    bool egir_member = false;
    int tier = 0; // Zero is unknown, including an unclassified summon.
    EgirIntent intent = EgirIntent::Material;
    std::optional<double> base_attack;
    std::optional<int> block_count;
    std::optional<double> hp_before_consumption;
    // Post-mitigation damage of one mark, only when actually established.
    // The printed 5000 physical damage is not automatically this value.
    std::optional<double> damage_per_mark;
    bool counts_for_literal_egir_milestone = true;
};

// Current attacking role, separate from a configuration's collection of
// possible carries. Dollkeeper remains a relay. An ambiguous physical copy
// is unknown rather than several protected output heads.
int egir_output_priority(const std::string& name);
std::optional<std::string> designate_egir_output(std::vector<EgirEntity>& entities);

struct EgirConsumptionSource
{
    std::string source;
    std::vector<std::string> marked_targets;
    std::optional<double> inherited_base_attack;
    std::optional<int> inherited_block_count;
};

struct EgirConsumptionVictim
{
    std::string id;
    int marks = 0;
    std::optional<bool> defeated;
    bool protected_target = false;
};

struct EgirConsumptionProjection
{
    bool complete = false;
    bool active = false;
    // The marked-closure mechanism is documented for the older season.
    // Current printed values/order are from act3autochess; an actual current
    // battle must establish closure/damage timing before this is acceptance.
    bool current_season_semantics_verified = false;
    int distinct_egir_members = 0;
    int printed_damage_value = 5000;
    int base_six_member_revive_limit = 0;
    int six_member_bench_replacement_limit = 0;
    std::optional<int> conditional_unique_target_layers;
    std::vector<EgirConsumptionSource> sources;
    std::vector<EgirConsumptionVictim> victims;
    std::vector<std::string> problems;
};

// A conditional mechanism projection, not an observed gain or an executable
// formation. Physical copies have separate ids; activation still counts names.
EgirConsumptionProjection project_egir_mark_closure(
    const std::vector<EgirEntity>& entities,
    bool pact_active,
    EgirSixMemberRule six_member_rule = EgirSixMemberRule::Revive);

struct EgirPlacementUnit
{
    EgirEntity entity;
    DeploymentUnit deployment;
    int core_priority = 0; // Explicit Egir role ordering, not a generic combat score.
    bool expendable_material = false;
    int same_row_recruitment_members = 0;
    bool eligible_chain_head = true;
    // A boss attacker can be fixed separately from the growth chain. Its
    // observed source cell is retained for journaled placement sequencing.
    std::optional<BoardCell> required_cell;
    std::optional<char> required_facing;
    bool preferred_boss_output = false;
};

struct EgirFormationContext
{
    bool pact_active = false;
    BoardCell home;
    std::vector<DeploymentLane> lanes;
    bool stationary_boss = false;
    // Client-route predictions anchored by the recognized boss phase and map.
    std::vector<BoardCell> boss_cells;
    bool boss_cells_are_alternatives = false;
    // Logical board direction. Normal defense uses its registered gate side;
    // boss attacks evaluate each cardinal direction that can both hit and feed.
    char chain_facing = 'E';
    EgirSixMemberRule six_member_rule = EgirSixMemberRule::Revive;
    int evaluation_budget = 4000;
    // Physical ids at the top of the food chain, never the terminal donors.
    // Normal waves anchor the output head immediately beside the blue gate.
    // A boss attack pose takes precedence over this defensive anchor.
    std::vector<std::string> defended_chain_prefix;
    // Preferred roles can choose any eligible physical copy. Unlike a
    // straight coordinate prefix, these names allow relays to turn corners.
    std::vector<std::string> defended_chain_names;
    // Reject an unreachable target or an already failed first physical input
    // while the planner still has an opportunity to examine alternatives.
    std::function<bool(const std::vector<DeploymentUnit>&)> target_admissible;
    bool boss_opening_only = false;
    std::vector<BoardCell> boss_movement_waypoints;
};

// Append a preferred physical member only when it can occupy the next
// defended-chain cell. An immovable duplicate elsewhere remains in the field
// and can still feed the chain; it must not make the whole prefix impossible.
bool append_egir_defended_member(
    std::vector<std::string>& prefix,
    const std::vector<EgirPlacementUnit>& units,
    const std::string& name,
    BoardCell home);
bool append_egir_defended_name(
    std::vector<std::string>& names,
    const std::vector<EgirPlacementUnit>& units,
    const std::string& name,
    BoardCell home,
    char head_facing = 'E');

struct EgirFormationPlan
{
    bool input_complete = false;
    bool found = false;
    bool budget_exhausted = false;
    int evaluations = 0;
    int changed_units = 0;
    std::string core;
    bool attacking_core_separate_from_chain = false;
    int boss_priority_attackers = 0;
    int boss_chain_attack_priority = 0;
    int boss_core_movement_waypoints = 0;
    int boss_surviving_attackers_movement_waypoints = 0;
    std::vector<std::string> chain;
    std::vector<DeploymentUnit> units;
    EgirConsumptionProjection conditional_consumption;
    // Directed simple chains remain provisional until current-season
    // closure and damage are observed. Camera mirroring is a separate input.
    bool current_season_semantics_verified = false;
    std::string reason;
};

EgirFormationPlan plan_egir_formation(const std::vector<EgirPlacementUnit>& units, const EgirFormationContext& context);
EgirFormationPlan plan_egir_core_chain(
    const std::vector<EgirPlacementUnit>& units,
    const EgirFormationContext& context,
    const std::string& forced_core);
}
