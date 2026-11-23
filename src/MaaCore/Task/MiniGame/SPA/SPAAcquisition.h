#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace asst::spa
{
struct OperatorFormCounts
{
    int initial = 0;
    int elite = 0;

    bool operator==(const OperatorFormCounts&) const = default;
};

// Subtract independently observed field forms from a reconciled physical total.
std::optional<OperatorFormCounts> subtract_operator_forms(OperatorFormCounts total, OperatorFormCounts field);

struct ObservedOperatorInventory
{
    // A candidate scan or a name-only ledger is not a complete observation.
    bool complete = false;
    std::map<std::string, OperatorFormCounts> members;
};

struct PactRecruitmentDelta
{
    bool verified = false;
    std::vector<std::string> gained_initial_operators;
    std::vector<std::string> promoted_operators;
    std::string reason;
};

// Compare complete physical snapshots across an otherwise isolated phone
// acquisition. Existing elite and initial copies remain separate. This does
// not prove item consumption, source attribution, field poses or trait gifts.
PactRecruitmentDelta verify_pact_recruitment_delta(
    const ObservedOperatorInventory& before,
    const ObservedOperatorInventory& after,
    const std::string& pact,
    int expected_grants);

bool verify_strategy_copy_delta(
    const ObservedOperatorInventory& before,
    const ObservedOperatorInventory& after,
    const std::string& recipient,
    bool elite);

bool strategy_copy_remained_unspent(
    const ObservedOperatorInventory& before,
    const ObservedOperatorInventory& first,
    const ObservedOperatorInventory& second,
    bool strategy_ready);

enum class AcquiredEffectKind
{
    Layers,
    FreeRefresh,
    Equipment,
    SpecialRefresh,
    Summon,
    Stateful
};

struct AcquiredEffect
{
    AcquiredEffectKind kind;
    std::string target;
    int amount = 0;
    bool randomized_quality = false;
    bool requires_observed_amount = false;
};

// Catalogue facts are not proof that the current unit, trait or skill can be triggered.
// Shop-dependent effects stay unknown until the shop level is observed.
std::optional<std::vector<AcquiredEffect>> acquired_effects(const std::string& name, bool elite, int shop_level = 0);
std::vector<std::string> acquired_operator_names();
// Guaranteed named gifts only; these are discovery hints, never inventory
// counts. Random quality, random names and stateful effects stay unasserted.
std::vector<std::string> acquired_equipment_hints(const std::string& name, bool elite);
}
