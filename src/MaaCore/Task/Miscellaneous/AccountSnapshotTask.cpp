#include "AccountSnapshotTask.h"

#include <regex>

#include "Controller/Controller.h"
#include "Utils/Logger.hpp"
#include "Vision/OCRer.h"
#include "Vision/RegionOCRer.h"

namespace
{
std::string trim_spaces(std::string text)
{
    static const std::regex space_re(R"(\s+)");
    text = std::regex_replace(text, space_re, "");
    return text;
}

bool has_digit(const std::string& text)
{
    static const std::regex digit_re(R"([0-9])");
    return std::regex_search(text, digit_re);
}

std::string normalize_uid_text(std::string text)
{
    // The small white UID glyphs are frequently confused with Latin letters
    // by the detector (O/D/B/S/I).  Normalize only this numeric field, then
    // let the length check below reject unrelated OCR text.
    for (char& ch : text) {
        switch (ch) {
        case 'O':
        case 'o':
            ch = '0';
            break;
        case 'D':
            ch = '2';
            break;
        case 'B':
            ch = '8';
            break;
        case 'S':
        case 's':
            ch = '5';
            break;
        case 'I':
        case 'l':
            ch = '1';
            break;
        default:
            break;
        }
    }
    return text;
}
}

bool asst::AccountSnapshotTask::_run()
{
    LogTraceFunction;
    m_uid.clear();
    m_nick.clear();

    // 约定：调用方已把游戏停在首页（OperStrengthTask 的第一个子任务）
    LogInfo << "正在识别账号：读取首页左下角名片上的昵称与 UID（识别不到不影响后续练度扫描）。";
    const auto image = ctrler()->get_image();

    // 全屏 OCR：不赌固定 ROI（不同皮肤 / 分辨率 / 剧情立绘都会让名片位置挪动）
    // 也不挂 task_info 的 text 过滤——首页整屏文本量小，交给正则自己挑 UID 更稳
    OCRer analyzer(image);
    auto result_opt = analyzer.analyze();

    if (!result_opt.has_value() || result_opt->empty()) {
        LogWarn << "账号名片 OCR 没有读到任何文本；本次练度快照不会绑定到具体账号。";
    }
    else {
        const auto uid_re = std::regex(R"(([0-9]{8,12}))");
        std::smatch match;
        std::optional<Rect> uid_rect;

        for (const auto& res : *result_opt) {
            // Account IDs belong to the lower-left player card. Small home
            // widget captions can hallucinate long digit strings elsewhere.
            // Reject those before considering the bounded card-ROI fallback.
            if (res.score < 0.95 || res.rect.x < 0 || res.rect.x + res.rect.width > 300 || res.rect.y < 300 ||
                res.rect.y + res.rect.height > 500) {
                continue;
            }
            auto candidate = normalize_uid_text(res.text);
            if (std::regex_search(candidate, match, uid_re) && match.size() > 1) {
                m_uid = trim_spaces(match[1].str());
                uid_rect = res.rect;
                break;
            }
        }

        // Full-screen OCR often drops the first/last UID glyph under the
        // player-card artwork.  A raw, detector-free pass over the fixed
        // lower-left card recovers the complete number without affecting the
        // general OCR path used for the nickname.
        if (m_uid.empty()) {
            RegionOCRer uid_analyzer(image, Rect { 0, 390, 230, 75 });
            uid_analyzer.set_use_raw(true);
            uid_analyzer.set_bin_threshold(80, 255);
            if (const auto uid_result = uid_analyzer.analyze(); uid_result.has_value()) {
                auto candidate = normalize_uid_text(uid_result->text);
                if (std::regex_search(candidate, match, uid_re) && match.size() > 1) {
                    m_uid = trim_spaces(match[1].str());
                    uid_rect = uid_result->rect;
                    LogInfo << __FUNCTION__ << "| UID recovered by card ROI OCR:" << m_uid;
                }
            }
        }

        // 昵称：名片上紧贴 UID 上方那一行，左对齐、且不含数字
        if (uid_rect.has_value()) {
            std::string best;
            int best_gap = 1000;
            for (const auto& res : *result_opt) {
                if (res.rect.y >= uid_rect->y) {
                    continue;
                }
                const int gap = uid_rect->y - res.rect.y;
                if (gap > 45 || has_digit(res.text)) {
                    continue;
                }
                // 与 UID 左边缘大致对齐的才算名片内的昵称行
                if (res.rect.x > uid_rect->x + 40) {
                    continue;
                }
                if (gap < best_gap) {
                    best_gap = gap;
                    best = trim_spaces(res.text);
                }
            }
            m_nick = best;
        }
    }

    LogInfo << __FUNCTION__ << "| nick:" << m_nick << "uid:" << m_uid;
    if (m_uid.empty()) {
        LogWarn << "没能读到账号 UID：请确认游戏停在主界面（终端页），而不是干员列表 / 作战中；"
                << "本次练度快照不会绑定到具体账号。";
    }
    else {
        LogInfo << "账号识别完成：昵称「" << (m_nick.empty() ? std::string("<未识别>") : m_nick) << "」，UID " << m_uid
                << "；练度快照将绑定到这个账号。";
    }

    json::value info = basic_info_with_what("OperStrengthAccount");
    auto& details = info["details"];
    details["nick"] = m_nick;
    details["uid"] = m_uid;
    callback(AsstMsg::SubTaskExtraInfo, info);

    // 读不到账号不算整条链失败（后面练度扫描仍可跑）
    return true;
}
