#pragma once

#include "AbstractTask.h"
#include "BattleDeploymentEvidence.h"

#include "Common/AsstBattleDef.h"
#include "Config/Miscellaneous/TilePack.h"
#include "InstHelper.h"
#include "MaaUtils/NoWarningCVMat.hpp"
#include "Utils/Platform.hpp"
#include "Utils/WorkingDir.hpp"
#include "Vision/BestMatcher.h"

#include <concepts>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>

namespace asst
{
template <typename T>
concept OperAvatarPair = requires {
    typename T::first_type;
    typename T::second_type;
    requires std::same_as<std::string, std::remove_cvref_t<typename T::first_type>>;
    requires std::same_as<cv::Mat, std::remove_cvref_t<typename T::second_type>>;
};

class BattleHelper
{
public:
    ~BattleHelper() = default;

protected:
    BattleHelper(Assistant* inst);

    virtual AbstractTask& this_task() = 0;

    virtual bool set_stage_name(const std::string& name);
    virtual void clear();

    virtual const std::string oper_name_ocr_task_name() const noexcept { return "BattleOperName"; }

    virtual bool do_strategic_action(const cv::Mat& reusable = cv::Mat());

    bool calc_tiles_info(const std::string& stage_name, double shift_x = 0, double shift_y = 0);

    bool pause();
    bool speed_up();
    bool abandon();

    bool update_deployment(bool init = false, const cv::Mat& reusable = cv::Mat(), bool need_oper_cost = false);
    // 更新部署区的干员，仅当存在未识别干员且不处于冷却中return false
    bool update_deployment_(
        std::vector<battle::DeploymentOper>& cur_opers,
        const std::vector<battle::DeploymentOper>& old_deployment_opers,
        bool stop_on_unknown);
    bool update_kills(const cv::Mat& image, const cv::Mat& image_prev = cv::Mat());
    bool update_cost(const cv::Mat& image, const cv::Mat& image_prev = cv::Mat());

    cv::Mat get_top_view(const cv::Mat& cam_img, bool side = true, bool has_multi_stages = false);

    bool deploy_oper(const std::string& name, const Point& loc, battle::DeployDirection direction);
    bool deploy_oper(battle::Role role, const std::string& name, const Point& loc, battle::DeployDirection direction);

    static bool is_terrain_roadblock(const battle::DeploymentOper& oper);
    static battle::DeployDirection
        deployment_direction_for(const battle::DeploymentOper& oper, battle::DeployDirection requested);

    virtual bool verify_deployment_enabled() const { return false; }

    virtual bool ordinary_occupancy_inspection_enabled() const { return false; }

    bool ordinary_occupancy_inspection_applies(const battle::DeploymentOper& oper) const;

    virtual bool ordinary_occupancy_normal_view(const cv::Mat&) const { return false; }

    virtual bool ordinary_occupancy_selected_view(const cv::Mat&, const battle::SelectedObjectIdentity&) const
    {
        return false;
    }

    static std::optional<Rect> ordinary_occupancy_cancel_rect(const TaskConstPtr& task, const cv::Mat& image);
    battle::SelectionCancellationReceipt
        cancel_owned_ordinary_occupancy_selection(const cv::Mat& image, std::uint64_t observed_ms);

    virtual bool consume_ordinary_occupancy_selection(battle::SelectedObjectObservation, const cv::Mat&)
    {
        return false;
    }

    virtual bool deployment_target_allowed(const battle::DeploymentOper&, const Point&) const { return true; }

    virtual battle::DeploymentKind deployment_kind(const battle::DeploymentOper& oper) const
    {
        return oper.role == battle::Role::Drone ? battle::DeploymentKind::ReusableSummon
                                                : battle::DeploymentKind::Operator;
    }

protected:
    // 肉鸽地图自带的放置物（如黑流树海的「工人」）在部署栏里只有图标、没有名字可以 OCR。
    // 这类卡片统一挂一个合成名前缀，交给 RoguelikeBattleTaskPlugin 用固定组名去匹配；
    // 普通战斗里不会出现这个前缀，因此不影响既有流程。
    static constexpr std::string_view NamelessDeployablePrefix = "__nameless_deployable_";

    static bool is_nameless_deployable(const std::string& name) { return name.starts_with(NamelessDeployablePrefix); }

    // 部署栏上「没有真实名字」的卡片：OCR 结果为空，或只读到数字/符号（例如角落里的 ×2）。
    static bool is_card_name_unusable(const std::string& name)
    {
        if (name.empty()) {
            return true;
        }
        for (const unsigned char ch : name) {
            // ASCII 字母、数字，以及 UTF-8 多字节（中文）都视为「可能的名字」
            if (std::isalnum(ch) != 0 || ch >= 0x80) {
                return false;
            }
        }
        return true;
    }

protected:
    battle::DeploymentOutcome
        verify_deployment(const battle::DeploymentOper& oper, const Point& loc, bool input_accepted);
    bool deployment_retry_allowed(const battle::DeploymentOper& oper, const Point& loc) const;
    bool retreat_oper(const std::string& name);
    bool retreat_oper(battle::Role role, const std::string& name);
    bool retreat_oper(const Point& loc, bool manually = true);
    // 设置战场单位（干员位移修正 / 装置设施登记）的格子坐标：已在场则迁移双表记录，不在场则登记新条目
    bool set_unit_location(battle::Role role, const std::string& name, const Point& loc);
    bool is_skill_ready(const Point& loc, const cv::Mat& reusable = cv::Mat());
    bool is_skill_ready(const std::string& name, const cv::Mat& reusable = cv::Mat());
    bool is_skill_ready(battle::Role role, const std::string& name, const cv::Mat& reusable = cv::Mat());
    // timeout_ms：0 为暂停快速路径（识别未命中即失败），正数为限时等待技能转好，-1 为不暂停无限等待
    bool use_skill(const std::string& name, int timeout_ms = 500'000);
    bool use_skill(battle::Role role, const std::string& name, int timeout_ms = 500'000);
    bool use_skill(const Point& loc, int timeout_ms = 500'000);
    bool check_pause_button(const cv::Mat& reusable = cv::Mat());
    bool check_skip_plot_button(const cv::Mat& reusable = cv::Mat());
    // 检查是否有战斗中带头像的对话框
    bool check_avatar_dialog(const cv::Mat& reusable = cv::Mat());
    // 检查是否处于二倍速
    bool check_in_speedup(const cv::Mat& reusable = cv::Mat());
    virtual bool check_in_battle(const cv::Mat& reusable = cv::Mat(), bool weak = true);
    virtual bool wait_until_start(bool weak = true);
    bool wait_until_end(bool weak = true, bool do_strategy = true);

    // Pure intents never interpret a stop button as activation or a ready button as stop.
    enum class SkillButtonIntent
    {
        ActivateOnly,
        StopOnly
    };
    enum class SkillButtonState
    {
        Unknown,
        Ready,
        Active
    };
    enum class SkillInputResult
    {
        Failed,
        InputIssued,
        AlreadyActive,
        AlreadyInactive,
        Interrupted
    };
    static SkillButtonState classify_skill_button(const cv::Mat& top_view);
    static bool skill_button_click_allowed(SkillButtonState state, SkillButtonIntent intent);
    static SkillInputResult dispatch_skill_button(
        SkillButtonState state,
        SkillButtonIntent intent,
        bool input_allowed,
        const std::function<bool()>& click,
        const std::function<bool()>& close_selection);
    static bool record_automatic_skill_result(
        SkillInputResult result,
        battle::SkillUsage& usage,
        int& times,
        int& errors,
        std::chrono::steady_clock::time_point& last_poll,
        std::chrono::steady_clock::time_point now,
        bool suppress_error_backoff);
    SkillInputResult use_skill(const Point& loc, int timeout_ms, SkillButtonIntent intent);
    SkillInputResult click_skill(int timeout_ms, SkillButtonIntent intent);
    bool use_all_ready_skill(const cv::Mat& reusable, SkillButtonIntent intent);
    bool use_all_ready_skill_impl(const cv::Mat& reusable, std::optional<SkillButtonIntent> intent);
    bool use_all_ready_skill(const cv::Mat& reusable = cv::Mat());
    bool check_and_use_skill(const std::string& name, bool& has_error, const cv::Mat& reusable = cv::Mat());
    bool check_and_use_skill(
        battle::Role role,
        const std::string& name,
        bool& has_error,
        const cv::Mat& reusable = cv::Mat());
    bool check_and_use_skill(const Point& loc, bool& has_error, const cv::Mat& reusable = cv::Mat());
    void save_map(const cv::Mat& image);

    bool click_oper_on_deployment(const std::string& name);
    bool click_oper_on_deployment(battle::Role role, const std::string& name);
    bool click_oper_on_deployment(const Rect& rect);
    bool click_oper_on_battlefield(const std::string& name);
    bool click_oper_on_battlefield(battle::Role role, const std::string& name);
    bool click_oper_on_battlefield(const Point& loc);
    bool click_retreat();                       // 这个是不带识别的，直接点
    bool click_skill(int timeout_ms = 500'000); // 这个是带识别的，转好了才点
    bool cancel_oper_selection();
    // 修正终点超出范围的滑动，纠正时是否需要顺时针旋转
    void fix_swipe_out_of_limit(
        Point& p1,
        Point& p2,
        int width,
        int height,
        int max_distance = INT_MAX,
        double radian = 0);
    bool move_camera(const std::pair<double, double>& delta, bool keep_kills = false);

    std::string analyze_detail_page_oper_name(const cv::Mat& image, battle::Role role);
    battle::SelectedObjectIdentity analyze_detail_page_object_identity(const cv::Mat& image);
    std::optional<Rect> get_oper_rect_on_deployment(battle::Role role, const std::string& name) const;

    int elapsed_time();

    // 注册已部署干员及位置
    void register_deployed_oper(battle::Role role, const std::string& name, const Point& loc);
    // 从场上干员和已占用格子中移除冷却中的干员
    void remove_cooling_from_battlefield(const battle::DeploymentOper& oper);
    bool remove_returned_operator_from_battlefield(const battle::DeploymentOper& oper);

    std::string m_stage_name;
    Map::Level m_map_data;
    // 拖拽部署时倾斜视角（俯角 10°）投影的格子屏幕坐标，拖拽落点按此视角计算（肉鸽朝向评估亦查其 TileKey）
    std::unordered_map<Point, TilePack::TileInfo> m_side_tile_info;
    // 正常战斗视角（无俯角）投影的格子屏幕坐标，点击场上格子、技能就绪检测等使用
    std::unordered_map<Point, TilePack::TileInfo> m_normal_tile_info;
    Point m_skill_button_pos;
    Point m_retreat_button_pos;
    bool m_has_multi_stages = false;
    // 技能用法, <可unknown职业,名称> -> <用法>
    std::unordered_map<battle::OperNameTag, battle::SkillUsage> m_skill_usage;
    // 技能使用次数, <可unknown职业,名称> -> <次数>
    std::unordered_map<battle::OperNameTag, int> m_skill_times;
    // 技能就绪状态错误计数, <可unknown职业,名称> -> <次数>
    std::unordered_map<battle::OperNameTag, int> m_skill_error_count;
    // 上次使用技能的时间, <实际职业,名称> -> <时间>
    std::unordered_map<battle::OperNameTag, std::chrono::steady_clock::time_point> m_last_use_skill_time;
    int m_camera_count = 0;
    bool m_in_speedup = false; // 是否处于2倍速
    std::pair<double, double> m_camera_shift = { 0., 0. };

    /* 实时更新的数据 */
    bool m_in_battle = false;
    int m_kills = 0;
    int m_total_kills = 0;
    int m_cost = 0;
    bool m_stopwatch_enabled = false;
    std::chrono::steady_clock::time_point m_stopwatch_start_time;

    std::vector<battle::DeploymentOper> m_cur_deployment_opers;

    std::map<battle::OperNameTag, Point> m_battlefield_opers; // 已部署的干员, <实际职业,名称> -> <坐标>
    std::map<Point, battle::OperNameTag> m_used_tiles;        // 已占用的格子, <坐标> -> <实际职业,名称>
    // Unverified attempts are quarantined per operator and tile, not declared blocked.
    std::map<std::pair<battle::OperNameTag, Point>, int> m_unverified_deployment_attempts;
    // Unknown evidence shares the bounded retry budget across operators on a tile.
    std::map<Point, int> m_unverified_deployment_tiles;

private:
    InstHelper m_inst_helper;
};
} // namespace asst
