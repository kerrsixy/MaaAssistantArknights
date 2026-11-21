#pragma once
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace asst::spa
{
inline bool is_kjerag_freeze_stacker(std::string_view name)
{
    return name == "初雪" || name == "圣聆初雪";
}

inline bool is_kjerag_freeze_provider(std::string_view name)
{
    return name == "灵知" || name == "极光" || name == "耶拉" || name == "圣聆初雪" || name == "凛御银灰";
}

inline bool is_kjerag_freeze_equipment(std::string_view name)
{
    return name == "谢拉格不融冰" || name == "进阶谢拉格不融冰";
}

enum class GrowthKind
{
    FixedPhase,
    CappedConditional,
    SalePriceDependent,
    TemporaryInjection,
    Conversion,
    // A known per-member phase trigger whose eligible population still
    // requires observed row/equipment state before projecting its yield.
    PopulationPhase,
    SupportOnly,
    Unmodeled
};
enum class GrowthPhase
{
    PreparationStart,
    PreparationEnd,
    BattleStart,
    BattleEvent,
    BattleEnd,
    ShopRefresh
};

struct GrowthFormula
{
    int normal_gain = 0;
    int elite_gain = 0;
    int normal_cap = 0;
    int elite_cap = 0;
};

struct GrowthProfile
{
    std::string name;
    GrowthKind kind;
    GrowthPhase phase;
    std::vector<std::string> pacts;
    int normal_gain = 0;
    int elite_gain = 0;
    int normal_cap = 0;
    int elite_cap = 0;
    std::string normal_description;
    std::string elite_description;
    // The numeric entry covers printed recipients only. Recipients selected
    // by position or current layer ranking remain separate until their
    // identities/poses and trigger conditions are observed.
    bool dynamic_recipients = false;
    bool works_on_bench = false;
    // A single trigger may have different gains/caps for different pacts.
    std::map<std::string, GrowthFormula> pact_formulas;
    std::vector<GrowthPhase> additional_phases;
};

const std::map<std::string, GrowthProfile>& growth_profiles();
bool is_primary_growth_engine(const std::string& name, const std::string& pact);
bool is_redeployment_growth_engine(const std::string& name, const std::vector<std::string>& active_pacts, bool elite);

// A tactical preference only. The actual trigger, active pact and board
// adjacency must still be checked by the caller; this does not project layers.
bool is_front_growth_target(const std::string& source, const std::string& target, const std::string& primary);

// These traits raise the sale quote read by a separate growth source.
inline bool has_iberia_sale_price_trait(const std::string& name)
{
    return name == "温蒂" || name == "海沫";
}

struct GrowthSource
{
    std::string name;
    bool elite = false;
    bool deployed = true;
    std::optional<int> eligible_population;
};

struct UrsusLayerCycle
{
    int opening_temporary_layers = 0;
    int conversion_capacity = 0;
    int converted_from_opening = 0;
    int minimum_temporary_layers_per_qualifying_defeat = 0;
    int converted_with_one_qualifying_defeat = 0;
    bool conversion_population_unknown = false;
};

// One unchanged field from battle start through the next preparation.
// A single qualifying defeat is a separate scenario, not an assumed outcome.
UrsusLayerCycle assess_ursus_layer_cycle(
    const std::vector<GrowthSource>& sources,
    std::optional<int> distinct_ursus_members = std::nullopt);

struct GrowthProjection
{
    bool valid = false;
    int preparation_ends = 0;
    int completed_battles_before_boss = 0;
    int fixed_phase_additions = 0;
    int conditional_cap_additions = 0;
    int conditional_preparation_cap_additions = 0;
    int population_phase_additions_if_unchanged = 0;
    UrsusLayerCycle ursus_cycle;
    int ursus_converted_opening_layers_if_unchanged = 0;
    int conversion_sources = 0;
    int sale_price_sources = 0;
    int temporary_sources = 0;
    int unmodeled_sources = 0;
    std::vector<std::string> engine_names;
};

// Scenario only: current preparation -> immediately before boss combat starts.
// Assumes the pact remains active and sources remain eligible. Conditional
// battle/refresh caps are scenarios, not expected yields or unused charges.
GrowthProjection project_primary_growth(
    const std::string& pact,
    int round,
    int boss_round,
    bool active,
    const std::vector<GrowthSource>& deployed,
    std::optional<int> distinct_ursus_members = std::nullopt);
}
