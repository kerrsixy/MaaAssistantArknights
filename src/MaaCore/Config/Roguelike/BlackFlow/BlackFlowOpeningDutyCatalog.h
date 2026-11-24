#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <meojson/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace asst::blackflow
{
enum class BaseOpeningDuty
{
    Unknown,
    HealingOnly
};
enum class HealingPatientState
{
    Unknown,
    NoneCovered,
    Covered
};
enum class OpeningDutyChoice
{
    KeepUnknown,
    KeepForTask,
    DeferIdleHealing
};

struct OpeningDutyRuntime
{
    std::string unique_operator_id;
    std::string prepared_operator_id;
    std::string properties_operator_id;
    int selected_skill = -1;
    int elite = -1;
    int displayed_skill_level = -1;
    std::uint64_t selection_sequence = 0;
    std::uint64_t training_sequence = 0;
    bool properties_known = false;
    bool medic_role = false;
    bool wander_subrole = false;
    std::array<std::string, 3> ranges;
    std::vector<std::string> tokens;
};

struct OpeningDutyContext
{
    bool authored_plan = false;
    bool uncertain_placement = false;
    bool explicit_nonhealing_task = false;
    bool verified_incoming_covered_ally_task = false;
    HealingPatientState patient = HealingPatientState::Unknown;
};

class BlackFlowOpeningDutyCatalog
{
public:
    bool load(
        const std::filesystem::path& facts,
        const std::filesystem::path& training,
        const std::filesystem::path& battle);
    bool configure(const json::value& facts, const json::value& training, const json::value& battle);
    void clear();
    [[nodiscard]] BaseOpeningDuty resolve(const OpeningDutyRuntime& runtime) const;
    [[nodiscard]] OpeningDutyChoice choose(const OpeningDutyRuntime& runtime, const OpeningDutyContext& context) const;

    [[nodiscard]] std::size_t rule_count() const { return m_rules.size(); }

private:
    struct Rule
    {
        int minimum_elite = 0;
        bool all_skill_levels = false;
        std::vector<int> levels;
        std::array<std::string, 3> ranges;
        std::vector<std::string> tokens;
    };

    std::map<std::pair<std::string, int>, Rule> m_rules;
};

BlackFlowOpeningDutyCatalog& opening_duty_catalog();
}
