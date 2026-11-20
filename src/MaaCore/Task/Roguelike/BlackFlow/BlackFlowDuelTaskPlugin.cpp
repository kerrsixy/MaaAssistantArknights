#include "BlackFlowDuelTaskPlugin.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <numeric>
#include <regex>
#include <tuple>
#include <utility>

#include "BlackFlowSession.h"
#include "Config/Miscellaneous/TilePack.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Task/ProcessTask.h"
#include "Utils/DebugImageHelper.hpp"
#include "Utils/Logger.hpp"
#include "Vision/Battle/BattlefieldMatcher.h"
#include "Vision/OCRer.h"

namespace asst::blackflow::duel
{
constexpr std::string_view BlackFlowDuelCombatTask = "BlackFlow@Roguelike@DuelCombat-Enter";
constexpr std::string_view BlackFlowDuelOpponentSelectionTask = "BlackFlow@Roguelike@DuelOpponentSelection";

struct DuelCardCandidate
{
    asst::Rect rect;
    double score = 0.0;
};

struct DuelOpponentGroup
{
    std::vector<DuelCardCandidate> cards;
    double threat = 0.0;
    double confidence = 0.0;
    double win_probability = 0.0;
    double reward = 0.0;
    double utility = 0.0;
    bool viable = false;
    std::string evidence;
    std::string detail_text;
};

struct DuelOperatorDetail
{
    asst::Rect card_rect;
    std::string name;
    std::string role;
    std::string skill_name;
    std::string skill_text;
    std::string ocr_text;
    int elite = -1;
    int level = -1;
    int hp = -1;
    int attack = -1;
    int defense = -1;
    int magic_resistance = -1;
    int block = -1;
};

std::optional<std::pair<int, int>> read_duel_counter(const cv::Mat& image)
{
    if (image.empty()) {
        return std::nullopt;
    }
    asst::OCRer counter(image);
    counter.set_roi({ image.cols * 3 / 10, 0, image.cols * 4 / 10, image.rows / 5 });
    counter.set_use_raw(true);
    const auto result = counter.analyze();
    if (!result) {
        return std::nullopt;
    }
    for (const auto& item : *result) {
        std::string text = item.text;
        for (char& ch : text) {
            if (ch == 'O' || ch == 'o') {
                ch = '0';
            }
            else if (ch == 'I' || ch == 'l') {
                ch = '1';
            }
        }
        std::smatch match;
        static const std::regex pattern(R"(([0-2])\s*(?:/|／)\s*([12]))");
        if (std::regex_search(text, match, pattern)) {
            const int selected = std::stoi(match[1].str());
            const int limit = std::stoi(match[2].str());
            if (selected <= limit) {
                return std::pair { selected, limit };
            }
        }
    }
    return std::nullopt;
}

std::vector<DuelCardCandidate> detect_duel_cards(const cv::Mat& image)
{
    if (image.empty()) {
        return {};
    }

    const int width = image.cols;
    const int height = image.rows;
    // 敌人卡牌的可识别特征是重复的青绿色卡面。上一版对整张图做 Canny，
    // 会把瀑布/植被等动态背景合并成巨大的轮廓，点击位置因此落在背景上。
    // 这里只在标题下方、刷新按钮上方的相对区域取 HSV 掩膜；区域和卡牌框
    // 均从当前帧尺寸推导，不依赖敌人名称或固定 x 坐标。
    const int roi_y = static_cast<int>(height * 0.22);
    const int roi_bottom = static_cast<int>(height * 0.63);
    const cv::Rect roi(0, roi_y, static_cast<int>(width * 0.90), std::max(1, roi_bottom - roi_y));
    cv::Mat hsv;
    cv::cvtColor(image(roi), hsv, cv::COLOR_BGR2HSV);
    cv::Mat mask;
    // 青绿色卡牌在不同主题背景下亮度会变化，色相范围保持宽一些，
    // 再用饱和度/亮度过滤金色背景和黑色边框。
    // BlackFlow 的狭路相逢卡面在当前官号主题下是青绿色（OpenCV HSV
    // hue 约 45--90），而不是旧版只覆盖的青色 75--125。动态收窄到
    // 45--100 可以覆盖绿/青两种卡面，同时避开大部分低饱和背景植被。
    cv::inRange(hsv, cv::Scalar(45, 70, 60), cv::Scalar(100, 255, 255), mask);
    // 卡牌之间的缝隙通常约 6~8px，5x5 闭运算可以填补卡面内部断裂，
    // 又不会把同组两张卡粘成一个大块。
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_RECT, { 5, 5 }));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_RECT, { 3, 3 }));

    cv::Mat labels, stats, centroids;
    const int count = cv::connectedComponentsWithStats(mask, labels, stats, centroids, 8, CV_32S);
    std::vector<DuelCardCandidate> result;
    result.reserve(static_cast<size_t>(std::max(0, count - 1)));
    for (int i = 1; i < count; ++i) {
        const int x = stats.at<int>(i, cv::CC_STAT_LEFT);
        const int y = stats.at<int>(i, cv::CC_STAT_TOP);
        const int w = stats.at<int>(i, cv::CC_STAT_WIDTH);
        const int h = stats.at<int>(i, cv::CC_STAT_HEIGHT);
        const int area = stats.at<int>(i, cv::CC_STAT_AREA);
        const double fill = static_cast<double>(area) / std::max(1, w * h);
        if (area < width * height * 0.002 || area > width * height * 0.045 || w < width * 0.045 || w > width * 0.25 ||
            h < height * 0.06 || h > height * 0.28 || fill < 0.20) {
            continue;
        }
        const asst::Rect rect { x + roi.x, y + roi.y, w, h };
        result.push_back({ rect, fill });
    }

    std::ranges::sort(result, [](const auto& lhs, const auto& rhs) {
        return std::tie(lhs.rect.x, lhs.rect.y) < std::tie(rhs.rect.x, rhs.rect.y);
    });
    // 立绘和卡面图标也可能满足颜色/尺寸过滤，但同一帧六张卡会形成最
    // 密集的横向 y 带。保留候选最多的一条 y 带，避免把上方图标混入分组。
    if (result.size() > 6) {
        std::vector<DuelCardCandidate> band;
        for (const auto& seed : result) {
            std::vector<DuelCardCandidate> current;
            for (const auto& candidate : result) {
                // 图标/立绘连通域通常比卡面低约 20px；使用更窄的带宽，
                // 保留六张卡共同的 y 基线，避免把右侧图标再分成第三张卡。
                if (std::abs(candidate.rect.y - seed.rect.y) <= std::max(10, height / 40)) {
                    current.push_back(candidate);
                }
            }
            if (current.size() > band.size()) {
                band = std::move(current);
            }
        }
        if (band.size() >= 3) {
            result = std::move(band);
            std::ranges::sort(result, [](const auto& lhs, const auto& rhs) {
                return std::tie(lhs.rect.x, lhs.rect.y) < std::tie(rhs.rect.x, rhs.rect.y);
            });
        }
    }
    return result;
}

std::vector<DuelOpponentGroup> cluster_duel_groups(std::vector<DuelCardCandidate> cards, int image_width)
{
    static_cast<void>(image_width);
    if (cards.size() < 3) {
        return {};
    }

    // 页面卡牌数量和间距会随“搏杀/共斗”及分辨率变化。组间横向间隔明显大于
    // 同组卡牌间隔，因此选取当前帧中最大的两个 x 间隙作为分界，避免把卡牌
    // 按固定三等分切开。
    std::ranges::sort(cards, [](const auto& lhs, const auto& rhs) {
        return std::tie(lhs.rect.x, lhs.rect.y) < std::tie(rhs.rect.x, rhs.rect.y);
    });
    std::vector<std::pair<int, int>> gaps;
    gaps.reserve(cards.size() - 1);
    for (size_t i = 0; i + 1 < cards.size(); ++i) {
        const int gap = cards[i + 1].rect.x - (cards[i].rect.x + cards[i].rect.width);
        gaps.emplace_back(gap, static_cast<int>(i));
    }
    std::ranges::sort(gaps, std::greater {}, [](const auto& item) { return item.first; });
    if (gaps.size() < 2) {
        return {};
    }
    std::array<int, 2> boundaries { gaps[0].second, gaps[1].second };
    std::ranges::sort(boundaries);
    std::vector<DuelOpponentGroup> groups;
    groups.resize(3);
    for (size_t i = 0; i < cards.size(); ++i) {
        size_t group = 0;
        if (static_cast<int>(i) > boundaries[0]) {
            group = 1;
        }
        if (static_cast<int>(i) > boundaries[1]) {
            group = 2;
        }
        groups[group].cards.push_back(std::move(cards[i]));
    }
    return groups;
}

std::optional<int> parse_duel_operator_number(std::string text, std::string_view label)
{
    for (char& ch : text) {
        if (ch == 'O' || ch == 'o' || ch == 'I' || ch == 'l' || ch == '|') {
            ch = (ch == 'O' || ch == 'o') ? '0' : '1';
        }
    }
    const std::regex pattern(std::string(label) + R"([^0-9]{0,8}([0-9]{1,5}))");
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) {
        return std::nullopt;
    }
    try {
        return std::stoi(match[1].str());
    }
    catch (...) {
        return std::nullopt;
    }
}

std::optional<int> parse_duel_operator_health(std::string text)
{
    for (char& ch : text) {
        if (ch == 'O' || ch == 'o') {
            ch = '0';
        }
        else if (ch == 'I' || ch == 'l' || ch == '|') {
            ch = '1';
        }
    }
    static const std::regex health_pattern(R"((\d{3,5})\s*/\s*\d{3,5})");
    std::smatch match;
    if (!std::regex_search(text, match, health_pattern)) {
        return std::nullopt;
    }
    try {
        return std::stoi(match[1].str());
    }
    catch (...) {
        return std::nullopt;
    }
}

std::optional<int> parse_duel_operator_level(std::string text)
{
    for (char& ch : text) {
        if (ch == 'O' || ch == 'o' || ch == 'I' || ch == 'l' || ch == '|') {
            ch = (ch == 'O' || ch == 'o') ? '0' : '1';
        }
    }
    std::smatch match;
    static const std::regex level_pattern(R"((?:LV|Lv|lv)\s*[^0-9]{0,4}([0-9]{1,3}))");
    if (!std::regex_search(text, match, level_pattern)) {
        return std::nullopt;
    }
    try {
        return std::stoi(match[1].str());
    }
    catch (...) {
        return std::nullopt;
    }
}

std::optional<int> parse_duel_operator_level(const asst::OCRer::ResultsVec& results, int image_height)
{
    // 面板的等级行位于名称正下方。完整屏幕 OCR 偶尔把 “Lv90” 识成
    // “xvO” 或 “x™55”，因此先在等级行的窄 ROI 内找标准 Lv，再退化
    // 为该行的两/三位数字。数字行的纵坐标来自当前详情图尺寸，不写死
    // 屏幕坐标。
    const int level_top = static_cast<int>(image_height * 0.27);
    const int level_bottom = static_cast<int>(image_height * 0.38);
    static const std::regex number_pattern(R"((\d{2,3}))");
    for (const auto& result : results) {
        if (result.rect.y < level_top || result.rect.y > level_bottom) {
            continue;
        }
        if (auto level = parse_duel_operator_level(result.text); level && *level > 0) {
            return level;
        }
        std::string normalized = result.text;
        for (char& ch : normalized) {
            if (ch == 'O' || ch == 'o') {
                ch = '0';
            }
            else if (ch == 'I' || ch == 'l' || ch == '|') {
                ch = '1';
            }
        }
        std::smatch match;
        if (std::regex_search(normalized, match, number_pattern)) {
            try {
                const int level = std::stoi(match[1].str());
                if (level > 0 && level <= 120) {
                    return level;
                }
            }
            catch (...) {
                // OCR 噪声不应中断其它候选。
            }
        }
    }
    const std::string all_text =
        std::accumulate(results.begin(), results.end(), std::string {}, [](std::string text, const auto& result) {
            if (!text.empty()) {
                text += ' ';
            }
            text += result.text;
            return text;
        });
    return parse_duel_operator_level(all_text);
}

bool is_duel_operator_detail_text(const std::string& text)
{
    return text.find("生命") != std::string::npos || text.find("攻击") != std::string::npos ||
           text.find("防御") != std::string::npos || text.find("法抗") != std::string::npos ||
           text.find("阻挡") != std::string::npos || text.find("LV") != std::string::npos ||
           text.find("Lv") != std::string::npos;
}

std::vector<int> extract_ocr_numbers(const asst::OCRer::ResultsVec& results)
{
    std::vector<int> numbers;
    static const std::regex number_pattern(R"((\d{2,6}))");
    for (const auto& result : results) {
        // 面板中的 0/1 经常被 OCR 识成 O/l/I；只在已经拿到属性详情的
        // OCR 文本上做保守归一化，避免把页面其它数字混进来。
        std::string normalized = result.text;
        for (char& ch : normalized) {
            if (ch == 'O' || ch == 'o') {
                ch = '0';
            }
            else if (ch == 'I' || ch == 'l' || ch == '|') {
                ch = '1';
            }
        }
        for (std::sregex_iterator it(normalized.begin(), normalized.end(), number_pattern), end; it != end; ++it) {
            try {
                numbers.push_back(std::stoi(it->str()));
            }
            catch (...) {
                // OCR 噪声不应中断其它候选的评估。
            }
        }
    }
    return numbers;
}

std::pair<double, int> extract_duel_ratings(const asst::OCRer::ResultsVec& results)
{
    static constexpr std::array<std::pair<std::string_view, double>, 7> ratings {
        std::pair<std::string_view, double> { "S+", 1.35 }, std::pair<std::string_view, double> { "S", 1.20 },
        std::pair<std::string_view, double> { "A", 1.00 },  std::pair<std::string_view, double> { "B", 0.80 },
        std::pair<std::string_view, double> { "C", 0.60 },  std::pair<std::string_view, double> { "D", 0.40 },
        std::pair<std::string_view, double> { "E", 0.20 },
    };
    double score = 0.0;
    int count = 0;
    for (const auto& result : results) {
        const bool stat_line =
            result.text.find("生命") != std::string::npos || result.text.find("攻击") != std::string::npos ||
            result.text.find("防御") != std::string::npos || result.text.find("法抗") != std::string::npos ||
            result.text.find("抗性") != std::string::npos;
        if (!stat_line) {
            continue;
        }
        for (const auto& [rating, value] : ratings) {
            if (result.text.find(rating) != std::string::npos) {
                score += value;
                ++count;
                break;
            }
        }
    }
    return { score, count };
}
}

using namespace asst::blackflow::duel;

asst::blackflow::BlackFlowDuelTaskPlugin::BlackFlowDuelTaskPlugin(
    const AsstCallback& callback,
    Assistant* inst,
    std::string_view task_chain,
    const std::shared_ptr<RoguelikeConfig>& config,
    const std::shared_ptr<RoguelikeControlTaskPlugin>& control,
    std::shared_ptr<BlackFlowSession> session) :
    AbstractRoguelikeTaskPlugin(callback, inst, task_chain, config, control),
    BattleHelper(inst),
    m_blackflow_session(std::move(session))
{
}

bool asst::blackflow::BlackFlowDuelTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    if (m_config->get_theme() != RoguelikeTheme::BlackFlow || details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }
    const std::string task = details.get("details", "task", "");
    if (msg == AsstMsg::SubTaskStart && task == BlackFlowDuelOpponentSelectionTask) {
        m_pending = PendingWork::Prepare;
        m_duel_refresh_attempted = false;
        return true;
    }
    if (msg == AsstMsg::SubTaskCompleted &&
        (task == BlackFlowDuelCombatTask || task == "BlackFlow@Roguelike@DuelBattleResume")) {
        m_pending = PendingWork::Battle;
        return true;
    }
    return false;
}

bool asst::blackflow::BlackFlowDuelTaskPlugin::load_params(const json::value& params)
{
    m_requested_duel_group = params.get("blackflow_duel_group", 0);
#ifdef ASST_DEBUG
    m_duel_stage = params.get("duel_battle_resume", std::string());
    if (!m_duel_stage.empty() && m_duel_stage != "搏杀" && m_duel_stage != "共斗") {
        return false;
    }
#endif
    return m_config->get_theme() == RoguelikeTheme::BlackFlow && m_requested_duel_group >= 0 &&
           m_requested_duel_group <= 3;
}

void asst::blackflow::BlackFlowDuelTaskPlugin::reset_in_run_variables()
{
    m_pending = PendingWork::None;
    m_duel_refresh_attempted = false;
    m_duel_stage.clear();
    BattleHelper::clear();
}

bool asst::blackflow::BlackFlowDuelTaskPlugin::_run()
{
    const PendingWork work = std::exchange(m_pending, PendingWork::None);
    const bool result = work == PendingWork::Prepare  ? run_blackflow_duel_opponent_selection()
                        : work == PendingWork::Battle ? run_blackflow_duel_battle()
                                                      : true;
    if (!result) {
        m_task_ptr->set_enable(false);
    }
    return result;
}

bool asst::blackflow::BlackFlowDuelTaskPlugin::save_img(const cv::Mat& image, std::string_view description)
{
    return utils::save_debug_image(image, utils::path("debug") / "roguelike" / "duel", true, description);
}

bool asst::blackflow::BlackFlowDuelTaskPlugin::run_blackflow_duel_opponent_selection()
{
    LogTraceFunction;
    cv::Mat image;
    std::vector<DuelOpponentGroup> groups;
    // 页面标题先出现、卡面后出现是正常的过渡帧；连续采样并优先保留
    // 当前帧检测到卡牌数量最多的一帧，再以覆盖面积打破平局，避免把
    // 过渡动画中的背景大块当成三组敌人。
    int64_t best_coverage = -1;
    size_t best_card_count = 0;
    for (int attempt = 0; attempt < 8 && !need_exit(); ++attempt) {
        cv::Mat frame = ctrler()->get_image();
        auto cards = detect_duel_cards(frame);
        auto frame_groups = cluster_duel_groups(std::move(cards), frame.cols);
        if (frame_groups.size() == 3) {
            const int64_t coverage = std::accumulate(
                frame_groups.begin(),
                frame_groups.end(),
                int64_t { 0 },
                [](int64_t total, const auto& group) {
                    return total + std::accumulate(
                                       group.cards.begin(),
                                       group.cards.end(),
                                       int64_t { 0 },
                                       [](int64_t area, const auto& card) {
                                           return area + static_cast<int64_t>(card.rect.width) * card.rect.height;
                                       });
                });
            const size_t card_count = std::accumulate(
                frame_groups.begin(),
                frame_groups.end(),
                size_t { 0 },
                [](size_t total, const auto& group) { return total + group.cards.size(); });
            if (card_count > best_card_count || (card_count == best_card_count && coverage > best_coverage)) {
                best_coverage = coverage;
                best_card_count = card_count;
                image = std::move(frame);
                groups = std::move(frame_groups);
            }
        }
        sleep(280);
    }
    if (groups.size() != 3) {
        LogError << "BlackFlow duel opponent page did not yield three card groups" << groups.size();
        auto info = basic_info_with_what("DuelEnemyCandidates");
        info["details"]["status"] = "unrecognized";
        info["details"]["group_count"] = groups.size();
        info["details"]["reason"] = "dynamic card contour detection did not find exactly three groups";
        callback(AsstMsg::SubTaskExtraInfo, info);
        return false;
    }

    const auto initial_counter = read_duel_counter(image);
    if (!initial_counter || initial_counter->first != 0) {
        LogError << "BlackFlow duel requires a recognized empty 0/1 or 0/2 preparation counter";
        return false;
    }
    const size_t duel_limit = static_cast<size_t>(initial_counter->second);
    for (auto& group : groups) {
        std::ranges::sort(group.cards, [](const auto& lhs, const auto& rhs) { return lhs.rect.y < rhs.rect.y; });
        if (group.cards.size() > duel_limit) {
            group.cards.resize(duel_limit);
        }
    }
    LogInfo << "BlackFlow duel observed deployment limit" << duel_limit;

    // 狭路相逢准备页底部的干员卡本身不显示名称，但点击后会在左侧打开
    // 详情面板。先从当前帧动态分割卡条，逐张点击并用 OCR 读取 LV、生命、
    // 攻击、防御、法抗、阻挡和技能文本；点回同一卡关闭详情后再处理下一张。
    // 这一步只停留在准备页，不会确认敌人或刷新敌人。
    std::vector<DuelOperatorDetail> operator_details;
    std::vector<Rect> operator_cards;
    BattlefieldMatcher deployment(image);
    deployment.set_object_of_interest({ .flag = false, .deployment = true });
    if (const auto detected = deployment.analyze()) {
        for (const auto& oper : detected->deployment) {
            operator_cards.emplace_back(oper.rect);
        }
    }
    if (operator_cards.empty()) {
        LogError << "BlackFlow duel operator cards were not recognized";
        return false;
    }
    // 详情页 OCR 是整页最昂贵的一步。卡条按当前界面战力顺序排列，
    // 先扫描前 8 张动态卡框即可覆盖高战力候选；其余卡仍保留卡框，
    // 只在前 8 张不足两张时作为低分兜底，避免在更新前把准备页卡住。
    const size_t detail_scan_count = std::min<size_t>(operator_cards.size(), 8);
    for (size_t card_index = 0; card_index < detail_scan_count; ++card_index) {
        const auto& card = operator_cards[card_index];
        if (need_exit()) {
            break;
        }
        const Point click_point { card.x + card.width / 2,
                                  std::clamp(card.y + card.height * 3 / 5, 0, image.rows - 1) };
        if (!ctrler()->click(click_point) || !sleep(180)) {
            continue;
        }
        const cv::Mat detail_image = ctrler()->get_image();
        OCRer detail_ocr(detail_image);
        // 干员详情的姓名、等级、属性和技能都位于左侧面板；右侧的敌人
        // 卡片、刷新按钮和开始按钮不参与干员评分。限制 OCR ROI 可以避开
        // 每张卡都重复识别整张 1280x720 画面的开销。
        detail_ocr.set_roi(
            { 0,
              static_cast<int>(detail_image.rows * 0.20),
              static_cast<int>(detail_image.cols * 0.45),
              static_cast<int>(detail_image.rows * 0.62) });
        detail_ocr.set_use_raw(true);
        const auto detail_result = detail_ocr.analyze();
        if (detail_result) {
            DuelOperatorDetail detail;
            detail.card_rect = card;
            const int name_top = static_cast<int>(detail_image.rows * 0.22);
            const int name_bottom = static_cast<int>(detail_image.rows * 0.31);
            for (const auto& result : *detail_result) {
                if (!result.text.empty()) {
                    if (!detail.ocr_text.empty()) {
                        detail.ocr_text += " | ";
                    }
                    detail.ocr_text += result.text;
                }
                if (result.rect.x < detail_image.cols * 0.42 && result.rect.y >= detail_image.rows * 0.56 &&
                    result.rect.y < detail_image.rows * 0.64 && result.text.size() >= 2 &&
                    result.text.find("开始") == std::string::npos && result.text.find("刷新") == std::string::npos) {
                    if (detail.skill_name.empty() || result.text.size() > detail.skill_name.size()) {
                        detail.skill_name = result.text;
                    }
                }
                if (result.rect.x < detail_image.cols * 0.45 && result.rect.y >= detail_image.rows * 0.60 &&
                    result.rect.y < detail_image.rows * 0.84 && result.text.size() >= 2 &&
                    result.text.find("开始战斗") == std::string::npos &&
                    result.text.find("刷新敌人") == std::string::npos) {
                    if (!detail.skill_text.empty()) {
                        detail.skill_text += " | ";
                    }
                    detail.skill_text += result.text;
                }
                if (result.rect.x < detail_image.cols * 0.30 && result.rect.y >= name_top &&
                    result.rect.y <= name_bottom && result.text.size() >= 2 &&
                    !is_duel_operator_detail_text(result.text) && result.text.find("选择") == std::string::npos &&
                    !std::ranges::any_of(result.text, [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
                    if (result.text.find("预备干员-") != std::string::npos ||
                        result.text.find("预备干员") != std::string::npos) {
                        detail.role = result.text;
                    }
                    else if (detail.name.empty() || result.text.size() > detail.name.size()) {
                        detail.name = result.text;
                    }
                }
            }
            detail.level = parse_duel_operator_level(*detail_result, detail_image.rows).value_or(-1);
            if (detail.level <= 0) {
                // 只有主 ROI 没读到等级时才回退到窄字符 OCR，避免每张卡固定
                // 进行第二次识别。
                OCRer level_ocr(detail_image);
                level_ocr.set_roi(
                    { 0,
                      static_cast<int>(detail_image.rows * 0.25),
                      static_cast<int>(detail_image.cols * 0.22),
                      static_cast<int>(detail_image.rows * 0.16) });
                level_ocr.set_use_raw(true);
                level_ocr.set_use_char_model(true);
                if (const auto level_result = level_ocr.analyze(); level_result && !level_result->empty()) {
                    const auto level_from_roi = parse_duel_operator_level(*level_result, detail_image.rows);
                    if (level_from_roi) {
                        detail.level = *level_from_roi;
                    }
                }
            }
            detail.hp = parse_duel_operator_health(detail.ocr_text)
                            .value_or(parse_duel_operator_number(detail.ocr_text, "生命").value_or(-1));
            detail.attack = parse_duel_operator_number(detail.ocr_text, "攻击").value_or(-1);
            detail.defense = parse_duel_operator_number(detail.ocr_text, "防御").value_or(-1);
            detail.magic_resistance = parse_duel_operator_number(detail.ocr_text, "法抗").value_or(-1);
            detail.block = parse_duel_operator_number(detail.ocr_text, "阻挡").value_or(-1);
            // LV>80 是精二的确定证据；其它等级保留未知，不把等级上限反推成
            // 精英化，避免低星干员和未满级干员被误判。
            detail.elite = detail.level >= 81 ? 2 : -1;
            const bool has_detail = detail.level > 0 || detail.hp > 0 || detail.attack > 0 || detail.defense > 0;
            if (has_detail) {
                operator_details.emplace_back(std::move(detail));
            }
        }
        // 当前 UI 点同一卡即可关闭详情；即使 OCR 没有读到数字，也必须关闭后
        // 才能保证下一张卡不是在上一张详情覆盖层上点击。
        ctrler()->click(click_point);
        sleep(180);
    }

    auto& opers = m_config->status().opers;
    for (const auto& detail : operator_details) {
        if (!detail.name.empty() && detail.level > 0) {
            opers[detail.name] = { .elite = detail.elite >= 0 ? detail.elite : 0, .level = detail.level };
        }
    }
    const bool detail_power_available = std::ranges::any_of(operator_details, [](const auto& detail) {
        return detail.level > 0 || detail.attack > 0 || detail.defense > 0 || detail.hp > 0;
    });
    const bool team_power_available = detail_power_available || !opers.empty();
    std::vector<double> operator_power;
    if (detail_power_available) {
        operator_power.reserve(operator_details.size());
        for (const auto& detail : operator_details) {
            double power = detail.level > 0 ? 30.0 + std::max(0, detail.elite) * 25.0 + detail.level * 0.30 : 0.0;
            if (detail.attack > 0) {
                power += detail.attack * 0.04;
            }
            if (detail.defense > 0) {
                power += detail.defense * 0.02;
            }
            if (detail.hp > 0) {
                power += detail.hp * 0.005;
            }
            if (power > 0.0) {
                operator_power.emplace_back(power);
            }
        }
    }
    else {
        operator_power.reserve(opers.size());
        for (const auto& [name, oper] : opers) {
            static_cast<void>(name);
            operator_power.emplace_back(30.0 + oper.elite * 25.0 + oper.level * 0.30);
        }
    }
    std::ranges::sort(operator_power, std::greater {});
    const size_t deployable = std::min(operator_power.size(), duel_limit);
    double team_power = std::accumulate(operator_power.begin(), operator_power.begin() + deployable, 0.0);

    json::array candidate_report;
    json::array operator_report;
    for (const auto& detail : operator_details) {
        operator_report.emplace_back(
            json::object {
                { "card_rect",
                  json::object {
                      { "x", detail.card_rect.x },
                      { "y", detail.card_rect.y },
                      { "width", detail.card_rect.width },
                      { "height", detail.card_rect.height },
                  } },
                { "name", detail.name },
                { "role", detail.role },
                { "skill_name", detail.skill_name },
                { "skill_text", detail.skill_text },
                { "elite", detail.elite },
                { "level", detail.level },
                { "hp", detail.hp },
                { "attack", detail.attack },
                { "defense", detail.defense },
                { "magic_resistance", detail.magic_resistance },
                { "block", detail.block },
                { "detail_ocr", detail.ocr_text },
            });
    }
    for (size_t index = 0; index < groups.size() && !need_exit(); ++index) {
        auto& group = groups[index];
        json::array card_rects;
        for (const auto& card_candidate : group.cards) {
            card_rects.emplace_back(
                json::object {
                    { "x", card_candidate.rect.x },
                    { "y", card_candidate.rect.y },
                    { "width", card_candidate.rect.width },
                    { "height", card_candidate.rect.height },
                });
        }
        if (group.cards.empty()) {
            group.confidence = 0.0;
            group.evidence = "no card";
            candidate_report.emplace_back(
                json::object {
                    { "group", index + 1 },
                    { "card_count", 0 },
                    { "card_rects", std::move(card_rects) },
                    { "threat", 0.0 },
                    { "confidence", 0.0 },
                    { "win_probability", 0.0 },
                    { "reward_value", index == 0 ? 24.0 : (index == 1 ? 70.0 : 50.0) },
                    { "reward_kind",
                      index == 0 ? "part_low_mid" : (index == 1 ? "part_mid_high_or_natural" : "collectible") },
                    { "utility", -8.0 },
                    { "viable", false },
                    { "evidence", group.evidence },
                    { "detail_ocr", group.detail_text },
                });
            continue;
        }

        // 一个组可能包含多张卡。卡面框来自当前帧 HSV 连通域，优先点击卡面中心；
        // 若游戏把详情热区放在立绘上，再尝试按当前卡面高度向上偏移的位置。
        // 每次点击都用 OCR 确认详情页，避免在详情页未打开时误处理下一组。
        cv::Mat detail_image;
        std::optional<asst::OCRer::ResultsVec> detail_result;
        bool detail_opened = false;
        std::vector<Point> detail_click_points;
        auto ordered_cards = group.cards;
        std::ranges::sort(ordered_cards, [](const auto& lhs, const auto& rhs) { return lhs.rect.x > rhs.rect.x; });
        for (const auto& card_candidate : ordered_cards) {
            const Rect card = card_candidate.rect;
            detail_click_points.emplace_back(card.x + card.width / 2, card.y);
            // 卡面上方的敌方立绘会随窗口缩放而改变，但始终位于卡面框的
            // 相对区域内。优先右侧立绘（用户界面中点击敌人右侧打开详情），
            // 再尝试同一框的中间位置；成功与否由详情 OCR 确认。
            // 连通域可能覆盖同组的两张叠放卡；用当前框的相对左右两个
            // 立绘中心取样，单张卡则取中心，避免依赖固定屏幕坐标。
            // 详情热区位于卡面上方，右侧卡组的立绘在当前 UI 中会再
            // 上移约一个卡面高度。候选顺序按当前卡面相对位置自适应，
            // 不使用固定屏幕坐标：普通卡先探测 -0.8h/-0.7h，右侧
            // 卡组先探测 -1.0h/-0.9h；失败时再探测另一个相对位置。
            const bool far_right_card = card.x > image.cols * 0.65;
            // 使用有理数比例保留卡面内的亚格点位置。不同列的敌人
            // 立绘并不总在卡框中心，整数十分位会错过窄热区；这些
            // 比例仍完全由当前卡框和画面尺寸计算，不依赖屏幕坐标。
            // 一张卡只保留三个由卡框推导出的探测点：当前版本最稳定的
            // 立绘中心、同一高度的中点，以及同一横向位置的下移点。旧实现
            // 为每张卡生成最多 70 个点，导致一组详情打开前要做几十次全屏
            // OCR；当前帧已经证明首点通常命中，后两点只作为轻量回退。
            const std::array<std::tuple<int, int, int, int>, 3> probe_ratios = far_right_card
                                                                                  ? std::array<std::tuple<int, int, int, int>, 3> {
                                                                                        std::tuple { 3, 7, -13, 20 },
                                                                                        std::tuple { 1, 2, -13, 20 },
                                                                                        std::tuple { 3, 7, -1, 1 },
                                                                                    }
                                                                                  : std::array<std::tuple<int, int, int, int>, 3> {
                                                                                        std::tuple { 9, 16, -17, 25 },
                                                                                        std::tuple { 1, 2, -17, 25 },
                                                                                        std::tuple { 9, 16, -1, 1 },
                                                                                    };
            for (const auto [x_numerator, x_denominator, y_numerator, y_denominator] : probe_ratios) {
                detail_click_points.emplace_back(
                    card.x + card.width * x_numerator / x_denominator,
                    std::clamp(card.y + card.height * y_numerator / y_denominator, 0, image.rows - 1));
            }
        }
        for (const auto& click_point : detail_click_points) {
            if (!ctrler()->click(click_point) || !sleep(350)) {
                continue;
            }
            detail_image = ctrler()->get_image();
            OCRer detail_probe(detail_image);
            detail_probe.set_roi({ 0, 0, detail_image.cols, detail_image.rows });
            detail_probe.set_use_raw(true);
            auto probe_result = detail_probe.analyze();
            if (!probe_result) {
                continue;
            }
            const int detail_tag_count = static_cast<int>(std::ranges::count_if(*probe_result, [&](const auto& result) {
                const bool tag =
                    result.text.find("全部") != std::string::npos || result.text.find("精英") != std::string::npos ||
                    result.text.find("领袖") != std::string::npos || result.text.find("普通") != std::string::npos;
                return tag && result.rect.x < detail_image.cols / 2 && result.rect.y > detail_image.rows / 5;
            }));
            const auto is_stat_label = [](const auto& result) {
                return result.text.find("生命") != std::string::npos || result.text.find("攻击") != std::string::npos ||
                       result.text.find("防御") != std::string::npos || result.text.find("法抗") != std::string::npos ||
                       result.text.find("抗性") != std::string::npos || result.text.find("重量") != std::string::npos ||
                       result.text.find("阻挡") != std::string::npos || result.text.find("特性") != std::string::npos ||
                       result.text.find("天赋") != std::string::npos;
            };
            const int stat_label_count = static_cast<int>(std::ranges::count_if(*probe_result, [&](const auto& result) {
                return result.rect.x < detail_image.cols / 2 && result.rect.y > detail_image.rows / 5 &&
                       is_stat_label(result);
            }));
            // 预备页右侧只有刷新/开始按钮；点击敌人后会在同一相对区域
            // 出现敌名和至少一行描述。用 OCR 文本的空间聚类确认提示框，
            // 不依赖敌人名称或固定坐标。
            const int tooltip_text_count =
                static_cast<int>(std::ranges::count_if(*probe_result, [&](const auto& result) {
                    const bool in_tooltip = result.rect.x > detail_image.cols * 0.72 &&
                                            result.rect.y > detail_image.rows * 0.12 &&
                                            result.rect.y < detail_image.rows * 0.35;
                    const bool not_page_control = result.text.find("刷新敌人") == std::string::npos &&
                                                  result.text.find("开始战斗") == std::string::npos &&
                                                  result.text.find("选择干员") == std::string::npos;
                    return in_tooltip && not_page_control && result.text.size() >= 2;
                }));
            // 详情面板是覆盖层，底下的“刷新敌人/开始战斗”仍会被 OCR 看见，
            // 因此不能用 on_prebattle_page 作为否决条件；属性标签/敌情标签才是
            // 当前帧确实打开了敌人详情的证据。
            if (detail_tag_count >= 1 || stat_label_count >= 3 || tooltip_text_count >= 2) {
                detail_result = std::move(probe_result);
                detail_opened = true;
                break;
            }
        }
        if (!detail_opened) {
            group.evidence = "detail_open_failed";
            group.confidence = 0.0;
            candidate_report.emplace_back(
                json::object {
                    { "group", index + 1 },
                    { "card_count", group.cards.size() },
                    { "card_rects", std::move(card_rects) },
                    { "threat", group.threat },
                    { "confidence", group.confidence },
                    { "win_probability", 0.35 },
                    { "reward_value", index == 0 ? 24.0 : (index == 1 ? 70.0 : 50.0) },
                    { "reward_kind",
                      index == 0 ? "part_low_mid" : (index == 1 ? "part_mid_high_or_natural" : "collectible") },
                    { "utility", 0.35 * (index == 0 ? 24.0 : (index == 1 ? 70.0 : 50.0)) - 0.65 * 8.0 },
                    { "viable", false },
                    { "evidence", group.evidence },
                    { "detail_ocr", group.detail_text },
                });
            continue;
        }

        if (detail_result) {
            for (const auto& result : *detail_result) {
                if (!result.text.empty()) {
                    if (!group.detail_text.empty()) {
                        group.detail_text += " | ";
                    }
                    group.detail_text += result.text;
                }
            }
        }
        const auto numbers = detail_result ? extract_ocr_numbers(*detail_result) : std::vector<int> {};
        const auto [rating_score, rating_count] =
            detail_result ? extract_duel_ratings(*detail_result) : std::pair<double, int> {};
        const bool has_stat_label =
            detail_result && std::ranges::any_of(*detail_result, [&](const auto& result) {
                return result.rect.x < detail_image.cols / 2 && result.rect.y > detail_image.rows / 5 &&
                       (result.text.find("生命") != std::string::npos ||
                        result.text.find("攻击") != std::string::npos ||
                        result.text.find("防御") != std::string::npos || result.text.find("法抗") != std::string::npos);
            });
        const bool tooltip_evidence =
            detail_result && std::ranges::count_if(*detail_result, [&](const auto& result) {
                                 const bool in_tooltip = result.rect.x > detail_image.cols * 0.72 &&
                                                         result.rect.y > detail_image.rows * 0.12 &&
                                                         result.rect.y < detail_image.rows * 0.35;
                                 const bool not_page_control = result.text.find("刷新敌人") == std::string::npos &&
                                                               result.text.find("开始战斗") == std::string::npos &&
                                                               result.text.find("选择干员") == std::string::npos;
                                 return in_tooltip && not_page_control && result.text.size() >= 2;
                             }) >= 2;
        const bool detail_page =
            detail_result && std::ranges::any_of(*detail_result, [](const auto& result) {
                return result.text.find("全部") != std::string::npos || result.text.find("精英") != std::string::npos ||
                       result.text.find("领袖") != std::string::npos || result.text.find("生命") != std::string::npos ||
                       result.text.find("攻击") != std::string::npos || result.text.find("防御") != std::string::npos ||
                       result.text.find("法抗") != std::string::npos || result.text.find("抗性") != std::string::npos ||
                       result.text.find("重量") != std::string::npos;
            });
        const int text_size =
            detail_result
                ? std::accumulate(
                      detail_result->begin(),
                      detail_result->end(),
                      0,
                      [](int value, const auto& result) { return value + static_cast<int>(result.text.size()); })
                : 0;
        const double numeric_sum = std::accumulate(numbers.begin(), numbers.end(), 0.0);
        // 预备页的敌人详情是右上方动态提示框，通常只有敌名和描述，
        // 不会出现左侧属性标签。只要提示框空间聚类成立，就以它作为
        // 详情证据，不能再被 detail_page 的旧属性页判断覆盖。
        if (tooltip_evidence) {
            group.threat = 95.0;
            group.confidence = 0.55;
            group.evidence = "enemy tooltip OCR";
        }
        else if (!detail_page) {
            group.threat = 140.0;
            group.confidence = 0.1;
            group.evidence = "detail_open_failed";
        }
        else if (rating_count >= 2) {
            // 敌情面板通常给出 E~S+ 评级而非绝对数值；按评级计算相对威胁，避免把界面其它数字混入。
            group.threat = rating_score / rating_count * 110.0;
            group.confidence = std::clamp(0.65 + rating_count * 0.05, 0.65, 0.95);
            group.evidence = "details OCR + E-S+ ratings";
        }
        else if (has_stat_label && !numbers.empty()) {
            // 某些版本面板会直接显示数值，只有在检测到属性标签后才使用这些数字。
            group.threat = std::log1p(numeric_sum) * 18.0;
            group.confidence = std::clamp(0.55 + numbers.size() * 0.05, 0.55, 0.9);
            group.evidence = "details OCR + labeled numeric stats";
        }
        else if (has_stat_label || text_size > 0) {
            group.threat = 95.0;
            group.confidence = 0.35;
            group.evidence = "details OCR without numeric stats";
        }
        else {
            group.threat = 120.0;
            group.confidence = 0.15;
            group.evidence = "details OCR unavailable";
        }

        bool closed = false;
        // 关闭后核对原提示框文本，避免把同一份敌情重复计入下一组。
        const auto detail_overlay_visible = [&]() {
            const cv::Mat current = ctrler()->get_image();
            OCRer checker(current);
            checker.set_roi({ 0, 0, current.cols, current.rows });
            checker.set_use_raw(true);
            const auto checked = checker.analyze();
            if (!checked) {
                // OCR 失败时按“仍可能打开”处理，避免把未知状态当成已
                // 关闭而继续点击下一组敌卡。
                return true;
            }
            const int label_count = static_cast<int>(std::ranges::count_if(*checked, [&](const auto& result) {
                const bool label =
                    result.text.find("生命") != std::string::npos || result.text.find("攻击") != std::string::npos ||
                    result.text.find("防御") != std::string::npos || result.text.find("法抗") != std::string::npos ||
                    result.text.find("阻挡") != std::string::npos || result.text.find("特性") != std::string::npos ||
                    result.text.find("天赋") != std::string::npos;
                return label && result.rect.x < current.cols / 2 && result.rect.y > current.rows / 5;
            }));
            const int tooltip_count = static_cast<int>(std::ranges::count_if(*checked, [&](const auto& result) {
                const bool in_tooltip = result.rect.x > current.cols * 0.72 && result.rect.y > current.rows * 0.12 &&
                                        result.rect.y < current.rows * 0.35;
                const bool not_page_control = result.text.find("刷新敌人") == std::string::npos &&
                                              result.text.find("开始战斗") == std::string::npos &&
                                              result.text.find("选择干员") == std::string::npos;
                return in_tooltip && not_page_control && result.text.size() >= 6 && detail_result &&
                       std::ranges::any_of(*detail_result, [&](const auto& previous) {
                           return previous.rect.x > current.cols * 0.72 && previous.rect.y > current.rows * 0.12 &&
                                  previous.rect.y < current.rows * 0.35 && previous.text == result.text;
                       });
            }));
            return label_count >= 3 || tooltip_count >= 1;
        };
        // 本场实测空白点可关闭提示框；避开敌人、刷新和开始按钮。
        const Point close_point { image.cols * 887 / 1000, image.rows * 82 / 100 };
        closed = false;
        if (ctrler()->click(close_point) && sleep(600)) {
            for (int attempt = 0; attempt < 3 && !need_exit(); ++attempt) {
                if (!detail_overlay_visible()) {
                    closed = true;
                    break;
                }
                sleep(250);
            }
        }
        // 不使用返回键兜底：Android 返回键会暂停当前战斗预备页，
        // 反而把后续敌组识别带到暂停层。
        if (!closed) {
            group.evidence += ";detail_close_failed";
            save_img(ctrler()->get_image(), "duel_enemy_close_failed");
        }

        // 三列奖励类别是游戏规则：左=低/中零件，中=中/高零件或代步，右=收藏品；
        // 这里仅量化当前需求，敌组本身仍完全由当前帧 OCR/视觉结果决定。
        double reward = index == 0 ? 24.0 : (index == 1 ? 70.0 : 50.0);
        const std::string reward_kind =
            index == 0 ? "part_low_mid" : (index == 1 ? "part_mid_high_or_natural" : "collectible");
        if (index == 1 && m_blackflow_session != nullptr) {
            const int free_movement_slots = blackflow::movement_slots_remaining(m_blackflow_session->run().resources);
            if (free_movement_slots <= 0) {
                reward *= 0.80;
            }
        }
        const double win_probability =
            group.confidence < 0.25 ? 0.35 : 1.0 / (1.0 + std::exp((group.threat - team_power) / 70.0));
        // 搏杀与共斗失败不扣目标生命值；仅保留本次节点和奖励机会的损失。
        const double failure_cost = 8.0;
        const double utility = win_probability * reward - (1.0 - win_probability) * failure_cost;
        group.win_probability = win_probability;
        group.reward = reward;
        group.utility = utility;
        group.viable = group.confidence >= 0.45 && win_probability >= 0.60;

        json::object item {
            { "group", index + 1 },     { "card_count", group.cards.size() }, { "card_rects", std::move(card_rects) },
            { "threat", group.threat }, { "confidence", group.confidence },   { "win_probability", win_probability },
            { "reward_value", reward }, { "reward_kind", reward_kind },       { "utility", utility },
            { "viable", group.viable }, { "evidence", group.evidence },       { "detail_ocr", group.detail_text },
        };
        candidate_report.emplace_back(std::move(item));
        if (!closed) {
            // 详情覆盖层仍在时不能点击下一组，否则会把同一组详情重复计入。
            break;
        }
    }

    const bool any_viable = std::ranges::any_of(groups, [](const auto& group) { return group.viable; });
    const bool any_detail_evidence = std::ranges::any_of(groups, [](const auto& group) {
        return group.evidence.find("detail_open_failed") == std::string::npos &&
               group.evidence.find("detail unavailable") == std::string::npos && group.confidence > 0.0;
    });
    if (m_requested_duel_group == 0 && !any_viable && team_power_available && any_detail_evidence &&
        !m_duel_refresh_attempted) {
        OCRer refresh(image);
        refresh.set_roi({ image.cols * 3 / 4, image.rows / 4, image.cols / 4, image.rows / 2 });
        refresh.set_use_raw(true);
        if (const auto refresh_result = refresh.analyze()) {
            const auto button = std::ranges::find_if(*refresh_result, [](const auto& result) {
                return result.text.find("刷新") != std::string::npos;
            });
            if (button != refresh_result->end()) {
                m_duel_refresh_attempted = true;
                LogInfo << "BlackFlow duel candidates are unsafe; refreshing once before choosing";
                if (ctrler()->click(button->rect) && sleep(700)) {
                    return run_blackflow_duel_opponent_selection();
                }
            }
        }
    }

    auto best = std::ranges::max_element(groups, [](const auto& lhs, const auto& rhs) {
        if (lhs.viable != rhs.viable) {
            return !lhs.viable;
        }
        if (lhs.utility != rhs.utility) {
            return lhs.utility < rhs.utility;
        }
        return lhs.win_probability < rhs.win_probability;
    });
    if (m_requested_duel_group > 0) {
        best = groups.begin() + m_requested_duel_group - 1;
        LogInfo << "BlackFlow duel follows user-selected group" << m_requested_duel_group;
    }
    if (best == groups.end() || need_exit()) {
        return false;
    }
    if (m_requested_duel_group == 0 && !best->viable && best->win_probability < 0.50) {
        LogError << "BlackFlow duel has no opponent with a reliable win estimate";
        auto unsafe = basic_info_with_what("DuelEnemyCandidates");
        unsafe["details"]["groups"] = std::move(candidate_report);
        if (team_power_available) {
            unsafe["details"]["team_power"] = team_power;
        }
        unsafe["details"]["team_power_available"] = team_power_available;
        unsafe["details"]["operators"] = operator_report;
        unsafe["details"]["operator_count"] = operator_details.size();
        unsafe["details"]["deployment_limit"] = duel_limit;
        unsafe["details"]["power_operator_count"] = deployable;
        unsafe["details"]["team_power_source"] =
            detail_power_available ? "duel_operator_detail_ocr"
                                   : (team_power_available ? "roguelike_status.opers" : "unavailable");
        unsafe["details"]["win_probability_source"] =
            team_power_available ? "team_power_vs_ocr_threat" : "prior_without_team_power";
        unsafe["details"]["win_probability_is_estimate"] = true;
        unsafe["details"]["status"] = !team_power_available
                                          ? "team_power_unavailable"
                                          : (any_detail_evidence ? "no_safe_opponent" : "detail_unavailable");
        unsafe["details"]["unsafe_fallback"] = team_power_available && any_detail_evidence;
        unsafe["details"]["recommended_group"] = static_cast<size_t>(std::distance(groups.begin(), best)) + 1;
        unsafe["details"]["recommendation_reason"] = team_power_available
                                                         ? "max expected utility below safe threshold"
                                                         : "max expected utility; team power unavailable";
        unsafe["details"]["selection_policy"] = "max(expected reward after win probability and failure cost)";
        callback(AsstMsg::SubTaskExtraInfo, unsafe);
        return false;
    }
    const size_t selected_index = static_cast<size_t>(std::distance(groups.begin(), best));

    auto info = basic_info_with_what("DuelEnemyCandidates");
    info["details"]["groups"] = std::move(candidate_report);
    info["details"]["selected_group"] = selected_index + 1;
    info["details"]["recommended_group"] = selected_index + 1;
    info["details"]["unsafe_fallback"] = !best->viable;
    info["details"]["selection_source"] = m_requested_duel_group > 0 ? "user_selected_group" : "heuristic";
    if (team_power_available) {
        info["details"]["team_power"] = team_power;
    }
    info["details"]["team_power_available"] = team_power_available;
    info["details"]["operators"] = operator_report;
    info["details"]["operator_count"] = operator_details.size();
    info["details"]["deployment_limit"] = duel_limit;
    info["details"]["power_operator_count"] = deployable;
    info["details"]["team_power_source"] = detail_power_available
                                               ? "duel_operator_detail_ocr"
                                               : (team_power_available ? "roguelike_status.opers" : "unavailable");
    info["details"]["win_probability_source"] =
        team_power_available ? "team_power_vs_ocr_threat" : "prior_without_team_power";
    info["details"]["win_probability_is_estimate"] = true;
    info["details"]["selection_policy"] = "max(expected reward after win probability and failure cost)";
    callback(AsstMsg::SubTaskExtraInfo, info);

    const auto& selected_cards = groups[selected_index].cards;
    if (selected_cards.empty()) {
        return false;
    }
    const auto selected_card = std::ranges::max_element(selected_cards, [](const auto& lhs, const auto& rhs) {
        return lhs.rect.y < rhs.rect.y;
    });
    const Rect selected = selected_card->rect;
    if (!ctrler()->click(Point { selected.x + selected.width / 2, selected.y + selected.height / 2 })) {
        return false;
    }
    if (!sleep(500)) {
        return false;
    }
    // 点击敌人卡会同时打开详情面板；点地图中央空白处收起面板，
    // 保留当前敌组选择后再执行拖拽，避免详情层拦截部署手势。
    const cv::Mat post_select_image = ctrler()->get_image();
    if (!post_select_image.empty()) {
        ctrler()->click(Point { post_select_image.cols / 2, static_cast<int>(post_select_image.rows * 0.55) });
        sleep(120);
    }

    // 狭路相逢不是普通的“快捷编队”页：底部干员卡需要拖拽到敌方
    // 卡面上的部署格，单击只会打开详情。这里复用本次动态识别到的
    // 干员卡和已选敌组，不依赖固定列号或固定干员名称。
    auto read_duel_deploy_count = [&]() -> int {
        const auto counter = read_duel_counter(ctrler()->get_image());
        return counter && counter->second == static_cast<int>(duel_limit) ? counter->first : -1;
    };
    auto cancel_duel_deploy_preview = [&]() {
        const cv::Mat current = ctrler()->get_image();
        if (current.empty()) {
            return;
        }
        OCRer cancel_ocr(current);
        cancel_ocr.set_roi({ 0, 0, current.cols, current.rows });
        cancel_ocr.set_use_raw(true);
        if (const auto result = cancel_ocr.analyze()) {
            const auto cancel = std::ranges::find_if(*result, [](const auto& item) {
                return item.text.find("点击取消") != std::string::npos || item.text == "取消";
            });
            if (cancel != result->end()) {
                ctrler()->click(cancel->rect);
            }
        }
    };

    struct DuelOperatorChoice
    {
        Rect card;
        double score = 0.0;
    };

    std::vector<DuelOperatorChoice> choices;
    choices.reserve(operator_details.size() + operator_cards.size());
    for (const auto& detail : operator_details) {
        double score = detail.level > 0 ? detail.level : 0.0;
        score += detail.elite >= 0 ? detail.elite * 100.0 : 0.0;
        score += detail.attack > 0 ? detail.attack * 0.08 : 0.0;
        score += detail.hp > 0 ? detail.hp * 0.01 : 0.0;
        score += detail.defense > 0 ? detail.defense * 0.04 : 0.0;
        choices.push_back({ detail.card_rect, score });
    }
    // OCR 暂时没读到某张卡的详情时仍保留它的动态卡框，但分数低于
    // 已读到属性的卡；这只作为识别不完整时的可逆兜底。
    for (const auto& card : operator_cards) {
        const bool already_added = std::ranges::any_of(choices, [&](const auto& choice) {
            return std::abs(choice.card.x - card.x) <= 2 && std::abs(choice.card.y - card.y) <= 2;
        });
        if (!already_added) {
            choices.push_back({ card, 0.0 });
        }
    }
    std::ranges::stable_sort(choices, [](const auto& lhs, const auto& rhs) { return lhs.score > rhs.score; });
    const size_t operator_count = std::min(duel_limit, choices.size());
    const int initial_deploy_count = read_duel_deploy_count();
    bool duel_deploy_succeeded = initial_deploy_count == 0;
    if (initial_deploy_count < 0) {
        LogError << "BlackFlow duel deployment counter was not recognized";
        return false;
    }
    TilePack::LevelKey duel_key;
    duel_key.name = duel_limit == 1 ? "搏杀" : "共斗";
    m_duel_stage = duel_key.name;
    const auto duel_level = TilePack::find_level(duel_key);
    if (!duel_level) {
        LogError << "BlackFlow duel fixed battlefield geometry is unavailable" << duel_key.name;
        return false;
    }
    const auto duel_geometry = TilePack::calc(*duel_level);
    BattleHelper::clear();
    if (!calc_tiles_info(duel_key.name)) {
        return false;
    }
    for (size_t index = 0; index < operator_count; ++index) {
        const auto& choice = choices[index];
        const Point from { choice.card.x + choice.card.width / 2, choice.card.y + choice.card.height * 3 / 5 };
        // 三组选敌格固定在第 1/4/7 列，共斗每组增加右邻格。
        // 血条和介绍可能遮住左格，拖拽始终使用地图资源的拖动视角投影。
        const Point grid { 1 + static_cast<int>(selected_index) * 3 + static_cast<int>(index), 3 };
        const auto slot = duel_geometry.side_tile_info.find(grid);
        if (slot == duel_geometry.side_tile_info.end()) {
            LogError << "BlackFlow duel fixed deployment slot is unavailable" << grid;
            return false;
        }
        const Point to = slot->second.pos;
        LogInfo << "BlackFlow duel fixed deployment" << "limit" << duel_limit << "group" << selected_index + 1 << "grid"
                << grid << "drag pixel" << to;
        if (!ctrler()->swipe(from, to, 600) || !sleep(350)) {
            cancel_duel_deploy_preview();
            duel_deploy_succeeded = false;
            break;
        }
        // 预览状态下沿右侧拖一次完成朝向确认；没有检测到格子时保留
        // 旧的同点点击兜底，但仍必须由 0/2 -> 1/2 -> 2/2 验证。
        const std::array<asst::Point, 4> direction_ends = {
            asst::Point { std::min(image.cols - 1, to.x + 150), to.y },
            asst::Point { to.x, std::max(0, to.y - 150) },
            asst::Point { std::max(0, to.x - 150), to.y },
            asst::Point { to.x, std::min(image.rows - 1, to.y + 150) },
        };
        int deploy_count = -1;
        for (const auto& direction_end : direction_ends) {
            if (!ctrler()->swipe(to, direction_end, 250) || !sleep(300)) {
                continue;
            }
            deploy_count = read_duel_deploy_count();
            if (deploy_count >= static_cast<int>(index + 1)) {
                break;
            }
        }
        if (deploy_count >= 0 && deploy_count < static_cast<int>(index + 1)) {
            cancel_duel_deploy_preview();
            duel_deploy_succeeded = false;
            break;
        }
        if (deploy_count != static_cast<int>(index + 1)) {
            cancel_duel_deploy_preview();
            duel_deploy_succeeded = false;
            break;
        }
        duel_deploy_succeeded = true;
    }
    if (operator_count != duel_limit || !duel_deploy_succeeded ||
        read_duel_deploy_count() != static_cast<int>(duel_limit)) {
        LogError << "BlackFlow duel operator deployment did not reach observed limit" << duel_limit;
        return false;
    }
    // 正常路径由 DuelOpponentSelection 的 next 转发；这里同时设置收尾占位，
    // 兼容从已打开的对手页恢复执行的情况。
    Task.set_task_base("BlackFlow@Roguelike@StageEncounterResult", std::string(BlackFlowDuelCombatTask));
    return true;
}

bool asst::blackflow::BlackFlowDuelTaskPlugin::run_blackflow_duel_battle()
{
    if (m_duel_stage.empty()) {
        LogError << "BlackFlow duel battle has no observed arena mode";
        return false;
    }
    BattleHelper::clear();
    // 预备区与正式战场共用一张地图。开战后镜头向右移动九格，
    // 预备区选择只限定参战名单，不代表正式战场已经部署。
    if (!calc_tiles_info(m_duel_stage, -9.0, 0.0)) {
        return false;
    }
    if (!wait_until_start(false)) {
        return false;
    }
    if (!update_deployment(true)) {
        return false;
    }
    const auto participants = m_cur_deployment_opers;
    const size_t expected = m_duel_stage == "搏杀" ? 1 : 2;
    size_t deployed = 0;
    for (const auto& oper : participants) {
        if (oper.role == battle::Role::Drone || oper.cooling || oper.name.empty()) {
            continue;
        }
        const Point location { 12 - static_cast<int>(deployed), 3 };
        LogInfo << "BlackFlow duel arena deployment" << oper.name << location;
        if (!deploy_oper(oper.role, oper.name, location, battle::DeployDirection::Right)) {
            LogError << "BlackFlow duel arena deployment was not confirmed" << oper.name;
            return false;
        }
        m_skill_usage[{ oper.role, oper.name }] = battle::SkillUsage::Possibly;
        if (++deployed == expected) {
            break;
        }
    }
    if (deployed != expected) {
        LogError << "BlackFlow duel arena participant count mismatch" << deployed << expected;
        return false;
    }
    speed_up();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(3);
    int absent_frames = 0;
    while (!need_exit() && std::chrono::steady_clock::now() < deadline) {
        const cv::Mat image = ctrler()->get_image();
        if (!check_in_battle(image, false)) {
            if (++absent_frames >= 3) {
                callback(AsstMsg::SubTaskExtraInfo, basic_info_with_what("RoguelikeCombatEnd"));
                return true;
            }
        }
        else {
            absent_frames = 0;
            use_all_ready_skill(image);
        }
        if (!sleep(250)) {
            return false;
        }
    }
    LogError << "BlackFlow duel battle did not reach a settlement within its deadline";
    save_img(ctrler()->get_image(), "duel_battle_timeout");
    return false;
}

