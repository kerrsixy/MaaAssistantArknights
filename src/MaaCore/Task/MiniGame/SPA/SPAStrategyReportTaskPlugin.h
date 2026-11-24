#pragma once

#include <functional>

#include "MaaUtils/NoWarningCV.hpp"
#include "Task/AbstractTaskPlugin.h"

namespace asst::spa
{
class StrategyReportTaskPlugin final : public AbstractTaskPlugin
{
public:
    using AbstractTaskPlugin::AbstractTaskPlugin;

    void set_target(std::string band_id) { m_target = std::move(band_id); }

    void set_portrait_sink(std::function<void(const std::string&, cv::Mat)> sink) { m_portrait_sink = std::move(sink); }

    bool verify(AsstMsg msg, const json::value& details) const override;

protected:
    bool _run() override;

private:
    std::string m_target;
    std::function<void(const std::string&, cv::Mat)> m_portrait_sink;
};
}
