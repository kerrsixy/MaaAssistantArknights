#pragma once

#include "Task/AbstractTaskPlugin.h"

namespace asst::spa
{
class SetupTaskPlugin final : public AbstractTaskPlugin
{
public:
    using AbstractTaskPlugin::AbstractTaskPlugin;

    bool configure(std::string core_pact, std::vector<std::string> support_pacts, int strategy_index = 0);
    bool verify(AsstMsg msg, const json::value& details) const override;

    void set_recovery_task(std::string task) { m_recovery_task = std::move(task); }

protected:
    bool _run() override;
    bool on_run_fails() override;

private:
    std::string m_recovery_task;

    struct PactText
    {
        std::string name;
        Rect rect;
        bool core = false;
    };

    bool selected(const cv::Mat& image, const PactText& pact) const;
    std::optional<PactText> find_pact(const std::vector<PactText>& pacts, const std::string& name) const;

    std::string m_core_pact;
    // `auto` can only choose a provisional faction on the 1/3 page; the
    // formation roster is not visible until 3/3 and must verify activation.
    bool m_auto_core_probe = false;
    std::vector<std::string> m_support_pacts;
    // Final selected-group availability, collected only after core selection.
    // Gray cores before selection are alternatives, not permanent bans.
    std::vector<std::string> m_disabled_pacts;
    int m_strategy_index = 0;
};
}
