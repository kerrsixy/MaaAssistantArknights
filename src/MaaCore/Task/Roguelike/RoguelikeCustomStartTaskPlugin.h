#pragma once
#include "Common/AsstBattleDef.h"
#include "AbstractRoguelikeTaskPlugin.h"

#include <string_view>

namespace asst
{
enum class RoguelikeCustomType
{
    None,
    Squad,    // 分队类型， like 指挥分队, 矛头分队, etc
    Reward,   // 奖励类型， like 热水壶, 演讲稿, etc
    Roles,    // 职业类型， like 先手必胜, 稳扎稳打, etc
    Voucher,
};

struct RoguelikeStartSelect // 刷开局模式下凹开局奖励选择
{
    bool hot_water = false; // 热水壶
    bool shield = false;    // 盾；傀影没盾，是生命
    bool ingot = false;     // 源石锭
    bool hope = false;      // 希望
    bool random = false;    // 随机奖励
    bool key = false;       // 钥匙
    bool dice = false;      // 骰子
    bool ideas = false;     // 构想
    bool ticket = false;    // 票券
};

class RoguelikeCustomStartTaskPlugin : public AbstractRoguelikeTaskPlugin
{
public:
    using AbstractRoguelikeTaskPlugin::AbstractRoguelikeTaskPlugin;
    virtual ~RoguelikeCustomStartTaskPlugin() override = default;

public:
    virtual bool verify(AsstMsg msg, const json::value& details) const override;
    virtual bool load_params([[maybe_unused]] const json::value& params) override;

protected:
    virtual bool _run() override;

private:
    bool hijack_squad();
    bool hijack_reward();
    bool hijack_roles();
    bool select_initial_voucher();
    std::string resolve_start_roles() const;
    static bool role_group_contains(std::string_view roles, battle::Role role);
    void skip_incompatible_start_opers(const std::string& roles);
    std::vector<std::string> get_select_list() const;

private:
    RoguelikeStartSelect m_start_select; // 开局选择
    mutable RoguelikeCustomType m_waiting_to_run = RoguelikeCustomType::None;

    std::string m_squad;
    std::string m_collectible_mode_squad;
};
}
