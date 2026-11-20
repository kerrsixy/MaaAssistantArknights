#pragma once

#include <array>
#include <string_view>

namespace asst::spa
{
// Generated from home-anchored act3autochess sub_01 and sub_03 by generate_board_data.py.
// G: ground; F: fence; R: ranged-only; M: mire; .: walk-only; X: blocking device.
struct BoardTerrain
{
    std::string_view id;
    std::array<std::string_view, 5> rows;
};

inline constexpr std::array<BoardTerrain, 10> BoardTerrains { {
    { "m01", { { "           ", "   FGGGGG.S", "   FGXFFG. ", "   FGXXFG. ", "  EGG   GGS" } } },
    { "m02", { { "           ", "   GGGGGG.S", "   GFFGGG. ", "   GFFG  . ", "  EGGGG  GS" } } },
    { "m03", { { "           ", "     FGGG.S", "     XGRRX ", "   RRGGFFX ", "  EGGGGGGGS" } } },
    { "m04", { { "           ", "   XX   X.S", "   FGGGGG. ", "   FGGGGG. ", "  EGG   GGS" } } },
    { "m05", { { "           ", "    RGGG .S", "    RGFG . ", "    RG G . ", "  EGGG GGGS" } } },
    { "m06", { { "           ", "      RMG.S", "     RFMFX ", "    RFFMFX ", "  EGGGGGGGS" } } },
    { "m09", { { "           ", "   GGGGGG.S", "   GFGGG X ", "   G GRG X ", "  EGGG GGGS" } } },
    { "m10", { { "           ", "   RGGGGG.S", "   RGFGGG  ", "   RGFGFG. ", "  EGGGGF GS" } } },
    { "m11", { { "           ", "   FFFGGG.S", "   GGGGFF  ", "   GR GGG. ", "  EGGGGF .S" } } },
    { "m12", { { "           ", "   FFGGG .S", "   GGG GG. ", "   GGG F   ", "  EGFGGGGGS" } } },
} };
inline constexpr std::array<BoardTerrain, 10> BossBoardTerrains { {
    { "m01", { { "           ", "   FGGGGG..", "   FGXFFG..", "   FGXXFG..", "  EGG   GG." } } },
    { "m02", { { "           ", "   GGGGGG..", "   GFFGGG..", "   GFFG  ..", "  EGGGG  G." } } },
    { "m03", { { "           ", "     FGGG..", "     XGRR..", "   RRGGFF..", "  EGGGGGGG." } } },
    { "m04", { { "           ", "   XX   X..", "   FGGGGG..", "   FGGGGG..", "  EGG   GG." } } },
    { "m05", { { "           ", "    RGGGF..", "    RGFGF..", "    RG GF..", "  EGGG GGG." } } },
    { "m06", { { "           ", "      RMG..", "     RFMF..", "    RFFMF..", "  EGGGGGGG." } } },
    { "m09", { { "           ", "   GGGGGG..", "   GFGGG ..", "   G GRG ..", "  EGGG GGG." } } },
    { "m10", { { "           ", "   RGGGGG..", "   RGFGGG..", "   RGFGFG..", "  EGGGGF G." } } },
    { "m11", { { "           ", "   FFFGGG..", "   GGGGFF..", "   GR GGG..", "  EGGGGF .." } } },
    { "m12", { { "           ", "   FFGGG ..", "   GGG GG..", "   GGG F ..", "  EGFGGGGG." } } },
} };
}
