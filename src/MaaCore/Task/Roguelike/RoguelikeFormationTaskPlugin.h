#pragma once

#include "AbstractRoguelikeTaskPlugin.h"
#include "RoguelikePreparationTask.h"
#include "Vision/Roguelike/RoguelikeFormationImageAnalyzer.h"

namespace asst
{
// 集成战略模式快捷编队任务
class RoguelikeFormationTaskPlugin : public AbstractRoguelikeTaskPlugin
{
public:
    static constexpr size_t MaxNumOfOperPerPage = 8;

public:
    using AbstractRoguelikeTaskPlugin::AbstractRoguelikeTaskPlugin;
    virtual ~RoguelikeFormationTaskPlugin() override = default;

    virtual bool verify(AsstMsg msg, const json::value& details) const override;
    virtual bool load_params(const json::value& params) override;

protected:
    virtual bool _run() override;
    virtual bool on_run_fails() override;

    std::optional<blackflow::FormationRoleProposal>
        plan_primary_members(const std::vector<std::string>& all, const std::vector<std::string>& legacy) const;
    bool confirm_primary_members(blackflow::FormationRoleProposal& proposal, const std::vector<std::string>& actual)
        const;
    bool clear_and_reselect();
    bool analyze(bool read_training = true);
    bool select(RoguelikeFormationImageAnalyzer::FormationOper oper);
    void swipe_to_first_page();
    bool scan_oper(bool read_training = true);
    void observe_preparation(
        const cv::Mat& frame,
        const std::vector<RoguelikeFormationImageAnalyzer::FormationOper>& operators);

private:
    friend class RoguelikePreparedPrimaryTestAccess;
    friend class BlackFlowFormationCapacityTestAccess;
    static std::optional<std::size_t> read_formation_capacity(const cv::Mat& frame);
    static bool append_formation_candidates(
        std::vector<RoguelikeFormationImageAnalyzer::FormationOper>& selected,
        const std::vector<RoguelikeFormationImageAnalyzer::FormationOper>& remaining,
        std::size_t capacity);
    mutable bool m_capacity_observation_only = false;
    mutable bool m_use_observed_capacity = false;
    std::optional<std::size_t> m_verified_formation_capacity;
    bool m_scan_failed = false;
    bool m_selection_input_issued = false;
    int cur_page = 1;
    int max_page = 1;
    std::vector<RoguelikeFormationImageAnalyzer::FormationOper> oper_list;
    std::vector<std::string> m_last_detected_oper_names; // 上一页识别到的干员
};
}
