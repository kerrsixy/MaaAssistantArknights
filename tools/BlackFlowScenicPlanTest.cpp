#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>

class ScenicProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    ScenicProbe() :
        RoguelikeBattleTaskPlugin({}, nullptr, "ScenicOfflineProbe", std::make_shared<asst::RoguelikeConfig>(), nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_requirements_met;
    using RoguelikeBattleTaskPlugin::get_attack_range;

    void elite(const std::string& n, int e) { m_oper_elite[n] = e; }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        return 2;
    }
    using namespace asst;
    using namespace asst::battle;
    const std::filesystem::path fixture(argv[1]), plans(argv[2]);
    if (!BattleData.load(fixture / utils::path("resource/battle_data.json")) ||
        !RoguelikeRecruit.load(fixture / utils::path("resource/roguelike/BlackFlow/recruitment.json")) ||
        !RoguelikeCopilot.load(plans)) {
        return 3;
    }
    const auto data = RoguelikeCopilot.get_stage_data("取景胜地");
    const auto tile = json::open(
        fixture / utils::path("resource/Arknights-Tile-Pos/ro6_n_4_8-obt-roguelike-ro6-level_rogue6_4-8.json"));
    if (!data || !tile) {
        return 4;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const auto& upper = data->deploy_plan.at("提丰").front();
    const auto& tank = data->deploy_plan.at("机械师").front();
    const auto& core = data->deploy_plan.at("益达").front();
    const auto& caster = data->deploy_plan.at("术师").front();
    const auto& medic = data->deploy_plan.at("群奶").front();
    expect(data->deploy_plan_only && data->reserve_deploy_cost, "only authored duties spend opening DP");
    expect(
        upper.plan_index == 0 && tank.plan_index == 1 && core.plan_index == 2 && caster.plan_index == 3 &&
            medic.plan_index == 4,
        "upper range duty precedes core-slot competition");
    expect(upper.rank < tank.rank && tank.rank < core.rank, "first upper output then lower blocker then core");
    expect(
        medic.required_attack_tiles.empty() && medic.required_range_tiles.size() == 4,
        "conditional healing uses real range gate");
    for (const auto& p : { upper, tank, core, caster, medic }) {
        expect(
            tile->at("tiles")[p.location.y][p.location.x].at("buildableType").as_integer() ==
                (p.plan_index == 1 ? 1 : 2),
            "native terrain legality");
        expect(p.max_deployments > 1, "ordinary redeployment remains allowed");
    }
    ScenicProbe probe;
    const auto unit = [](const char* n, Role role) {
        DeploymentOper o;
        o.name = n;
        o.role = role;
        return o;
    };
    const auto typhon = unit("提丰", Role::Sniper);
    const auto mech = unit("机械师", Role::Tank);
    const auto ew = unit("维什戴尔", Role::Sniper);
    const auto carda = unit("卡达", Role::Caster);
    const auto pod = unit("波登可", Role::Support);
    const auto leizi = unit("司霆惊蛰", Role::Warrior);
    for (const auto& o : { typhon, mech, carda, pod }) {
        probe.elite(o.name, 1);
    }
    for (const auto& o : { ew, leizi }) {
        probe.elite(o.name, 2);
    }
    expect(
        probe.deployment_requirements_met(typhon, upper),
        "actual E1 Typhon covers upper and middle exit approaches");
    expect(probe.deployment_requirements_met(mech, tank), "actual E1 Mech lower blocking duty");
    expect(probe.deployment_requirements_met(ew, core), "actual E2 EW middle and lower role");
    expect(probe.deployment_requirements_met(carda, caster), "actual E1 Carda supports both lower approaches");
    expect(probe.deployment_requirements_met(pod, medic), "Pod geometry only; S1 timing not certified");
    auto guard = tank;
    expect(!probe.deployment_requirements_met(leizi, guard), "Liberator cannot be permanent lower blocker");
    auto wrong = upper;
    wrong.location = Point(4, 4);
    wrong.direction = DeployDirection::Left;
    expect(!probe.deployment_requirements_met(typhon, wrong), "old 4,4 LEFT lacks upper duty");
    wrong.location = Point(3, 2);
    wrong.direction = DeployDirection::Down;
    expect(!probe.deployment_requirements_met(typhon, wrong), "failed late 3,2 DOWN is also wrong coverage");
    const auto in = [&](const DeploymentOper& o, const auto& p, Point target) {
        const auto range = probe.get_attack_range(o, p.direction);
        return std::ranges::find(range, target - p.location) != range.end();
    };
    for (const auto& target : upper.required_attack_tiles) {
        expect(in(typhon, upper, target), "each true upper/middle gate cell reached");
    }
    expect(
        !in(typhon, upper, Point(0, 5)) && !in(typhon, upper, Point(1, 5)),
        "upper unit does not magically cover third lower gate");
    for (const auto& target : medic.required_range_tiles) {
        expect(in(pod, medic, target), "each conditional heal recipient geometrically reachable");
    }
    std::cout << "SCENIC_CANDIDATE checks=" << checks << " failures=" << failures
              << " device_inputs=0 live_verified=0 enemy_flight_claim=0 healing_timing_qualified=0\n";
    return failures ? 1 : 0;
}
