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

class RequiemProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    RequiemProbe() :
        RoguelikeBattleTaskPlugin(
            {},
            nullptr,
            "RequiemOfflineProbe",
            std::make_shared<asst::RoguelikeConfig>(),
            nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_cost_wait_required;
    using RoguelikeBattleTaskPlugin::deployment_requirements_met;
    using RoguelikeBattleTaskPlugin::get_attack_range;
    using Plan = DeployPlanInfo;

    void elite(const std::string& n, int e) { m_oper_elite[n] = e; }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        return 2;
    }
    using namespace asst;
    using namespace asst::battle;
    const std::filesystem::path fixture(argv[1]), folder(argv[2]);
    if (!BattleData.load(fixture / utils::path("resource/battle_data.json")) ||
        !RoguelikeRecruit.load(fixture / utils::path("resource/roguelike/BlackFlow/recruitment.json")) ||
        !RoguelikeCopilot.load(folder)) {
        return 3;
    }
    const auto data = RoguelikeCopilot.get_stage_data("安魂节起义");
    const auto tile = json::open(
        fixture / utils::path("resource/Arknights-Tile-Pos/ro6_n_4_9-obt-roguelike-ro6-level_rogue6_4-9.json"));
    if (!data || !tile) {
        return 4;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* why) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << why << '\n';
        }
    };
    const auto& block = data->deploy_plan.at("机械师").front();
    const auto& core = data->deploy_plan.at("益达").front();
    const auto& upper = data->deploy_plan.at("术师").front();
    const auto& heal = data->deploy_plan.at("元素奶").front();
    const auto& temporary = data->deploy_plan.at("群奶").back();
    RequiemProbe probe;
    const auto unit = [](const char* n, Role r) {
        DeploymentOper o;
        o.name = n;
        o.role = r;
        return o;
    };
    auto mech = unit("机械师", Role::Tank), ew = unit("维什戴尔", Role::Sniper), carda = unit("卡达", Role::Caster),
         pure = unit("纯烬艾雅法拉", Role::Medic), pod = unit("波登可", Role::Support);
    for (const auto& o : { mech, carda, pure, pod }) {
        probe.elite(o.name, 1);
    }
    probe.elite(ew.name, 2);
    probe.elite(mech.name, 2);
    const auto in = [&](const DeploymentOper& o, const auto& p, Point t) {
        const auto r = probe.get_attack_range(o, p.direction);
        return std::ranges::find(r, t - p.location) != r.end();
    };
    expect(data->deploy_plan_only && data->reserve_deploy_cost, "exclusive duties with cost reservation");
    std::vector<RequiemProbe::Plan> order;
    auto lower_healer = pod;
    const bool pod_conditional_fixture =
        std::ranges::find(temporary.operator_names, pod.name) != temporary.operator_names.end();
    if (!pod_conditional_fixture) {
        lower_healer = unit("Touch", Role::Medic);
        probe.elite(lower_healer.name, 1);
    }
    const std::vector<DeploymentOper> actors { mech, ew, carda, pure, lower_healer };
    const std::vector<int> costs { 21, 24, 18, 16, pod_conditional_fixture ? 12 : 0 };
    int index = 0;
    for (const auto& p : { block, core, upper, heal, temporary }) {
        const auto& o = actors.at(index);
        expect(p.plan_index == index, "parser preserves explicit duty order");
        expect(probe.deployment_requirements_met(o, p), "actual elite base-range qualification");
        expect(
            tile->at("tiles")[p.location.y][p.location.x].at("buildableType").as_integer() == (index == 0 ? 1 : 2),
            "actual deployment tile legal");
        expect(p.max_deployments > 1, "ordinary redeployment remains possible");
        for (const auto& target : p.required_attack_tiles) {
            expect(in(o, p, target), "each actual attack target covered");
        }
        for (const auto& target : p.required_range_tiles) {
            expect(in(o, p, target), "each actual healing target covered");
        }
        RequiemProbe::Plan q;
        q.rank = p.rank;
        q.plan_index = p.plan_index;
        q.prefer_group_order = p.prefer_group_order;
        q.oper_name = o.name;
        q.cost = costs.at(index);
        order.push_back(q);
        ++index;
    }
    std::sort(order.begin(), order.end());
    for (int i = 0; i < 5; ++i) {
        expect(order.at(i).oper_name == actors.at(i).name, "native comparator prioritizes duties over cheap filler");
    }
    expect(!in(pure, heal, Point(6, 6)), "upper healer does not falsely promise lower core healing");
    expect(!in(pure, data->deploy_plan.at("元素奶").front(), Point(9, 7)), "upper healing is not global");
    auto old_heal = heal;
    old_heal.location = Point(7, 4);
    old_heal.direction = DeployDirection::Left;
    expect(!in(pure, old_heal, Point(5, 1)), "actual old medic footprint misses upper blocker");
    expect(in(pure, old_heal, Point(6, 6)), "old medic footprint does cover lower EW");
    auto wrong = upper;
    wrong.direction = DeployDirection::Down;
    expect(!probe.deployment_requirements_met(carda, wrong), "wrong Carda facing rejected");
    wrong = upper;
    wrong.required_attack_tiles.front() = Point(9, 7);
    expect(!probe.deployment_requirements_met(carda, wrong), "legal far cell required target rejected");
    auto nonblock = unit("桃金娘", Role::Pioneer);
    probe.elite(nonblock.name, 1);
    expect(!probe.deployment_requirements_met(nonblock, block), "Bearer cannot assume permanent portal block");
    mech.cost = 21;
    mech.available = false;
    mech.cooling = false;
    expect(RequiemProbe::deployment_cost_wait_required(mech, 10), "initial10 must wait for actual Mech21");
    expect(
        block.rank < core.rank && core.rank < upper.rank && upper.rank < heal.rank,
        "upper block and upper damage before optional healing");
    expect(
        heal.required_attack_tiles.empty() && temporary.required_attack_tiles.empty(),
        "healing uses range contract");
    for (const auto& p : { heal, temporary }) {
        for (const auto& n : p.operator_names) {
            if (n == pod.name) {
                continue;
            }
            auto o = unit(n.c_str(), Role::Medic);
            for (int e : { 1, 2 }) {
                probe.elite(n, e);
                expect(probe.deployment_requirements_met(o, p), "versioned ordinary Medic E1/E2 required range");
            }
        }
    }
    std::cout << "pod_S1_conditional_fixture=" << pod_conditional_fixture << "\n";
    std::cout
        << "REQUIEM_OPENING checks=" << checks << " failures=" << failures
        << " device_inputs=0 full_actor_selection=0 live_verified=0 survival_qualified=0 pod_continuous_healing=0\n";
    return failures ? 1 : 0;
}
