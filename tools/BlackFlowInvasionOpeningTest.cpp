#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <vector>

class InvasionProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    InvasionProbe() :
        RoguelikeBattleTaskPlugin(
            {},
            nullptr,
            "InvasionOfflineProbe",
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
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    using namespace asst::battle;
    const std::filesystem::path input(argv[1]), plans(argv[2]);
    if (!BattleData.load(input / utils::path("resource/battle_data.json")) ||
        !RoguelikeRecruit.load(input / utils::path("resource/roguelike/BlackFlow/recruitment.json")) ||
        !RoguelikeCopilot.load(plans)) {
        return 3;
    }
    const auto stage = RoguelikeCopilot.get_stage_data("侵占家园");
    const auto tile = json::open(
        input / utils::path("resource/Arknights-Tile-Pos/ro6_n_4_7-obt-roguelike-ro6-level_rogue6_4-7.json"));
    if (!stage || !tile) {
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
    const auto& block = stage->deploy_plan.at("机械师").front();
    const auto& core = stage->deploy_plan.at("益达").front();
    const auto& left = stage->deploy_plan.at("地面阻挡").front();
    const auto& upper = stage->deploy_plan.at("术师").front();
    const auto& heal = stage->deploy_plan.at("元素奶").front();
    const auto unit = [](const char* n, Role r) {
        DeploymentOper o;
        o.name = n;
        o.role = r;
        return o;
    };
    auto mech = unit("机械师", Role::Tank), ew = unit("维什戴尔", Role::Sniper), rollo = unit("罗小黑", Role::Warrior),
         carda = unit("卡达", Role::Caster), pure = unit("纯烬艾雅法拉", Role::Medic);
    InvasionProbe probe;
    probe.elite(mech.name, 2);
    probe.elite(ew.name, 2);
    for (const auto& o : { rollo, carda, pure }) {
        probe.elite(o.name, 1);
    }
    const auto in = [&](const auto& o, const auto& p, Point t) {
        const auto r = probe.get_attack_range(o, p.direction);
        return std::ranges::find(r, t - p.location) != r.end();
    };
    expect(stage->deploy_plan_only && stage->reserve_deploy_cost, "dedicated duties and actual cost reservation");
    const std::vector<DeploymentOper> actors { mech, ew, rollo, carda, pure };
    const std::vector<int> costs { 21, 24, 17, 18, 16 };
    std::vector<InvasionProbe::Plan> sorted;
    int i = 0;
    for (const auto& p : { block, core, left, upper, heal }) {
        const auto& o = actors.at(i);
        expect(p.plan_index == i, "actual parser preserves duty order");
        expect(probe.deployment_requirements_met(o, p), "actual elite native qualification");
        expect(
            tile->at("tiles")[p.location.y][p.location.x].at("buildableType").as_integer() ==
                ((i == 0 || i == 2) ? 1 : 2),
            "actual tile deployment legal");
        expect(p.max_deployments > 1, "normal redeployment not exhausted after one use");
        for (const auto& t : p.required_attack_tiles) {
            expect(in(o, p, t), "every required attack tile covered");
        }
        for (const auto& t : p.required_range_tiles) {
            expect(in(o, p, t), "every mandatory healing tile covered");
        }
        InvasionProbe::Plan q;
        q.rank = p.rank;
        q.plan_index = p.plan_index;
        q.prefer_group_order = p.prefer_group_order;
        q.oper_name = o.name;
        q.cost = costs.at(i);
        sorted.push_back(q);
        ++i;
    }
    std::sort(sorted.begin(), sorted.end());
    for (int j = 0; j < 5; ++j) {
        expect(sorted.at(j).oper_name == actors.at(j).name, "native comparator protects main duties");
    }
    for (const auto& t : { Point(4, 4), Point(4, 5), Point(4, 6) }) {
        expect(!in(ew, core, t), "EW RIGHT cannot pretend direct cover of left blocker line");
    }
    expect(
        in(mech, block, Point(4, 4)) && in(mech, block, Point(4, 5)) && in(mech, block, Point(4, 6)),
        "actual Mech basic self and forward two cells");
    expect(!in(carda, upper, Point(4, 4)), "upper Carda duty does not also cover middle block");
    // Random object positions come from the original stage, not a copied map fixture.
    const auto raw = json::open(std::filesystem::path(argv[3]));
    if (!raw) {
        return 5;
    }
    const int height = static_cast<int>(raw->at("mapData").at("map").as_array().size());
    std::set<std::pair<int, int>> random_positions;
    for (const auto& token : raw->at("predefines").at("tokenInsts").as_array()) {
        const auto& id = token.at("inst").at("characterKey").as_string();
        if (id.starts_with("trap_320_") || id.starts_with("trap_321_") || id.starts_with("trap_322_") ||
            id.starts_with("trap_323_")) {
            random_positions.emplace(
                token.at("position").at("col").as_integer(),
                height - 1 - token.at("position").at("row").as_integer());
        }
    }
    expect(
        random_positions == std::set<std::pair<int, int>> { { 1, 2 }, { 5, 6 } },
        "actual primary random-object positions, including lower5,6");
    for (const auto& [x, y] : random_positions) {
        const Point tile(x, y);
        expect(!in(carda, upper, tile), "upper single-target basic footprint excludes actual random cells");
        expect(
            in(ew, core, tile) == (x == 5 && y == 6),
            "EW directly covers lower random object: plant safety remains unqualified");
    }
    expect(!probe.deployment_requirements_met(mech, left), "one Mech cannot fill both independent blocks");
    auto bearer = unit("桃金娘", Role::Pioneer);
    probe.elite(bearer.name, 1);
    expect(!probe.deployment_requirements_met(bearer, left), "Bearer excluded as permanent left blocker");
    auto wrong = upper;
    wrong.direction = DeployDirection::Down;
    expect(!probe.deployment_requirements_met(carda, wrong), "Carda wrong facing rejected");
    wrong = upper;
    wrong.required_attack_tiles.front() = Point(9, 6);
    expect(!probe.deployment_requirements_met(carda, wrong), "legal far target cannot pass range requirements");
    for (const auto& n : heal.operator_names) {
        auto o = unit(n.c_str(), Role::Medic);
        for (int e : { 1, 2 }) {
            probe.elite(n, e);
            expect(
                probe.deployment_requirements_met(o, heal),
                "each version-bound ordinary healer covers all four targets");
        }
    }
    auto touch = unit("Touch", Role::Medic);
    probe.elite(touch.name, 1);
    expect(
        !probe.deployment_requirements_met(touch, heal),
        "short conventional healer cannot falsely promise wide four-target coverage");
    auto alternative = heal;
    alternative.location = Point(3, 3);
    alternative.direction = DeployDirection::Right;
    expect(
        !probe.deployment_requirements_met(pure, alternative),
        "soul-external alternative misses left front blocker");
    expect(in(ew, core, heal.location), "chosen all-target healer lies in possible soul creation domain");
    mech.cost = 21;
    mech.available = false;
    mech.cooling = false;
    expect(InvasionProbe::deployment_cost_wait_required(mech, 10), "initial10 cannot deploy actual Mech21");
    auto typhon = unit("提丰", Role::Sniper);
    probe.elite(typhon.name, 1);
    auto disputed = upper;
    disputed.location = Point(5, 2);
    disputed.direction = DeployDirection::Up;
    expect(
        !in(typhon, disputed, Point(4, 1)) && !in(typhon, disputed, Point(5, 1)) && !in(typhon, disputed, Point(6, 1)),
        "Typhon 4-3 near blind row correctly rejected");
    std::cout << "INVASION_OPENING checks=" << checks << " failures=" << failures
              << " device_inputs=0 full_actor_selection=0 live_verified=0 block_survival_qualified=0 "
                 "skill_timing_qualified=0\n";
    return failures ? 1 : 0;
}
