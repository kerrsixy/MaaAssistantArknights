#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"

class Ending1Probe : public asst::RoguelikeBattleTaskPlugin
{
public:
    Ending1Probe() :
        RoguelikeBattleTaskPlugin(
            {},
            nullptr,
            "Ending1OfflineProbe",
            std::make_shared<asst::RoguelikeConfig>(),
            nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_cost_wait_required;
    using RoguelikeBattleTaskPlugin::deployment_requirements_met;
    using RoguelikeBattleTaskPlugin::get_attack_range;
    using RoguelikeBattleTaskPlugin::preferred_deployment_coverage;
    using Plan = DeployPlanInfo;

    void core(const std::string& name) { m_config->set_start_opers({ { name, false } }); }

    void elite(const std::string& name, int value) { m_oper_elite[name] = value; }

    void forget(const std::string& name) { m_oper_elite.erase(name); }
};

// Arguments: frozen fixture root (contains resource/) and ONE candidate plan
// directory. No task, controller, image capture, Asst instance or device exists.
int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        return 2;
    }
    using namespace asst;
    using namespace asst::battle;
    const std::filesystem::path fixture(argv[1]), plans(argv[2]);
    if (!BattleData.load(fixture / utils::path("resource/battle_data.json")) ||
        !RoguelikeRecruit.load(fixture / utils::path("resource/roguelike/BlackFlow/recruitment.json")) ||
        !RoguelikeCopilot.load(plans)) {
        return 3;
    }
    const auto stage = RoguelikeCopilot.get_stage_data("永无安宁");
    const auto terrain = json::open(
        fixture / utils::path("resource/Arknights-Tile-Pos/ro6_b_4-obt-roguelike-ro6-level_rogue6_b-4.json"));
    if (!stage || !terrain || !stage->deploy_plan.contains("元素奶")) {
        return 4; // This probe's contract is the continuous-medic UP candidate.
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool pass, const char* label) {
        ++checks;
        if (!pass) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const auto& hunter = stage->deploy_plan.at("术师").front();
    const auto& blocker = stage->deploy_plan.at("机械师").front();
    const auto& medic = stage->deploy_plan.at("元素奶").front();
    const auto& core = stage->deploy_plan.at("益达").front();
    const auto& middle = stage->deploy_plan.at("提丰").front();
    expect(stage->deploy_plan_only && stage->reserve_deploy_cost, "plan-only and cost reservation parsed");
    expect(
        hunter.plan_index == 0 && blocker.plan_index == 1 && medic.plan_index == 2 && core.plan_index == 3 &&
            middle.plan_index == 4,
        "five responsibilities retain order after actual parser flattening");
    expect(
        hunter.rank < blocker.rank && blocker.rank < medic.rank && medic.rank < core.rank && core.rank < middle.rank,
        "all earlier group aliases still precede following duties");
    expect(
        medic.required_attack_tiles.empty() && medic.required_range_tiles.size() == 5,
        "medical contract uses required_range_tiles");
    expect(blocker.require_persistent_blocking, "ordinary blocker capability requested");
    expect(
        core.minimum_elite == 2 && core.direction == DeployDirection::Up,
        "core elite and new facing survive parser");
    for (const auto& plan : { hunter, blocker, medic, core, middle }) {
        const int type = plan.plan_index == 1 ? 1 : 2;
        expect(
            terrain->at("tiles").at(plan.location.y).at(plan.location.x).at("buildableType").as_integer() == type,
            "each authored duty has legal actual tile type");
        expect(plan.max_deployments > 1, "ordinary operators may redeploy");
    }

    Ending1Probe probe;
    const auto oper = [](const char* name, Role role) {
        DeploymentOper result;
        result.name = name;
        result.role = role;
        return result;
    };
    const auto ew = oper("维什戴尔", Role::Sniper);
    const auto mech = oper("机械师", Role::Tank);
    const auto carda = oper("卡达", Role::Caster);
    const auto goat = oper("纯烬艾雅法拉", Role::Medic);
    const auto typhon = oper("提丰", Role::Sniper);
    const auto pod = oper("波登可", Role::Support);
    const auto leizi = oper("司霆惊蛰", Role::Warrior);
    probe.elite(ew.name, 2);
    probe.elite(mech.name, 2);
    probe.elite(carda.name, 1);
    probe.elite(goat.name, 1);
    probe.elite(typhon.name, 1);
    probe.elite(pod.name, 1);
    probe.elite(leizi.name, 2);
    expect(probe.deployment_requirements_met(carda, hunter), "actual E1 Carda reaches hunter trigger tiles");
    expect(probe.deployment_requirements_met(mech, blocker), "actual E2 Mech is persistent basic blocker");
    expect(probe.deployment_requirements_met(goat, medic), "actual E1 pure Eyjafjalla covers all medical targets");
    expect(probe.deployment_requirements_met(ew, core), "actual E2 core covers gate approaches");
    expect(probe.deployment_requirements_met(typhon, middle), "actual E1 Typhon covers middle lower route targets");
    const auto has = [&](const DeploymentOper& unit, const auto& plan, Point target) {
        const auto range = probe.get_attack_range(unit, plan.direction);
        return std::ranges::find(range, target - plan.location) != range.end();
    };
    for (const auto& target : medic.required_range_tiles) {
        expect(has(goat, medic, target), "native medical footprint reaches each separately named recipient");
    }
    for (const auto& target : core.required_attack_tiles) {
        expect(has(ew, core, target), "native EW footprint reaches each gate approach");
    }
    expect(!has(ew, core, Point(6, 5)) && !has(ew, core, Point(9, 4)), "core UP tradeoff remains visible");
    expect(
        has(typhon, middle, Point(6, 5)) && has(typhon, middle, Point(9, 4)),
        "middle output compensates specified base-range gap");
    expect(!has(typhon, middle, Point(7, 1)) && !has(typhon, middle, Point(9, 3)), "do not fill Typhon blind corners");
    auto wrong = core;
    wrong.direction = DeployDirection::Down;
    expect(!probe.deployment_requirements_met(ew, wrong), "old DOWN direction cannot meet gate UP contract");
    wrong = medic;
    wrong.location = Point(7, 2);
    wrong.direction = DeployDirection::Up;
    expect(!probe.deployment_requirements_met(goat, wrong), "old medical tile misses full recipient set");
    wrong = medic;
    wrong.required_attack_tiles = { Point(7, 1) };
    expect(!probe.deployment_requirements_met(goat, wrong), "ordinary Medic is not damage-qualified");
    probe.forget(goat.name);
    expect(!probe.deployment_requirements_met(goat, medic), "unknown elite refuses mandatory range qualification");
    probe.elite(goat.name, 1);
    expect(!probe.deployment_requirements_met(pod, medic), "Pod does not satisfy full continuous medic footprint");
    expect(!probe.deployment_requirements_met(leizi, blocker), "Liberator is not persistent main blocker");
    for (const auto& name : medic.operator_names) {
        const auto props = BattleData.find_first_oper(Role::Medic, name);
        expect(
            props && props->role == Role::Medic && props->sub_role != SubRole::Medic_IncantationMedic,
            "generated medical whitelist remains actual ordinary Medic form");
        auto unit = oper(name.c_str(), Role::Medic);
        probe.elite(name, 1);
        expect(
            probe.deployment_requirements_met(unit, medic),
            "each generated medical candidate passes native range gate");
    }
    expect(
        std::ranges::find(medic.operator_names, goat.name) != medic.operator_names.end(),
        "freshly recruited E1 healer is in the capability-generated whitelist");
    expect(
        std::ranges::find(medic.operator_names, pod.name) == medic.operator_names.end(),
        "conditional Pod is not mislabeled continuous healing");
    const auto& fallback = stage->deploy_plan.at("狙击").back();
    expect(
        fallback.plan_index == 5 && middle.rank < fallback.rank,
        "complete middle duty remains earlier than the partial fallback");
    expect(
        fallback.location == middle.location && fallback.direction == DeployDirection::Down,
        "fallback shares the existing middle tile and facing");
    expect(
        fallback.operator_names.empty() && fallback.exclude_starting_core && fallback.minimum_elite == 1,
        "fallback uses real capabilities rather than a fixed operator name");
    expect(fallback.prefer_group_order, "qualified sniper group order precedes cheap filler");
    expect(
        fallback.required_attack_tiles.size() == 3 && fallback.preferred_attack_tiles.size() == 1,
        "three required tiles do not silently become four required tiles");
    expect(terrain->at("tiles")[2][7].at("buildableType").as_integer() == 2, "middle fallback is real ranged terrain");
    const auto snow = oper("白雪", Role::Sniper);
    probe.elite(snow.name, 1);
    expect(!probe.deployment_requirements_met(snow, middle), "actual E1 Shirayuki still fails complete old duty");
    expect(probe.deployment_requirements_met(snow, fallback), "actual E1 Shirayuki reaches the partial lower duty");
    expect(!has(snow, fallback, Point(9, 4)), "Shirayuki is not credited with the missing fourth target");
    expect(
        probe.preferred_deployment_coverage(snow, fallback) == 0,
        "uncovered preferred tile contributes no coverage score");
    expect(
        probe.deployment_requirements_met(typhon, fallback) &&
            probe.preferred_deployment_coverage(typhon, fallback) == 1,
        "a real complete-range sniper retains the preferred tile advantage");
    expect(has(goat, medic, fallback.location), "existing actual healer covers the fallback position");
    const auto groups = RoguelikeRecruit.get_group_names("BlackFlow");
    const auto reaches_fallback = [&](const DeploymentOper& unit) {
        for (int id : RoguelikeRecruit.get_group_ids_of_oper("BlackFlow", OperNameTag { unit.role, unit.name })) {
            const auto found = stage->deploy_plan.find(groups.at(id));
            if (found != stage->deploy_plan.end() && std::ranges::any_of(found->second, [&](const auto& item) {
                    return item.plan_index == fallback.plan_index && probe.deployment_requirements_met(unit, item);
                })) {
                return true;
            }
        }
        return false;
    };
    expect(reaches_fallback(snow), "actual sniper membership reaches the parsed fallback");
    const auto angel = oper("新约能天使", Role::Special);
    probe.elite(angel.name, 2);
    expect(probe.deployment_requirements_met(angel, fallback), "geek has the same partial geometric reach");
    expect(!reaches_fallback(angel), "geometric reach alone does not admit the S2 attack-speed stealing geek");
    expect(
        !reaches_fallback(pod) && !reaches_fallback(goat),
        "support and healing labels cannot replace the sniper duty");
    probe.core(ew.name);
    expect(!probe.deployment_requirements_met(ew, fallback), "starting EW remains reserved for its authored core tile");
    probe.forget(snow.name);
    expect(!probe.deployment_requirements_met(snow, fallback), "unknown elite cannot borrow fallback range");
    probe.elite(snow.name, 0);
    expect(!probe.deployment_requirements_met(snow, fallback), "E0 sniper cannot borrow E1 eligibility");
    probe.elite(snow.name, 1);
    auto backwards = fallback;
    backwards.direction = DeployDirection::Up;
    expect(!probe.deployment_requirements_met(snow, backwards), "wrong facing cannot meet lower-route duty");
    Ending1Probe::Plan exact_order, fallback_order;
    exact_order.rank = middle.rank;
    exact_order.plan_index = middle.plan_index;
    exact_order.cost = 30;
    fallback_order.rank = fallback.rank;
    fallback_order.plan_index = fallback.plan_index;
    fallback_order.cost = 23;
    expect(
        exact_order < fallback_order && !(fallback_order < exact_order),
        "actual ordering retains the complete duty before cheaper fallback");
    auto pending = snow;
    pending.cost = 23;
    pending.available = false;
    pending.cooling = false;
    expect(
        Ending1Probe::deployment_cost_wait_required(pending, 20),
        "known insufficient cost waits for the selected sniper");
    expect(!Ending1Probe::deployment_cost_wait_required(pending, 23), "sufficient observed cost releases the wait");
    pending.cooling = true;
    expect(!Ending1Probe::deployment_cost_wait_required(pending, 20), "cooling card is not a funding wait");
    // These native eligibility, ordering and cost predicates do not execute
    // Controller selection or certify block4, damage, survival or victory.
    std::cout << "ENDING1_CANDIDATE checks=" << checks << " failures=" << failures
              << " device_inputs=0 native_runtime_geometry=1 live_verified=0 block4_qualified=0\n";
    return failures ? 1 : 0;
}
