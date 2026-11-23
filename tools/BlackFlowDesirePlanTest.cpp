#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"

class DesireProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    DesireProbe() :
        RoguelikeBattleTaskPlugin({}, nullptr, "DesireOfflineProbe", std::make_shared<asst::RoguelikeConfig>(), nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_requirements_met;
    using RoguelikeBattleTaskPlugin::get_attack_range;

    void elite(const std::string& name, int value) { m_oper_elite[name] = value; }

    void forget(const std::string& name) { m_oper_elite.erase(name); }
};

// Args: frozen inputs root, single plans directory. Never creates a task or device.
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
    const auto data = RoguelikeCopilot.get_stage_data("物欲遮天");
    const auto terrain = json::open(
        fixture / utils::path("resource/Arknights-Tile-Pos/ro6_n_5_2-obt-roguelike-ro6-level_rogue6_5-2.json"));
    if (!data || !terrain) {
        return 4;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool pass, const char* label) {
        ++checks;
        if (!pass) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const auto& air = data->deploy_plan.at("提丰").front();
    const auto& ew_plan = data->deploy_plan.at("益达").front();
    const auto& med = data->deploy_plan.at("元素奶").front();
    const auto& lower = data->deploy_plan.at("机械师").front();
    const auto& caster = data->deploy_plan.at("术师").front();
    const auto& upper = data->deploy_plan.at("地面阻挡").front();
    expect(data->deploy_plan_only && data->reserve_deploy_cost, "no generic filler budget");
    expect(
        air.plan_index == 0 && ew_plan.plan_index == 1 && med.plan_index == 2 && lower.plan_index == 3 &&
            caster.plan_index == 4 && upper.plan_index == 5,
        "actual parser preserves first air duty");
    expect(
        air.rank < ew_plan.rank && ew_plan.rank < med.rank && med.rank < lower.rank,
        "first air then core then healing and lower ground defense");
    expect(
        air.location == Point(3, 4) && air.direction == DeployDirection::Left,
        "new first-air location and facing parsed");
    expect(
        med.required_attack_tiles.empty() && med.required_range_tiles.size() == 5,
        "medical ability uses range predicate");
    expect(
        lower.require_persistent_blocking && upper.require_persistent_blocking,
        "both ground duties reject skill-only blockers");
    for (const auto& plan : { air, ew_plan, med, lower, caster, upper }) {
        const int type = plan.plan_index == 3 || plan.plan_index == 5 ? 1 : 2;
        expect(
            terrain->at("tiles")[plan.location.y][plan.location.x].at("buildableType").as_integer() == type,
            "authored position matches native terrain");
        expect(plan.max_deployments > 1, "ordinary operator can redeploy");
    }
    DesireProbe probe;
    const auto unit = [](const char* name, Role role) {
        DeploymentOper op;
        op.name = name;
        op.role = role;
        return op;
    };
    const auto typhon = unit("提丰", Role::Sniper);
    const auto ew = unit("维什戴尔", Role::Sniper);
    const auto goat = unit("纯烬艾雅法拉", Role::Medic);
    const auto mech = unit("机械师", Role::Tank);
    const auto carda = unit("卡达", Role::Caster);
    const auto frost = unit("凛御银灰", Role::Pioneer);
    const auto leizi = unit("司霆惊蛰", Role::Warrior);
    for (const auto& op : { typhon, goat, carda, frost }) {
        probe.elite(op.name, 1);
    }
    for (const auto& op : { ew, mech, leizi }) {
        probe.elite(op.name, 2);
    }
    expect(probe.deployment_requirements_met(typhon, air), "E1 Typhon covers both air exits");
    expect(probe.deployment_requirements_met(ew, ew_plan), "EW original RIGHT role remains geometrically valid");
    expect(probe.deployment_requirements_met(goat, med), "actual E1 wide healer covers all five recipients");
    expect(probe.deployment_requirements_met(mech, lower), "Mech can occupy lower permanent blocking duty");
    expect(probe.deployment_requirements_met(carda, caster), "Carda reaches upper approach and middle");
    expect(probe.deployment_requirements_met(frost, upper), "future qualified Frost can fill independent upper duty");
    expect(!probe.deployment_requirements_met(mech, upper), "same Mech cannot be copied to second duty");
    auto independent_upper = upper;
    independent_upper.excluded_operator_names.clear();
    expect(!probe.deployment_requirements_met(leizi, independent_upper), "Liberator still not persistent blocker");
    const auto in = [&](const DeploymentOper& op, const auto& plan, Point target) {
        const auto footprint = probe.get_attack_range(op, plan.direction);
        return std::ranges::find(footprint, target - plan.location) != footprint.end();
    };
    for (const auto& target : air.required_attack_tiles) {
        expect(in(typhon, air, target), "real rotated range reaches each air gate or approach");
    }
    auto old_air = air;
    old_air.location = Point(4, 5);
    old_air.direction = DeployDirection::Up;
    expect(!probe.deployment_requirements_met(typhon, old_air), "old late Typhon position fails both-gate contract");
    for (const auto& target : air.required_attack_tiles) {
        expect(!in(typhon, old_air, target), "old range omits each of four critical exit cells");
    }
    for (const auto& target : med.required_range_tiles) {
        expect(in(goat, med, target), "healing footprint reaches each independent front-line position");
    }
    auto wrong_med = med;
    wrong_med.required_attack_tiles = { Point(1, 2) };
    expect(!probe.deployment_requirements_met(goat, wrong_med), "ordinary healing cannot be treated as attack damage");
    probe.forget(goat.name);
    expect(!probe.deployment_requirements_met(goat, med), "unknown elite refuses mandatory range qualification");
    // Geometry cannot certify air-target engine flags, actual skill, DPS,
    // invisibility/barrier eligibility, first-enemy timing or battle victory.
    std::cout << "DESIRE_CANDIDATE checks=" << checks << " failures=" << failures
              << " device_inputs=0 live_verified=0 air_engine_filter_qualified=0 upper_roster_gap_retained=1\n";
    return failures ? 1 : 0;
}
