#pragma once

#include "Task/Roguelike/AbstractRoguelikeTaskPlugin.h"

namespace asst::blackflow
{
// Identity verification is separate from the optional account-data refresh.
class BlackFlowAccountTaskPlugin final : public AbstractRoguelikeTaskPlugin
{
public:
    using AbstractRoguelikeTaskPlugin::AbstractRoguelikeTaskPlugin;
    bool verify(AsstMsg msg, const json::value& details) const override;
    bool load_params(const json::value& params) override;

protected:
    bool _run() override;

private:
    bool m_verified = false;
    bool m_refresh_requested = true;
    bool m_refresh_attempted = false;
    bool m_module_refresh_complete = false;
    std::string m_cache_file;
#ifdef ASST_DEBUG
    std::string m_resume_roster_file;
#endif
};
}
