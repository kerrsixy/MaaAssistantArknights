#pragma once

#include <array>
#include <optional>
#include <vector>

namespace asst::blackflow
{
struct ProjectedPhysicalProfile
{
    double base_damage = 0;
    double base_period = 0;
    double skill_damage = 0;
    double skill_period = 0;
    double hits = 0;
    double sp_debt = 0;
    bool operator==(const ProjectedPhysicalProfile&) const = default;
};

// Numeric facts already validated against the actual identity/phase/rank source.
// Trust/potential uncertainty is represented by multiple variants/bounds.
struct PrimaryComponentVariant
{
    std::array<double, 2> pre_talent_attack { 0, 0 };
    double own_atk_fraction = 0;
    double own_aspd = 0;
    double base_period = 0;
    double skill_interval_add = 0;
    std::array<double, 2> skill_scale { 0, 0 };
    double hits = 0;
    double sp_cost = 0;
    double initial_sp = 0;
    double first_deploy_sp_upper = 0;
};

struct PrimaryExternalBounds
{
    std::array<double, 2> atk_fraction { 0, 0 };
    std::array<double, 2> aspd { 0, 0 };
};

struct PrimaryComponentEnvelope
{
    ProjectedPhysicalProfile lower;
    ProjectedPhysicalProfile upper;
};

struct PhysicalHitTerm
{
    double damage = 0;
    int count = 0;
};

// Separate hit terms preserve the defense floor per hit. Ammunition is an
// attack-capacity condition, not proof that all attacks occur before a deadline.
struct PhysicalHitSequence
{
    std::vector<PhysicalHitTerm> hits;
    double period = 0;
    std::optional<int> ammunition;
};

struct PhysicalHitScale
{
    std::array<double, 2> scale { 1, 1 };
    std::array<int, 2> count { 1, 1 };
};

struct PrimarySequenceMode
{
    std::array<double, 2> extra_atk_fraction { 0, 0 };
    std::array<double, 2> extra_aspd { 0, 0 };
    double interval_add = 0;
    std::vector<PhysicalHitScale> hits;
    std::optional<std::array<int, 2>> ammunition;
};

struct PhysicalSequenceEnvelope
{
    PhysicalHitSequence lower;
    PhysicalHitSequence upper;
};

// Reuses the original primary arithmetic for each distinct hit. These bounds
// describe one active attack mode, without weighting unknown mode durations.
[[nodiscard]] std::optional<PhysicalSequenceEnvelope> calculate_primary_sequence(
    const PrimaryComponentVariant& attributes,
    const PrimarySequenceMode& mode,
    const std::optional<PrimaryExternalBounds>& current_run_modifiers);

[[nodiscard]] std::optional<double> physical_sequence_damage(const PhysicalHitSequence& sequence, double defense);
[[nodiscard]] std::optional<double> physical_sequence_rate(const PhysicalHitSequence& sequence, double defense);
[[nodiscard]] std::optional<double> physical_ammunition_damage(const PhysicalHitSequence& sequence, double defense);

// Absent external input means a defined intrinsic-reference objective, not a
// claim that the actual run has zero external effects. Present input remains
// conditional on the complete current-run component contract at its adapter.
[[nodiscard]] std::optional<PrimaryComponentEnvelope> calculate_primary_component(
    const PrimaryComponentVariant& variant,
    const std::optional<PrimaryExternalBounds>& current_run_modifiers);

[[nodiscard]] bool primary_component_curves_strictly_better(
    const std::vector<ProjectedPhysicalProfile>& candidate_lower,
    const std::vector<ProjectedPhysicalProfile>& other_upper,
    bool require_no_more_sp_debt = true);
}
