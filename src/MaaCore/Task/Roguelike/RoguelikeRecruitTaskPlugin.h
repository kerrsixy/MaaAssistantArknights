#pragma once
#include "AbstractRoguelikeTaskPlugin.h"
#include "Common/AsstBattleDef.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "RoguelikeRecruitPolicy.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowVoucherStash.h"

namespace asst
{
namespace blackflow
{
struct RecruitmentDutyOffer;
}

// 干员招募信息
struct RoguelikeRecruitInfo
{
    std::string name;          // 干员名字
    int priority = 0;          // 招募优先级 (0-1000)
    int page_index = 0;        // 所在页码 (用于判断翻页方向)
    bool is_alternate = false; // 是否后备干员 (允许重复招募、划到后备干员时不再往右划动)
    RoguelikeRecruitTier tier = RoguelikeRecruitTier::Skip;
};

class RoguelikeRecruitTaskPlugin : public AbstractRoguelikeTaskPlugin
{
public:
    using AbstractRoguelikeTaskPlugin::AbstractRoguelikeTaskPlugin;
    virtual ~RoguelikeRecruitTaskPlugin() override = default;

    virtual bool verify(AsstMsg msg, const json::value& details) const override;
    virtual bool load_params(const json::value& params) override;

protected:
    virtual bool _run() override;
    virtual void reset_in_run_variables() override;
    // 滑动到干员列表的最左侧
    void swipe_to_the_left_of_operlist(int loop_times = 2);
    // 缓慢向干员列表的左侧/右侧滑动
    void slowly_swipe(bool to_left, int swipe_dist = 500);

private:
    // 尝试招募指定助战干员
    bool recruit_support_char(const std::string& name, const int max_refresh);
    // 招募指定的自有干员
    bool recruit_own_char(const std::string& name);
    // 直接招募第一个干员
    bool lazy_recruit();
    // Preserve an unused BlackFlow voucher only with capacity, enabled-button and completion evidence.
    bool finish_without_recruitment();
    bool verify_voucher_stash();
    // 招募指定干员
    //
    // 输入参数:
    //   char_name: 干员名称
    //   is_rtl: 滑动方向 (true: 从右向左; false: 从左向右，需要先滑动到最左侧)
    //   require_strength: 是否应用自有干员的练度门槛
    // 返回值: 招募结果 (true: 成功; false: 失败)
    bool recruit_appointed_char(
        const std::string& char_name,
        bool is_rtl = false,
        bool require_strength = false,
        const blackflow::RecruitmentDutyOffer* duty_intent = nullptr);
    // 选择干员
    bool select_oper(const battle::roguelike::Recruitment& oper);
    int read_current_hope(const cv::Mat& image);
    bool can_afford(const battle::roguelike::Recruitment& oper, int hope) const;
    void discard_pending_recruitment(std::string_view reason);
    bool commit_pending_recruitment();
    bool resolve_elite_two_start_observation(battle::roguelike::Recruitment& selected);

    enum class RecruitmentEvent
    {
        Select,
        Confirm,
        Receipt,
        Discard
    };

    struct PendingRecruitment
    {
        battle::roguelike::Recruitment oper;
        bool confirmed = false;
        bool advance_start = false;
        bool opening = false;
    };

    std::optional<PendingRecruitment> m_pending_recruitment;
    mutable RecruitmentEvent m_recruitment_event = RecruitmentEvent::Select;
    // 找出给定的 Offset 组所有满足条件的干员
    std::unordered_set<std::string> calculate_condition_oper(
        const RecruitPriorityOffset& condition,
        const std::unordered_map<std::string, RoguelikeOper>& chars_map);

    int m_recruit_count = 0;                // 第几次招募
    mutable bool m_initail_recruit = false; // 是否第一次招募，第一次招募时会进行一些初始化操作
    bool m_starts_complete = false; // 开局干员是否已经招募，阵容中必须有开局干员，没有前仅招募start干员或预备干员
    bool m_team_complete = false;   // 阵容是否完备，阵容完备前，仅招募key干员或预备干员
    // 开局阶段（前 3 次招募）已覆盖的职业集合。
    // 用于「职业覆盖优先」：未覆盖职业的候选在开局阶段获得资源声明
    // opening.role_diversity_bonus 的加分，避免开局三人全是同一职业。
    std::unordered_set<battle::Role> m_opening_roles;
    // 指定的黑流策略；刷等级为空时 Core 推断为会飞三层的 burn 系策略
    std::string m_blackflow_strategy;
    blackflow::VoucherStashAttempt m_voucher_stash;
    mutable std::string m_stash_resume_source;
};
}
