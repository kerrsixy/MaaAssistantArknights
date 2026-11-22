#include <algorithm>
#include <filesystem>
#include <iostream>

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"

class NatureProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    NatureProbe() :
        RoguelikeBattleTaskPlugin({}, nullptr, "NatureProbe", std::make_shared<asst::RoguelikeConfig>(), nullptr)
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
    const auto data = RoguelikeCopilot.get_stage_data("本性难移");
    const auto terrain =
        json::open(repo / utils::path("resource/Arknights-Tile-Pos/ro6_n_2_3-obt-roguelike-ro6-level_rogue6_2-3.json"));
    if (!data || !terrain) {
        return 4;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool value, const char* label) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    expect(data->deploy_plan_only && data->reserve_deploy_cost, "filler cannot spend core opening budget");
    const auto& upper = data->deploy_plan.at("奶盾").front();
    const auto& lower = data->deploy_plan.at("奶盾").back();
    const auto& core = data->deploy_plan.at("益达").front();
    const auto& healer = data->deploy_plan.at("元素奶").front();
    const auto& lower_healer = data->deploy_plan.at("元素奶").back();
    const auto& support = data->deploy_plan.at("辅助").front();
    expect(upper.location == Point(8, 3) && upper.direction == DeployDirection::Up, "upper gate approach intercepted");
    expect(lower.location == Point(8, 5), "separate lower road retained");
    expect(
        upper.require_persistent_blocking && lower.require_persistent_blocking,
        "both blockers need permanent capability");
    expect(
        upper.rank < core.rank && core.rank < healer.rank && healer.rank < lower.rank && lower.rank < support.rank,
        "upper blocker and core precede optional lower damage");
    for (const auto& [point, type] : std::vector<std::pair<Point, int>> { { { 8, 3 }, 1 },
                                                                          { { 7, 2 }, 2 },
                                                                          { { 7, 3 }, 2 },
                                                                          { { 8, 5 }, 1 },
                                                                          { { 7, 4 }, 2 },
                                                                          { { 7, 6 }, 2 } }) {
        expect(terrain->at("tiles")[point.y][point.x].at("buildableType").as_integer() == type, "role matches terrain");
    }
    NatureProbe probe;
    const auto op = [&](Role role, const std::string& name, int elite) {
        DeploymentOper result;
        result.name = name;
        result.role = role;
        probe.elite(name, elite);
        return result;
    };
    const auto ew = op(Role::Sniper, "维什戴尔", 2);
    expect(probe.deployment_requirements_met(ew, core), "EW covers upper road and middle approach");
    auto wrong = core;
    wrong.direction = DeployDirection::Right;
    expect(!probe.deployment_requirements_met(ew, wrong), "backward facing fails route coverage");
    for (const auto& name : { "蜜莓", "纯烬艾雅法拉", "芙蓉", "安赛尔", "调香师" }) {
        const auto medic = op(Role::Medic, name, 1);
        expect(probe.deployment_requirements_met(medic, healer), "ordinary medic supports upper blocker and core");
        expect(
            probe.deployment_requirements_met(medic, lower_healer),
            "ordinary medic supports lower blocker and damage");
        auto damage = healer;
        damage.required_attack_tiles = damage.required_range_tiles;
        damage.required_range_tiles.clear();
        expect(!probe.deployment_requirements_met(medic, damage), "healing cannot satisfy damage predicate");
    }
    const auto leizi = op(Role::Warrior, "司霆惊蛰", 2);
    expect(!probe.deployment_requirements_met(leizi, upper), "idle Liberator cannot fill upper blocker");
    expect(!probe.deployment_requirements_met(leizi, lower), "idle Liberator cannot fill lower blocker");
    for (const auto& name : { "古米", "机械师", "号角" }) {
        expect(
            probe.deployment_requirements_met(op(Role::Tank, name, 1), upper),
            "real tank can fill upper blocker without module assumptions");
    }
    expect(
        probe.deployment_requirements_met(op(Role::Caster, "卡达", 1), support),
        "Carda reaches both central approaches");
    expect(
        !probe.deployment_requirements_met(op(Role::Medic, "unknown", 1), healer),
        "unknown identity has no fabricated healing range");
    std::cout << "NATURE_PLAN checks=" << checks << " failures=" << failures << " live_verified=0\n";
    return failures ? 1 : 0;
}

