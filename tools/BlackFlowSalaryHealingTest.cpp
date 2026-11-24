#include <algorithm>
#include <filesystem>
#include <iostream>

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"

class SalaryProbe final : public asst::RoguelikeBattleTaskPlugin
{
public:
    SalaryProbe() :
        RoguelikeBattleTaskPlugin(
            {},
            nullptr,
            "SalaryHealingQualification",
            std::make_shared<asst::RoguelikeConfig>(),
            nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_requirements_met;
    using RoguelikeBattleTaskPlugin::get_attack_range;

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
    const auto stage = RoguelikeCopilot.get_stage_data("恶意讨薪");
    const auto tile =
        json::open(repo / utils::path("resource/Arknights-Tile-Pos/ro6_n_5_3-obt-roguelike-ro6-level_rogue6_5-3.json"));
    if (!stage || !tile) {
        return 4;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool value, const char* text) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << text << '\n';
        }
    };
    const auto& block = stage->deploy_plan.at("机械师").front();
    const auto& heal = stage->deploy_plan.at("元素奶").front();
    const auto& ew = stage->deploy_plan.at("益达").front();
    const auto& typhon = stage->deploy_plan.at("提丰").front();
    SalaryProbe probe;
    expect(stage->deploy_plan_only && stage->reserve_deploy_cost, "explicit tactical duties and cost reservation");
    expect(
        block.plan_index == 0 && heal.plan_index == 1 && ew.plan_index == 2 && typhon.plan_index == 3,
        "healer claims a verified site before the EW Soul domain");
    expect(
        heal.location == Point(3, 2) && heal.direction == DeployDirection::Right,
        "shared healing site preserves diagonal offset from main blocker");
    expect(tile->at("tiles")[2][3].at("buildableType").as_integer() == 2, "healing site is actual ranged terrain");
    expect(heal.required_range_tiles.size() == 3, "both damage dealers and blocker require real healing coverage");
    for (int elite : { 1, 2 }) {
        DeploymentOper pure;
        pure.role = Role::Medic;
        pure.name = "纯烬艾雅法拉";
        probe.elite(pure.name, elite);
        expect(probe.deployment_requirements_met(pure, heal), "actual E1/E2 wandering medic covers all three duties");
        auto wrong = heal;
        wrong.direction = DeployDirection::Left;
        expect(!probe.deployment_requirements_met(pure, wrong), "wrong facing cannot claim shared healing");
    }
    for (const auto& name : { "安赛尔", "芙蓉", "闪灵", "白面鸮" }) {
        DeploymentOper medic;
        medic.role = Role::Medic;
        medic.name = name;
        probe.elite(name, 1);
        expect(
            !probe.deployment_requirements_met(medic, heal),
            "shorter base healing range cannot substitute by label");
    }
    const std::vector<DeploymentOper> actors {
        { .role = Role::Tank, .name = "机械师" },
        { .role = Role::Sniper, .name = "维什戴尔" },
        { .role = Role::Sniper, .name = "提丰" },
    };
    const std::vector duties { block, ew, typhon };
    for (std::size_t i = 0; i < actors.size(); ++i) {
        probe.elite(actors[i].name, i == 1 ? 2 : 1);
        expect(
            probe.deployment_requirements_met(actors[i], duties[i]),
            "original damage/block coverage remains qualified");
        expect(
            tile->at("tiles")[duties[i].location.y][duties[i].location.x].at("buildableType").as_integer() ==
                (i ? 2 : 1),
            "selected duty terrain matches role");
    }
    const auto delta = heal.location - block.location;
    expect(delta.x != 0 && delta.y != 0, "healer outside blocker-centred cross-shaped direct splash");
    const auto soul_domain = probe.get_attack_range(actors[1], ew.direction);
    expect(
        std::ranges::find(soul_domain, heal.location - ew.location) != soul_domain.end(),
        "healer-first ordering is necessary because its site is in complete Soul domain");
    expect(
        std::ranges::find(soul_domain, typhon.location - ew.location) != soul_domain.end(),
        "later Typhon site still requires existing verified Soul reclaim, not assumed vacancy");
    auto old = typhon;
    old.location = Point(9, 3);
    old.direction = DeployDirection::Left;
    expect(!probe.deployment_requirements_met(actors[2], old), "run60 blind right-side location remains rejected");
    for (int phase : { 1, 2 }) {
        DeploymentOper berry { .role = Role::Medic, .name = "蜜莓" };
        probe.elite(berry.name, phase);
        expect(
            probe.deployment_requirements_met(berry, heal),
            "Honeyberry E1/E2 covers all three duties at the same diagonally offset candidate site");
    }
    std::cout << "SALARY_HEALING checks=" << checks << " failures=" << failures
              << " device_inputs=0 live_pass=false survival_simulated=false\n";
    return failures ? 1 : 0;
}
