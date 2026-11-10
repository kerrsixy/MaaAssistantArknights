#pragma once

#include "Task/AbstractTask.h"

#include <string>
#include <utility>

namespace asst
{
// 只做一件事：把一句面向用户的提示写进日志，然后立刻结束。
// 用来在任务链的关键步骤之间给界面/日志一个明确的心智锚点，
// 避免用户误以为「没开肉鸽模式」「脚本卡住了」。
class LogHintTask final : public AbstractTask
{
public:
    LogHintTask(const AsstCallback& callback, Assistant* inst, std::string message) :
        AbstractTask(callback, inst, TaskType),
        m_message(std::move(message))
    {
    }

    virtual ~LogHintTask() override = default;

    inline static constexpr std::string_view TaskType = "LogHint";

protected:
    virtual bool _run() override;

private:
    std::string m_message;
};
}
