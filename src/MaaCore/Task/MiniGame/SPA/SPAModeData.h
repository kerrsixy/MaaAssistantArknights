#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// Generated from act3autochess/V3_1 modeDataDict and bondInfoDict.
// Source SHA256 is stored in SPACombatData.json. Unknown modes remain unknown.
namespace asst::spa
{
inline constexpr std::array<std::string_view, 26> SeasonModePactNames {
    { "奥术",   "助力",     "聚合", "阿戈尔", "协防干员", "伊比利亚", "不屈",   "投资人", "卡西米尔",
      "谢拉格", "莱塔尼亚", "调和", "奇迹",   "精准",     "突袭",     "萨尔贡", "叙拉古", "灵巧",
      "独行",   "坚守",     "绝技", "迅捷",   "乌萨斯",   "维多利亚", "远见",   "炎" }
};

struct SeasonModePactData
{
    std::string_view id;
    int difficulty;
    std::uint32_t active_pacts;
    std::uint32_t inactive_pacts;
};

inline constexpr std::array<SeasonModePactData, 25> SeasonModePactCatalogue { {
    { "mode_multi_abyss", 4, 0x03ffffffu, 0x00000000u },  { "mode_multi_funny", 1, 0x03e9ae77u, 0x00165188u },
    { "mode_multi_hard", 3, 0x03ffffffu, 0x00000000u },   { "mode_multi_normal", 2, 0x03ffffffu, 0x00000000u },
    { "mode_single_1", 1, 0x03e9ae77u, 0x00165188u },     { "mode_single_10", 10, 0x03ffffffu, 0x00000000u },
    { "mode_single_11", 11, 0x03ffffffu, 0x00000000u },   { "mode_single_12", 12, 0x03ffffffu, 0x00000000u },
    { "mode_single_13", 13, 0x03ffffffu, 0x00000000u },   { "mode_single_14", 14, 0x03ffffffu, 0x00000000u },
    { "mode_single_15", 15, 0x03ffffffu, 0x00000000u },   { "mode_single_16", 16, 0x03ffffffu, 0x00000000u },
    { "mode_single_17", 17, 0x03ffffffu, 0x00000000u },   { "mode_single_18", 18, 0x03ffffffu, 0x00000000u },
    { "mode_single_19", 19, 0x03ffffffu, 0x00000000u },   { "mode_single_2", 2, 0x03e9ae77u, 0x00165188u },
    { "mode_single_20", 20, 0x03ffffffu, 0x00000000u },   { "mode_single_3", 3, 0x03e9ae77u, 0x00165188u },
    { "mode_single_4", 4, 0x03e9ae77u, 0x00165188u },     { "mode_single_5", 5, 0x03ffffffu, 0x00000000u },
    { "mode_single_6", 6, 0x03ffffffu, 0x00000000u },     { "mode_single_7", 7, 0x03ffffffu, 0x00000000u },
    { "mode_single_8", 8, 0x03ffffffu, 0x00000000u },     { "mode_single_9", 9, 0x03ffffffu, 0x00000000u },
    { "mode_single_funny", 1, 0x03e9ae77u, 0x00165188u },
} };
}
