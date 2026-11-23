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

class CornProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    CornProbe() :
        RoguelikeBattleTaskPlugin({}, nullptr, "CornOfflineProbe", std::make_shared<asst::RoguelikeConfig>(), nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_cost_wait_required;
    using RoguelikeBattleTaskPlugin::deployment_requirements_met;
    using RoguelikeBattleTaskPlugin::get_attack_range;
    using Plan = DeployPlanInfo;

    void elite(const std::string& n, int e) { m_oper_elite[n] = e; }

    void forget(const std::string& n) { m_oper_elite.erase(n); }
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
    const auto data = RoguelikeCopilot.get_stage_data("木本玉米");
    const auto tile = json::open(
        fixture / utils::path("resource/Arknights-Tile-Pos/ro6_n_2_6-obt-roguelike-ro6-level_rogue6_2-6.json"));
    if (!data || !tile) {
        return 4;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool yes, const char* label) {
        ++checks;
        if (!yes) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const auto& first = data->deploy_plan.at("术师").front();
    const auto& lower = data->deploy_plan.at("机械师").front();
    const auto& core = data->deploy_plan.at("益达").front();
    const auto& medic = data->deploy_plan.at("群奶").front();
    expect(data->deploy_plan_only && data->reserve_deploy_cost, "dedicated output budget and no generic filler");
    expect(
        first.plan_index == 0 && lower.plan_index == 1 && core.plan_index == 2 && medic.plan_index == 3,
        "cheap upper output duty is first");
    expect(
        first.rank < lower.rank && lower.rank < core.rank && core.rank < medic.rank,
        "actual parser flattened ranks maintain duties");
    expect(
        medic.required_attack_tiles.empty() && medic.required_range_tiles.size() == 3,
        "medic uses mandatory healing range");
    CornProbe probe;
    const auto unit = [](const char* n, Role r) {
        DeploymentOper o;
        o.name = n;
        o.role = r;
        return o;
    };
    auto carda = unit("卡达", Role::Caster);
    auto mech = unit("机械师", Role::Tank);
    auto ew = unit("维什戴尔", Role::Sniper);
    auto pod = unit("波登可", Role::Support);
    probe.elite(carda.name, 1);
    probe.elite(mech.name, 1);
    probe.elite(ew.name, 2);
    probe.elite(pod.name, 1);
    expect(probe.deployment_requirements_met(carda, first), "actual E1 Carda can shoot upper gate approach");
    expect(probe.deployment_requirements_met(mech, lower), "actual E1 Mech remains lower blocker");
    expect(probe.deployment_requirements_met(ew, core), "actual E2 EW DOWN covers both later pressure areas");
    expect(probe.deployment_requirements_met(pod, medic), "conditional Pod S1 base geometry covers all three");
    const auto in = [&](const DeploymentOper& o, const auto& p, Point target) {
        const auto range = probe.get_attack_range(o, p.direction);
        return std::ranges::find(range, target - p.location) != range.end();
    };
    for (const auto& t : first.required_attack_tiles) {
        expect(in(carda, first, t), "each upper sheep target covered");
    }
    expect(
        !in(carda, first, Point(3, 2)) && !in(carda, first, Point(4, 5)),
        "basic single-target footprint avoids both random objects");
    expect(
        !in(mech, lower, Point(4, 2)) && !in(mech, lower, Point(5, 2)),
        "old lower-only blocker is not an upper-line guarantee");
    for (const auto& t : core.required_attack_tiles) {
        expect(in(ew, core, t), "DOWN core required targets stay in actual elite range");
    }
    for (const auto& t : medic.required_range_tiles) {
        expect(in(pod, medic, t), "each proposed healing recipient in actual range");
    }
    for (const auto& n : medic.operator_names) {
        if (n == pod.name) {
            continue;
        }
        DeploymentOper o;
        o.name = n;
        o.role = Role::Medic;
        probe.elite(n, 1);
        expect(
            probe.deployment_requirements_met(o, medic),
            "each ordinary Medic alternative really covers all recipients");
    }
    // These two outer filters live in do_best_deploy, not in the range helper.
    // Replaying the predicates is not execution of the whole actor.
    const auto outer_qualification = [&](const DeploymentOper& o, const auto& p, int observed_elite) {
        return observed_elite >= p.minimum_elite &&
               (p.operator_names.empty() || std::ranges::find(p.operator_names, o.name) != p.operator_names.end()) &&
               probe.deployment_requirements_met(o, p);
    };
    probe.elite(carda.name, 0);
    const bool carda_e0_geometry_only = probe.deployment_requirements_met(carda, first);
    expect(!outer_qualification(carda, first, 0), "Carda E0 rejected by actual outer minimum_elite contract");
    probe.elite(carda.name, 1);
    probe.elite(ew.name, 1);
    const bool ew_e1_geometry_only = probe.deployment_requirements_met(ew, core);
    expect(!outer_qualification(ew, core, 1), "EW E1 rejected by outer elite2 contract even if geometry matches");
    probe.elite(ew.name, 2);
    auto wrong_first = first;
    wrong_first.direction = DeployDirection::Down;
    expect(!probe.deployment_requirements_met(carda, wrong_first), "Carda DOWN cannot shoot upper approach");
    auto unreachable = first;
    unreachable.required_attack_tiles.front() = Point(0, 6);
    expect(
        !probe.deployment_requirements_met(carda, unreachable),
        "legal map cell outside real range rejects qualification");
    auto wrong_medic = medic;
    wrong_medic.direction = medic.direction == DeployDirection::Up ? DeployDirection::Down : DeployDirection::Right;
    auto touch = unit("Touch", Role::Medic);
    probe.elite(touch.name, 1);
    expect(probe.deployment_requirements_met(touch, medic), "ordinary medic original facing qualifies");
    expect(
        !probe.deployment_requirements_met(touch, wrong_medic),
        "ordinary medic wrong facing fails a required recipient");
    auto manticore = unit("狮蝎", Role::Special);
    auto leizi = unit("司霆惊蛰", Role::Warrior);
    auto myrtle = unit("桃金娘", Role::Pioneer);
    for (const auto& o : { manticore, leizi, myrtle }) {
        probe.elite(o.name, 1);
        expect(
            !probe.deployment_requirements_met(o, lower),
            "Ambusher Liberator or Bearer cannot replace permanent blocker");
    }
    auto holy = unit("圣聆初雪", Role::Caster);
    probe.elite(holy.name, 1);
    expect(!outer_qualification(holy, first, 1), "phalanx identity cannot imitate approved ordinary opening shot");
    probe.forget(carda.name);
    expect(!probe.deployment_requirements_met(carda, first), "unknown elite refuses mandatory range qualification");
    probe.elite(carda.name, 1);
    for (const auto& p : { first, lower, core, medic }) {
        expect(
            tile->at("tiles")[p.location.y][p.location.x].at("buildableType").as_integer() ==
                (p.plan_index == 1 ? 1 : 2),
            "all candidate tile types legal");
        expect(p.max_deployments > 1, "ordinary redeployment not locked after one attempt");
    }
    std::vector<CornProbe::Plan> order;
    for (const auto& p : { first, lower, core, medic }) {
        CornProbe::Plan q;
        q.rank = p.rank;
        q.plan_index = p.plan_index;
        q.prefer_group_order = p.prefer_group_order;
        q.oper_name = p.plan_index == 0   ? carda.name
                      : p.plan_index == 1 ? mech.name
                      : p.plan_index == 2 ? ew.name
                                          : pod.name;
        q.cost = p.plan_index == 0 ? 18 : p.plan_index == 1 ? 19 : p.plan_index == 2 ? 24 : 12;
        order.push_back(q);
    }
    std::sort(order.begin(), order.end());
    expect(
        order.front().oper_name == carda.name && order.front().cost <= 20,
        "native production comparator selects affordable Carda18 before Mech19 and Pod12 at DP20");
    order.erase(order.begin());
    expect(order.front().oper_name == mech.name, "then preserve lower duty");
    order.erase(order.begin());
    expect(order.front().oper_name == ew.name, "core rank precedes cheaper healing replacement");
    ew.cost = 24;
    ew.cooling = false;
    ew.available = false;
    expect(
        CornProbe::deployment_cost_wait_required(ew, 20),
        "real production predicate waits known unaffordable core card");
    // No do_best_deploy or controller input is called. This tests the real range,
    // parser, comparator and budget predicate, not a full actor deployment.
    std::cout << "CORN_OPENING checks=" << checks << " failures=" << failures
              << " Carda_E0_geometry_only=" << carda_e0_geometry_only << " EW_E1_geometry_only=" << ew_e1_geometry_only
              << " device_inputs=0 full_actor_selection=0 live_verified=0 plant_safety_qualified=0 "
                 "skill_timing_qualified=0\n";
    return failures ? 1 : 0;
}
