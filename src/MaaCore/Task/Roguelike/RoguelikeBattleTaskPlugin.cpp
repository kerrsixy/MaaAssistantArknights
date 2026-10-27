#include <regex>

#include "RoguelikeBattleTaskPlugin.h"

#include <chrono>
#include <future>
#include <ranges>
#include <vector>

#include "Config/GeneralConfig.h"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Miscellaneous/TilePack.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "Task/ProcessTask.h"
#include "Utils/Logger.hpp"
#include "Vision/RegionOCRer.h"

using namespace asst::battle;
using namespace asst::battle::roguelike;

asst::RoguelikeBattleTaskPlugin::RoguelikeBattleTaskPlugin(
    const AsstCallback& callback,
    Assistant* inst,
    std::string_view task_chain,
    const std::shared_ptr<RoguelikeConfig>& roguelike_config_ptr,
    const std::shared_ptr<RoguelikeControlTaskPlugin>& control) :
    AbstractRoguelikeTaskPlugin(callback, inst, task_chain, roguelike_config_ptr, control),
    BattleHelper(inst)
{
}

bool asst::RoguelikeBattleTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    if (msg != AsstMsg::SubTaskCompleted || details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }

    if (!RoguelikeConfig::is_valid_theme(m_config->get_theme())) {
        Log.error("Roguelike name doesn't exist!");
        return false;
    }
    const std::string roguelike_name = m_config->get_theme() + "@";
    const std::string& task = details.get("details", "task", "");
    std::string_view task_view = task;
    if (task_view.starts_with(roguelike_name)) {
        task_view.remove_prefix(roguelike_name.length());
    }
    if (task_view == "Roguelike@StartAction") {
        return true;
    }
    else {
        return false;
    }
}

bool asst::RoguelikeBattleTaskPlugin::_run()
{

    // 进入战斗前位于地图界面，读取「目标生命值」并记录掉血，供判断关卡风险
    {
        RegionOCRer hp_ocr(ctrler()->get_image());
        hp_ocr.set_task_info("Mizuki@Roguelike@HpRecognition");
        const auto hp_res = hp_ocr.analyze();
        const bool hp_ok = hp_res.has_value();
        if (hp_ok) {
            int hp_now = -1;
            // 只在读到 "X/Y" 形式时采信，避免把标签文字当数值
            {
                static const std::regex hp_re(R"((\d{1,2})\s*/\s*(\d{1,2}))");
                std::smatch hp_match;
                const std::string hp_text = hp_ocr.get_result().text;
                if (std::regex_search(hp_text, hp_match, hp_re)) {
                    try {
                        hp_now = std::stoi(hp_match[1].str());
                    }
                    catch (...) {
                        hp_now = -1;
                    }
                }
            }
            if (hp_now > 0 && hp_now <= 30) {
                const int hp_old = m_config->status().hp;
                if (hp_now != hp_old) {
                    Log.info("Roguelike HP changed:", hp_old, "->", hp_now, "loss:", hp_old - hp_now);
                }
                m_config->status().hp = hp_now;
            }
        }
    }
    using namespace std::chrono_literals;
    LogTraceFunction;

    if (!calc_stage_info()) {
        return false;
    }

    if (!update_deployment(true)) {
        Log.error("update deployment failed");
        return false;
    }

    cache_oper_elite_status();
    speed_up();
    bool timeout = false;
    auto start_time = std::chrono::steady_clock::now();
    cv::Mat image, image_prev;
    while (!need_exit()) {
        // 不在战斗场景，且已使用过了干员，说明已经打完了，就结束循环
        image = ctrler()->get_image();
        if (!do_once(image, image_prev) && !m_first_deploy) {
            break;
        }
        image_prev = std::move(image);

        auto duration = std::chrono::steady_clock::now() - start_time;
        if (!timeout && duration > 8min) {
            timeout = true;
            Log.info("Timeout, retreat!");
            // 超时了，一般是某个怪被干员卡住了，一直不结束。
            all_melee_retreat();
        }
        else if (timeout && duration > 10min) {
            Log.info("Timeout again, abandon!");
            // 超时撤退了还一直卡着，只能放弃了
            abandon();
            break;
        }
    }
    // 通知战斗结束，无论成功与否
    auto info = basic_info_with_what("RoguelikeCombatEnd");
    callback(AsstMsg::SubTaskExtraInfo, info);
    return true;
}

void asst::RoguelikeBattleTaskPlugin::wait_until_start_button_clicked()
{
    ProcessTask(*this, { m_config->get_theme() + "@Roguelike@WaitForStartButtonClicked" })
        .set_task_delay(0)
        .set_retry_times(0)
        .run();
}

bool asst::RoguelikeBattleTaskPlugin::calc_stage_info()
{
    LogTraceFunction;

    wait_until_start_button_clicked();
    clear();

    bool calced = false;

    const auto stage_name_task_ptr = Task.get("BattleStageName");
    sleep(stage_name_task_ptr->pre_delay);

    auto start = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(Config.get_options().battle_start_timeout_seconds);

    while (std::chrono::steady_clock::now() - start < timeout) {
        if (need_exit()) {
            return false;
        }
        RegionOCRer name_analyzer(ctrler()->get_image());
        name_analyzer.set_task_info(stage_name_task_ptr);
        if (!name_analyzer.analyze()) {
            continue;
        }
        const std::string& text = name_analyzer.get_result().text;
        if (text.empty()) {
            continue;
        }

        static const std::vector<std::string> RoguelikeStageCode = { "ISW-NO",
                                                                     "ISW-DF",
                                                                     "ISW-DU",
                                                                     "ISW-SP",
                                                                     std::string() };
        TilePack::LevelKey stage_key;
        stage_key.name = text;

        for (const std::string& code : RoguelikeStageCode) {
            stage_key.code = code;
            if (!Tile.find(stage_key)) {
                continue;
            }
            calced = true;
            m_stage_name = text;
            m_map_data = TilePack::find_level(stage_key).value_or(Map::Level {});
            auto calc_result = TilePack::calc(m_map_data);
            m_normal_tile_info = std::move(calc_result.normal_tile_info);
            m_side_tile_info = std::move(calc_result.side_tile_info);
            m_retreat_button_pos = calc_result.retreat_button;
            m_skill_button_pos = calc_result.skill_button;
            break;
        }

        if (calced) {
            break;
        }
        std::this_thread::yield();
    }

    if (!calced) {
        callback(AsstMsg::SubTaskExtraInfo, basic_info_with_what("StageInfoError"));
        return false;
    }

    std::optional<CombatData> opt;
    if (m_config->get_mode() == RoguelikeMode::CLP_PDS) {
        opt = RoguelikeCopilot.get_stage_data(m_stage_name + "_collapse");
        if (opt == std::nullopt) {
            opt = RoguelikeCopilot.get_stage_data(m_stage_name);
        }
    }
    else {
        opt = RoguelikeCopilot.get_stage_data(m_stage_name);
    }

    if (opt) {
        m_homes = opt->replacement_home;
        m_allow_to_use_dice = opt->use_dice_stage;
        m_blacklist_location = opt->blacklist_location;
        m_role_order = opt->role_order;
        m_force_deploy_direction = opt->force_deploy_direction;
        m_force_air_defense = AirDefenseData { .stop_blocking_deploy_num = opt->stop_deploy_blocking_num,
                                               .deploy_air_defense_num = opt->force_deploy_air_defense_num,
                                               .ban_medic = opt->force_ban_medic };
        m_deploy_plan = opt->deploy_plan;
        m_retreat_plan = opt->retreat_plan;
    }
    if (m_homes.empty()) {
        for (const auto& [loc, side] : m_normal_tile_info) {
            if (side.key == TilePack::TileKey::Home) {
                m_homes.emplace_back(ReplacementHome { loc, battle::DeployDirection::None });
            }
        }
        m_allow_to_use_dice = true;
        m_role_order = {
            battle::Role::Warrior, battle::Role::Pioneer, battle::Role::Medic,
            battle::Role::Tank,    battle::Role::Sniper,  battle::Role::Caster,
            battle::Role::Support, battle::Role::Special, battle::Role::Drone,
        };
    }
    else {
        auto homes_pos = m_homes | std::views::transform(&ReplacementHome::location);
        auto invalid_homes_pos =
            homes_pos |
            std::views::filter([&](const auto& home_pos) { return !m_normal_tile_info.contains(home_pos); }) |
            std::views::transform(&Point::to_string);
        if (!invalid_homes_pos.empty()) {
            Log.error("No replacement homes point:", invalid_homes_pos);
        }
        Log.info("replacement home:", homes_pos | std::views::transform(&Point::to_string));
    }

    if (m_homes.empty()) {
        Log.error("Unknown home pos");
        return false;
    }
    m_homes_status.resize(m_homes.size());

    auto cb_info = basic_info_with_what("StageInfo");
    auto& details = cb_info["details"];
    details["name"] = m_stage_name;
    details["size"] = m_side_tile_info.size();
    callback(AsstMsg::SubTaskExtraInfo, cb_info);

    return true;
}

asst::battle::OperPosition asst::RoguelikeBattleTaskPlugin::get_role_position(const battle::Role& role) const
{
    switch (role) {
    case battle::Role::Support:
    case battle::Role::Sniper:
    case battle::Role::Caster:
        return battle::OperPosition::AirDefense;
        break;
    case battle::Role::Pioneer:
    case battle::Role::Warrior:
    case battle::Role::Tank:
        return battle::OperPosition::Blocking;
        break;
    case battle::Role::Medic:
        return m_force_air_defense.ban_medic ? battle::OperPosition::None : battle::OperPosition::AirDefense;
        break;
    case battle::Role::Special:
    case battle::Role::Drone:
    default:
        return battle::OperPosition::None;
        break;
    }
}

void asst::RoguelikeBattleTaskPlugin::cache_oper_elite_status()
{
    const auto& oper_list = m_config->status().opers;
    for (const auto& oper : m_cur_deployment_opers) {
        m_oper_elite.emplace(oper.name, oper_list.contains(oper.name) ? oper_list.at(oper.name).elite : 0);
    }
}

void asst::RoguelikeBattleTaskPlugin::set_position_full(battle::LocationType loc_type, bool full)
{
    switch (loc_type) {
    case battle::LocationType::Melee:
        m_melee_full = full;
        break;
    case battle::LocationType::Ranged:
        m_ranged_full = full;
        break;
    case battle::LocationType::All:
        m_melee_full = full;
        m_ranged_full = full;
        break;
    case battle::LocationType::Invalid:
    case battle::LocationType::None:
    default:
        break;
    }
}

void asst::RoguelikeBattleTaskPlugin::set_position_full(const Point& loc, bool full)
{
    if (auto tile_iter = m_normal_tile_info.find(loc); tile_iter != m_normal_tile_info.end()) {
        set_position_full(tile_iter->second.buildable, full);
    }
}

void asst::RoguelikeBattleTaskPlugin::set_position_full(const battle::DeploymentOper& oper, bool full)
{
    set_position_full(BattleData.get_location_type(oper.role, oper.name), full);
}

bool asst::RoguelikeBattleTaskPlugin::get_position_full(battle::LocationType loc_type) const
{
    switch (loc_type) {
    case battle::LocationType::Melee:
        return m_melee_full;
        break;
    case battle::LocationType::Ranged:
        return m_ranged_full;
        break;
    case battle::LocationType::All:
        return m_melee_full && m_ranged_full;
        break;
    case battle::LocationType::Invalid:
    case battle::LocationType::None:
    default:
        break;
    }
    return false;
}

bool asst::RoguelikeBattleTaskPlugin::get_position_full(const battle::DeploymentOper& oper) const
{
    return get_position_full(BattleData.get_location_type(oper.role, oper.name));
}

bool asst::RoguelikeBattleTaskPlugin::do_best_deploy()
{
    Log.info("deploy diag: cur_opers", m_cur_deployment_opers.size(), "homes", m_homes.size());
    LogTraceFunction;
    Log.info("m_kills", m_kills);

    bool is_success = false;
    for (const auto& info : m_retreat_plan) {
        if (m_kills >= info.kill_lower_bound && m_kills <= info.kill_upper_bound) {
            if (m_used_tiles.contains(info.location)) {
                retreat_oper(info.location);
                Log.info("retreat operator");
                return true;
            }
        }
    }
    Log.debug("No operator needs to be retreated.");
    // 构造当前地图的部署指令列表
    std::vector<DeployPlanInfo> deploy_plan_list;
    // 获取当前肉鸽的分组信息[干员组1名称,干员组2名称,...]
    const auto& groups = RoguelikeRecruit.get_group_names(m_config->get_theme());
    // 获取当前肉鸽的分组内排名信息
    // const auto& group_rank = RoguelikeRecruit.get_group_rank(rogue_theme);
    for (const auto& oper : m_cur_deployment_opers) {
        // 骰子等道具永远不在任何 deploy plan 里，直接跳过。
        // 否则每轮循环都会刷一条 "is not in ALL deploy plan"，并且它们还会被当成
        // 候选干员参与后续的部署链判断，干扰正常干员的部署次序。
        if (DiceSet.contains(oper.name)) {
            continue;
        }
        // 干员冷却中
        if (oper.cooling) {
            LogDebug << "operator" << oper.name << "is cooling now.";
            continue;
        }

        const battle::OperNameTag oper_tag { oper.role, oper.name };
        // 获取招募信息
        const auto& recruit_info = RoguelikeRecruit.get_oper_info(m_config->get_theme(), oper_tag);
        // 获取会用到该干员的干员组[干员组1序号,干员组2序号,...]
        std::vector<int> group_ids = RoguelikeRecruit.get_group_ids_of_oper(m_config->get_theme(), oper_tag);

        bool is_in_group = false;
        for (const auto& group_id : group_ids) {
            // 当前干员组名,string类型
            std::string group_name = groups[group_id];
            LogDebug << m_stage_name << "group_name" << group_name;
            if (m_deploy_plan.contains(group_name)) {
                is_in_group = true;
                for (const auto& info : m_deploy_plan[group_name]) {
                    if (m_kills < info.kill_lower_bound || m_kills > info.kill_upper_bound) {
                        LogDebug << "    deploy info" << oper.name << "in group" << group_name << "is waiting.";
                        is_success = true; // 如果发现了待命干员，此函数最终返回true
                        continue;
                    }

                    DeployPlanInfo deploy_plan;
                    deploy_plan.role = oper.role;
                    deploy_plan.oper_name = oper.name;
                    // deploy_plan.oper_priority = recruit_info.recruit_priority;
                    // deploy_plan.oper_order_in_group = recruit_info.order_in_group.at(group_id);

                    deploy_plan.oper_order_in_group =
                        recruit_info.order_in_group.empty() ? INT_MAX : recruit_info.order_in_group.at(group_id);
                    deploy_plan.rank = info.rank;
                    deploy_plan.placed = info.location;
                    deploy_plan.direction = info.direction;
                    deploy_plan_list.emplace_back(deploy_plan);
                    LogDebug << "    deploy info" << deploy_plan.oper_name << "is No. "
                             << deploy_plan.oper_order_in_group + 1 << "in group" << group_name
                             << ", with deploy command rank" << deploy_plan.rank;
                }
            }
            else {
                LogDebug << m_stage_name << "group_name" << group_name << "operator" << oper.name
                         << "is not in the deploy plan.";
            }
        }
        if (!is_in_group) {
            LogTrace << m_stage_name << "operator" << oper.name << "is not in ALL deploy plan.";
        }
    }
    std::sort(deploy_plan_list.begin(), deploy_plan_list.end());
    // 部署位预留策略：记录「优先级更高但费用不足」的待命项，
    // 非开局阶段遇到优先级更低的项时先跳过，把部署位留给待命项。
    int best_waiting_rank = INT_MAX;
    std::string best_waiting_name;
    const bool early_phase = m_kills < 30;
    // 保命优先：场上还没有医疗时，医疗干员不参与「留位」跳过，费用一够就下。
    // 依据（用户实测）：领地意识等图有远程/爆炸原石虫，没有医疗时前期就会漏怪。
    bool need_medic = true;
    for (const auto& [tag, _] : m_deployed_time) {
        if (tag.role == battle::Role::Medic) {
            need_medic = false;
            break;
        }
    }

    for (const auto& deploy_plan : deploy_plan_list) {
        if (!m_used_tiles.contains(deploy_plan.placed) &&
            !m_blacklist_location.contains(deploy_plan.placed) /* 判断该位置是否已被占据 */) {
            if (auto oper_it = std::ranges::find_if(
                    m_cur_deployment_opers,
                    [&](const auto& oper) {
                        return oper.role == deploy_plan.role && oper.name == deploy_plan.oper_name;
                    });
                oper_it == m_cur_deployment_opers.end() || !oper_it->available) { // 等费
                // 费用不够时继续尝试排名更低的干员，而不是直接放弃本次部署。
                // 否则高费干员排在 rank 1 时会把整条部署链卡死：既不放低费先锋回费，
                // 也永远等不到高费干员所需的费用，最终一路漏怪。
                Log.trace("    best deploy is", deploy_plan.oper_name, "with rank", deploy_plan.rank,
                          "but now waiting for cost, try next.");
                if (deploy_plan.rank < best_waiting_rank) {
                    best_waiting_rank = deploy_plan.rank;
                    best_waiting_name = deploy_plan.oper_name;
                }
                continue;
            }
            // 但也不能一味等：场上人数不足 3 时先把人放下，否则前期就漏怪。
            if (!early_phase && m_used_tiles.size() >= 3 && deploy_plan.rank > best_waiting_rank &&
                !(need_medic && deploy_plan.role == battle::Role::Medic)) {
                Log.info("reserve slot: skip", deploy_plan.oper_name, "rank", deploy_plan.rank,
                         "waiting for", best_waiting_name, "rank", best_waiting_rank);
                continue;
            }
            deploy_oper(deploy_plan.role, deploy_plan.oper_name, deploy_plan.placed, deploy_plan.direction);
            battle::OperNameTag oper_tag { deploy_plan.role, deploy_plan.oper_name };
            // 防止滑动丢失(有些物体比如鸟笼没有方向),点一下右下角费用那里
            asst::BattleHelper::cancel_oper_selection();
            // 开始计时
            auto deployed_time = std::chrono::steady_clock::now();
            m_deployed_time.insert_or_assign(oper_tag, deployed_time);
            // 获取技能用法和使用次数
            const auto& oper_info = RoguelikeRecruit.get_oper_info(m_config->get_theme(), oper_tag);
            m_skill_usage[oper_tag] = oper_info.skill_usage;
            m_skill_times[oper_tag] = oper_info.skill_times;
            Log.trace("    best deploy is", deploy_plan.oper_name, "with rank", deploy_plan.rank);
            return true;
        }
    }
    return is_success;
}

// 漏怪 / 崩盘诊断。
// 依据：
//   1)《敌人机制与 MAA 可观测性盘点》§4：战斗内可观测项只有「已击杀 / 敌人总数 / 费用」，
//      敌人血量、攻击类型（物理/法术/真实/元素）、攻击范围、元素损伤条、以及「村民」这类
//      中立单位全部不可读；因此元素损伤、村民扣血、精英/领袖识别在本架构下无法做反应式判断。
//   2)《水月肉鸽全地图机制盘点》§5：漏怪集中在窄图与多蓝门图的早期费用真空期。
// 实现取舍：本轮只「把看不见的变成看得见」，不在战斗内做任何会自动改变部署行为的判断，
//      以免在冲 N8 的过程中引入回归。心跳 30 秒一条、停滞告警每场最多一次，日志量可控。
void asst::RoguelikeBattleTaskPlugin::diagnose_battle_progress()
{
    const auto now = std::chrono::steady_clock::now();

    // 击杀数回退说明已进入新的一场战斗，重置统计窗口
    if (m_kills < m_diag_last_kills) {
        m_diag_battle_start = now;
        m_diag_last_kill_time = now;
        m_diag_last_kills = m_kills;
        m_diag_last_heartbeat = -1;
        m_diag_stall_warned = false;
        return;
    }
    if (m_diag_battle_start == std::chrono::steady_clock::time_point {}) {
        m_diag_battle_start = now;
        m_diag_last_kill_time = now;
    }
    if (m_kills != m_diag_last_kills) {
        m_diag_last_kills = m_kills;
        m_diag_last_kill_time = now;
        m_diag_stall_warned = false;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_diag_battle_start).count();
    const auto stalled = std::chrono::duration_cast<std::chrono::seconds>(now - m_diag_last_kill_time).count();

    // 心跳：每 30 秒一条，记录 地图 / 已用时间 / 已击杀 / 总数 / 剩余 / 费用
    if (elapsed >= 30 && static_cast<int>(elapsed / 30) != m_diag_last_heartbeat) {
        m_diag_last_heartbeat = static_cast<int>(elapsed / 30);
        Log.info("battle_progress", m_stage_name, "elapsed", elapsed, "kills", m_kills);
        Log.info("battle_progress", m_stage_name, "total", m_total_kills, "remain", m_total_kills - m_kills,
                 "cost", m_cost);
    }

    // 停滞告警：45 秒没有任何击杀，基本等价于「打不动 / 已经漏怪 / 输出不足」
    if (!m_diag_stall_warned && elapsed >= 45 && stalled >= 45) {
        m_diag_stall_warned = true;
        Log.warn("battle_stalled", m_stage_name, "no_kill_for", stalled, "s", "kills", m_kills,
                 "total", m_total_kills, "cost", m_cost);
    }
}

bool asst::RoguelikeBattleTaskPlugin::do_once(const cv::Mat& image, const cv::Mat& image_prev)
{
    // 每 5 秒输出一次战斗状态快照，定位漏怪时的可用兵力
    {
        static auto last_snap = std::chrono::steady_clock::now();
        const auto now_ts = std::chrono::steady_clock::now();
        if (now_ts - last_snap >= std::chrono::seconds(5)) {
            last_snap = now_ts;
            Log.info(
                "battle snap: avail",
                m_cur_deployment_opers.size(),
                "kills",
                m_kills);
        }
    }
    check_drone_tiles();

    // 注：战斗界面无「目标生命值」显示，血量采集已移至地图阶段（见 TODO）

    thread_local auto prev_frame_time = std::chrono::steady_clock::time_point {};
    static const auto min_frame_interval =
        std::chrono::milliseconds(Config.get_options().roguelike_fight_screencap_interval);

    // prevent our program from consuming too much CPU
    if (const auto now = std::chrono::steady_clock::now(); prev_frame_time > now - min_frame_interval) [[unlikely]] {
        Log.debug("Sleeping for framerate limit");
        std::this_thread::sleep_for(min_frame_interval - (now - prev_frame_time));
    }

    prev_frame_time = std::chrono::steady_clock::now();

    if (!m_first_deploy) {
        use_all_ready_skill(image);
    }

    std::unordered_set<std::string> pre_cooling;
    for (const auto& oper : m_cur_deployment_opers) {
        if (oper.cooling) {
            pre_cooling.emplace(oper.name);
            m_deployed_time.erase({ oper.role, oper.name });
        }
    }

    auto pre_battlefield = m_battlefield_opers;
    for (const auto& [oper_tag, loc] : pre_battlefield) {
        const auto& oper_info = RoguelikeRecruit.get_oper_info(m_config->get_theme(), oper_tag);
        auto iter = m_deployed_time.find(oper_tag);
        if (iter != m_deployed_time.end() && oper_info.auto_retreat > 0) {
            if (std::chrono::steady_clock::now() - m_deployed_time.at(oper_tag) >=
                oper_info.auto_retreat * std::chrono::seconds(1)) {
                // 时间到了就撤退
                asst::BattleHelper::retreat_oper(oper_tag.role, oper_tag.name);
                m_deployed_time.erase(oper_tag);
            }
        }
    }
    pre_battlefield = m_battlefield_opers;

    if (!update_deployment(false, image)) {
        return false;
    }
    update_cost(image, image_prev);
    update_kills(image, image_prev);
    // 漏怪 / 崩盘诊断：只记录，不干预部署
    diagnose_battle_progress();

    std::unordered_set<std::string> cur_cooling;
    size_t cur_available_count = 0;   // without drones
    size_t cur_deployments_count = 0; // without drones
    for (const auto& oper : m_cur_deployment_opers) {
        if (oper.role == Role::Drone) {
            continue;
        }

        ++cur_deployments_count;
        if (oper.cooling) {
            cur_cooling.emplace(oper.name);
            m_deployed_time.erase({ oper.role, oper.name });
        }
        if (oper.available) {
            ++cur_available_count;
        }
    }

    if (do_best_deploy()) { // 这是新的部署逻辑，更加精确
        m_first_deploy = false;
        return true;
    }

    else { // 这是旧的、通用部署逻辑
        auto urgent_home_opt = check_urgent(pre_cooling, cur_cooling, pre_battlefield);

        battle::DeploymentOper best_oper;
        bool best_oper_is_dice = false;
        size_t normal_home_index = m_cur_home_index;
        if (!urgent_home_opt) {
            // 不要随便谁好了就上，等等费用，下点厉害的干员
            // TODO: 这个逻辑目前太简单了，待优化
            // 原实现用 m_homes.size()（蓝门数量）当「应该部署几个人」的阈值，语义不成立：
            // 蓝门多不代表该多下人，蓝门少反而会永远觉得不够。改用场上人数的绝对下限。
            bool battlefield_enough = m_battlefield_opers.size() >= 4;
            bool available_too_few = cur_available_count <= cur_deployments_count / 2;
            bool not_too_many_cooling = cur_cooling.size() < cur_available_count;

            if (battlefield_enough && available_too_few && not_too_many_cooling) {
                Log.info("wait a minute");
                return true;
            }
        }
        else {
            m_cur_home_index = *urgent_home_opt;

            if (m_allow_to_use_dice) {
                auto dice_key_iter = std::ranges::find_if(m_cur_deployment_opers, [&](const auto& oper) {
                    return DiceSet.contains(oper.name);
                });
                if (dice_key_iter != m_cur_deployment_opers.end()) {
                    best_oper_is_dice = true;
                    best_oper = *dice_key_iter;
                }
            }
        }

        Log.info("To path", m_cur_home_index);

        if (!update_deployment(false, image, true)) {
            return false;
        }

        if (!best_oper_is_dice) {
            if (auto best_oper_opt = calc_best_oper()) {
                best_oper = *best_oper_opt;
            }
            else {
                return true;
            }
        }

        // 计算最优部署位置及方向
        auto best_loc_opt = calc_best_loc(best_oper);
        if (!best_loc_opt) {
            Log.info("Tiles full while calc best plan.");
            set_position_full(best_oper, true);
            return true;
        }
        const auto& [placed_loc, direction] = *best_loc_opt;

        deploy_oper(best_oper.role, best_oper.name, placed_loc, direction);
        postproc_of_deployment_conditions(best_oper, placed_loc, direction);

        battle::OperNameTag oper_tag { best_oper.role, best_oper.name };
        m_first_deploy = false;
        // 开始计时
        auto deployed_time = std::chrono::steady_clock::now();
        m_deployed_time.insert_or_assign(oper_tag, deployed_time);
        // 获取技能用法和使用次数
        const auto& oper_info = RoguelikeRecruit.get_oper_info(m_config->get_theme(), oper_tag);
        m_skill_usage[oper_tag] = oper_info.skill_usage;
        m_skill_times[oper_tag] = oper_info.skill_times;

        if (urgent_home_opt) {
            m_urgent_home_index.pop_front();
            m_cur_home_index = normal_home_index;
        }
        ++m_cur_home_index;
        if (m_cur_home_index >= m_homes.size()) {
            m_cur_home_index = 0;
        }
    }

    return true;
}

void asst::RoguelikeBattleTaskPlugin::postproc_of_deployment_conditions(
    const battle::DeploymentOper& oper,
    const Point& placed_loc,
    battle::DeployDirection direction)
{
    // 如果是医疗干员，判断覆盖范围内有无第一次放置的干员
    if (oper.role == battle::Role::Medic) {
        std::vector<size_t> contain_index;
        for (const Point& relative_pos : get_attack_range(oper, direction)) {
            Point absolute_pos = placed_loc + relative_pos;
            if (auto iter = m_blocking_for_home_index.find(absolute_pos); iter != m_blocking_for_home_index.end()) {
                m_homes_status[iter->second].wait_medic = false;
                contain_index.emplace_back(iter->second);
            }
        }
        if (!contain_index.empty()) {
            m_medic_for_home_index.emplace(placed_loc, std::move(contain_index));
        }

        m_homes_status[m_cur_home_index].wait_medic = false;
    }

    auto position = get_role_position(oper.role);
    switch (position) {
    case OperPosition::Blocking: {
        m_force_air_defense.has_deployed_blocking_num++;
        bool& wait_blocking = m_homes_status[m_cur_home_index].wait_blocking;
        if (wait_blocking) {
            wait_blocking = false;
            m_blocking_for_home_index.emplace(placed_loc, m_cur_home_index);
        }
    } break;
    case OperPosition::AirDefense:
        if (!m_force_air_defense.has_finished_deploy_air_defense) {
            m_force_air_defense.has_deployed_air_defense_num++;
            if (m_force_air_defense.has_deployed_air_defense_num >= m_force_air_defense.deploy_air_defense_num) {
                m_force_air_defense.has_finished_deploy_air_defense = true;
                Log.info("FORCE RANGED OPER DEPLOY END");
            }
        }
        break;
    default:
        break;
    }

    if (m_force_air_defense.has_finished_deploy_air_defense && position != OperPosition::AirDefense) {
        Log.info("FORCE RANGED OPER DEPLOY END");
        m_force_air_defense.has_finished_deploy_air_defense = true;
    }
}

// 将存在于场上超过限时的召唤物所在的地块重新设为可用
void asst::RoguelikeBattleTaskPlugin::check_drone_tiles()
{
    Time_Point now_time = std::chrono::system_clock::now();
    while ((!m_need_clear_tiles.empty()) && m_need_clear_tiles.top().placed_time < now_time) {
        const auto& placed_loc = m_need_clear_tiles.top().placed_loc;
        if (auto iter = m_used_tiles.find(placed_loc); iter != m_used_tiles.end()) {
            Log.info("Drone at location (", placed_loc.x, ",", placed_loc.y, ") is recognized as retreated");
            set_position_full(placed_loc, false);
            m_battlefield_opers.erase(iter->second);
            m_used_tiles.erase(iter);
        }
        m_need_clear_tiles.pop();
    }
}

std::optional<size_t> asst::RoguelikeBattleTaskPlugin::check_urgent(
    const std::unordered_set<std::string>& pre_cooling,
    const std::unordered_set<std::string>& cur_cooling,
    const std::map<battle::OperNameTag, Point>& pre_battlefield)
{
    std::vector<size_t> new_urgent;
    for (const auto& name : cur_cooling) {
        if (pre_cooling.find(name) != pre_cooling.cend()) {
            continue;
        }
        LogInfo << name << "retreated";
        auto pre_loc_iter =
            std::ranges::find_if(pre_battlefield, [&](const auto& pair) { return pair.first.name == name; });
        if (pre_loc_iter == pre_battlefield.cend()) {
            Log.error("the oper", name, "was not on the battlefield before");
            continue;
        }
        Point pre_loc = pre_loc_iter->second;

        if (auto del_loc_blocking = m_blocking_for_home_index.find(pre_loc);
            del_loc_blocking != m_blocking_for_home_index.end()) {
            Log.info("Urgent situation detected");

            size_t home_index = del_loc_blocking->second;
            m_homes_status[home_index].wait_blocking = true;
            new_urgent.emplace_back(home_index);
            m_blocking_for_home_index.erase(del_loc_blocking);
        }
        else if (
            auto del_loc_medic = m_medic_for_home_index.find(pre_loc); del_loc_medic != m_medic_for_home_index.end()) {
            for (const size_t& home_index : del_loc_medic->second) {
                m_homes_status[home_index].wait_medic = true;
            }
            m_medic_for_home_index.erase(del_loc_medic);
        }
        set_position_full(pre_loc, false);
    }

    // 同时出现了多个家门的紧急情况，index 序号小的是靠前的家门，优先入栈
    std::ranges::sort(new_urgent);
    for (const size_t& home_index : new_urgent) {
        if (std::ranges::find(m_urgent_home_index, home_index) != m_urgent_home_index.cend()) {
            continue;
        }
        m_urgent_home_index.emplace_back(home_index);
    }

    if (m_urgent_home_index.empty()) {
        return std::nullopt;
    }
    return m_urgent_home_index.front();
}

std::optional<asst::battle::DeploymentOper> asst::RoguelikeBattleTaskPlugin::calc_best_oper() const
{
    bool has_medic = false;
    bool has_blocking = false;
    bool has_air_defense = false;
    for (const auto& oper : m_cur_deployment_opers) {
        const auto& role = oper.role;
        switch (role) {
        case Role::Medic:
            has_medic = true;
            break;
        default:
            break;
        }

        const auto& position = get_role_position(role);
        switch (position) {
        case OperPosition::Blocking:
            has_blocking = true;
            break;
        case OperPosition::AirDefense:
            has_air_defense = true;
            break;
        default:
            break;
        }

        // found all
        if (has_medic && has_blocking && has_air_defense) {
            break;
        }
    }

    bool use_air_defense =
        has_air_defense && !m_force_air_defense.has_finished_deploy_air_defense &&
        m_force_air_defense.has_deployed_blocking_num >= m_force_air_defense.stop_blocking_deploy_num && !m_ranged_full;
    bool use_blocking = has_blocking && m_homes_status[m_cur_home_index].wait_blocking && !m_melee_full;
    bool use_medic = has_medic && m_homes_status[m_cur_home_index].wait_medic && !m_ranged_full;
    Log.trace("use_air_defense", use_air_defense, ", use_blocking", use_blocking, ", use_medic", use_medic);

    std::vector<DeploymentOper> cur_available;
    for (const auto& oper : m_cur_deployment_opers) {
        if (oper.available) {
            cur_available.emplace_back(oper);
        }
    }
    // 费用高的优先用，放前面
    std::ranges::sort(cur_available, [](const auto& lhs, const auto& rhs) { return lhs.cost > rhs.cost; });

    DeploymentOper best_oper;

    for (auto role : m_role_order) {
        battle::OperPosition position = get_role_position(role);
        if (use_air_defense) {
            if (position != battle::OperPosition::AirDefense) {
                continue;
            }
        }
        else if (use_blocking) {
            if (position != battle::OperPosition::Blocking) {
                continue;
            }
        }
        else if (use_medic) {
            if (role != battle::Role::Medic) {
                continue;
            }
        }

        for (const battle::DeploymentOper& oper : cur_available) {
            if (oper.role == role && !DiceSet.contains(oper.name) && !get_position_full(oper)) {
                best_oper = oper;
                break;
            }
        }
        if (!best_oper.name.empty()) {
            break;
        }
    }
    // 2026-10-26 移除「随机兑底」：职业组全不匹配时强行放人，是无效站位的主要来源
    // （会把近卫放到高台、把医疗放到没怪的路径上）。没有匹配就不放。
    if (best_oper.name.empty()) {
        Log.info("No best oper");
        return std::nullopt;
    }
    Log.info("best oper is", best_oper.name, "with cost being", best_oper.cost);
    return best_oper;
}

void asst::RoguelikeBattleTaskPlugin::all_melee_retreat()
{
    std::vector<Point> retreat_locs {};
    for (const auto& loc : m_used_tiles | std::views::keys) {
        auto& tile_info = m_normal_tile_info[loc];
        auto& type = tile_info.buildable;
        if (type == battle::LocationType::Melee || type == battle::LocationType::All) {
            retreat_locs.push_back(loc);
        }
    }
    for (const auto& loc : retreat_locs) {
        retreat_oper(loc);
    }
}

void asst::RoguelikeBattleTaskPlugin::clear()
{
    BattleHelper::clear();
    m_stage_name.clear();

    m_homes.clear();
    m_allow_to_use_dice = true;
    m_blacklist_location.clear();
    m_force_deploy_direction.clear();
    m_force_air_defense = decltype(m_force_air_defense)();

    m_cur_home_index = 0;
    m_first_deploy = true;
    m_melee_full = false;
    m_ranged_full = false;
    m_homes_status.clear();
    m_blocking_for_home_index.clear();
    m_medic_for_home_index.clear();
    m_urgent_home_index = decltype(m_urgent_home_index)();
    m_need_clear_tiles = decltype(m_need_clear_tiles)();
    m_deploy_plan.clear();
    m_retreat_plan.clear();
    m_deployed_time.clear();

    m_oper_elite.clear();
}

std::vector<asst::Point> asst::RoguelikeBattleTaskPlugin::available_locations(const DeploymentOper& oper) const
{
    auto type = BattleData.get_location_type(oper.role, oper.name);
    if (type == LocationType::Invalid || type == LocationType::None) {
        return available_locations(get_role_usual_location(oper.role));
    }
    return available_locations(type);
}

std::vector<asst::Point> asst::RoguelikeBattleTaskPlugin::available_locations(battle::LocationType type) const
{
    std::vector<Point> result;
    for (const auto& [loc, tile] : m_normal_tile_info) {
        bool position_matched = tile.buildable == type || tile.buildable == battle::LocationType::All;
        position_matched |= (type == battle::LocationType::All) && (tile.buildable == battle::LocationType::Melee ||
                                                                    tile.buildable == battle::LocationType::Ranged);
        if (position_matched &&
            tile.key != TilePack::TileKey::DeepSea && // 水上要先放板子才能放人，肉鸽里也没板子，那就当作不可放置
            !m_used_tiles.contains(loc) && !m_blacklist_location.contains(loc)) {
            result.emplace_back(loc);
        }
    }
    return result;
}

asst::battle::AttackRange asst::RoguelikeBattleTaskPlugin::get_attack_range(
    const battle::DeploymentOper& oper,
    DeployDirection direction) const
{
    int64_t elite = 0;
    if (m_oper_elite.contains(oper.name)) {
        elite = m_oper_elite.at(oper.name);
    }
    battle::AttackRange right_attack_range = BattleData.get_range(oper.role, oper.name, elite);

    if (right_attack_range == BattleDataConfig::EmptyRange) {
        switch (oper.role) {
        case battle::Role::Support:
            right_attack_range = {
                Point(-1, -1), Point(0, -1), Point(1, -1), Point(2, -1), //
                Point(-1, 0),  Point(0, 0),  Point(1, 0),  Point(2, 0),  //
                Point(-1, 1),  Point(0, 1),  Point(1, 1),  Point(2, 1),  //
            };
            break;
        case battle::Role::Caster:
            right_attack_range = {
                Point(0, -1), Point(1, -1), Point(2, -1),              //
                Point(0, 0),  Point(1, 0),  Point(2, 0),  Point(3, 0), //
                Point(0, 1),  Point(1, 1),  Point(2, 1),               //
            };
            break;
        case battle::Role::Medic:
        case battle::Role::Sniper:
            right_attack_range = {
                Point(0, -1), Point(1, -1), Point(2, -1), Point(3, -1), //
                Point(0, 0),  Point(1, 0),  Point(2, 0),  Point(3, 0),  //
                Point(0, 1),  Point(1, 1),  Point(2, 1),  Point(3, 1),  //
            };
            break;
        case battle::Role::Warrior:
            right_attack_range = { Point(0, 0), Point(1, 0), Point(2, 0) };
            break;
        case battle::Role::Special:
        case battle::Role::Tank:
        case battle::Role::Pioneer:
        case battle::Role::Drone:
            right_attack_range = { Point(0, 0), Point(1, 0) };
            break;
        default:
            break;
        }
    }

    for (auto cur_direction :
         { DeployDirection::Right, DeployDirection::Up, DeployDirection::Left, DeployDirection::Down }) {
        if (cur_direction == direction) {
            break;
        }

        // rotate relative attack range counterclockwise
        for (Point& point : right_attack_range) {
            point = { point.y, -point.x };
        }
    }

    return right_attack_range;
}

std::optional<asst::RoguelikeBattleTaskPlugin::DeployInfo>
    asst::RoguelikeBattleTaskPlugin::calc_best_loc(const battle::DeploymentOper& oper) const
{
    size_t home_index = m_cur_home_index;
    if (home_index >= m_homes.size()) {
        Log.warn("home index is out of range", m_cur_home_index, m_homes.size());
        home_index = 0;
    }
    const ReplacementHome& home = m_homes[home_index];

    auto comp_dist = [&](const Point& lhs, const Point& rhs) -> bool {
        int lhs_y_dist = std::abs(lhs.y - home.location.y);
        int lhs_dist = std::abs(lhs.x - home.location.x) + lhs_y_dist;
        int rhs_y_dist = std::abs(rhs.y - home.location.y);
        int rhs_dist = std::abs(rhs.x - home.location.x) + rhs_y_dist;
        // 距离一样选择 x 轴上的，因为一般的地图都是横向的长方向
        return lhs_dist == rhs_dist ? lhs_y_dist < rhs_y_dist : lhs_dist < rhs_dist;
    };
    std::vector<Point> available_loc = available_locations(oper);
    if (available_loc.empty()) {
        Log.error("No available locations");
        return std::nullopt;
    }
    // 把所有可用的点按距离排个序
    std::ranges::sort(available_loc, comp_dist);

    if (DiceSet.contains(oper.name)) {
        return DeployInfo { available_loc.back(), DeployDirection::None };
    }

    Point best_location;
    DeployDirection best_direction = DeployDirection::None;
    int max_score = INT_MIN;
    int best_raw_score = INT_MIN; // 不含距离惩罚的原始覆盖分，用于识别「无处可打」的盲放

    const auto& near_loc = available_loc.front();
    int min_dist = std::abs(near_loc.x - home.location.x) + std::abs(near_loc.y - home.location.y);

    // 取距离最近的N个点，计算分数。然后使用得分最高的点
    constexpr int CalcPointCount = 4;
    for (const auto& loc : available_loc | std::views::take(CalcPointCount)) {
        const auto& [cur_direction, cur_score] = calc_best_direction_and_score(loc, oper, home.direction);
        // 离得远的要扣分
        constexpr int DistWeights = -1050;
        int extra_dist = std::abs(loc.x - home.location.x) + std::abs(loc.y - home.location.y) - min_dist;
        int extra_dist_score = DistWeights * extra_dist;
        if (oper.role == battle::Role::Medic) { // 医疗干员离得远无所谓
            extra_dist_score = 0;
        }

        if (cur_score + extra_dist_score > max_score) {
            max_score = cur_score + extra_dist_score;
            best_raw_score = cur_score;
            best_location = loc;
            best_direction = cur_direction;
        }
    }

    // 强制变化为确定的攻击方向
    if (auto iter = m_force_deploy_direction.find(best_location); iter != m_force_deploy_direction.end()) {
        if (iter->second.role.contains(oper.role)) {
            best_direction = iter->second.direction;
        }
    }

    // 2026-10-26 盲放抑制：场上已有 >=2 人，而最近 4 个候选点的原始覆盖分都不为正，
    // 说明这些位置打不到任何敌人，放出去就是浪费费用和部署位（用户反馈的「瞎放」）。
    if (best_raw_score <= 0 && m_used_tiles.size() >= 2) {
        Log.info("skip blind deploy: no positive coverage, best raw score", best_raw_score);
        return std::nullopt;
    }

    return DeployInfo { best_location, best_direction };
}

asst::RoguelikeBattleTaskPlugin::DirectionAndScore asst::RoguelikeBattleTaskPlugin::calc_best_direction_and_score(
    Point loc,
    const battle::DeploymentOper& oper,
    DeployDirection recommended_direction) const
{
    LogTraceFunction;

    size_t home_index = m_cur_home_index;
    if (home_index >= m_homes.size()) {
        Log.warn("home index is out of range", m_cur_home_index, m_homes.size());
        home_index = 0;
    }
    Point home_loc = m_homes[home_index].location;

    DeployDirection base_direction = DeployDirection::None;
    if (loc.x == home_loc.x) {
        if (loc.y - home_loc.y >= 0) {
            base_direction = DeployDirection::Up;
        }
        else {
            base_direction = DeployDirection::Down;
        }
    }
    else {
        if (loc.x - home_loc.x >= 0) {
            base_direction = DeployDirection::Right;
        }
        else {
            base_direction = DeployDirection::Left;
        }
    }

    // 家门反过来
    DeployDirection home_direction = DeployDirection::None;
    switch (base_direction) {
    case DeployDirection::Right:
        home_direction = DeployDirection::Left;
        break;
    case DeployDirection::Up:
        home_direction = DeployDirection::Down;
        break;
    case DeployDirection::Left:
        home_direction = DeployDirection::Right;
        break;
    case DeployDirection::Down:
        home_direction = DeployDirection::Up;
        break;
    default:
        break;
    }

    // 医疗优先朝向家门
    if (oper.role == battle::Role::Medic) {
        base_direction = home_direction;
    }

    int max_score = 0;
    DeployDirection best_direction = DeployDirection::None;

    for (auto direction :
         { DeployDirection::Right, DeployDirection::Up, DeployDirection::Left, DeployDirection::Down }) {
        int score = 0;
        // 按朝右算，后面根据方向做转换
        for (const Point& relative_pos : get_attack_range(oper, direction)) {
            Point absolute_pos = loc + relative_pos;

            using TileKey = TilePack::TileKey;
            // 战斗干员朝向的权重
            static const std::unordered_map<TileKey, int> TileKeyFightWeights = {
                { TileKey::Invalid, 0 },    { TileKey::Forbidden, 0 },  { TileKey::Wall, 500 },
                { TileKey::Road, 1000 },    { TileKey::Home, 500 },     { TileKey::EnemyHome, 1000 },
                { TileKey::Green, 0 },      { TileKey::Airport, 1000 }, { TileKey::Floor, 1000 },
                { TileKey::Hole, 0 },       { TileKey::Telin, 700 },    { TileKey::Telout, 800 },
                { TileKey::Grass, 500 },    { TileKey::DeepSea, 1000 }, { TileKey::Volcano, 1000 },
                { TileKey::Healing, 1000 }, { TileKey::Fence, 800 },    { TileKey::Infection, 100 },
            };
            // 治疗干员朝向的权重
            static const std::unordered_map<TileKey, int> TileKeyMedicWeights = {
                { TileKey::Invalid, 0 },    { TileKey::Forbidden, 0 }, { TileKey::Wall, 1000 },
                { TileKey::Road, 1000 },    { TileKey::Home, 0 },      { TileKey::EnemyHome, 0 },
                { TileKey::Green, 0 },      { TileKey::Airport, 0 },   { TileKey::Floor, 0 },
                { TileKey::Hole, 0 },       { TileKey::Telin, 0 },     { TileKey::Telout, 0 },
                { TileKey::Grass, 500 },    { TileKey::DeepSea, 0 },   { TileKey::Volcano, 1000 },
                { TileKey::Healing, 1000 }, { TileKey::Fence, 1000 },  { TileKey::Infection, 1000 },
            };

            switch (oper.role) {
            case battle::Role::Medic:
                if (auto iter = m_used_tiles.find(absolute_pos);
                    iter != m_used_tiles.cend() &&
                    iter->second.role != battle::Role::Drone) { // 根据哪个方向上人多决定朝向哪
                    score += 10'000;
                }
                if (auto iter = m_side_tile_info.find(absolute_pos); iter == m_side_tile_info.end()) {
                }
                else if (auto it = TileKeyMedicWeights.find(iter->second.key); it != TileKeyMedicWeights.end()) {
                    score += it->second;
                }
                break;
            default:
                if (auto iter = m_side_tile_info.find(absolute_pos); iter == m_side_tile_info.end()) {
                }
                else if (auto it = TileKeyFightWeights.find(iter->second.key); it != TileKeyFightWeights.end()) {
                    score += it->second;
                }
                break;
            }
        }

        if (direction == base_direction) {
            score += 300;
        }
        if (oper.role != battle::Role::Medic && direction == home_direction) {
            score -= 500;
        }
        if (oper.role != battle::Role::Medic && direction == recommended_direction) {
            score += 2000;
        }
        if (score > max_score) {
            max_score = score;
            best_direction = direction;
        }
    }

    return { best_direction, max_score };
}
