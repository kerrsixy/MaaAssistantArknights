#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

class DevotionProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    DevotionProbe() :
        RoguelikeBattleTaskPlugin(
            {},
            nullptr,
            "DevotionPrivateQualification",
            std::make_shared<asst::RoguelikeConfig>(),
            nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_cost_wait_required;
    using RoguelikeBattleTaskPlugin::deployment_requirements_met;
    using RoguelikeBattleTaskPlugin::get_attack_range;
    using Plan = DeployPlanInfo;

    void elite(const std::string& name, int elite) { m_oper_elite[name] = elite; }
};

int wmain(int argc, wchar_t** argv)
{
    using namespace asst;
    using namespace asst::battle;
    if (argc != 3) {
        return 2;
    }
    const std::filesystem::path input(argv[1]), plans(argv[2]);
    if (!BattleData.load(input / utils::path("resource/battle_data.json")) ||
        !RoguelikeRecruit.load(input / utils::path("resource/roguelike/BlackFlow/recruitment.json")) ||
        !RoguelikeCopilot.load(plans)) {
        return 3;
    }
    const auto stage = RoguelikeCopilot.get_stage_data("狂热敬虔");
    const auto tile =
        json::open(input / utils::path("resource/Arknights-Tile-Pos/ro6_b_3-obt-roguelike-ro6-level_rogue6_b-3.json"));
    if (!stage || !tile) {
        return 4;
    }
    int checks = 0, failures = 0;
    auto expect = [&](bool value, const char* label) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    DevotionProbe probe;
    const auto unit = [](const char* name, Role role) {
        DeploymentOper oper;
        oper.name = name;
        oper.role = role;
        return oper;
    };
    auto mech = unit("机械师", Role::Tank), typhon = unit("提丰", Role::Sniper), ew = unit("维什戴尔", Role::Sniper),
         medic = unit("纯烬艾雅法拉", Role::Medic);
    probe.elite(mech.name, 1);
    probe.elite(typhon.name, 1);
    probe.elite(ew.name, 2);
    probe.elite(medic.name, 1);
    const auto& block = stage->deploy_plan.at("机械师").front();
    const auto& upper = stage->deploy_plan.at("提丰").front();
    const auto& core = stage->deploy_plan.at("益达").front();
    const auto& heal = stage->deploy_plan.at("元素奶").front();
    const auto contains = [&](const auto& oper, const auto& plan, Point target) {
        const auto range = probe.get_attack_range(oper, plan.direction);
        return std::ranges::find(range, target - plan.location) != range.end();
    };
    expect(stage->deploy_plan_only && stage->reserve_deploy_cost, "dedicated duties and reservation parsed");
    expect(
        block.location == Point(10, 4) && block.direction == DeployDirection::Down,
        "baseline main blocker unchanged");
    expect(upper.location == Point(9, 3) && upper.direction == DeployDirection::Left, "baseline Typhon unchanged");
    expect(core.location == Point(7, 4) && core.direction == DeployDirection::Left, "baseline EW unchanged");
    expect(block.require_persistent_blocking, "persistent ordinary blocker required");
    std::vector<DevotionProbe::Plan> order;
    const std::vector<DeploymentOper> actors { mech, typhon, ew, medic };
    const std::vector<int> costs { 19, 24, 24, 16 };
    int index = 0;
    for (const auto& plan : { block, upper, core, heal }) {
        expect(plan.plan_index == index, "Mech then early Typhon then EW then actual ordinary healing");
        expect(probe.deployment_requirements_met(actors.at(index), plan), "actual E1 or E2 base qualification");
        expect(
            tile->at("tiles")[plan.location.y][plan.location.x].at("buildableType").as_integer() ==
                (index == 0 ? 1 : 2),
            "real target terrain legal");
        expect(plan.max_deployments > 1, "ordinary redeployment remains allowed");
        DevotionProbe::Plan sorted;
        sorted.rank = plan.rank;
        sorted.plan_index = plan.plan_index;
        sorted.prefer_group_order = plan.prefer_group_order;
        sorted.oper_name = actors.at(index).name;
        sorted.cost = costs.at(index);
        order.push_back(sorted);
        ++index;
    }
    std::sort(order.begin(), order.end());
    for (int i = 0; i < 4; ++i) {
        expect(
            order.at(i).oper_name == actors.at(i).name,
            "real comparator gives authored order despite cheaper healer");
    }
    expect(
        !contains(typhon, upper, Point(9, 2)) && !contains(typhon, upper, Point(10, 2)),
        "baseline Typhon misses final upper door approach");
    for (Point t : { Point(5, 3), Point(7, 3), Point(7, 2), Point(7, 1) }) {
        expect(contains(typhon, upper, t), "earlier Typhon has real approach firing windows");
    }
    auto variant = upper;
    variant.direction = DeployDirection::Up;
    expect(contains(typhon, variant, Point(10, 1)), "UP comparison improves upper row only, not adopted");
    expect(!contains(typhon, variant, Point(10, 2)), "UP retains adjacent minimum-distance blind spot");
    expect(!contains(mech, block, Point(10, 2)), "baseline Mech DOWN cannot shoot upper entry");
    variant = block;
    variant.direction = DeployDirection::Up;
    expect(
        contains(mech, variant, Point(10, 2)),
        "Mech UP can shoot upper entry but blocking target priority not simulated");
    expect(
        tile->at("tiles")[2][10].at("buildableType").as_integer() == 1,
        "10,2 ordinary token terrain legal, not path-rewriting proof");
    expect(tile->at("tiles")[2][9].at("buildableType").as_integer() == 0, "9,2 is not a deployable alternative");
    for (const auto& name : heal.operator_names) {
        auto oper = unit(name.c_str(), Role::Medic);
        for (int elite : { 1, 2 }) {
            probe.elite(name, elite);
            expect(
                probe.deployment_requirements_met(oper, heal),
                "ordinary medical base range covers all three real positions");
            std::cout << "MEDIC_RANGE name=" << name << " elite=" << elite
                      << " qualified=" << probe.deployment_requirements_met(oper, heal) << '\n';
        }
    }
    auto short_medic = unit("苏苏洛", Role::Medic);
    probe.elite(short_medic.name, 1);
    expect(
        !probe.deployment_requirements_met(short_medic, heal),
        "old short ordinary healer cannot restore all three duties");
    auto wrong_heal = heal;
    wrong_heal.direction = DeployDirection::Up;
    expect(
        !probe.deployment_requirements_met(medic, wrong_heal),
        "wrong medical direction rejects missing real targets");
    expect(
        std::ranges::find(heal.operator_names, "凯尔希·思衡托") == heal.operator_names.end(),
        "watchman form excluded by ordinary healing names, not by requirements alone");
    expect(
        std::ranges::find(heal.operator_names, "焰影苇草") == heal.operator_names.end(),
        "incantation damage healing not ordinary healing");
    expect(!contains(ew, core, heal.location), "medical site outside baseline EW soul-generation domain");
    mech.cost = 19;
    mech.available = false;
    mech.cooling = false;
    expect(
        DevotionProbe::deployment_cost_wait_required(mech, 10),
        "actual initial10 needs nine DP for observed Mech19");
    std::cout << "DEVOTION_ROUTES checks=" << checks << " failures=" << failures
              << " device_inputs=0 full_actor_selection=0 live_verified=0 damage_qualified=0 fire_survival=0 "
                 "token_route_rewrite_proven=0\n";
    return failures ? 1 : 0;
}
