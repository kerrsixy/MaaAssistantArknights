#include "BlackFlowPauseTransitionAnalyzer.h"

#include <algorithm>
#include <cctype>

#include "Vision/Battle/BattlefieldMatcher.h"
#include "Vision/OCRer.h"

namespace asst::blackflow
{
PauseTransitionFrame analyze_pause_transition_frame(
    const cv::Mat& image,
    std::int64_t captured_ms,
    bool paused,
    bool official_running,
    bool normal_running)
{
    PauseTransitionFrame result;
    result.captured_ms = captured_ms;
    result.paused = paused;
    result.strong_running = !paused && official_running && normal_running;
    if (image.empty()) {
        return result;
    }
    BattlefieldMatcher hud(image);
    hud.set_object_of_interest({ .kills = true });
    if (const auto match = hud.analyze(); match && match->kills.status == BattlefieldMatcher::MatchStatus::Success) {
        const auto [kills, total] = match->kills.value;
        if (total > 0 && total <= 10'000 && kills >= 0 && kills <= total) {
            result.hud_known = true;
            result.kills = kills;
            result.total = total;
        }
    }
    if (paused || !result.hud_known || result.kills != result.total) {
        return result;
    }
    OCRer banner(image);
    banner.set_roi({ 0, 180, 1280, 390 });
    banner.set_required({ "任务", "完成", "MISSION", "ACCOMPLISHED", "CCOMPLISHED" });
    std::string text;
    Rect left_mission;
    Rect left_accomplished;
    if (const auto words = banner.analyze()) {
        for (const auto& word : *words) {
            if (word.score >= 0.95) {
                text += word.text;
                if (word.rect.x == 0 && word.text == "MISSION") {
                    left_mission = word.rect;
                }
                else if (word.rect.x == 0 && word.text == "CCOMPLISHED") {
                    left_accomplished = word.rect;
                }
            }
        }
    }
    auto english = text;
    std::erase_if(english, [](unsigned char c) { return std::isspace(c); });
    std::transform(english.begin(), english.end(), english.begin(), [](unsigned char c) { return std::tolower(c); });
    result.banner = (text.find("任务") != std::string::npos && text.find("完成") != std::string::npos) ||
                    (english.find("mission") != std::string::npos && english.find("accomplished") != std::string::npos);
    // A sliding completion band can leave only its initial A outside the image.
    // Accept that one exact clipped token only as a high-score two-line pair
    // touching the physical left boundary while the normal HUD still exists.
    const bool clipped_english = result.strong_running && !left_mission.empty() && !left_accomplished.empty() &&
                                 left_accomplished.width >= left_mission.width &&
                                 left_accomplished.height * 2 >= left_mission.height &&
                                 left_mission.height * 2 >= left_accomplished.height &&
                                 left_accomplished.y >= left_mission.y + left_mission.height / 2 &&
                                 left_accomplished.y <= left_mission.y + left_mission.height * 3;
    result.banner = result.banner || clipped_english;
    if (result.banner) {
        result.strong_running = false;
    }
    return result;
}
}
