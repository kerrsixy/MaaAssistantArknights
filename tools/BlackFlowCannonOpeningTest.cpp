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

class CannonProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    CannonProbe() :
        RoguelikeBattleTaskPlugin(
            {},
            nullptr,
            "CannonOfflineQualification",
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
    const std::filesystem::path input(argv[1]), folder(argv[2]);
    if (!BattleData.load(input / utils::path("resource/battle_data.json")) ||
        !RoguelikeRecruit.load(input / utils::path("resource/roguelike/BlackFlow/recruitment.json")) ||
        !RoguelikeCopilot.load(folder)) {
        return 3;
    }
    const auto data = RoguelikeCopilot.get_stage_data("光荣远征");
    const auto tile =
        json::open(input / utils::path("resource/Arknights-Tile-Pos/ro6_b_2-obt-roguelike-ro6-level_rogue6_b-2.json"));
    if (!data || !tile) {
        return 4;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* msg) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << msg << '\n';
        }
    };
    const auto& block = data->deploy_plan.at("机械师").front();
    const auto& clear = data->deploy_plan.at("术师").front();
    const auto& core = data->deploy_plan.at("益达").front();
    const auto& heal = data->deploy_plan.at("元素奶").front();
    const auto& push = data->deploy_plan.at("其他地面").front();
    const auto unit = [](const char* n, Role r) {
        DeploymentOper o;
        o.name = n;
        o.role = r;
        return o;
    };
    CannonProbe probe;
    auto mech = unit("机械师", Role::Tank), carda = unit("卡达", Role::Caster), ew = unit("维什戴尔", Role::Sniper),
         pod = unit("波登可", Role::Support), typhon = unit("提丰", Role::Sniper);
    for (const auto& o : { mech, carda, pod, typhon }) {
        probe.elite(o.name, 1);
    }
    probe.elite(ew.name, 2);
    const bool conditional_pod = std::ranges::find(heal.operator_names, pod.name) != heal.operator_names.end();
    auto med = conditional_pod ? pod : unit("Touch", Role::Medic);
    probe.elite(med.name, 1);
    const auto in = [&](const auto& o, const auto& p, Point t) {
        const auto r = probe.get_attack_range(o, p.direction);
        return std::ranges::find(r, t - p.location) != r.end();
    };
    expect(data->deploy_plan_only && data->reserve_deploy_cost, "dedicated duties and actual card budget");
    std::vector<CannonProbe::Plan> order;
    const std::vector<DeploymentOper> actors { mech, carda, ew, med };
    const std::vector<int> costs { 19, 18, 24, conditional_pod ? 12 : 0 };
    int i = 0;
    for (const auto& p : { block, clear, core, heal }) {
        expect(p.plan_index == i, "block cannon caster core then healing order");
        expect(probe.deployment_requirements_met(actors.at(i), p), "actual E1 or E2 requirements qualified");
        expect(
            tile->at("tiles")[p.location.y][p.location.x].at("buildableType").as_integer() == (i == 0 ? 1 : 2),
            "actual target terrain legal");
        expect(p.location.x < 6, "normal operators respect user right-side restriction");
        expect(p.max_deployments > 1, "ordinary redeployment remains legal");
        CannonProbe::Plan q;
        q.rank = p.rank;
        q.plan_index = p.plan_index;
        q.prefer_group_order = p.prefer_group_order;
        q.oper_name = actors.at(i).name;
        q.cost = costs.at(i);
        order.push_back(q);
        ++i;
    }
    std::sort(order.begin(), order.end());
    for (int j = 0; j < 4; ++j) {
        expect(order.at(j).oper_name == actors.at(j).name, "real comparator retains authored duty priority");
    }
    expect(in(carda, clear, Point(2, 5)), "Carda E1 directly reaches lower cannon");
    expect(in(carda, clear, Point(3, 5)), "lower taunting chef also competes for caster targeting");
    expect(!in(carda, clear, Point(2, 2)), "lower cannon duty cannot claim upper cannon damage");
    expect(in(ew, core, Point(7, 4)), "EW intercepts the lower delivered route after its first turn");
    expect(!in(ew, core, Point(2, 2)), "EW interception does not claim direct upper cannon damage");
    for (const auto& t : core.required_attack_tiles) {
        expect(in(ew, core, t), "all main-core attack requirements covered");
    }
    auto old = clear;
    old.location = Point(4, 3);
    old.direction = DeployDirection::Up;
    expect(
        !in(typhon, old, Point(2, 2)) && !in(typhon, old, Point(2, 5)),
        "old Typhon UP does not address either cannon");
    expect(
        !in(typhon, old, Point(7, 2)) && !in(typhon, old, Point(7, 5)),
        "old Typhon UP misses both cannon unloading positions");
    auto wrong = clear;
    wrong.direction = DeployDirection::Right;
    expect(!probe.deployment_requirements_met(carda, wrong), "wrong cannon direction rejected");
    wrong = clear;
    wrong.required_attack_tiles.front() = Point(9, 1);
    expect(!probe.deployment_requirements_met(carda, wrong), "legal far cell rejects false range qualification");
    auto steward = unit("史都华德", Role::Caster);
    probe.elite(steward.name, 1);
    expect(
        probe.deployment_requirements_met(steward, clear),
        "ordinary E1 single-target magic alternative range qualified");
    // Fixed regression examples, not a production operator allowlist.
    const std::vector<std::string> qualified_samples {
        "阿米娅",   "炎狱炎熔", "荒芜拉普兰德", "烛煌",   "远山",           "星源",          "夜烟",   "夜魔",
        "天火",     "艾雅法拉", "刻俄柏",       "夕",     "史都华德",       "莫斯提马",      "格雷伊", "和弦",
        "惊蛰",     "卡达",     "爱丽丝",       "寒檀",   "莱恩哈特",       "澄闪",          "布丁",   "耶拉",
        "霍尔海雅", "复奏",     "洛洛",         "黑键",   "至简",           "苦艾",          "温米",   "戴菲恩",
        "逻各斯",   "玛露西尔", "妮芙",         "特克诺", "Miss.Christine", "真言",          "维伊",   "时隙",
        "死芒",     "雪绒",     "深靛",         "异客",   "折光",           "预备干员-术师", "Pith"
    };
    expect(clear.operator_names.empty(), "caster duty has no frozen operator allowlist");
    for (const auto& n : qualified_samples) {
        auto caster = unit(n.c_str(), Role::Caster);
        for (int e : { 1, 2 }) {
            if (e == 2 && (n == "史都华德" || n == "预备干员-术师")) {
                continue;
            }
            probe.elite(n, e);
            expect(
                probe.deployment_requirements_met(caster, clear),
                "v2 ordinary base-magic caster E1/E2 cannon range qualified");
            std::cout << "CASTER_RANGE name=" << n << " elite=" << e
                      << " qualified=" << probe.deployment_requirements_met(caster, clear) << '\n';
        }
    }
    auto tomimi = unit("特米米", Role::Caster);
    probe.elite(tomimi.name, 1);
    expect(
        !probe.deployment_requirements_met(tomimi, clear),
        "physical conversion and short-range exception rejected by real requirements");
    expect(!conditional_pod, "ordinary plan cannot import a previous run's Pod S1 proof");
    for (const auto& n : heal.operator_names) {
        if (n == pod.name) {
            continue;
        }
        auto o = unit(n.c_str(), Role::Medic);
        for (int e : { 1, 2 }) {
            probe.elite(n, e);
            expect(
                probe.deployment_requirements_met(o, heal) ==
                    (n != "夜莺" && n != "白面鸮" && n != "调香师" && n != "微风" && n != "明椒" && n != "莎草"),
                "shorter base healing ranges cannot claim the three-cell forward core");
        }
    }
    expect(
        !probe.deployment_requirements_met(pod, heal),
        "Pod base range does not reach the repositioned EW and cannot bypass medical requirements");
    expect(!in(pod, heal, Point(5, 3)), "Pod condition cannot claim future pusher treatment");
    expect(in(pod, heal, clear.location), "reaching the nearby caster alone does not qualify healing the farther core");
    expect(!in(ew, core, heal.location), "medic lies outside the new right-facing Soul domain");
    expect(
        push.location == Point(5, 3) && push.direction == DeployDirection::Down,
        "user pusher site and direction preserved without inventing recruitment");
    expect(
        core.location == Point(4, 3) && core.direction == DeployDirection::Right,
        "intercept platform is left of the user right-side exclusion");
    for (const auto target : { Point(7, 2), Point(7, 3), Point(7, 4) }) {
        expect(in(ew, core, target), "EW directly covers both delivered approach turns");
        auto stale = core;
        stale.location = Point(4, 5);
        stale.direction = DeployDirection::Up;
        expect(!in(ew, stale, target), "run61 facing misses delivered approaches while all operators survive");
    }
    expect(!in(ew, core, Point(9, 0)), "approach interception is not full-route coverage");
    expect(
        in(ew, core, clear.location) && clear.rank < core.rank,
        "caster claims the shared Soul-domain site before EW");
    expect(!in(ew, core, heal.location), "healer cannot be displaced by EW Soul generation");
    auto pure = unit("纯烬艾雅法拉", Role::Medic);
    probe.elite(pure.name, 1);
    expect(probe.deployment_requirements_met(pure, heal), "actual run61 E1 healer covers main damage and blocker");
    expect(!in(ew, core, Point(2, 5)), "interception does not claim EW still focuses the lower cannon");
    mech.cost = 19;
    mech.available = false;
    mech.cooling = false;
    expect(CannonProbe::deployment_cost_wait_required(mech, 15), "actual initial15 still needs4 cost for Mech19");
    auto summon = unit("结构性原理", Role::Drone);
    probe.elite(summon.name, 1);
    expect(
        tile->at("tiles")[1][9].at("buildableType").as_integer() == 0,
        "right-door 9,1 is not deployable; no unsupported summon position added");
    std::cout << "CANNON_OPENING checks=" << checks << " failures=" << failures
              << " conditional_PodS1=" << conditional_pod
              << " device_inputs=0 full_actor_selection=0 live_verified=0 cannon_targetability_native_combat=0 "
                 "cannon_destroy_timing=0 damage_qualified=0\n";
    return failures ? 1 : 0;
}
