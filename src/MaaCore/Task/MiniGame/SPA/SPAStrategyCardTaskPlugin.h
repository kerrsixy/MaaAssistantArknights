#pragma once

#include "SPAStrategyEligibility.h"
#include "Task/AbstractTaskPlugin.h"
#include <functional>

namespace asst::spa
{
class StrategyCardTaskPlugin final : public AbstractTaskPlugin
{
public:
    using AbstractTaskPlugin::AbstractTaskPlugin;
    bool configure(int strategy_index, bool prefer_unclaimed = false, std::string strategy_id = {});
    bool verify(AsstMsg msg, const json::value& details) const override;

    void set_recovery_task(std::string task) { m_recovery_task = std::move(task); }

    void set_context_provider(std::function<OpeningStrategyContext()> provider)
    {
        m_context_provider = std::move(provider);
    }

protected:
    bool _run() override;
    bool on_run_fails() override;

private:
    std::string m_recovery_task;
    std::string m_requested_strategy;
    std::function<OpeningStrategyContext()> m_context_provider;
    int m_strategy_index = 0;
    int m_selected_strategy_index = -1;
    bool m_auto_selected = false;
    bool m_prefer_unclaimed = true;
};
}
