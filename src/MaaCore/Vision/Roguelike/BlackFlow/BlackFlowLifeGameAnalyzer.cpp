#include "BlackFlowLifeGameAnalyzer.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>

#include "Config/TaskData.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Vision/OCRer.h"

namespace asst::blackflow::life_game_vision
{
std::string compact(std::string_view input)
{
    std::string result;
    for (const unsigned char c : input) {
        if (!std::isspace(c)) {
            result.push_back(static_cast<char>(c));
        }
    }
    return result;
}

std::vector<TextRect> read(const cv::Mat& image, std::string_view task, const std::vector<std::string>& names = {})
{
    const auto definition = Task.get<OcrTaskInfo>(task);
    if (image.empty() || !definition || definition->roi.empty() || definition->roi.x < 0 || definition->roi.y < 0 ||
        definition->roi.x + definition->roi.width > image.cols ||
        definition->roi.y + definition->roi.height > image.rows) {
        return {};
    }
    OCRer reader(image);
    reader.set_task_info(definition);
    if (!names.empty()) {
        reader.set_required(names);
    }
    const auto results = reader.analyze();
    if (!results) {
        return {};
    }
    std::vector<TextRect> accepted;
    for (const auto& result : *results) {
        if (result.score >= 0.90) {
            accepted.push_back(result);
        }
    }
    return accepted;
}

std::optional<std::string> single(const std::vector<TextRect>& values)
{
    if (values.size() != 1) {
        return std::nullopt;
    }
    return compact(values.front().text);
}
}

std::optional<int> asst::blackflow::BlackFlowLifeGameAnalyzer::parse_number(std::string_view text)
{
    const std::string value = life_game_vision::compact(text);
    if (value.empty() || value.size() > 6 ||
        !std::ranges::all_of(value, [](unsigned char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    int result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::pair<int, int>> asst::blackflow::BlackFlowLifeGameAnalyzer::parse_collected(std::string_view text)
{
    const std::string value = life_game_vision::compact(text);
    const auto slash = value.find('/');
    if (slash == std::string::npos || value.find('/', slash + 1) != std::string::npos) {
        return std::nullopt;
    }
    const auto count = parse_number(value.substr(0, slash));
    const auto total = parse_number(value.substr(slash + 1));
    if (!count || !total || *total == 0 || *count > *total || *total > 512) {
        return std::nullopt;
    }
    return std::pair { *count, *total };
}

std::vector<asst::Rect>
    asst::blackflow::BlackFlowLifeGameAnalyzer::selection_candidates(const cv::Mat& image, const Rect& area)
{
    // Only the native 1280x720 layout is validated. These circles locate selectable
    // illustrations; they deliberately supply no active/inactive or node-ID evidence.
    if (image.cols != 1280 || image.rows != 720 || image.channels() != 3 || area.x < 0 || area.y < 0 || area.empty() ||
        area.x + area.width > image.cols || area.y + area.height > image.rows) {
        return {};
    }
    cv::Mat gray;
    cv::cvtColor(image(make_rect<cv::Rect>(area)), gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, { 5, 5 }, 1.0);
    std::vector<Rect> result;
    // KEY illustrations are larger than NORMAL nodes. Search each scale and
    // merge concentric detections; a single radius range missed normal nodes.
    for (const auto radii : { std::pair { 50, 95 }, std::pair { 35, 55 } }) {
        std::vector<cv::Vec3f> circles;
        cv::HoughCircles(gray, circles, cv::HOUGH_GRADIENT, 1.0, 85.0, 100.0, 40.0, radii.first, radii.second);
        for (const auto& circle : circles) {
            const int x = static_cast<int>(std::lround(circle[0])) + area.x;
            const int y = static_cast<int>(std::lround(circle[1])) + area.y;
            if (std::ranges::none_of(result, [x, y](const Rect& previous) {
                    const int dx = previous.x + 8 - x;
                    const int dy = previous.y + 8 - y;
                    return dx * dx + dy * dy < 55 * 55;
                })) {
                result.emplace_back(x - 8, y - 8, 16, 16);
            }
        }
    }
    std::ranges::sort(result, [](const Rect& a, const Rect& b) {
        const int band_a = (a.x + 30) / 60;
        const int band_b = (b.x + 30) / 60;
        return band_a == band_b ? a.y < b.y : band_a < band_b;
    });
    return result;
}

std::optional<asst::Rect> asst::blackflow::BlackFlowLifeGameAnalyzer::selected_candidate(
    const cv::Mat& image,
    const std::vector<Rect>& candidates)
{
    if (image.cols != 1280 || image.rows != 720 || image.channels() != 3) {
        return std::nullopt;
    }
    cv::Mat mask;
    cv::inRange(image, cv::Scalar(230, 230, 230), cv::Scalar(255, 255, 255), mask);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    std::vector<cv::Point> downward;
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        const auto box = cv::boundingRect(contour);
        if (area < 25 || area > 400 || box.width < 10 || box.width > 35 || box.height < 6 || box.height > 30) {
            continue;
        }
        std::vector<cv::Point> polygon;
        cv::approxPolyDP(contour, polygon, 0.06 * cv::arcLength(contour, true), true);
        if (polygon.size() != 3) {
            continue;
        }
        std::ranges::sort(polygon, {}, &cv::Point::y);
        if (std::abs(polygon[0].y - polygon[1].y) <= 3 && polygon[2].y - polygon[1].y >= 5) {
            downward.emplace_back(box.x + box.width / 2, box.y + box.height / 2);
        }
    }
    std::vector<Rect> matches;
    for (const auto& upper : downward) {
        for (const auto& lower : downward) {
            if (std::abs(upper.x - lower.x) > 4 || lower.y - upper.y < 9 || lower.y - upper.y > 20) {
                continue;
            }
            for (const auto& candidate : candidates) {
                const int dy = candidate.y + candidate.height / 2 - lower.y;
                if (std::abs(candidate.x + candidate.width / 2 - lower.x) <= 30 && dy >= 40 && dy <= 140) {
                    matches.push_back(candidate);
                }
            }
        }
    }
    return matches.size() == 1 ? std::optional(matches.front()) : std::nullopt;
}

asst::blackflow::LifeGameScreenObservation
    asst::blackflow::BlackFlowLifeGameAnalyzer::analyze(const std::vector<LifeGameNode>& nodes, bool details) const
{
    LifeGameScreenObservation result;
    if (m_image.cols != 1280 || m_image.rows != 720) {
        return result;
    }
    const auto label =
        life_game_vision::single(life_game_vision::read(m_image, "BlackFlow@Roguelike@LifeGamePageLabel"));
    result.page_verified = label && (*label == "演化算子" || *label == "采集完成");
    result.collection_complete = label && *label == "采集完成";
    if (!result.page_verified) {
        return result;
    }
    if (!result.collection_complete) {
        if (const auto text =
                life_game_vision::single(life_game_vision::read(m_image, "BlackFlow@Roguelike@LifeGamePoints"))) {
            result.points = parse_number(*text);
        }
    }
    const auto counters = life_game_vision::read(m_image, "BlackFlow@Roguelike@LifeGameCollected");
    for (const auto& counter : counters) {
        const auto parsed = parse_collected(counter.text);
        if (!parsed) {
            continue;
        }
        if (result.collected) {
            result.collected.reset();
            result.total.reset();
            break;
        }
        result.collected = parsed->first;
        result.total = parsed->second;
    }
    if (!details) {
        return result;
    }
    std::vector<std::string> names;
    for (const auto& node : nodes) {
        if (std::ranges::find(names, node.name) == names.end()) {
            names.push_back(node.name);
        }
    }
    if (const auto name = life_game_vision::single(
            life_game_vision::read(m_image, "BlackFlow@Roguelike@LifeGameSelectedName", names))) {
        result.selected_name = *name;
    }
    for (const auto& text : life_game_vision::read(m_image, "BlackFlow@Roguelike@LifeGameSelectedEffect")) {
        result.selected_effect += text.text;
    }
    const auto active =
        life_game_vision::single(life_game_vision::read(m_image, "BlackFlow@Roguelike@LifeGameActiveState"));
    const auto unpurchased =
        life_game_vision::single(life_game_vision::read(m_image, "BlackFlow@Roguelike@LifeGameUnpurchasedState"));
    const bool is_active = active && *active == "生效中";
    const bool is_unpurchased = unpurchased && *unpurchased == "确认升级";
    if (is_active != is_unpurchased) {
        result.selected_state = is_active ? LifeGameSelectedState::Active : LifeGameSelectedState::Unpurchased;
    }
    if (is_unpurchased && !is_active) {
        if (const auto text =
                life_game_vision::single(life_game_vision::read(m_image, "BlackFlow@Roguelike@LifeGameCost"))) {
            // The x glyph is part of the cost badge, never an implicit numeric zero.
            const auto offset = text->find_first_not_of("xX*");
            result.selected_cost = offset == std::string::npos ? std::nullopt : parse_number(text->substr(offset));
        }
    }
    if (const auto area = Task.get("BlackFlow@Roguelike@LifeGameNodeArea")) {
        result.selection_candidates = selection_candidates(m_image, area->roi);
        result.selected_rect = selected_candidate(m_image, result.selection_candidates);
    }
    return result;
}

std::vector<std::string> asst::blackflow::BlackFlowLifeGameAnalyzer::selected_node_ids(
    const LifeGameScreenObservation& screen,
    const std::vector<LifeGameNode>& nodes)
{
    std::vector<std::string> result;
    if (!screen.page_verified || screen.selected_name.empty() ||
        screen.selected_state == LifeGameSelectedState::Unknown) {
        return result;
    }
    for (const auto& node : nodes) {
        if (node.name == screen.selected_name && (!screen.selected_cost || node.cost == *screen.selected_cost)) {
            result.push_back(node.id);
        }
    }
    return result;
}

bool asst::blackflow::BlackFlowLifeGameAnalyzer::verifies_upgrade(
    const LifeGameScreenObservation& before,
    const LifeGameScreenObservation& after,
    const LifeGameNode& node)
{
    return node.purchasable && node.cost > 0 && before.page_verified && after.page_verified && before.points &&
           after.points && before.collected && after.collected && before.total && after.total &&
           *before.total == *after.total && *before.collected < *before.total && before.selected_name == node.name &&
           after.selected_name == node.name && before.selected_state == LifeGameSelectedState::Unpurchased &&
           before.selected_cost == node.cost && after.selected_state == LifeGameSelectedState::Active &&
           *after.collected == *before.collected + 1 && *before.points >= node.cost &&
           *after.points == *before.points - node.cost;
}

bool asst::blackflow::BlackFlowLifeGameAnalyzer::verifies_terminal_upgrade(
    const LifeGameScreenObservation& before,
    const LifeGameScreenObservation& after,
    const LifeGameNode& node,
    int expected_total)
{
    // Completion hides the balance. This proves the terminal UI result only;
    // it must never be reported as an observed exact point debit.
    return expected_total > 0 && node.purchasable && node.cost > 0 && before.page_verified && after.page_verified &&
           before.points && !after.points && before.collected && after.collected && before.total && after.total &&
           *before.total == expected_total && *after.total == expected_total &&
           *before.collected == expected_total - 1 && *after.collected == expected_total &&
           !before.collection_complete && after.collection_complete && before.selected_name == node.name &&
           after.selected_name == node.name && before.selected_state == LifeGameSelectedState::Unpurchased &&
           before.selected_cost == node.cost && after.selected_state == LifeGameSelectedState::Active &&
           *before.points >= node.cost;
}
