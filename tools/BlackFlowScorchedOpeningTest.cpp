#include <algorithm>
#include <filesystem>
#include <iostream>

#include "Arknights-Tile-Pos/TileDef.hpp"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"

class ScorchedOpeningProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    ScorchedOpeningProbe() :
        RoguelikeBattleTaskPlugin({}, nullptr, "ScorchedProbe", std::make_shared<asst::RoguelikeConfig>(), nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_cost_wait_required;
    using RoguelikeBattleTaskPlugin::deployment_plan_exhausted;
    using RoguelikeBattleTaskPlugin::deployment_requirements_met;
    using RoguelikeBattleTaskPlugin::get_attack_range;

    void elite(const std::string& name, int value) { m_oper_elite[name] = value; }

    void confirmations(int index, int value) { m_plan_deployment_counts[index] = value; }
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
    const auto data = RoguelikeCopilot.get_stage_data("焦土之家");
    const auto raw = json::open(
        repo / utils::path(
                   "resource/Arknights-Tile-Pos/"
                   "ro6_n_3_5-obt-roguelike-ro6-level_rogue6_3-5.json"));
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
    const Map::Level level(*raw);
    ScorchedOpeningProbe probe;
    const auto crate_entries = data->deploy_plan.find("障碍物");
    expect(crate_entries != data->deploy_plan.end(), "real terrain crate has an explicit deployment plan");
    if (crate_entries == data->deploy_plan.end()) {
        std::cout << "SCORCHED_OPENING checks=" << checks << " failures=" << failures << " device_inputs=0\n";
        return 1;
    }
    expect(crate_entries->second.size() == 1, "only one planned crate is paid for");
    const auto& crate = crate_entries->second.front();
    const auto& main = data->deploy_plan.at("益达").front();
    const auto& blocker = data->deploy_plan.at("机械师").front();
    const auto& medic = data->deploy_plan.at("元素奶").front();
    const auto& secondary = data->deploy_plan.at("提丰").back();
    expect(data->reserve_deploy_cost, "wait for core instead of spending its budget on a cheaper substitute");
    expect(data->deploy_plan_only, "only the reviewed terrain and combat plans may execute");
    expect(crate.location == Point(5, 2), "crate closes the lower approach while leaving the upper approach open");
    expect(crate.direction == DeployDirection::None, "crate requires no facing gesture");
    expect(crate.max_deployments == 1, "one confirmation exhausts this crate plan");
    expect(
        crate.operator_names == std::vector<std::string> { "障碍物" },
        "covers and phantoms cannot become roadblocks");
    expect(crate.rank < blocker.rank, "cheap terrain is prepared before the main blocker");
    expect(blocker.rank < main.rank && main.rank < medic.rank, "blocker then main then independent healing");
    expect(
        main.location == Point(6, 2) && main.direction == DeployDirection::Up,
        "main keeps the upper approach covered even if the crate fails");
    expect(medic.location != main.location, "healing has a separate tile");
    for (const auto& [group, entries] : data->deploy_plan) {
        for (const auto& entry : entries) {
            const auto tile = level.get_item(entry.location.y, entry.location.x);
            expect(tile.buildableType != 0 && tile.tileKey != "tile_volcano", "all placements are legal and off lava");
            if (entry.location == main.location) {
                expect(
                    std::ranges::find(entry.required_attack_tiles, Point(5, 1)) != entry.required_attack_tiles.end() &&
                        std::ranges::find(entry.required_attack_tiles, Point(8, 1)) !=
                            entry.required_attack_tiles.end(),
                    "every main-slot substitute must reach the choke and the extended "
                    "right approach");
            }
        }
    }
    const auto eligible = [&](Role role, const std::string& name, const auto& plan) {
        probe.elite(name, name == "维什戴尔" ? 2 : 1);
        DeploymentOper oper;
        oper.role = role;
        oper.name = name;
        return probe.deployment_requirements_met(oper, plan);
    };
    expect(eligible(Role::Sniper, "维什戴尔", main), "actual E2 core reaches both lanes");
    expect(!eligible(Role::Caster, "卡达", main), "short caster does not pretend to cover the extended right approach");
    expect(!eligible(Role::Medic, "纯烬艾雅法拉", main), "ordinary healing cannot substitute for primary damage");
    auto nearby = main;
    nearby.required_attack_tiles = { Point(5, 1) };
    expect(eligible(Role::Support, "酒神", nearby), "ritualist remains eligible for an actually covered damage task");
    expect(!eligible(Role::Support, "浊心斯卡蒂", main), "non-attacking bard cannot substitute for primary damage");
    const auto main_rank = [&](Role role, const std::string& name) {
        int rank = INT_MAX;
        const auto groups = RoguelikeRecruit.get_group_names("BlackFlow");
        for (int id : RoguelikeRecruit.get_group_ids_of_oper("BlackFlow", OperNameTag { role, name })) {
            const auto iter = data->deploy_plan.find(groups.at(id));
            if (iter == data->deploy_plan.end()) {
                continue;
            }
            for (const auto& entry : iter->second) {
                if (entry.location == main.location && eligible(role, name, entry)) {
                    rank = std::min(rank, entry.rank);
                }
            }
        }
        return rank;
    };
    expect(
        main_rank(Role::Sniper, "维什戴尔") < main_rank(Role::Support, "酒神"),
        "real recruitment groups prefer the equipped core without banning "
        "other compositions");
    expect(!eligible(Role::Sniper, "提丰", main), "Typhon near blind zone is rejected at the primary tile");
    expect(eligible(Role::Sniper, "提丰", secondary), "Typhon uses its own distance-aware secondary tile");
    expect(eligible(Role::Tank, "机械师", blocker), "actual opening blocker reaches its approach");
    expect(!eligible(Role::Drone, "结构性原理", blocker), "self-only token is not the permanent opening blocker");
    expect(eligible(Role::Drone, "障碍物", crate), "actual named crate qualifies without operator promotion");
    expect(!eligible(Role::Drone, "掩体", crate), "other members of the obstacle group cannot rewrite routes");
    expect(!probe.deployment_plan_exhausted(crate), "crate starts uncommitted");
    probe.confirmations(crate.plan_index, 1);
    expect(probe.deployment_plan_exhausted(crate), "confirmed crate is never bought twice");
    DeploymentOper waiting { .role = Role::Drone, .cost = 5, .available = false, .cooling = false, .name = "障碍物" };
    expect(
        probe.deployment_cost_wait_required(waiting, 4),
        "insufficient observed DP waits for the real five-cost card");
    expect(
        !probe.deployment_cost_wait_required(waiting, 5),
        "unavailable with enough DP is not misreported as a cost wait");
    waiting.cooling = true;
    expect(!probe.deployment_cost_wait_required(waiting, 4), "card cooldown is not a new terrain budget reservation");
    for (const auto& name : { "纯烬艾雅法拉", "蜜莓", "芙蓉" }) {
        probe.elite(name, 1);
        DeploymentOper oper;
        oper.role = Role::Medic;
        oper.name = name;
        const auto range = probe.get_attack_range(oper, medic.direction);
        expect(std::ranges::find(range, main.location - medic.location) != range.end(), "actual medic reaches core");
        expect(
            std::ranges::find(range, blocker.location - medic.location) != range.end(),
            "actual medic reaches blocker");
    }
    std::cout << "SCORCHED_OPENING checks=" << checks << " failures=" << failures << " device_inputs=0\n";
    return failures ? 1 : 0;
}
