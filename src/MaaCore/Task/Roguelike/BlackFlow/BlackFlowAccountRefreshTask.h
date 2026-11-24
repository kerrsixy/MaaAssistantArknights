#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "Config/Roguelike/BlackFlow/BlackFlowAccountScanCatalog.h"
#include "Config/Roguelike/BlackFlow/BlackFlowAccountTraining.h"
#include "MaaUtils/NoWarningCVMat.hpp"
#include "Task/AbstractTask.h"
#include "Vision/Oper/OperBoxImageAnalyzer.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowDetailStrengthAnalyzer.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowModulePanelAnalyzer.h"

namespace asst::blackflow
{
struct FreshAccountModulePanel
{
    std::string operator_name;
    ModulePanelObservation observation;
    cv::Mat source_image;
};

struct FreshAccountDetailStrength
{
    DetailStrengthObservation observation;
    cv::Mat source_image;
    std::optional<AccountTrainingEvidence> training;
};

struct AccountRefreshObservation
{
    std::string client;
    std::string uid;
    std::map<std::string, OperBoxInfo> strengths;
    std::vector<FreshAccountModulePanel> modules;
    std::map<std::string, FreshAccountDetailStrength> verified_strengths;
    std::set<std::string> unknown_targets;
    bool roster_complete = false;
    bool stopped_at_repeated_page = false;
    bool returned_same_uid = false;

    [[nodiscard]] bool required_modules_complete() const noexcept { return unknown_targets.empty(); }
};

// UI-only staged observation, not a cache writer. Account modules never prove battle equipment.
class BlackFlowAccountRefreshTask final : public AbstractTask
{
public:
    using AbstractTask::AbstractTask;
    void configure(
        std::string client,
        std::string uid,
        std::vector<AccountModuleScanTarget> targets,
        std::set<std::string> opening_training_names = {});

    [[nodiscard]] const AccountRefreshObservation& observation() const noexcept { return m_observation; }

protected:
    bool _run() override;

private:
    bool visit_page(const std::vector<OperBoxInfo>& page);
    bool visit_target(const OperBoxInfo& oper, const AccountModuleScanTarget* module);
    bool action(const std::string& task, const std::shared_ptr<AbstractTaskPlugin>& observer = nullptr);
    std::vector<AccountModuleScanTarget> m_targets;
    std::set<std::string> m_attempted;
    std::set<std::string> m_opening_training_names;
    AccountRefreshObservation m_observation;
};
}
