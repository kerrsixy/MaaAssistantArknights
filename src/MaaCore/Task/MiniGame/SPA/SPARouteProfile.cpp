#include "SPARouteProfile.h"

namespace asst::spa
{
const std::map<std::string, RouteProfile>& route_profiles()
{
    // These are role and timing hints collected from the current-season
    // pact/operator research.  They are deliberately separate from the
    // authoritative pact catalogue: a missing hint must never make a card
    // illegal, and a hint never proves that a pact is active.
    static const std::map<std::string, RouteProfile> profiles {
        // Precision-led mech guides are primary routes despite Precision
        // being an auxiliary pact in the game's activation catalogue.
        // The strategy gift is protected separately; it does not acquire
        // Precision membership merely because this route was selected.
        { "精准",
          { "精准",
            2,
            { "承曦格雷伊", "空弦", "霍尔海雅", "黑键" },
            { "迷迭香" },
            {},
            { "奥术", "远见", "灵巧", "聚合", "奇迹" },
            {} } },
        { "炎",
          { "炎",
            2,
            { "拜松", "矩", "风絮", "仇白", "黍", "夕" },
            { "烛煌", "夕", "仇白", "老鲤" },
            {},
            { "投资人", "远见", "奇迹", "聚合", "奥术" } } },
        { "萨尔贡",
          { "萨尔贡",
            2,
            { "艾丝黛尔", "泡泡", "森蚺", "佩佩", "鸿雪" },
            { "佩佩", "鸿雪", "真言", "异客" },
            {},
            { "精准", "奥术", "迅捷" } } },
        { "维多利亚",
          { "维多利亚",
            3,
            { "慕斯", "Miss.Christine", "水灯心", "涤火杰西卡" },
            { "水灯心", "涤火杰西卡", "烛煌" },
            {},
            { "坚守", "奥术", "奇迹", "投资人" } } },
        { "谢拉格",
          { "谢拉格",
            3,
            { "初雪", "雪猎", "极光", "耶拉", "灵知", "圣聆初雪" },
            { "圣聆初雪", "凛御银灰", "锏" },
            {},
            { "精准", "灵巧", "迅捷", "奥术", "投资人" } } },
        { "阿戈尔",
          { "阿戈尔",
            3,
            { "幽灵鲨", "安哲拉", "斯卡蒂" },
            { "克莱门莎", "乌尔比安", "幽灵鲨", "斯卡蒂" },
            {},
            { "助力", "不屈", "灵巧" } } },
        { "叙拉古",
          { "叙拉古",
            2,
            { "拉普兰德", "复奏", "伺夜", "忍冬", "安洁莉娜" },
            { "荒芜拉普兰德", "贝洛内", "伺夜" },
            {},
            { "精准", "迅捷", "突袭" } } },
        { "卡西米尔",
          { "卡西米尔",
            2,
            { "野鬃", "砾", "瑕光", "远牙", "焰尾", "耀骑士临光" },
            { "玛恩纳", "耀骑士临光", "锏" },
            { "砾", "远牙", "历阵锐枪芬", "耀骑士临光" },
            { "突袭", "不屈", "迅捷" } } },
        { "乌萨斯",
          { "乌萨斯",
            2,
            { "折桠", "烈夏", "乌啾", "赫拉格", "冻河" },
            { "塔露拉", "怒潮凛冬" },
            {},
            { "迅捷", "奇迹", "突袭" } } },
        { "伊比利亚",
          { "伊比利亚",
            3,
            { "深靛", "旅骨", "流明", "棘刺" },
            { "蓝毒", "引星棘刺", "棘刺", "艾丽妮" },
            {},
            { "聚合", "精准", "迅捷" } } },
        { "莱塔尼亚",
          { "莱塔尼亚",
            2,
            { "地灵", "折光", "复奏", "黑键", "纯烬艾雅法拉" },
            { "艾雅法拉", "纯烬艾雅法拉", "黑键" },
            {},
            { "奥术", "精准", "聚合" } } },
    };
    return profiles;
}
}
