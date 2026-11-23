#include <algorithm>
#include <filesystem>
#include <iostream>

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"

class BaiyeProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    BaiyeProbe() :
        RoguelikeBattleTaskPlugin({}, nullptr, "BaiyeProbe", std::make_shared<asst::RoguelikeConfig>(), nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_requirements_met;
    using RoguelikeBattleTaskPlugin::get_attack_range;

    void elite(const std::string& name, int value) { m_oper_elite[name] = value; }

    void forget_elite(const std::string& name) { m_oper_elite.erase(name); }
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
    const auto data = RoguelikeCopilot.get_stage_data("败叶");
    const auto level =
        json::open(repo / utils::path("resource/Arknights-Tile-Pos/ro6_t_11-obt-roguelike-ro6-level_rogue6_t-11.json"));
    if (!data || !level) {
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
    expect(data->deploy_plan_only && data->reserve_deploy_cost, "generic filler cannot spend the opening budget");
    expect(data->replacement_home.size() == 2, "both real route destinations are explicit");
    expect(data->replacement_home[0].location == Point(9, 4), "middle gate is first");
    expect(data->replacement_home[1].location == Point(10, 1), "tree-spawn destination comes from extra routes");
    const auto& lower = data->deploy_plan.at("奶盾").front();
    const auto& upper = data->deploy_plan.at("奶盾").back();
    const auto& core = data->deploy_plan.at("益达").front();
    const auto& healer = data->deploy_plan.at("元素奶").front();
    const auto& burst = data->deploy_plan.at("近卫").back();
    expect(upper.location == Point(8, 1), "upper blocker leaves a separate rear damage tile");
    expect(burst.location == Point(9, 1), "late Liberator has a distinct output tile");
    expect(burst.kill_lower_bound == 6, "actual parsed condition delays burst until the middle wave is processed");
    expect(
        burst.max_deployments == INT_MAX && !burst.require_persistent_blocking,
        "optional burst can return without becoming a permanent blocker");
    expect(
        burst.operator_names == std::vector<std::string> { "司霆惊蛰" },
        "ground-expanding skill is not attributed to all guards");
    expect(healer.required_range_tiles.size() == 4, "healing coverage is a mandatory requirement");
    expect(lower.require_persistent_blocking, "middle slot cannot use an idle Liberator");
    expect(upper.require_persistent_blocking, "upper slot cannot use an idle Liberator");
    expect(
        lower.rank < core.rank && core.rank < healer.rank && healer.rank < upper.rank,
        "middle defense precedes upper reinforcement");
    for (const auto& [point, type] : std::vector<std::pair<Point, int>> { { { 8, 4 }, 1 },
                                                                          { { 9, 3 }, 2 },
                                                                          { { 10, 3 }, 2 },
                                                                          { { 8, 1 }, 1 },
                                                                          { { 9, 1 }, 1 } }) {
        expect(
            level->at("tiles")[point.y][point.x].at("buildableType").as_integer() == type,
            "candidate role matches terrain");
    }
    expect(core.minimum_elite == 2, "two-gate core coverage requires verified elite2");
    BaiyeProbe probe;
    DeploymentOper ew;
    ew.name = "维什戴尔";
    ew.role = Role::Sniper;
    probe.elite(ew.name, 2);
    expect(probe.deployment_requirements_met(ew, core), "actual E2 EW reaches both defended approaches");
    const auto range = probe.get_attack_range(ew, core.direction);
    for (const Point target : { Point(8, 4), Point(9, 1), Point(8, 1) }) {
        expect(
            std::ranges::find(range, target - core.location) != range.end(),
            "actual core range covers a phase-specific approach");
    }
    auto wrong = core;
    wrong.direction = DeployDirection::Up;
    expect(!probe.deployment_requirements_met(ew, wrong), "upper-only facing cannot cover the middle gate");
    for (const auto& name : { "蜜莓", "纯烬艾雅法拉" }) {
        DeploymentOper medic;
        medic.name = name;
        medic.role = Role::Medic;
        for (const int elite : { 1, 2 }) {
            probe.elite(name, elite);
            expect(
                probe.deployment_requirements_met(medic, healer),
                "verified wide medic covers all four actual frontline tiles");
            const auto healing = probe.get_attack_range(medic, healer.direction);
            for (const Point target : { lower.location, core.location }) {
                expect(
                    std::ranges::find(healing, target - healer.location) != healing.end(),
                    "observed roster healer reaches lower defense");
            }
            expect(
                std::ranges::find(healing, upper.location - healer.location) != healing.end(),
                "observed wide-range healer also reaches the upper blocker");
            auto facing = healer;
            facing.direction = DeployDirection::Right;
            expect(
                !probe.deployment_requirements_met(medic, facing),
                "medic facing away cannot meet the coverage contract");
        }
    }
    DeploymentOper narrow;
    narrow.name = "芙蓉";
    narrow.role = Role::Medic;
    probe.elite(narrow.name, 1);
    expect(!probe.deployment_requirements_met(narrow, healer), "ordinary short medic cannot imitate wide coverage");
    DeploymentOper unknown;
    unknown.name = "蜜莓";
    unknown.role = Role::Medic;
    probe.forget_elite(unknown.name);
    expect(
        !probe.deployment_requirements_met(unknown, healer),
        "cached ownership cannot replace this battle's unknown elite");
    probe.elite(unknown.name, 2);
    auto false_damage = healer;
    false_damage.required_attack_tiles = { { 8, 4 } };
    expect(
        !probe.deployment_requirements_met(unknown, false_damage),
        "healing range never becomes damage qualification");
    DeploymentOper leizi;
    leizi.name = "司霆惊蛰";
    leizi.role = Role::Warrior;
    probe.elite(leizi.name, 1);
    expect(probe.deployment_requirements_met(leizi, burst), "Liberator can supply late damage behind a real blocker");
    auto guard = burst;
    guard.require_persistent_blocking = true;
    expect(!probe.deployment_requirements_met(leizi, guard), "the same Liberator cannot replace the upper guard");

    for (const auto& [name, role] : std::vector<std::pair<std::string, Role>> { { "古米", Role::Tank },
                                                                                { "机械师", Role::Tank },
                                                                                { "山", Role::Warrior },
                                                                                { "清道夫", Role::Pioneer } }) {
        DeploymentOper blocker;
        blocker.name = name;
        blocker.role = role;
        probe.elite(name, 1);
        expect(probe.deployment_requirements_met(blocker, lower), "real persistent roles can defend middle gate");
        expect(probe.deployment_requirements_met(blocker, upper), "same role is eligible for the distinct upper duty");
    }
    DeploymentOper flag;
    flag.name = "桃金娘";
    flag.role = Role::Pioneer;
    probe.elite(flag.name, 1);
    expect(!probe.deployment_requirements_met(flag, upper), "skill-dependent zero block is not persistent defence");
    const auto north = RoguelikeCopilot.get_stage_data("远北猎场");
    const auto master = RoguelikeCopilot.get_stage_data("无主地");
    if (!north || !master) {
        return 5;
    }
    const auto& north_heal = north->deploy_plan.at("元素奶").front();
    expect(
        north_heal.location == Point(6, 3) && north_heal.direction == DeployDirection::Left,
        "north healer faces the actual three friendly sites");
    expect(
        north_heal.required_range_tiles.size() == 3,
        "north range contract includes main blocker and both damage slots");
    for (const auto& name : { "纯烬艾雅法拉", "蜜莓", "桑葚" }) {
        DeploymentOper medic;
        medic.name = name;
        medic.role = Role::Medic;
        for (int elite : { 1, 2 }) {
            probe.elite(name, elite);
            expect(
                probe.deployment_requirements_met(medic, north_heal),
                "actual wide healer reaches the three northern priority sites");
            auto down = north_heal;
            down.direction = DeployDirection::Down;
            expect(
                !probe.deployment_requirements_met(medic, down),
                "old downward facing cannot meet northern coverage");
        }
    }
    DeploymentOper chestnut;
    chestnut.name = "褐果";
    chestnut.role = Role::Medic;
    probe.elite(chestnut.name, 1);
    expect(
        probe.deployment_requirements_met(chestnut, north_heal),
        "Chestnut actual E1 range covers these three priority sites");
    DeploymentOper short_medic;
    short_medic.name = "芙蓉";
    short_medic.role = Role::Medic;
    probe.elite(short_medic.name, 1);
    expect(
        !probe.deployment_requirements_met(short_medic, north_heal),
        "short healing range cannot cover all three northern sites");
    const OperNameTag angelina { Role::Special, "予愿安洁莉娜" };
    const auto group_ids = RoguelikeRecruit.get_group_ids_of_oper("BlackFlow", angelina);
    const auto group_names = RoguelikeRecruit.get_group_names("BlackFlow");
    expect(
        group_ids.size() == 1 && group_names.at(group_ids.front()) == "其他地面",
        "unlisted melee specialist uses the actual canonical fallback");
    const auto& specialist = master->deploy_plan.at("其他地面").front();
    expect(
        specialist.operator_names == std::vector<std::string> { "予愿安洁莉娜" },
        "fallback group does not admit every melee substitute to this duty");

    auto identity_plan = core;
    identity_plan.operator_names = { "维什戴尔" };
    identity_plan.required_attack_tiles.clear();
    DeploymentOper wrong_name;
    wrong_name.name = "卡达";
    wrong_name.role = Role::Caster;
    probe.elite(wrong_name.name, 2);
    expect(
        !probe.deployment_requirements_met(wrong_name, identity_plan),
        "exact identity filter applies even without range targets");
    probe.elite(ew.name, 1);
    expect(!probe.deployment_requirements_met(ew, identity_plan), "minimum elite applies even without range targets");
    probe.forget_elite(ew.name);
    expect(
        !probe.deployment_requirements_met(ew, identity_plan),
        "unknown elite cannot meet an explicit elite requirement");
    probe.elite(ew.name, 2);
    expect(probe.deployment_requirements_met(ew, identity_plan), "actual named E2 core remains eligible");
    std::cout << "BAIYE_PLAN checks=" << checks << " failures=" << failures << " device_inputs=0 timing_qualified=0\n";
    return failures ? 1 : 0;
}
