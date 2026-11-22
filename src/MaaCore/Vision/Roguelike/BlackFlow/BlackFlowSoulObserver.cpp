#include "BlackFlowSoulObserver.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "MaaUtils/ImageIo.h"
#include "MaaUtils/NoWarningCV.hpp"

using namespace asst::blackflow;

bool BlackFlowSoulObserver::load(const std::filesystem::path& directory, std::string* error)
{
    m_ready = false;
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
    for (const double scale : { 0.85, 1.0, 1.15 }) {
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
    BossBodyEvidence best;
    if (!m_ready || normal_image.size() != cv::Size(1280, 720) || normal_image.type() != CV_8UC3) {
        return best;
    }
    cv::Mat gray;
    // A selected unit's portrait is not a visible battlefield Boss instance.
    const cv::Rect search_rect = selection_view ? cv::Rect(380, 60, 770, 525) : cv::Rect(0, 60, 1150, 525);
    cv::cvtColor(normal_image(search_rect), gray, cv::COLOR_BGR2GRAY);
    cv::Mat coarse;
    cv::resize(gray, coarse, {}, 0.5, 0.5, cv::INTER_AREA);
    for (const auto& profile : selection_view ? m_selected_boss : m_boss) {
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

