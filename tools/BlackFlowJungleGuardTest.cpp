#include "Arknights-Tile-Pos/TileCalc2.hpp"
#include "AsstCaller.h"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Config/Roguelike/RoguelikeTrainingConfig.h"
#include "MaaUtils/Encoding.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

class JungleProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    JungleProbe(const std::shared_ptr<asst::RoguelikeConfig>& c) :
        RoguelikeBattleTaskPlugin({}, nullptr, "JungleProbe", c, nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_cost_wait_required;
    using RoguelikeBattleTaskPlugin::deployment_requirements_met;
    using RoguelikeBattleTaskPlugin::DeployPlanInfo;
    using RoguelikeBattleTaskPlugin::get_attack_range;

    void observe(const std::string& n, int e, int l)
    {
        m_oper_elite[n] = e;
        m_config->status().opers[n] = { .elite = e, .level = l };
    }

    void forget(const std::string& n)
    {
        m_oper_elite.erase(n);
        m_config->status().opers.erase(n);
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
        !AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str()) ||
        !RoguelikeTraining.load(repo / utils::path("resource/roguelike/BlackFlow/operator_training.json")) ||
        !RoguelikeCopilot.load(plans)) {
        return 3;
    }
    auto st = RoguelikeCopilot.get_stage_data("丛林法则");
    if (!st) {
        return 4;
    }
    auto cfg = std::make_shared<RoguelikeConfig>();
    cfg->verify_and_load_params(json::object { { "theme", "BlackFlow" }, { "mode", 0 } });
    cfg->set_start_opers({ { "维什戴尔", false } });
    JungleProbe probe(cfg);
    int checks = 0, failures = 0;
    auto ck = [&](bool v, const char* msg) {
        ++checks;
        if (!v) {
            ++failures;
            std::cerr << "FAIL " << msg << '\n';
        }
    };
    const auto& core = st->deploy_plan.at("益达").front();
    const auto& right = st->deploy_plan.at("地面阻挡").at(0);
    const auto& left = st->deploy_plan.at("地面阻挡").at(1);
    const auto& med = st->deploy_plan.at("单奶").front();
    const auto& secondary = st->deploy_plan.at("高台输出").front();
    ck(st->deploy_plan_only && st->reserve_deploy_cost, "strict authored plan with actual cost reservation");
    ck(core.rank < right.rank && right.rank < med.rank && med.rank < left.rank && left.rank < secondary.rank,
       "core right medic left secondary priority");
    ck(core.direction == DeployDirection::Up && core.location == Point(9, 2), "core actual highland upward");
    ck(right.location == Point(10, 1) && right.direction == DeployDirection::Right && left.location == Point(8, 1) &&
           left.direction == DeployDirection::Left,
       "two opposite approach guards");
    ck(right.minimum_base_block_count == 3 && left.minimum_base_block_count == 3 && right.require_persistent_blocking &&
           left.require_persistent_blocking,
       "both duties require persistent real three block");
    ck(!right.prefer_group_order && !left.prefer_group_order, "intra-group priority does not force fixed tier order");
    auto card = [](Role r, std::string n, int cost) {
        DeploymentOper o;
        o.role = r;
        o.name = std::move(n);
        o.cost = cost;
        return o;
    };
    auto ew = card(Role::Sniper, "维什戴尔", 24), mech = card(Role::Tank, "机械师", 21),
         horn = card(Role::Tank, "号角", 26), pode = card(Role::Support, "波登可", 12),
         carda = card(Role::Caster, "卡达", 18), stew = card(Role::Caster, "史都华德", 16),
         typhon = card(Role::Sniper, "提丰", 24), nova = card(Role::Special, "新约能天使", 12);
    for (const auto& [n, e, l] : std::vector<std::tuple<std::string, int, int>> { { ew.name, 2, 90 },
                                                                                  { mech.name, 2, 90 },
                                                                                  { horn.name, 1, 80 },
                                                                                  { pode.name, 1, 60 },
                                                                                  { carda.name, 1, 60 },
                                                                                  { stew.name, 1, 55 },
                                                                                  { typhon.name, 1, 80 },
                                                                                  { nova.name, 1, 80 } }) {
        probe.observe(n, e, l);
    }
    json::array ranges, projections;
    for (const auto& n : std::vector<std::string> { ew.name,
                                                    carda.name,
                                                    stew.name,
                                                    typhon.name,
                                                    pode.name,
                                                    "安赛尔",
                                                    "芙蓉",
                                                    "纯烬艾雅法拉" }) {
        auto role = n == ew.name || n == typhon.name    ? Role::Sniper
                    : n == carda.name || n == stew.name ? Role::Caster
                    : n == pode.name                    ? Role::Support
                                                        : Role::Medic;
        for (int elite : { 1, 2 }) {
            if (elite == 2 && (n == stew.name || n == "安赛尔" || n == "芙蓉")) {
                continue;
            }
            probe.observe(n, elite, elite == 1 ? (n == stew.name || n == "安赛尔" || n == "芙蓉" ? 55 : 60) : 80);
            auto op = card(role, n, 10);
            const auto& duty = n == ew.name ? core : (role == Role::Medic || n == pode.name) ? med : secondary;
            bool actual = probe.deployment_requirements_met(op, duty);
            auto r = probe.get_attack_range(op, duty.direction);
            auto has = [&](Point p) {
                return std::ranges::find(r, p - duty.location) != r.end();
            };
            ranges.emplace_back(
                json::object { { "name", n },
                               { "elite", elite },
                               { "range_constraints_only", actual },
                               { "8_1", has({ 8, 1 }) },
                               { "10_1", has({ 10, 1 }) },
                               { "11_1", has({ 11, 1 }) },
                               { "6_3", has({ 6, 3 }) },
                               { "7_1", has({ 7, 1 }) },
                               { "9_2", has({ 9, 2 }) } });
            if (n == ew.name) {
                ck(actual, "EW actual E1 or E2 upward covers both final guards without S3 range");
                auto old = core;
                old.direction = DeployDirection::Left;
                ck(!probe.deployment_requirements_met(op, old), "actual run74 left direction misses right guard");
                ck(has({ 11, 1 }), "upward basic range also reaches right incoming11,1");
            }
            if (n == carda.name || n == stew.name) {
                ck(actual, "actual caster phase covers6,3 and7,1 and7,2");
            }
            if (role == Role::Medic || n == pode.name) {
                ck(actual, "base range covers both guards and core not a healing action proof");
            }
            if (n == typhon.name) {
                ck(!actual, "Typhon near blind spot cannot claim secondary central duty");
            }
        }
    }
    probe.forget(ew.name);
    ck(!probe.deployment_requirements_met(ew, core), "unknown EW phase not borrowed");
    probe.observe(ew.name, 2, 90);
    probe.observe(mech.name, 2, 90);
    probe.observe(horn.name, 1, 80);
    ck(probe.deployment_requirements_met(mech, right) && probe.deployment_requirements_met(horn, left),
       "actual run74 MechE2 and HornE1 have real3block");
    probe.observe(horn.name, 0, 50);
    ck(!probe.deployment_requirements_met(horn, right), "HornE0 cannot borrow E1 third blocking slot");
    probe.observe(horn.name, 1, 80);
    auto scav = card(Role::Pioneer, "清道夫", 10);
    probe.observe(scav.name, 1, 60);
    ck(!probe.deployment_requirements_met(scav, right), "cheap two block cannot replace required3block enemy counter");
    auto kal = card(Role::Medic, "凯尔希", 20);
    probe.observe(kal.name, 2, 90);
    ck(!probe.deployment_requirements_met(kal, right),
       "medic token availability does not make body a three-block ground guard");
    probe.forget(mech.name);
    ck(!probe.deployment_requirements_met(mech, right), "unknown current level cannot borrow account capacity");
    probe.observe(mech.name, 2, 90);
    auto mylnar = card(Role::Warrior, "玛恩纳", 12);
    probe.observe(mylnar.name, 2, 90);
    ck(!probe.deployment_requirements_met(mylnar, right), "skill-only blocking excluded despite printed capacity");
    const auto& groups = RoguelikeRecruit.get_group_names("BlackFlow");
    auto trace = [&](const std::vector<DeploymentOper>& roster) {
        std::vector<JungleProbe::DeployPlanInfo> selected;
        std::vector<std::string> used;
        std::vector<Point> occupied;
        for (int step = 0; step < 8; ++step) {
            std::vector<JungleProbe::DeployPlanInfo> pool;
            for (const auto& op : roster) {
                if (op.cooling || std::ranges::find(used, op.name) != used.end()) {
                    continue;
                }
                const auto& ri = RoguelikeRecruit.get_oper_info("BlackFlow", { op.role, op.name });
                for (int gid : RoguelikeRecruit.get_group_ids_of_oper("BlackFlow", { op.role, op.name })) {
                    auto it = st->deploy_plan.find(groups.at(gid));
                    if (it == st->deploy_plan.end()) {
                        continue;
                    }
                    for (const auto& duty : it->second) {
                        if (std::ranges::find(occupied, duty.location) != occupied.end() ||
                            !probe.deployment_requirements_met(op, duty)) {
                            continue;
                        }
                        JungleProbe::DeployPlanInfo d;
                        d.oper_name = op.name;
                        d.role = op.role;
                        d.cost = op.cost;
                        d.rank = duty.rank;
                        d.plan_index = duty.plan_index;
                        d.placed = duty.location;
                        d.direction = duty.direction;
                        d.prefer_group_order = duty.prefer_group_order;
                        d.oper_order_in_group = ri.order_in_group.empty() ? INT_MAX : ri.order_in_group.at(gid);
                        pool.push_back(d);
                    }
                }
            }
            if (pool.empty()) {
                break;
            }
            std::stable_sort(pool.begin(), pool.end());
            selected.push_back(pool.front());
            used.push_back(pool.front().oper_name);
            occupied.push_back(pool.front().placed);
        }
        return selected;
    };
    probe.observe(carda.name, 1, 60);
    probe.observe(stew.name, 1, 55);
    probe.observe(typhon.name, 1, 80);
    probe.observe(pode.name, 1, 60);
    auto selected = trace({ ew, mech, horn, pode, carda, stew, typhon, nova });
    json::array order;
    for (const auto& a : selected) {
        order.emplace_back(
            json::object { { "name", a.oper_name },
                           { "tile", json::array { a.placed.x, a.placed.y } },
                           { "cost", a.cost },
                           { "rank", a.rank } });
    }
    ck(selected.size() == 4, "actual roster has four eligible duties without a directly healing medic");
    if (selected.size() == 4) {
        ck(selected[0].oper_name == ew.name, "actual roster core first");
        ck(selected[1].oper_name == mech.name && selected[1].placed == Point(10, 1),
           "actual primary Mech group goes right first");
        ck(selected[2].oper_name == horn.name && selected[2].placed == Point(8, 1),
           "remaining E1 Horn fills left without module evidence");
        ck(selected[3].oper_name == stew.name, "same duty caster chosen by actual lower price");
    }
    ck(std::ranges::none_of(selected, [&](const auto& p) { return p.placed == med.location; }),
       "actual Podenco S1 is not silently promoted into direct-medical group");
    auto real_med = card(Role::Medic, "纯烬艾雅法拉", 16);
    probe.observe(real_med.name, 2, 90);
    auto with_med = trace({ ew, mech, horn, pode, carda, stew, real_med });
    ck(with_med.size() == 5 && with_med[2].oper_name == real_med.name && with_med[2].placed == med.location,
       "control roster with direct healer fills medical duty");
    const auto pod_ids = RoguelikeRecruit.get_group_ids_of_oper("BlackFlow", { pode.role, pode.name });
    bool pod_medical_group = false;
    for (int id : pod_ids) {
        const auto& name = groups.at(id);
        if (name == "元素奶" || name == "单奶") {
            pod_medical_group = true;
        }
    }
    ck(!pod_medical_group, "Podenco S2 cannot enter direct-medical slot merely by geometric range");
    auto single = trace({ ew, mech, pode, carda });
    ck(std::ranges::none_of(single, [](const auto& a) { return a.placed == Point(8, 1); }),
       "single real blocker not duplicated into left duty");
    auto no_guard = trace({ ew, scav, pode, carda });
    ck(std::ranges::none_of(
           no_guard,
           [](const auto& a) { return a.placed == Point(10, 1) || a.placed == Point(8, 1); }),
       "no qualifying blocker does not silently accept two block");
    auto expensive = mech;
    expensive.cost = 35;
    auto cheaper = horn;
    cheaper.cost = 26;
    auto swap = trace({ ew, expensive, cheaper, pode, carda });
    ck(swap.size() > 1 && swap[1].oper_name == mech.name, "cross-group rank remains explicit over card price");
    auto saria = card(Role::Tank, "塞雷娅", 17);
    probe.observe(saria.name, 1, 80);
    auto no_mech = trace({ ew, horn, saria, pode, carda });
    ck(no_mech.size() >= 4 && no_mech[1].oper_name == saria.name && no_mech[1].placed == Point(10, 1),
       "without Mech cheaper qualified same-group defender fills right");
    ck(no_mech.size() >= 3 && no_mech[2].oper_name == horn.name && no_mech[2].placed == Point(8, 1),
       "without Mech a second real guard still fills left");
    auto only_horn = trace({ ew, horn, pode, carda });
    ck(only_horn.size() >= 2 && only_horn[1].oper_name == horn.name && only_horn[1].placed == Point(10, 1),
       "single alternative three-block fills right without invented Mech");
    ew.available = false;
    ck(!JungleProbe::deployment_cost_wait_required(ew, 30) && JungleProbe::deployment_cost_wait_required(ew, 6),
       "actual24 core payable at30 but not after imagined21 spend");
    ck(24 <= 30 && 24 + 21 > 30 && 24 + 12 + 12 + 21 > 24 + 21,
       "core then guard has15 deficit rather than observed fillers before guard");
    for (const auto& file : std::vector<std::string> { "ro6_n_5_6-obt-roguelike-ro6-level_rogue6_5-6.json",
                                                       "ro6_e_5_6-obt-roguelike-ro6-level_rogue6_5-6.json" }) {
        auto raw = json::open(repo / utils::path("resource/Arknights-Tile-Pos/" + file));
        if (!raw) {
            return 5;
        }
        Map::Level level(*raw);
        for (auto p : std::vector<Point> { { 10, 1 }, { 8, 1 } }) {
            ck(level.get_item(p.y, p.x).buildableType == 1,
               "both blocker tiles actually ground in normal and emergency");
        }
        for (auto p : std::vector<Point> { { 9, 2 }, { 10, 2 }, { 5, 2 } }) {
            ck(level.get_item(p.y, p.x).buildableType == 2, "three output and medical sites are actual highland");
        }
        ck(level.get_item(1, 9).buildableType == 0, "blue9,1 remains unbuildable no Horn exception");
        if (file[4] == 'n') {
            for (auto p : std::vector<
                     Point> { { 8, 1 }, { 9, 1 }, { 10, 1 }, { 11, 1 }, { 9, 2 }, { 10, 2 }, { 5, 2 }, { 6, 3 } }) {
                auto pt = Map::TileCalc2::get_tile_screen_pos(level, p.y, p.x);
                projections.emplace_back(
                    json::object { { "tile", json::array { p.x, p.y } }, { "normal", json::array { pt.x, pt.y } } });
            }
        }
    }
    auto skills = json::open(plans.parent_path() / "inputs/skills_nov1.json");
    auto chars = json::open(plans.parent_path() / "inputs/character_semantics_nov1.json");
    if (!skills || !chars) {
        return 6;
    }
    ck(chars->at("char_1035_wisdel").at("phases").at(1).at("rangeId").as_string() == "3-9" &&
           chars->at("char_1035_wisdel").at("phases").at(2).at("rangeId").as_string() == "3-9",
       "pinned real E1/E2 base range independent of selected skill");
    ck(chars->at("char_1035_wisdel").at("skills").at(2).at("unlockCond").at("phase").as_string() == "PHASE_2",
       "E1 range does not grant E2 S3");
    ck(skills->at("skchr_wisdel_3").at("levels").at(9).at("spData").at("initSp").as_integer() == 40 &&
           skills->at("skchr_wisdel_3").at("levels").at(9).at("spData").at("spCost").as_integer() == 50,
       "S3M3 still needs10SP after deployment");
    auto prep = json::open(plans.parent_path() / "inputs/run74_preparations.json");
    if (!prep) {
        return 7;
    }
    ck(prep->as_array().size() == 6, "six actual preparation receipts preserved");
    const auto& actual = prep->at(5).at("payload").at("details").at("current_run_preparation");
    bool actual_pod_s1 = false, actual_ew_s3 = false, actual_horn_e1 = false;
    for (const auto& op : actual.as_array()) {
        const auto id = op.at("operator_id").as_string();
        if (id == "char_258_podego") {
            actual_pod_s1 =
                op.at("selected_skill").as_integer() == 1 && op.at("selection_source_sequence").as_integer() == 15;
        }
        if (id == "char_1035_wisdel") {
            actual_ew_s3 = op.at("elite").as_integer() == 2 && op.at("selected_skill").as_integer() == 3 &&
                           op.at("displayed_skill_levels").at(2).as_integer() == 10;
        }
        if (id == "char_4039_horn") {
            actual_horn_e1 = op.at("elite").as_integer() == 1 && op.at("level").as_integer() == 80 &&
                             op.at("selected_skill").as_integer() == 1;
        }
    }
    ck(actual_pod_s1, "real run74 Podenco selectedS1 from current sequence15 not account backup");
    ck(actual_ew_s3 && actual_horn_e1, "real run74 EW S3M3 and HornE1S1 bound to receipt");
    const auto& pod7 = skills->at("skchr_podego_1").at("levels").at(6);
    ck(pod7.at("duration").as_double() == 22 && pod7.at("spData").at("spCost").as_integer() == 29 &&
           pod7.at("spData").at("initSp").as_integer() == 6,
       "Podenco S1 heals only skill interval not permanent HPS");
    auto primary = json::open(plans.parent_path() / "inputs/level_rogue6_5-6.json");
    if (!primary) {
        return 8;
    }
    const auto native_xy = [](const json::value& pos) {
        return Point(pos.at("col").as_integer(), 7 - pos.at("row").as_integer());
    };
    for (int idx : { 13, 16 }) {
        const auto& route = primary->at("routes").at(idx);
        bool teleport = false, right_final = false;
        for (const auto& cp : route.at("checkpoints").as_array()) {
            if (cp.at("type").as_string() == "APPEAR_AT_POS" && native_xy(cp.at("position")) == Point(12, 6)) {
                teleport = true;
            }
            if (cp.at("type").as_string() == "MOVE" && native_xy(cp.at("position")) == Point(10, 1)) {
                right_final = true;
            }
        }
        ck(teleport && right_final && native_xy(route.at("endPosition")) == Point(9, 1),
           "raw portal route attacks gate from right through10,1");
    }
    std::ofstream(out / "range_results.json") << json::value(ranges).format();
    std::ofstream(out / "selection_results.json") << json::value(order).format();
    std::ofstream(out / "projection.json") << json::value(projections).format();
    std::cout << "JUNGLE128 checks=" << checks << " failures=" << failures << " device_inputs=0 live_pass=false\n";
    return failures ? 1 : 0;
}
