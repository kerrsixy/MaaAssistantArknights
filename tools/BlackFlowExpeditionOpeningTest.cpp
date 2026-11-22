#include <algorithm>
#include <filesystem>
#include <iostream>

#include "Arknights-Tile-Pos/TileDef.hpp"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"

class BossOpeningProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    BossOpeningProbe() :
        RoguelikeBattleTaskPlugin({}, nullptr, "BossOpeningProbe", std::make_shared<asst::RoguelikeConfig>(), nullptr)
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
    const auto data = RoguelikeCopilot.get_stage_data("光荣远征");
    const auto raw = json::open(
        repo / utils::path(
                   "resource/Arknights-Tile-Pos/"
                   "ro6_b_2-obt-roguelike-ro6-level_rogue6_b-2.json"));
    if (!data || !raw) {
        return 4;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool result, const char* label) {
        ++checks;
        if (!result) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const auto& core = data->deploy_plan.at("益达").front();
    const auto& blocker = data->deploy_plan.at("机械师").front();
    const auto& medic = data->deploy_plan.at("元素奶").front();
    expect(data->reserve_deploy_cost, "actual card cost is reserved for the earlier role");
    expect(
        blocker.rank < medic.rank && medic.rank < core.rank,
        "medic occupies the shared tile before core summons can claim it");
    expect(
        blocker.location == Point(5, 5) && blocker.direction == DeployDirection::Up,
        "position4 is the main blocker facing the gold spawn approach");
    expect(
        core.location == Point(4, 5) && core.direction == DeployDirection::Up,
        "core covers the middle lane from the user position2");
    expect(
        medic.location == Point(4, 3) && medic.direction == DeployDirection::Down,
        "healing faces the core and two blocking lanes");
    const Map::Level level(*raw);
    for (const auto& [group, entries] : data->deploy_plan) {
        for (const auto& entry : entries) {
            const auto tile = level.get_item(entry.location.y, entry.location.x);
            expect(
                entry.location.x < 6 && tile.buildableType != 0 && entry.location != Point(5, 4),
                "placements stay on legal left or central tiles and off the gold "
                "objective");
        }
    }
    BossOpeningProbe probe;
    DeploymentOper ew;
    ew.role = Role::Sniper;
    ew.name = "维什戴尔";
    probe.elite(ew.name, 2);
    const auto range = probe.get_attack_range(ew, core.direction);
    for (const Point target : { Point(3, 2), Point(3, 3), Point(5, 2), Point(5, 3) }) {
        expect(
            std::ranges::find(range, target - core.location) != range.end(),
            "E2 core reaches the middle approach and both central lanes");
    }
    expect(probe.deployment_requirements_met(ew, core), "actual core satisfies its target coverage");
    auto stale_direction = core;
    stale_direction.direction = DeployDirection::Left;
    expect(
        !probe.deployment_requirements_met(ew, stale_direction),
        "run39 left-facing deployment cannot satisfy the central lane coverage");
    expect(!medic.operator_names.empty(), "medical candidates are independently role and range qualified");
    expect(
        std::ranges::find(medic.operator_names, "蜜莓") != medic.operator_names.end(),
        "actual roster healer is eligible");
    const auto groups = RoguelikeRecruit.get_group_names("BlackFlow");
    for (const auto& name : medic.operator_names) {
        DeploymentOper oper;
        oper.role = Role::Medic;
        oper.name = name;
        const auto props = BattleData.find_first_oper(oper.role, name);
        expect(static_cast<bool>(props), "listed healer has a real Medic identity");
        const auto ids = RoguelikeRecruit.get_group_ids_of_oper("BlackFlow", OperNameTag { Role::Medic, name });
        std::cout << "MEDIC_GROUPS " << name;
        for (int id : ids) {
            std::cout << ' ' << groups.at(id);
        }
        std::cout << '\n';
        expect(
            std::ranges::any_of(
                ids,
                [&](int id) {
                    const auto it = data->deploy_plan.find(groups.at(id));
                    return it != data->deploy_plan.end() && std::ranges::any_of(it->second, [&](const auto& plan) {
                               return plan.plan_index == medic.plan_index;
                           });
                }),
            "actual recruitment groups can reach the intended healer plan");
        for (const int elite : { 1, 2 }) {
            probe.elite(name, elite);
            const auto heal_range = probe.get_attack_range(oper, medic.direction);
            expect(
                std::ranges::all_of(
                    std::vector<Point> { core.location, blocker.location, Point(5, 3) },
                    [&](const Point& target) {
                        return std::ranges::find(heal_range, target - medic.location) != heal_range.end();
                    }),
                "actual E1 and E2 healer ranges cover all three defended tiles");
        }
    }
    const auto& pusher = data->deploy_plan.at("其他地面").front();
    expect(
        pusher.location == Point(5, 3) && pusher.direction == DeployDirection::Down,
        "position1 pushes the gold toward the lower blue gate");
    expect(pusher.rank > core.rank, "functional pusher does not spend the opening core budget");
    for (const auto& name : pusher.operator_names) {
        DeploymentOper oper;
        oper.name = name;
        oper.role = Role::Special;
        for (const int elite : { 0, 1, 2 }) {
            probe.elite(name, elite);
            expect(
                probe.deployment_requirements_met(oper, pusher),
                "real pusher attack reaches the adjacent gold tile");
        }
    }
    expect(
        std::ranges::find(pusher.operator_names, "暗索") == pusher.operator_names.end() &&
            std::ranges::find(pusher.operator_names, "崖心") == pusher.operator_names.end() &&
            std::ranges::find(pusher.operator_names, "结构性原理") == pusher.operator_names.end(),
        "pullers and expendable workers cannot replace the functional pusher");
    std::cout << "BOSS_OPENING checks=" << checks << " failures=" << failures << " device_inputs=0\n";
    return failures ? 1 : 0;
}
