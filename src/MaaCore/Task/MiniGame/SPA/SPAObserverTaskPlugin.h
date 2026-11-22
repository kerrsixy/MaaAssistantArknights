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
    bool observe_strategy_report_card(const cv::Mat& image, const Rect& selected_badge_roi);

    void enable_joint_countdown() { m_joint_countdown_enabled = true; }

    void expect_unchanged_preparation(int round) { m_expected_preparation_round = round; }

    bool verify(AsstMsg msg, const json::value& details) const override;

protected:
    bool _run() override;

private:
    bool observe_frame(const cv::Mat& image, int expected_preparation_round, const Rect* selected_badge_roi = nullptr);
    bool m_joint_countdown_enabled = false;
    int m_expected_preparation_round = -1;
    json::object m_last_observation;
};
}
