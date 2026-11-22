#pragma once

#include <filesystem>
#include <optional>

#include "BlackFlowAccountRefreshTask.h"

namespace asst::blackflow
{
// Retains the original complete JSON instead of serializing a lossy profile projection.
class BlackFlowAccountSnapshotStore
{
public:
    static std::optional<json::value> merge(
        const json::value& previous,
        const AccountRefreshObservation& observed,
        const std::vector<AccountModuleScanTarget>& targets,
        const std::vector<std::filesystem::path>& images,
        const std::string& stamp,
        std::string* error);
    static bool save_atomic(
        const std::filesystem::path& path,
        const json::value& snapshot,
        const std::string& stamp,
        std::string* error);
};
}
