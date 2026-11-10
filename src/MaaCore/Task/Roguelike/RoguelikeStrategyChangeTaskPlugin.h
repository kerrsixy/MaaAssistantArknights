#pragma once
#include "AbstractRoguelikeTaskPlugin.h"

namespace asst
{
class RoguelikeStrategyChangeTaskPlugin : public AbstractRoguelikeTaskPlugin
{
public:
    using AbstractRoguelikeTaskPlugin::AbstractRoguelikeTaskPlugin;
    virtual ~RoguelikeStrategyChangeTaskPlugin() override = default;

public:
    virtual bool verify(AsstMsg msg, const json::value& details) const override;

protected:
    virtual bool _run() override;

    // 2026-10-29 阶段C：新一次探索开始时必须清掉上一局的残留。
    // 由 RoguelikeResetTaskPlugin 在 Roguelike@StartExplore 时统一调用。
    virtual void reset_in_run_variables() override
    {
        m_strategy.clear();
        m_observe_hp = false;
        m_on_battle_node = false;
        m_pending_hp = -1;
    }

private:
    // 2026-10-29 阶段C：删掉 m_result。它同时承载「策略 OCR 结果」和「血量采样占位」，
    // 两条语义无关的路径互相覆盖，导致 _run() 读到的策略名恒为空。
    // 策略名现在只经 m_strategy 传递；血量采样只经 m_observe_hp 传递。
    mutable std::string m_strategy;
    // 由 verify() 的地图阶段分支置位，表示「这一帧可以采血量」。
    mutable bool m_observe_hp = false;
    // 只有「地图上选中战斗节点」这一帧才采信血量：
    // 事件页/商店页/加载页的左上角不是目标生命值，OCR 会读出 2、33、72 这类垃圾值。
    mutable bool m_on_battle_node = false;
    // 2026-10-29 阶段C：原为 _run() 内的 static 变量，跨实例共享。
    // 上一局的待确认值会污染下一局（新局血量 8、旧局 pending 7 时，首帧会被判成「未确认」而丢弃）。
    int m_pending_hp = -1;
};
}
