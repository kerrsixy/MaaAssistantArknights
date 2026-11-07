#include "RoguelikeStageEncounterTaskPlugin.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <functional>
#include <numeric>
#include <regex>
#include <tuple>

#include "Config/Roguelike/RoguelikeStageEncounterConfig.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "MaaUtils/ImageIo.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Task/ProcessTask.h"
#include "Task/Roguelike/BlackFlow/BlackFlowSession.h"
#include "Task/Roguelike/Map/RoguelikeBoskyPassageMap.h"
#include "Utils/DebugImageHelper.hpp"
#include "Utils/Logger.hpp"
#include "Vision/BestMatcher.h"
#include "Vision/Matcher.h"
#include "Vision/RegionOCRer.h"

namespace
{
// 黑流事件收尾的占位任务，本插件按实际选择改写它的 baseTask。
constexpr std::string_view BlackFlowResultTask = "BlackFlow@Roguelike@StageEncounterResult";
constexpr std::string_view BlackFlowOptionTask = "BlackFlow@Roguelike@StageEncounterOption-Enter";
constexpr std::string_view BlackFlowDuelCombatTask = "BlackFlow@Roguelike@DuelCombat-Enter";
constexpr std::string_view BlackFlowDuelOpponentSelectionTask = "BlackFlow@Roguelike@DuelOpponentSelection";
constexpr std::string_view BlackFlowRecoveryFailedTask = "BlackFlow@Roguelike@RecoveryFailed";
constexpr std::string_view BlackFlowRewardTask = "BlackFlow@Roguelike@StageEncounterReward";
constexpr std::string_view BlackFlowAbandonTask = "BlackFlow@Roguelike@ExitThenAbandon-Enter";
constexpr std::string_view BlackFlowTerminatedTask = "BlackFlow@Roguelike@StrategyTerminated-Enter";
constexpr std::string_view BlackFlowStagesTask = "BlackFlow@Roguelike@Stages_default";
constexpr std::string_view BlackFlowLeaveConfirmCompletedTask =
    "BlackFlow@Roguelike@StageEncounterLeaveConfirmCompleted";
constexpr std::string_view BlackFlowContinueTask = "BlackFlow@Roguelike@StageEncounterContinue";
constexpr std::string_view BlackFlowContinueMapReadyTask = "BlackFlow@Roguelike@StageEncounterContinueMapReady";
constexpr std::string_view BlackFlowContinueOptionsReadyTask = "BlackFlow@Roguelike@StageEncounterContinueOptionsReady";
constexpr std::string_view BlackFlowTitleOcrTask = "BlackFlow@Roguelike@StageEncounterOcr";
constexpr std::string_view EncounterLoadingCompletedTask = "Roguelike@StageEncounterLoadingCompleted";

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
    cv::inRange(hsv, cv::Scalar(75, 70, 60), cv::Scalar(125, 255, 255), mask);
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
        if (area < width * height * 0.002 || area > width * height * 0.045 ||
            w < width * 0.045 || w > width * 0.25 || h < height * 0.06 || h > height * 0.28 || fill < 0.20) {
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

std::vector<asst::Rect> detect_duel_operator_cards(const cv::Mat& image)
{
    if (image.empty()) {
        return {};
    }

    // 狭路相逢底部是横向重复的干员卡条。边界从当前帧底部投影中寻找，
    // 用最佳周期拟合得到卡片数和矩形，不依赖固定的 12 张或固定 x 坐标。
    const int width = image.cols;
    const int height = image.rows;
    const int top = static_cast<int>(height * 0.80);
    cv::Mat gray, edges;
    cv::cvtColor(image(cv::Rect(0, top, width, height - top)), gray, cv::COLOR_BGR2GRAY);
    cv::Canny(gray, edges, 40, 120);
    cv::Mat projection;
    cv::reduce(edges, projection, 0, cv::REDUCE_SUM, CV_32S);
    std::vector<double> scores(static_cast<size_t>(width));
    double max_score = 0.0;
    for (int x = 0; x < width; ++x) {
        scores[static_cast<size_t>(x)] = projection.at<int>(0, x) / 255.0;
        max_score = std::max(max_score, scores[static_cast<size_t>(x)]);
    }
    if (max_score < 20.0) {
        return {};
    }

    const double threshold = std::max(20.0, max_score * 0.35);
    int best_count = 0;
    double best_ratio = -1.0;
    double best_quality = -1.0;
    for (int count = 8; count <= 16; ++count) {
        const double period = static_cast<double>(width) / count;
        const int radius = std::max(2, static_cast<int>(period * 0.08));
        int hits = 0;
        double quality = 0.0;
        for (int boundary = 1; boundary < count; ++boundary) {
            const int center = static_cast<int>(std::lround(boundary * period));
            const int begin = std::max(0, center - radius);
            const int end = std::min(width - 1, center + radius);
            double local_max = 0.0;
            for (int x = begin; x <= end; ++x) {
                local_max = std::max(local_max, scores[static_cast<size_t>(x)]);
            }
            if (local_max >= threshold) {
                ++hits;
                quality += local_max;
            }
        }
        const int expected = count - 1;
        if (hits * 100 < expected * 80) {
            continue;
        }
        const double ratio = static_cast<double>(hits) / expected;
        if (ratio > best_ratio || (std::abs(ratio - best_ratio) < 1e-9 && quality > best_quality)) {
            best_count = count;
            best_ratio = ratio;
            best_quality = quality;
        }
    }
    if (best_count < 4) {
        return {};
    }

    std::vector<asst::Rect> cards;
    cards.reserve(best_count);
    for (int index = 0; index < best_count; ++index) {
        const int left = static_cast<int>(std::lround(static_cast<double>(index) * width / best_count));
        const int right = static_cast<int>(std::lround(static_cast<double>(index + 1) * width / best_count));
        if (right - left < width * 0.04) {
            continue;
        }
        cards.emplace_back(left, top, right - left, height - top);
    }
    return cards;
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
    const std::string all_text = std::accumulate(
        results.begin(), results.end(), std::string {}, [](std::string text, const auto& result) {
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

bool asst::RoguelikeStageEncounterTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    // 安全屋，掷骰子之类的带选项的也都是视为不期而遇了
    if (msg != AsstMsg::SubTaskStart || details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }

    if (!RoguelikeConfig::is_valid_theme(m_config->get_theme())) {
        Log.error("Roguelike name doesn't exist!");
        return false;
    }
    const std::string roguelike_name = m_config->get_theme() + "@";
    const std::string& task = details.get("details", "task", "");
    if (task == BlackFlowDuelOpponentSelectionTask) {
        m_duel_opponent_selection_pending = true;
        m_duel_refresh_attempted = false;
        return true;
    }
    std::string_view task_view = task;
    if (task_view.starts_with(roguelike_name)) {
        task_view.remove_prefix(roguelike_name.length());
    }
    if (task_view == "Roguelike@StageEncounterJudgeOption") {
        return true;
    }
    else {
        return false;
    }
}

bool asst::RoguelikeStageEncounterTaskPlugin::_run()
{
    LogTraceFunction;

    if (m_duel_opponent_selection_pending) {
        m_duel_opponent_selection_pending = false;
        return run_blackflow_duel_opponent_selection();
    }

    const std::string& theme = m_config->get_theme();
    std::vector<std::string> event_names = RoguelikeStageEncounter.get_event_names(theme);

    if (theme == RoguelikeTheme::BlackFlow) {
        // 标题识别失败时不会进入 handle_single_event，这里先复位收尾占位，避免沿用上一次事件的去向。
        set_blackflow_result(BlackFlowRecoveryFailedTask);
    }

    const bool duel_node = m_blackflow_session != nullptr && m_blackflow_session->page_context().has_value() &&
                           m_blackflow_session->page_context()->node_type == blackflow::NodeType::Duel;

    // 狭路相逢先显示事件选项页；选完第一项后，资源链会再次进入独立的对手选择任务。
    // 这里保留节点语义兜底，但不把对手选择页误当成普通事件选项页。
    if (duel_node) {
        const cv::Mat current_image = ctrler()->get_image();
        if (cluster_duel_groups(detect_duel_cards(current_image), current_image.cols).size() == 3) {
            return run_blackflow_duel_opponent_selection();
        }
        reset_option_list_and_view_data();
        if (update_option_list()) {
            handle_single_event("狭路相逢");
            return true;
        }
        LogInfo << "BlackFlow duel event options are not visible; waiting for the dedicated opponent-selection task";
        return false;
    }

    const std::string themed_ocr_task = theme + "@Roguelike@StageEncounterOcr";
    const auto event_name_task_ptr =
        Task.get(themed_ocr_task) != nullptr ? Task.get(themed_ocr_task) : Task.get("Roguelike@StageEncounterOcr");
    sleep(event_name_task_ptr->pre_delay);

    if (need_exit()) {
        return false;
    }

    cv::Mat image = ctrler()->get_image();
    OCRer name_analyzer(image);
    name_analyzer.set_task_info(event_name_task_ptr);
    name_analyzer.set_required(event_names);

    if (!name_analyzer.analyze()) {
        Log.error("Unknown Event");
        callback(AsstMsg::SubTaskExtraInfo, basic_info_with_what("EncounterOcrError"));
        return theme != RoguelikeTheme::Phantom;
    }

    const auto& result_vec = name_analyzer.get_result();
    if (result_vec.empty()) {
        Log.error("Unknown Event");
        return true;
    }

    std::string current_event_name = result_vec.front().text;
    m_reported_event_name.clear();

    // 处理主事件及其链式 next_event
    while (!current_event_name.empty() && !need_exit()) {
        auto next = handle_single_event(current_event_name);
        if (!next) {
            break;
        }
        current_event_name = next.value();
    }

    return true;
}

std::optional<std::string> asst::RoguelikeStageEncounterTaskPlugin::handle_single_event(const std::string& event_name)
{
    const std::string& theme = m_config->get_theme();
    const RoguelikeMode& mode = m_config->get_mode();
    if (theme == RoguelikeTheme::BlackFlow) {
        set_blackflow_result(BlackFlowRecoveryFailedTask);
    }
    const auto& event_map = RoguelikeStageEncounter.get_events(theme, mode);

    auto it = event_map.find(event_name);
    if (it == event_map.end()) {
        Log.error("Unknown event:", event_name);
        return std::nullopt;
    }

    const auto& event = it->second;

    cv::Mat image = ctrler()->get_image();

    int special_val = 0;
    // 萨米：抗干扰指数；水月：灯火值。两者 ROI 不同，各自主题覆盖 @SpecialValRecognition。
    if (theme == RoguelikeTheme::Sami || theme == RoguelikeTheme::Mizuki) {
        OCRer analyzer(image);
        analyzer.set_task_info(theme + "@Roguelike@SpecialValRecognition");
        analyzer.set_replace(Task.get<OcrTaskInfo>("NumberOcrReplace")->replace_map);
        analyzer.set_use_char_model(true);
        if (!analyzer.analyze()) {
            // return std::nullopt;
            Log.error("Failed to recognize special value for event:", event.name);
        }
        else {
            utils::chars_to_number(analyzer.get_result().front().text, special_val);
            if (theme == RoguelikeTheme::Sami) {
                m_config->status().chaos = special_val;
            }
            else if (theme == RoguelikeTheme::Mizuki) {
                m_config->status().light = special_val;
            }
        }
    }

    size_t choose_option = process_task(event, special_val);
    Log.info("Event:", event.name, "special_val", special_val, "choose option", choose_option);

    // 连续单选项事件会按页重复进入本函数，同一事件只向界面报告一次。
    if (m_reported_event_name != event.name) {
        m_reported_event_name = event.name;
        auto info = basic_info_with_what("RoguelikeEvent");
        info["details"]["name"] = event.name;
        info["details"]["default_choose"] = event.default_choose;
        info["details"]["choose_option"] = choose_option;
        callback(AsstMsg::SubTaskExtraInfo, info);
    }

    // 萨卡兹内容拓展 II，#11861
    if (event.name.starts_with("魂灵见闻：")) {
        Matcher matcher(image);
        matcher.set_task_info("Sarkaz@Roguelike@CloseCollectionClose");
        if (matcher.analyze()) {
            Log.trace("Found extra 'Plans', click CloseCollectionClose and StageEncounterJudgeClick");
            ctrler()->click(matcher.get_result().rect);
            ProcessTask(*this, { "Roguelike@StageEncounterJudgeClick" }).run();
            ProcessTask(*this, { "Roguelike@StageEncounterJudgeClick2" }).run();
        }
    }

    // 界园树洞
    if (m_config->get_theme() == RoguelikeTheme::JieGarden) {
        auto& bosky_map = RoguelikeBoskyPassageMap::get_instance();
        if (event.name == "掷地有声") {
            bosky_map.set_node_subtype(bosky_map.get_curr_pos(), RoguelikeBoskySubNodeType::Ling);
        }
        else if (event.name == "种因得果") {
            bosky_map.set_node_subtype(bosky_map.get_curr_pos(), RoguelikeBoskySubNodeType::Shu);
        }
        else if (event.name == "三缺一") {
            bosky_map.set_node_subtype(bosky_map.get_curr_pos(), RoguelikeBoskySubNodeType::Nian);
        }

        if (bosky_map.get_target_subtype() != RoguelikeBoskySubNodeType::Unknown) {
            if (bosky_map.get_node_subtype(bosky_map.get_curr_pos()) == bosky_map.get_target_subtype()) {
                Log.info(__FUNCTION__, "| Found target playtime node, completing task and exiting");

                auto target_info = basic_info_with_what("RoguelikeJieGardenTargetFound");
                target_info["details"]["target_subtype"] = subtype2name(bosky_map.get_target_subtype());
                callback(AsstMsg::SubTaskExtraInfo, target_info);

                // 完成任务，退出
                m_control_ptr->exit_then_stop();
                m_task_ptr->set_enable(false);
                return std::nullopt;
            }
        }
    }

    if (theme == RoguelikeTheme::BlackFlow) {
        return select_blackflow_option(event, choose_option);
    }

    // 界园只操作本次识别的选项，失败后结束当前事件处理。
    if (theme == RoguelikeTheme::JieGarden) {
        reset_option_list_and_view_data();
        if (update_option_list()) {
            if (const auto selected = select_event_option(plan_from_event(event, choose_option), event.name)) {
                report_selected_option(event, *selected, nullptr);
                return next_event(event);
            }
        }
        return std::nullopt;
    }

    const auto click_option_task_name = [&](size_t item, size_t total) {
        if (item > total) {
            Log.warn("Event:", event.name, "Total:", total, "Choice", item, "out of range, switch to choice", total);
            item = total;
        }
        return m_config->get_theme() + "@Roguelike@OptionChoose" + std::to_string(total) + "-" + std::to_string(item);
    };

    for (int j = 0; j < 2; ++j) {
        ProcessTask(*this, { click_option_task_name(choose_option, event.option_num) }).run();
        sleep(300);
    }

    sleep(1500);

    // 判断是否点击成功，成功进入对话后左上角的生命值会消失
    image = ctrler()->get_image();
    bool hp_disappeared = (hp(image) < 0);
    // fallback 可变选项，临时处理，之后还得改成更通用的方式
    if (!hp_disappeared) {
        for (const auto& [total, item] : event.fallback_choices) {
            Log.info("Trying fallback choice", total, "-", item);
            for (int j = 0; j < 2; ++j) {
                ProcessTask(*this, { click_option_task_name(item, total) }).run();
                sleep(300);
            }
            sleep(500);
            image = ctrler()->get_image();
            if (hp(image) < 0) {
                Log.info("Fallback choice success");
                hp_disappeared = true;
                break;
            }
        }
    }

    if (hp_disappeared) {
        return next_event(event);
    }

    // 兜底处理，从 option_num-option_num 点到 1-1
    size_t max_time = event.option_num;
    while (max_time > 0) {
        for (size_t i = max_time; i > 0; --i) {
            // This is an optional tutorial overlay probe. A miss is normal and
            // must not be surfaced as a ProcessTask/SubTaskError.
            Matcher tutorial_button(image);
            tutorial_button.set_task_info("Roguelike@TutorialButton");
            if (tutorial_button.analyze()) {
                return std::nullopt;
            }

            for (int j = 0; j < 2; ++j) {
                ProcessTask(*this, { click_option_task_name(i, max_time) }).run();
                sleep(300);
            }

            if (need_exit()) {
                return std::nullopt;
            }

            sleep(500);
            image = ctrler()->get_image();
            if (hp(image) < 0) {
                return std::nullopt;
            }
        }
        --max_time;
    }

    return event.next_event.empty() ? std::nullopt : std::optional { event.next_event };
}

bool asst::RoguelikeStageEncounterTaskPlugin::run_blackflow_duel_opponent_selection()
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

    // 狭路相逢准备页底部的干员卡本身不显示名称，但点击后会在左侧打开
    // 详情面板。先从当前帧动态分割卡条，逐张点击并用 OCR 读取 LV、生命、
    // 攻击、防御、法抗、阻挡和技能文本；点回同一卡关闭详情后再处理下一张。
    // 这一步只停留在准备页，不会确认敌人或刷新敌人。
    std::vector<DuelOperatorDetail> operator_details;
    const auto operator_cards = detect_duel_operator_cards(image);
    for (const auto& card : operator_cards) {
        if (need_exit()) {
            break;
        }
        const Point click_point { card.x + card.width / 2,
                                  std::clamp(card.y + card.height * 3 / 5, 0, image.rows - 1) };
        if (!ctrler()->click(click_point) || !sleep(300)) {
            continue;
        }
        const cv::Mat detail_image = ctrler()->get_image();
        OCRer detail_ocr(detail_image);
        detail_ocr.set_roi({ 0, 0, detail_image.cols, detail_image.rows });
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
                    result.text.find("开始战斗") == std::string::npos && result.text.find("刷新敌人") == std::string::npos) {
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
            // 详情图的等级行很窄，单独 OCR 一次能把完整屏幕中被立绘/按钮
            // 干扰的 “Lv90” 恢复出来；ROI 由当前图像尺寸推导。
            OCRer level_ocr(detail_image);
            level_ocr.set_roi({ 0,
                                static_cast<int>(detail_image.rows * 0.25),
                                static_cast<int>(detail_image.cols * 0.22),
                                static_cast<int>(detail_image.rows * 0.16) });
            level_ocr.set_use_raw(true);
            level_ocr.set_use_char_model(true);
            std::optional<asst::OCRer::ResultsVec> level_result = level_ocr.analyze();
            detail.level = parse_duel_operator_level(*detail_result, detail_image.rows).value_or(-1);
            if (level_result && !level_result->empty()) {
                const auto level_from_roi = parse_duel_operator_level(*level_result, detail_image.rows);
                if (level_from_roi) {
                    detail.level = *level_from_roi;
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
        sleep(260);
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
    const size_t deployable =
        std::min<size_t>(operator_power.size(), std::max(1, m_config->status().formation_upper_limit));
    double team_power = std::accumulate(operator_power.begin(), operator_power.begin() + deployable, 0.0);
    team_power *= std::clamp(m_config->status().formation_upper_limit / 6.0, 0.5, 1.0);

    json::array candidate_report;
    json::array operator_report;
    for (const auto& detail : operator_details) {
        operator_report.emplace_back(json::object {
            { "card_rect", json::object {
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
            card_rects.emplace_back(json::object {
                { "x", card_candidate.rect.x },
                { "y", card_candidate.rect.y },
                { "width", card_candidate.rect.width },
                { "height", card_candidate.rect.height },
            });
        }
        if (group.cards.empty()) {
            group.confidence = 0.0;
            group.evidence = "no card";
            candidate_report.emplace_back(json::object {
                { "group", index + 1 },
                { "card_count", 0 },
                { "card_rects", std::move(card_rects) },
                { "threat", 0.0 },
                { "confidence", 0.0 },
                { "win_probability", 0.0 },
                { "reward_value", index == 0 ? 24.0 : (index == 1 ? 40.0 : 58.0) },
                { "reward_kind", index == 0 ? "part_low_mid" : (index == 1 ? "part_high_or_movement" : "collectible") },
                { "utility", -80.0 },
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
        Point opened_point;
        Rect opened_card;
        std::vector<Point> detail_click_points;
        auto ordered_cards = group.cards;
        std::ranges::sort(ordered_cards, [](const auto& lhs, const auto& rhs) {
            return lhs.rect.x > rhs.rect.x;
        });
        for (const auto& card_candidate : ordered_cards) {
            const Rect card = card_candidate.rect;
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
            const std::vector<std::pair<int, int>> x_ratios = far_right_card
                                                                   ? std::vector<std::pair<int, int>> { { 3, 7 }, { 2, 5 }, { 31, 40 }, { 3, 4 }, { 1, 4 }, { 1, 3 }, { 3, 8 }, { 1, 2 }, { 5, 8 }, { 7, 8 } }
                                                                   : std::vector<std::pair<int, int>> { { 9, 16 }, { 1, 2 }, { 5, 8 }, { 4, 7 } };
            const std::vector<std::pair<int, int>> y_ratios = far_right_card
                                                                   ? std::vector<std::pair<int, int>> { { -13, 20 }, { -17, 25 }, { -7, 10 }, { -1, 1 }, { -9, 10 }, { -4, 5 }, { -3, 4 } }
                                                                   : std::vector<std::pair<int, int>> { { -17, 25 }, { -13, 20 }, { -7, 10 }, { -73, 100 }, { -77, 100 }, { -3, 4 }, { -4, 5 } };
            for (const auto [x_numerator, x_denominator] : x_ratios) {
                for (const auto [y_numerator, y_denominator] : y_ratios) {
                    detail_click_points.emplace_back(
                        card.x + card.width * x_numerator / x_denominator,
                        std::clamp(card.y + card.height * y_numerator / y_denominator, 0, image.rows - 1));
                }
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
                const bool tag = result.text.find("全部") != std::string::npos ||
                                 result.text.find("精英") != std::string::npos ||
                                 result.text.find("领袖") != std::string::npos ||
                                 result.text.find("普通") != std::string::npos;
                return tag && result.rect.x < detail_image.cols / 2 && result.rect.y > detail_image.rows / 5;
            }));
            const auto is_stat_label = [](const auto& result) {
                return result.text.find("生命") != std::string::npos ||
                       result.text.find("攻击") != std::string::npos || result.text.find("防御") != std::string::npos ||
                       result.text.find("法抗") != std::string::npos || result.text.find("抗性") != std::string::npos ||
                       result.text.find("重量") != std::string::npos || result.text.find("阻挡") != std::string::npos ||
                       result.text.find("特性") != std::string::npos || result.text.find("天赋") != std::string::npos;
            };
            const int stat_label_count = static_cast<int>(std::ranges::count_if(*probe_result, [&](const auto& result) {
                return result.rect.x < detail_image.cols / 2 && result.rect.y > detail_image.rows / 5 &&
                       is_stat_label(result);
            }));
            // 预备页右侧只有刷新/开始按钮；点击敌人后会在同一相对区域
            // 出现敌名和至少一行描述。用 OCR 文本的空间聚类确认提示框，
            // 不依赖敌人名称或固定坐标。
            const int tooltip_text_count = static_cast<int>(std::ranges::count_if(*probe_result, [&](const auto& result) {
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
                opened_point = click_point;
                // 记录当前点击点所属卡面，关闭时可在同一卡面的立绘相对区域重试。
                const auto opened_card_it = std::ranges::find_if(group.cards, [&](const auto& candidate) {
                    return click_point.x >= candidate.rect.x && click_point.x <= candidate.rect.x + candidate.rect.width;
                });
                if (opened_card_it != group.cards.end()) {
                    opened_card = opened_card_it->rect;
                }
                break;
            }
        }
        if (!detail_opened) {
            group.evidence = "detail_open_failed";
            group.confidence = 0.0;
            candidate_report.emplace_back(json::object {
                { "group", index + 1 },
                { "card_count", group.cards.size() },
                { "card_rects", std::move(card_rects) },
                { "threat", group.threat },
                { "confidence", group.confidence },
                { "win_probability", 0.35 },
                { "reward_value", index == 0 ? 24.0 : (index == 1 ? 40.0 : 58.0) },
                { "reward_kind", index == 0 ? "part_low_mid" : (index == 1 ? "part_high_or_movement" : "collectible") },
                { "utility", 0.35 * (index == 0 ? 24.0 : (index == 1 ? 40.0 : 58.0)) - 0.65 * 80.0 },
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
                       (result.text.find("生命") != std::string::npos || result.text.find("攻击") != std::string::npos ||
                        result.text.find("防御") != std::string::npos || result.text.find("法抗") != std::string::npos);
            });
        const bool tooltip_evidence = detail_result &&
                                      std::ranges::count_if(*detail_result, [&](const auto& result) {
                                          const bool in_tooltip = result.rect.x > detail_image.cols * 0.72 &&
                                                                  result.rect.y > detail_image.rows * 0.12 &&
                                                                  result.rect.y < detail_image.rows * 0.35;
                                          const bool not_page_control = result.text.find("刷新敌人") == std::string::npos &&
                                                                         result.text.find("开始战斗") == std::string::npos &&
                                                                         result.text.find("选择干员") == std::string::npos;
                                          return in_tooltip && not_page_control && result.text.size() >= 2;
                                      }) >= 2;
        const bool detail_page = detail_result && std::ranges::any_of(*detail_result, [](const auto& result) {
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
        // 本版本详情面板点同一名敌人即可关闭。点击后再次 OCR 左侧属性栏，
        // 若仍可见再发送返回键，防止详情覆盖层残留导致下一组重复读取同一敌人。
        const auto detail_overlay_visible = [&]() {
            const cv::Mat current = ctrler()->get_image();
            OCRer checker(current);
            checker.set_roi({ 0, 0, current.cols, current.rows });
            checker.set_use_raw(true);
            const auto checked = checker.analyze();
            if (!checked) {
                return false;
            }
            const int label_count = static_cast<int>(std::ranges::count_if(*checked, [&](const auto& result) {
                const bool label = result.text.find("生命") != std::string::npos ||
                                   result.text.find("攻击") != std::string::npos ||
                                   result.text.find("防御") != std::string::npos ||
                                   result.text.find("法抗") != std::string::npos ||
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
                return in_tooltip && not_page_control && result.text.size() >= 2;
            }));
            return label_count >= 3 || tooltip_count >= 2;
        };
        // 详情提示框点击同一敌人不会总是关闭；本 UI 需要再次点击
        // 立绘上沿（约卡框上方一倍高度）。候选点仍从当前卡框的
        // 相对比例推导，并优先覆盖打开该详情的卡。
        std::vector<Point> close_points;
        // 优先在当前详情 OCR 框和卡框之外寻找动态空白点。点击空白
        // 是本页面稳定的关闭方式，且不会依赖某个敌人的固定立绘位置。
        const auto is_safe_background = [&](const Point& point) {
            for (const auto& candidate : group.cards) {
                const Rect& rect = candidate.rect;
                if (point.x >= rect.x - 8 && point.x <= rect.x + rect.width + 8 && point.y >= rect.y - 8 &&
                    point.y <= rect.y + rect.height + 8) {
                    return false;
                }
            }
            if (detail_result) {
                for (const auto& result : *detail_result) {
                    const Rect& rect = result.rect;
                    if (point.x >= rect.x - 8 && point.x <= rect.x + rect.width + 8 && point.y >= rect.y - 8 &&
                        point.y <= rect.y + rect.height + 8) {
                        return false;
                    }
                }
            }
            return true;
        };
        for (const auto [x_numerator, x_denominator] :
             std::vector<std::pair<int, int>> { { 3, 8 }, { 1, 10 }, { 1, 2 }, { 7, 10 }, { 9, 10 } }) {
            for (const auto [y_numerator, y_denominator] :
                 std::vector<std::pair<int, int>> { { 1, 4 }, { 1, 5 }, { 3, 10 }, { 2, 5 }, { 1, 2 } }) {
                const Point background_point { image.cols * x_numerator / x_denominator,
                                               image.rows * y_numerator / y_denominator };
                if (is_safe_background(background_point)) {
                    close_points.emplace_back(background_point);
                }
            }
        }
        close_points.emplace_back(opened_point);
        if (opened_card.width > 0 && opened_card.height > 0) {
            const bool far_right_card = opened_card.x > image.cols * 0.65;
            const std::vector<std::pair<int, int>> x_ratios = far_right_card
                                                                   ? std::vector<std::pair<int, int>> { { 3, 4 }, { 7, 8 }, { 5, 8 }, { 1, 2 } }
                                                                   : std::vector<std::pair<int, int>> { { 9, 16 }, { 1, 2 }, { 5, 8 }, { 4, 7 } };
            const std::vector<std::pair<int, int>> y_ratios = far_right_card
                                                                   ? std::vector<std::pair<int, int>> { { -14, 10 }, { -13, 10 }, { -12, 10 }, { -11, 10 }, { -1, 1 } }
                                                                   : std::vector<std::pair<int, int>> { { -14, 10 }, { -13, 10 }, { -12, 10 }, { -11, 10 }, { -1, 1 } };
            for (const auto [x_numerator, x_denominator] : x_ratios) {
                for (const auto [y_numerator, y_denominator] : y_ratios) {
                    close_points.emplace_back(
                        opened_card.x + opened_card.width * x_numerator / x_denominator,
                        std::clamp(opened_card.y + opened_card.height * y_numerator / y_denominator, 0, image.rows - 1));
                }
            }
        }
        closed = false;
        for (const auto& close_point : close_points) {
            if (!ctrler()->click(close_point)) {
                continue;
            }
            closed = true;
            if (!sleep(300) || !detail_overlay_visible()) {
                break;
            }
            closed = false;
        }
        if (closed && detail_overlay_visible()) {
            closed = false;
        }
        // 不使用返回键兜底：Android 返回键会暂停当前战斗预备页，
        // 反而把后续敌组识别带到暂停层。
        if (!closed) {
            group.evidence += ";detail_close_failed";
        }

        // 三列奖励类别是游戏规则：左=低/中零件，中=中/高零件或代步，右=收藏品；
        // 这里仅量化当前需求，敌组本身仍完全由当前帧 OCR/视觉结果决定。
        double reward = index == 0 ? 24.0 : (index == 1 ? 40.0 : 58.0);
        const std::string reward_kind =
            index == 0 ? "part_low_mid" : (index == 1 ? "part_high_or_movement" : "collectible");
        if (index == 1 && m_blackflow_session != nullptr) {
            const int free_movement_slots = blackflow::movement_slots_remaining(m_blackflow_session->run().resources);
            if (free_movement_slots <= 0) {
                reward *= 0.65;
            }
        }
        const double win_probability =
            group.confidence < 0.25 ? 0.35 : 1.0 / (1.0 + std::exp((group.threat - team_power) / 70.0));
        const double failure_cost = 80.0;
        const double utility = win_probability * reward - (1.0 - win_probability) * failure_cost;
        group.win_probability = win_probability;
        group.reward = reward;
        group.utility = utility;
        group.viable = group.confidence >= 0.45 && win_probability >= 0.60;

        json::object item {
            { "group", index + 1 },
            { "card_count", group.cards.size() },
            { "card_rects", std::move(card_rects) },
            { "threat", group.threat },
            { "confidence", group.confidence },
            { "win_probability", win_probability },
            { "reward_value", reward },
            { "reward_kind", reward_kind },
            { "utility", utility },
            { "viable", group.viable },
            { "evidence", group.evidence },
            { "detail_ocr", group.detail_text },
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
    if (!any_viable && team_power_available && any_detail_evidence && !m_duel_refresh_attempted) {
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

    const auto best = std::ranges::max_element(groups, [](const auto& lhs, const auto& rhs) {
        if (lhs.viable != rhs.viable) {
            return !lhs.viable;
        }
        if (lhs.utility != rhs.utility) {
            return lhs.utility < rhs.utility;
        }
        return lhs.win_probability < rhs.win_probability;
    });
    if (best == groups.end() || need_exit()) {
        return false;
    }
    if (!best->viable && best->win_probability < 0.50) {
        LogError << "BlackFlow duel has no opponent with a reliable win estimate";
        auto unsafe = basic_info_with_what("DuelEnemyCandidates");
        unsafe["details"]["groups"] = std::move(candidate_report);
        if (team_power_available) {
            unsafe["details"]["team_power"] = team_power;
        }
        unsafe["details"]["team_power_available"] = team_power_available;
        unsafe["details"]["operators"] = operator_report;
        unsafe["details"]["operator_count"] = operator_details.size();
        unsafe["details"]["team_power_source"] = detail_power_available
                                                          ? "duel_operator_detail_ocr"
                                                          : (team_power_available ? "roguelike_status.opers" : "unavailable");
        unsafe["details"]["win_probability_source"] =
            team_power_available ? "team_power_vs_ocr_threat" : "prior_without_team_power";
        unsafe["details"]["win_probability_is_estimate"] = !team_power_available;
        unsafe["details"]["status"] = !team_power_available ? "team_power_unavailable"
                                                                : (any_detail_evidence ? "no_safe_opponent"
                                                                                        : "detail_unavailable");
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
    if (team_power_available) {
        info["details"]["team_power"] = team_power;
    }
    info["details"]["team_power_available"] = team_power_available;
    info["details"]["operators"] = operator_report;
    info["details"]["operator_count"] = operator_details.size();
    info["details"]["team_power_source"] = detail_power_available
                                                  ? "duel_operator_detail_ocr"
                                                  : (team_power_available ? "roguelike_status.opers" : "unavailable");
    info["details"]["win_probability_source"] =
        team_power_available ? "team_power_vs_ocr_threat" : "prior_without_team_power";
    info["details"]["win_probability_is_estimate"] = !team_power_available;
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
    // 正常路径由 DuelOpponentSelection 的 next 转发；这里同时设置收尾占位，
    // 兼容从已打开的对手页恢复执行的情况。
    set_blackflow_result(BlackFlowDuelCombatTask);
    return true;
}

bool asst::RoguelikeStageEncounterTaskPlugin::satisfies_condition(
    const Config::ChoiceRequire& requirement,
    const int special_val)
{
    int value = 0;
    bool ret = utils::chars_to_number(requirement.vision.value, value);
    Log.trace("special_val: ", special_val, "value: ", value);
    switch (requirement.vision.type) {
    case Config::ComparisonType::GreaterThan:
        ret &= special_val > value;
        Log.trace("special_val > value: ", special_val > value ? "true" : "false");
        break;
    case Config::ComparisonType::LessThan:
        ret &= special_val < value;
        Log.trace("special_val < value: ", special_val < value ? "true" : "false");
        break;
    case Config::ComparisonType::Equal:
        ret &= special_val == value;
        Log.trace("special_val == value: ", special_val == value ? "true" : "false");
        break;
    case Config::ComparisonType::None:
        Log.warn("no vision type");
        break;
    case Config::ComparisonType::Unsupported:
        Log.warn("unsupported vision type");
        return false;
    }
    /*
    switch (requirement.hp.type) {
        // ...
    }
    */
    if (!ret) {
        return false;
    }
    return true;
}

size_t asst::RoguelikeStageEncounterTaskPlugin::process_task(const Config::RoguelikeEvent& event, const int special_val)
{
    for (const auto& requirement : event.choice_require) {
        if (requirement.choose == -1) {
            continue;
        }
        if (satisfies_condition(requirement, special_val)) {
            return requirement.choose;
        }
    }
    return event.default_choose;
}

int asst::RoguelikeStageEncounterTaskPlugin::hp(const cv::Mat& image) const
{
    LogTraceFunction;

    if (image.empty()) {
        LogTrace << __FUNCTION__ << "Empty image; HP observation unavailable";
        return -1;
    }
    // HP is optional event-page evidence. A miss must never spawn a ProcessTask
    // or abort a valid event transition with SubTaskError.
    Matcher hp_flag(image);
    hp_flag.set_task_info("Roguelike@HpFlag");
    if (!hp_flag.analyze()) {
        LogTrace << __FUNCTION__ << "HP flag not found; observation unavailable";
        return -1;
    }

    auto task = Task.get<OcrTaskInfo>("Roguelike@HpRecognition");
    std::vector<std::pair<std::string, std::string>> merged_map;
    merged_map.insert(merged_map.end(), task->replace_map.begin(), task->replace_map.end());
    merged_map.emplace_back("(.*)/.*", "$1");

    auto roi_image = make_roi(image, task->roi).clone();
    cv::Mat r_channel;
    cv::extractChannel(roi_image, r_channel, 2);
    cv::Mat mask;
    cv::threshold(r_channel, mask, 50, 255, cv::THRESH_BINARY);
    cv::Mat inv_mask;
    cv::bitwise_not(mask, inv_mask);
    roi_image.setTo(cv::Scalar(0, 0, 0), mask);

    RegionOCRer analyzer(roi_image);
    analyzer.set_replace(merged_map);
    analyzer.set_use_char_model(true);
    analyzer.set_bin_threshold(60); // 血量没有红色通道，虽然它看着很明显，但实际上在灰度中只有 2/3

    auto res_vec_opt = analyzer.analyze();
    if (!res_vec_opt) {
        return 0;
    }

    int hp_val;
    return utils::chars_to_number(res_vec_opt->text, hp_val) ? hp_val : 0;
}

bool asst::RoguelikeStageEncounterTaskPlugin::update_option_list()
{
    LogTraceFunction;

    const std::string& theme = m_config->get_theme();

    // 不期而遇默认位置会在选项列表中央, 为了从上到下检视选项列表, 需要先向上滑动
    if (!move_to_option_list_head()) {
        return false;
    }

    cv::Mat image = ctrler()->get_image();
    RoguelikeEncounterOptionAnalyzer analyzer(image);
    analyzer.set_theme(theme);
    for (size_t swipe_times = 0; swipe_times < MAX_SWIPE_TIMES && !need_exit(); ++swipe_times) {
        if (!move_forward()) {
            return false;
        }
        image = ctrler()->get_image();
        const std::optional<int> ret = analyzer.merge_image(image);
        if (!ret) {
            return false;
        }
        if (ret.value() <= 0) {
            break;
        }
    }

    if (need_exit() || !analyzer.analyze()) {
        return false;
    }

    m_option_list = analyzer.get_result();
    report_analyzed_options();

    return update_view(image);
}

asst::RoguelikeStageEncounterTaskPlugin::SelectionPlan
    asst::RoguelikeStageEncounterTaskPlugin::plan_from_event(const Config::RoguelikeEvent& event, size_t choose_option)
{
    return SelectionPlan {
        .option_text = event.option_text,
        .option_num = event.option_num,
        .choose = choose_option,
        .fallback_choices = event.fallback_choices,
        .allow_fallback = true,
        .continue_single_option = event.continue_single_option,
    };
}

asst::RoguelikeStageEncounterTaskPlugin::SelectionPlan
    asst::RoguelikeStageEncounterTaskPlugin::plan_from_rule(const blackflow::EncounterRule& rule)
{
    // 规则命中后只按规则选择，事件默认配置的编号与兜底表不再参与。
    return SelectionPlan {
        .option_text = rule.option_text,
        .option_num = rule.option_num,
        .choose = rule.choose,
        .allow_fallback = rule.allow_fallback,
    };
}

void asst::RoguelikeStageEncounterTaskPlugin::set_blackflow_result(std::string_view base_task)
{
    Task.set_task_base(std::string(BlackFlowResultTask), std::string(base_task));
}

void asst::RoguelikeStageEncounterTaskPlugin::report_selected_option(
    const Config::RoguelikeEvent& event,
    const SelectedOption& selected,
    const blackflow::EncounterRule* rule)
{
    auto info = basic_info_with_what("RoguelikeEventSelected");
    info["details"] = json::object {
        { "name", event.name },
        { "option_num", static_cast<std::int64_t>(m_option_list.size()) },
        { "choose_option", static_cast<std::int64_t>(selected.index + 1) },
        { "option_text", m_option_list[selected.index].text },
        { "rule_id", rule != nullptr ? rule->id : std::string() },
        { "rule_description", rule != nullptr ? rule->description : std::string() },
        { "used_fallback", selected.used_fallback },
    };
    callback(AsstMsg::SubTaskExtraInfo, info);
}

std::optional<std::string> asst::RoguelikeStageEncounterTaskPlugin::select_blackflow_option(
    const Config::RoguelikeEvent& event,
    size_t choose_option)
{
    reset_option_list_and_view_data();
    if (!update_option_list()) {
        LogError << "BlackFlow encounter option analysis failed";
        return std::nullopt;
    }
    for (const auto& [text, task] : event.option_tasks) {
        if (Task.get(task) == nullptr) {
            LogError << "Encounter result task is unavailable" << event.name << text << task;
            return std::nullopt;
        }
    }
    if (m_blackflow_session == nullptr) {
        LogError << "BlackFlow encounter session is unavailable";
        return std::nullopt;
    }

    std::vector<std::string> available_options;
    for (const auto& option : m_option_list) {
        if (option.enabled) {
            available_options.emplace_back(option.text);
        }
    }
    std::string error;
    const auto context =
        m_blackflow_session->prepare_encounter(event.name, m_option_list.size(), available_options, &error);
    if (!context.has_value()) {
        LogError << "BlackFlow encounter decision failed" << error;
        return std::nullopt;
    }
    const blackflow::EncounterRule* rule = context->rule.has_value() ? &*context->rule : nullptr;
    SelectionPlan plan = rule != nullptr ? plan_from_rule(*rule) : plan_from_event(event, choose_option);
    plan.continue_single_option = event.continue_single_option;

    // The BlackFlow "Face to Face" node is an opponent-selection page. Its
    // first enabled entry is the stable, user-requested choice for every
    // duel stage; do not reuse an incident rule or the event's default index.
    const auto page_context = m_blackflow_session->page_context();
    const bool duel_node =
        event.name == "狭路相逢" || (page_context.has_value() && page_context->node_type == blackflow::NodeType::Duel);
    if (duel_node) {
        const auto first_enabled =
            std::ranges::find_if(m_option_list, [](const OptionAnalyzer::Option& option) { return option.enabled; });
        if (first_enabled == m_option_list.end()) {
            LogError << "BlackFlow duel has no enabled opponent option";
            set_blackflow_result(BlackFlowAbandonTask);
            return std::nullopt;
        }
        plan.option_text.clear();
        plan.option_num = m_option_list.size();
        plan.choose = static_cast<size_t>(std::distance(m_option_list.begin(), first_enabled)) + 1;
        plan.fallback_choices.clear();
        plan.allow_fallback = false;
        plan.continue_single_option = false;
        LogInfo << "BlackFlow duel selects first enabled opponent option" << plan.choose;
    }

    const auto selected = select_event_option(plan, event.name);
    if (!selected.has_value()) {
        set_blackflow_result(BlackFlowAbandonTask);
        return std::nullopt;
    }
    // 保留黑流确认完成后的页面稳定等待，再记录策略结果。
    if (!sleep(Task.get(std::string(BlackFlowLeaveConfirmCompletedTask))->post_delay)) {
        return std::nullopt;
    }
    const std::string& selected_text = m_option_list[selected->index].text;
    blackflow::EncounterSelection selection {
        .event_name = event.name,
        .option_num = m_option_list.size(),
        .choose = selected->index + 1,
        .option_text = selected_text,
        .rule_id = rule != nullptr ? rule->id : std::string(),
        .used_fallback = selected->used_fallback,
    };
    if (!m_blackflow_session->apply_encounter_selection(*context, std::move(selection), &error)) {
        LogError << "BlackFlow encounter result failed" << error;
        return std::nullopt;
    }
    report_selected_option(event, *selected, rule);

    if (m_blackflow_session->terminated()) {
        set_blackflow_result(BlackFlowTerminatedTask);
        return std::nullopt;
    }
    if (duel_node) {
        // 事件页的第一项只是“直视对方”，随后必须进入独立的三组敌人选择页；
        // 只有敌人组评估并点击完成后才进入通用编队和战斗链。
        if (!Task.lazy_parse(
                json::object {
                    { "BlackFlow@Roguelike@DuelOpponentSelection-Enter",
                      json::object { { "next", json::array { std::string(BlackFlowDuelOpponentSelectionTask) } } } },
                })) {
            LogError << "Failed to configure duel opponent-selection chain";
            return std::nullopt;
        }
        set_blackflow_result("BlackFlow@Roguelike@DuelOpponentSelection-Enter");
        return std::nullopt;
    }
    if (event.continue_single_option) {
        return continue_blackflow_event(event);
    }
    if (const auto result_task = event.option_tasks.find(selected_text); result_task != event.option_tasks.end()) {
        // 经独立入口转发，保留目标任务的模板、回调名称和执行次数。
        if (!Task.lazy_parse(
                json::object {
                    { std::string(BlackFlowOptionTask),
                      json::object { { "next", json::array { result_task->second } } } },
                })) {
            LogError << __FUNCTION__ << "Failed to configure encounter option task" << result_task->second;
            return std::nullopt;
        }
        set_blackflow_result(BlackFlowOptionTask);
        return std::nullopt;
    }
    set_blackflow_result(BlackFlowRewardTask);
    return next_event(event);
}

std::optional<asst::RoguelikeStageEncounterTaskPlugin::SelectedOption>
    asst::RoguelikeStageEncounterTaskPlugin::select_event_option(const SelectionPlan& plan, std::string_view event_name)
{
    if (plan.continue_single_option) {
        const auto enabled = [](const OptionAnalyzer::Option& option) {
            return option.enabled;
        };
        if (std::ranges::count_if(m_option_list, enabled) != 1) {
            LogError << "RoguelikeEncounter | Continuous event requires exactly one enabled option" << event_name;
            return std::nullopt;
        }
        const size_t index = std::distance(m_option_list.begin(), std::ranges::find_if(m_option_list, enabled));
        if (select_analyzed_option(index)) {
            return SelectedOption { index, false };
        }
        return std::nullopt;
    }

    size_t choice = 0; // 以 0 作为无效 index。
    bool used_fallback = false;
    if (!plan.option_text.empty()) {
        for (const std::string& target : plan.option_text) {
            const auto option_it = std::ranges::find_if(m_option_list, [&target](const OptionAnalyzer::Option& option) {
                return option.text == target && option.enabled;
            });
            if (option_it != m_option_list.end()) {
                choice = std::distance(m_option_list.begin(), option_it) + 1;
                break;
            }
        }
    }
    else if (plan.option_num == m_option_list.size()) {
        choice = plan.choose;
    }
    else {
        for (const auto& [total, item] : plan.fallback_choices) {
            if (total == m_option_list.size()) {
                choice = item;
                used_fallback = true;
                break;
            }
        }
    }

    if (choice == 0) {
        LogInfo << "RoguelikeEncounter | No configured choice for" << event_name << "with" << m_option_list.size()
                << "option(s)";
    }
    if (choice == 0 || choice > m_option_list.size() || !m_option_list[choice - 1].enabled) {
        if (!plan.allow_fallback) {
            LogError << "RoguelikeEncounter | Target option unavailable and fallback disabled" << event_name;
            return std::nullopt;
        }
        // 点击前确定可用备选，执行失败后不再操作另一条旧选项。
        choice = m_option_list.size();
        while (choice > 0 && !m_option_list[choice - 1].enabled) {
            --choice;
        }
        used_fallback = true;
    }
    if (choice == 0 || !select_analyzed_option(choice - 1)) {
        return std::nullopt;
    }
    return SelectedOption { choice - 1, used_fallback };
}

bool asst::RoguelikeStageEncounterTaskPlugin::select_analyzed_option(size_t index)
{
    LogTraceFunction;

    if (index >= m_option_list.size()) [[unlikely]] {
        LogError << __FUNCTION__ << "Attempt to select option" << index + 1 << "out of" << m_option_list.size();
        return false;
    }
    if (!m_option_list[index].enabled) {
        LogInfo << __FUNCTION__ << "Attempt to select disabled option" << index + 1;
        return false;
    }

    using Clock = std::chrono::steady_clock;
    constexpr auto ClickRetryDelay = std::chrono::seconds(3);
    constexpr unsigned ObservationDelay = 500;

    const auto confirm_task = Task.get(m_config->get_theme() + "@Roguelike@StageEncounterLeaveConfirm");
    auto next_click = Clock::time_point {};
    bool option_clicked = false;
    int confirmation_clicks = 0;
    while (!need_exit()) {
        const auto observed_at = Clock::now();
        const cv::Mat image = get_event_image();
        if (image.empty() || need_exit()) {
            return false;
        }

        Matcher::ResultOpt confirmation;
        if (option_clicked) {
            // 点选后每张新图先检查确认，滑动定位期间也能接上晚到的确认。
            Matcher confirm(image);
            confirm.set_task_info(confirm_task->name);
            confirmation = confirm.analyze();
            if (need_exit()) {
                return false;
            }
            if (confirmation_clicks > 0 && !confirmation) {
                return true;
            }
        }

        if (confirmation) {
            if (confirmation_clicks == 0 || observed_at >= next_click) {
                // 最后一次点击也先观察完整一轮，确认提前消失时仍按成功处理。
                if (confirmation_clicks >= confirm_task->max_times) {
                    LogError << __FUNCTION__ << "Confirmation is still visible after" << confirmation_clicks
                             << "click(s)" << m_option_list[index].text;
                    save_img(image, "confirmation still visible");
                    return false;
                }
                LogInfo << __FUNCTION__ << "Confirming option" << index + 1 << m_option_list[index].text;
                if (!ctrler()->click(confirmation->rect)) {
                    return false;
                }
                ++confirmation_clicks;
                next_click = Clock::now() + ClickRetryDelay;
            }
        }
        else if (!option_clicked || observed_at >= next_click) {
            if (!update_view(image)) {
                return false;
            }

            // 每轮只推进一次定位，继续寻找同一目标；滑动和匹配不刷新点击计时。
            if (index < m_view_begin) {
                if (!move_backward()) {
                    return false;
                }
                continue;
            }
            if (index >= m_view_end) {
                if (!move_forward()) {
                    return false;
                }
                continue;
            }
            if (m_option_y_in_view[index] == UNDEFINED) {
                LogError << __FUNCTION__ << "Target option is missing within the visible range" << index + 1;
                continue;
            }
            if (!click_analyzed_option(index)) {
                return false;
            }
            option_clicked = true;
            next_click = Clock::now() + ClickRetryDelay;
        }
        if (!sleep(ObservationDelay)) {
            return false;
        }
    }
    return false;
}

bool asst::RoguelikeStageEncounterTaskPlugin::click_analyzed_option(size_t index)
{
    if (need_exit()) {
        return false;
    }

    LogInfo << __FUNCTION__ << "Clicking option" << index + 1 << m_option_list[index].text;
    if (m_config->get_theme() == RoguelikeTheme::JieGarden) {
        Rect click_rect = Task.get("JieGarden@RoguelikeEncounter-ClickOption")->specific_rect;
        click_rect.y = m_option_y_in_view[index];
        return ctrler()->click(click_rect);
    }

    const Rect& header_rect = m_option_rect_in_view[index];
    if (header_rect.x == UNDEFINED) {
        LogError << __FUNCTION__ << "BlackFlow option header is unavailable";
        return false;
    }
    const Point click_point {
        header_rect.x + header_rect.width / 2 + 100,
        header_rect.y + header_rect.height / 2,
    };
    return ctrler()->click(click_point);
}

cv::Mat asst::RoguelikeStageEncounterTaskPlugin::get_event_image()
{
    while (!need_exit()) {
        cv::Mat image = ctrler()->get_image();
        if (image.empty()) {
            LogError << __FUNCTION__ << "Failed to capture the encounter page";
            return {};
        }

        OCRer loading(image);
        loading.set_task_info("LoadingText");
        if (!loading.analyze()) {
            return image;
        }

        // 加载结束后只返回本次事件，由调用者继续观察确认或下一组选项。
        ProcessTask wait_task(*this, { std::string(EncounterLoadingCompletedTask) + "@LoadingText" });
        wait_task.set_reusable_image(image);
        if (!wait_task.run() || need_exit() || wait_task.get_last_task_name() != EncounterLoadingCompletedTask) {
            return {};
        }
    }
    return {};
}

void asst::RoguelikeStageEncounterTaskPlugin::reset_option_list_and_view_data()
{
    m_option_list.clear();
    reset_view();
}

void asst::RoguelikeStageEncounterTaskPlugin::report_analyzed_options()
{
    std::vector<json::value> options;

    Log.info("Analyzed Options");
    Log.info(std::string(40, '-'));
    Log.info(std::format("{:^9} | {}", "Enabled", "Text"));
    Log.info(std::string(40, '-'));
    for (const auto& [enabled, templ, text, merged_y] : m_option_list) {
        json::value option = json::object {
            { "enabled", enabled },
            { "text", text },
        };
        options.emplace_back(std::move(option));
        Log.info(std::format("{:^9} | {}", enabled ? "Y" : "N", text));
    }
    Log.info(std::string(40, '-'));

    json::value info = basic_info_with_what("RoguelikeEncounterOptions");
    info["details"]["options"] = std::move(options);
    callback(AsstMsg::SubTaskExtraInfo, info);
}

bool asst::RoguelikeStageEncounterTaskPlugin::update_view(const cv::Mat& image)
{
    LogTraceFunction;

    reset_view();
    cv::Mat img = image.empty() ? ctrler()->get_image() : image;
    if (img.empty() || need_exit()) {
        return false;
    }

    if (anchored_view_enabled(m_config->get_theme())) {
        update_anchored_view(img);
    }
    else {
        for (size_t i = 0; i < m_option_list.size() && !need_exit(); ++i) {
            if (const auto option_match = RoguelikeEncounterOptionAnalyzer::match_option(
                    m_config->get_theme(),
                    img,
                    m_option_list[i].templ)) {
                m_view_begin = std::min(m_view_begin, i);
                m_view_end = std::max(m_view_end, i + 1);
                m_option_y_in_view[i] = option_match->rect.y;
                m_option_rect_in_view[i] = option_match->rect;
            }
        }
    }

    LogInfo << __FUNCTION__ << std::format("| Current view is [{}, {}]", m_view_begin + 1, m_view_end);
    return !need_exit();
}

// 滑出画面的选项仍会在相似条带上取得过阈值的分数；只信任得分最高的一项，
// 其余选项按拼接图中的相对位置整体平移，落在搜索范围内才算可见。
void asst::RoguelikeStageEncounterTaskPlugin::update_anchored_view(const cv::Mat& image)
{
    const std::string& theme = m_config->get_theme();

    size_t anchor = m_option_list.size();
    Matcher::Result anchor_match;
    for (size_t i = 0; i < m_option_list.size() && !need_exit(); ++i) {
        const auto option_match = RoguelikeEncounterOptionAnalyzer::match_option(theme, image, m_option_list[i].templ);
        if (option_match && (anchor == m_option_list.size() || option_match->score > anchor_match.score)) {
            anchor = i;
            anchor_match = *option_match;
        }
    }
    if (anchor == m_option_list.size() || need_exit()) {
        return;
    }

    const int offset = anchor_match.rect.y - m_option_list[anchor].merged_y;
    const Rect roi = RoguelikeEncounterOptionAnalyzer::option_roi(theme, image.rows);
    for (size_t i = 0; i < m_option_list.size(); ++i) {
        Rect rect = anchor_match.rect;
        rect.y = m_option_list[i].merged_y + offset;
        if (rect.y < roi.y || rect.y + rect.height > roi.y + roi.height) {
            continue;
        }
        m_view_begin = std::min(m_view_begin, i);
        m_view_end = std::max(m_view_end, i + 1);
        m_option_y_in_view[i] = rect.y;
        m_option_rect_in_view[i] = rect;
    }

    LogInfo << __FUNCTION__ << "| Anchor option" << anchor + 1 << "score" << anchor_match.score << "offset" << offset;
}

void asst::RoguelikeStageEncounterTaskPlugin::reset_view()
{
    m_view_begin = m_option_list.size();
    m_view_end = 0;
    m_option_y_in_view.assign(m_option_list.size(), UNDEFINED);
    m_option_rect_in_view.assign(m_option_list.size(), Rect { UNDEFINED, UNDEFINED, 0, 0 });
}

bool asst::RoguelikeStageEncounterTaskPlugin::move_to_option_list_head()
{
    LogTraceFunction;

    return !need_exit() && ProcessTask(*this, { m_config->get_theme() + "@RoguelikeEncounter-InitialMoveUp" }).run() &&
           !need_exit();
}

bool asst::RoguelikeStageEncounterTaskPlugin::move_forward()
{
    LogTraceFunction;

    return !need_exit() && ProcessTask(*this, { m_config->get_theme() + "@RoguelikeEncounter-MoveDown" }).run() &&
           !need_exit();
}

bool asst::RoguelikeStageEncounterTaskPlugin::move_backward()
{
    LogTraceFunction;

    return !need_exit() && ProcessTask(*this, { m_config->get_theme() + "@RoguelikeEncounter-MoveUp" }).run() &&
           !need_exit();
}

std::optional<std::string>
    asst::RoguelikeStageEncounterTaskPlugin::continue_blackflow_event(const Config::RoguelikeEvent& event)
{
    ProcessTask continuation(*this, { std::string(BlackFlowContinueTask) });
    if (!continuation.run() || need_exit()) {
        return std::nullopt;
    }

    const std::string& last_task = continuation.get_last_task_name();
    if (last_task == BlackFlowContinueMapReadyTask) {
        set_blackflow_result(BlackFlowStagesTask);
        return std::nullopt;
    }
    if (last_task != BlackFlowContinueOptionsReadyTask) {
        LogError << "Continuous encounter did not reach another option page or the map" << event.name << last_task;
        return std::nullopt;
    }

    OCRer title(ctrler()->get_image());
    title.set_task_info(std::string(BlackFlowTitleOcrTask));
    title.set_required({ event.name });
    if (!title.analyze() || title.get_result().empty() || title.get_result().front().text != event.name) {
        LogError << "Continuous encounter title changed or could not be recognized" << event.name;
        return std::nullopt;
    }
    return event.name;
}

std::optional<std::string> asst::RoguelikeStageEncounterTaskPlugin::next_event(const Config::RoguelikeEvent& event)
{
    LogTraceFunction;

    if (event.next_event.empty()) {
        return std::nullopt;
    }
    if (m_config->get_theme() == RoguelikeTheme::JieGarden) {
        return next_jiegarden_event(event);
    }

    const auto& task = Task.get("Roguelike@StageEncounterJudgeClick");
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 2; ++j) {
            ctrler()->click(task->specific_rect);
            sleep(500);
        }
        if (hp(ctrler()->get_image()) >= 0) {
            Log.debug("HP restored, going to next_event:", event.next_event);
            // 多点一次，确保选项恢复
            ctrler()->click(task->specific_rect);
            sleep(500);
            return event.next_event;
        }
    }

    return std::nullopt;
}

std::optional<std::string>
    asst::RoguelikeStageEncounterTaskPlugin::next_jiegarden_event(const Config::RoguelikeEvent& event)
{
    // 保留原有三轮、每轮两次的叙述推进次数，每次点击前检查下一组选项。
    constexpr int MaxAdvanceClicks = 3 * 2;
    const auto click_task = Task.get("Roguelike@StageEncounterJudgeClick");
    for (int clicks = 0; clicks <= MaxAdvanceClicks && !need_exit(); ++clicks) {
        const cv::Mat image = get_event_image();
        if (image.empty() || need_exit()) {
            return std::nullopt;
        }

        Matcher options(image);
        options.set_task_info("JieGarden@RoguelikeEncounterOptionAnalyzer-OptionHeaderBar");
        if (options.analyze()) {
            return event.next_event;
        }
        Matcher map(image);
        map.set_task_info("Roguelike@TutorialButton");
        if (map.analyze() || clicks == MaxAdvanceClicks) {
            return std::nullopt;
        }
        if (need_exit() || !ctrler()->click(click_task->specific_rect) || !sleep(500)) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool asst::RoguelikeStageEncounterTaskPlugin::save_img(const cv::Mat& image, const std::string_view description)
{
    return utils::save_debug_image(image, utils::path("debug") / "roguelike" / "encounter", true, description);
}
