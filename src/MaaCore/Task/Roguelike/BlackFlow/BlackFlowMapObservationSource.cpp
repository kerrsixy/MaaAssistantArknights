#include "BlackFlowMapObservationSource.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <opencv2/imgproc.hpp>

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

std::optional<std::vector<cv::Point>> detect_temporal_buff_scope(
    const cv::Mat& image,
    const std::deque<cv::Mat>& frames,
    const std::vector<perception::Node>& nodes)
{
    if (frames.size() < 4 || image.empty() || nodes.empty()) return std::nullopt;
    cv::Mat mean = cv::Mat::zeros(image.size(), CV_32F);
    for (const auto& frame : frames) { cv::Mat f; frame.convertTo(f, CV_32F); mean += f; }
    mean /= static_cast<float>(frames.size());
    cv::Mat variance = cv::Mat::zeros(image.size(), CV_32F);
    for (const auto& frame : frames) { cv::Mat f; frame.convertTo(f, CV_32F); cv::Mat d = f - mean; variance += d.mul(d); }
    variance /= static_cast<float>(frames.size());
    std::vector<double> x_positions;
    std::vector<double> y_positions;
    std::vector<double> horizontal_gaps;
    for (const auto& node : nodes) {
        x_positions.push_back(node.visual_center.x);
        y_positions.push_back(node.visual_center.y);
    }
    std::ranges::sort(x_positions);
    std::ranges::sort(y_positions);
    for (std::size_t index = 1; index < x_positions.size(); ++index) {
        const double gap = x_positions[index] - x_positions[index - 1];
        if (gap > 1.0) horizontal_gaps.push_back(gap);
    }
    if (horizontal_gaps.empty()) return std::nullopt;
    std::ranges::sort(horizontal_gaps);
    const double pitch = horizontal_gaps[horizontal_gaps.size() / 2];
    const double x_cut = x_positions[static_cast<std::size_t>((x_positions.size() - 1) * 0.70)];
    const int left = std::max(0, static_cast<int>(std::lround(x_cut - 0.60 * pitch)));
    const int right = std::min(image.cols, static_cast<int>(std::lround(x_positions.back() + 0.60 * pitch)));
    const int top = std::max(0, static_cast<int>(std::lround(y_positions.front() - 0.60 * pitch)));
    const int bottom = std::min(image.rows, static_cast<int>(std::lround(y_positions.back() + 0.60 * pitch)));
    if (left >= right || top >= bottom) return std::nullopt;

    cv::Mat flat = variance.reshape(1, 1), sorted;
    cv::sort(flat, sorted, cv::SORT_ASCENDING);
    const auto index = std::min(static_cast<std::size_t>(sorted.total() - 1), static_cast<std::size_t>(sorted.total() * 0.90));
    cv::Mat mask;
    cv::threshold(variance, mask, sorted.at<float>(0, static_cast<int>(index)), 255.0, cv::THRESH_BINARY);
    mask.convertTo(mask, CV_8U);
    cv::Mat roi_mask = cv::Mat::zeros(mask.size(), CV_8U);
    roi_mask(cv::Rect(left, top, right - left, bottom - top)).setTo(255);
    mask &= roi_mask;
    const int kernel_size = std::max(3, static_cast<int>(std::lround(pitch * 0.25)) | 1);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_ELLIPSE, { kernel_size, kernel_size }));

    cv::Mat labels, stats, centroids;
    const int component_count = cv::connectedComponentsWithStats(mask, labels, stats, centroids, 8, CV_32S);
    int best_label = 0;
    std::size_t best_nodes = 0;
    int best_area = 0;
    for (int label = 1; label < component_count; ++label) {
        std::size_t covered = 0;
        for (const auto& node : nodes) {
            const int x = std::clamp(static_cast<int>(std::lround(node.visual_center.x)), 0, labels.cols - 1);
            const int y = std::clamp(static_cast<int>(std::lround(node.visual_center.y)), 0, labels.rows - 1);
            covered += labels.at<int>(y, x) == label ? 1U : 0U;
        }
        const int area = stats.at<int>(label, cv::CC_STAT_AREA);
        if (covered > best_nodes || (covered == best_nodes && area > best_area)) {
            best_label = label;
            best_nodes = covered;
            best_area = area;
        }
    }
    if (best_label == 0 || best_nodes == 0) return std::nullopt;
    cv::Mat selected = labels == best_label;
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(selected, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) return std::nullopt;
    return *std::ranges::max_element(contours, [](const auto& lhs, const auto& rhs) {
        return cv::contourArea(lhs) < cv::contourArea(rhs);
    });
}
} // namespace

bool BlackFlowMapObservationSource::prepare(std::string* error)
{
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
    m_accumulated_screenshot_us = 0;
    m_accumulated_recognition_us = 0;
    m_last_attempt_count = 0;
    m_last_retry_count = 0;
    m_temporal_gray_frames.clear();
    m_last_temporal_gray.release();
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
        cv::Mat gray;
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        if (!m_last_temporal_gray.empty() && m_last_temporal_gray.size() == gray.size()) {
            cv::Mat delta;
            cv::absdiff(gray, m_last_temporal_gray, delta);
            if (cv::mean(delta)[0] > 18.0) m_temporal_gray_frames.clear();
        }
        m_last_temporal_gray = gray.clone();
        m_temporal_gray_frames.push_back(gray);
        while (m_temporal_gray_frames.size() > 8) m_temporal_gray_frames.pop_front();
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
            m_last_result = std::move(result);
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
        const auto buff_scope = detect_temporal_buff_scope(image, m_temporal_gray_frames, result.node_detection.nodes);
        if (buff_scope.has_value()) {
            next.floor_buff_id = "temporal_buff_scope";
            next.floor_buff_scope_confirmed = true;
        }

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
                    buff_scope.has_value() && cv::pointPolygonTest(*buff_scope, node.visual_center, false) >= 0.0,
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

        observation = std::move(next);
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
    m_last_result = std::move(result);
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
    try {
        const auto directory = UserDir.get() / "debug" / "BlackFlow" / request.artifact_set_id;
        std::filesystem::create_directories(directory);
        json::object snapshot = request.snapshot;
        snapshot["accepted_observation_id"] = request.observation_id;
        snapshot["recognition_attempt_id"] = m_last_attempt_id;
        snapshot["recognition_attempt_count"] = m_last_attempt_count;
        snapshot["recognition_retry_count"] = m_last_retry_count;
        snapshot["recognition_screenshot_us"] = m_accumulated_screenshot_us;
        snapshot["recognition_us"] = m_accumulated_recognition_us;
        snapshot["recognition_floor"] = m_last_result.floor;
        snapshot["recognition_floor_source"] = "next_level_ocr";
        if (!m_last_result.error.empty()) {
            snapshot["recognition_error"] = m_last_result.error;
        }
        std::ofstream snapshot_file(directory / "snapshot.json", std::ios::binary);
        if (!snapshot_file) {
            set_error(error, "failed to create BlackFlow diagnostic snapshot");
            return false;
        }
        snapshot_file << json::value(std::move(snapshot)).format();
        if (request.include_images) {
            cv::Mat overlay = m_last_result.overlay_bgr;
            if (overlay.empty()) {
                overlay = m_analyzer != nullptr ? m_analyzer->draw_overlay(m_last_result)
                                                : m_last_result.captured_bgr.clone();
            }
            if (!write_image_if_present(directory / "captured.png", m_last_result.captured_bgr, error) ||
                !write_image_if_present(directory / "overlay.png", overlay, error)) {
                return false;
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
