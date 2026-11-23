#pragma once

#include <memory>
#include <optional>
#include <queue>
#include <stack>

#include "AbstractRoguelikeTaskPlugin.h"
#include "Common/AsstBattleDef.h"
#include "Common/AsstTypes.h"
#include "Config/Miscellaneous/TilePack.h"
#include "MaaUtils/NoWarningCVMat.hpp"
#include "Task/BattleHelper.h"

namespace asst
{
namespace blackflow
{
class BlackFlowHunterPreludeController;
class BlackFlowSoulObserver;
class BlackFlowSummonRetreat;
} // namespace blackflow

class RoguelikeBattleTaskPlugin : public AbstractRoguelikeTaskPlugin, private BattleHelper
{
    using Time_Point = std::chrono::time_point<std::chrono::system_clock>;

    inline static const std::unordered_set<std::string> DiceSet = { "骰子", "8面骰子", "12面骰子" };

public:
    RoguelikeBattleTaskPlugin(
        const AsstCallback& callback,
        Assistant* inst,
        std::string_view task_chain,
        const std::shared_ptr<RoguelikeConfig>& roguelike_config_ptr,
        const std::shared_ptr<RoguelikeControlTaskPlugin>& control);
    virtual ~RoguelikeBattleTaskPlugin() override;

    virtual bool verify(AsstMsg msg, const json::value& details) const override;
    bool load_params(const json::value& params) override;

protected:
    virtual bool _run() override;

    virtual AbstractTask& this_task() override { return *this; }

    bool verify_deployment_enabled() const override { return true; }

    bool deployment_target_allowed(const battle::DeploymentOper& oper, const Point& loc) const override;
    void set_dynamic_tile_observation(std::unordered_map<Point, battle::TileAvailability> observation);

    battle::DeploymentKind deployment_kind(const battle::DeploymentOper& oper) const override
    {
        return DiceSet.contains(oper.name) ? battle::DeploymentKind::Consumable : BattleHelper::deployment_kind(oper);
    }

    virtual void clear() override;
    bool do_once(cv::Mat& image, const cv::Mat& image_prev);

    struct DeployPlanInfo
    {
        // int oper_priority;                         // 干员招募优先级
        battle::Role role = battle::Role::Unknown; // 干员职业
        std::string oper_name;                     // 干员名称
        int oper_order_in_group {};                // 干员在干员组中排名
        int rank {};                               // 干员组在部署指令扁平化后的排名
        int plan_index = 0;
        int preferred_coverage = 0;
        Point placed;                         // 指令坐标
        battle::DeployDirection direction {}; // 部署朝向
        int cost = 0;                         // 干员部署费用，0 表示未识别
        bool prefer_group_order = false;

        bool operator<(const DeployPlanInfo& x) const
        {
            if (plan_index == x.plan_index && preferred_coverage != x.preferred_coverage) {
                return preferred_coverage > x.preferred_coverage;
            }
            if (rank != x.rank) {
                return rank < x.rank;
            }
            // Keep the tactical preference in the ordering key so mixed candidates
            // retain a strict weak ordering rather than using a pairwise exception.
            if (prefer_group_order != x.prefer_group_order) {
                return prefer_group_order;
            }
            if ((cost > 0) != (x.cost > 0)) {
                return cost > 0;
            }
            if (prefer_group_order && oper_order_in_group != x.oper_order_in_group) {
                return oper_order_in_group < x.oper_order_in_group;
            }
            // 未显式指定组内顺序时，同 rank（同一落点位）内先比费用，再比组内排名。
            // 依据：实测 2511 条 `best deploy is X with rank N`，同一 rank
            // 上维什戴尔（25+ 费） 与史都华德（19
            // 费）交替出现、每轮都是贵的那位先被选中然后 `waiting for cost` 空转。
            // 原因是组内排名 oper_order_in_group 排在费用前面，而两人的组内排名不同，
            // 费用永远轮不到比较。同一位配多个组时，「谁能先落地」比「作者把谁排在组内第几」更关键。
            // Unknown cost sorts after known positive costs; pairwise conditional
            // comparisons would violate strict weak ordering when three candidates
            // differ.
            const int known_cost = cost > 0 ? cost : INT_MAX;
            const int other_known_cost = x.cost > 0 ? x.cost : INT_MAX;
            if (known_cost != other_known_cost) {
                return known_cost < other_known_cost;
            }
            if (oper_order_in_group != x.oper_order_in_group) {
                return oper_order_in_group < x.oper_order_in_group;
            }
            return false;
        }
    };

    bool do_best_deploy(bool allow_soul_inspection = true);
    static bool deployment_cost_wait_required(const battle::DeploymentOper& oper, int observed_cost);
    std::unordered_set<std::string> reconcile_returned_blackflow_operators();
    bool deployment_requirements_met(
        const battle::DeploymentOper& oper,
        const battle::roguelike::DeployInfoWithRank& plan) const;
    int preferred_deployment_coverage(
        const battle::DeploymentOper& oper,
        const battle::roguelike::DeployInfoWithRank& plan) const;
    bool observe_hunter_prelude(const cv::Mat& image, bool normal_battle = true);
    bool observe_blackflow_souls(std::optional<Point> required_tile = std::nullopt, cv::Mat* handoff_frame = nullptr);
    bool calc_stage_info();

    void all_melee_retreat();

    std::optional<size_t> check_urgent(
        const std::unordered_set<std::string>& pre_cooling,
        const std::unordered_set<std::string>& cur_cooling,
        const std::map<battle::OperNameTag, Point>& pre_battlefield);
    std::optional<battle::DeploymentOper> calc_best_oper(bool urgent_home, bool cost_observed) const;

    struct DeployInfo
    {
        Point placed;
        battle::DeployDirection direction;
    };

    std::optional<DeployInfo> calc_best_loc(const battle::DeploymentOper& oper) const;
    battle::DeployDirection resolve_deploy_direction(
        const battle::DeploymentOper& oper,
        const Point& location,
        battle::DeployDirection proposed) const;
    battle::AttackRange get_attack_range(
        const battle::DeploymentOper& oper,
        battle::DeployDirection direction = battle::DeployDirection::Right) const;

    struct DirectionAndScore
    {
        battle::DeployDirection direction;
        int score = 0;
    };

    DirectionAndScore calc_best_direction_and_score(
        Point loc,
        const battle::DeploymentOper& oper,
        battle::DeployDirection recommended_direction) const;

    void postproc_of_deployment_conditions(
        const battle::DeploymentOper& oper,
        const Point& placed_loc,
        battle::DeployDirection direction);

    void check_drone_tiles();
    bool wait_until_start_button_clicked();
    void observe_hunter_after_deployment();
    std::string m_resume_stage;

    std::vector<Point> available_locations(const battle::DeploymentOper& oper) const;
    std::vector<Point> available_locations(battle::LocationType type) const;
    bool get_position_full(const battle::DeploymentOper& oper) const;
    bool get_position_full(battle::LocationType loc_type) const;
    void set_position_full(const battle::DeploymentOper& oper, bool full);
    void set_position_full(battle::LocationType loc_type, bool full);
    void set_position_full(const Point& loc, bool full);
    battle::OperPosition get_role_position(const battle::Role& role) const;
    battle::OperPosition get_oper_position(const battle::OperNameTag& oper) const;
    bool persistent_blocking_available(const battle::OperNameTag& oper) const;

    /* from config */

    std::vector<battle::roguelike::ReplacementHome> m_homes;
    bool m_allow_to_use_dice = true;
    bool m_deploy_plan_only = false;
    bool m_reserve_deploy_cost = false;
    std::unique_ptr<blackflow::BlackFlowHunterPreludeController> m_hunter_prelude;
    bool m_hunter_main_allowed = false;
    std::uint64_t m_hunter_observation_sequence = 0;
    bool m_waiting_for_manual_resume = false;
    std::unordered_map<int, int> m_plan_deployment_counts;
    std::unordered_set<Point> m_blacklist_location;
    std::unordered_map<Point, battle::TileAvailability> m_dynamic_tile_observation;
    bool m_dynamic_tile_observation_enabled = false;
    std::array<battle::Role, 9> m_role_order {};
    std::unordered_map<Point, battle::roguelike::ForceDeployDirection> m_force_deploy_direction;

    struct AirDefenseData
    {
        /* from config */
        int stop_blocking_deploy_num = INT_MAX;
        int deploy_air_defense_num = 0;
        bool ban_medic = false;
        /* real time */
        int has_deployed_blocking_num = 0;
        int has_deployed_air_defense_num = 0;
        bool has_finished_deploy_air_defense = false;
    };

    AirDefenseData m_force_air_defense;

    /* real time */

    size_t m_cur_home_index = 0;
    bool m_first_deploy = true;
    bool m_melee_full = false;
    bool m_ranged_full = false;

    struct HomeInfo
    {
        bool wait_blocking = true;
        bool wait_medic = true;
        Point blocking_pos;
    };

    std::vector<HomeInfo> m_homes_status;
    std::unordered_map<Point, size_t> m_blocking_for_home_index;
    std::unordered_map<Point, std::vector<size_t>> m_medic_for_home_index;
    std::deque<size_t> m_urgent_home_index;

    struct DroneTile
    {
        Time_Point placed_time;
        Point placed_loc;

        DroneTile(Time_Point placed_time, Point placed_loc)
        {
            this->placed_time = placed_time;
            this->placed_loc = placed_loc;
        }

        bool operator<(const DroneTile& x) const { return x.placed_time < placed_time; }
    };

    std::priority_queue<DroneTile> m_need_clear_tiles;
    std::unordered_map<std::string, std::vector<battle::roguelike::DeployInfoWithRank>> m_deploy_plan;
    std::vector<battle::roguelike::DeployInfoWithRank> m_retreat_plan;
    std::unordered_map<battle::OperNameTag, std::chrono::steady_clock::time_point> m_deployed_time;

    // 阵亡热点：同一格短时间内反复阵亡，说明这个位置守不住，
    // 再往这里补人只是继续送，黑名单掉让后续改选别的位置。
    std::unordered_map<Point, std::chrono::steady_clock::time_point> m_last_death_time;
    std::unordered_map<Point, int> m_death_count;

    // 缓存干员精英
    std::unordered_map<std::string, int64_t> m_oper_elite;
    // 缓存干员精英情况
    void cache_oper_elite_status();

    // ==== 漏怪 / 崩盘诊断（只读观测，不改变既有部署策略）====
    // 依据：
    //   1)《敌人机制与 MAA 可观测性盘点》§4「MAA 侧缺什么」：战斗内 MAA 只能读到
    //      「已击杀 / 敌人总数 /
    //      费用」，读不到敌人血量、攻击类型、元素损伤条，也读不到
    //      「村民」这类中立单位，因此无法做敌人级反应式决策。
    //   2)《水月肉鸽全地图机制盘点》§5：漏怪集中在窄图（部署格少、蓝门集中）与
    //      多蓝门图的早期费用真空期。
    // 做法：不猜敌人，只把「本场战斗多久没涨击杀、还剩几个敌人」量化成日志事件，
    //       供离线按地图名聚合定位漏怪图，作为下一轮行为改动的数据基础。
    void diagnose_battle_progress();

    std::chrono::steady_clock::time_point m_diag_battle_start {};   // 本场战斗首次观测时刻
    std::chrono::steady_clock::time_point m_diag_last_kill_time {}; // 最近一次击杀数变化的时刻
    int m_diag_last_kills = 0;                                      // 上一帧击杀数，用于检测停滞
    int m_diag_last_heartbeat = -1;                                 // 上次打印心跳的 30 秒刻度，避免刷屏
    bool m_diag_stall_warned = false;                               // 本轮停滞是否已告警，避免重复刷

    std::unique_ptr<blackflow::BlackFlowSoulObserver> m_soul_observer;
    std::unique_ptr<blackflow::BlackFlowSummonRetreat> m_soul_retreat;
    std::map<battle::OperNameTag, battle::DeployDirection> m_observed_deploy_directions;
    std::uint64_t m_soul_sequence = 0;
    bool m_soul_selection_owned = false;
};
} // namespace asst
