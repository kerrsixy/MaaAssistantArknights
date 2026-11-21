#include "BlackFlowMapObservationSource.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <opencv2/imgproc.hpp>
#include <optional>
#include <string_view>

#include "Config/Roguelike/BlackFlow/BlackFlowMapPerceptionResource.h"
#include "MaaUtils/ImageIo.h"
#include "Utils/WorkingDir.hpp"

namespace asst::blackflow
{
namespace
{
void set_error(std::string* error, std::string message)
{
    if (error != nullptr) {
        *error = std::move(message);
    }
}

Rect visual_rect(const perception::Node& node, const cv::Size& bounds)
{
    const int left = static_cast<int>(std::floor(node.visual_center.x - node.visual_half_width));
    const int top = static_cast<int>(std::floor(node.visual_center.y - node.visual_half_height));
    const int right = static_cast<int>(std::ceil(node.visual_center.x + node.visual_half_width));
    const int bottom = static_cast<int>(std::ceil(node.visual_center.y + node.visual_half_height));
    const cv::Rect clipped = cv::Rect(left, top, std::max(1, right - left), std::max(1, bottom - top)) &
                             cv::Rect(0, 0, bounds.width, bounds.height);
    return { clipped.x, clipped.y, clipped.width, clipped.height };
}

bool valid_artifact_set_id(std::string_view id)
{
    return !id.empty() && std::ranges::all_of(id, [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '-' || character == '_';
    });
}

bool write_image_if_present(const std::filesystem::path& path, const cv::Mat& image, std::string* error)
{
    if (image.empty()) {
        return true;
    }
    if (!MAA_NS::imwrite(path, image)) {
        set_error(error, "failed to write BlackFlow diagnostic image: " + path.string());
        return false;
    }
    return true;
}

void dashed_weather_rect(cv::Mat& image, const cv::Rect& rect, const cv::Scalar& color)
{
    constexpr int dash = 8;
    for (int x = rect.x; x < rect.x + rect.width; x += dash * 2) {
        cv::line(image, { x, rect.y }, { std::min(x + dash, rect.x + rect.width), rect.y }, color, 2);
        cv::line(
            image,
            { x, rect.y + rect.height },
            { std::min(x + dash, rect.x + rect.width), rect.y + rect.height },
            color,
            2);
    }
    for (int y = rect.y; y < rect.y + rect.height; y += dash * 2) {
        cv::line(image, { rect.x, y }, { rect.x, std::min(y + dash, rect.y + rect.height) }, color, 2);
        cv::line(
            image,
            { rect.x + rect.width, y },
            { rect.x + rect.width, std::min(y + dash, rect.y + rect.height) },
            color,
            2);
    }
}

void draw_weather_overlay(
    cv::Mat& image,
    const WeatherRecognitionResult& weather,
    const std::vector<perception::Node>& nodes)
{
    if (image.empty()) {
        return;
    }
    const cv::Scalar color = weather.scope_known ? cv::Scalar(80, 220, 80) : cv::Scalar(0, 180, 255);
    std::vector<double> pitches;
    for (const auto& first : nodes) {
        for (const auto& second : nodes) {
            if (first.row == second.row && second.column > first.column) {
                const double pitch = (second.center.x - first.center.x) / (second.column - first.column);
                if (pitch > 0.0) {
                    pitches.push_back(pitch);
                }
            }
        }
    }
    std::ranges::sort(pitches);
    const int half = pitches.empty() ? 40 : static_cast<int>(pitches[pitches.size() / 2] * 0.44);
    const auto& cells = weather.scope_known ? weather.affected_cells : weather.candidate_affected_cells;
    for (const auto& cell : cells) {
        const auto node = std::ranges::find_if(nodes, [&](const auto& item) {
            return item.row == cell.row && item.column == cell.column;
        });
        if (node == nodes.end()) {
            continue;
        }
        const cv::Rect region = cv::Rect(
                                    static_cast<int>(std::lround(node->center.x)) - half,
                                    static_cast<int>(std::lround(node->center.y)) - half,
                                    half * 2,
                                    half * 2) &
                                cv::Rect(0, 0, image.cols, image.rows);
        if (region.empty()) {
            continue;
        }
        cv::Mat tint(region.size(), image.type(), color);
        cv::addWeighted(image(region), 0.87, tint, 0.13, 0.0, image(region));
        if (weather.scope_known) {
            cv::rectangle(image, region, color, 2);
        }
        else {
            dashed_weather_rect(image, region, color);
        }
    }
    if (weather.selected_source) {
        const auto& source = *weather.selected_source;
        const bool confirmed = source.state == WeatherSourceState::PreviewConfirmed;
        const cv::Point center(
            source.icon_rect.x + source.icon_rect.width / 2,
            source.icon_rect.y + source.icon_rect.height / 2);
        cv::circle(image, center, 44, color, 3, cv::LINE_AA);
        cv::putText(
            image,
            confirmed ? "SOURCE CONFIRMED" : "SOURCE CANDIDATE",
            { std::max(0, center.x - 90), std::max(20, center.y - 50) },
            cv::FONT_HERSHEY_SIMPLEX,
            0.43,
            color,
            1,
            cv::LINE_AA);
    }
    std::string label = "WX UNKNOWN | SCOPE UNKNOWN";
    if (weather.main_weather.metadata) {
        label = std::string(
                    weather.main_weather.state == WeatherIdentityState::TooltipConfirmed ? "WX CONFIRMED "
                                                                                         : "WX CANDIDATE ") +
                weather.main_weather.metadata->icon_id;
        label += weather.scope_known ? " | SCOPE CONFIRMED" : " | SCOPE UNKNOWN";
    }
    cv::putText(image, label, { 25, 150 }, cv::FONT_HERSHEY_SIMPLEX, 0.55, color, 2, cv::LINE_AA);
    if (!cells.empty() && !weather.scope_known) {
        cv::putText(
            image,
            "DASHED RANGE = CANDIDATE; NOT USED AS ROUTING FACT",
            { 25, 173 },
            cv::FONT_HERSHEY_SIMPLEX,
            0.46,
            color,
            1,
            cv::LINE_AA);
    }
}

} // namespace

bool BlackFlowMapObservationSource::prepare(std::string* error)
{
    if (!m_weather_initialization_attempted) {
        m_weather_initialization_attempted = true;
        m_weather_analyzer.load(
            ResDir.get() / "roguelike/BlackFlow/weather_catalog.json",
            ResDir.get() / "template/Roguelike/BlackFlow/Weather/manifest.json",
            &m_weather_initialization_error);
    }
    if (m_analyzer != nullptr) {
        return true;
    }
    if (!m_initialization_error.empty()) {
        set_error(error, m_initialization_error);
        return false;
    }
    try {
        std::string current_error;
        m_analyzer = BlackFlowMapPerception.acquire(current_error);
        if (m_analyzer == nullptr) {
            m_initialization_error = current_error.empty() ? "BlackFlow map perception is unavailable" : current_error;
            set_error(error, m_initialization_error);
            return false;
        }
        m_initialization_error.clear();
        return true;
    }
    catch (const std::exception& exception) {
        m_initialization_error = "BlackFlow map perception initialization failed: " + std::string(exception.what());
    }
    catch (...) {
        m_initialization_error = "BlackFlow map perception initialization failed: unknown exception";
    }
    set_error(error, m_initialization_error);
    return false;
}

void BlackFlowMapObservationSource::release() noexcept
{
    m_analyzer.reset();
    m_initialization_error.clear();
    m_last_result = {};
    m_last_attempt_id.clear();
    m_last_accepted_observation_id.clear();
    m_accumulated_screenshot_us = 0;
    m_accumulated_recognition_us = 0;
    m_last_attempt_count = 0;
    m_last_retry_count = 0;
    m_weather_analyzer = {};
    m_weather_initialization_attempted = false;
    m_weather_initialization_error.clear();
    m_last_weather = {};
    m_last_weather_us = 0;
}

bool BlackFlowMapObservationSource::recognize(
    const cv::Mat& image,
    const BlackFlowObservationRequest& request,
    BlackFlowMapObservation& observation,
    FactStore& observed_facts,
    std::string* error)
{
    observed_facts.clear();
    const int floor = request.floor;
    const int attempt_count = std::max(1, request.attempt_count);
    if (attempt_count == 1) {
        m_accumulated_screenshot_us = 0;
        m_accumulated_recognition_us = 0;
    }

    BlackFlowMapObservation next;
    next.sequence = ++m_sequence;
    next.viewport_revision = next.sequence;
    next.observation_id = "BF-O" + std::to_string(next.sequence);
    next.floor = floor;
    next.floor_from_ocr = true;
    next.coverage = ObservationCoverage::FullMap;
    next.attempt_count = attempt_count;
    next.retry_count = attempt_count - 1;
    m_last_attempt_id = next.observation_id;
    m_last_attempt_count = next.attempt_count;
    m_last_retry_count = next.retry_count;

    perception::MapRecognitionResult result;
    result.floor = floor;
    bool timing_recorded = false;
    const auto record_timing = [&]() {
        if (timing_recorded) {
            return;
        }
        m_accumulated_screenshot_us += std::max<std::int64_t>(0, request.capture_us) + result.normalization_us;
        m_accumulated_recognition_us += result.recognition_us;
        next.screenshot_us = m_accumulated_screenshot_us;
        next.recognition_us = m_accumulated_recognition_us;
        timing_recorded = true;
    };

    try {
        if (m_analyzer == nullptr && !prepare(error)) {
            result.error = m_initialization_error;
            result.captured_bgr = image.clone();
            record_timing();
            m_last_result = std::move(result);
            observation = std::move(next);
            return false;
        }

        result = m_analyzer->recognize(image, floor, m_diagnostics.level != DiagnosticLevel::Normal);
        record_timing();
        if (!result.ok) {
            set_error(error, result.error);
            if (m_diagnostics.level != DiagnosticLevel::Normal && (attempt_count == 1 || attempt_count == 5)) {
                const auto directory = UserDir.get() / "BlackFlowEvidence" / "recognition_failures" /
                                       ("floor-" + std::to_string(floor) + "-" + next.observation_id);
                std::filesystem::create_directories(directory);
                std::string image_error;
                write_image_if_present(directory / "original.png", image, &image_error);
                cv::Mat overlay = m_analyzer->draw_overlay(result);
                if (overlay.empty()) {
                    overlay = image.clone();
                }
                write_image_if_present(directory / "recognition.png", overlay, &image_error);
                cv::putText(
                    overlay,
                    "MAP VALIDATION FAILED - ROUTE PENDING",
                    { 30, 100 },
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.8,
                    { 0, 0, 255 },
                    2);
                write_image_if_present(directory / "route_pending.png", overlay, &image_error);
                json::object failure {
                    { "observation_id", next.observation_id },
                    { "floor", floor },
                    { "attempt", attempt_count },
                    { "recognition_error", result.error },
                    { "route_status", "pending" },
                    { "image_error", image_error },
                };
                std::ofstream(directory / "failure.json", std::ios::binary) << json::value(std::move(failure)).format();
            }
            // 保留最近一次带有效当前位置的地图帧，供诊断三图使用；
            // 过渡到战斗/选人页的失败帧不能覆盖它，否则会把选人页保存成地图。
            if (!m_last_result.ok || m_last_result.node_detection.current_marker_node_id < 0) {
                m_last_result = std::move(result);
            }
            observation = std::move(next);
            return false;
        }

        next.rows = result.rows;
        next.columns = result.columns;
        next.covered_positions = BlackFlowObservationAdapter::expected_grid_positions(floor, result.columns);
        next.recognition_ok = true;
        next.allow_exit_supplement = result.allow_exit_supplement;
        next.graph_connected = result.edge_detection.graph_connected;
        next.current_marker_temporary_id = result.node_detection.current_marker_node_id;
        next.current_marker_score = result.node_detection.current_marker_score;
        WeatherFrameContext weather_context;
        weather_context.frame_sequence = next.sequence;
        weather_context.rows = result.rows;
        weather_context.columns = result.columns;
        weather_context.map_geometry_verified = true;
        weather_context.verified_difficulty = request.verified_difficulty;
        // No run identity, live tooltip or source-preview evidence is supplied
        // here yet. Do not fabricate temporal confirmation or semantic facts.
        for (const auto& node : result.node_detection.nodes) {
            weather_context.nodes.push_back(
                { { node.row, node.column },
                  visual_rect(node, result.normalized_bgr.size()),
                  node.type,
                  node.exists,
                  std::nullopt });
        }
        const auto weather_started = std::chrono::steady_clock::now();
        WeatherRecognitionResult weather;
        if (m_weather_analyzer.loaded()) {
            weather = m_weather_analyzer.analyze(result.normalized_bgr, weather_context);
        }
        else {
            weather.reasons.push_back("Weather resources unavailable: " + m_weather_initialization_error);
        }
        next.weather_recognition_us =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - weather_started)
                .count();
        m_accumulated_recognition_us += next.weather_recognition_us;
        next.recognition_us = m_accumulated_recognition_us;
        next.weather = weather;
        if (weather.main_weather.state == WeatherIdentityState::TooltipConfirmed && weather.main_weather.metadata) {
            const auto& metadata = *weather.main_weather.metadata;
            next.floor_buff_id = metadata.id;
            next.floor_buff_name = metadata.name;
            next.floor_buff_type = metadata.type;
            next.floor_buff_effect = metadata.body;
            next.floor_buff_positive = metadata.positive;
        }
        next.floor_buff_scope_confirmed = weather.scope_known;

        next.nodes.reserve(result.node_detection.nodes.size());
        for (const auto& node : result.node_detection.nodes) {
            next.nodes.emplace_back(
                PerceptionNodeObservation {
                    node.id,
                    { node.row, node.column },
                    node.exists,
                    node.type,
                    node.display_name,
                    std::max(node.confidence, node.existence_confidence),
                    node.existence_source,
                    visual_rect(node, result.normalized_bgr.size()),
                    std::nullopt,
                    node.marker_type,
                    node.marker_display_name,
                    node.marker_score,
                    false,
                    weather.scope_known &&
                        std::ranges::find(weather.affected_cells, WeatherCell { node.row, node.column }) !=
                            weather.affected_cells.end(),
                    std::nullopt,
                });
        }

        next.edges.reserve(result.edge_detection.edges.size());
        for (const auto& edge : result.edge_detection.edges) {
            next.edges.emplace_back(
                PerceptionEdgeObservation {
                    edge.node_a,
                    edge.node_b,
                    edge.connected,
                    edge.cnn_connected,
                    edge.forced_by_connectivity_constraint,
                    edge.calibrated_probability,
                    edge.decision_source,
                });
        }

        observed_facts = observed_floor_buff_facts(next);
        observation = std::move(next);
        m_last_accepted_observation_id = observation.observation_id;
        m_last_weather = std::move(weather);
        m_last_weather_us = observation.weather_recognition_us;
        m_last_result = std::move(result);
        return true;
    }
    catch (const std::exception& exception) {
        result.ok = false;
        result.error = "BlackFlow map recognition failed: " + std::string(exception.what());
    }
    catch (...) {
        result.ok = false;
        result.error = "BlackFlow map recognition failed: unknown exception";
    }
    if (result.captured_bgr.empty() && !image.empty()) {
        result.captured_bgr = image.clone();
    }
    record_timing();
    set_error(error, result.error);
    if (!m_last_result.ok || m_last_result.node_detection.current_marker_node_id < 0) {
        m_last_result = std::move(result);
    }
    observation = std::move(next);
    return false;
}

void BlackFlowMapObservationSource::configure_diagnostics(const DiagnosticSettings& settings)
{
    m_diagnostics = settings;
}

bool BlackFlowMapObservationSource::persist_diagnostics(const DiagnosticArtifactRequest& request, std::string* error)
{
    if (!valid_artifact_set_id(request.artifact_set_id)) {
        set_error(error, "BlackFlow diagnostic artifact set id is invalid");
        return false;
    }
    // 诊断请求可能在节点战斗/选人页失败后才到达。没有有效的当前地图标记
    // 时，禁止把预备干员/选人画面写成地图原图，等待下一次真正的地图观测。
    if (!m_last_result.ok || m_last_result.node_detection.current_marker_node_id < 0 ||
        m_last_result.node_detection.nodes.empty()) {
        return true;
    }
    if (!request.observation_id.empty() && request.observation_id != m_last_accepted_observation_id) {
        // 排队中的旧决策不能画到新识别帧上。
        return true;
    }
    try {
        std::string round_id = request.artifact_set_id;
        if (const auto separator = round_id.find('-', 5); separator != std::string::npos) {
            round_id.erase(separator);
        }
        const auto directory = UserDir.get() / "BlackFlowEvidence" / ("round-" + round_id) /
                               ("floor-" + std::to_string(m_last_result.floor));
        std::filesystem::create_directories(directory);
        json::object snapshot = request.snapshot;
        snapshot["accepted_observation_id"] = request.observation_id;
        snapshot["recognition_attempt_id"] = m_last_attempt_id;
        snapshot["recognition_attempt_count"] = m_last_attempt_count;
        snapshot["recognition_retry_count"] = m_last_retry_count;
        snapshot["recognition_screenshot_us"] = m_accumulated_screenshot_us;
        snapshot["recognition_us"] = m_accumulated_recognition_us;
        snapshot["weather_recognition_us"] = m_last_weather_us;
        snapshot["weather_observation_id"] = m_last_accepted_observation_id;
        snapshot["weather_recognition"] = m_last_weather.to_json();
        snapshot["recognition_floor"] = m_last_result.floor;
        snapshot["recognition_floor_source"] = "next_level_ocr";
        snapshot["route_status"] = request.snapshot.contains("planned_route_steps") ? "planned" : "pending";
        if (!m_last_result.error.empty()) {
            snapshot["recognition_error"] = m_last_result.error;
        }
        std::ofstream snapshot_file(directory / "snapshot.json", std::ios::binary);
        if (!snapshot_file) {
            set_error(error, "failed to create BlackFlow diagnostic snapshot");
            return false;
        }
        snapshot_file << json::value(std::move(snapshot)).format();
        std::ofstream weather_file(directory / "weather_recognition.json", std::ios::binary);
        if (!weather_file) {
            set_error(error, "failed to create BlackFlow weather diagnostic snapshot");
            return false;
        }
        json::object weather_snapshot = m_last_weather.to_json().as_object();
        weather_snapshot["observation_id"] = m_last_accepted_observation_id;
        weather_snapshot["recognition_us"] = m_last_weather_us;
        weather_file << json::value(std::move(weather_snapshot)).format();
        if (request.include_images) {
            cv::Mat overlay = m_last_result.overlay_bgr.clone();
            if (overlay.empty()) {
                overlay = m_analyzer != nullptr ? m_analyzer->draw_overlay(m_last_result)
                                                : m_last_result.captured_bgr.clone();
            }
            draw_weather_overlay(overlay, m_last_weather, m_last_result.node_detection.nodes);
            // 同一层固定保留三张图：地图原图、识别/Buff 叠加图、路线规划图。
            // 当前路线规划图以识别叠加图为底，后续规划步骤会继续在该图上补线。
            if (!write_image_if_present(directory / "map_original.png", m_last_result.captured_bgr, error) ||
                !write_image_if_present(directory / "recognition_overlay.png", overlay, error)) {
                return false;
            }
            const auto route_path = directory / "recommended_route.png";
            if (request.snapshot.contains("planned_route_steps")) {
                cv::Mat route = overlay.clone();
                const auto point_for = [&](const json::value& node) -> std::optional<cv::Point> {
                    const int row = node.at("row").as_integer();
                    const int column = node.at("column").as_integer();
                    const auto& nodes = m_last_result.node_detection.nodes;
                    const auto found = std::find_if(nodes.begin(), nodes.end(), [&](const auto& detected) {
                        return detected.row == row && detected.column == column;
                    });
                    if (found == nodes.end()) {
                        return std::nullopt;
                    }
                    return cv::Point(std::lround(found->center.x), std::lround(found->center.y));
                };
                for (const auto& step : request.snapshot.at("planned_route_steps").as_array()) {
                    const auto& path = step.at("path").as_array();
                    auto previous = point_for(step.at("source"));
                    for (const auto& node : path) {
                        const auto current = point_for(node);
                        if (previous && current && *previous != *current) {
                            cv::arrowedLine(route, *previous, *current, cv::Scalar(0, 80, 255), 5, cv::LINE_AA, 0, 0.2);
                        }
                        previous = current;
                    }
                }
                if (!write_image_if_present(route_path, route, error)) {
                    return false;
                }
            }
            else {
                cv::Mat route = overlay.clone();
                cv::putText(
                    route,
                    "Route pending for " + request.observation_id,
                    { 30, 110 },
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.7,
                    cv::Scalar(0, 200, 255),
                    2,
                    cv::LINE_AA);
                if (!write_image_if_present(route_path, route, error)) {
                    return false;
                }
            }
        }
        return true;
    }
    catch (const std::exception& exception) {
        set_error(error, "BlackFlow diagnostic persistence failed: " + std::string(exception.what()));
        return false;
    }
    catch (...) {
        set_error(error, "BlackFlow diagnostic persistence failed: unknown exception");
        return false;
    }
}
} // namespace asst::blackflow
