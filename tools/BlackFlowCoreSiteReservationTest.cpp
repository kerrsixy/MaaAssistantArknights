#include <filesystem>
#include <fstream>
#include <iostream>

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"

class CoreSiteProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    explicit CoreSiteProbe(const std::shared_ptr<asst::RoguelikeConfig>& config) :
        RoguelikeBattleTaskPlugin({}, nullptr, "CoreSiteProbe", config, nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::starting_core_reserves_site;

    void stage(const asst::battle::roguelike::CombatData& data) { m_deploy_plan = data.deploy_plan; }

    void elite(const std::string& name, int value) { m_oper_elite[name] = value; }

    void count(int plan, int value) { m_plan_deployment_counts[plan] = value; }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    using namespace asst::battle;
    const std::filesystem::path repo(argv[1]), plans(argv[2]), scratch(argv[3]);
    if (!BattleData.load(repo / utils::path("resource/battle_data.json")) ||
        !RoguelikeRecruit.load(repo / utils::path("resource/roguelike/BlackFlow/recruitment.json")) ||
        !RoguelikeCopilot.load(plans)) {
        return 3;
    }
    const auto data = RoguelikeCopilot.get_stage_data("狂热敬虔");
    auto config = std::make_shared<RoguelikeConfig>();
    if (!data || !config->verify_and_load_params(json::object { { "theme", "BlackFlow" }, { "mode", 0 } })) {
        return 4;
    }
    config->set_start_opers({ { "维什戴尔", false } });
    CoreSiteProbe probe(config);
    probe.stage(*data);
    probe.elite("维什戴尔", 2);
    probe.elite("望", 2);
    probe.elite("卡达", 1);
    int checks = 0, failures = 0;
    const auto expect = [&](bool value, const char* label) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    DeploymentOper core;
    core.name = "维什戴尔";
    core.role = Role::Sniper;
    core.cost = 36;
    core.cooling = true;
    const auto& fallback = data->deploy_plan.at("术师").back();
    const auto& primary = data->deploy_plan.at("益达").front();
    const auto& typhon = data->deploy_plan.at("提丰").front();
    const auto reserve = [&](const auto& candidate) {
        return probe.starting_core_reserves_site(core, candidate, 11, data->stage_name);
    };
    expect(reserve(fallback), "owned cooling core keeps its explicitly reserved site");
    core.cooling = false;
    core.available = true;
    expect(reserve(fallback), "returned core is not blocked by weaker same-site fallback");
    expect(!reserve(typhon), "independent upper damage duty remains deployable");
    auto other = fallback;
    other.location = Point(8, 3);
    expect(!reserve(other), "independent qualified medical tile is not reserved");
    const auto& alternative = data->deploy_plan.at("术师").front();
    expect(reserve(alternative), "actual mutually exclusive alternative cannot lock out the core");
    core.name = "卡达";
    expect(!reserve(fallback), "a non-core card cannot establish core ownership");
    core.name = "维什戴尔";
    core.role = Role::Unknown;
    expect(!reserve(fallback), "unknown role cannot establish reservation");
    core.role = Role::Drone;
    expect(!reserve(fallback), "summons cannot reserve a starting operator site");
    core.role = Role::Sniper;
    probe.elite(core.name, 0);
    expect(!reserve(fallback), "ineligible elite does not reserve a future core location");
    probe.elite(core.name, 2);
    probe.count(primary.plan_index, primary.max_deployments);
    expect(!reserve(fallback), "exhausted primary plan releases its site");
    probe.count(primary.plan_index, 0);
    auto expired = *data;
    for (auto& [group, commands] : expired.deploy_plan) {
        for (auto& command : commands) {
            if (command.plan_index == primary.plan_index) {
                command.kill_upper_bound = 10;
            }
        }
    }
    probe.stage(expired);
    expect(!reserve(fallback), "expired primary duty does not reserve the site");
    probe.stage(*data);
    config->set_start_opers({ { "望", false } });
    core.name = "望";
    core.role = Role::Support;
    expect(reserve(fallback), "reservation follows the configured core, including Wang");
    config->set_start_opers({ { "卡达", false } });
    core.name = "卡达";
    core.role = Role::Caster;
    expect(reserve(fallback), "configured caster core is supported without a fixed EW name");
    auto source = json::open(plans / utils::path("狂热敬虔.json"));
    if (!source) {
        return 5;
    }
    (*source)["reserve_starting_core_groups"] = json::array {};
    if (!std::filesystem::create_directory(scratch)) {
        return 6;
    }
    const auto reset = scratch / utils::path("狂热敬虔.json");
    std::ofstream(reset) << source->to_string();
    expect(RoguelikeCopilot.load(scratch), "reload accepts explicit default-off policy");
    expect(!reserve(fallback), "reload removes a stale reservation declaration");
    std::cout << "CORE_SITE checks=" << checks << " failures=" << failures
              << " full_actor_selection=0 device_inputs=0 live_verified=0\n";
    return failures == 0 ? 0 : 1;
}
