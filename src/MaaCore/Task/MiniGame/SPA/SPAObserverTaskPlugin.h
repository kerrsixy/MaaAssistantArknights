#pragma once

#include "Task/AbstractTaskPlugin.h"

namespace cv
{
class Mat;
}

namespace asst::spa
{
class ObserverTaskPlugin final : public AbstractTaskPlugin
{
public:
    using AbstractTaskPlugin::AbstractTaskPlugin;

    const json::object& last_observation() const { return m_last_observation; }

    bool observe_frame(const cv::Mat& image);

    void expect_unchanged_preparation(int round) { m_expected_preparation_round = round; }

    bool verify(AsstMsg msg, const json::value& details) const override;

protected:
    bool _run() override;

private:
    bool observe_frame(const cv::Mat& image, int expected_preparation_round);
    int m_expected_preparation_round = -1;
    json::object m_last_observation;
};
}
