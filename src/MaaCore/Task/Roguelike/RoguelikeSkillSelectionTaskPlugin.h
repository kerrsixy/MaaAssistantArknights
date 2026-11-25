#pragma once

#include <unordered_map>
#include <vector>

#include "AbstractRoguelikeTaskPlugin.h"
#include "RoguelikePreparationTask.h"

namespace asst
{
class RoguelikeSkillSelectionTaskPlugin final : public AbstractRoguelikeTaskPlugin
{
public:
    using AbstractRoguelikeTaskPlugin::AbstractRoguelikeTaskPlugin;
    virtual ~RoguelikeSkillSelectionTaskPlugin() override = default;

    virtual bool verify(AsstMsg msg, const json::value& details) const override;

protected:
    virtual bool _run() override;

private:
    friend class RoguelikePreparedPrimaryTestAccess;
    std::optional<blackflow::FormationRoleProposal> take_prepared_formation_role();
    void begin_prepared_primary_observation();
    void publish_prepared_primary_observation(
        const std::vector<std::string>& names,
        const std::optional<blackflow::FormationRoleProposal>& proposal = std::nullopt);
    bool run_blackflow_preparation();
    bool interrupt_before_battle(std::string_view reason);
};
}
