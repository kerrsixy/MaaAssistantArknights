#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Config/AbstractConfig.h"

namespace asst
{
enum class SkillAvailability
{
    Unknown,
    Available,
    Locked,
    Absent,
};

struct OperatorSkillRule
{
    bool known = false;
    int unlock_elite = -1;
    int unlock_level = -1;
    int mastery_steps = -1;
};

struct OperatorTrainingRule
{
    std::string id;
    std::string name;
    std::vector<int> maximum_levels;
    int base_skill_max = -1;
    std::vector<OperatorSkillRule> skills;
};

// Game rules only. This resource contains no account ownership or actual mastery.
class RoguelikeTrainingConfig final : public MAA_NS::SingletonHolder<RoguelikeTrainingConfig>, public AbstractConfig
{
public:
    std::string unique_identity(const std::string& name) const;

    bool contains_name(const std::string& name) const { return m_by_name.contains(name); }

    std::optional<int> maximum_level(const std::string& name, int elite) const;
    std::optional<int> maximum_base_skill(const std::string& name) const;
    std::optional<int> maximum_training(const std::string& name, int skill) const;
    SkillAvailability skill_availability(const std::string& name, int skill, int elite, int level) const;

    std::size_t operator_count() const noexcept { return m_operator_count; }

    void clear() noexcept;

protected:
    bool parse(const json::value& value) override;

private:
    std::unordered_map<std::string, std::vector<OperatorTrainingRule>> m_by_name;
    std::size_t m_operator_count = 0;
};

inline static auto& RoguelikeTraining = RoguelikeTrainingConfig::get_instance();
}
