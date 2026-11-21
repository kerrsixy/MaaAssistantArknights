#pragma once

#include "Task/Roguelike/AbstractRoguelikeTaskPlugin.h"

namespace asst::blackflow
{
[[nodiscard]] bool stashed_recruit_entry_enabled(const cv::Mat& image, const Rect& roi);
// Called only after the configured selected operator name was positively read.
[[nodiscard]] bool stashed_recruit_portrait_page(const cv::Mat& image);

class BlackFlowStashedRecruitTaskPlugin final : public AbstractRoguelikeTaskPlugin
{
public:
    using AbstractRoguelikeTaskPlugin::AbstractRoguelikeTaskPlugin;
    bool verify(AsstMsg msg, const json::value& details) const override;

protected:
    bool _run() override;

private:
    bool decide();
    bool guard_entry();
    bool observe_portrait(bool advance);
    enum class Work
    {
        Decide,
        Enter,
        PortraitObserve,
        PortraitAdvance
    };
    mutable Work m_work = Work::Decide;
};
}
