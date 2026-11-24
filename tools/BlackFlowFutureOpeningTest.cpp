#include <algorithm>
#include <filesystem>
#include <iostream>

#include "Arknights-Tile-Pos/TileDef.hpp"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"

class FutureOpeningProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    FutureOpeningProbe() :
        RoguelikeBattleTaskPlugin({}, nullptr, "FutureOpeningProbe", std::make_shared<asst::RoguelikeConfig>(), nullptr)
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
    const auto data = RoguelikeCopilot.get_stage_data("未来见闻");
    const auto raw = json::open(
        repo / utils::path(
                   "resource/Arknights-Tile-Pos/"
                   "ro6_n_4_1-obt-roguelike-ro6-level_rogue6_4-1.json"));
    if (!data || !raw) {
        return 4;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const auto& mech = data->deploy_plan.at("机械师").front();
    const auto& carda = data->deploy_plan.at("术师").front();
    const auto& medic = data->deploy_plan.at("元素奶").front();
    const auto& core = data->deploy_plan.at("益达").front();
    expect(
        data->deploy_plan_only && data->reserve_deploy_cost,
        "generic role substitution cannot spend the opening budget");
    expect(
        mech.rank < carda.rank && carda.rank < medic.rank && medic.rank < core.rank,
        "actual air damage precedes healing and ground core");
    expect(
        mech.location == Point(9, 3) && mech.direction == DeployDirection::Left,
        "main blocker faces the final approach");
    expect(
        carda.location == Point(9, 4) && carda.direction == DeployDirection::Up,
        "caster covers the blue-gate air approach");
    const Map::Level level(*raw);
    FutureOpeningProbe probe;
    const auto groups = RoguelikeRecruit.get_group_names("BlackFlow");
    for (const auto& [role, name, elite, plan] :
         std::vector<std::tuple<Role, std::string, int, roguelike::DeployInfoWithRank>> {
             { Role::Tank, "机械师", 1, mech },
             { Role::Caster, "卡达", 1, carda },
             { Role::Medic, "纯烬艾雅法拉", 1, medic },
             { Role::Sniper, "维什戴尔", 2, core } }) {
        DeploymentOper oper;
        oper.role = role;
        oper.name = name;
        probe.elite(name, elite);
        const auto tile = level.get_item(plan.location.y, plan.location.x);
        expect(tile.buildableType != 0, "operator site is not a hole or forbidden tile");
        const auto ids = RoguelikeRecruit.get_group_ids_of_oper("BlackFlow", OperNameTag { role, name });
        expect(
            std::ranges::any_of(
                ids,
                [&](int id) {
                    const auto it = data->deploy_plan.find(groups.at(id));
                    return it != data->deploy_plan.end() && std::ranges::any_of(it->second, [&](const auto& entry) {
                               return entry.plan_index == plan.plan_index;
                           });
                }),
            "actual roster membership reaches intended deployment plan");
        expect(
            (role == Role::Medic && plan.operator_names.empty()) ||
                std::ranges::find(plan.operator_names, name) != plan.operator_names.end(),
            "plan keeps real identity qualified");
        expect(
            probe.deployment_requirements_met(oper, plan),
            "actual observed elite satisfies attack coverage requirements");
        if (role == Role::Medic) {
            const auto range = probe.get_attack_range(oper, plan.direction);
            expect(
                std::ranges::all_of(
                    std::vector<Point> { mech.location, carda.location, core.location },
                    [&](const Point& p) { return std::ranges::find(range, p - plan.location) != range.end(); }),
                "actual healer reaches both air defense operators and EW");
        }
    }
    expect(
        mech.minimum_elite == 1 && mech.require_persistent_blocking,
        "E1 eligibility retains actual persistent blocking");
    DeploymentOper mechanical { .role = Role::Tank, .name = "机械师" };
    probe.elite(mechanical.name, 0);
    expect(!probe.deployment_requirements_met(mechanical, mech), "E0 cannot borrow E1 qualification");
    for (const auto& name : { "蜜莓", "纯烬艾雅法拉" }) {
        DeploymentOper healer { .role = Role::Medic, .name = name };
        probe.elite(healer.name, 1);
        expect(
            probe.deployment_requirements_met(healer, medic),
            "actual E1 wandering healer covers all three current duties");
        auto wrong = medic;
        wrong.direction = DeployDirection::Up;
        expect(
            !probe.deployment_requirements_met(healer, wrong),
            "wrong heading cannot qualify by a broad group label");
    }
    std::cout << "FUTURE_OPENING checks=" << checks << " failures=" << failures << " device_inputs=0\n";
    return failures ? 1 : 0;
}
