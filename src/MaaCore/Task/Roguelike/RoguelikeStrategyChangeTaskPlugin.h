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
    // 只有「地图上选中战斗节点」这一帧才采信血量：
    // 事件页/商店页/加载页的左上角不是目标生命值，OCR 会读出 2、33、72 这类垃圾值。
    mutable bool m_on_battle_node = false;
};
}
