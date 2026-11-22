#pragma once
#include "Config/AbstractConfig.h"

#include <optional>

#include "Common/AsstBattleDef.h"
#include "Config/Roguelike/BlackFlow/BlackFlowHunterOpening.h"

namespace asst
{
class RoguelikeCopilotConfig final : public MAA_NS::SingletonHolder<RoguelikeCopilotConfig>, public AbstractConfig
{
public:
    virtual ~RoguelikeCopilotConfig() override = default;

    virtual bool load(const std::filesystem::path& path) override;

    std::optional<battle::roguelike::CombatData> get_stage_data(const std::string& stage_name) const;
    std::optional<blackflow::HunterOpening> get_hunter_opening(const std::string& stage_name) const;

protected:
    virtual bool parse(const json::value& json) override;
    std::unordered_map<std::string, battle::roguelike::CombatData> m_stage_data;
    std::unordered_map<std::string, blackflow::HunterOpening> m_hunter_openings;
};

inline static auto& RoguelikeCopilot = RoguelikeCopilotConfig::get_instance();
}
