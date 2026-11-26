#pragma once
#include "Config/AbstractConfig.h"

#include <optional>
#include <utility>
#include <vector>

#include "Common/AsstBattleDef.h"
#include "Config/Roguelike/BlackFlow/BlackFlowHunterOpening.h"

namespace asst
{
struct RoguelikeRosterDuty
{
    std::string id;
    std::vector<int> plans;
    std::vector<int> preferred_plans;

    bool operator==(const RoguelikeRosterDuty&) const = default;
};

class RoguelikeCopilotConfig final : public MAA_NS::SingletonHolder<RoguelikeCopilotConfig>, public AbstractConfig
{
public:
    virtual ~RoguelikeCopilotConfig() override = default;

    virtual bool load(const std::filesystem::path& path) override;

    std::optional<battle::roguelike::CombatData> get_stage_data(const std::string& stage_name) const;
    std::vector<std::pair<int, std::string>> get_stage_variant_keys(const std::string& base_stage) const;
    const std::vector<std::string>& reserved_core_groups(const std::string& stage_name) const;
    std::optional<int> maximum_deploy_cost(const std::string& stage_name, int plan_index) const;
    const std::vector<RoguelikeRosterDuty>& recruitment_duties(const std::string& stage_name) const;
    std::optional<blackflow::HunterOpening> get_hunter_opening(const std::string& stage_name) const;

protected:
    virtual bool parse(const json::value& json) override;
    std::unordered_map<std::string, battle::roguelike::CombatData> m_stage_data;
    std::unordered_map<std::string, blackflow::HunterOpening> m_hunter_openings;
    std::unordered_map<std::string, std::vector<std::string>> m_reserved_core_groups;
    std::unordered_map<std::string, std::unordered_map<int, int>> m_maximum_deploy_costs;
    std::unordered_map<std::string, std::vector<RoguelikeRosterDuty>> m_recruitment_duties;
};

inline static auto& RoguelikeCopilot = RoguelikeCopilotConfig::get_instance();
} // namespace asst
