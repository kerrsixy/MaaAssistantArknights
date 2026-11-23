#include "SPAProjection.h"
#include "SPAOperatorCatalogue.h"
#include "Utils/Logger.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <opencv2/calib3d.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>
#include <set>

namespace asst::spa
{
bool inventory_slot_removal_observed(
    const std::vector<cv::Point>& before,
    const std::vector<cv::Point>& after,
    cv::Point selected)
{
    if (before.empty() || before.size() > 20 || after.size() + 1 != before.size()) {
        return false;
    }
    int sources = 0;
    std::set<size_t> matched;
    for (const auto& slot : before) {
        if (cv::norm(slot - selected) < 15) {
            ++sources;
            if (std::any_of(after.begin(), after.end(), [&](const auto& point) {
                    return cv::norm(point - selected) < 25;
                })) {
                return false;
            }
            continue;
        }
        std::optional<size_t> match;
        for (size_t i = 0; i < after.size(); ++i) {
            if (!matched.contains(i) && cv::norm(after[i] - slot) < 15) {
                if (match) {
                    return false;
                }
                match = i;
            }
        }
        if (!match) {
            return false;
        }
        matched.insert(*match);
    }
    return sources == 1 && matched.size() == after.size();
}

bool pact_hud_single_one_visible(const cv::Mat& image, int center_x)
{
    if (image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3 || center_x <= 300 || center_x >= 1050) {
        return false;
    }
    constexpr int HalfWidth = 16;
    const cv::Rect counter(center_x - HalfWidth, 99, HalfWidth * 2 + 1, 17);
    cv::Mat white(counter.size(), CV_8U);
    for (int y = 0; y < counter.height; ++y) {
        for (int x = 0; x < counter.width; ++x) {
            const auto pixel = image.at<cv::Vec3b>(counter.y + y, counter.x + x);
            const int low = std::min({ pixel[0], pixel[1], pixel[2] });
            const int high = std::max({ pixel[0], pixel[1], pixel[2] });
            white.at<unsigned char>(y, x) = low >= 160 && high - low <= 45 ? 255 : 0;
        }
    }
    cv::Mat labels, stats, centers;
    const int count = cv::connectedComponentsWithStats(white, labels, stats, centers);
    cv::Rect glyph;
    for (int i = 1; i < count; ++i) {
        const int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area < 3) {
            continue;
        }
        if (!glyph.empty() || area < 14 || area > 30) {
            return false;
        }
        glyph = cv::Rect(
            stats.at<int>(i, cv::CC_STAT_LEFT),
            stats.at<int>(i, cv::CC_STAT_TOP),
            stats.at<int>(i, cv::CC_STAT_WIDTH),
            stats.at<int>(i, cv::CC_STAT_HEIGHT));
    }
    if (glyph.width < 4 || glyph.width > 6 || glyph.height < 10 || glyph.height > 12 || glyph.y < 2 ||
        glyph.br().y >= counter.height || std::abs(glyph.x + (glyph.width - 1) / 2.0 - HalfWidth) > 2.5) {
        return false;
    }
    // The HUD's '1' has a left flag and a straight stem, with no bottom bar.
    const auto numeral = white(glyph);
    const int stem_start = glyph.width - 2;
    if (cv::countNonZero(numeral.row(0).colRange(0, stem_start))) {
        return false;
    }
    for (int y = glyph.height / 2; y < glyph.height; ++y) {
        if (cv::countNonZero(numeral.row(y).colRange(0, stem_start)) ||
            !cv::countNonZero(numeral.row(y).colRange(stem_start, glyph.width))) {
            return false;
        }
    }
    return true;
}

std::optional<cv::Rect> defence_life_number_roi(const cv::Mat& image)
{
    if (image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3) {
        return std::nullopt;
    }
    const cv::Rect header(730, 18, 170, 30);
    cv::Mat cyan;
    cv::inRange(image(header), cv::Scalar(155, 155, 0), cv::Scalar(255, 255, 114), cyan);
    cv::Mat joined;
    cv::morphologyEx(cyan, joined, cv::MORPH_CLOSE, cv::Mat::ones(5, 1, CV_8U));
    cv::Mat labels, stats, centers;
    const int count = cv::connectedComponentsWithStats(joined, labels, stats, centers);
    std::optional<cv::Rect> tower;
    for (int i = 1; i < count; ++i) {
        const cv::Rect box(
            stats.at<int>(i, cv::CC_STAT_LEFT),
            stats.at<int>(i, cv::CC_STAT_TOP),
            stats.at<int>(i, cv::CC_STAT_WIDTH),
            stats.at<int>(i, cv::CC_STAT_HEIGHT));
        if (box.width < 8 || box.width > 16 || box.height < 17 || box.height > 24 || box.y < 3 || box.y > 9 ||
            stats.at<int>(i, cv::CC_STAT_AREA) < 110 ||
            cv::countNonZero(cyan(cv::Rect(box.x, box.y, box.width, 5))) < 25 ||
            cv::countNonZero(cyan(cv::Rect(box.x, box.y + box.height - 9, box.width, 9))) < 40) {
            continue;
        }
        if (tower) {
            return std::nullopt;
        }
        tower = box + header.tl();
    }
    if (!tower) {
        return std::nullopt;
    }
    const cv::Rect search(tower->x + tower->width + 3, 23, 55, 22);
    cv::Mat white = cv::Mat::zeros(search.size(), CV_8U);
    int pending_start = search.width;
    for (int x = 0; x < search.width; ++x) {
        int red = 0;
        for (int y = 0; y < search.height; ++y) {
            const auto pixel = image.at<cv::Vec3b>(search.y + y, search.x + x);
            const int low = std::min({ pixel[0], pixel[1], pixel[2] });
            const int high = std::max({ pixel[0], pixel[1], pixel[2] });
            white.at<unsigned char>(y, x) = low > 145 && high - low < 45 ? 255 : 0;
            red += pixel[2] > 150 && pixel[2] > pixel[0] + 60 && pixel[2] > pixel[1] + 60;
        }
        if (red >= 4) {
            pending_start = std::min(pending_start, x);
        }
    }
    if (pending_start < search.width) {
        white.colRange(std::max(0, pending_start - 2), search.width).setTo(0);
    }
    const int glyphs = cv::connectedComponentsWithStats(white, labels, stats, centers);
    std::vector<cv::Rect> digits;
    for (int i = 1; i < glyphs; ++i) {
        const cv::Rect box(
            stats.at<int>(i, cv::CC_STAT_LEFT),
            stats.at<int>(i, cv::CC_STAT_TOP),
            stats.at<int>(i, cv::CC_STAT_WIDTH),
            stats.at<int>(i, cv::CC_STAT_HEIGHT));
        if (box.height >= 12 && box.height <= 19 && box.width >= 3 && box.width <= 17 && box.y > 0 &&
            box.y + box.height < search.height && stats.at<int>(i, cv::CC_STAT_AREA) >= 25) {
            digits.push_back(box);
        }
    }
    std::sort(digits.begin(), digits.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
    if (digits.empty() || digits.front().x > 12) {
        return std::nullopt;
    }
    cv::Rect number = digits.front();
    int used = 1;
    for (size_t i = 1; i < digits.size(); ++i) {
        if (digits[i].x - number.br().x > 6) {
            break;
        }
        number |= digits[i];
        ++used;
    }
    if (used > 3 || number.width > 48) {
        return std::nullopt;
    }
    number += search.tl();
    return cv::Rect(number.x - 2, number.y - 2, number.width + 4, number.height + 4);
}

// The bench detector reports y=450, while inventory probes use badge-relative
// hitboxes near y=466. Both refer to the same expanded bench strip.
constexpr int InventoryOperatorSpriteY = 466;

bool remember_inventory_operator_title(
    std::vector<InventoryOperatorHint>& hints,
    const cv::Mat& image,
    cv::Point source,
    const std::string& title,
    bool title_verified)
{
    const cv::Rect body(source.x - 28, InventoryOperatorSpriteY - 58, 56, 54);
    if (!title_verified || source.x < 140 || source.x >= 1000 || source.y < 450 || source.y > 490 ||
        image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3 ||
        (body & cv::Rect(0, 0, image.cols, image.rows)) != body ||
        std::none_of(OperatorCatalogue.begin(), OperatorCatalogue.end(), [&](const auto& item) {
            return item.name == title;
        })) {
        return false;
    }
    cv::Mat sprite;
    cv::GaussianBlur(image(body), sprite, cv::Size(3, 3), 0);
    cv::Scalar mean, deviation;
    cv::meanStdDev(sprite, mean, deviation);
    if (std::max({ deviation[0], deviation[1], deviation[2] }) < 20) {
        return false;
    }
    // Idle and elite visuals can differ enough that replacing the sole
    // sample makes the next inventory pass open the same operator again.
    // Preserve a short history of distinct, title-verified appearances.
    constexpr int max_appearances_per_title = 4;
    auto oldest = hints.end();
    int appearances = 0;
    for (auto hint = hints.begin(); hint != hints.end(); ++hint) {
        if (hint->title != title) {
            continue;
        }
        if (oldest == hints.end()) {
            oldest = hint;
        }
        ++appearances;
        if (hint->sprite.size() == sprite.size() && hint->sprite.type() == sprite.type()) {
            cv::Mat similarity;
            cv::matchTemplate(sprite, hint->sprite, similarity, cv::TM_CCOEFF_NORMED);
            if (similarity.at<float>(0, 0) >= .985F) {
                return true;
            }
        }
    }
    if (appearances >= max_appearances_per_title) {
        hints.erase(oldest);
    }
    hints.push_back({ title, std::move(sprite) });
    return true;
}

std::optional<std::pair<std::string, double>> inventory_operator_visual_hint(
    const std::vector<InventoryOperatorHint>& hints,
    const cv::Mat& image,
    cv::Point source)
{
    const cv::Rect search(source.x - 30, InventoryOperatorSpriteY - 60, 60, 58);
    if (hints.empty() || source.x < 140 || source.x >= 1000 || source.y < 450 || source.y > 490 ||
        image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3 ||
        (search & cv::Rect(0, 0, image.cols, image.rows)) != search) {
        return std::nullopt;
    }
    cv::Mat current;
    cv::GaussianBlur(image(search), current, cv::Size(3, 3), 0);
    std::string title;
    double best = -1, other = -1;
    for (const auto& hint : hints) {
        if (hint.sprite.size() != cv::Size(56, 54) || hint.sprite.type() != CV_8UC3 || hint.title.empty()) {
            continue;
        }
        cv::Mat scores;
        cv::matchTemplate(current, hint.sprite, scores, cv::TM_CCOEFF_NORMED);
        double score = 0;
        cv::minMaxLoc(scores, nullptr, &score);
        if (!std::isfinite(score)) {
            continue;
        }
        if (score > best) {
            if (hint.title != title) {
                other = best;
            }
            best = score;
            title = hint.title;
        }
        else if (hint.title != title) {
            other = std::max(other, score);
        }
    }
    return best >= .97 && best - other >= .03 ? std::optional(std::pair { title, best }) : std::nullopt;
}

bool bench_operator_visible(const cv::Mat& image, cv::Point center)
{
    if (image.empty() || image.type() != CV_8UC3 || center.x < 140 || center.x >= 1000 || center.y < 420 ||
        center.y >= 510) {
        return false;
    }
    const cv::Rect bounds(0, 0, image.cols, image.rows);
    const cv::Rect roi(center.x - 38, center.y - 58, 76, 78);
    if ((roi & bounds) != roi) {
        return false;
    }
    // A broad saturation/value threshold is tempting here, but it also marks
    // the coloured map floor and the blue empty-slot panels as occupied.  Use
    // the sprite's texture instead: a real operator/equipment sprite has
    // visible edges through at least two vertical bands, whereas a placeholder
    // or terrain patch normally has one long horizontal edge only.
    const cv::Rect body(center.x - 34, center.y - 44, 68, 60);
    if ((body & bounds) != body) {
        return false;
    }
    cv::Mat body_gray;
    cv::cvtColor(image(body), body_gray, cv::COLOR_BGR2GRAY);
    cv::Mat body_edges;
    cv::Canny(body_gray, body_edges, 60, 120);
    const double edge_density =
        static_cast<double>(cv::countNonZero(body_edges)) / static_cast<double>(body_edges.total());
    const double contrast = [&] {
        cv::Scalar mean, stddev;
        cv::meanStdDev(body_gray, mean, stddev);
        return stddev[0];
    }();
    int textured_bands = 0;
    constexpr int band_height = 20;
    for (int band = 0; band < 3; ++band) {
        const cv::Rect band_rect(
            0,
            band * band_height,
            body_edges.cols,
            band == 2 ? body_edges.rows - band * band_height : band_height);
        const double band_density =
            static_cast<double>(cv::countNonZero(body_edges(band_rect))) / static_cast<double>(band_rect.area());
        if (band_density >= 0.11) {
            ++textured_bands;
        }
    }
    if (edge_density >= 0.16 && contrast >= 30.0 && textured_bands >= 2) {
        return true;
    }

    // Operators/equipment carry a small dark tier badge at the upper-left of
    // the bench sprite.  This is a much stronger occupancy signal than broad
    // colour ratios because the terrain and empty slots are also colourful.
    // The badge is roughly 35--45 px above and 35--55 px left of the sprite
    // centre.  Keep the band tight so map props cannot create false slots.
    const cv::Rect badge_band(center.x - 65, center.y - 82, 60, 58);
    if ((badge_band & bounds) != badge_band) {
        return false;
    }
    cv::Mat gray, dark;
    cv::cvtColor(image(badge_band), gray, cv::COLOR_BGR2GRAY);
    cv::inRange(gray, cv::Scalar(15), cv::Scalar(105), dark);
    cv::morphologyEx(dark, dark, cv::MORPH_OPEN, cv::Mat::ones(2, 2, CV_8UC1));
    cv::Mat labels, stats, centroids;
    cv::connectedComponentsWithStats(dark, labels, stats, centroids, 8, CV_32S);
    for (int index = 1; index < stats.rows; ++index) {
        const int width = stats.at<int>(index, cv::CC_STAT_WIDTH);
        const int height = stats.at<int>(index, cv::CC_STAT_HEIGHT);
        const int area = stats.at<int>(index, cv::CC_STAT_AREA);
        const double aspect = static_cast<double>(width) / std::max(height, 1);
        if (width >= 18 && width <= 38 && height >= 18 && height <= 42 && area >= 220 && aspect >= 0.65 &&
            aspect <= 1.5) {
            // The badge alone is still insufficient: map props can contain a
            // similarly sized dark rectangle.  Require a textured sprite body
            // below it as well, but do not recurse into this function (which
            // would re-run badge detection).
            const cv::Rect badge_body(center.x - 34, center.y - 44, 68, 60);
            if ((badge_body & bounds) != badge_body) {
                continue;
            }
            cv::Mat badge_body_gray;
            cv::cvtColor(image(badge_body), badge_body_gray, cv::COLOR_BGR2GRAY);
            cv::Mat badge_body_edges;
            cv::Canny(badge_body_gray, badge_body_edges, 60, 120);
            const double badge_edge_density =
                static_cast<double>(cv::countNonZero(badge_body_edges)) / static_cast<double>(badge_body_edges.total());
            cv::Scalar badge_mean, badge_stddev;
            cv::meanStdDev(badge_body_gray, badge_mean, badge_stddev);
            int badge_textured_bands = 0;
            for (int band = 0; band < 3; ++band) {
                const cv::Rect band_rect(
                    0,
                    band * 20,
                    badge_body_edges.cols,
                    band == 2 ? badge_body_edges.rows - band * 20 : 20);
                if (static_cast<double>(cv::countNonZero(badge_body_edges(band_rect))) /
                        static_cast<double>(band_rect.area()) >=
                    0.11) {
                    ++badge_textured_bands;
                }
            }
            if (badge_edge_density < 0.16 || badge_stddev[0] < 30.0 || badge_textured_bands < 2) {
                continue;
            }
            return true;
        }
    }
    return false;
}

bool bench_christine_sprite_visible(const cv::Mat& image, cv::Point center)
{
    if (image.type() != CV_8UC3 || image.size() != cv::Size(1280, 720) || center.y != 450 || center.x < 140 ||
        center.x > 935) {
        return false;
    }
    // Christine sits low on the bench. The normal upper-body texture gate
    // misses her, but her dark body, cyan eyes and red ribbon are visible.
    const cv::Rect body(center.x - 20, 456, 50, 35);
    cv::Mat hsv, dark, eyes, ribbon;
    cv::cvtColor(image(body), hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv(cv::Rect(0, 12, 50, 20)), cv::Scalar(0, 0, 0), cv::Scalar(180, 255, 90), dark);
    cv::inRange(hsv(cv::Rect(30, 0, 17, 17)), cv::Scalar(87, 121, 101), cv::Scalar(114, 255, 255), eyes);
    cv::inRange(hsv(cv::Rect(26, 11, 17, 20)), cv::Scalar(0, 151, 91), cv::Scalar(9, 255, 255), ribbon);
    const int dark_pixels = cv::countNonZero(dark);
    const int ribbon_pixels = cv::countNonZero(ribbon);
    if (dark_pixels >= 500 && cv::countNonZero(eyes) >= 8 && ribbon_pixels >= 26) {
        return true;
    }
    // In profile only one eye is visible. Require the solid body and a
    // stronger ribbon before looking for that lower, smaller highlight.
    if (dark_pixels < 600 || ribbon_pixels < 45) {
        return false;
    }
    cv::inRange(hsv(cv::Rect(30, 9, 16, 10)), cv::Scalar(87, 121, 101), cv::Scalar(114, 255, 255), eyes);
    return cv::countNonZero(eyes) >= 4;
}

static bool bench_tier_three_glyph(const cv::Mat& inner_hsv)
{
    cv::Mat cyan, labels, stats, centroids;
    cv::inRange(inner_hsv, cv::Scalar(73, 120, 125), cv::Scalar(111, 255, 255), cyan);
    const int components = cv::connectedComponentsWithStats(cyan, labels, stats, centroids);
    int strokes = 0;
    for (int index = 1; index < components; ++index) {
        const int width = stats.at<int>(index, cv::CC_STAT_WIDTH);
        const int height = stats.at<int>(index, cv::CC_STAT_HEIGHT);
        const int area = stats.at<int>(index, cv::CC_STAT_AREA);
        if (width >= 2 && width * 3 <= height && height * 5 >= inner_hsv.rows * 3 && area * 2 >= width * height) {
            ++strokes;
        }
    }
    return strokes == 3;
}

std::vector<cv::Point> bench_source_centres(const cv::Mat& image, bool include_christine)
{
    std::vector<cv::Point> result;
    if (image.empty() || image.type() != CV_8UC3 || image.cols != 1280 || image.rows != 720) {
        return result;
    }
    // The bench is right-aligned, but once it contains many cards the first
    // occupied slot can be close to x=150. Scanning only x>=360 silently
    // dropped those cards and made the controller skip the wrong slot.
    const cv::Rect search(140, 360, 900, 85);
    cv::Mat hsv, dark;
    cv::cvtColor(image(search), hsv, cv::COLOR_BGR2HSV);
    // Tier badges are dark charcoal with a bright outline; reject the very
    // dark map background separately by requiring a compact connected shape.
    // A 110 value cut merges the left cards' dark badge with the sprite shadow
    // on some maps.  The 100 cut keeps the compact Roman-numeral badge while
    // preserving the bright-outline/body gates below.
    cv::inRange(hsv, cv::Scalar(0, 0, 0), cv::Scalar(180, 255, 100), dark);
    cv::morphologyEx(dark, dark, cv::MORPH_OPEN, cv::Mat::ones(2, 2, CV_8UC1));
    cv::Mat labels, stats, centroids;
    const int count = cv::connectedComponentsWithStats(dark, labels, stats, centroids, 8, CV_32S);
    std::vector<cv::Rect> candidates;
    for (int index = 1; index < count; ++index) {
        const int x = stats.at<int>(index, cv::CC_STAT_LEFT);
        const int y = stats.at<int>(index, cv::CC_STAT_TOP);
        const int width = stats.at<int>(index, cv::CC_STAT_WIDTH);
        const int height = stats.at<int>(index, cv::CC_STAT_HEIGHT);
        const int area = stats.at<int>(index, cv::CC_STAT_AREA);
        const double aspect = static_cast<double>(width) / std::max(height, 1);
        // The actual tier badge sits in the upper half of the bench strip
        // (roughly y=384..410 on a 720p frame).  Components below it are
        // map props/feet and must not become fake bench slots.
        if (x < 0 || y < 12 || y >= 40 || width < 18 || width > 38 || height < 18 || height > 42 || area < 220 ||
            aspect < 0.65 || aspect > 1.5) {
            continue;
        }
        // A tier badge is a dark, compact tile with a brighter outline. The
        // same-size dark blobs occur on map props and empty-slot borders, but
        // those have a much lighter interior or no outline. Check the badge
        // itself before probing the card so those pixels cannot become fake
        // bench occupants.
        const cv::Rect badge_abs(search.x + x, search.y + y, width, height);
        const cv::Rect badge_inner(badge_abs.x + 3, badge_abs.y + 3, width - 6, height - 6);
        const cv::Rect badge_outer(badge_abs.x - 2, badge_abs.y - 2, width + 4, height + 4);
        const cv::Rect bounds(0, 0, image.cols, image.rows);
        if ((badge_inner & bounds) != badge_inner || (badge_outer & bounds) != badge_outer) {
            continue;
        }
        cv::Mat badge_gray;
        cv::cvtColor(image(badge_abs), badge_gray, cv::COLOR_BGR2GRAY);
        const cv::Mat inner_gray = badge_gray(cv::Rect(3, 3, width - 6, height - 6));
        cv::Mat outer_gray;
        cv::cvtColor(image(badge_outer), outer_gray, cv::COLOR_BGR2GRAY);
        const double inner_dark =
            static_cast<double>(cv::countNonZero(inner_gray < 110)) / static_cast<double>(inner_gray.total());
        const double outline_bright =
            static_cast<double>(cv::countNonZero(outer_gray > 125)) / static_cast<double>(outer_gray.total());
        // A bright, wide III can lower the dark fraction even on an intact
        // badge. Require its three turquoise strokes if darkness alone is
        // marginal. The connected shape, bright outline and sprite still
        // have to pass before any panel probe.
        if (outline_bright < 0.12 ||
            (inner_dark < 0.60 &&
             (inner_dark < 0.55 || !bench_tier_three_glyph(hsv(cv::Rect(x + 3, y + 3, width - 6, height - 6)))))) {
            continue;
        }
        // The source click point is below/right of the badge.  Use a stable
        // y=450 centre and deduplicate adjacent components from the same card.
        const cv::Point centre(search.x + x + 42, 450);
        if (!bench_operator_visible(image, centre) &&
            !(include_christine && bench_christine_sprite_visible(image, centre))) {
            continue;
        }
        candidates.emplace_back(badge_abs);
    }
    // Sprite shadows can make a second dark component 27 pixels from the
    // actual badge. Prefer the compact badge shape before suppressing nearby
    // components; neighbouring bench slots are about 81 pixels apart.
    std::stable_sort(candidates.begin(), candidates.end(), [](const cv::Rect& left, const cv::Rect& right) {
        return std::abs(left.width - 26) + std::abs(left.height - 28) <
               std::abs(right.width - 26) + std::abs(right.height - 28);
    });
    std::vector<cv::Rect> badges;
    for (const auto& badge : candidates) {
        if (std::none_of(badges.begin(), badges.end(), [&](const cv::Rect& prior) {
                return std::abs(prior.x - badge.x) < 40;
            })) {
            badges.emplace_back(badge);
        }
    }
    std::sort(badges.begin(), badges.end(), [](const cv::Rect& left, const cv::Rect& right) {
        return left.x < right.x;
    });
    for (const auto& badge : badges) {
        result.emplace_back(badge.x + 42, 450);
    }
    // A sprite's shadow can join a real badge to the bench frame. One
    // measured adjacent pair can establish the pitch when other badges
    // bound a separate gap of two or three slots at the same height.
    // Longer gaps need another independently measured adjacent interval.
    // The missing slot still needs its own badge and sprite evidence.
    std::vector<int> intervals;
    for (size_t index = 1; index < badges.size(); ++index) {
        const int distance = badges[index].x - badges[index - 1].x;
        if (distance >= 65 && distance <= 100 && std::abs(badges[index].y - badges[index - 1].y) <= 4) {
            intervals.emplace_back(distance);
        }
    }
    if (!intervals.empty()) {
        std::sort(intervals.begin(), intervals.end());
        const int pitch = intervals[intervals.size() / 2];
        const bool consistent = std::all_of(intervals.begin(), intervals.end(), [&](int interval) {
            return std::abs(interval - pitch) <= 4;
        });
        const int maximum_gap_steps = intervals.size() >= 2 ? 9 : 3;
        for (size_t index = 1; consistent && index < badges.size(); ++index) {
            const auto& left = badges[index - 1];
            const auto& right = badges[index];
            const int distance = right.x - left.x;
            const int steps = cvRound(static_cast<double>(distance) / pitch);
            if (steps < 2 || steps > maximum_gap_steps || std::abs(distance - steps * pitch) > 5 ||
                std::abs(left.y - right.y) > 4) {
                continue;
            }
            for (int step = 1; step < steps; ++step) {
                const int x = left.x + cvRound(static_cast<double>(distance * step) / steps);
                const cv::Point center(x + 42, 450);
                if (!bench_operator_visible(image, center)) {
                    continue;
                }
                const cv::Rect badge(
                    x,
                    (left.y + right.y) / 2,
                    (left.width + right.width) / 2,
                    (left.height + right.height) / 2);
                const cv::Rect outer(badge.x - 2, badge.y - 2, badge.width + 4, badge.height + 4);
                if ((outer & cv::Rect(0, 0, image.cols, image.rows)) != outer) {
                    continue;
                }
                cv::Mat badge_gray, outer_gray;
                cv::cvtColor(image(badge), badge_gray, cv::COLOR_BGR2GRAY);
                cv::cvtColor(image(outer), outer_gray, cv::COLOR_BGR2GRAY);
                const auto inner = badge_gray(cv::Rect(3, 3, badge.width - 6, badge.height - 6));
                const double inner_dark = static_cast<double>(cv::countNonZero(inner < 110)) / inner.total();
                const double glyph_bright = static_cast<double>(cv::countNonZero(inner > 150)) / inner.total();
                const double outline_bright =
                    static_cast<double>(cv::countNonZero(outer_gray > 125)) / outer_gray.total();
                if (inner_dark >= 0.60 && glyph_bright >= 0.015 && glyph_bright <= 0.35 && outline_bright >= 0.12) {
                    result.emplace_back(center);
                }
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const cv::Point& left, const cv::Point& right) {
        return left.x < right.x;
    });
    return result;
}

bool shop_book_one_glyph_matches(const cv::Mat& glyph, const cv::Mat& reference)
{
    if (glyph.size() != cv::Size(48, 50) || reference.size() != glyph.size() || glyph.type() != CV_8UC3 ||
        reference.type() != CV_8UC3) {
        return false;
    }
    cv::Mat actual, expected;
    cv::inRange(glyph, cv::Scalar(0, 0, 0), cv::Scalar(100, 100, 100), actual);
    cv::inRange(reference, cv::Scalar(0, 0, 0), cv::Scalar(100, 100, 100), expected);
    // LEVEL's dark frame crosses the top of this anchored crop. Its width
    // changes with the card background; the complete numeral starts below it.
    // Keep the full numeral width so a stroke of II/VI cannot pass as I.
    actual.rowRange(0, 6).setTo(0);
    expected.rowRange(0, 6).setTo(0);
    if (cv::countNonZero(expected) < 40 || cv::countNonZero(actual) < 40) {
        return false;
    }
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            cv::Mat shifted, intersection, combined;
            const cv::Mat transform = (cv::Mat_<double>(2, 3) << 1, 0, dx, 0, 1, dy);
            cv::warpAffine(expected, shifted, transform, expected.size(), cv::INTER_NEAREST);
            cv::bitwise_and(actual, shifted, intersection);
            cv::bitwise_or(actual, shifted, combined);
            if (cv::countNonZero(intersection) >= .90 * cv::countNonZero(combined)) {
                return true;
            }
        }
    }
    return false;
}

// The red cancel button can hide the upper vertex of the white direction
// diamond. Its bounding-box midpoint is then below the actual diagonals.
// Reconstruct all four edges before using that fallback as a board anchor.
static std::optional<cv::Point> direction_edge_center(const cv::Mat& mask, cv::Rect envelope)
{
    const cv::Point2d anchor(envelope.x + envelope.width / 2, envelope.y + envelope.height / 2);
    const auto bounds = (envelope + cv::Size(40, 40) - cv::Point(20, 20)) & cv::Rect(0, 0, mask.cols, mask.rows);
    cv::Mat restricted = cv::Mat::zeros(mask.size(), CV_8UC1);
    mask(bounds).copyTo(restricted(bounds));
    std::vector<cv::Vec4i> lines;
    cv::HoughLinesP(restricted, lines, 1, CV_PI / 720, 60, 100, 12);
    std::array<std::optional<cv::Vec4i>, 4> edges;
    std::array<double, 4> lengths {};
    for (const auto& line : lines) {
        const cv::Point2d start(line[0], line[1]), end(line[2], line[3]);
        const auto delta = end - start;
        if (std::abs(delta.x) < 1) {
            continue;
        }
        const auto slope = delta.y / delta.x;
        const auto midpoint = (start + end) * .5;
        if (std::abs(slope) < .35 || std::abs(slope) > 1.6) {
            continue;
        }
        const int side = midpoint.y < anchor.y
                             ? (midpoint.x < anchor.x && slope < 0 ? 0 : (midpoint.x > anchor.x && slope > 0 ? 1 : -1))
                             : (midpoint.x < anchor.x && slope > 0 ? 2 : (midpoint.x > anchor.x && slope < 0 ? 3 : -1));
        if (side >= 0 && delta.dot(delta) > lengths[side]) {
            edges[side] = line;
            lengths[side] = delta.dot(delta);
        }
    }
    if (std::any_of(edges.begin(), edges.end(), [](const auto& edge) { return !edge; })) {
        return std::nullopt;
    }
    const auto intersection = [](cv::Vec4d left, cv::Vec4d right) -> std::optional<cv::Point2d> {
        const cv::Point2d a(left[0], left[1]), b(right[0], right[1]);
        const cv::Point2d u(left[2] - left[0], left[3] - left[1]), v(right[2] - right[0], right[3] - right[1]);
        const auto cross = u.x * v.y - u.y * v.x;
        if (std::abs(cross) < 1) {
            return std::nullopt;
        }
        const auto delta = b - a;
        return a + ((delta.x * v.y - delta.y * v.x) / cross) * u;
    };
    const auto left = intersection(*edges[0], *edges[2]), top = intersection(*edges[0], *edges[1]),
               right = intersection(*edges[1], *edges[3]), bottom = intersection(*edges[2], *edges[3]);
    if (!left || !top || !right || !bottom || left->x >= top->x || top->x >= right->x || left->x >= bottom->x ||
        bottom->x >= right->x || top->y >= left->y || left->y >= bottom->y || top->y >= right->y ||
        right->y >= bottom->y || right->x - left->x < 250 || right->x - left->x > 600 || bottom->y - top->y < 180 ||
        bottom->y - top->y > 550) {
        return std::nullopt;
    }
    const std::array<cv::Point2d, 4> corners { *left, *top, *right, *bottom };
    for (std::size_t side = 0; side < corners.size(); ++side) {
        int support = 0;
        for (int sample = 0; sample <= 50; ++sample) {
            const auto point = corners[side] + (corners[(side + 1) % 4] - corners[side]) * (sample / 50.);
            const cv::Rect patch(cvRound(point.x) - 2, cvRound(point.y) - 2, 5, 5);
            if ((patch & cv::Rect(0, 0, mask.cols, mask.rows)) == patch && cv::countNonZero(mask(patch)) > 0) {
                ++support;
            }
        }
        if (support < 32) {
            return std::nullopt;
        }
    }
    const auto center =
        intersection(cv::Vec4d(left->x, left->y, right->x, right->y), cv::Vec4d(top->x, top->y, bottom->x, bottom->y));
    if (!center || cv::norm(*center - anchor) > 55) {
        return std::nullopt;
    }
    return cv::Point(cvRound(center->x), cvRound(center->y));
}

static cv::Mat occupied_preview_integral(const cv::Mat& image)
{
    if (image.empty() || image.type() != CV_8UC3 || image.size() != cv::Size(1280, 720)) {
        return {};
    }
    cv::Mat hsv, mask, integral;
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(35, 80, 90), cv::Scalar(85, 255, 255), mask);
    mask /= 255;
    cv::integral(mask, integral, CV_32S);
    return integral;
}

static int occupied_preview_votes(const cv::Mat& integral, cv::Point center)
{
    if (integral.empty() || center.x < 340 || center.x >= 1000 || center.y < 170 || center.y >= 490) {
        return 0;
    }
    // The body obscures the tile centre. Require separated green perimeter
    // patches so a single neighboring tile edge cannot authorize an exchange.
    int votes = 0;
    for (const auto offset : { cv::Point(-28, -23),
                               cv::Point(0, -27),
                               cv::Point(28, -23),
                               cv::Point(-32, 0),
                               cv::Point(32, 0),
                               cv::Point(-28, 23),
                               cv::Point(0, 27),
                               cv::Point(28, 23) }) {
        const auto point = center + offset;
        const int x = point.x - 4, y = point.y - 4;
        const int count = integral.at<int>(y + 9, x + 9) - integral.at<int>(y, x + 9) - integral.at<int>(y + 9, x) +
                          integral.at<int>(y, x);
        votes += count >= 49 ? 1 : 0;
    }
    return votes;
}
}

std::optional<int> asst::spa::battle_boss_hp_bar_fill_percent(const cv::Mat& image)
{
    if (image.type() != CV_8UC3 || image.size() != cv::Size(1280, 720)) {
        return std::nullopt;
    }
    const auto pink = [](const cv::Vec3b& pixel, int min_red) {
        const auto [blue, green, red] = std::array { pixel[0], pixel[1], pixel[2] };
        return red >= min_red && red - green >= 58 && blue - green >= 18 && red - blue >= 40;
    };
    // The battle HUD has a pink right edge and footer. The ordinary
    // enemy counter is gold and the preparation bar is on the battlefield.
    int right_edge = 0, footer = 0;
    for (int y = 13; y < 52; ++y) {
        right_edge += pink(image.at<cv::Vec3b>(y, 776), 105);
    }
    for (int x = 598; x < 769; ++x) {
        footer += pink(image.at<cv::Vec3b>(56, x), 105);
    }
    if (right_edge < 28 || footer < 135) {
        return std::nullopt;
    }
    constexpr int left = 597, width = 170;
    std::array<bool, width> dark {};
    int pink_columns = 0;
    for (int i = 0; i < width; ++i) {
        int dim_pixels = 0, pink_pixels = 0;
        for (int y = 30; y < 34; ++y) {
            const auto& pixel = image.at<cv::Vec3b>(y, left + i);
            dim_pixels += pixel[2] < 75 && pixel[1] < 45 && pixel[0] < 55;
            pink_pixels += pink(pixel, 145);
        }
        dark[i] = dim_pixels >= 3;
        pink_columns += pink_pixels >= 2;
    }
    // Fit one filled prefix followed by the empty dark track. This keeps
    // the cyan endpoint and allows small gaps from animated highlights.
    int errors = static_cast<int>(std::ranges::count(dark, false));
    int best_errors = errors, filled = 0;
    for (int i = 0; i < width; ++i) {
        errors += dark[i] ? 1 : -1;
        if (errors < best_errors) {
            best_errors = errors;
            filled = i + 1;
        }
    }
    if (filled < 8 || best_errors > std::min(4, filled / 5) || pink_columns < std::max(3, filled / 5)) {
        return std::nullopt;
    }
    return (filled * 100 + width / 2) / width;
}

std::optional<cv::Rect> asst::spa::preparation_boss_health_bar(const cv::Mat& image)
{
    if (image.empty() || image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3) {
        return std::nullopt;
    }
    const cv::Rect region(0, 150, image.cols, 340);
    cv::Mat hsv, red, wrap, labels, stats, centers;
    cv::cvtColor(image(region), hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(0, 120, 110), cv::Scalar(10, 255, 255), red);
    cv::inRange(hsv, cv::Scalar(170, 120, 110), cv::Scalar(180, 255, 255), wrap);
    red |= wrap;
    // The leader glyph can interrupt the fill with a narrow gap. Reconnect
    // only that small horizontal gap before requiring a long, thin bar.
    // The full width preserves an unclipped bar and its possible mirror.
    cv::morphologyEx(red, red, cv::MORPH_CLOSE, cv::Mat::ones(1, 9, CV_8UC1));
    cv::morphologyEx(red, red, cv::MORPH_OPEN, cv::Mat::ones(4, 120, CV_8UC1));
    const int count = cv::connectedComponentsWithStats(red, labels, stats, centers, 8);
    std::optional<cv::Rect> result;
    for (int index = 1; index < count; ++index) {
        const int width = stats.at<int>(index, cv::CC_STAT_WIDTH), height = stats.at<int>(index, cv::CC_STAT_HEIGHT);
        // Some bosses use a second, much wider bar. Its full span is visible
        // near the screen edge in the expanded preparation camera.
        const bool standard = width >= 140 && width <= 230;
        const bool wide = width >= 370 && width <= 430;
        if ((!standard && !wide) || height < 4 || height > 12 || width < height * 15) {
            continue;
        }
        if (result) {
            return std::nullopt;
        }
        result = cv::Rect(
            region.x + stats.at<int>(index, cv::CC_STAT_LEFT),
            region.y + stats.at<int>(index, cv::CC_STAT_TOP),
            width,
            height);
    }
    return result;
}

std::optional<cv::Mat> asst::spa::boss_attackable_region(const cv::Mat& image, cv::Rect boss_bar)
{
    if (image.type() != CV_8UC3 || image.size() != cv::Size(1280, 720) || boss_bar.width < 140 || boss_bar.height < 4 ||
        (boss_bar & cv::Rect(0, 0, 1280, 720)) != boss_bar || boss_bar.y <= 210 || boss_bar.y >= 495) {
        return std::nullopt;
    }
    cv::Mat hsv, red, wrap, closed, labels, stats, centers;
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(0, 110, 150), cv::Scalar(10, 255, 255), red);
    cv::inRange(hsv, cv::Scalar(170, 110, 150), cv::Scalar(180, 255, 255), wrap);
    red |= wrap;
    const cv::Rect roi = cv::Rect(
        std::max(180, boss_bar.x - 220),
        150,
        std::min(1270, boss_bar.x + boss_bar.width + 220) - std::max(180, boss_bar.x - 220),
        boss_bar.y - 160);
    cv::Mat visible = cv::Mat::zeros(image.size(), CV_8UC1);
    red(roi).copyTo(visible(roi));
    cv::morphologyEx(visible, closed, cv::MORPH_CLOSE, cv::Mat::ones(19, 19, CV_8UC1));
    const int count = cv::connectedComponentsWithStats(closed, labels, stats, centers, 8);
    cv::Mat result = cv::Mat::zeros(image.size(), CV_8UC1);
    for (int index = 1; index < count; ++index) {
        const cv::Rect bounds(
            stats.at<int>(index, cv::CC_STAT_LEFT),
            stats.at<int>(index, cv::CC_STAT_TOP),
            stats.at<int>(index, cv::CC_STAT_WIDTH),
            stats.at<int>(index, cv::CC_STAT_HEIGHT));
        const bool compact = bounds.width <= 140 && bounds.height <= 120;
        if (bounds.width < 70 || bounds.height < 60 ||
            stats.at<int>(index, cv::CC_STAT_AREA) < (compact ? 3000 : 6000)) {
            continue;
        }
        cv::Mat component;
        cv::compare(labels(bounds), index, component, cv::CMP_EQ);
        component &= visible(bounds);
        if (cv::countNonZero(component) < (compact ? 1500 : 3000)) {
            continue;
        }
        cv::Mat edges;
        cv::Canny(component, edges, 50, 100);
        std::vector<cv::Vec4i> lines;
        cv::HoughLinesP(edges, lines, 1, CV_PI / 180, 25, 25, 8);
        int positive = 0, negative = 0;
        std::vector<double> positive_bands, negative_bands;
        for (const auto& line : lines) {
            const double dx = line[2] - line[0], dy = line[3] - line[1];
            if (std::abs(dx) > 10 && std::abs(dy / dx) >= .6 && std::abs(dy / dx) <= 1.8) {
                positive += dy / dx > 0;
                negative += dy / dx < 0;
                auto& bands = dy / dx > 0 ? positive_bands : negative_bands;
                bands.push_back(
                    dy / dx > 0 ? (line[1] + line[3] - line[0] - line[2]) * .5
                                : (line[1] + line[3] + line[0] + line[2]) * .5);
            }
        }
        const auto separated_bands = [](std::vector<double> bands) {
            std::sort(bands.begin(), bands.end());
            int count = 0;
            double last = -1e9;
            for (const double band : bands) {
                if (band - last >= 8) {
                    ++count;
                    last = band;
                }
            }
            return count;
        };
        if (std::max(positive, negative) < 4 ||
            std::max(separated_bands(positive_bands), separated_bands(negative_bands)) < 3) {
            continue;
        }
        result(bounds) |= component;
    }
    if (cv::countNonZero(result) == 0) {
        return std::nullopt;
    }
    // The direction selector's red cancel stripe has diagonal bands too.
    // Its position follows the selected tile, not the boss.
    if (const auto center = direction_selection_center(image); center && direction_cancel_target(image, *center)) {
        return std::nullopt;
    }
    return result;
}

std::vector<std::pair<int, int>>
    asst::spa::project_boss_attackable_cells(const cv::Mat& region, const cv::Mat& grid_to_screen)
{
    std::vector<std::pair<int, int>> result;
    if (region.type() != CV_8UC1 || region.size() != cv::Size(1280, 720) || grid_to_screen.type() != CV_64FC1 ||
        grid_to_screen.rows != 3 || grid_to_screen.cols != 3 || !cv::checkRange(grid_to_screen) ||
        std::abs(cv::determinant(grid_to_screen)) < 1e-8) {
        return result;
    }
    for (int row = 0; row <= 4; ++row) {
        for (int col = 0; col <= 12; ++col) {
            std::vector<cv::Point2f> corners;
            cv::perspectiveTransform(
                std::vector<cv::Point2f> { { col - .48F, row - .48F },
                                           { col + .48F, row - .48F },
                                           { col + .48F, row + .48F },
                                           { col - .48F, row + .48F } },
                corners,
                grid_to_screen);
            if (std::any_of(corners.begin(), corners.end(), [](const auto& p) {
                    return !std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 5000 || std::abs(p.y) > 5000;
                })) {
                return {};
            }
            std::vector<cv::Point> polygon;
            for (const auto& p : corners) {
                polygon.emplace_back(cvRound(p.x), cvRound(p.y));
            }
            cv::Mat cell = cv::Mat::zeros(region.size(), CV_8UC1);
            cv::fillConvexPoly(cell, polygon, cv::Scalar(255));
            cell &= region;
            if (cv::countNonZero(cell) >= 20) {
                result.emplace_back(col, row);
            }
        }
    }
    return result;
}

std::vector<std::pair<int, int>>
    asst::spa::estimate_boss_opening_footprint(const cv::Mat& image, cv::Rect boss_bar, const cv::Mat& grid_to_screen)
{
    if (image.type() != CV_8UC3 || image.size() != cv::Size(1280, 720) || boss_bar.width < 140 ||
        boss_bar.width > 230 || boss_bar.height < 4 || boss_bar.height > 12 ||
        (boss_bar & cv::Rect(0, 0, 1280, 720)) != boss_bar || boss_bar.y < 150 || boss_bar.y > 480 ||
        grid_to_screen.size() != cv::Size(3, 3) || grid_to_screen.type() != CV_64FC1 ||
        !cv::checkRange(grid_to_screen) || std::abs(cv::determinant(grid_to_screen)) < 1e-8) {
        return {};
    }
    const cv::Rect below(boss_bar.x, boss_bar.br().y + 18, boss_bar.width, boss_bar.width * 3 / 8);
    cv::Mat hsv, red, wrap;
    cv::cvtColor(image(below), hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(0, 120, 110), cv::Scalar(10, 255, 255), red);
    cv::inRange(hsv, cv::Scalar(170, 120, 110), cv::Scalar(180, 255, 255), wrap);
    red |= wrap;

    int boundary_row = -1, widest = 0;
    for (int row = 0; row + 8 <= below.height; row += 4) {
        cv::Mat columns;
        cv::reduce(red.rowRange(row, row + 8), columns, 0, cv::REDUCE_MAX);
        const int third = below.width / 3;
        const int left = cv::countNonZero(columns.colRange(0, third));
        const int middle = cv::countNonZero(columns.colRange(third, third * 2));
        const int right = cv::countNonZero(columns.colRange(third * 2, below.width));
        if (std::min({ left, middle, right }) >= third / 7 && left + middle + right > widest) {
            boundary_row = below.y + row + 4;
            widest = left + middle + right;
        }
    }
    if (boundary_row < 0 || widest * 3 < below.width) {
        return {};
    }
    // Sample just inside the boundary. Inverting the registered grid handles
    // perspective and the opposite player's mirrored board in the same way.
    std::vector<cv::Point2f> mapped;
    cv::perspectiveTransform(
        std::vector<cv::Point2f> { { boss_bar.x + boss_bar.width * .5F, boundary_row - boss_bar.width * .08F } },
        mapped,
        grid_to_screen.inv());
    if (!std::isfinite(mapped.front().x) || !std::isfinite(mapped.front().y) || mapped.front().x < 1.5F ||
        mapped.front().x >= 10.5F || mapped.front().y < 1.5F || mapped.front().y >= 4.5F) {
        return {};
    }
    const int center_col = cvRound(mapped.front().x), bottom_row = cvRound(mapped.front().y);
    std::vector<std::pair<int, int>> cells;
    for (int row = bottom_row - 2; row <= bottom_row; ++row) {
        for (int col = center_col - 2; col <= center_col + 2; ++col) {
            cells.emplace_back(col, row);
        }
    }
    return cells;
}

bool asst::spa::occupied_swap_hint(const cv::Mat& image, cv::Point center)
{
    return occupied_preview_votes(occupied_preview_integral(image), center) >= 3;
}

std::optional<bool> asst::spa::pact_icon_available(const cv::Mat& image, const cv::Rect& label, bool core)
{
    if (image.empty() || image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3 || label.x < 0 ||
        label.x + label.width >= 900 || label.width <= 0) {
        return std::nullopt;
    }
    const cv::Point center(label.x + label.width / 2, label.y - (core ? 50 : 34));
    const cv::Rect bounds(center.x - 24, center.y - 24, 48, 48);
    if (bounds.x < 0 || bounds.y < 175 || bounds.x + bounds.width >= image.cols || bounds.y + bounds.height >= 710) {
        return std::nullopt;
    }
    cv::Mat hsv, green, neutral;
    cv::cvtColor(image(bounds), hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(35, 75, 70), cv::Scalar(100, 255, 255), green);
    if (cv::countNonZero(green) >= 220) {
        return true;
    }
    // Unselected/inactive icons retain a gray/white glyph and backing.
    // Color alone cannot distinguish an alternative core group from a ban.
    // Sampling an empty region after a pan must remain unknown.
    cv::inRange(hsv, cv::Scalar(0, 0, 80), cv::Scalar(180, 65, 255), neutral);
    if (cv::countNonZero(neutral) >= 160 && cv::countNonZero(green) < 80) {
        return false;
    }
    return std::nullopt;
}

bool asst::spa::overlaps_expanded_shop_controls(cv::Point point, int top_margin)
{
    // Expanded-shop controls begin above the nominal bench sprite foot. Keep
    // a margin around their left edge: trial2's (963, 466) title probe toggled
    // freeze even though the operator badge above it was a valid component.
    return cv::Rect(950, 430 - top_margin, 330, 80 + top_margin).contains(point);
}

bool asst::spa::equipment_inventory_probe_safe(cv::Point point)
{
    return point.x >= 140 && point.x < 1000 && point.y >= 380 && point.y < 510 &&
           !overlaps_expanded_shop_controls(point, 10);
}

std::vector<cv::Point> asst::spa::equipment_inventory_candidates(const cv::Mat& image)
{
    std::vector<cv::Point> result;
    if (image.empty() || image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3) {
        return result;
    }
    const cv::Rect search(140, 300, 900, 145);
    cv::Mat hsv, dark, labels, stats, centroids;
    // Inventory can occupy the raised row or the main bench row. Scan whole
    // badges before filtering their row; clipping the search at y=362 turned
    // scenery into compact components and missed lower-row items entirely.
    cv::cvtColor(image(search), hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(0, 0, 0), cv::Scalar(180, 255, 100), dark);
    cv::morphologyEx(dark, dark, cv::MORPH_OPEN, cv::Mat::ones(2, 2, CV_8UC1));
    const int count = cv::connectedComponentsWithStats(dark, labels, stats, centroids, 8, CV_32S);
    for (int index = 1; index < count; ++index) {
        const int x = stats.at<int>(index, cv::CC_STAT_LEFT), y = stats.at<int>(index, cv::CC_STAT_TOP);
        const int width = stats.at<int>(index, cv::CC_STAT_WIDTH), height = stats.at<int>(index, cv::CC_STAT_HEIGHT);
        const int area = stats.at<int>(index, cv::CC_STAT_AREA);
        const bool inventory_row = (y >= 8 && y <= 38) || (y >= 72 && y <= 110);
        const double aspect = static_cast<double>(width) / std::max(height, 1);
        if (!inventory_row || width < 20 || width > 32 || height < 22 || height > 32 || area < 220 || aspect < 0.8 ||
            aspect > 1.25) {
            continue;
        }
        const cv::Rect badge(search.x + x, search.y + y, width, height);
        cv::Mat inner, outer;
        cv::cvtColor(image(cv::Rect(badge.x + 3, badge.y + 3, width - 6, height - 6)), inner, cv::COLOR_BGR2GRAY);
        cv::cvtColor(image(cv::Rect(badge.x - 2, badge.y - 2, width + 4, height + 4)), outer, cv::COLOR_BGR2GRAY);
        if (cv::countNonZero(inner < 110) < inner.total() * 0.55 ||
            cv::countNonZero(outer > 125) < outer.total() * 0.12) {
            continue;
        }
        if (y <= 38) {
            // The raised inventory row overlaps battle scenery. A dark
            // square there is a candidate only if it also has the bright
            // rank strokes carried by the equipment badge.
            const cv::Rect rank_glyph(x + 3, y + 3, 17, 16);
            cv::Mat brightness;
            cv::extractChannel(hsv(rank_glyph), brightness, 2);
            if (cv::countNonZero(brightness > 145) < 13) {
                continue;
            }
        }
        const cv::Point source(badge.x + 42, badge.y + 80);
        if (!equipment_inventory_probe_safe(source)) {
            continue;
        }
        cv::Mat body, edges;
        cv::cvtColor(image(cv::Rect(source.x - 24, source.y - 54, 48, 54)), body, cv::COLOR_BGR2GRAY);
        cv::Canny(body, edges, 60, 120);
        if (cv::countNonZero(edges) < 110 ||
            std::any_of(result.begin(), result.end(), [&](auto prior) { return cv::norm(prior - source) < 24; })) {
            continue;
        }
        result.push_back(source);
    }
    // The raised row is searched first. Lower-row candidates may be operators;
    // only the caller's actual title check can authorize equipment contact.
    std::sort(result.begin(), result.end(), [](auto a, auto b) {
        return a.y / 40 != b.y / 40 ? a.y < b.y : a.x < b.x;
    });
    return result;
}

std::optional<cv::Point>
    asst::spa::added_equipment_contact_hint(const std::vector<cv::Point>& before, const std::vector<cv::Point>& after)
{
    if (after.size() != before.size() + 1 || after.size() > 20) {
        return std::nullopt;
    }
    const auto added = std::find_if(after.begin(), after.end(), [&](const auto& point) {
        return equipment_inventory_probe_safe(point) && inventory_slot_removal_observed(after, before, point);
    });
    return added == after.end() ? std::nullopt : std::optional(*added);
}

std::optional<asst::spa::RetreatControlMatch>
    asst::spa::selected_retreat_control(const cv::Mat& image, const cv::Mat& reference)
{
    if (image.empty() || image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3 || reference.empty() ||
        reference.type() != CV_8UC3 || reference.cols < 20 || reference.cols > 80 || reference.rows < 20 ||
        reference.rows > 90) {
        return std::nullopt;
    }
    const cv::Rect roi(400, 150, 650, 500);
    std::vector<RetreatControlMatch> matches;
    for (const double scale : { .95, .975, 1.0, 1.025, 1.05, 1.075, 1.1, 1.125 }) {
        cv::Mat scaled, scores;
        cv::resize(reference, scaled, cv::Size(), scale, scale);
        cv::matchTemplate(image(roi), scaled, scores, cv::TM_CCOEFF_NORMED);
        double score = 0;
        cv::Point location;
        cv::minMaxLoc(scores, nullptr, &score, nullptr, &location);
        while (std::isfinite(score) && score >= .92) {
            const cv::Rect rect(roi.tl() + location, scaled.size());
            matches.push_back({ rect, score, scale });
            // Suppress neighboring peaks of the same glyph, not other buttons.
            const auto nearby =
                cv::Rect(location.x - 12, location.y - 12, 25, 25) & cv::Rect(0, 0, scores.cols, scores.rows);
            scores(nearby).setTo(-1);
            cv::minMaxLoc(scores, nullptr, &score, nullptr, &location);
            if (matches.size() > 32) {
                return std::nullopt;
            }
        }
    }
    if (matches.empty()) {
        return std::nullopt;
    }
    const auto best = std::max_element(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
        return a.score < b.score;
    });
    const auto center = best->rect.tl() + cv::Point(best->rect.width / 2, best->rect.height / 2);
    if (std::any_of(matches.begin(), matches.end(), [&](const auto& match) {
            const auto other = match.rect.tl() + cv::Point(match.rect.width / 2, match.rect.height / 2);
            return cv::norm(other - center) > 18;
        })) {
        return std::nullopt;
    }
    return *best;
}

asst::spa::OccupiedPreviewRegistration
    asst::spa::align_occupied_preview(const cv::Mat& image, const std::vector<cv::Point>& tracked)
{
    OccupiedPreviewRegistration result;
    const auto integral = occupied_preview_integral(image);
    if (integral.empty()) {
        return result;
    }
    std::vector<cv::Point> distinct;
    for (const auto point : tracked) {
        if (point.x >= 360 && point.x < 980 && point.y >= 185 && point.y < 475 &&
            std::none_of(
                distinct.begin(),
                distinct.end(),
                [&](const auto previous) { return cv::norm(point - previous) < 45; })) {
            distinct.push_back(point);
        }
    }
    result.total_cells = static_cast<int>(distinct.size());
    if (result.total_cells < 3 || result.total_cells > 8) {
        return result;
    }
    for (int dy = -12; dy <= 12; dy += 2) {
        for (int dx = -12; dx <= 12; dx += 2) {
            const cv::Point shift(dx, dy);
            const int matched = static_cast<int>(std::count_if(distinct.begin(), distinct.end(), [&](const auto point) {
                return occupied_preview_votes(integral, point + shift) >= 3;
            }));
            if (matched > result.matched_cells ||
                (matched == result.matched_cells && shift.dot(shift) < result.shift.dot(result.shift))) {
                result.matched_cells = matched;
                result.shift = shift;
            }
        }
    }
    result.verified = result.matched_cells >= 3 && result.matched_cells * 4 >= result.total_cells * 3;
    return result;
}

asst::spa::CameraProjection
    asst::spa::estimate_camera_projection(const cv::Mat& before, const cv::Mat& selected, bool board_view)
{
    CameraProjection result;
    if (before.empty() || selected.empty() || before.size() != cv::Size(1280, 720) ||
        selected.size() != before.size()) {
        return result;
    }
    cv::Mat source_mask = cv::Mat::zeros(before.size(), CV_8UC1);
    cv::Mat target_mask = cv::Mat::zeros(selected.size(), CV_8UC1);
    // Operators on the lower deployment row sit below y=390.  Excluding
    // that band made a valid camera registration unusable for direction
    // input after the selection panel panned the board.
    source_mask(board_view ? cv::Rect(300, 135, 800, 355) : cv::Rect(300, 140, 800, 480)).setTo(255);
    target_mask(board_view ? cv::Rect(300, 135, 800, 355) : cv::Rect(420, 300, 680, 420)).setTo(255);
    const auto orb = cv::ORB::create(5000);
    std::vector<cv::KeyPoint> source_keys, target_keys;
    cv::Mat source_descriptors, target_descriptors;
    orb->detectAndCompute(before, source_mask, source_keys, source_descriptors);
    orb->detectAndCompute(selected, target_mask, target_keys, target_descriptors);
    if (source_descriptors.empty() || target_descriptors.rows < 2) {
        return result;
    }
    std::vector<std::vector<cv::DMatch>> matches;
    cv::BFMatcher(cv::NORM_HAMMING).knnMatch(source_descriptors, target_descriptors, matches, 2);
    std::vector<cv::Point2f> source, target;
    for (const auto& pair : matches) {
        if (pair.size() == 2 && pair[0].distance < 0.72F * pair[1].distance) {
            source.push_back(source_keys[pair[0].queryIdx].pt);
            target.push_back(target_keys[pair[0].trainIdx].pt);
        }
    }
    result.matches = static_cast<int>(source.size());
    if (result.matches < 16) {
        return result;
    }
    cv::Mat inlier_mask;
    result.matrix = cv::findHomography(source, target, cv::RANSAC, 4, inlier_mask);
    if (result.matrix.empty() || !cv::checkRange(result.matrix)) {
        return result;
    }
    std::vector<cv::Point2f> inlier_source;
    std::vector<float> errors;
    std::vector<cv::Point2f> projected;
    cv::perspectiveTransform(source, projected, result.matrix);
    for (size_t index = 0; index < source.size(); ++index) {
        if (inlier_mask.at<unsigned char>(static_cast<int>(index)) != 0) {
            inlier_source.push_back(source[index]);
            errors.push_back(static_cast<float>(cv::norm(projected[index] - target[index])));
        }
    }
    result.inliers = static_cast<int>(inlier_source.size());
    if (result.inliers < 12 || result.inliers < 0.45 * result.matches) {
        return result;
    }
    std::sort(errors.begin(), errors.end());
    result.median_error = errors[errors.size() / 2];
    cv::convexHull(inlier_source, result.source_hull);
    result.estimated = result.median_error <= 3 && cv::contourArea(result.source_hull) >= 3000;
    return result;
}

std::optional<cv::Point2f> asst::spa::project_supported_point(const CameraProjection& projection, cv::Point2f point)
{
    if (!projection.estimated || projection.source_hull.size() < 3 ||
        cv::pointPolygonTest(projection.source_hull, point, false) < 0) {
        return std::nullopt;
    }
    std::vector<cv::Point2f> output;
    cv::perspectiveTransform(std::vector<cv::Point2f> { point }, output, projection.matrix);
    const auto target = output.front();
    if (!std::isfinite(target.x) || !std::isfinite(target.y) || target.x < 0 || target.x >= 1280 || target.y < 0 ||
        target.y >= 720) {
        return std::nullopt;
    }
    return target;
}

std::optional<cv::Point>
    asst::spa::registered_deployment_pose(const cv::Mat& held, const cv::Mat& collapsed, cv::Point held_target)
{
    return registered_deployment_pose(estimate_camera_projection(held, collapsed, true), held_target);
}

std::optional<cv::Point>
    asst::spa::registered_deployment_pose(const CameraProjection& fit, cv::Point held_target)
{
    const auto points = registered_field_points(fit, { held_target });
    return points ? std::optional(points->front()) : std::nullopt;
}

std::optional<cv::Point> asst::spa::registered_deployment_cell_pose(
    const cv::Mat& held,
    const cv::Mat& collapsed,
    const cv::Mat& grid_to_held,
    cv::Point receiver_hint)
{
    return registered_deployment_cell_pose(estimate_camera_projection(held, collapsed, true), grid_to_held, receiver_hint);
}

std::optional<cv::Point> asst::spa::registered_deployment_cell_pose(
    const CameraProjection& projection,
    const cv::Mat& grid_to_held,
    cv::Point receiver_hint)
{
    if (grid_to_held.type() != CV_64FC1 || grid_to_held.rows != 3 || grid_to_held.cols != 3 ||
        !cv::checkRange(grid_to_held) || std::abs(cv::determinant(grid_to_held)) < 1e-8) {
        return std::nullopt;
    }
    if (!registered_field_points(projection, { receiver_hint })) {
        return std::nullopt;
    }
    std::vector<cv::Point2f> grid;
    cv::perspectiveTransform(std::vector<cv::Point2f> { cv::Point2f(receiver_hint) }, grid, grid_to_held.inv());
    if (grid.empty() || !std::isfinite(grid[0].x) || !std::isfinite(grid[0].y) || grid[0].x < -.35 ||
        grid[0].x > 10.35 || grid[0].y < -.35 || grid[0].y > 4.35) {
        return std::nullopt;
    }
    const int col = cvRound(grid[0].x), row = cvRound(grid[0].y);
    // This tolerance belongs to the approximate diamond hint only. Field
    // member mapping and supported-camera hull checks remain unchanged.
    if (std::abs(grid[0].x - col) > .35 || std::abs(grid[0].y - row) > .35) {
        return std::nullopt;
    }
    std::vector<cv::Point2f> centers;
    cv::perspectiveTransform(
        std::vector<cv::Point2f> { { static_cast<float>(col), static_cast<float>(row) } },
        centers,
        grid_to_held);
    if (centers.empty() || !std::isfinite(centers[0].x) || !std::isfinite(centers[0].y)) {
        return std::nullopt;
    }
    const auto points = registered_field_points(projection, { { cvRound(centers[0].x), cvRound(centers[0].y) } });
    return points ? std::optional(points->front()) : std::nullopt;
}

std::optional<cv::Point> asst::spa::registered_grid_receiver_pose(
    const cv::Mat& placement,
    const cv::Mat& held,
    const cv::Mat& grid_to_held,
    std::pair<int, int> expected_cell)
{
    if (expected_cell.first < 0 || expected_cell.first > 10 || expected_cell.second < 0 || expected_cell.second > 4) {
        return std::nullopt;
    }
    const auto center = direction_selection_center(placement);
    if (!center || !direction_cancel_target(placement, *center)) {
        return std::nullopt;
    }
    const auto grid_to_placement = registered_grid_projection(held, placement, grid_to_held);
    if (!grid_to_placement) {
        return std::nullopt;
    }
    std::vector<cv::Point2f> cell, floor;
    cv::perspectiveTransform(
        std::vector<cv::Point2f> { { static_cast<float>(center->x), static_cast<float>(center->y + 20) } },
        cell,
        grid_to_placement->inv());
    if (cell.size() != 1 || !std::isfinite(cell.front().x) || !std::isfinite(cell.front().y) ||
        std::abs(cell.front().x - expected_cell.first) > .35 || std::abs(cell.front().y - expected_cell.second) > .35) {
        return std::nullopt;
    }
    cv::perspectiveTransform(
        std::vector<cv::Point2f> {
            { static_cast<float>(expected_cell.first), static_cast<float>(expected_cell.second) } },
        floor,
        grid_to_held);
    if (floor.size() != 1 || !std::isfinite(floor.front().x) || !std::isfinite(floor.front().y) ||
        !cv::Rect2f(120, 100, 1060, 470).contains(floor.front())) {
        return std::nullopt;
    }
    return cv::Point(cvRound(floor.front().x), cvRound(floor.front().y));
}

std::optional<cv::Point> asst::spa::registered_green_grid_cell_pose(
    const cv::Mat& held,
    const cv::Mat& after,
    const cv::Mat& grid_to_held,
    std::pair<int, int> cell)
{
    if (cell.first < 0 || cell.first > 10 || cell.second < 0 || cell.second > 4) {
        return std::nullopt;
    }
    const auto after_grid = registered_grid_projection(held, after, grid_to_held);
    if (!after_grid) {
        return std::nullopt;
    }
    const std::vector<cv::Point2f> cells { { static_cast<float>(cell.first), static_cast<float>(cell.second) } };
    std::vector<cv::Point2f> before_point, after_point;
    cv::perspectiveTransform(cells, before_point, grid_to_held);
    cv::perspectiveTransform(cells, after_point, *after_grid);
    const auto before = before_point.front();
    const auto target = after_point.front();
    const cv::Rect2f field(180, 140, 820, 370);
    if (!std::isfinite(before.x) || !std::isfinite(before.y) || !std::isfinite(target.x) || !std::isfinite(target.y) ||
        !field.contains(target) || !green_deployment_hint(held, { cvRound(before.x), cvRound(before.y) })) {
        return std::nullopt;
    }
    return cv::Point(cvRound(target.x), cvRound(target.y));
}

static bool valid_field_camera_projection(const asst::spa::CameraProjection& fit)
{
    return fit.estimated && fit.inliers >= 30 && fit.inliers >= .65 * fit.matches && fit.median_error <= 2 &&
           fit.source_hull.size() >= 3 && cv::contourArea(fit.source_hull) >= 30'000 && fit.matrix.type() == CV_64FC1 &&
           fit.matrix.rows == 3 && fit.matrix.cols == 3 && cv::checkRange(fit.matrix);
}

std::optional<std::vector<cv::Point>> asst::spa::registered_field_points(
    const cv::Mat& before,
    const cv::Mat& after,
    const std::vector<cv::Point>& points,
    FieldView view)
{
    if (points.empty() || points.size() > 8) {
        return std::nullopt;
    }
    return registered_field_points(estimate_camera_projection(before, after, true), points, view);
}

std::optional<std::vector<cv::Point>> asst::spa::registered_field_points(
    const CameraProjection& fit,
    const std::vector<cv::Point>& points,
    FieldView view)
{
    if (points.empty() || points.size() > 8 || !valid_field_camera_projection(fit)) {
        return std::nullopt;
    }
    std::vector<cv::Point> result;
    for (const auto& source : points) {
        const auto point = project_supported_point(fit, cv::Point2f(source));
        const cv::Rect2f bounds =
            view == FieldView::Held ? cv::Rect2f(120, 100, 1060, 470) : cv::Rect2f(180, 140, 820, 370);
        if (!point || !bounds.contains(*point) || std::any_of(result.begin(), result.end(), [&](const auto& other) {
                return cv::norm(*point - cv::Point2f(other)) < 20;
            })) {
            return std::nullopt;
        }
        result.emplace_back(cvRound(point->x), cvRound(point->y));
    }
    return result;
}

std::optional<cv::Mat> asst::spa::registered_grid_projection(const CameraProjection& fit, const cv::Mat& grid_to_before)
{
    if (grid_to_before.type() != CV_64FC1 || grid_to_before.rows != 3 || grid_to_before.cols != 3 ||
        !cv::checkRange(grid_to_before) || std::abs(cv::determinant(grid_to_before)) < 1e-8 ||
        !valid_field_camera_projection(fit)) {
        return std::nullopt;
    }
    cv::Mat result = fit.matrix * grid_to_before;
    if (!cv::checkRange(result) || std::abs(cv::determinant(result)) < 1e-8) {
        return std::nullopt;
    }
    return result;
}

std::optional<cv::Mat>
    asst::spa::registered_grid_projection(const cv::Mat& before, const cv::Mat& after, const cv::Mat& grid_to_before)
{
    return registered_grid_projection(estimate_camera_projection(before, after, true), grid_to_before);
}

std::optional<std::vector<cv::Point>> asst::spa::registered_held_field_points(
    const cv::Mat& before,
    const cv::Mat& held,
    const std::vector<cv::Point>& points,
    const cv::Mat& grid_to_before,
    size_t held_source,
    std::pair<int, int> source_cell)
{
    if (points.size() < 2 || points.size() > 8 || held_source >= points.size() || source_cell.first < 0 ||
        source_cell.first > 10 || source_cell.second < 0 || source_cell.second > 4) {
        return std::nullopt;
    }
    const auto fit = estimate_camera_projection(before, held, true);
    const auto held_grid = registered_grid_projection(fit, grid_to_before);
    if (!held_grid) {
        return std::nullopt;
    }
    std::vector<cv::Point2f> original_cell, source;
    cv::perspectiveTransform(
        std::vector<cv::Point2f> { cv::Point2f(points[held_source]) },
        original_cell,
        grid_to_before.inv());
    if (original_cell.size() != 1 || !std::isfinite(original_cell.front().x) ||
        !std::isfinite(original_cell.front().y) || std::abs(original_cell.front().x - source_cell.first) > .18 ||
        std::abs(original_cell.front().y - source_cell.second) > .18) {
        return std::nullopt;
    }
    auto peers = points;
    peers.erase(peers.begin() + static_cast<std::ptrdiff_t>(held_source));
    const auto projected = registered_field_points(fit, peers, FieldView::Held);
    if (!projected) {
        return std::nullopt;
    }
    cv::perspectiveTransform(
        std::vector<cv::Point2f> { { static_cast<float>(source_cell.first), static_cast<float>(source_cell.second) } },
        source,
        *held_grid);
    if (source.size() != 1 || !std::isfinite(source.front().x) || !std::isfinite(source.front().y) ||
        !cv::Rect2f(120, 100, 1060, 470).contains(source.front()) ||
        std::any_of(projected->begin(), projected->end(), [&](const auto& point) {
            return cv::norm(source.front() - cv::Point2f(point)) < 20;
        })) {
        return std::nullopt;
    }
    auto result = *projected;
    result.insert(
        result.begin() + static_cast<std::ptrdiff_t>(held_source),
        cv::Point(cvRound(source.front().x), cvRound(source.front().y)));
    return result;
}

std::optional<std::vector<asst::spa::GridPlacementMember>> asst::spa::registered_grid_members(
    const cv::Mat& grid_to_screen,
    const std::vector<PlacementMember>& members,
    FieldView view)
{
    if (members.empty() || members.size() > 8 || grid_to_screen.type() != CV_64FC1 || grid_to_screen.rows != 3 ||
        grid_to_screen.cols != 3 || !cv::checkRange(grid_to_screen) ||
        std::abs(cv::determinant(grid_to_screen)) < 1e-8) {
        return std::nullopt;
    }
    std::vector<cv::Point2f> screen;
    for (const auto& member : members) {
        const cv::Rect bounds = view == FieldView::Held ? cv::Rect(120, 100, 1060, 470) : cv::Rect(180, 140, 820, 370);
        if (member.name.empty() || !bounds.contains(cv::Point(member.x, member.y))) {
            return std::nullopt;
        }
        screen.emplace_back(static_cast<float>(member.x), static_cast<float>(member.y));
    }
    std::vector<cv::Point2f> grid;
    cv::perspectiveTransform(screen, grid, grid_to_screen.inv());
    std::vector<GridPlacementMember> result;
    for (std::size_t index = 0; index < grid.size(); ++index) {
        const auto& point = grid[index];
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < -.25 || point.x > 10.25 || point.y < -.25 ||
            point.y > 4.25) {
            return std::nullopt;
        }
        const int col = static_cast<int>(std::lround(point.x)), row = static_cast<int>(std::lround(point.y));
        if (std::abs(point.x - col) > .25 || std::abs(point.y - row) > .25 ||
            std::any_of(result.begin(), result.end(), [&](const auto& other) {
                return other.col == col && other.row == row;
            })) {
            return std::nullopt;
        }
        const auto& member = members[index];
        result.push_back({ col, row, member.role, member.name, member.facing });
    }
    return result;
}

std::optional<cv::Point> asst::spa::direction_cancel_target(const cv::Mat& image, cv::Point center)
{
    if (image.type() != CV_8UC3) {
        return std::nullopt;
    }
    const cv::Point target(center.x - 150, center.y - 105);
    const cv::Rect roi(target.x - 25, target.y - 22, 50, 44);
    if ((roi & cv::Rect(0, 0, image.cols, image.rows)) != roi) {
        return std::nullopt;
    }
    cv::Mat hsv, red_low, red_high, white;
    cv::cvtColor(image(roi), hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(0, 120, 80), cv::Scalar(12, 255, 255), red_low);
    cv::inRange(hsv, cv::Scalar(170, 120, 80), cv::Scalar(180, 255, 255), red_high);
    cv::inRange(hsv, cv::Scalar(0, 0, 170), cv::Scalar(180, 80, 255), white);
    if (cv::countNonZero(red_low | red_high) < 700 || cv::countNonZero(white) < 50) {
        return std::nullopt;
    }
    return target;
}

std::optional<cv::Point>
    asst::spa::replacement_direction_center(const cv::Mat& placement, const cv::Mat& held, cv::Point expected_target)
{
    const auto center = direction_selection_center(placement);
    if (!center || !direction_cancel_target(placement, *center)) {
        return std::nullopt;
    }
    const auto placement_foot = *center + cv::Point(0, 20);
    // The verified receiver can be inside the feature hull even when the
    // diamond's estimated foot is outside it. On the lower row B-server
    // captures show a 25-27 px offset to the held cell center; the adjacent
    // targets in those frames are more than 60 px from the estimated foot.
    const auto fit = estimate_camera_projection(held, placement, true);
    const auto projected_receiver = registered_field_points(fit, { expected_target }, FieldView::Held);
    if (projected_receiver) {
        return cv::norm(projected_receiver->front() - placement_foot) <= 28 ? center : std::nullopt;
    }
    // The direction overlay hides terrain around the already verified receiver.
    // Use the diamond foot when the camera stayed still and the terrain just
    // outside the overlay supplies a consistent fit.
    if (valid_field_camera_projection(fit) && fit.inliers >= 60 && fit.median_error <= 1) {
        const cv::Point2f receiver(expected_target);
        const auto hull_distance = cv::pointPolygonTest(fit.source_hull, receiver, true);
        if (hull_distance >= -80) {
            std::vector<cv::Point2f> projected;
            cv::perspectiveTransform(std::vector<cv::Point2f> { receiver }, projected, fit.matrix);
            const auto camera_shift = cv::norm(projected.front() - receiver);
            const auto foot_error = cv::norm(projected.front() - cv::Point2f(placement_foot));
            const bool near_registered_terrain = hull_distance >= -24 && camera_shift <= 8 && foot_error <= 18;
            // A tightly registered stationary scene can bridge roughly one tile of occlusion.
            const bool stationary_camera = fit.inliers >= 120 && fit.inliers * 5 >= fit.matches * 4 &&
                                           fit.median_error <= .35 && camera_shift <= 2 && foot_error <= 12;
            if (near_registered_terrain || stationary_camera) {
                return center;
            }
        }
    }
    const auto foot = registered_deployment_pose(placement, held, placement_foot);
    return foot && cv::norm(*foot - expected_target) <= 22 ? center : std::nullopt;
}

std::optional<cv::Point> asst::spa::registered_replacement_cell(const cv::Mat& grid_to_screen, cv::Point recipient)
{
    if (grid_to_screen.type() != CV_64FC1 || grid_to_screen.rows != 3 || grid_to_screen.cols != 3 ||
        !cv::checkRange(grid_to_screen) || std::abs(cv::determinant(grid_to_screen)) < 1e-8) {
        return std::nullopt;
    }
    constexpr float foot_tolerance = .35F;
    std::vector<cv::Point2f> logical;
    cv::perspectiveTransform(std::vector<cv::Point2f> { cv::Point2f(recipient) }, logical, grid_to_screen.inv());
    const auto foot = logical.front();
    if (!std::isfinite(foot.x) || !std::isfinite(foot.y) || foot.x < -foot_tolerance || foot.x > 10 + foot_tolerance ||
        foot.y < -foot_tolerance || foot.y > 4 + foot_tolerance) {
        return std::nullopt;
    }
    const cv::Point cell(cvRound(foot.x), cvRound(foot.y));
    return std::abs(foot.x - cell.x) <= foot_tolerance && std::abs(foot.y - cell.y) <= foot_tolerance
               ? std::optional(cell)
               : std::nullopt;
}

std::optional<cv::Point> asst::spa::registered_relocation_anchor(
    const cv::Mat& grid_to_screen,
    cv::Point verified_point,
    cv::Point destination)
{
    if (registered_replacement_cell(grid_to_screen, verified_point) != destination) {
        return std::nullopt;
    }
    std::vector<cv::Point2f> projected;
    cv::perspectiveTransform(std::vector<cv::Point2f> { cv::Point2f(destination) }, projected, grid_to_screen);
    const auto anchor = projected.front();
    if (!std::isfinite(anchor.x) || !std::isfinite(anchor.y) || !cv::Rect2f(180, 140, 820, 370).contains(anchor)) {
        return std::nullopt;
    }
    return cv::Point(cvRound(anchor.x), cvRound(anchor.y));
}

static std::optional<cv::Point> registered_replacement_preview(
    const cv::Mat& before,
    const cv::Mat& held,
    cv::Point recipient,
    const asst::spa::CameraProjection& projection,
    const asst::spa::RegisteredReplacementReceiver& receiver)
{
    const auto& [cell, grid_to_before] = receiver;
    if (asst::spa::registered_replacement_cell(grid_to_before, recipient) != cell) {
        return std::nullopt;
    }
    const auto grid_to_held = asst::spa::registered_grid_projection(projection, grid_to_before);
    if (!grid_to_held) {
        return std::nullopt;
    }
    const cv::Mat before_to_grid = grid_to_before.inv();
    // Normalize only the inner 86 percent of this logical tile. Compare a
    // clean reference with the held overlay so an adjacent green cell and
    // the recipient's own green costume cannot supply the missing evidence.
    constexpr int sample_size = 80;
    constexpr double radius = .43;
    constexpr double scale = (sample_size - 1) / (2 * radius);
    const cv::Mat grid_to_patch =
        (cv::Mat_<double>(3, 3) << scale, 0, scale * (radius - cell.x), 0, scale, scale * (radius - cell.y), 0, 0, 1);
    const auto green_cell = [&](const cv::Mat& image, const cv::Mat& image_to_grid) {
        cv::Mat patch, hsv, mask;
        cv::warpPerspective(image, patch, grid_to_patch * image_to_grid, { sample_size, sample_size });
        cv::cvtColor(patch, hsv, cv::COLOR_BGR2HSV);
        cv::inRange(hsv, cv::Scalar(35, 80, 90), cv::Scalar(85, 255, 255), mask);
        return mask;
    };
    const auto previous = green_cell(before, before_to_grid);
    const auto current = green_cell(held, grid_to_held->inv());
    cv::Mat added;
    cv::bitwise_and(current, ~previous, added);
    const auto side_supported = [](const cv::Mat& side) {
        int rows = 0;
        for (int row = 0; row < side.rows; ++row) {
            rows += cv::countNonZero(side.row(row)) >= 5;
        }
        return cv::countNonZero(side) >= 80 && rows >= 6;
    };
    if (cv::countNonZero(added) < 400 || !side_supported(added.colRange(0, 32)) ||
        !side_supported(added.colRange(48, sample_size))) {
        return std::nullopt;
    }
    // The green evidence was registered to the logical tile. Return that
    // same tile's projected centre, rather than the shifted portrait foot.
    std::vector<cv::Point2f> centers;
    cv::perspectiveTransform(std::vector<cv::Point2f> { cv::Point2f(cell) }, centers, grid_to_before);
    const auto center = asst::spa::project_supported_point(projection, centers.front());
    if (!center || !cv::Rect2f(340, 170, 660, 320).contains(*center)) {
        return std::nullopt;
    }
    return cv::Point(cvRound(center->x), cvRound(center->y));
}

asst::spa::EquipmentPreviewEvidence asst::spa::equipment_target_preview(
    const cv::Mat& before,
    const cv::Mat& held,
    cv::Point recipient,
    const std::vector<cv::Point>& verified_field_feet,
    bool replacement_above_blocking_device,
    const std::optional<RegisteredReplacementReceiver>& registered_receiver)
{
    if (before.type() != CV_8UC3 || held.type() != CV_8UC3) {
        return {};
    }
    return equipment_target_preview(
        estimate_camera_projection(before, held, true),
        before,
        held,
        recipient,
        verified_field_feet,
        replacement_above_blocking_device,
        registered_receiver);
}

asst::spa::EquipmentPreviewEvidence asst::spa::equipment_target_preview(
    const CameraProjection& projection,
    const cv::Mat& before,
    const cv::Mat& held,
    cv::Point recipient,
    const std::vector<cv::Point>& verified_field_feet,
    bool replacement_above_blocking_device,
    const std::optional<RegisteredReplacementReceiver>& registered_receiver)
{
    EquipmentPreviewEvidence result;
    if (before.size() != cv::Size(1280, 720) || held.size() != before.size() || before.type() != CV_8UC3 ||
        held.type() != CV_8UC3) {
        return result;
    }
    result.matches = projection.matches;
    result.inliers = projection.inliers;
    if (!valid_field_camera_projection(projection)) {
        return result;
    }
    const auto target = project_supported_point(projection, cv::Point2f(recipient));
    if (!target) {
        return result;
    }
    result.target = cv::Point(cvRound(target->x), cvRound(target->y));
    const cv::Rect roi(result.target.x - 36, result.target.y - 28, 73, 57);
    if (result.target.x < 340 || result.target.x >= 1000 || result.target.y < 170 || result.target.y >= 490 ||
        (roi & cv::Rect(0, 0, held.cols, held.rows)) != roi) {
        return result;
    }
    cv::Mat warped, before_hsv, held_hsv, before_green, held_green, added;
    cv::warpPerspective(before, warped, projection.matrix, held.size());
    cv::cvtColor(warped(roi), before_hsv, cv::COLOR_BGR2HSV);
    cv::cvtColor(held(roi), held_hsv, cv::COLOR_BGR2HSV);
    cv::inRange(before_hsv, cv::Scalar(35, 80, 90), cv::Scalar(85, 255, 255), before_green);
    cv::inRange(held_hsv, cv::Scalar(35, 80, 90), cv::Scalar(85, 255, 255), held_green);
    cv::bitwise_and(held_green, ~before_green, added);
    result.added_green = cv::countNonZero(added);
    result.left_green = cv::countNonZero(added(cv::Rect(0, 0, 32, 57)));
    result.right_green = cv::countNonZero(added(cv::Rect(41, 0, 32, 57)));
    result.verified = result.added_green >= 400 && result.left_green >= 80 && result.right_green >= 80;
    if (!result.verified && result.added_green >= 250 && result.left_green >= 80 && result.right_green >= 80) {
        // A verified body one row below can hide most of this recipient's
        // tile. Retain both independent newly-green side strips and the same
        // strict feature projection; the extra evidence is an actual lower
        // field occupant, never a generic reduction for every crowded tile.
        result.occlusion_assisted =
            std::any_of(verified_field_feet.begin(), verified_field_feet.end(), [&](cv::Point foot) {
                const auto lower = project_supported_point(projection, cv::Point2f(foot));
                return lower && std::abs(lower->x - target->x) <= 25 && lower->y - target->y >= 50 &&
                       lower->y - target->y <= 100;
            });
        result.verified = result.occlusion_assisted;
    }
    if (!result.verified && replacement_above_blocking_device) {
        // The crate immediately below hides the lower edge of this occupied
        // receiver. Its upper left and right strips must both light up over
        // several common scanlines after the independent camera alignment.
        const auto left = added(cv::Rect(0, 6, 16, 30));
        const auto right = added(cv::Rect(57, 6, 16, 30));
        int paired_rows = 0;
        for (int row = 0; row < left.rows; ++row) {
            paired_rows += cv::countNonZero(left.row(row)) >= 3 && cv::countNonZero(right.row(row)) >= 3;
        }
        result.blocking_device_assisted =
            cv::countNonZero(left) >= 60 && cv::countNonZero(right) >= 60 && paired_rows >= 5;
        result.verified = result.blocking_device_assisted;
    }
    if (!result.verified && registered_receiver) {
        const auto center = registered_replacement_preview(before, held, recipient, projection, *registered_receiver);
        if (center) {
            result.target = *center;
            result.registered_cell_assisted = true;
            result.verified = true;
        }
    }
    return result;
}

bool asst::spa::green_deployment_hint(const cv::Mat& image, cv::Point center)
{
    const cv::Rect patch(center.x - 18, center.y - 18, 37, 37);
    if (image.empty() || image.type() != CV_8UC3 || center.x < 180 || center.x >= 1000 || center.y < 140 ||
        center.y >= 620 || (patch & cv::Rect(0, 0, image.cols, image.rows)) != patch) {
        return false;
    }
    cv::Mat hsv, mask;
    cv::cvtColor(image(patch), hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(35, 80, 70), cv::Scalar(85, 255, 255), mask);
    int votes = 0;
    for (const auto offset :
         { cv::Point(0, 0), cv::Point(-12, 0), cv::Point(12, 0), cv::Point(0, -12), cv::Point(0, 12) }) {
        const cv::Rect sample(12 + offset.x, 12 + offset.y, 13, 13);
        votes += cv::countNonZero(mask(sample)) >= 0.75 * sample.area() ? 1 : 0;
    }
    // Multiple local patches tolerate UI-label occlusion without accepting a single neighboring green edge.
    return votes >= 2;
}

bool asst::spa::registered_added_green_preview(
    const cv::Mat& before,
    const cv::Mat& held,
    cv::Point held_target,
    const std::vector<cv::Point>& verified_field_feet)
{
    const cv::Rect roi(held_target.x - 18, held_target.y - 18, 37, 37);
    if (before.size() != cv::Size(1280, 720) || held.size() != before.size() || before.type() != CV_8UC3 ||
        held.type() != CV_8UC3 || held_target.x < 340 || held_target.x >= 1000 || held_target.y < 170 ||
        held_target.y >= 490 || (roi & cv::Rect(0, 0, held.cols, held.rows)) != roi) {
        return false;
    }
    const auto projection = estimate_camera_projection(before, held, true);
    if (!projection.estimated || projection.inliers < 30 || projection.inliers < .65 * projection.matches ||
        projection.median_error > 2 || cv::contourArea(projection.source_hull) < 30'000) {
        return false;
    }
    std::vector<cv::Point2f> source;
    cv::perspectiveTransform(std::vector<cv::Point2f> { cv::Point2f(held_target) }, source, projection.matrix.inv());
    const auto supported = source.size() == 1 ? project_supported_point(projection, source.front()) : std::nullopt;
    if (!supported || cv::norm(*supported - cv::Point2f(held_target)) > 2) {
        return false;
    }
    cv::Mat warped, before_hsv, held_hsv, before_green, held_green, added;
    cv::warpPerspective(before, warped, projection.matrix, held.size());
    cv::cvtColor(warped(roi), before_hsv, cv::COLOR_BGR2HSV);
    cv::cvtColor(held(roi), held_hsv, cv::COLOR_BGR2HSV);
    cv::inRange(before_hsv, cv::Scalar(35, 80, 55), cv::Scalar(85, 255, 255), before_green);
    cv::inRange(held_hsv, cv::Scalar(35, 80, 55), cv::Scalar(85, 255, 255), held_green);
    cv::bitwise_and(held_green, ~before_green, added);
    int votes = 0;
    for (const auto offset :
         { cv::Point(0, 0), cv::Point(-12, 0), cv::Point(12, 0), cv::Point(0, -12), cv::Point(0, 12) }) {
        const cv::Rect sample(12 + offset.x, 12 + offset.y, 13, 13);
        votes += cv::countNonZero(held_green(sample)) >= .75 * sample.area() &&
                         cv::countNonZero(added(sample)) >= .50 * sample.area()
                     ? 1
                     : 0;
    }
    if (votes >= 2) {
        return true;
    }
    // A verified unit one row below can cover the target's centre with its
    // sprite and HP label. Require new green across the whole upper band,
    // after the same camera-support checks; foliage present before cannot pass.
    const bool lower_body = std::any_of(verified_field_feet.begin(), verified_field_feet.end(), [&](cv::Point foot) {
        return std::abs(foot.x - held_target.x) <= 25 && foot.y - held_target.y >= 50 && foot.y - held_target.y <= 100;
    });
    if (!lower_body) {
        return false;
    }
    for (const int x : { 0, 12, 24 }) {
        const cv::Rect sample(x, 0, 13, 13);
        if (cv::countNonZero(held_green(sample)) < .75 * sample.area() ||
            cv::countNonZero(added(sample)) < .50 * sample.area()) {
            return false;
        }
    }
    return true;
}

std::vector<asst::spa::DeploymentCellCandidate> asst::spa::detect_held_deployment_cells(const cv::Mat& image)
{
    std::vector<DeploymentCellCandidate> result;
    if (image.empty() || image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3) {
        return result;
    }
    cv::Mat hsv, mask;
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(35, 80, 90), cv::Scalar(85, 255, 255), mask);
    // Exclude the HUD, staging sprite, cyan bench and the shop/sale overlay.
    // The mask is evaluated after the camera has settled with contact held.
    mask(cv::Rect(0, 0, 1280, 160)).setTo(0);
    mask(cv::Rect(0, 495, 1280, 225)).setTo(0);
    mask(cv::Rect(0, 0, 300, 720)).setTo(0);
    mask(cv::Rect(1000, 0, 280, 720)).setTo(0);
    cv::Mat distance, maximum, peaks;
    cv::distanceTransform(mask, distance, cv::DIST_L2, 5);
    cv::dilate(distance, maximum, cv::Mat::ones(45, 45, CV_8UC1));
    cv::compare(distance, maximum, peaks, cv::CMP_EQ);
    peaks &= distance > 16.0;
    cv::Mat labels, stats, centers;
    const int count = cv::connectedComponentsWithStats(peaks, labels, stats, centers, 8, CV_32S);
    for (int index = 1; index < count; ++index) {
        const cv::Point center(cvRound(centers.at<double>(index, 0)), cvRound(centers.at<double>(index, 1)));
        if (green_deployment_hint(image, center)) {
            result.push_back({ center, distance.at<float>(center) });
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    std::vector<DeploymentCellCandidate> distinct;
    for (const auto& candidate : result) {
        if (std::none_of(distinct.begin(), distinct.end(), [&](const auto& accepted) {
                return cv::norm(candidate.center - accepted.center) < 50;
            })) {
            distinct.push_back(candidate);
        }
    }
    return distinct;
}

bool asst::spa::occupied_deployment_cell(const cv::Mat& image, cv::Point center)
{
    if (image.empty() || image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3 || center.x < 440 ||
        center.x >= 1000 || center.y < 160 || center.y >= 500) {
        return false;
    }
    // A deployed unit's facing chevron is the most stable occupancy marker on
    // this server.  Probe the tile centre and its foot-point offsets because
    // the sprite is rendered one half-tile above the logical cell.
    for (const auto offset :
         { cv::Point(0, 0), cv::Point(0, 25), cv::Point(0, 45), cv::Point(-25, 25), cv::Point(25, 25) }) {
        if (deployed_facing(image, center + offset)) {
            return true;
        }
    }
    // Facing chevrons are rendered near the neighbouring tile edge on the
    // held-drag camera. Use a wider vertical neighbourhood so a candidate
    // one half-cell away from a visible operator is still rejected as
    // occupied instead of becoming an unintended swap target.
    cv::Rect patch(center.x - 60, center.y - 60, 121, 121);
    if ((patch & cv::Rect(0, 0, image.cols, image.rows)) != patch) {
        return false;
    }
    cv::Mat hsv, orange;
    cv::cvtColor(image(patch), hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(8, 125, 120), cv::Scalar(35, 255, 255), orange);
    if (cv::countNonZero(orange) >= 55) {
        return true;
    }
    // During the held-drag green overlay the facing chevron can be washed
    // out, while the operator body remains as a compact dark silhouette on
    // an otherwise bright green tile. Use that as a conservative fallback;
    // this is evaluated only inside a legal-cell patch, so map-wide dark
    // props do not become occupied operators.
    cv::Mat dark;
    cv::inRange(hsv, cv::Scalar(0, 0, 0), cv::Scalar(180, 255, 82), dark);
    cv::morphologyEx(dark, dark, cv::MORPH_OPEN, cv::Mat::ones(2, 2, CV_8UC1));
    return cv::countNonZero(dark) >= 110;
}

std::vector<asst::spa::DeploymentCellCandidate> asst::spa::detect_deployment_cells(const cv::Mat& image)
{
    std::vector<DeploymentCellCandidate> result;
    if (image.empty() || image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3) {
        return result;
    }

    // The normal preparation map draws legal deployment cells with a bright
    // blue/cyan border.  Restrict the mask to the board and reject elongated
    // blue UI/equipment components; the returned point is only a candidate.
    cv::Mat hsv, mask = cv::Mat::zeros(image.size(), CV_8UC1);
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(85, 120, 130), cv::Scalar(115, 255, 255), mask);
    mask(cv::Rect(0, 0, 180, 140)).setTo(0);
    mask(cv::Rect(1000, 0, 280, 720)).setTo(0);
    mask(cv::Rect(0, 420, 1280, 300)).setTo(0);

    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, cv::Mat::ones(3, 3, CV_8UC1));
    cv::Mat labels, stats, centroids;
    cv::connectedComponentsWithStats(mask, labels, stats, centroids, 8, CV_32S);
    for (int index = 1; index < stats.rows; ++index) {
        const cv::Rect box(
            stats.at<int>(index, cv::CC_STAT_LEFT),
            stats.at<int>(index, cv::CC_STAT_TOP),
            stats.at<int>(index, cv::CC_STAT_WIDTH),
            stats.at<int>(index, cv::CC_STAT_HEIGHT));
        const int area = stats.at<int>(index, cv::CC_STAT_AREA);
        if (box.width < 55 || box.height < 55 || box.width > 140 || box.height > 140 || area < 400 || area > 2200) {
            continue;
        }
        const double aspect = static_cast<double>(box.width) / static_cast<double>(box.height);
        if (aspect < 0.75 || aspect > 1.33) {
            continue;
        }
        const cv::Point center(box.x + box.width / 2, box.y + box.height / 2);
        // The persistent cyan marker on the left side of the board is the
        // player's base/entry indicator, not a legal operator deployment
        // cell.  It has the same square outline as the real cell overlay, so
        // geometry alone cannot distinguish it.  Keep the autonomous probe
        // conservative until a map-specific legal-cell classifier exists:
        // left-of-board candidates are diagnostic only and must not become a
        // drag target.
        if (center.x < 500 || center.x >= 980 || center.y < 150 || center.y >= 410) {
            continue;
        }
        const double fill = static_cast<double>(area) / static_cast<double>(box.area());
        if (fill < 0.03 || fill > 0.45) {
            continue;
        }
        const double score = fill * std::min(box.width, box.height);
        if (std::any_of(result.begin(), result.end(), [&](const auto& existing) {
                return cv::norm(existing.center - center) < 30;
            })) {
            continue;
        }
        result.push_back({ center, score });
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.center.y != right.center.y) {
            return left.center.y < right.center.y;
        }
        return left.center.x < right.center.x;
    });

    // Normal preparation does not render the legal-cell overlay until a card
    // is held.  Consequently a clean frame can contain no blue cell outline
    // at all, even though the first probeable tile is known from the stable
    // B-server board geometry.  Return a small, ordered probe set as a
    // *candidate* fallback instead of pretending these points are legal.  The
    // drag plugin still requires the live green preview, placement page,
    // identity and population reconciliation before accepting one.  Keep the
    // list away from the persistent left base marker and the red enemy zone;
    // failed points are consumed by the caller and never replayed.
    if (result.empty()) {
        // Maps use different legal tiles.  A nine-point center sample left
        // large deserts and lower lanes untested, which made a valid
        // preparation page look blocked.  Keep this as a bounded diagnostic
        // grid; legality is still decided by the held-card green preview and
        // the post-drag population/identity reconciliation in SwapTaskPlugin.
        // The lower lane is the only geometry-backed deployment area we have
        // verified across the current B-server desert layout.  Probe it first
        // so a transient OCR miss cannot spend the bounded retry budget on
        // decorative upper cells or obstacle tiles.  The remaining points are
        // still retained as map-specific fallbacks and remain guarded by the
        // held-card green preview plus post-drop reconciliation.
        const std::array<cv::Point, 24> probe_points {
            cv::Point(700, 340), cv::Point(620, 340), cv::Point(780, 340), cv::Point(860, 340), cv::Point(540, 340),
            cv::Point(940, 340), cv::Point(700, 260), cv::Point(780, 260), cv::Point(620, 260), cv::Point(860, 260),
            cv::Point(540, 260), cv::Point(940, 260), cv::Point(700, 180), cv::Point(780, 180), cv::Point(620, 180),
            cv::Point(860, 180), cv::Point(540, 420), cv::Point(620, 420), cv::Point(700, 420), cv::Point(780, 420),
            cv::Point(860, 420), cv::Point(940, 420), cv::Point(620, 500), cv::Point(780, 500),
        };
        result.reserve(probe_points.size());
        for (size_t index = 0; index < probe_points.size(); ++index) {
            result.push_back({ probe_points[index], 0.10 / static_cast<double>(index + 1) });
        }
    }
    return result;
}

std::optional<cv::Point> asst::spa::direction_selection_center(const cv::Mat& image)
{
    if (image.empty() || image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3) {
        return std::nullopt;
    }
    cv::Mat hsv, mask;
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(0, 0, 180), cv::Scalar(180, 65, 255), mask);
    mask(cv::Rect(0, 0, 1280, 60)).setTo(0);
    // A lower-left placement can have its left vertex near x=320 even when
    // its center is on the board. Keep the entire outline for the diagonal fit.
    mask(cv::Rect(0, 0, 280, 720)).setTo(0);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    std::vector<std::pair<cv::Point2f, double>> diamond_candidates;
    for (const auto& contour : contours) {
        const auto area = cv::contourArea(contour);
        // B-server's placement diamond is larger than the original range on
        // the 1280x720 layout (roughly 170k px² on the verified lower-row
        // screenshot).  Keep a bounded upper limit to avoid accepting the
        // full-screen white UI frame.
        if (area < 30'000 || area > 260'000) {
            continue;
        }
        std::vector<cv::Point> corners;
        cv::approxPolyDP(contour, corners, 0.025 * cv::arcLength(contour, true), true);
        if (corners.size() != 4 || !cv::isContourConvex(corners)) {
            continue;
        }
        const cv::Point2d a(corners[0]), b(corners[1]);
        const cv::Point2d u = cv::Point2d(corners[2]) - a;
        const cv::Point2d v = cv::Point2d(corners[3]) - b;
        const double cross = u.x * v.y - u.y * v.x;
        if (std::abs(cross) < 1) {
            continue;
        }
        const auto delta = b - a;
        const double t = (delta.x * v.y - delta.y * v.x) / cross;
        const auto center = a + t * u;
        // The direction control can extend beyond the deployment-board ROI.
        // A legal right-hand road tile has an observed diamond centre near
        // x=1000; the old board-width bound rejected the valid control.
        if (center.x < 450 || center.x >= 1230 || center.y < 140 || center.y >= 650) {
            continue;
        }
        diamond_candidates.emplace_back(cv::Point2f(static_cast<float>(center.x), static_cast<float>(center.y)), area);
    }
    // The white diamond is commonly split into inner and outer contours by
    // the operator sprite/green overlay.  Their centers differ by a tile
    // fraction; treating that as a contradiction used to reject an otherwise
    // perfectly usable direction control.  Anchor on the largest contour and
    // average nearby contours instead of requiring pixel-identical centers.
    std::optional<cv::Point> result;
    if (!diamond_candidates.empty()) {
        const auto anchor = std::max_element(
            diamond_candidates.begin(),
            diamond_candidates.end(),
            [](const auto& left, const auto& right) { return left.second < right.second; });
        cv::Point2f weighted { 0.0F, 0.0F };
        double weight = 0.0;
        for (const auto& [center, area] : diamond_candidates) {
            if (cv::norm(center - anchor->first) <= 55.0) {
                weighted += center * static_cast<float>(area);
                weight += area;
            }
        }
        if (weight > 0.0) {
            weighted *= static_cast<float>(1.0 / weight);
            result = cv::Point(cvRound(weighted.x), cvRound(weighted.y));
        }
    }
    if (!result) {
        // Some B-server placement diamonds are split by the operator sprite
        // and green-tile overlay, so contour approximation may not produce a
        // single quadrilateral.  A bounded white-line envelope is still a
        // useful center estimate for the subsequent arrow click; direction
        // and post-release verification remain mandatory.
        // The upper vertex can be above y=180 when the target is on the
        // upper half of an ice/snow map. Use a separate, lower-contrast mask.
        // Closing the broken white edges joins the four sides into one stable
        // envelope; the geometry and post-release checks remain authoritative.
        const cv::Rect roi(280, 50, 720, 570);
        cv::Mat fallback;
        cv::inRange(hsv, cv::Scalar(0, 0, 160), cv::Scalar(180, 100, 255), fallback);
        fallback(cv::Rect(0, 0, 1280, 50)).setTo(0);
        fallback(cv::Rect(1000, 0, 280, 720)).setTo(0);
        fallback = fallback(roi).clone();
        cv::morphologyEx(fallback, fallback, cv::MORPH_CLOSE, cv::Mat::ones(5, 5, CV_8UC1), cv::Point(-1, -1), 2);
        cv::dilate(fallback, fallback, cv::Mat::ones(3, 3, CV_8UC1));
        std::vector<std::vector<cv::Point>> fallback_contours;
        cv::findContours(fallback, fallback_contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        for (const auto& contour : fallback_contours) {
            const auto area = cv::contourArea(contour);
            const auto bounds = cv::boundingRect(contour);
            if (area < 5000 || bounds.width < 250 || bounds.width > 500 || bounds.height < 180 || bounds.height > 500) {
                continue;
            }
            const cv::Point point(roi.x + bounds.x + bounds.width / 2, roi.y + bounds.y + bounds.height / 2);
            if (point.x >= 450 && point.x < 1000 && point.y >= 140 && point.y < 620) {
                result = direction_edge_center(mask, bounds + roi.tl()).value_or(point);
                break;
            }
        }
    }
    return result;
}

std::optional<char> asst::spa::deployed_facing(const cv::Mat& image, cv::Point foot)
{
    try {
        const cv::Rect patch(foot.x - 50, foot.y - 50, 101, 101);
        if (image.empty() || image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3 ||
            (patch & cv::Rect(0, 0, image.cols, image.rows)) != patch) {
            return std::nullopt;
        }
        cv::Mat hsv, mask;
        cv::cvtColor(image(patch), hsv, cv::COLOR_BGR2HSV);
        cv::inRange(hsv, cv::Scalar(8, 160, 160), cv::Scalar(35, 255, 255), mask);
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        std::optional<char> result;
        for (const auto& contour : contours) {
            const auto area = cv::contourArea(contour);
            const auto bounds = cv::boundingRect(contour);
            if (area < 45 || area > 450 || bounds.width < 8 || bounds.height < 8 || bounds.width > 40 ||
                bounds.height > 40 || contour.size() < 4) {
                continue;
            }
            const auto moments = cv::moments(contour);
            if (std::abs(moments.m00) < 1e-6) {
                continue;
            }
            const cv::Point2d offset(moments.m10 / moments.m00 - 50, moments.m01 / moments.m00 - 50);
            if (offset.dot(offset) < 100 || offset.dot(offset) > 2025) {
                continue;
            }
            std::vector<int> hull;
            std::vector<cv::Vec4i> defects;
            cv::convexHull(contour, hull, false, false);
            if (hull.size() < 3 || hull.size() == contour.size()) {
                continue;
            }
            cv::convexityDefects(contour, hull, defects);
            std::optional<char> component_direction;
            double best_score = 0;
            for (const auto& defect : defects) {
                const cv::Point2d a(contour[defect[0]]), b(contour[defect[1]]), notch(contour[defect[2]]);
                const auto edge = b - a;
                const double length = cv::norm(edge);
                const double depth = defect[3] / 256.0;
                if (length < 12 || length > 45 || depth < 1.8 || depth > 0.45 * length) {
                    continue;
                }
                // A chevron's inward notch points along the normal to its open hull edge.
                // This does not assume an exact operator foot pixel or a symmetric visible marker.
                cv::Point2d direction(edge.y, -edge.x);
                if (direction.dot(notch - (a + b) * 0.5) < 0) {
                    direction *= -1;
                }
                if (direction.dot(offset) < 5 * length) {
                    continue;
                }
                std::optional<char> facing;
                if (std::abs(direction.x) > 1.5 * std::abs(direction.y)) {
                    facing = direction.x > 0 ? 'E' : 'W';
                }
                else if (std::abs(direction.y) > 1.5 * std::abs(direction.x)) {
                    facing = direction.y > 0 ? 'S' : 'N';
                }
                if (facing && depth * length > best_score) {
                    best_score = depth * length;
                    component_direction = facing;
                }
            }
            if (component_direction) {
                if (result && result != component_direction) {
                    return std::nullopt;
                }
                result = component_direction;
            }
        }
        return result;
    }
    catch (const cv::Exception& error) {
        LogWarn << "SPA facing probe ignored OpenCV exception" << error.what();
        return std::nullopt;
    }
}

bool asst::spa::active_strategy_ready(const cv::Mat& image)
{
    if (image.empty() || image.size() != cv::Size(1280, 720) || image.type() != CV_8UC3) {
        return false;
    }
    cv::Mat hsv, mask;
    cv::cvtColor(image(cv::Rect(1190, 175, 90, 115)), hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(65, 80, 100), cv::Scalar(100, 255, 255), mask);
    return cv::countNonZero(mask) >= 500;
}
