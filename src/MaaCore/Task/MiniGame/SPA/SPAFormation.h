#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace asst::spa
{
struct PactRequirement
{
    std::string name;
    int members = 0;
    bool bench_counts = false;
    bool core = false;
    int advanced_members = 0;
};

struct RosterUnit
{
    std::string name;
    bool deployed = false;
    std::vector<std::string> added_pacts;
    std::optional<bool> elite = std::nullopt;
};

struct FormationEvaluation
{
    // Completeness of member/elite observations, not all combat mechanics.
    bool complete = true;
    std::map<std::string, int> distinct_members;
    std::vector<std::string> active_pacts;
    // Member-count milestones only; layer milestones require separate observations.
    std::vector<std::string> advanced_pacts;
    std::map<std::string, int> activation_members;
    std::optional<int> amiya_bonus_percent;
};

// A 3/3 formation is a game-side input, not evidence by itself.  This helper
// selects only distinct, catalogue-known operators visible on the current
// formation page and requires enough members to activate the requested pact.
// It is intentionally pure so the selection rule can be regression-tested
// without a device or OCR runtime.
struct FormationCandidateSelection
{
    bool valid = false;
    std::string core_pact;
    int required_members = 0;
    int score = 0;
    std::vector<std::string> operators;
    std::vector<std::string> eligible_visible;
    std::string reason;
};

FormationEvaluation evaluate_formation(
    const std::vector<RosterUnit>& roster,
    const std::map<std::string, std::vector<std::string>>& catalog,
    const std::vector<PactRequirement>& requirements,
    const std::vector<std::string>& enabled_pacts);
FormationCandidateSelection choose_formation_candidates(
    const std::string& requested_core_pact,
    const std::vector<std::string>& visible_names,
    const std::map<std::string, std::vector<std::string>>& catalog,
    const std::vector<PactRequirement>& requirements);
std::map<std::string, std::vector<std::string>> operator_pacts();
std::vector<PactRequirement> pact_requirements();
// Core availability comes from the match configuration; eligible auxiliary
// pacts must also participate in activation, including bench-counting rules.
std::vector<std::string> enabled_formation_pacts(
    const std::vector<std::string>& enabled_core_pacts,
    const std::vector<std::string>& disabled_pacts);
}
