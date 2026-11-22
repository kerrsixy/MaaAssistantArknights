#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "Arknights-Tile-Pos/TileDef.hpp"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"

class SurvivalLayoutProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    SurvivalLayoutProbe() :
        RoguelikeBattleTaskPlugin({}, nullptr, "LayoutProbe", std::make_shared<asst::RoguelikeConfig>(), nullptr)
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
    const std::filesystem::path repo(argv[1]), out(argv[2]);
    std::filesystem::create_directories(out);
    if (!BattleData.load(repo / utils::path("resource/battle_data.json")) ||
        !RoguelikeRecruit.load(repo / utils::path("resource/roguelike/BlackFlow/recruitment.json")) ||
        !RoguelikeCopilot.load(repo / utils::path("resource/roguelike/BlackFlow/autopilot"))) {
        return 3;
    }
    const auto data = RoguelikeCopilot.get_stage_data("生存习性");
    const auto raw =
        json::open(repo / utils::path("resource/Arknights-Tile-Pos/ro6_n_3_2-obt-roguelike-ro6-level_rogue6_3-2.json"));
    if (!data || !raw) {
        return 4;
    }
    const Map::Level level(*raw);
    SurvivalLayoutProbe probe;
    int checks = 0, failures = 0;
    const auto expect = [&](bool result, const char* label) {
        ++checks;
        if (!result) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const auto& main = data->deploy_plan.at("益达").front();
    const auto& blocker = data->deploy_plan.at("机械师").front();
    const auto& medic = data->deploy_plan.at("元素奶").front();
    const auto& token = data->deploy_plan.at("其他地面").front();
    const auto& typhon = data->deploy_plan.at("提丰").front();
    expect(
        data->deploy_plan_only && data->reserve_deploy_cost,
        "core budget is reserved and generic placement disabled");
    expect(main.location == Point(6, 5) && main.direction == DeployDirection::Up, "bottom main faces up");
    expect(
        blocker.location.y == main.location.y && blocker.location.x > main.location.x,
        "bottom interception is before the main on the right-to-left pig route");
    expect(main.rank < blocker.rank && blocker.rank < medic.rank, "healing cannot spend the opening core budget first");
    expect(
        medic.location != main.location && medic.location != blocker.location,
        "medical and combat sites are distinct");
    expect(
        medic.required_attack_tiles.empty(),
        "medical placement does not misuse damage-only eligibility constraints");
    for (const auto& [group, plans] : data->deploy_plan) {
        for (const auto& plan : plans) {
            const auto tile = level.get_item(plan.location.y, plan.location.x);
            expect(tile.buildableType == 3, "every configured placement is a real ALL tile");
        }
    }
    expect(level.get_item(2, 9).buildableType == 0, "raised-looking non-buildable tile stays excluded");
    const auto in_group = [](Role role, const std::string& name, const std::string& group) {
        const auto ids = RoguelikeRecruit.get_group_ids_of_oper("BlackFlow", OperNameTag { role, name });
        const auto names = RoguelikeRecruit.get_group_names("BlackFlow");
        return std::ranges::any_of(ids, [&](int id) { return names.at(id) == group; });
    };
    expect(in_group(Role::Drone, "结构性原理", "其他地面"), "actual summon fallback group reaches its named plan");
    expect(
        token.operator_names == std::vector<std::string> { "结构性原理" },
        "other high-ground units cannot steal summon site");
    expect(in_group(Role::Tank, "机械师", "机械师"), "actual opener matches bottom interception group");
    expect(in_group(Role::Medic, "蜜莓", "元素奶"), "actual healer matches its separate medical plan");
    for (const auto& group : { "元素奶", "奶", "单奶", "群奶" }) {
        expect(!in_group(Role::Sniper, "维什戴尔", group), "core is not routed through healing groups");
        expect(
            std::ranges::none_of(
                data->deploy_plan.at(group),
                [&](const auto& p) { return p.location == main.location; }),
            "healing groups have no main output placement");
    }
    json::array range_receipts;
    for (int elite : { 1, 2 }) {
        const DeploymentOper ew { .role = Role::Sniper, .name = "维什戴尔" };
        const DeploymentOper healer { .role = Role::Medic, .name = "蜜莓" };
        const DeploymentOper sniper { .role = Role::Sniper, .name = "提丰" };
        probe.elite(ew.name, elite);
        probe.elite(healer.name, elite);
        probe.elite(sniper.name, elite);
        expect(probe.deployment_requirements_met(ew, main), "actual actor accepts promoted EW main coverage");
        expect(!probe.deployment_requirements_met(healer, main), "actual actor refuses a healer as main damage");
        auto wrong_direction = main;
        wrong_direction.direction = DeployDirection::Left;
        expect(
            !probe.deployment_requirements_met(ew, wrong_direction),
            "left-facing EW cannot replace the up-facing plan");
        expect(probe.deployment_requirements_met(sniper, typhon), "actual actor accepts promoted Typhon special range");
        expect(!probe.deployment_requirements_met(sniper, main), "Typhon cannot steal the incompatible main site");
        const auto healing = probe.get_attack_range(healer, medic.direction);
        for (Point target : { main.location, blocker.location, token.location }) {
            expect(
                std::ranges::find(healing, target - medic.location) != healing.end(),
                "actual Honeyberry range covers output, bottom blocker and upper token");
        }
        json::array absolute;
        for (Point tile : probe.get_attack_range(ew, main.direction)) {
            absolute.emplace_back(json::array { tile.x + main.location.x, tile.y + main.location.y });
        }
        range_receipts.emplace_back(json::object { { "elite", elite }, { "ew_absolute_range", absolute } });
    }
    const auto recruitment = json::open(repo / utils::path("resource/roguelike/BlackFlow/recruitment.json"));
    if (!recruitment) {
        return 5;
    }
    auto reordered = *recruitment;
    auto& priorities = reordered["priority"].as_array();
    std::ranges::reverse(priorities);
    priorities.push_back(json::object { { "name", "AppendedTestGroup" }, { "opers", json::array {} } });
    const auto reordered_path = out / "reordered_recruitment.json";
    std::ofstream(reordered_path) << reordered.to_string();
    expect(RoguelikeRecruit.load(reordered_path), "reordered real recruitment still parses");
    expect(in_group(Role::Drone, "结构性原理", "其他地面"), "actual melee token survives group reordering and append");
    expect(in_group(Role::Tank, "UnlistedTestTank", "其他地面"), "unlisted tank resolves semantic ground fallback");
    expect(
        in_group(Role::Caster, "UnlistedTestCaster", "其他高台"),
        "unlisted caster resolves semantic ranged fallback");
    expect(in_group(Role::Medic, "UnlistedTestMedic", "其他高台"), "unlisted healer resolves semantic ranged fallback");
    expect(in_group(Role::Sniper, "维什戴尔", "益达"), "explicit core membership survives fallback changes");
    expect(
        RoguelikeRecruit.get_group_ids_of_oper("BlackFlow", "结构性原理") ==
            RoguelikeRecruit.get_group_ids_of_oper("BlackFlow", OperNameTag { Role::Drone, "结构性原理" }),
        "name-only and typed lookups share one resolver");
    expect(
        RoguelikeRecruit.load(repo / utils::path("resource/roguelike/Phantom/recruitment.json")),
        "legacy Phantom recruitment loads");
    const auto legacy_ids =
        RoguelikeRecruit.get_group_ids_of_oper("Phantom", OperNameTag { Role::Caster, "UnlistedTestCaster" });
    const auto legacy_groups = RoguelikeRecruit.get_group_names("Phantom");
    expect(
        legacy_ids.size() == 1 && legacy_groups.at(legacy_ids.front()) == "高台预备",
        "legacy ranged alias resolves without positional assumptions");
    expect(
        RoguelikeRecruit.load(repo / utils::path("resource/roguelike/BlackFlow/recruitment.json")),
        "original recruitment restored after isolated regression");
    expect(
        token.operator_names == std::vector<std::string> { "结构性原理" },
        "only the known blocking token may take the temporary upper site");
    expect(in_group(Role::Drone, "结构性原理", "其他地面"), "the token really matches its configured fallback group");
    std::ofstream(out / "ranges.json") << json::value(range_receipts).to_string();
    std::ofstream(out / "receipt.json") << json::value(
                                               json::object { { "checks", checks },
                                                              { "failures", failures },
                                                              { "connections", 0 },
                                                              { "inputs", 0 },
                                                              { "live_clear_proven", false } })
                                               .to_string();
    std::cout << "SURVIVAL_LAYOUT checks=" << checks << " failures=" << failures
              << " actual_actor_range=true connections=0 inputs=0 live_clear_proven=false\n";
    return failures ? 1 : 0;
}
