#pragma once

#include "BlackFlowTaskPluginBase.h"

namespace asst::blackflow
{
class BlackFlowLifecycleTaskPlugin final : public BlackFlowTaskPluginBase
{
public:
    using BlackFlowTaskPluginBase::BlackFlowTaskPluginBase;

    virtual bool load_params(const json::value& params) override;
    virtual bool verify(AsstMsg msg, const json::value& details) const override;
    virtual void reset_in_run_variables() override;

protected:
    virtual bool _run() override;

private:
    friend class BlackFlowActiveRecovery151TestAccess;

    enum class PendingWork
    {
        None,
        RecordCurrentFloor,
        ResolveTerminalAction,
        ResetAfterAbandon,
        StartRunStarted,
        PickLastReward,
        CheckStartRunReward,
        StartRunRewardPageMissing,
        OwnExploration,
        BeginActiveRecovery,
        RecoveryIdentity,
        RecoveryMapSeen,
        AlignRecoveryMap,
        StopActiveRecovery,
    };

    // 刷开局支援页上认出的一张卡；index 即 blackflow_start_rewards 的位
    struct StartRunCard
    {
        int index = 0;
        std::string name;
        Rect rect;
    };

    // 按策略的支援顺序点第一张认出的卡
    bool pick_last_reward();
    bool check_start_run_reward();
    std::vector<StartRunCard> recognize_start_run_cards(const cv::Mat& image) const;
    // 凹第一层实托邦的目标局按用户给的优先级选卡
    bool pick_start_run_card(const std::vector<StartRunCard>& cards);
    void check_start_run_utopia(int floor);
    void apply_start_loadout();
    // rewards 是在支援页认出的奖励名，随回调上报给界面
    void report_start_run(std::string_view status, const std::vector<std::string>& rewards = {});
    void stop_start_run(std::string_view status, const std::vector<std::string>& rewards = {});
    void complete_start_run(const std::vector<std::string>& rewards = {});

    bool owns_current_exploration() const;
    bool begin_active_recovery();
    bool accept_recovery_identity(std::string_view client, std::string_view uid);
    bool align_recovery_map(const BlackFlowPerceptionSnapshot& snapshot, std::string* error);
    void stop_active_recovery(std::string_view reason);
    void report_active_recovery(std::string_view status, std::string_view reason = {});
    void remove_recovery_override();

    // Process-local ownership, never restored from a cache or a requested difficulty.
    bool m_exploration_started = false;
    bool m_active_recovery = false;
    bool m_recovery_map_seen = false;
    std::string m_owned_run_id;
    std::string m_owned_uid;
    std::string m_owned_client;

    std::vector<std::string> m_last_reward_tasks;
    bool m_start_run = false;
    bool m_prepared_run_finished = false;
    std::string m_start_core_char;
    std::string m_target_squad;
    std::string m_preparation_squad;
    std::string m_start_roles;
    std::string m_preparation_core_char;
    std::string m_preparation_roles;
    // 凹第一层实托邦的目标局已经选过支援；之后的职业组、招募和地图都是预期内的
    bool m_target_rewards_picked = false;
    std::vector<std::string> m_target_rewards_seen;

    mutable PendingWork m_pending = PendingWork::None;
    mutable json::value m_pending_details;
    mutable std::string m_terminal_trigger;
    mutable std::string m_terminal_pre_task;
    bool m_stop_after_abandon = false;
};
} // namespace asst::blackflow
