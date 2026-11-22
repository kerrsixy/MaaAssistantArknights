#include <algorithm>
#include <filesystem>
#include <iostream>

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"

class MechanismProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    MechanismProbe() :
        RoguelikeBattleTaskPlugin({}, nullptr, "MechanismProbe", std::make_shared<asst::RoguelikeConfig>(), nullptr)
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
    int checks = 0, failures = 0;
    const auto expect = [&](bool value, const char* label) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    MechanismProbe probe;
    const auto op = [&](Role role, const std::string& name, int elite) {
        DeploymentOper result;
        result.name = name;
        result.role = role;
        probe.elite(name, elite);
        return result;
    };
    const auto ew = op(Role::Sniper, "维什戴尔", 1);
    const auto mech = op(Role::Tank, "机械师", 1);
    const auto liberator = op(Role::Warrior, "司霆惊蛰", 2);
    for (const auto& stage : { "选边站", "刺生灾害" }) {
        const auto data = RoguelikeCopilot.get_stage_data(stage);
        expect(data.has_value(), "real parser accepts mechanism candidate");
        if (!data) {
            continue;
        }
        expect(
            data->deploy_plan_only && data->reserve_deploy_cost,
            "core tile and budget protected from generic filler");
        const auto& blocker = data->deploy_plan.at("机械师").front();
        const auto& core = data->deploy_plan.at("益达").front();
        expect(blocker.rank < core.rank, "first interception precedes reserved core");
        expect(blocker.require_persistent_blocking, "first intercept needs persistent block");
        expect(probe.deployment_requirements_met(mech, blocker), "real tank is eligible");
        expect(
            !probe.deployment_requirements_met(liberator, blocker),
            "idle Liberator cannot substitute persistent block");
        expect(probe.deployment_requirements_met(ew, core), "actual EW range reaches authored approach");
        expect(
            core.operator_names == std::vector<std::string> { "维什戴尔" },
            "conditional EW mechanism cannot take unrelated generic ranged");
        auto wrong = core;
        wrong.direction = core.direction == DeployDirection::Left ? DeployDirection::Right : DeployDirection::Left;
        expect(!probe.deployment_requirements_met(ew, wrong), "reverse orientation misses approach");
        const std::string tilefile =
            std::string(stage) == "选边站"
                ? "resource/Arknights-Tile-Pos/ro6_n_4_3-obt-roguelike-ro6-level_rogue6_4-3.json"
                : "resource/Arknights-Tile-Pos/ro6_n_5_9-obt-roguelike-ro6-level_rogue6_5-9.json";
        const auto terrain = json::open(repo / utils::path(tilefile));
        expect(terrain.has_value(), "stage terrain available");
        if (!terrain) {
            continue;
        }
        expect(
            terrain->at("tiles")[blocker.location.y][blocker.location.x].at("buildableType").as_integer() == 1,
            "first blocker stands on ground");
        expect(
            terrain->at("tiles")[core.location.y][core.location.x].at("buildableType").as_integer() == 2,
            "EW has a real high tile");
        if (std::string(stage) == "选边站") {
            auto bottom = core;
            bottom.required_attack_tiles = { { 5, 6 } };
            expect(
                !probe.deployment_requirements_met(ew, bottom),
                "EW base range does not pretend to cover bottom beast");
            const auto& support = data->deploy_plan.at("辅助").front();
            expect(
                probe.deployment_requirements_met(op(Role::Support, "波登可", 1), support),
                "Podenco range reaches blocker without claiming permanent healing");
            const auto& long_range = data->deploy_plan.at("狙击").back();
            expect(
                probe.deployment_requirements_met(op(Role::Sniper, "提丰", 1), long_range),
                "Typhon real range covers lower central road from left");
            auto clipped_corner = long_range;
            clipped_corner.required_attack_tiles = { { 5, 6 } };
            expect(
                !probe.deployment_requirements_met(op(Role::Sniper, "提丰", 1), clipped_corner),
                "Typhon clipped corner does not cover bottom beast at 5,6");
            expect(
                !probe.deployment_requirements_met(op(Role::Caster, "卡达", 1), long_range),
                "ordinary caster cannot replace long lower coverage");
        }
        else {
            int high = 0;
            for (const auto& row : terrain->at("tiles").as_array()) {
                for (const auto& tile : row.as_array()) {
                    high += tile.at("buildableType").as_integer() == 2;
                }
            }
            expect(high == 2, "Spine has exactly two high tiles");
            expect(
                !data->deploy_plan.contains("高台输出") && !data->deploy_plan.contains("辅助"),
                "limited high tiles exclude early generic attackers");
            const auto& medic = data->deploy_plan.at("元素奶").front();
            expect(core.rank < medic.rank, "EW budget reserved before healing");
            for (const auto& name : { "蜜莓", "纯烬艾雅法拉" }) {
                expect(
                    probe.deployment_requirements_met(op(Role::Medic, name, 1), medic),
                    "real elemental medic range covers core and central interception");
            }
            auto left_approach = blocker;
            left_approach.required_attack_tiles = { { 2, 6 } };
            expect(probe.deployment_requirements_met(mech, left_approach), "left blocker reaches incoming runners");
            for (const auto& side : data->deploy_plan.at("机械师")) {
                expect(side.require_persistent_blocking, "temporary tokens cannot fill later permanent intercepts");
                expect(
                    terrain->at("tiles")[side.location.y][side.location.x].at("buildableType").as_integer() == 1,
                    "all interception sites have ground terrain");
            }
        }
    }
    std::cout << "MECHANISM_PLAN checks=" << checks << " failures=" << failures << " live_verified=0\n";
    return failures ? 1 : 0;
}
