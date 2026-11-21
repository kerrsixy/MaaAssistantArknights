#pragma once

#include "Task/AbstractTaskPlugin.h"

namespace asst::spa
{
class FormationSelectTaskPlugin final : public AbstractTaskPlugin
{
public:
    using AbstractTaskPlugin::AbstractTaskPlugin;
    bool configure(std::vector<std::string> operators, std::string core_pact = {});
    bool verify(AsstMsg msg, const json::value& details) const override;

    void set_recovery_task(std::string task) { m_recovery_task = std::move(task); }

protected:
    bool _run() override;
    bool on_run_fails() override;

private:
    std::string m_recovery_task;
    std::vector<std::string> m_operators;
    std::string m_core_pact;
    std::vector<std::string> m_requested_operators;
    std::string m_requested_core;
};
}
