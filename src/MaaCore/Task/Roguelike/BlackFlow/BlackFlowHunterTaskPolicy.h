#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <meojson/json.hpp>

#include "Common/AsstBattleDef.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowHunterPreludeAnalyzer.h"

namespace asst::blackflow
{
enum class HunterTaskKind
{
    Medical,
    Guard,
    AoeRisk
};

struct HunterTaskCandidate
{
    battle::OperNameTag oper;
    std::string operator_id;
    int elite = -1;
    int selected_skill = 0;
    int skill_rank = -1;
    bool preparation_known = false;
    // Legacy field layout retained; Hunter now binds this to uniqueness for an
    // outstanding qualified guard task, never a total ordinary-blocker count.
    bool sole_persistent_blocker = false;
    Point location;
    battle::DeployDirection direction = battle::DeployDirection::None;
    battle::AttackRange range;
};

struct HunterGuardTask
{
    int plan_index = -1;
    Point location;
    battle::DeployDirection direction = battle::DeployDirection::None;
};

struct HunterTaskWorld
{
    bool normal_verified = false;
    bool spawn_contract_verified = false;
    bool first_bait_consumed = false;
    bool bait_removal_verified = false;
    bool guard_present = false;
    bool medical_present = false;
    std::array<HunterSpawnObservation, 2> spawns;
    std::array<Point, 2> spawn_tiles { Point { 2, 2 }, Point { 4, 2 } };
    std::vector<Point> lower_possible_tiles;
    std::vector<Point> upper_positive_possible_tiles;
    bool upper_visible = false;
    bool upper_scope_covers_spawn_rois = false;
    HunterPresence upper_presence = HunterPresence::Unknown;
    int upper_empty_frames = 0;
    bool lower_visible = false;
    HunterPresence lower_presence = HunterPresence::Unknown;
};

struct HunterTaskPermit
{
    bool allowed = false;
    bool accepted_aoe_uncertainty = false;
    std::string reason;
};

// Versioned base-effect facts, not a DPS model or a claim that every relic/module
// modifier is known. Unknown identities/skills/ranges retain the original gate.
class BlackFlowHunterTaskPolicy
{
public:
    bool load(const json::value& value, std::string* error = nullptr);
    [[nodiscard]] bool has_plan(int plan_index) const noexcept;
    [[nodiscard]] HunterTaskPermit
        assess(int plan_index, const HunterTaskCandidate& candidate, const HunterTaskWorld& world) const;
    [[nodiscard]] bool reserve_guard_identity(const HunterTaskCandidate& candidate) const;
    [[nodiscard]] std::vector<HunterGuardTask> guard_tasks() const;
    [[nodiscard]] bool guard_identity_required(
        const HunterTaskCandidate& requested,
        const std::vector<HunterTaskCandidate>& projected,
        const std::vector<int>& satisfied_plans,
        const HunterTaskWorld& world) const;
    [[nodiscard]] bool skill_known_allowed(const HunterTaskCandidate& candidate) const;
    [[nodiscard]] HunterTaskKind kind(int plan_index) const;
    [[nodiscard]] Point location(int plan_index) const;

private:
    struct Rule
    {
        int plan_index = -1;
        HunterTaskKind kind = HunterTaskKind::Medical;
        Point location;
        battle::DeployDirection direction = battle::DeployDirection::None;
        std::string operator_id;
        int min_elite = 0;
        int max_elite = 2;
        int selected_skill = 0;
        int min_skill_rank = 1;
        std::optional<double> self_effect_radius;
        battle::AttackRange base_right_range;
    };

    [[nodiscard]] bool matches(const Rule& rule, const HunterTaskCandidate& candidate, bool position) const;
    [[nodiscard]] bool guard_effect_domain_allowed(
        const Rule& rule,
        const HunterTaskCandidate& candidate,
        const HunterTaskWorld& world) const;
    std::map<int, std::vector<Rule>> m_rules;
};
}
