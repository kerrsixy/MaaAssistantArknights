#include "OperStrengthTask.h"

#include "Task/Miscellaneous/AccountSnapshotTask.h"
#include "Task/Miscellaneous/LogHintTask.h"
#include "Task/Miscellaneous/OperBoxRecognitionTask.h"
#include "Task/Miscellaneous/ScreenshotTaskPlugin.h"
#include "Task/ProcessTask.h"

asst::OperStrengthTask::OperStrengthTask(const AsstCallback& callback, Assistant* inst) :
    InterfaceTask(callback, inst, TaskType)
{
    auto hint = [&](std::string message) {
        m_subtasks.emplace_back(std::make_shared<LogHintTask>(m_callback, m_inst, std::move(message)));
    };

    // 0) 开场提示：说明这一步到底在干嘛
    hint(
        "识别账号与干员练度：打肉鸽之前先匹配一下干员练度，识别结果会写进账号快照，"
        "供开局抓位闸门（练度不足顺延）与模组判断使用；这一步只读界面，不会改动任何游戏状态。");

    // 1) 账号绑定：首页左下角卡片 -> OperStrengthAccount 回调
    hint("第 1/3 步：正在识别账号（首页左下角的名片卡片），用于把练度快照绑定到当前账号。");
    auto account_task = std::make_shared<AccountSnapshotTask>(m_callback, m_inst, TaskType);
    account_task->set_retry_times(1);
    m_subtasks.emplace_back(account_task);

    // 2) 进入干员列表
    hint("第 2/3 步：正在进入干员列表，准备逐页读取每名干员的精英化、等级与潜能。");
    auto enter_task = std::make_shared<ProcessTask>(m_callback, m_inst, TaskType);
    // Start from the public OperBox entry chain.  OperBox-Entry is only the
    // inner theme-card probe; using it as the root silently continued on the
    // home screen when the card was not visible, producing an empty scan.
    enter_task
        ->set_tasks(
            { "OperBoxHomeDirect",
              "OperBoxTutorialClose",
              "OperBoxBegin",
              "OperBoxRoleTabSelect",
              "OperBoxSortTabSelect",
              "OperBoxRoleTab" })
        .set_ignore_error(false)
        .set_retry_times(3);
    enter_task->register_plugin<ScreenshotTaskPlugin>();
    m_subtasks.emplace_back(enter_task);

    // 3) 复用 OperBox 的网格识别：逐页读 名字/精英化/等级/潜能，抛 OperBoxInfo
    hint("第 3/3 步：正在识别干员练度（按干员列表逐页读卡：名字 / 精英化 / 等级 / 潜能）。");
    auto recognition_task = std::make_shared<OperBoxRecognitionTask>(m_callback, m_inst, TaskType);
    recognition_task->set_retry_times(0);
    m_subtasks.emplace_back(recognition_task);

    // 4) 收尾提示：说明模组这块的状态，避免用户以为「模组已经自动改了」
    hint(
        "练度识别结束。模组检查名单（只看特限与机械师）：号角 / 提丰 / 水月 / 灵知 / 机械师；"
        "模组只有带特限模组的干员才在肉鸽强势，本次仅做名单提示，模组开启状态需在干员面板单独扫描后再写入快照。");
}
