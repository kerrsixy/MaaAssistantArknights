#include "Arknights-Tile-Pos/TileCalc2.hpp"
#include "AsstCaller.h"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "MaaUtils/Encoding.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

class Probe : public asst::RoguelikeBattleTaskPlugin
{
public:
    Probe(std::shared_ptr<asst::RoguelikeConfig> c) :
        RoguelikeBattleTaskPlugin({}, nullptr, "ConvergentProbe", std::move(c), nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_cost_wait_required;
    using RoguelikeBattleTaskPlugin::deployment_requirements_met;
    using RoguelikeBattleTaskPlugin::DeployPlanInfo;
    using RoguelikeBattleTaskPlugin::get_attack_range;

    void elite(const std::string& n, int e)
    {
        if (e < 0) {
            m_oper_elite.erase(n);
        }
        else {
            m_oper_elite[n] = e;
        }
    }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    using namespace asst::battle;
    const std::filesystem::path repo(argv[1]), plans(argv[2]), out(argv[3]);
    std::filesystem::create_directories(out);
    if (!AsstSetUserDir(MAA_NS::from_u16(out.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str()) || !RoguelikeCopilot.load(plans)) {
        return 3;
    }
    auto st = RoguelikeCopilot.get_stage_data("进退趋同");
    auto raw =
        json::open(repo / utils::path("resource/Arknights-Tile-Pos/ro6_t_9-obt-roguelike-ro6-level_rogue6_t-9.json"));
    if (!st || !raw) {
        return 4;
    }
    Map::Level level(*raw);
    auto cfg = std::make_shared<RoguelikeConfig>();
    cfg->set_start_opers({ { "维什戴尔", false } });
    Probe probe(cfg);
    int checks = 0, failures = 0;
    const auto ck = [&](bool v, const char* s) {
        ++checks;
        if (!v) {
            ++failures;
            std::cerr << "FAIL " << s << '\n';
        }
    };
    const auto& upper = st->deploy_plan.at("地面阻挡").at(1);
    const auto& lower = st->deploy_plan.at("地面阻挡").at(0);
    const auto& core = st->deploy_plan.at("益达").front();
    const auto& output = st->deploy_plan.at("高台输出").front();
    ck(st->deploy_plan_only && st->reserve_deploy_cost, "authored-only strict budget");
    for (auto pt : std::vector<Point> { { 7, 1 }, { 7, 6 } }) {
        ck(level.get_item(pt.y, pt.x).buildableType == 1, "front blocker real ground");
    }
    for (auto pt : std::vector<Point> { { 6, 2 }, { 8, 5 }, { 4, 2 }, { 6, 5 } }) {
        ck(level.get_item(pt.y, pt.x).buildableType == 2, "rear route output real highland");
    }
    for (auto pt : std::vector<Point> { { 8, 1 }, { 8, 6 } }) {
        ck(level.get_item(pt.y, pt.x).buildableType == 0, "blue gate cannot host blocker");
    }
    for (auto pt : std::vector<Point> { { 9, 1 }, { 9, 6 } }) {
        ck(level.get_item(pt.y, pt.x).buildableType == 1, "post portal lane is real road");
    }
    ck(upper.location == Point(7, 1) && lower.location == Point(7, 6), "distinct two front lanes");
    ck(core.rank < lower.rank && lower.rank < upper.rank && upper.rank < output.rank,
       "core before lower blocker before upper blocker before secondary output");
    ck(!upper.prefer_group_order, "blocker does not prefer expensive intra-group rank");
    ck(upper.rank < st->deploy_plan.at("机械师").at(1).rank,
       "generic upper blocker beats same-cell mech fallback group");
    ck(core.location == Point(6, 2) && core.direction == DeployDirection::Right,
       "upper output faces rear portal segment");
    ck(output.location == Point(8, 5) && output.direction == DeployDirection::Down,
       "lower output points down across both sides of blue gate");
    DeploymentOper ew { .role = Role::Sniper, .name = "维什戴尔" };
    ew.cost = 24;
    probe.elite(ew.name, 2);
    ck(probe.deployment_requirements_met(ew, core), "EW E2 covers upper front and rear");
    auto old = core;
    old.location = { 7, 2 };
    old.direction = DeployDirection::Left;
    ck(!probe.deployment_requirements_met(ew, old), "old left-facing EW misses rear9,1");
    probe.elite(ew.name, 1);
    ck(!probe.deployment_requirements_met(ew, core), "E1 cannot borrow E2 core requirement");
    probe.elite(ew.name, -1);
    ck(!probe.deployment_requirements_met(ew, core), "unknown elite not inferred E2");
    probe.elite(ew.name, 2);
    json::array range_results;
    for (const auto& [name, role] : std::vector<std::pair<std::string, Role>> { { "克洛丝", Role::Sniper },
                                                                                { "能天使", Role::Sniper },
                                                                                { "提丰", Role::Sniper },
                                                                                { "卡达", Role::Caster },
                                                                                { "史都华德", Role::Caster },
                                                                                { "假日威龙陈", Role::Sniper },
                                                                                { "浊心斯卡蒂", Role::Support },
                                                                                { "波登可", Role::Support } }) {
        DeploymentOper op { .role = role, .name = name };
        for (int e : { 1, 2 }) {
            if (e == 2 && (name == "克洛丝" || name == "史都华德")) {
                continue;
            }
            probe.elite(name, e);
            auto range = probe.get_attack_range(op, output.direction);
            auto has = [&](Point x) {
                return std::ranges::find(range, x - output.location) != range.end();
            };
            bool accepted = probe.deployment_requirements_met(op, output);
            range_results.emplace_back(
                json::object { { "name", name },
                               { "elite", e },
                               { "front7_6", has({ 7, 6 }) },
                               { "rear9_6", has({ 9, 6 }) },
                               { "accepted", accepted } });
            if (name == "克洛丝" && e == 1) {
                ck(accepted, "Kroos E1 actually covers both lower cells");
            }
            if (name == "卡达" && e == 1) {
                ck(accepted, "Carda E1 down-facing covers front7,6 and rear9,6");
            }
            if (name == "史都华德" && e == 1) {
                ck(accepted, "Steward E1 down-facing covers both lower duties");
            }
            if (name == "提丰") {
                ck(!accepted, "Typhon near blind spot cannot claim lower front duty");
            }
            if (name == "浊心斯卡蒂") {
                ck(!accepted, "support does not fabricate attack duty");
            }
        }
    }
    std::ofstream ranges(out / "range_results.json");
    ranges << json::value(range_results).format();
    ranges.close();
    auto old_lower = output;
    old_lower.location = { 6, 5 };
    old_lower.direction = DeployDirection::Right;
    probe.elite("卡达", 1);
    ck(!probe.deployment_requirements_met({ .role = Role::Caster, .name = "卡达" }, old_lower),
       "old lower6,5Right cannot claim Carda rear coverage");
    const auto& groups = RoguelikeRecruit.get_group_names(std::string(RoguelikeTheme::BlackFlow));
    auto best = [&](const std::vector<DeploymentOper>& ops) {
        std::vector<Probe::DeployPlanInfo> list;
        for (const auto& op : ops) {
            if (op.cooling) {
                continue;
            }
            auto ids =
                RoguelikeRecruit.get_group_ids_of_oper(std::string(RoguelikeTheme::BlackFlow), { op.role, op.name });
            const auto& ri =
                RoguelikeRecruit.get_oper_info(std::string(RoguelikeTheme::BlackFlow), { op.role, op.name });
            for (int id : ids) {
                auto it = st->deploy_plan.find(groups.at(id));
                if (it == st->deploy_plan.end()) {
                    continue;
                }
                for (const auto& d : it->second) {
                    if (d.location != upper.location || !probe.deployment_requirements_met(op, d)) {
                        continue;
                    }
                    Probe::DeployPlanInfo a;
                    a.oper_name = op.name;
                    a.cost = op.cost;
                    a.rank = d.rank;
                    a.plan_index = d.plan_index;
                    a.prefer_group_order = d.prefer_group_order;
                    a.oper_order_in_group = ri.order_in_group.empty() ? INT_MAX : ri.order_in_group.at(id);
                    list.push_back(a);
                }
            }
        }
        std::stable_sort(list.begin(), list.end());
        return list;
    };
    const auto card = [](Role role, const std::string& name, int cost) {
        DeploymentOper o;
        o.role = role;
        o.name = name;
        o.cost = cost;
        return o;
    };
    auto silver = card(Role::Pioneer, "凛御银灰", 13), mech = card(Role::Tank, "机械师", 21),
         fang = card(Role::Pioneer, "芬", 10), hoshi = card(Role::Tank, "星熊", 30),
         myrtle = card(Role::Pioneer, "桃金娘", 9);
    for (auto op : { silver, mech, fang, hoshi, myrtle }) {
        probe.elite(op.name, 1);
    }
    ck(probe.deployment_requirements_met(silver, upper), "observed Silver is persistent blocker candidate");
    ck(!probe.deployment_requirements_met(myrtle, upper), "flagbearer rejected as persistent blocker");
    auto a = best({ mech, silver });
    ck(!a.empty() && a.front().oper_name == silver.name, "actual group rank cannot let Mech21 beat Silver13");
    auto b = best({ mech, hoshi, fang });
    ck(!b.empty() && b.front().oper_name == fang.name, "known10 Vanguard beats30tank and21mech");
    ck(!Probe::deployment_cost_wait_required(silver, 13) && Probe::deployment_cost_wait_required(mech, 13),
       "real13 budget admits low blocker but notMech21");
    auto unknown = fang;
    unknown.cost = 0;
    auto c = best({ unknown, silver });
    ck(!c.empty() && c.front().oper_name == silver.name, "unknown zero cost not treated free");
    ck(!Probe::deployment_cost_wait_required(silver, 24),
       "early core budget could pay blocker but no timing guarantee");

    const auto& top_med = st->deploy_plan.at("单奶").at(0);
    const auto& bottom_med = st->deploy_plan.at("单奶").at(1);
    ck(output.rank < top_med.rank && top_med.rank < bottom_med.rank, "healers after actual output duties");
    json::array medic_results;
    for (const auto& d : { top_med, bottom_med }) {
        ck(level.get_item(d.location.y, d.location.x).buildableType == 2, "medic tile legal");
        for (const auto& name : { "安赛尔", "芙蓉", "纯烬艾雅法拉" }) {
            for (int e : { 1, 2 }) {
                if (e == 2 && std::string_view(name) != "纯烬艾雅法拉") {
                    continue;
                }
                DeploymentOper op { .role = Role::Medic, .name = name };
                probe.elite(name, e);
                const bool accepted = probe.deployment_requirements_met(op, d);
                const bool unsupported_elite = e == 2 && std::string_view(name) != "纯烬艾雅法拉";
                medic_results.emplace_back(
                    json::object { { "name", name },
                                   { "elite", e },
                                   { "x", d.location.x },
                                   { "y", d.location.y },
                                   { "accepted", accepted },
                                   { "unsupported_elite", unsupported_elite } });
                ck(accepted != unsupported_elite, "real three-star E1 and Pure E1/E2 cover medic duties");
            }
        }
    }
    std::ofstream meds(out / "medic_results.json");
    meds << json::value(medic_results).format();
    meds.close();
    const auto routed = [&](const std::string& name, Role role, Point at) {
        for (int id : RoguelikeRecruit.get_group_ids_of_oper(std::string(RoguelikeTheme::BlackFlow), { role, name })) {
            auto it = st->deploy_plan.find(groups.at(id));
            if (it != st->deploy_plan.end() &&
                std::ranges::any_of(it->second, [&](const auto& d) { return d.location == at; })) {
                return true;
            }
        }
        return false;
    };
    ck(!routed("浊心斯卡蒂", Role::Support, top_med.location), "Bard cannot fill real medic slot via mixed group");
    ck(!routed("波登可", Role::Support, output.location),
       "Podenco healing-skill operator not routed to lower output by groups");
    ck(!routed("波登可", Role::Support, top_med.location), "unsupported healing-mode assumption does not enter slot");

    json::array upper_ranges;
    for (int e : { 0, 1, 2 }) {
        probe.elite(ew.name, e);
        const auto offsets = probe.get_attack_range(ew, core.direction);
        const auto covers = [&](Point p) {
            return std::ranges::find(offsets, p - core.location) != offsets.end();
        };
        upper_ranges.emplace_back(
            json::object { { "elite", e },
                           { "front7_1", covers({ 7, 1 }) },
                           { "rear9_1", covers({ 9, 1 }) },
                           { "existing_plan_accepts", probe.deployment_requirements_met(ew, core) } });
        if (e == 1) {
            ck(covers({ 7, 1 }) && covers({ 9, 1 }), "actual E1 range covers both; rejection is not smaller range");
        }
    }
    std::ofstream upper_file(out / "upper_range_by_elite.json");
    upper_file << json::value(upper_ranges).format();
    probe.elite(ew.name, 2);
    auto er = probe.get_attack_range(ew, DeployDirection::Right);
    const auto domain = [&](Point p) {
        return std::ranges::find(er, p - core.location) != er.end();
    };
    ck(domain({ 8, 2 }), "old upper medic8,2 is in EW attack and possible Soul domain");
    ck(!domain({ 4, 2 }) && !domain({ 6, 5 }) && !domain({ 8, 5 }),
       "new rear medic cells outside current EW rightward domain");
    for (auto d : { top_med, bottom_med }) {
        d.location = d.location.y == 2 ? Point(8, 2) : Point(8, 5);
        d.direction = DeployDirection::Left;
        probe.elite("安赛尔", 1);
        ck(probe.deployment_requirements_met({ .role = Role::Medic, .name = "安赛尔" }, d),
           "old nearer medic also range-valid but domain risk remains distinct");
    }
    ck(Probe::deployment_cost_wait_required(silver, 10), "initial ten cost does not pay thirteen-cost blocker");
    auto unavailable = silver;
    unavailable.cooling = true;
    ck(best({ unavailable }).empty(), "single deployed or cooling blocker is not invented as second available unit");
    auto primary = json::open(plans.parent_path() / "inputs/level_t-9.json");
    if (!primary) {
        return 5;
    }
    const int height = static_cast<int>((*primary).at("mapData").at("map").as_array().size());
    const auto xy = [&](const json::value& p) {
        return Point(p.at("col").as_integer(), height - 1 - p.at("row").as_integer());
    };
    const auto& routes = (*primary).at("routes");
    ck(height == 8, "primary coordinate conversion height8");
    ck(xy(routes[4].at("startPosition")) == Point(0, 1) && xy(routes[4].at("endPosition")) == Point(8, 1),
       "raw upper left path maps correctly");
    ck(xy(routes[5].at("startPosition")) == Point(0, 6) && xy(routes[5].at("endPosition")) == Point(8, 6),
       "raw lower left path maps correctly");
    for (int route : { 0, 1, 2, 3 }) {
        const auto& r = routes[route];
        bool upper_exit = route >= 2;
        const auto& cp = r.at("checkpoints");
        ck(cp[0].at("type").as_string() == "WAIT_FOR_SECONDS" && cp[0].at("time").as_double() == 120,
           "central start waits120");
        ck(cp[2].at("type").as_string() == "DISAPPEAR" && cp[3].at("time").as_double() == 10,
           "left disappear then10second wait");
        ck(cp[4].at("type").as_string() == "APPEAR_AT_POS" && xy(cp[4].at("position")) == Point(11, upper_exit ? 3 : 4),
           "right portal is appearance checkpoint");
        ck(xy(cp[6].at("position")) == Point(9, upper_exit ? 1 : 6), "last waypoint is behind blue door");
        ck(xy(r.at("endPosition")) == Point(8, upper_exit ? 1 : 6), "rear segment reaches corresponding blue door");
    }
    int spawns = 0;
    for (const auto& w : (*primary).at("waves").as_array()) {
        for (const auto& f : w.at("fragments").as_array()) {
            for (const auto& a : f.at("actions").as_array()) {
                if (a.at("actionType").as_string() == "SPAWN") {
                    spawns += a.at("count").as_integer();
                }
            }
        }
    }
    ck(spawns == 38, "raw declares38 spawn instances; not asserted equal to actual34 HUD accounting");
    // Compare exact observed card prices, never infer a deployment timestamp
    // from regeneration or assume that an unavailable card became ready.
    ck(!Probe::deployment_cost_wait_required(ew, 30), "observed30 can pay actual24 core");
    ck(24 <= 30 && 10 + 24 > 30 && 21 + 24 > 30 && 10 + 21 + 24 > 30,
       "core-first has no initial deficit; cheap-ground-first and old two-ground prefix do");
    const auto trace = [&](std::vector<DeploymentOper> roster) {
        std::vector<std::string> used_names;
        std::vector<Point> used_tiles;
        std::vector<Probe::DeployPlanInfo> selected;
        for (int step = 0; step < 8; ++step) {
            std::vector<Probe::DeployPlanInfo> candidates;
            for (const auto& op : roster) {
                if (op.cooling || std::ranges::find(used_names, op.name) != used_names.end()) {
                    continue;
                }
                const auto& ri = RoguelikeRecruit.get_oper_info("BlackFlow", { op.role, op.name });
                for (int id : RoguelikeRecruit.get_group_ids_of_oper("BlackFlow", { op.role, op.name })) {
                    const auto found = st->deploy_plan.find(groups.at(id));
                    if (found == st->deploy_plan.end()) {
                        continue;
                    }
                    for (const auto& duty : found->second) {
                        if (std::ranges::find(used_tiles, duty.location) != used_tiles.end() ||
                            !probe.deployment_requirements_met(op, duty)) {
                            continue;
                        }
                        Probe::DeployPlanInfo next;
                        next.oper_name = op.name;
                        next.role = op.role;
                        next.cost = op.cost;
                        next.rank = duty.rank;
                        next.plan_index = duty.plan_index;
                        next.placed = duty.location;
                        next.direction = duty.direction;
                        next.prefer_group_order = duty.prefer_group_order;
                        next.oper_order_in_group = ri.order_in_group.empty() ? INT_MAX : ri.order_in_group.at(id);
                        candidates.push_back(next);
                    }
                }
            }
            if (candidates.empty()) {
                break;
            }
            std::stable_sort(candidates.begin(), candidates.end());
            selected.push_back(candidates.front());
            used_names.push_back(candidates.front().oper_name);
            used_tiles.push_back(candidates.front().placed);
        }
        return selected;
    };
    auto carda = card(Role::Caster, "卡达", 18), shirayuki = card(Role::Sniper, "白雪", 25),
         touch = card(Role::Medic, "Touch", 5), scavenger = card(Role::Pioneer, "清道夫", 10);
    probe.elite(ew.name, 2);
    probe.elite(mech.name, 2);
    probe.elite(carda.name, 1);
    probe.elite(shirayuki.name, 2);
    probe.elite(touch.name, 2);
    probe.elite(scavenger.name, 1);
    const auto single = trace({ ew, mech, carda, shirayuki, touch });
    const auto dual = trace({ ew, mech, carda, shirayuki, touch, scavenger });
    ck(single.size() >= 2 && single[0].oper_name == ew.name && single[0].placed == Point(6, 2) &&
           single[1].oper_name == mech.name && single[1].placed == Point(7, 6),
       "actual battle6 roster puts core first and only permanent blocker lower");
    ck(std::ranges::none_of(single, [](const auto& p) { return p.placed == Point(7, 1); }),
       "single blocker cannot be duplicated into upper lane");
    ck(dual.size() >= 3 && dual[0].oper_name == ew.name && dual[1].oper_name == scavenger.name &&
           dual[1].placed == Point(7, 6) && dual[2].oper_name == mech.name && dual[2].placed == Point(7, 1),
       "actual battle8 roster puts core before both cheap-lower and expensive-upper blockers");
    auto absent_core = ew;
    absent_core.cooling = true;
    const auto unavailable_core = trace({ absent_core, mech, scavenger });
    ck(std::ranges::none_of(unavailable_core, [&](const auto& p) { return p.oper_name == ew.name; }),
       "cooling core not invented as already deployed");
    probe.elite(ew.name, 1);
    ck(!probe.deployment_requirements_met(ew, core),
       "existing E2 upper duty still rejects E1 without fabricating coverage");
    probe.elite(ew.name, 2);
    auto skills = json::open(plans.parent_path() / "inputs/skill_wisdel_3_nov1.json");
    auto preparation = json::open(plans.parent_path() / "inputs/run73_preparation.json");
    if (!skills || !preparation) {
        return 6;
    }
    auto character = json::open(plans.parent_path() / "inputs/wisdel_character_nov1.json");
    if (!character) {
        return 7;
    }
    ck(character->at("phases").at(1).at("rangeId").as_string() ==
           character->at("phases").at(2).at("rangeId").as_string(),
       "pinned E1 and E2 base range IDs equal");
    ck(character->at("skills").at(2).at("skillId").as_string() == "skchr_wisdel_3" &&
           character->at("skills").at(2).at("unlockCond").at("phase").as_string() == "PHASE_2",
       "S3 unlock requires E2 despite same E1 base range");
    ck(skills->at("skillId").as_string() == "skchr_wisdel_3", "pinned skill identity");
    const auto& m3 = skills->at("levels").at(9).at("spData");
    ck(m3.at("initSp").as_integer() == 40 && m3.at("spCost").as_integer() == 50,
       "pinned M3 starts40 of50; deployment alone does not make skill ready");
    for (const auto& p : preparation->as_array()) {
        bool exact_skill = false;
        for (const auto& op : p.at("details").at("current_run_preparation").as_array()) {
            if (op.at("operator_id").as_string() == "char_1035_wisdel") {
                exact_skill = op.at("elite").as_integer() == 2 && op.at("selected_skill").as_integer() == 3 &&
                              op.at("displayed_skill_levels").at(2).as_integer() == 10;
            }
        }
        ck(exact_skill, "actual battle6/8 preparation identifies E2 S3 M3, not a role-name assumption");
    }
    json::array order_results;
    for (const auto& [label, order] : std::vector<std::pair<std::string, std::vector<Probe::DeployPlanInfo>>> {
             { "actual_battle6_counterfactual", single },
             { "actual_battle8_counterfactual", dual } }) {
        json::array sequence;
        for (const auto& p : order) {
            sequence.emplace_back(
                json::object { { "name", p.oper_name },
                               { "tile", json::array { p.placed.x, p.placed.y } },
                               { "cost", p.cost },
                               { "rank", p.rank } });
        }
        order_results.emplace_back(
            json::object { { "fixture", label },
                           { "priority_only", true },
                           { "time_or_success_predicted", false },
                           { "sequence", sequence } });
    }
    std::ofstream order_file(out / "priority_counterfactual.json");
    order_file << json::value(order_results).format();
    std::cout << "CONVERGENT124 checks=" << checks << " failures=" << failures << " device_inputs=0 live_pass=false\n";
    return failures ? 1 : 0;
}
