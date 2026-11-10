#pragma once
#include "Task/InterfaceTask.h"

namespace asst
{
class OperStrengthTask final : public InterfaceTask
{
public:
    inline static constexpr std::string_view TaskType = "OperStrength";

    OperStrengthTask(const AsstCallback& callback, Assistant* inst);
    virtual ~OperStrengthTask() override = default;
};
}
