#include "BlackFlowSoulObserver.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

#include "Config/TaskData.h"
#include "MaaUtils/ImageIo.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Vision/Matcher.h"
#include "Vision/OCRer.h"

using namespace asst::blackflow;

bool BlackFlowSoulObserver::load(const std::filesystem::path& directory, std::string* error)
{
    m_ready = false;
    m_goal_life_roi.release();
    m_goal_life_cached = {};
    m_goal_life_ocr_attempts = 0;
    m_boss.clear();
    m_selected_boss.clear();
    m_soul.clear();
    const auto load_profile =
        [&](const char* image_name, const char* mask_name, cv::Point2d anchor, std::vector<Profile>& profiles) {
            const auto image = MAA_NS::imread(directory / image_name, cv::IMREAD_GRAYSCALE);
            const auto mask = MAA_NS::imread(directory / mask_name, cv::IMREAD_GRAYSCALE);
            if (image.empty() || mask.empty() || image.size() != mask.size() || cv::countNonZero(mask) == 0) {
                return false;
            }
            for (const double scale : { 0.85, 1.0, 1.15 }) {
                Profile profile;
                const cv::Size size(std::lround(image.cols * scale), std::lround(image.rows * scale));
                cv::resize(image, profile.image, size, 0, 0, cv::INTER_LINEAR);
                cv::resize(mask, profile.mask, size, 0, 0, cv::INTER_NEAREST);
                cv::resize(profile.image, profile.coarse_image, {}, 0.5, 0.5, cv::INTER_AREA);
                cv::resize(profile.mask, profile.coarse_mask, profile.coarse_image.size(), 0, 0, cv::INTER_NEAREST);
                profile.anchor = anchor * scale;
                profiles.emplace_back(std::move(profile));
            }
            return true;
        };
    if (!load_profile("boss.png", "boss_mask.png", { 54, 112 }, m_boss) ||
        !load_profile("selected_boss.png", "selected_boss_mask.png", { 65, 114 }, m_selected_boss) ||
        !load_profile("soul.png", "soul_mask.png", { 31, 51 }, m_soul)) {
        if (error != nullptr) {
            *error = "soul observer templates are missing or inconsistent";
        }
        return false;
    }
    m_ready = true;
    m_pause_mask = MAA_NS::imread(directory / "pause_mask.png", cv::IMREAD_GRAYSCALE);
    if (m_pause_mask.size() != cv::Size(60, 60) || cv::countNonZero(m_pause_mask) < 20) {
        m_ready = false;
        if (error != nullptr) {
            *error = "soul observer pause template is invalid";
        }
    }
    m_retreat_mask = MAA_NS::imread(directory / "retreat_mask.png", cv::IMREAD_GRAYSCALE);
    if (m_retreat_mask.size() != cv::Size(50, 50) || cv::countNonZero(m_retreat_mask) < 20) {
        m_ready = false;
        if (error != nullptr) {
            *error = "soul observer retreat template is invalid";
        }
    }
    return m_ready;
}

bool BlackFlowSoulObserver::retreat_button(const cv::Mat& image, Point expected) const
{
    if (!m_ready || image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3) {
        return false;
    }
    const cv::Rect bounds(0, 0, image.cols, image.rows);
    const auto roi = cv::Rect(expected.x - 65, expected.y - 60, 130, 120) & bounds;
    if (roi.width != 130 || roi.height != 120) {
        return false;
    }
    cv::Mat red = cv::Mat::zeros(roi.size(), CV_32FC1);
    for (int y = 0; y < roi.height; ++y) {
        const auto* pixels = image.ptr<cv::Vec3b>(roi.y + y) + roi.x;
        auto* mask = red.ptr<float>(y);
        for (int x = 0; x < roi.width; ++x) {
            const int blue = pixels[x][0], green = pixels[x][1], value = pixels[x][2];
            mask[x] = value >= 120 && value - green >= 45 && value - blue >= 45 ? 1.0F : 0.0F;
        }
    }
    // A half-pixel change on this 50px glyph already changes its binary edge.
    // Search the same scale bounds at that resolution, without lowering Dice.
    for (int step = 0; step <= 30; ++step) {
        const int offset = step == 0 ? 0 : (step % 2 == 0 ? step / 2 : -(step + 1) / 2);
        const double scale = 1.0 + offset * 0.01;
        cv::Mat glyph, intersection, occupancy;
        cv::resize(m_retreat_mask, glyph, {}, scale, scale, cv::INTER_NEAREST);
        glyph.convertTo(glyph, CV_32FC1, 1.0 / 255.0);
        cv::matchTemplate(red, glyph, intersection, cv::TM_CCORR);
        cv::matchTemplate(red, cv::Mat::ones(glyph.size(), CV_32FC1), occupancy, cv::TM_CCORR);
        const double foreground = cv::sum(glyph)[0];
        cv::Mat denominator;
        cv::max(occupancy + foreground, 1.0, denominator);
        cv::Mat dice;
        cv::divide(2.0 * intersection, denominator, dice);
        double maximum = 0;
        cv::minMaxLoc(dice, nullptr, &maximum);
        if (maximum >= 0.9) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> BlackFlowSoulObserver::battle_end_scene(const cv::Mat& image) const
{
    if (!m_ready || image.type() != CV_8UC3 || image.cols != 1280 || image.rows != 720) {
        return std::nullopt;
    }
    for (const auto* task : { "BlackFlow@Roguelike@MissionCompletedFlag",
                              "BlackFlow@Roguelike@MissionFailedFlag",
                              "BlackFlow@Roguelike@MissionFailedFlag2" }) {
        asst::Matcher matcher(image);
        matcher.set_task_info(task);
        if (matcher.analyze()) {
            return task;
        }
    }
    return std::nullopt;
}

bool BlackFlowSoulObserver::normal_running(const cv::Mat& image) const
{
    if (!m_ready || image.type() != CV_8UC3 || image.cols != 1280 || image.rows != 720 || paused(image)) {
        return false;
    }
    // Both speed glyphs disappear in a selected-unit panel. Match their actual
    // shapes instead of treating a missing panel/name as proof of normal view.
    asst::Matcher speed(image);
    speed.set_roi({ 1060, 20, 80, 70 });
    speed.set_threshold(0.9);
    speed.set_method(asst::MatchMethod::Ccoeff);
    speed.set_templ("Battle/BattleFlag/BattleSpeedUpCheck.png");
    if (!speed.analyze()) {
        speed.set_templ("Battle/BattleFlag/BattleSpeedUp.png");
        if (!speed.analyze()) {
            return false;
        }
    }
    asst::Matcher counter(image);
    counter.set_task_info("BattleKillsFlag");
    return counter.analyze().has_value();
}

bool BlackFlowSoulObserver::paused(const cv::Mat& image) const
{
    if (!m_ready || image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3) {
        return false;
    }
    cv::Mat gray, white, intersection;
    cv::cvtColor(image(cv::Rect(1170, 20, 60, 60)), gray, cv::COLOR_BGR2GRAY);
    cv::threshold(gray, white, 200, 255, cv::THRESH_BINARY);
    cv::bitwise_and(white, m_pause_mask, intersection);
    const int total = cv::countNonZero(white) + cv::countNonZero(m_pause_mask);
    return total > 0 && 2.0 * cv::countNonZero(intersection) / total >= 0.9;
}

BossBodyEvidence BlackFlowSoulObserver::boss(const cv::Mat& normal_image, bool selection_view) const
{
    return boss_impl(normal_image, selection_view ? Rect(380, 60, 770, 525) : Rect(0, 60, 1150, 525), selection_view);
}

BossBodyEvidence BlackFlowSoulObserver::boss_in_roi(const cv::Mat& normal_image, Rect proposal) const
{
    return boss_impl(normal_image, proposal, false);
}

BossBodyEvidence
    BlackFlowSoulObserver::boss_impl(const cv::Mat& normal_image, Rect requested, bool selection_view) const
{
    BossBodyEvidence best;
    if (!m_ready || normal_image.size() != cv::Size(1280, 720) || normal_image.type() != CV_8UC3) {
        return best;
    }
    cv::Mat gray;
    // A selected unit's portrait is not a visible battlefield Boss instance.
    const cv::Rect permitted = selection_view ? cv::Rect(380, 60, 770, 525) : cv::Rect(0, 60, 1150, 525);
    const cv::Rect search_rect = cv::Rect(requested.x, requested.y, requested.width, requested.height) & permitted;
    if (search_rect.width < 2 || search_rect.height < 2) {
        return best;
    }
    cv::cvtColor(normal_image(search_rect), gray, cv::COLOR_BGR2GRAY);
    cv::Mat coarse;
    cv::resize(gray, coarse, {}, 0.5, 0.5, cv::INTER_AREA);
    for (const auto& profile : selection_view ? m_selected_boss : m_boss) {
        if (gray.cols < profile.image.cols || gray.rows < profile.image.rows ||
            coarse.cols < profile.coarse_image.cols || coarse.rows < profile.coarse_image.rows) {
            continue;
        }
        cv::Mat scores;
        cv::matchTemplate(coarse, profile.coarse_image, scores, cv::TM_CCOEFF_NORMED, profile.coarse_mask);
        for (int row = 0; row < scores.rows; ++row) {
            auto* values = scores.ptr<float>(row);
            for (int column = 0; column < scores.cols; ++column) {
                if (!std::isfinite(values[column])) {
                    values[column] = -1.0F;
                }
            }
        }
        double score = -1;
        cv::Point point;
        for (int candidate = 0; candidate < 3; ++candidate) {
            double coarse_score = -1;
            cv::Point coarse_point;
            cv::minMaxLoc(scores, nullptr, &coarse_score, nullptr, &coarse_point);
            const cv::Point origin(
                std::lround(coarse_point.x * static_cast<double>(gray.cols) / coarse.cols),
                std::lround(coarse_point.y * static_cast<double>(gray.rows) / coarse.rows));
            const auto local_roi =
                cv::Rect(origin.x - 8, origin.y - 8, profile.image.cols + 16, profile.image.rows + 16) &
                cv::Rect(0, 0, gray.cols, gray.rows);
            if (local_roi.width >= profile.image.cols && local_roi.height >= profile.image.rows) {
                cv::Mat refined;
                cv::matchTemplate(gray(local_roi), profile.image, refined, cv::TM_CCOEFF_NORMED, profile.mask);
                cv::patchNaNs(refined, -1);
                double refined_score = -1;
                cv::Point refined_point;
                cv::minMaxLoc(refined, nullptr, &refined_score, nullptr, &refined_point);
                if (std::isfinite(refined_score) && refined_score > score) {
                    score = refined_score;
                    point = local_roi.tl() + refined_point;
                }
            }
            scores(
                cv::Rect(coarse_point.x - 12, coarse_point.y - 12, 25, 25) & cv::Rect(0, 0, scores.cols, scores.rows))
                .setTo(-1);
        }
        if (score <= best.score) {
            continue;
        }
        best = {};
        best.score = score;
        point += search_rect.tl();
        best.body = { point.x, point.y, profile.image.cols, profile.image.rows };
        if (score < m_body_threshold) {
            continue;
        }
        const auto anchor = cv::Point2d(point) + profile.anchor;
        const cv::Rect hp_rect =
            (selection_view ? cv::Rect(std::lround(anchor.x - 44), std::lround(anchor.y - 6), 28, 24)
                            : cv::Rect(std::lround(anchor.x - 54), std::lround(anchor.y + 10), 24, 38)) &
            cv::Rect(0, 0, normal_image.cols, normal_image.rows);
        cv::Mat red = cv::Mat::zeros(hp_rect.size(), CV_8UC1);
        const auto roi = normal_image(hp_rect);
        for (int y = 0; y < roi.rows; ++y) {
            const auto* pixels = roi.ptr<cv::Vec3b>(y);
            auto* mask = red.ptr<unsigned char>(y);
            for (int x = 0; x < roi.cols; ++x) {
                const int blue = pixels[x][0], green = pixels[x][1], value = pixels[x][2];
                mask[x] =
                    value >= 140 && green <= 100 && blue <= 100 && value - green >= 45 && value - blue >= 45 ? 255 : 0;
            }
        }
        if (selection_view) {
            // Selection shading can connect a real HP strip to surrounding red
            // terrain. Require a thin filled strip with separate upper/lower
            // edges, inside the body's left HP region, excluding its foot mark.
            for (int y = 1; y + 4 < red.rows && !best.positive_hp; ++y) {
                for (int height = 4; height <= 9 && y + height < red.rows && !best.positive_hp; ++height) {
                    for (int x = 0; x + 6 <= red.cols; ++x) {
                        if (cv::countNonZero(red(cv::Rect(x, y, 6, height))) != 6 * height ||
                            cv::countNonZero(red(cv::Rect(x, y - 1, 6, 1))) > 1 ||
                            cv::countNonZero(red(cv::Rect(x, y + height, 6, 1))) > 1) {
                            continue;
                        }
                        best.positive_hp = true;
                        best.hp_fill = { hp_rect.x + x, hp_rect.y + y, 6, height };
                        break;
                    }
                }
            }
            continue;
        }
        cv::Mat labels, stats, centroids;
        const int count = cv::connectedComponentsWithStats(red, labels, stats, centroids);
        for (int index = 1; index < count; ++index) {
            const int width = stats.at<int>(index, cv::CC_STAT_WIDTH);
            const int height = stats.at<int>(index, cv::CC_STAT_HEIGHT);
            const int area = stats.at<int>(index, cv::CC_STAT_AREA);
            if (width >= 6 && height >= 4 && height <= 9 && static_cast<double>(area) / (width * height) >= 0.85) {
                best.positive_hp = true;
                best.hp_fill = { hp_rect.x + stats.at<int>(index, cv::CC_STAT_LEFT),
                                 hp_rect.y + stats.at<int>(index, cv::CC_STAT_TOP),
                                 width,
                                 height };
                break;
            }
        }
    }
    return best;
}

std::vector<asst::Point>
    BlackFlowSoulObserver::souls(const cv::Mat& normal_image, const std::vector<SoulSite>& legal_sites) const
{
    std::vector<std::pair<double, Point>> found;
    if (!m_ready || normal_image.size() != cv::Size(1280, 720) || normal_image.type() != CV_8UC3) {
        return {};
    }
    cv::Mat gray;
    cv::cvtColor(normal_image, gray, cv::COLOR_BGR2GRAY);
    const cv::Rect bounds(0, 60, 1150, 525);
    for (const auto& site : legal_sites) {
        double best = -1;
        for (const auto& profile : m_soul) {
            const cv::Rect roi = cv::Rect(
                                     std::lround(site.pixel.x - profile.anchor.x) - 12,
                                     std::lround(site.pixel.y - profile.anchor.y) - 12,
                                     profile.image.cols + 24,
                                     profile.image.rows + 24) &
                                 bounds;
            if (roi.width < profile.image.cols || roi.height < profile.image.rows) {
                continue;
            }
            cv::Mat scores;
            cv::matchTemplate(gray(roi), profile.image, scores, cv::TM_CCOEFF_NORMED, profile.mask);
            for (int y = 0; y < scores.rows; ++y) {
                const auto* values = scores.ptr<float>(y);
                for (int x = 0; x < scores.cols; ++x) {
                    if (std::isfinite(values[x])) {
                        best = std::max(best, static_cast<double>(values[x]));
                    }
                }
            }
        }
        if (best >= m_soul_threshold) {
            found.emplace_back(best, site.tile);
        }
    }
    std::ranges::stable_sort(found, [](const auto& first, const auto& second) { return first.first > second.first; });
    std::vector<Point> result;
    for (const auto& [score, tile] : found) {
        result.emplace_back(tile);
    }
    return result;
}

GoalLifeObservation BlackFlowSoulObserver::goal_life(const cv::Mat& image)
{
    GoalLifeObservation observed;
    const auto reject = [&](std::string reason) {
        m_goal_life_roi.release();
        m_goal_life_cached = {};
        observed.reason = std::move(reason);
        return observed;
    };
    if (!m_ready || image.type() != CV_8UC3 || image.size() != cv::Size(1280, 720)) {
        return reject("observer_or_frame_unavailable");
    }
    if (paused(image) || battle_end_scene(image)) {
        return reject("paused_or_terminal_scene");
    }
    asst::Matcher kills(image);
    kills.set_task_info("BattleKillsFlag");
    if (!kills.analyze()) {
        return reject("battle_counter_anchor_missing");
    }
    for (const auto* task_name : { "BattleHpFlag", "BattleHpFlag2" }) {
        asst::Matcher flag(image);
        flag.set_task_info(task_name);
        flag.set_threshold(0.9);
        if (const auto match = flag.analyze()) {
            const auto task = Task.get(task_name);
            auto roi = match->rect.move(task->rect_move);
            // The task's original100px region approaches the red loss badge.
            // Keep only the numeric field immediately right of the goal flag.
            roi.width = std::min(roi.width, 80);
            const cv::Rect bounds(0, 0, image.cols, image.rows);
            const auto area = asst::make_rect<cv::Rect>(roi);
            if (roi.empty() || (area & bounds) != area || roi.y < 0 || roi.y + roi.height > 65) {
                return reject("goal_number_roi_invalid");
            }
            observed.hud_verified = true;
            observed.anchor_task = task_name;
            observed.number_roi = roi;
            const auto pixels = image(area);
            if (!m_goal_life_roi.empty() && m_goal_life_cached.anchor_task == observed.anchor_task &&
                m_goal_life_cached.number_roi == roi && m_goal_life_roi.size() == pixels.size() &&
                cv::norm(m_goal_life_roi, pixels, cv::NORM_INF) == 0) {
                observed = m_goal_life_cached;
                observed.cache_hit = true;
                observed.ocr_performed = false;
                return observed;
            }
            // Cache unknown ink/recognition too, so an unchanged unreadable ROI
            // does not spend an OCR call on every actor frame.
            const auto cache = [&] {
                m_goal_life_roi = pixels.clone();
                m_goal_life_cached = observed;
                return observed;
            };
            cv::Mat ink = cv::Mat::zeros(pixels.size(), CV_8UC1);
            for (int y = 0; y < pixels.rows; ++y) {
                const auto* row = pixels.ptr<cv::Vec3b>(y);
                auto* output = ink.ptr<unsigned char>(y);
                for (int x = 0; x < pixels.cols; ++x) {
                    const auto& p = row[x];
                    const bool white = std::min({ p[0], p[1], p[2] }) >= 130;
                    const bool red = p[2] >= 120 && p[2] - p[1] >= 45 && p[2] - p[0] >= 45;
                    output[x] = white || red ? 255 : 0;
                }
            }
            const auto glyph = cv::boundingRect(ink);
            if (glyph.empty() || glyph.x < 1 || glyph.y < 1 || glyph.br().x > pixels.cols - 1 ||
                glyph.br().y > pixels.rows - 1 || glyph.width > 60 || glyph.height < 8) {
                observed.reason = "goal_digit_ink_missing_or_clipped";
                return cache();
            }
            const cv::Rect expanded(glyph.x - 1, glyph.y - 1, glyph.width + 2, glyph.height + 2);
            observed.digits_rect = { roi.x + expanded.x, roi.y + expanded.y, expanded.width, expanded.height };
            asst::OCRer ocr(image, observed.digits_rect);
            ocr.set_without_det(true);
            ocr.set_use_char_model(true);
            // No NumberOcrReplace: I, fractions and signed losses stay unknown.
            observed.ocr_performed = true;
            observed.ocr_attempt = ++m_goal_life_ocr_attempts;
            if (const auto result = ocr.analyze(); result && result->size() == 1) {
                observed.raw_digits = result->front().text;
                observed.score = result->front().score;
            }
            if (observed.score < 0.92 || observed.raw_digits.empty() || observed.raw_digits.size() > 3 ||
                !std::ranges::all_of(observed.raw_digits, [](unsigned char c) { return c >= '0' && c <= '9'; })) {
                observed.reason = "goal_digits_unverified";
                return cache();
            }
            observed.life = std::stoi(observed.raw_digits);
            observed.reason = "goal_life_digits_verified";
            return cache();
        }
    }
    return reject("goal_life_anchor_missing");
}
