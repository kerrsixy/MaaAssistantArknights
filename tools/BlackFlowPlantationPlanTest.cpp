#include <filesystem>
#include <iostream>

#include "Arknights-Tile-Pos/TileCalc2.hpp"
#include "AsstCaller.h"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "MaaUtils/Encoding.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"

class PlantationProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    PlantationProbe() :
        RoguelikeBattleTaskPlugin({}, nullptr, "PlantationProbe", std::make_shared<asst::RoguelikeConfig>(), nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_requirements_met;
    using RoguelikeBattleTaskPlugin::get_attack_range;

    void elite(const std::string& name, int value) { m_oper_elite[name] = value; }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    using namespace asst::battle;
    const std::filesystem::path repo(argv[1]), plans(argv[2]), output(argv[3]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str()) || !RoguelikeCopilot.load(plans)) {
        return 3;
    }
    const auto stage = RoguelikeCopilot.get_stage_data("种植园惊魂");
    const auto raw =
        json::open(repo / utils::path("resource/Arknights-Tile-Pos/ro6_n_4_2-obt-roguelike-ro6-level_rogue6_4-2.json"));
    const auto emergency =
        json::open(repo / utils::path("resource/Arknights-Tile-Pos/ro6_e_4_2-obt-roguelike-ro6-level_rogue6_4-2.json"));
    if (!stage || !raw || !emergency) {
        return 4;
    }
    const Map::Level level(*raw), emergency_level(*emergency);
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    expect(level.get_width() == 13 && level.get_height() == 8, "actual plantation is 13 by 8, not charging-pig map");
    expect(
        stage->deploy_plan_only && stage->reserve_deploy_cost,
        "owning parser retains authored-only and budget controls");
    const auto groups = RoguelikeRecruit.get_group_names(std::string(RoguelikeTheme::BlackFlow));
    PlantationProbe probe;

    struct Candidate
    {
        Role role;
        std::string name;
        std::string group;
        int elite;
        Point tile;
        DeployDirection direction;
        int buildable;
    };

    const std::vector<Candidate> candidates {
        { Role::Tank, "机械师", "机械师", 2, { 7, 4 }, DeployDirection::Down, 1 },
        { Role::Sniper, "维什戴尔", "益达", 2, { 9, 4 }, DeployDirection::Left, 2 },
        { Role::Sniper, "提丰", "提丰", 1, { 8, 1 }, DeployDirection::Left, 2 },
        { Role::Pioneer, "凛御银灰", "回费", 1, { 5, 1 }, DeployDirection::Left, 1 },
    };
    int previous_rank = -1;
    for (const auto& candidate : candidates) {
        const auto found = stage->deploy_plan.find(candidate.group);
        expect(found != stage->deploy_plan.end() && found->second.size() == 1, "real authored group resolves uniquely");
        if (found == stage->deploy_plan.end() || found->second.size() != 1) {
            continue;
        }
        const auto& plan = found->second.front();
        const DeploymentOper card { .role = candidate.role, .name = candidate.name };
        const auto ids = RoguelikeRecruit.get_group_ids_of_oper(
            std::string(RoguelikeTheme::BlackFlow),
            { candidate.role, candidate.name });
        expect(
            std::ranges::any_of(ids, [&](int id) { return groups.at(id) == candidate.group; }),
            "actual recruitment memberships admit intended operator");
        expect(
            plan.operator_names == std::vector<std::string> { candidate.name },
            "names prevent unqualified role substitution");
        expect(
            plan.location == candidate.tile && plan.direction == candidate.direction,
            "real parser retains intended tile and direction");
        expect(plan.rank > previous_rank, "initial blocker, core and two upper-route duties keep their order");
        previous_rank = plan.rank;
        expect(plan.max_deployments == INT_MAX, "ordinary combatants may redeploy after departure");
        expect(
            level.get_item(candidate.tile.y, candidate.tile.x).buildableType == candidate.buildable,
            "normal map has correct buildable terrain");
        expect(
            emergency_level.get_item(candidate.tile.y, candidate.tile.x).buildableType == candidate.buildable,
            "emergency variant retains planned buildable terrain");
        probe.elite(candidate.name, candidate.elite);
        expect(probe.deployment_requirements_met(card, plan), "actual owned elite range covers required route cells");
    }
    const DeploymentOper ew { .role = Role::Sniper, .name = "维什戴尔" };
    const DeploymentOper typhon { .role = Role::Sniper, .name = "提丰" };
    const auto excludes = [&](const DeploymentOper& card, Point origin, DeployDirection direction, Point target) {
        const auto range = probe.get_attack_range(card, direction);
        return std::ranges::find(range, target - origin) == range.end();
    };
    expect(excludes(ew, { 9, 4 }, DeployDirection::Left, { 3, 2 }), "EW basic attack excludes upper random object");
    expect(excludes(ew, { 9, 4 }, DeployDirection::Left, { 3, 6 }), "EW basic attack excludes lower random object");
    expect(
        excludes(typhon, { 8, 1 }, DeployDirection::Left, { 3, 2 }),
        "Typhon basic attack excludes upper random object");
    const auto outside_first_strike = [](Point target) {
        return std::abs(target.x - 7) > 1 || std::abs(target.y - 4) > 1;
    };
    expect(
        outside_first_strike({ 9, 4 }) && outside_first_strike({ 8, 1 }),
        "if struck defender is 7,4, outputs are outside its 3 by 3 splash");
    expect(!outside_first_strike({ 7, 4 }), "defender still takes first strike; geometry does not prove survival");
    expect(level.get_item(6, 6).buildableType == 0, "nonbuildable 6,6 cannot become a fabricated bait tile");
    // An optional inexpensive upper-route alternative must be assessed by its
    // own range. This test does not authorize treating its survivability as Typhon's.
    auto upper = stage->deploy_plan.at("提丰").front();
    upper.location = { 5, 2 };
    upper.direction = DeployDirection::Up;
    const DeploymentOper carda { .role = Role::Caster, .name = "卡达" };
    probe.elite(carda.name, 1);
    expect(level.get_item(2, 5).buildableType == 2, "Carda alternative is an actual ranged tile");
    expect(
        probe.deployment_requirements_met(carda, upper),
        "Carda alternative covers the three upper return-lane cells");
    expect(
        excludes(carda, { 5, 2 }, DeployDirection::Up, { 3, 2 }),
        "Carda alternative does not directly attack random object");
    const auto& carda_plan = stage->deploy_plan.at("术师").front();
    expect(
        carda_plan.operator_names == std::vector<std::string> { "卡达" } && carda_plan.location == Point(5, 2) &&
            carda_plan.direction == DeployDirection::Up && probe.deployment_requirements_met(carda, carda_plan),
        "actual fallback plan uses qualified Carda and its own upper-route geometry");
    expect(
        carda_plan.exclusive_locations == std::vector<Point> { { 8, 1 } } &&
            stage->deploy_plan.at("提丰").front().exclusive_locations == std::vector<Point> { { 5, 2 } },
        "two upper-route alternatives exclude one another instead of duplicating duty");
    auto impossible = stage->deploy_plan.at("机械师").front();
    impossible.required_attack_tiles.emplace_back(6, 5);
    expect(
        !probe.deployment_requirements_met({ .role = Role::Tank, .name = "机械师" }, impossible),
        "Mech does not cover adjacent left lane; EW has that duty");
    std::cout << "PLANTATION checks=" << checks << " failures=" << failures
              << " connections=0 inputs=0 live_pass=false\n";
    return failures ? 1 : 0;
}
