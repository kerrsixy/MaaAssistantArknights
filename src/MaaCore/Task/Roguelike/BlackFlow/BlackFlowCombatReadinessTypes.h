#pragma once

#include <string>
#include <vector>

namespace asst::blackflow
{
enum class CombatReadinessState
{
    Unknown,
    Supported,
    Missing,
};

struct CombatReadinessVariant
{
    std::string stage_key;
    std::vector<int> ordinary_healing_plans;
};

struct CombatReadinessProfile
{
    std::string id;
    std::string stage_title;
    std::vector<CombatReadinessVariant> variants;
};

// Necessary authored healing presence/geometry only, never an HPS or victory prediction.
struct CombatReadinessEvidence
{
    CombatReadinessState state = CombatReadinessState::Unknown;
    std::string profile_id;
    std::string profile_terms;
    std::string roster_scope;
    std::string reason;
    std::vector<std::string> variant_results;
    bool roster_complete = false;
};
}
