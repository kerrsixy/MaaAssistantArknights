#include "LogHintTask.h"

#include "Utils/Logger.hpp"

bool asst::LogHintTask::_run()
{
    LogInfo << m_message;
    return true;
}
