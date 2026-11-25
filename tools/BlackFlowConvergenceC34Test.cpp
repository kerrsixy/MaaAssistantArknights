#include "Arknights-Tile-Pos/TileCalc2.hpp"
#include "AsstCaller.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "MaaUtils/Encoding.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>

class C34Probe : public asst::RoguelikeBattleTaskPlugin
{
public:
    C34Probe(const std::shared_ptr<asst::RoguelikeConfig>& cfg) :
        RoguelikeBattleTaskPlugin({}, nullptr, "C34Offline", cfg, nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_cost_wait_required;
    using RoguelikeBattleTaskPlugin::deployment_plan_exhausted;
    using RoguelikeBattleTaskPlugin::deployment_requirements_met;
    using RoguelikeBattleTaskPlugin::DeployPlanInfo;
    using RoguelikeBattleTaskPlugin::get_attack_range;
    using RoguelikeBattleTaskPlugin::starting_core_reserves_site;

    void elite(const std::string& name, int phase) { m_oper_elite[name] = phase; }

    void plans(const asst::battle::roguelike::CombatData& data) { m_deploy_plan = data.deploy_plan; }

    void consumed(int index) { ++m_plan_deployment_counts[index]; }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 5) {
        return 2;
    }
    using namespace asst;
    using namespace asst::battle;
    const std::filesystem::path resources(argv[1]), plans(argv[2]), fixture_path(argv[3]), out(argv[4]);
    std::filesystem::create_directories(out);
    if (!AsstSetUserDir(MAA_NS::from_u16(out.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(resources.native()).c_str()) || !RoguelikeCopilot.load(plans)) {
        return 3;
    }
    const auto fixture = json::open(fixture_path);
    const auto stage = RoguelikeCopilot.get_stage_data("进退趋同#total34");
    if (!fixture || !stage) {
        return 4;
    }
    int checks = 0, failures = 0;
    const auto ck = [&](bool ok, const char* why) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << why << '\n';
        }
    };
    auto cfg = std::make_shared<RoguelikeConfig>();
    ck(cfg->verify_and_load_params(json::object { { "theme", "BlackFlow" }, { "mode", 0 } }), "real BlackFlow theme");
    cfg->set_start_opers({ { "维什戴尔", false } });
    C34Probe probe(cfg);
    probe.plans(*stage);
    const std::map<std::string, Role> roles { { "Pioneer", Role::Pioneer }, { "Warrior", Role::Warrior },
                                              { "Tank", Role::Tank },       { "Sniper", Role::Sniper },
                                              { "Caster", Role::Caster },   { "Medic", Role::Medic },
                                              { "Special", Role::Special } };
    std::vector<DeploymentOper> cards;
    for (const auto& row : fixture->at("operators").as_array()) {
        const auto name = row.at("name").as_string();
        const int elite = row.at("elite").as_integer(), level = row.at("level").as_integer();
        probe.elite(name, elite);
        cfg->status().opers[name] = RoguelikeOper { .elite = elite, .level = level };
        cards.emplace_back(
            DeploymentOper { .role = roles.at(row.at("role").as_string()),
                             .cost = static_cast<int>(row.at("cost").as_integer()),
                             .available = true,
                             .name = name });
    }
    const auto& opener = stage->deploy_plan.at("回费").front();
    const auto& core = stage->deploy_plan.at("益达").front();
    const auto& tank = stage->deploy_plan.at("地面阻挡").front();
    const auto& healer = stage->deploy_plan.at("元素奶").front();
    const auto& output = stage->deploy_plan.at("高台输出").front();
    const auto& auxiliary = stage->deploy_plan.at("术师").back();
    ck(stage->reserve_deploy_cost && opener.rank < core.rank && core.rank < tank.rank,
       "strict budget retains opener then core then lower tank");
    ck(opener.max_deployments == 1 && tank.minimum_base_block_count == 3, "bounded opener and genuine3-block duty");
    const auto card = [&](const std::string& name) -> DeploymentOper {
        return *std::ranges::find(cards, name, &DeploymentOper::name);
    };
    const auto ew = card("维什戴尔"), mech = card("机械师"), song = card("晓歌"), med = card("蜜莓");
    ck(!probe.starting_core_reserves_site(ew, opener, 0, stage->stage_name), "EW site reservation does not reserve7,1");
    auto collision = opener;
    collision.location = core.location;
    ck(probe.starting_core_reserves_site(ew, collision, 0, stage->stage_name), "same core site remains reserved");
    ck(probe.deployment_requirements_met(song, opener), "actual E1 song qualifies one-use upper opener");
    ck(probe.deployment_requirements_met(mech, tank), "actual E2 mech qualifies lower3 blocker");
    ck(!probe.deployment_requirements_met(song, tank), "one-block song cannot take lower3 duty");
    ck(!probe.deployment_requirements_met(card("司霆惊蛰"), tank), "liberator cannot fabricate persistent3 blocking");
    ck(probe.deployment_requirements_met(med, healer), "actual sole Honeyberry covers all three lower duties");
    ck(probe.deployment_requirements_met(card("卡达"), auxiliary), "actual E1 Carda7,5Right covers front and rear");
    auto wrong = auxiliary;
    wrong.location = { 6, 5 };
    ck(!probe.deployment_requirements_met(card("卡达"), wrong), "old6,5Right cannot claim Carda rear9,6");
    wrong = healer;
    wrong.location = { 4, 2 };
    ck(!probe.deployment_requirements_met(med, wrong), "old upper healer cannot claim lower coverage");
    ck(probe.deployment_requirements_met(ew, core), "actual E2 core covers upper front and rear");
    auto lower_claim = core;
    lower_claim.required_attack_tiles = { { 7, 6 }, { 9, 6 } };
    ck(!probe.deployment_requirements_met(ew, lower_claim), "upper EW does not fabricate lower direct coverage");
    probe.elite(ew.name, 1);
    ck(!probe.deployment_requirements_met(ew, core), "E1 cannot borrow required E2 core preparation");
    probe.elite(ew.name, 2);
    const auto soul_range = probe.get_attack_range(ew, core.direction);
    for (const auto pt : { healer.location, output.location, auxiliary.location }) {
        ck(std::ranges::find(soul_range, pt - core.location) == soul_range.end(),
           "lower highlands outside EW soul domain");
    }
    for (const auto& filename : { "ro6_t_9-obt-roguelike-ro6-level_rogue6_t-9.json",
                                  "ro6_t_9_b-obt-roguelike-ro6-level_rogue6_t-9-b.json",
                                  "ro6_t_9_c-obt-roguelike-ro6-level_rogue6_t-9-c.json" }) {
        const auto raw = json::open(resources / "resource" / "Arknights-Tile-Pos" / filename);
        if (!raw) {
            return 5;
        }
        Map::Level level(*raw);
        for (const auto pt : { opener.location, tank.location, Point(9, 1), Point(9, 6) }) {
            ck(level.get_item(pt.y, pt.x).buildableType == 1, "real ground on all three geometries");
        }
        for (const auto pt : { core.location, healer.location, output.location, auxiliary.location }) {
            ck(level.get_item(pt.y, pt.x).buildableType == 2, "real highland on all three geometries");
        }
        for (const auto pt : { Point(8, 1), Point(8, 6) }) {
            ck(level.get_item(pt.y, pt.x).buildableType == 0, "neither blue gate hosts a blocker");
        }
    }
    // Replay only the consumer's candidate order / budget predicate. These are
    // controlled DP snapshots and ledger transitions; no do_best_deploy input,
    // physical target observation, engine timing or damage simulation is run.
    std::set<std::string> deployed;
    std::set<Point> used;
    const auto& names = RoguelikeRecruit.get_group_names("BlackFlow");
    const auto select = [&](int dp, std::vector<DeploymentOper> available) -> std::optional<C34Probe::DeployPlanInfo> {
        std::vector<C34Probe::DeployPlanInfo> ordered;
        for (auto& op : available) {
            if (op.cooling || deployed.contains(op.name)) {
                continue;
            }
            const auto tag = OperNameTag { op.role, op.name };
            const auto& info = RoguelikeRecruit.get_oper_info("BlackFlow", tag);
            for (int group : RoguelikeRecruit.get_group_ids_of_oper("BlackFlow", tag)) {
                const auto plans_it = stage->deploy_plan.find(names.at(group));
                if (plans_it == stage->deploy_plan.end()) {
                    continue;
                }
                for (const auto& plan : plans_it->second) {
                    if (used.contains(plan.location) || probe.deployment_plan_exhausted(plan) ||
                        !probe.deployment_requirements_met(op, plan)) {
                        continue;
                    }
                    C34Probe::DeployPlanInfo item;
                    item.oper_name = op.name;
                    item.role = op.role;
                    item.rank = plan.rank;
                    item.plan_index = plan.plan_index;
                    item.placed = plan.location;
                    item.cost = op.cost;
                    item.prefer_group_order = plan.prefer_group_order;
                    item.oper_order_in_group = info.order_in_group.empty() ? INT_MAX : info.order_in_group.at(group);
                    ordered.push_back(item);
                }
            }
        }
        std::stable_sort(ordered.begin(), ordered.end());
        const C34Probe::DeployPlanInfo* waiting = nullptr;
        for (const auto& item : ordered) {
            auto op = *std::ranges::find(available, item.oper_name, &DeploymentOper::name);
            op.available = op.cost > 0 && dp >= op.cost;
            if (!op.available) {
                if (probe.deployment_cost_wait_required(op, dp) && (!waiting || item < *waiting)) {
                    waiting = &item;
                }
                continue;
            }
            if (stage->reserve_deploy_cost && waiting && *waiting < item) {
                continue;
            }
            return item;
        }
        return std::nullopt;
    };
    const auto step = [&](int dp, const std::string& expected, Point location) {
        const auto chosen = select(dp, cards);
        ck(chosen && chosen->oper_name == expected && chosen->placed == location, "real parsed plan ordered duty");
        if (chosen) {
            deployed.insert(chosen->oper_name);
            used.insert(chosen->placed);
            probe.consumed(chosen->plan_index);
        }
    };
    step(10, "晓歌", { 7, 1 });
    ck(probe.deployment_plan_exhausted(opener), "single opener completed once");
    ck(!select(2, cards), "after real8 spend on10 no fake affordability");
    ck(!select(21, cards), "affordable Mech21 cannot steal reserved core24 budget");
    step(24, "维什戴尔", { 6, 2 });
    step(21, "机械师", { 7, 6 });
    step(14, "蜜莓", { 6, 5 });
    step(12, "新约能天使", { 8, 5 });
    step(18, "卡达", { 7, 5 });
    ck(!select(99, cards), "completed roles do not fabricate extra actors");
    ck(8 + 24 + 21 - 10 == 43, "observed10 initial stock lower tank prefix deficit43");
    ck(8 + 24 + 21 + 14 - 10 == 57, "lower healer prefix deficit57 without invented refund");
    std::cout << "C34_SCOPE parsed comparator requirements site budget only; no "
                 "input / DPS / live fix proof\n";
    std::cout << "C34 checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
