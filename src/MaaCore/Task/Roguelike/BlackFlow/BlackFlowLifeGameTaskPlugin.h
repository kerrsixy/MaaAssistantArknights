#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

#include "BlackFlowLifeGamePlanner.h"
#include "Task/Roguelike/AbstractRoguelikeTaskPlugin.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowLifeGameAnalyzer.h"

namespace asst::blackflow
{
class BlackFlowLifeGameTaskPlugin final : public AbstractRoguelikeTaskPlugin
{
public:
    using AbstractRoguelikeTaskPlugin::AbstractRoguelikeTaskPlugin;
    bool load_params(const json::value& params) override;
    bool verify(AsstMsg msg, const json::value& details) const override;

protected:
    bool _run() override;

private:
    struct Location
    {
        int viewport = 0;
        Rect rect;
    };

    bool resource_action(std::string_view task);
    bool select(const Rect& rect);
    [[nodiscard]] std::optional<LifeGameScreenObservation> observe_stable(bool details = true);
    bool seek_first();
    bool scan_viewport(int viewport);
    bool scan_tree();
    bool go_to(const Location& location);
    bool record_selected(const LifeGameScreenObservation& observation, const Location& location);
    bool finish(std::string status, std::string reason, bool unknown);
    void report(std::string status, std::string reason, bool unknown);

    bool m_enabled = true;
    bool m_only = false;
    BlackFlowLifeGamePlanner m_planner;
    LifeGameEvidence m_evidence;
    LifeGameScreenObservation m_last_observation;
    std::string m_last_observation_stamp;
    std::filesystem::path m_last_evidence_file;
    std::unordered_map<std::string, Location> m_locations;
    int m_verified_upgrades = 0;
    int m_terminal_upgrades = 0;
    bool m_debit_verified = false;
    bool m_debit_unobservable = false;
    int m_ambiguous_selections = 0;
};
}
