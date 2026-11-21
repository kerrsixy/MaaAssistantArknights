#pragma once

#include "Task/AbstractTaskPlugin.h"

namespace asst::spa
{
class StrategyReportTaskPlugin final : public AbstractTaskPlugin
{
public:
    using AbstractTaskPlugin::AbstractTaskPlugin;

    void set_target(std::string band_id) { m_target = std::move(band_id); }

    bool verify(AsstMsg msg, const json::value& details) const override;

protected:
    bool _run() override;

private:
    std::string m_target;
};
}
