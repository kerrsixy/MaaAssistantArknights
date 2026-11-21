#include "BlackFlowWeatherAnalyzer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>
#include <utility>

#include "MaaUtils/ImageIo.h"
#include "MaaUtils/NoWarningCV.hpp"

namespace asst::blackflow::weather_vision
{
bool inside(const Rect& rect, const cv::Mat& image)
{
    return rect.x >= 0 && rect.y >= 0 && rect.width > 0 && rect.height > 0 && rect.width <= image.cols &&
           rect.height <= image.rows && rect.x <= image.cols - rect.width && rect.y <= image.rows - rect.height;
}

std::optional<Rect> read_rect(const json::value& value)
{
    if (!value.is<std::vector<int>>()) {
        return std::nullopt;
    }
    const auto items = value.as<std::vector<int>>();
    if (items.size() != 4) {
        return std::nullopt;
    }
    return Rect { items[0], items[1], items[2], items[3] };
}

std::string compact(std::string_view input)
{
    std::string output;
    for (const unsigned char c : input) {
        if (!std::isspace(c)) {
            output.push_back(static_cast<char>(c));
        }
    }
    return output;
}

bool simple_file_name(const std::string& file)
{
    return !file.empty() &&
           std::ranges::all_of(
               file,
               [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.'; }) &&
           file != "." && file != "..";
}

cv::Mat white_mask(const cv::Mat& image, bool top_glyph)
{
    cv::Mat mask;
    cv::inRange(
        image,
        top_glyph ? cv::Scalar(146, 146, 186) : cv::Scalar(201, 201, 201),
        cv::Scalar(255, 255, 255),
        mask);
    cv::Mat output;
    mask.convertTo(output, CV_32F, 1.0 / 255.0);
    return output;
}

double finite_max(const cv::Mat& scores, cv::Point* point = nullptr)
{
    double best = 0.0;
    cv::Point position;
    for (int y = 0; y < scores.rows; ++y) {
        const auto* row = scores.ptr<float>(y);
        for (int x = 0; x < scores.cols; ++x) {
            const double score = row[x];
            if (std::isfinite(score) && score <= 1.00001 && score > best) {
                best = std::min(score, 1.0);
                position = { x, y };
            }
        }
    }
    if (point) {
        *point = position;
    }
    return best;
}

bool emergency(std::string_view type)
{
    return type == "battle_elite" || type == "emergency" || type == "紧急作战";
}

double grid_pitch(const std::vector<WeatherNodeObservation>& nodes)
{
    std::vector<double> readings;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (!nodes[i].exists) {
            continue;
        }
        const double ax = nodes[i].icon_rect.x + nodes[i].icon_rect.width / 2.0;
        const double ay = nodes[i].icon_rect.y + nodes[i].icon_rect.height / 2.0;
        for (std::size_t j = i + 1; j < nodes.size(); ++j) {
            if (!nodes[j].exists) {
                continue;
            }
            const double bx = nodes[j].icon_rect.x + nodes[j].icon_rect.width / 2.0;
            const double by = nodes[j].icon_rect.y + nodes[j].icon_rect.height / 2.0;
            if (nodes[i].cell.row == nodes[j].cell.row && nodes[i].cell.column != nodes[j].cell.column) {
                readings.push_back(std::abs(ax - bx) / std::abs(nodes[i].cell.column - nodes[j].cell.column));
            }
            if (nodes[i].cell.column == nodes[j].cell.column && nodes[i].cell.row != nodes[j].cell.row) {
                readings.push_back(std::abs(ay - by) / std::abs(nodes[i].cell.row - nodes[j].cell.row));
            }
        }
    }
    if (readings.empty()) {
        return 0.0;
    }
    std::ranges::sort(readings);
    const double median = readings[readings.size() / 2];
    return median >= 45.0 && median <= 180.0 ? median : 0.0;
}

std::uint64_t image_hash(const cv::Mat& image)
{
    std::uint64_t hash = 1'469'598'103'934'665'603ULL;
    const std::size_t row_bytes = static_cast<std::size_t>(image.cols) * image.elemSize();
    for (int row = 0; row < image.rows; ++row) {
        const auto* data = image.ptr<unsigned char>(row);
        for (std::size_t index = 0; index < row_bytes; ++index) {
            hash = (hash ^ data[index]) * 1'099'511'628'211ULL;
        }
    }
    return hash;
}

bool load_offsets(const json::value& patterns, const std::string& name, int radius, std::vector<WeatherCell>& output)
{
    const auto entry = patterns.find<json::object>(name);
    if (!entry) {
        return false;
    }
    const json::value value(*entry);
    const auto offsets = value.find<json::array>("offsets");
    if (value.get("metric", std::string()) != "manhattan" || value.get("radius", 0) != radius ||
        !value.get("includes_source", false) || !offsets) {
        return false;
    }
    std::set<std::pair<int, int>> unique;
    for (const auto& offset : *offsets) {
        if (!offset.is<std::vector<int>>()) {
            return false;
        }
        const auto cell = offset.as<std::vector<int>>();
        if (cell.size() != 2 || std::abs(cell[0]) + std::abs(cell[1]) > radius ||
            !unique.emplace(cell[0], cell[1]).second) {
            return false;
        }
        output.push_back({ cell[0], cell[1] });
    }
    return static_cast<int>(unique.size()) == 1 + 2 * radius * (radius + 1);
}

struct RadialFeatures
{
    double score = 0.0;
    int sectors = 0;
    double density = 0.0;
    bool fully_visible = false;
};

RadialFeatures radial_features(
    const cv::Mat& image,
    const cv::Mat& image_edges,
    const cv::Mat& source_template,
    const Rect& node,
    double pitch)
{
    RadialFeatures result;
    const double scale = pitch / 100.0;
    const int size = static_cast<int>(std::lround(160.0 * scale));
    const int search = std::max(3, static_cast<int>(std::lround(8.0 * scale)));
    const int cx = node.x + node.width / 2;
    const int cy = node.y + node.height / 2;
    const Rect area { cx - size / 2 - search, cy - size / 2 - search, size + search * 2, size + search * 2 };
    if (size < 64 || !inside(area, image)) {
        return result;
    }
    result.fully_visible = true;
    cv::Mat resized;
    cv::resize(source_template, resized, { size, size }, 0, 0, cv::INTER_AREA);
    cv::Mat gray;
    cv::cvtColor(resized, gray, cv::COLOR_BGR2GRAY);
    cv::Mat template_edges;
    cv::Canny(gray, template_edges, 40, 90);
    cv::Mat annulus(size, size, CV_8U, cv::Scalar(0));
    int annular_pixels = 0;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double radius = std::hypot(x - size / 2.0, y - size / 2.0);
            if (radius >= 35 * scale && radius <= 78 * scale) {
                annulus.at<unsigned char>(y, x) = 255;
                ++annular_pixels;
            }
        }
    }
    cv::Mat scores;
    cv::matchTemplate(image_edges(make_rect<cv::Rect>(area)), template_edges, scores, cv::TM_CCORR_NORMED, annulus);
    cv::Point best;
    result.score = finite_max(scores, &best);
    const int left = area.x + best.x;
    const int top = area.y + best.y;
    std::array<int, 16> counts {};
    std::array<double, 16> radial_min;
    radial_min.fill(std::numeric_limits<double>::max());
    std::array<double, 16> radial_max {};
    int edge_count = 0;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (!annulus.at<unsigned char>(y, x) || !image_edges.at<unsigned char>(top + y, left + x)) {
                continue;
            }
            const auto color = image.at<cv::Vec3b>(top + y, left + x);
            if (std::max({ color[0], color[1], color[2] }) - std::min({ color[0], color[1], color[2] }) >= 45) {
                continue;
            }
            ++edge_count;
            const double angle = std::atan2(y - size / 2.0, x - size / 2.0) + std::numbers::pi;
            const int sector = std::clamp(static_cast<int>(angle * 16 / (2 * std::numbers::pi)), 0, 15);
            const double radius = std::hypot(x - size / 2.0, y - size / 2.0);
            ++counts[sector];
            radial_min[sector] = std::min(radial_min[sector], radius);
            radial_max[sector] = std::max(radial_max[sector], radius);
        }
    }
    for (int sector = 0; sector < 16; ++sector) {
        if (counts[sector] >= std::max(5, static_cast<int>(5 * scale)) &&
            radial_max[sector] - radial_min[sector] >= 18 * scale) {
            ++result.sectors;
        }
    }
    result.density = annular_pixels ? static_cast<double>(edge_count) / annular_pixels : 0.0;
    return result;
}
}

bool asst::blackflow::BlackFlowWeatherAnalyzer::load(
    const std::filesystem::path& catalog_path,
    const std::filesystem::path& template_manifest_path,
    std::string* error)
{
    m_loaded = false;
    m_entries.clear();
    m_glyphs.clear();
    m_map_anchors.clear();
    m_small_offsets.clear();
    m_large_offsets.clear();
    reset();
    const auto fail = [error](const std::string& reason) {
        if (error) {
            *error = reason;
        }
        return false;
    };
    const auto catalog = json::open(catalog_path);
    const auto manifest = json::open(template_manifest_path);
    if (!catalog || !manifest || catalog->get("schema_version", 0) != 1 ||
        catalog->get("theme", std::string()) != "BlackFlow" || manifest->get("schema_version", 0) != 1) {
        return fail("Weather catalog or template manifest is unavailable or has unsupported schema");
    }
    const auto items = catalog->find<json::array>("items");
    const auto glyphs = manifest->find<json::array>("glyphs");
    const auto anchors = manifest->find<json::array>("map_anchors");
    const auto patterns = catalog->find<json::object>("range_patterns");
    const auto native = manifest->find<std::vector<int>>("native_size");
    if (!items || items->size() != 43 || !glyphs || glyphs->size() != 23 || !anchors || anchors->size() != 2 ||
        !patterns || !native || *native != std::vector<int> { 1280, 720 }) {
        return fail("Incomplete weather entries, glyphs, map anchors, native size or range data");
    }
    if (!weather_vision::load_offsets(json::value(*patterns), "x-1", 2, m_small_offsets) ||
        !weather_vision::load_offsets(json::value(*patterns), "x-3", 3, m_large_offsets)) {
        return fail("Weather range offsets do not form the complete declared Manhattan patterns");
    }
    std::set<std::string> ids;
    std::set<std::string> expected_icons;
    for (const auto& item : *items) {
        Entry entry;
        auto& metadata = entry.metadata;
        metadata.id = item.get("id", std::string());
        metadata.icon_id = item.get("iconId", std::string());
        metadata.type = item.get("type", std::string());
        metadata.category = item.get("category", std::string());
        metadata.name = item.get("name", std::string());
        metadata.level = item.find<int>("level");
        metadata.positive = item.find<bool>("isPositive");
        metadata.minimum_difficulty = item.find<int>("min_difficulty");
        metadata.destructible_source = item.find<bool>("destructible_source");
        metadata.body = item.get("description", std::string());
        metadata.activation = item.get("activation", std::string());
        metadata.scope_mode = item.get("scope_mode", std::string());
        metadata.source_node_rule = item.get("source_node_rule", std::string());
        if (metadata.source_node_rule == "event_non_emergency_indestructible") {
            metadata.destructible_source = false;
        }
        if (const auto effects = item.find<json::array>("effects")) {
            metadata.effects = *effects;
        }
        if (const auto grades = item.find<std::vector<int>>("difficulty_grades")) {
            metadata.difficulty_grades = *grades;
        }
        if (metadata.id.empty() || metadata.icon_id.empty() || metadata.name.empty() || metadata.body.empty() ||
            (metadata.category != "eutopia_ideology" && metadata.category != "eutopia_policy" &&
             metadata.category != "utopia_variation") ||
            !ids.insert(metadata.id).second) {
            return fail("Weather catalog contains incomplete, unknown or duplicate entries");
        }
        expected_icons.insert(metadata.icon_id);
        m_entries.push_back(std::move(entry));
    }
    const auto root = template_manifest_path.parent_path();
    std::set<std::string> loaded_icons;
    for (const auto& glyph : *glyphs) {
        Glyph item;
        item.icon_id = glyph.get("icon_id", std::string());
        item.category = glyph.get("category", std::string());
        const auto file = glyph.get("file", std::string());
        if (!expected_icons.contains(item.icon_id) || !loaded_icons.insert(item.icon_id).second ||
            !std::ranges::any_of(
                m_entries,
                [&item](const Entry& entry) {
                    return entry.metadata.icon_id == item.icon_id && entry.metadata.category == item.category;
                }) ||
            !weather_vision::simple_file_name(file)) {
            return fail("Invalid weather glyph association or filename");
        }
        const auto image = MAA_NS::imread(root / MAA_NS::path(file));
        if (image.empty() || image.type() != CV_8UC3) {
            return fail("Weather glyph image is unavailable");
        }
        cv::inRange(image, cv::Scalar(211, 211, 211), cv::Scalar(255, 255, 255), item.mask);
        item.mask.convertTo(item.mask, CV_32F, 1.0 / 255.0);
        const double density = cv::sum(item.mask)[0] / item.mask.total();
        if (density < 0.01 || density > 0.75) {
            return fail("Weather glyph is empty or lacks a usable foreground");
        }
        m_glyphs.push_back(std::move(item));
    }
    if (loaded_icons != expected_icons) {
        return fail("Weather glyph coverage does not match the catalog");
    }
    for (const auto& anchor : *anchors) {
        const auto file = anchor.get("file", std::string());
        const auto* rect_value = anchor.find_value("search_roi");
        const auto rect = rect_value ? weather_vision::read_rect(*rect_value) : std::nullopt;
        if (!weather_vision::simple_file_name(file) || !rect) {
            return fail("Invalid map anchor configuration");
        }
        MapAnchor item;
        item.mask = MAA_NS::imread(root / MAA_NS::path(file), cv::IMREAD_GRAYSCALE);
        item.search_roi = *rect;
        item.threshold = anchor.get("threshold", 0.86);
        if (item.mask.empty() || item.threshold < 0.75 || item.threshold > 1.0 || item.search_roi.x < 0 ||
            item.search_roi.y < 0 || item.search_roi.width < item.mask.cols ||
            item.search_roi.height < item.mask.rows || item.search_roi.width > 1280 || item.search_roi.height > 720 ||
            item.search_roi.x > 1280 - item.search_roi.width || item.search_roi.y > 720 - item.search_roi.height) {
            return fail("Invalid map anchor template or threshold");
        }
        item.mask.convertTo(item.mask, CV_32F, 1.0 / 255.0);
        m_map_anchors.push_back(std::move(item));
    }
    const auto* glyph_roi = manifest->find_value("glyph_roi");
    const auto parsed_roi = glyph_roi ? weather_vision::read_rect(*glyph_roi) : std::nullopt;
    const auto source_ring = manifest->find<json::object>("source_ring");
    if (!parsed_roi || !source_ring) {
        return fail("Missing glyph ROI or radial source template");
    }
    m_glyph_roi = *parsed_roi;
    if (m_glyph_roi.x < 0 || m_glyph_roi.y < 0 || m_glyph_roi.width < 24 || m_glyph_roi.height < 24 ||
        m_glyph_roi.width > 1280 || m_glyph_roi.height > 720 || m_glyph_roi.x > 1280 - m_glyph_roi.width ||
        m_glyph_roi.y > 720 - m_glyph_roi.height) {
        return fail("Glyph ROI is clipped or too small for its declared scale search");
    }
    const auto source_file = json::value(*source_ring).get("file", std::string());
    if (!weather_vision::simple_file_name(source_file)) {
        return fail("Invalid radial source template filename");
    }
    m_source_template = MAA_NS::imread(root / MAA_NS::path(source_file));
    if (m_source_template.size() != cv::Size(160, 160) || m_source_template.type() != CV_8UC3) {
        return fail("Radial source template must be the native annular sample");
    }
    m_glyph_minimum_score = manifest->get("glyph_minimum_score", 0.76);
    m_glyph_minimum_margin = manifest->get("glyph_minimum_margin", 0.15);
    if (m_glyph_minimum_score < 0.70 || m_glyph_minimum_score > 1.0 || m_glyph_minimum_margin < 0.1 ||
        m_glyph_minimum_margin > 1.0) {
        return fail("Unsafe glyph confidence or runner-up margin");
    }
    m_loaded = true;
    return true;
}

std::optional<asst::blackflow::WeatherMetadata> asst::blackflow::BlackFlowWeatherAnalyzer::metadata_for_icon(
    const std::string& icon_id,
    std::optional<int> difficulty) const
{
    std::vector<const Entry*> matches;
    for (const auto& entry : m_entries) {
        if (entry.metadata.icon_id == icon_id) {
            matches.push_back(&entry);
        }
    }
    if (matches.size() == 1) {
        return matches.front()->metadata;
    }
    if (difficulty) {
        const Entry* selected = nullptr;
        for (const auto* entry : matches) {
            if (std::ranges::find(entry->metadata.difficulty_grades, *difficulty) !=
                entry->metadata.difficulty_grades.end()) {
                if (selected) {
                    selected = nullptr;
                    break;
                }
                selected = entry;
            }
        }
        if (selected) {
            auto result = selected->metadata;
            result.level_from_verified_difficulty = true;
            return result;
        }
    }
    if (!matches.empty()) {
        auto result = matches.front()->metadata;
        result.id.clear();
        result.level.reset();
        result.body.clear();
        result.effects = {};
        result.difficulty_grades.clear();
        for (const auto* entry : matches) {
            for (const int grade : entry->metadata.difficulty_grades) {
                if (std::ranges::find(result.difficulty_grades, grade) == result.difficulty_grades.end()) {
                    result.difficulty_grades.push_back(grade);
                }
            }
        }
        std::ranges::sort(result.difficulty_grades);
        return result;
    }
    return std::nullopt;
}

asst::blackflow::WeatherIdentityObservation asst::blackflow::BlackFlowWeatherAnalyzer::tooltip_observation(
    const std::optional<std::string>& id,
    const std::optional<std::string>& body,
    bool policy) const
{
    WeatherIdentityObservation result;
    result.reason = policy ? "Policy is not inferred from the main glyph" : "No verified tooltip body";
    if (!id || !body || body->empty()) {
        return result;
    }
    for (const auto& entry : m_entries) {
        if (entry.metadata.id == *id && (entry.metadata.category == "eutopia_policy") == policy) {
            if (weather_vision::compact(entry.metadata.body) != weather_vision::compact(*body)) {
                result.reason = "Tooltip body does not match the claimed catalog entry";
                return result;
            }
            result.state = WeatherIdentityState::TooltipConfirmed;
            result.metadata = entry.metadata;
            result.metadata->tooltip_body_verified = true;
            result.metadata->body_from_catalog = false;
            result.evidence_source = "caller_verified_game_tooltip_id_and_body";
            result.reason = "Exact tooltip identity and body agree with the catalog";
            return result;
        }
    }
    result.reason = "Unknown tooltip ID or wrong weather category";
    return result;
}

asst::blackflow::WeatherRecognitionResult
    asst::blackflow::BlackFlowWeatherAnalyzer::analyze(const cv::Mat& image, const WeatherFrameContext& context)
{
    WeatherRecognitionResult result;
    result.policy.reason = "Policy is unknown without its own tooltip evidence";
    if (!m_loaded || image.cols != 1280 || image.rows != 720 || image.type() != CV_8UC3 ||
        !context.map_geometry_verified || context.rows < 1 || context.rows > 16 || context.columns < 1 ||
        context.columns > 16 || context.nodes.size() < 3 || !weather_vision::inside(m_glyph_roi, image)) {
        result.reasons.push_back("Unavailable resources, unverified map geometry or unsupported/clipped image");
        reset();
        return result;
    }
    std::set<std::pair<int, int>> cells;
    int existing_nodes = 0;
    for (const auto& node : context.nodes) {
        if (!node.exists) {
            continue;
        }
        if (node.cell.row < 0 || node.cell.row >= context.rows || node.cell.column < 0 ||
            node.cell.column >= context.columns || !weather_vision::inside(node.icon_rect, image) ||
            !cells.emplace(node.cell.row, node.cell.column).second || node.icon_rect.y < 90 ||
            node.icon_rect.y + node.icon_rect.height > 630) {
            result.reasons.push_back("Map nodes have invalid/duplicate cells or clipped icon geometry");
            reset();
            return result;
        }
        ++existing_nodes;
    }
    if (existing_nodes < 3) {
        result.reasons.push_back("Insufficient observed map nodes");
        reset();
        return result;
    }
    const auto map_mask = weather_vision::white_mask(image, false);
    for (const auto& anchor : m_map_anchors) {
        if (!weather_vision::inside(anchor.search_roi, image) || anchor.mask.cols > anchor.search_roi.width ||
            anchor.mask.rows > anchor.search_roi.height) {
            result.reasons.push_back("Map anchor search is invalid");
            reset();
            return result;
        }
        cv::Mat scores;
        cv::matchTemplate(map_mask(make_rect<cv::Rect>(anchor.search_roi)), anchor.mask, scores, cv::TM_CCORR_NORMED);
        const double score = weather_vision::finite_max(scores);
        result.map_anchor_scores.push_back(score);
        if (score < anchor.threshold) {
            result.reasons.push_back("Map zoom-control anchors were not positively recognized");
            reset();
            return result;
        }
    }
    result.map_verified = true;
    result.main_weather = tooltip_observation(context.verified_weather_id, context.verified_weather_body, false);
    result.policy = tooltip_observation(context.verified_policy_id, context.verified_policy_body, true);
    const auto glyph_mask = weather_vision::white_mask(image(make_rect<cv::Rect>(m_glyph_roi)), true);
    const double foreground = cv::sum(glyph_mask)[0] / glyph_mask.total();
    std::vector<WeatherGlyphCandidate> ranked;
    if (foreground >= 0.03 && foreground <= 0.65) {
        for (const auto& glyph : m_glyphs) {
            WeatherGlyphCandidate candidate;
            candidate.icon_id = glyph.icon_id;
            candidate.category = glyph.category;
            for (int size = 24; size <= 46; ++size) {
                if (size > glyph_mask.cols || size > glyph_mask.rows) {
                    continue;
                }
                cv::Mat resized;
                cv::resize(glyph.mask, resized, { size, size }, 0, 0, cv::INTER_NEAREST);
                cv::Mat scores;
                cv::matchTemplate(glyph_mask, resized, scores, cv::TM_CCORR_NORMED);
                cv::Point location;
                const double score = weather_vision::finite_max(scores, &location);
                if (score > candidate.score) {
                    candidate.score = score;
                    candidate.matched_rect = { m_glyph_roi.x + location.x, m_glyph_roi.y + location.y, size, size };
                }
            }
            ranked.push_back(std::move(candidate));
        }
        std::ranges::sort(ranked, [](const auto& a, const auto& b) { return a.score > b.score; });
    }
    result.main_weather.ranked_glyphs = ranked;
    if (!ranked.empty()) {
        result.main_weather.best_score = ranked.front().score;
        result.main_weather.runner_up_score = ranked.size() > 1 ? ranked[1].score : 0.0;
        result.main_weather.margin = result.main_weather.best_score - result.main_weather.runner_up_score;
        if (result.main_weather.state != WeatherIdentityState::TooltipConfirmed &&
            ranked.front().category != "eutopia_policy" && ranked.front().score >= m_glyph_minimum_score &&
            result.main_weather.margin >= m_glyph_minimum_margin) {
            result.main_weather.state = WeatherIdentityState::GlyphCandidate;
            result.main_weather.metadata = metadata_for_icon(ranked.front().icon_id, context.verified_difficulty);
            result.main_weather.evidence_source = "main_bar_white_glyph_template_with_runner_up_margin";
            result.main_weather.reason = "Glyph candidate; body is catalog metadata, not a tooltip reading";
        }
    }
    if (result.main_weather.state == WeatherIdentityState::Unknown && !context.verified_weather_id) {
        result.main_weather.reason = "Main glyph is absent, low confidence or ambiguous";
    }

    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::Mat image_edges;
    cv::Canny(gray, image_edges, 40, 90);
    const double pitch = weather_vision::grid_pitch(context.nodes);
    for (const auto& node : context.nodes) {
        if (!node.exists || (node.ideal_source_from_preview && !*node.ideal_source_from_preview)) {
            continue;
        }
        WeatherSourceObservation candidate;
        candidate.cell = node.cell;
        candidate.icon_rect = node.icon_rect;
        if (node.ideal_source_from_preview == true) {
            candidate.state = WeatherSourceState::PreviewConfirmed;
            candidate.clearable = weather_vision::emergency(node.type) ? std::optional(true) : std::nullopt;
            candidate.evidence_source = "caller_verified_ideal_source_in_this_node_preview";
            result.source_candidates.push_back(std::move(candidate));
            continue;
        }
        if (!weather_vision::emergency(node.type) || pitch == 0.0) {
            continue;
        }
        const auto features =
            weather_vision::radial_features(image, image_edges, m_source_template, node.icon_rect, pitch);
        candidate.ring_template_score = features.score;
        candidate.radial_sectors = features.sectors;
        candidate.annular_edge_density = features.density;
        candidate.ring_fully_visible = features.fully_visible;
        candidate.evidence_source = "emergency_node_annular_template_and_multisector_radial_edges";
        if (!features.fully_visible || features.score < 0.60 || features.sectors < 10 || features.density < 0.05) {
            result.source_probes.push_back(std::move(candidate));
            continue;
        }
        candidate.state = WeatherSourceState::RadialCandidate;
        result.source_probes.push_back(candidate);
        result.source_candidates.push_back(std::move(candidate));
    }
    const auto confirmed = std::ranges::count_if(result.source_candidates, [](const auto& candidate) {
        return candidate.state == WeatherSourceState::PreviewConfirmed;
    });
    if (confirmed == 1) {
        result.selected_source = *std::ranges::find_if(result.source_candidates, [](const auto& candidate) {
            return candidate.state == WeatherSourceState::PreviewConfirmed;
        });
    }
    else if (confirmed == 0 && result.source_candidates.size() == 1) {
        auto& candidate = result.source_candidates.front();
        const auto hash = weather_vision::image_hash(image);
        const bool same_context = !context.floor_key.empty() && m_history.floor_key == context.floor_key &&
                                  m_history.cell == candidate.cell && context.frame_sequence > m_history.frame_sequence;
        if (!same_context) {
            m_history = { context.floor_key, candidate.cell, context.frame_sequence, hash, 1 };
        }
        else if (hash != m_history.image_hash) {
            ++m_history.distinct_frames;
            m_history.frame_sequence = context.frame_sequence;
            m_history.image_hash = hash;
        }
        candidate.distinct_consistent_frames = m_history.distinct_frames;
        if (m_history.distinct_frames >= 2) {
            candidate.state = WeatherSourceState::TemporallyConsistentCandidate;
        }
        result.selected_source = candidate;
    }
    else {
        reset();
        result.reasons.push_back(
            confirmed > 1 ? "Several previews claim an ideal source"
                          : "Source absent or ambiguous; absence is not confirmed");
    }
    if (!result.selected_source) {
        return result;
    }
    const bool fertile = result.main_weather.metadata && result.main_weather.metadata->icon_id == "rogue_6_weather_8";
    if (fertile) {
        result.reasons.push_back("Fertile-soil event source is not an ordinary destructible emergency source");
        if (result.main_weather.state == WeatherIdentityState::TooltipConfirmed) {
            result.selected_source->clearable = false;
        }
        return result;
    }
    if (result.main_weather.state == WeatherIdentityState::TooltipConfirmed && result.main_weather.metadata &&
        result.main_weather.metadata->scope_mode != "local_eutopia_region") {
        result.reasons.push_back("A zone-wide variation is not an ordinary source-radius effect");
        result.selected_source->clearable = result.main_weather.metadata->destructible_source;
        return result;
    }
    if (!context.verified_difficulty || *context.verified_difficulty < 2) {
        result.reasons.push_back("Source range requires a verified supported difficulty");
        return result;
    }
    result.radius = *context.verified_difficulty >= 12 ? 3 : 2;
    result.range_pattern = *result.radius == 3 ? "x-3" : "x-1";
    const auto& offsets = *result.radius == 3 ? m_large_offsets : m_small_offsets;
    for (const auto& offset : offsets) {
        const WeatherCell cell { result.selected_source->cell.row + offset.row,
                                 result.selected_source->cell.column + offset.column };
        if (cell.row >= 0 && cell.row < context.rows && cell.column >= 0 && cell.column < context.columns) {
            result.candidate_affected_cells.push_back(cell);
        }
    }
    if (result.selected_source->state == WeatherSourceState::PreviewConfirmed &&
        result.selected_source->clearable == true) {
        result.scope_known = true;
        result.affected_cells = result.candidate_affected_cells;
        result.scope_evidence_source =
            "explicit_emergency_ideal_source_preview_and_verified_difficulty_catalog_offsets";
    }
    else {
        result.reasons.push_back(
            "Radial appearance is a source candidate; a learned template is not source confirmation");
    }
    return result;
}

void asst::blackflow::BlackFlowWeatherAnalyzer::reset() noexcept
{
    m_history = {};
}

namespace asst::blackflow::weather_vision
{
std::string identity_state_name(WeatherIdentityState state)
{
    switch (state) {
    case WeatherIdentityState::GlyphCandidate:
        return "glyph_candidate";
    case WeatherIdentityState::TooltipConfirmed:
        return "tooltip_confirmed";
    default:
        return "unknown";
    }
}

std::string source_state_name(WeatherSourceState state)
{
    switch (state) {
    case WeatherSourceState::RadialCandidate:
        return "radial_candidate";
    case WeatherSourceState::TemporallyConsistentCandidate:
        return "temporal_candidate";
    case WeatherSourceState::PreviewConfirmed:
        return "preview_confirmed";
    default:
        return "unknown";
    }
}

json::value rect_json(const Rect& rect)
{
    return json::array { rect.x, rect.y, rect.width, rect.height };
}

json::value identity_json(const WeatherIdentityObservation& observation)
{
    json::array glyphs;
    for (const auto& glyph : observation.ranked_glyphs) {
        glyphs.emplace_back(
            json::object { { "icon_id", glyph.icon_id },
                           { "category", glyph.category },
                           { "score", glyph.score },
                           { "matched_rect", rect_json(glyph.matched_rect) } });
    }
    json::object result { { "state", identity_state_name(observation.state) },
                          { "identity_confirmed", observation.state == WeatherIdentityState::TooltipConfirmed },
                          { "best_score", observation.best_score },
                          { "runner_up_score", observation.runner_up_score },
                          { "margin", observation.margin },
                          { "ranked_glyphs", std::move(glyphs) },
                          { "evidence_source", observation.evidence_source },
                          { "reason", observation.reason },
                          { "metadata", nullptr } };
    if (observation.metadata) {
        const auto& item = *observation.metadata;
        json::array grades;
        for (const int grade : item.difficulty_grades) {
            grades.emplace_back(grade);
        }
        result["metadata"] =
            json::object { { "id", item.id },
                           { "icon_id", item.icon_id },
                           { "type", item.type },
                           { "category", item.category },
                           { "name", item.name },
                           { "level", item.level ? json::value(*item.level) : json::value(nullptr) },
                           { "positive", item.positive ? json::value(*item.positive) : json::value(nullptr) },
                           { "minimum_difficulty",
                             item.minimum_difficulty ? json::value(*item.minimum_difficulty) : json::value(nullptr) },
                           { "difficulty_grades", std::move(grades) },
                           { "destructible_source",
                             item.destructible_source ? json::value(*item.destructible_source) : json::value(nullptr) },
                           { "body", item.body },
                           { "activation", item.activation },
                           { "scope_mode", item.scope_mode },
                           { "source_node_rule", item.source_node_rule },
                           { "effects", item.effects },
                           { "level_from_verified_difficulty", item.level_from_verified_difficulty },
                           { "body_from_catalog", item.body_from_catalog },
                           { "tooltip_body_verified", item.tooltip_body_verified } };
    }
    return result;
}

json::value source_json(const WeatherSourceObservation& source)
{
    return json::object { { "row", source.cell.row },
                          { "column", source.cell.column },
                          { "icon_rect", rect_json(source.icon_rect) },
                          { "state", source_state_name(source.state) },
                          { "source_confirmed", source.state == WeatherSourceState::PreviewConfirmed },
                          { "ring_template_score", source.ring_template_score },
                          { "radial_sectors", source.radial_sectors },
                          { "annular_edge_density", source.annular_edge_density },
                          { "distinct_consistent_frames", source.distinct_consistent_frames },
                          { "ring_fully_visible", source.ring_fully_visible },
                          { "clearable", source.clearable ? json::value(*source.clearable) : json::value(nullptr) },
                          { "evidence_source", source.evidence_source } };
}

json::array cells_json(const std::vector<WeatherCell>& cells)
{
    json::array result;
    for (const auto& cell : cells) {
        result.emplace_back(json::object { { "row", cell.row }, { "column", cell.column } });
    }
    return result;
}
}

json::value asst::blackflow::WeatherRecognitionResult::to_json() const
{
    json::array anchors;
    for (const auto score : map_anchor_scores) {
        anchors.emplace_back(score);
    }
    json::array candidates;
    for (const auto& source : source_candidates) {
        candidates.emplace_back(weather_vision::source_json(source));
    }
    json::array probes;
    for (const auto& source : source_probes) {
        probes.emplace_back(weather_vision::source_json(source));
    }
    json::array messages;
    for (const auto& reason : reasons) {
        messages.emplace_back(reason);
    }
    return json::object {
        { "schema_version", 1 },
        { "map_verified", map_verified },
        { "map_anchor_scores", std::move(anchors) },
        { "main_weather", weather_vision::identity_json(main_weather) },
        { "policy", weather_vision::identity_json(policy) },
        { "source_candidates", std::move(candidates) },
        { "source_probes", std::move(probes) },
        { "selected_source", selected_source ? weather_vision::source_json(*selected_source) : json::value(nullptr) },
        { "scope",
          json::object { { "known", scope_known },
                         { "radius", radius ? json::value(*radius) : json::value(nullptr) },
                         { "range_pattern", range_pattern },
                         { "candidate_affected_cells", weather_vision::cells_json(candidate_affected_cells) },
                         { "affected_cells", weather_vision::cells_json(affected_cells) },
                         { "evidence_source", scope_evidence_source } } },
        { "reasons", std::move(messages) }
    };
}
