#pragma once

#include "Task/AbstractTaskPlugin.h"

namespace asst::spa
{
class DifficultyTaskPlugin final : public AbstractTaskPlugin
{
public:
    using AbstractTaskPlugin::AbstractTaskPlugin;
    bool set_target(int target, bool start = false, std::string mode = "independent");
    bool verify(AsstMsg msg, const json::value& details) const override;

    void set_recovery_task(std::string task) { m_recovery_task = std::move(task); }

protected:
    bool _run() override;
    bool on_run_fails() override;

private:
    std::string m_recovery_task;
    int m_target = -1;
    bool m_start = false;
    std::string m_mode = "independent";
};
}
