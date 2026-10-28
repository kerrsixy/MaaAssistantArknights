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

private:
    mutable json::object m_result;
    // 2026-10-28: 策略名单独存一份。verify() 里「地图阶段采样血量」那个分支会把
    // m_result 清空（刻意为之，让 _run() 跳过策略切换只采样），而地图节点每经过一次
    // 就会触发一次该分支，于是 StrategyChange 刚写进 m_result 的 text 立刻被清掉，
    // _run() 读到的永远是空串（日志 `Skip strategy change, current strategy is ` 尾部为空）。
    // 把策略名拆到独立成员后，两条路径互不干扰。
    mutable std::string m_strategy;
    // 只有「地图上选中战斗节点」这一帧才采信血量：
    // 事件页/商店页/加载页的左上角不是目标生命值，OCR 会读出 2、33、72 这类垃圾值。
    mutable bool m_on_battle_node = false;
};
}
