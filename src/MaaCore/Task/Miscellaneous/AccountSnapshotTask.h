#pragma once

#include "Task/AbstractTask.h"

namespace asst
{
// 读取首页左下角账号卡片：昵称 + ID，通过 SubTaskExtraInfo 抛出
class AccountSnapshotTask final : public AbstractTask
{
public:
    using AbstractTask::AbstractTask;
    virtual ~AccountSnapshotTask() override = default;

protected:
    virtual bool _run() override;

    std::string m_nick;
    std::string m_uid;
};
}
