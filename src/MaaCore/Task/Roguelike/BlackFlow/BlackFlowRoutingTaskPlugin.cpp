#include "BlackFlowRoutingTaskPlugin.h"

#include "BlackFlowRoutingLoop.h"

#include "Config/TaskData.h"
#include "Utils/Logger.hpp"

namespace asst::blackflow
{
namespace
{
// 首次完整确认耗尽时关闭预览面板并重新选择；再次耗尽时重开当前局。
constexpr int MoveConfirmationDismissRetryLimit = 1;
}

bool BlackFlowRoutingTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    if (details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }
    const std::string task = details.get("details", "task", "");
    if (msg == AsstMsg::SubTaskCompleted && task == "BlackFlow@Roguelike@MapPrepare-Ready") {
        m_pending = PendingWork::MapPrepared;
        return true;
    }
    if (msg != AsstMsg::SubTaskStart) {
        return false;
    }
    // RoutingAction is a shared placeholder.  After a move is committed the
    // node plugin also listens to it to bind the pending page to the selected
    // node.  If routing observes it as well, it rebuilds the map while the
    // node page is still open and the page is lost.  Routing-Enter/Routing
    // are the only entry points that need a routing cycle; leave the
    // placeholder to BlackFlowNodeTaskPlugin when a page is dispatchable.
    if (task == "BlackFlow@Roguelike@Routing-Enter" || task == "BlackFlow@Roguelike@Routing") {
        m_pending = PendingWork::ObserveAndPlan;
        return true;
    }
    if (task == "BlackFlow@Roguelike@RoutingReplan") {
        m_pending = PendingWork::ReplanCurrentMap;
        return true;
    }
    if (task == "BlackFlow@Roguelike@RoutingResume") {
        m_pending = PendingWork::ResumePendingMove;
        return true;
    }
    return false;
}

void BlackFlowRoutingTaskPlugin::reset_in_run_variables()
{
    m_pending = PendingWork::None;
    m_map_recovery_stage = MapRecoveryStage::None;
    m_replan_current_map = false;
    m_move_confirmation_dismiss_retries = 0;
}

bool BlackFlowRoutingTaskPlugin::_run()
{
    LogTraceFunction;
    const PendingWork work = m_pending;
    m_pending = PendingWork::None;
    if (work == PendingWork::None) {
        return true;
    }
    if (work == PendingWork::MapPrepared) {
        // 第五层每次准备地图都向左滑，避免最右列被遮挡
        const bool swipe = m_session != nullptr && !m_session->terminated() && m_session->current_floor() == 5;
        Task.set_task_base(
            "BlackFlow@Roguelike@MapPrepare-ReadyAction",
            swipe ? "BlackFlow@Roguelike@MapPrepare-Floor5Swipe" : "BlackFlow@Roguelike@Routing-Enter");
        return true;
    }
    LogInfo << __FUNCTION__ << "BlackFlow routing entry" << "work" << static_cast<int>(work) << "session"
            << (m_session != nullptr) << "port" << (m_port != nullptr);
    Task.set_task_base("BlackFlow@Roguelike@RoutingAction", "BlackFlow@Roguelike@RecoveryFailed");

    if (m_session == nullptr || m_port == nullptr) {
        if (m_session != nullptr) {
            m_session->fail("perception_port_missing", "BlackFlow perception and task port is not attached");
        }
        Task.set_task_base("BlackFlow@Roguelike@RoutingAction", "BlackFlow@Roguelike@StrategyTerminated-Enter");
        report_outputs();
        return true;
    }

    m_session->set_observed_difficulty(m_config->get_observed_difficulty());
    const RoutingCycleOutcome cycle =
        work == PendingWork::ResumePendingMove ? execute_pending_routing_cycle(*m_session, *m_port)
        : work == PendingWork::ReplanCurrentMap || m_replan_current_map ? execute_replanning_cycle(*m_session, *m_port)
                                                                        : execute_routing_cycle(*m_session, *m_port);
    LogInfo << __FUNCTION__ << "BlackFlow routing cycle outcome" << "status" << static_cast<int>(cycle.status)
            << "failure" << cycle.failure_code << "error" << cycle.error;
    if (cycle.status == RoutingCycleStatus::NeedsPageRecovery) {
        // 返回地图时按钮状态和实际镜头比例可能不同步。先在地图内重置镜头，
        // 仍失败才退出重进一次；每一阶段都有界，不能在相同失败帧上无限循环。
        if (m_map_recovery_stage == MapRecoveryStage::None) {
            m_map_recovery_stage = MapRecoveryStage::ViewportReset;
            Task.set_task_base("BlackFlow@Roguelike@RoutingAction", "BlackFlow@Roguelike@MapViewportReset-Enter");
        }
        else if (m_map_recovery_stage == MapRecoveryStage::ViewportReset) {
            m_map_recovery_stage = MapRecoveryStage::PageRecovery;
            Task.set_task_base("BlackFlow@Roguelike@RoutingAction", "BlackFlow@Roguelike@RecoverMap-Enter");
        }
        else {
            m_session->fail("map_rebuild_failed", cycle.error, FailureDisposition::RestartRun);
            Task.set_task_base("BlackFlow@Roguelike@RoutingAction", "BlackFlow@Roguelike@StrategyTerminated-Enter");
        }
        report_outputs();
        return true;
    }
    m_map_recovery_stage = MapRecoveryStage::None;
    if (cycle.status == RoutingCycleStatus::MovementInventoryObservationRequired) {
        Task.set_task_base("BlackFlow@Roguelike@RoutingAction", "BlackFlow@Roguelike@MovementInventoryCheck");
        report_outputs();
        return true;
    }
    if (cycle.status == RoutingCycleStatus::MovementSelectionRequired) {
        Task.set_task_base("BlackFlow@Roguelike@RoutingAction", "BlackFlow@Roguelike@SelectMovement-Enter");
        report_outputs();
        return true;
    }
    if (cycle.status == RoutingCycleStatus::ReplanRequired) {
        Task.set_task_base("BlackFlow@Roguelike@RoutingAction", "BlackFlow@Roguelike@MapPrepare");
        report_outputs();
        return true;
    }
    if (cycle.status == RoutingCycleStatus::PreviewNeedsReplan) {
        m_replan_current_map = true;
        Task.set_task_base("BlackFlow@Roguelike@RoutingAction", "BlackFlow@Roguelike@CancelNodeSelectionForReplan");
        report_outputs();
        return true;
    }
    if (cycle.status == RoutingCycleStatus::PreviewNeedsDismiss) {
        if (!cycle.failure_code.empty() || !cycle.error.empty()) {
            Log.info("BlackFlow move preview dismissed", cycle.failure_code, cycle.error);
        }
        Task.set_task_base("BlackFlow@Roguelike@RoutingAction", "BlackFlow@Roguelike@CancelNodeSelection");
        report_outputs();
        return true;
    }
    if (cycle.status == RoutingCycleStatus::InventoryCleaned) {
        m_replan_current_map = false;
        m_move_confirmation_dismiss_retries = 0;
        LogInfo << "BlackFlow inventory | event=routing_resumed_after_cleanup";
        Task.set_task_base("BlackFlow@Roguelike@RoutingAction", "BlackFlow@Roguelike@MapPrepare");
        report_outputs();
        return true;
    }
    if (cycle.status == RoutingCycleStatus::ConfirmationNeedsDismiss) {
        if (m_move_confirmation_dismiss_retries < MoveConfirmationDismissRetryLimit) {
            ++m_move_confirmation_dismiss_retries;
            LogInfo << __FUNCTION__ << "BlackFlow move confirmation exhausted; dismissing preview before retry"
                    << "attempt" << m_move_confirmation_dismiss_retries << "of" << MoveConfirmationDismissRetryLimit
                    << cycle.error;
            Task.set_task_base("BlackFlow@Roguelike@RoutingAction", "BlackFlow@Roguelike@CancelNodeSelection");
        }
        else {
            LogError << __FUNCTION__ << "BlackFlow move confirmation retries exhausted; restarting the run"
                     << cycle.failure_code << cycle.error;
            m_session->fail(cycle.failure_code, cycle.error, FailureDisposition::RestartRun);
            Task.set_task_base("BlackFlow@Roguelike@RoutingAction", "BlackFlow@Roguelike@StrategyTerminated-Enter");
        }
        report_outputs();
        return true;
    }
    if (cycle.status == RoutingCycleStatus::MoveCommittedToMap) {
        m_replan_current_map = false;
        m_move_confirmation_dismiss_retries = 0;
        Task.set_task_base("BlackFlow@Roguelike@RoutingAction", "BlackFlow@Roguelike@MapPrepare");
        report_outputs();
        return true;
    }
    if (cycle.status == RoutingCycleStatus::MoveCommitted) {
        m_replan_current_map = false;
        m_move_confirmation_dismiss_retries = 0;
        Task.set_task_base("BlackFlow@Roguelike@RoutingAction", "BlackFlow@Roguelike@NodeDispatch");
        report_outputs();
        return true;
    }
    if (cycle.status == RoutingCycleStatus::SessionTerminated) {
        m_replan_current_map = false;
        Task.set_task_base("BlackFlow@Roguelike@RoutingAction", "BlackFlow@Roguelike@StrategyTerminated-Enter");
        report_outputs();
        return true;
    }

    LogError << __FUNCTION__ << "BlackFlow routing cycle failed; restarting the run" << cycle.failure_code
             << cycle.error;
    m_session->fail(cycle.failure_code, cycle.error, FailureDisposition::RestartRun);
    Task.set_task_base("BlackFlow@Roguelike@RoutingAction", "BlackFlow@Roguelike@StrategyTerminated-Enter");
    report_outputs();
    return true;
}
} // namespace asst::blackflow

