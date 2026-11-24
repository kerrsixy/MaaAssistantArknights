#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"
#include <filesystem>
#include <iostream>

class ScorchedProbe final : public asst::RoguelikeBattleTaskPlugin
{
public:
    ScorchedProbe() :
        RoguelikeBattleTaskPlugin(
            {},
            nullptr,
            "ScorchedConditional",
            std::make_shared<asst::RoguelikeConfig>(),
            nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_requirements_met;

    void elite(const std::string& name, int value) { m_oper_elite[name] = value; }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        return 2;
    }
    using namespace asst;
    using namespace asst::battle;
    const std::filesystem::path repo(argv[1]), plans(argv[2]);
    if (!BattleData.load(repo / utils::path("resource/battle_data.json")) ||
        !RoguelikeRecruit.load(repo / utils::path("resource/roguelike/BlackFlow/recruitment.json")) ||
        !RoguelikeCopilot.load(plans)) {
        return 3;
    }
    const auto stage = RoguelikeCopilot.get_stage_data("焦土之家");
    const auto tile =
        json::open(repo / utils::path("resource/Arknights-Tile-Pos/ro6_n_3_5-obt-roguelike-ro6-level_rogue6_3-5.json"));
    if (!stage || !tile) {
        return 4;
    }
    const auto& main = stage->deploy_plan.at("机械师").at(0);
    const auto& right = stage->deploy_plan.at("机械师").at(1);
    const auto& left = stage->deploy_plan.at("地面阻挡").at(2);
    const auto& ew = stage->deploy_plan.at("益达").front();
    const auto& med = stage->deploy_plan.at("元素奶").front();
    const auto& carda = stage->deploy_plan.at("高台法术").at(1);
    int checks = 0, failures = 0;
    const auto expect = [&](bool value, const char* reason) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << reason << '\n';
        }
    };
    ScorchedProbe probe;
    expect(
        main.location == Point(5, 1) && right.location == Point(6, 1) && left.location == Point(5, 2),
        "main and two distinct lane fallback sites");
    expect(
        main.plan_index == 0 && right.plan_index == 1 && left.plan_index == 2 && ew.plan_index == 3,
        "fallback guards precede EW within authored sequence");
    for (auto point : { main.location, right.location, left.location }) {
        expect(
            tile->at("tiles")[point.y][point.x].at("buildableType").as_integer() == 1,
            "guard positions are real ground");
    }
    DeploymentOper mech { .role = Role::Tank, .name = "机械师" };
    DeploymentOper pioneer { .role = Role::Pioneer, .name = "晓歌" };
    probe.elite(mech.name, 1);
    probe.elite(pioneer.name, 1);
    expect(probe.deployment_requirements_met(mech, main), "normal main guard qualifies at actual E1");
    expect(probe.deployment_requirements_met(mech, right), "Mech can face root from right fallback");
    expect(probe.deployment_requirements_met(pioneer, left), "actual E1 Xiaoge covers left guard duty");
    expect(main.exclusive_locations == std::vector<Point> { { 6, 1 } }, "main is exclusive with its alternate");
    expect(
        right.exclusive_locations == std::vector<Point> { { 5, 1 } },
        "right alternate is exclusive with normal main");
    expect(
        left.exclusive_locations == std::vector<Point> { { 5, 1 } },
        "left supplemental guard is suppressed by normal main");
    DeploymentOper core { .role = Role::Sniper, .name = "维什戴尔" };
    probe.elite(core.name, 2);
    auto duties = ew;
    duties.required_attack_tiles = { { 5, 1 }, { 5, 2 }, { 6, 1 }, { 8, 1 } };
    expect(
        probe.deployment_requirements_met(core, duties),
        "EW up covers root and both fallback guards plus right approach");
    duties.direction = DeployDirection::Right;
    expect(!probe.deployment_requirements_met(core, duties), "right-facing EW misses left guard duty");
    for (const auto& name : { "安赛尔", "芙蓉", "纯烬艾雅法拉", "蜜莓" }) {
        DeploymentOper healer { .role = Role::Medic, .name = name };
        probe.elite(healer.name, 1);
        expect(
            probe.deployment_requirements_met(healer, med),
            "real E1 medic covers main, both fallback guards and EW");
    }
    DeploymentOper output { .role = Role::Caster, .name = "卡达" };
    probe.elite(output.name, 1);
    expect(probe.deployment_requirements_met(output, carda), "actual E1 Carda retains central coverage");
    std::cout << "SCORCHED_CONDITIONAL checks=" << checks << " failures=" << failures
              << " connections=0 input_calls=0 live_pass=false\n";
    return failures ? 1 : 0;
}
