#include "SPAProgress.h"

#include <algorithm>
#include <fstream>
#include <limits>

asst::spa::ProgressStore::ProgressStore(std::string account, std::string season) :
    m_account(std::move(account)),
    m_season(std::move(season))
{
}

bool asst::spa::ProgressStore::reconcile(const StrategyProgress& observed)
{
    if (m_account.empty() || m_season.empty() || observed.band_id.empty() ||
        (observed.wins && *observed.wins < 0) ||
        (observed.badge_verified && (!observed.unlocked || !observed.wins || *observed.wins < 3))) {
        return false;
    }
    // A fresh game report is authoritative, including corrections to an earlier observation.
    m_strategies.insert_or_assign(observed.band_id, observed);
    return true;
}

bool asst::spa::ProgressStore::credit(const Settlement& settlement)
{
    auto it = m_strategies.find(settlement.band_id);
    if (m_account.empty() || m_season.empty() || !settlement.final_result_verified || !settlement.victory ||
        !settlement.strategy_credit_verified || settlement.run_id.empty() || it == m_strategies.end() ||
        !it->second.unlocked || !it->second.wins || *it->second.wins == std::numeric_limits<int>::max() ||
        m_credited_runs.contains(settlement.run_id)) {
        return false;
    }
    m_credited_runs.emplace(settlement.run_id, settlement.band_id);
    ++*it->second.wins;
    return true;
}

json::value asst::spa::ProgressStore::snapshot() const
{
    json::array strategies;
    for (const auto& [id, progress] : m_strategies) {
        json::object item { { "band_id", id },
                            { "wins_known", progress.wins.has_value() },
                            { "wins", progress.wins.value_or(0) },
                            { "unlocked", progress.unlocked },
                            { "badge_verified", progress.badge_verified },
                            { "solo_eligible_verified", progress.solo_eligible_verified } };
        strategies.emplace_back(std::move(item));
    }
    json::array credited;
    for (const auto& [run, band] : m_credited_runs) {
        credited.emplace_back(json::object { { "run_id", run }, { "band_id", band } });
    }
    return json::object { { "schema_version", 1 },
                          { "account", m_account },
                          { "season", m_season },
                          { "strategies", std::move(strategies) },
                          { "credited_runs", std::move(credited) } };
}

bool asst::spa::ProgressStore::restore(const json::value& value)
{
    if (!value.is_object() || m_account.empty() || m_season.empty()) {
        return false;
    }
    const auto version = value.find<int>("schema_version");
    const auto account = value.find<std::string>("account");
    const auto season = value.find<std::string>("season");
    const auto entries = value.find<json::array>("strategies");
    const auto runs = value.find<json::array>("credited_runs");
    if (!version || *version != 1 || !account || *account != m_account || !season || *season != m_season || !entries ||
        !runs) {
        return false;
    }
    ProgressStore pending(m_account, m_season);
    for (const auto& item : *entries) {
        if (!item.is_object()) {
            return false;
        }
        const auto id = item.find<std::string>("band_id");
        const auto known = item.find<bool>("wins_known");
        const auto wins = item.find<int>("wins");
        const auto unlocked = item.find<bool>("unlocked");
        const auto badge = item.find<bool>("badge_verified");
        const auto solo = item.find<bool>("solo_eligible_verified");
        if (!id || !known || !wins || !unlocked || !badge || !solo || pending.m_strategies.contains(*id) || *wins < 0 ||
            (!*known && *wins != 0)) {
            return false;
        }
        StrategyProgress progress { *id, *known ? std::optional(*wins) : std::nullopt, *unlocked, *badge, *solo };
        if (!pending.reconcile(progress)) {
            return false;
        }
    }
    for (const auto& item : *runs) {
        if (!item.is_object()) {
            return false;
        }
        const auto run = item.find<std::string>("run_id");
        const auto band = item.find<std::string>("band_id");
        if (!run || run->empty() || !band || !pending.m_strategies.contains(*band) ||
            !pending.m_credited_runs.emplace(*run, *band).second) {
            return false;
        }
    }
    m_strategies = std::move(pending.m_strategies);
    m_credited_runs = std::move(pending.m_credited_runs);
    return true;
}

bool asst::spa::ProgressStore::save(const std::filesystem::path& path) const
{
    if (m_account.empty() || m_season.empty() || path.empty()) {
        return false;
    }
    std::error_code error;
    const bool existing = std::filesystem::exists(path, error);
    if (error) {
        return false;
    }
    if (existing) {
        const auto value = json::open(path);
        ProgressStore previous(m_account, m_season);
        if (!value || !previous.restore(*value)) {
            return false;
        }
    }
    auto temporary = path;
    temporary += ".tmp";
    auto backup = path;
    backup += ".bak";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return false;
        }
        stream << snapshot().to_string();
        stream.flush();
        if (!stream) {
            return false;
        }
        stream.close();
        if (!stream) {
            return false;
        }
    }
    if (std::filesystem::exists(path, error)) {
        std::filesystem::remove(backup, error);
        if (error) {
            return false;
        }
        std::filesystem::rename(path, backup, error);
        if (error) {
            return false;
        }
    }
    else if (error) {
        return false;
    }
    std::filesystem::rename(temporary, path, error);
    return !error;
}

bool asst::spa::ProgressStore::load(const std::filesystem::path& path)
{
    if (auto value = json::open(path)) {
        // An incompatible existing file must not silently fall back to another account's backup.
        return restore(*value);
    }
    auto temporary = path;
    temporary += ".tmp";
    if (auto value = json::open(temporary)) {
        return restore(*value);
    }
    auto backup = path;
    backup += ".bak";
    if (auto value = json::open(backup)) {
        return restore(*value);
    }
    return false;
}

std::vector<asst::spa::StrategyProgress> asst::spa::ProgressStore::strategies() const
{
    std::vector<StrategyProgress> result;
    for (const auto& [id, progress] : m_strategies) {
        result.emplace_back(progress);
    }
    return result;
}
