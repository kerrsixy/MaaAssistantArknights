#pragma once

#include "SPAPlanner.h"

#include <filesystem>
#include <map>
#include <meojson/json.hpp>

namespace asst::spa
{
class ProgressStore
{
public:
    ProgressStore(std::string account, std::string season);

    bool reconcile(const StrategyProgress& observed);
    bool credit(const Settlement& settlement);
    json::value snapshot() const;
    bool restore(const json::value& value);
    bool save(const std::filesystem::path& path) const;
    bool load(const std::filesystem::path& path);
    std::vector<StrategyProgress> strategies() const;

private:
    std::string m_account;
    std::string m_season;
    std::map<std::string, StrategyProgress> m_strategies;
    std::map<std::string, std::string> m_credited_runs;
};
}
