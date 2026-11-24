#include "RoguelikeTrainingConfig.h"

#include "Utils/Logger.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

void asst::RoguelikeTrainingConfig::clear() noexcept
{
    m_by_name.clear();
    m_operator_count = 0;
}

bool asst::RoguelikeTrainingConfig::parse(const json::value& value)
{
    clear();
    const auto entries = value.find<json::object>("operators");
    if (value.get("schema", 0) != 1 || !entries || entries->empty() || value.get("account_training_included", true)) {
        LogError << __FUNCTION__ << "Invalid operator training rules";
        return false;
    }
    std::unordered_map<std::string, std::vector<OperatorTrainingRule>> by_name;
    for (const auto& [id, entry] : *entries) {
        const auto name = entry.find<std::string>("name");
        const auto levels = entry.find<json::array>("max_levels");
        const auto skills = entry.find<json::array>("skills");
        if (id.empty() || !name || name->empty() || !levels || levels->empty() || !skills) {
            LogError << __FUNCTION__ << "Malformed operator rule" << id;
            return false;
        }
        OperatorTrainingRule rule;
        rule.id = id;
        rule.name = *name;
        rule.base_skill_max = entry.find<int>("base_skill_max").value_or(-1);
        for (const auto& maximum : *levels) {
            if (maximum.is_null()) {
                rule.maximum_levels.push_back(-1);
            }
            else if (
                maximum.is_number() && std::isfinite(maximum.as_double()) && maximum.as_double() > 0 &&
                maximum.as_double() <= std::numeric_limits<int>::max() &&
                std::floor(maximum.as_double()) == maximum.as_double()) {
                rule.maximum_levels.push_back(static_cast<int>(maximum.as_double()));
            }
            else {
                LogError << __FUNCTION__ << "Invalid level cap" << id;
                return false;
            }
        }
        for (const auto& skill : *skills) {
            if (skill.get("index", 0) != static_cast<int>(rule.skills.size()) + 1) {
                LogError << __FUNCTION__ << "Noncontiguous skill slot indices" << id;
                return false;
            }
            OperatorSkillRule slot;
            slot.known = skill.get("known", false);
            slot.unlock_elite = skill.find<int>("unlock_elite").value_or(-1);
            slot.unlock_level = skill.find<int>("unlock_level").value_or(-1);
            slot.mastery_steps = skill.find<int>("mastery_steps").value_or(-1);
            if (slot.known && (slot.unlock_elite < 0 || slot.unlock_level <= 0 ||
                               slot.unlock_elite >= static_cast<int>(rule.maximum_levels.size()))) {
                LogError << __FUNCTION__ << "Invalid skill unlock condition" << id;
                return false;
            }
            rule.skills.push_back(slot);
        }
        by_name[*name].push_back(std::move(rule));
    }
    m_by_name = std::move(by_name);
    m_operator_count = entries->size();
    return true;
}

std::string asst::RoguelikeTrainingConfig::unique_identity(const std::string& name) const
{
    const auto found = m_by_name.find(name);
    return found != m_by_name.end() && found->second.size() == 1 ? found->second.front().id : std::string {};
}

std::optional<int> asst::RoguelikeTrainingConfig::maximum_level(const std::string& name, int elite) const
{
    const auto found = m_by_name.find(name);
    if (found == m_by_name.end()) {
        return std::nullopt;
    }
    std::optional<int> maximum;
    for (const auto& rule : found->second) {
        int current = -1;
        if (elite >= 0) {
            if (elite >= static_cast<int>(rule.maximum_levels.size()) || rule.maximum_levels[elite] <= 0) {
                return std::nullopt;
            }
            current = rule.maximum_levels[elite];
        }
        else {
            if (std::ranges::any_of(rule.maximum_levels, [](int value) { return value <= 0; })) {
                return std::nullopt;
            }
            current = *std::ranges::max_element(rule.maximum_levels);
        }
        if (maximum && *maximum != current) {
            return std::nullopt;
        }
        maximum = current;
    }
    return maximum;
}

std::optional<int> asst::RoguelikeTrainingConfig::maximum_base_skill(const std::string& name) const
{
    const auto found = m_by_name.find(name);
    if (found == m_by_name.end()) {
        return std::nullopt;
    }
    const int maximum = found->second.front().base_skill_max;
    if (maximum <= 0 ||
        std::ranges::any_of(found->second, [&](const auto& rule) { return rule.base_skill_max != maximum; })) {
        return std::nullopt;
    }
    return maximum;
}

std::optional<int> asst::RoguelikeTrainingConfig::maximum_training(const std::string& name, int skill) const
{
    const auto found = m_by_name.find(name);
    if (found == m_by_name.end() || skill <= 0) {
        return std::nullopt;
    }
    std::optional<int> maximum;
    for (const auto& rule : found->second) {
        if (skill > static_cast<int>(rule.skills.size()) || rule.base_skill_max <= 0 || !rule.skills[skill - 1].known ||
            rule.skills[skill - 1].mastery_steps < 0) {
            return std::nullopt;
        }
        const int current = rule.base_skill_max + rule.skills[skill - 1].mastery_steps;
        if (maximum && *maximum != current) {
            return std::nullopt;
        }
        maximum = current;
    }
    return maximum;
}

asst::SkillAvailability
    asst::RoguelikeTrainingConfig::skill_availability(const std::string& name, int skill, int elite, int level) const
{
    const auto found = m_by_name.find(name);
    if (found == m_by_name.end() || skill <= 0) {
        return SkillAvailability::Unknown;
    }
    std::optional<SkillAvailability> consensus;
    for (const auto& rule : found->second) {
        SkillAvailability result = SkillAvailability::Unknown;
        if (skill > static_cast<int>(rule.skills.size())) {
            result = SkillAvailability::Absent;
        }
        else if (const auto& slot = rule.skills[skill - 1]; slot.known) {
            if (elite > slot.unlock_elite) {
                result = SkillAvailability::Available;
            }
            else if (
                (elite >= 0 && elite < slot.unlock_elite) ||
                (elite == slot.unlock_elite && level > 0 && level < slot.unlock_level)) {
                result = SkillAvailability::Locked;
            }
            else if (
                (elite >= slot.unlock_elite || slot.unlock_elite == 0) &&
                (level >= slot.unlock_level || slot.unlock_level == 1)) {
                result = SkillAvailability::Available;
            }
        }
        if (consensus && *consensus != result) {
            return SkillAvailability::Unknown;
        }
        consensus = result;
    }
    return consensus.value_or(SkillAvailability::Unknown);
}
