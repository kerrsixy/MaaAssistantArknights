#pragma once

#include "Task/BattleHelper.h"
#include "Task/Roguelike/AbstractRoguelikeTaskPlugin.h"

namespace asst::blackflow
{
class BlackFlowSession;

class BlackFlowDuelTaskPlugin final : public AbstractRoguelikeTaskPlugin, private BattleHelper
{
public:
    BlackFlowDuelTaskPlugin(
        const AsstCallback& callback,
        Assistant* inst,
        std::string_view task_chain,
        const std::shared_ptr<RoguelikeConfig>& config,
        const std::shared_ptr<RoguelikeControlTaskPlugin>& control,
        std::shared_ptr<BlackFlowSession> session);

    bool verify(AsstMsg msg, const json::value& details) const override;
    bool load_params(const json::value& params) override;
    void reset_in_run_variables() override;

protected:
    bool _run() override;

    AbstractTask& this_task() override { return *this; }

    bool verify_deployment_enabled() const override { return true; }

private:
    enum class PendingWork
    {
        None,
        Prepare,
        Battle
    };
    bool run_blackflow_duel_opponent_selection();
    bool run_blackflow_duel_battle();
    static bool save_img(const cv::Mat& image, std::string_view description);

    std::shared_ptr<BlackFlowSession> m_blackflow_session;
    mutable PendingWork m_pending = PendingWork::None;
    mutable bool m_duel_refresh_attempted = false;
    int m_requested_duel_group = 0;
    std::string m_duel_stage;
};
}
