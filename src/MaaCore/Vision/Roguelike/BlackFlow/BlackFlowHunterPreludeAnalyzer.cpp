#include "BlackFlowHunterPreludeAnalyzer.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include <meojson/json.hpp>

#include "MaaUtils/ImageIo.h"
#include "MaaUtils/NoWarningCV.hpp"

namespace asst::blackflow::hunter_vision
{
bool inside(const Rect& r, const cv::Mat& image)
{
    return r.x >= 0 && r.y >= 0 && r.width > 0 && r.height > 0 && r.x <= image.cols - r.width &&
           r.y <= image.rows - r.height;
}

bool overlaps(const Rect& a, const Rect& b)
{
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

Rect rect(const json::value& v)
{
    if (!v.is_array() || v.as_array().size() != 4) {
        return {};
    }
    const auto x = v.find<int>(0);
    const auto y = v.find<int>(1);
    const auto width = v.find<int>(2);
    const auto height = v.find<int>(3);
    return x && y && width && height ? Rect { *x, *y, *width, *height } : Rect {};
}

cv::Mat patch(const cv::Mat& image, const Rect& r)
{
    return image(cv::Rect(r.x, r.y, r.width, r.height));
}

bool simple_file(const std::string& s)
{
    return !s.empty() && s != "." && s != ".." && std::ranges::all_of(s, [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.';
    });
}

cv::Mat read_patch(const std::filesystem::path& folder, const std::string& file)
{
    return simple_file(file) ? MAA_NS::imread(folder / MAA_NS::path(file)) : cv::Mat {};
}

double correlation(const cv::Mat& image, const cv::Mat& reference)
{
    cv::Mat resized;
    cv::resize(image, resized, reference.size(), 0, 0, cv::INTER_AREA);
    cv::Scalar mean, deviation;
    cv::meanStdDev(resized, mean, deviation);
    if (deviation[0] + deviation[1] + deviation[2] < 9) {
        return -1;
    }
    cv::Mat scores;
    cv::matchTemplate(resized, reference, scores, cv::TM_CCOEFF_NORMED);
    const double score = scores.at<float>(0, 0);
    return std::isfinite(score) ? score : -1;
}

double best_score(const cv::Mat& image, const std::vector<cv::Mat>& references)
{
    double score = -1;
    for (const auto& reference : references) {
        score = std::max(score, correlation(image, reference));
    }
    return score;
}

double luminance(const cv::Mat& image)
{
    cv::Mat grey;
    cv::cvtColor(image, grey, cv::COLOR_BGR2GRAY);
    return cv::mean(grey)[0];
}
}

bool asst::blackflow::BlackFlowHunterPreludeAnalyzer::load(
    const std::filesystem::path& manifest_path,
    std::string* error)
{
    m_loaded = false;
    m_anchors.clear();
    m_head_templates.clear();
    m_sink_rois.clear();
    m_spawns = {};
    reset();
    const auto fail = [error](const std::string& why) {
        if (error) {
            *error = why;
        }
        return false;
    };
    const auto data = json::open(manifest_path);
    if (!data || data->get("schema_version", 0) != 2 || data->get("native_width", 0) != 1280 ||
        data->get("native_height", 0) != 720) {
        return fail("Hunter reference manifest or native dimensions are invalid");
    }
    const auto spawns = data->find<json::array>("spawn_models");
    const auto anchors = data->find<json::array>("anchors");
    if (!spawns || spawns->size() != 2 || !anchors || anchors->size() < 3) {
        return fail("Hunter reference patches are incomplete");
    }
    const auto folder = manifest_path.parent_path();
    const cv::Mat canvas(720, 1280, CV_8UC3);
    for (std::size_t i = 0; i < 2; ++i) {
        auto& model = m_spawns[i];
        const auto& value = (*spawns)[i];
        model.roi = hunter_vision::rect(value.get("roi", json::array {}));
        if (!hunter_vision::inside(model.roi, canvas)) {
            return fail("Hunter spawn reference rectangle is invalid");
        }
        for (const auto& [key, target] :
             std::vector<std::pair<std::string, std::vector<cv::Mat>*>> { { "present", &model.present },
                                                                          { "absent", &model.absent } }) {
            const auto files = value.find<std::vector<std::string>>(key);
            if (!files || files->empty()) {
                return fail("Both positive and empty spawn references are required");
            }
            for (const auto& file : *files) {
                auto image = hunter_vision::read_patch(folder, file);
                if (image.empty() || image.cols != model.roi.width || image.rows != model.roi.height) {
                    return fail("Hunter spawn patch is missing or has wrong dimensions");
                }
                target->emplace_back(std::move(image));
            }
        }
    }
    for (const auto& value : *anchors) {
        Anchor anchor;
        anchor.roi = hunter_vision::rect(value.get("roi", json::array {}));
        anchor.image = hunter_vision::read_patch(folder, value.get("file", std::string()));
        anchor.min_correlation = value.get("min_correlation", 0.9);
        anchor.min_luminance_ratio = value.get("min_luminance_ratio", 0.72);
        if (!hunter_vision::inside(anchor.roi, canvas) || anchor.image.empty() ||
            anchor.image.cols != anchor.roi.width || anchor.image.rows != anchor.roi.height ||
            anchor.min_correlation < 0.5 || anchor.min_correlation > 1 || anchor.min_luminance_ratio < 0.5 ||
            anchor.min_luminance_ratio > 1) {
            return fail("Hunter battle anchor is invalid");
        }
        m_anchors.emplace_back(std::move(anchor));
    }
    m_lower_roi = hunter_vision::rect(data->get("lower_roi", json::array {}));
    if (!hunter_vision::inside(m_lower_roi, canvas)) {
        return fail("Hunter lower search region or candidate threshold is invalid");
    }
    const auto heads = data->find<std::vector<std::string>>("head_templates");
    const auto sinks = data->find<json::array>("sink_head_rois");
    m_head_present_threshold = data->get("head_present_threshold", 0.68);
    m_head_absent_threshold = data->get("head_absent_threshold", 0.50);
    m_min_clear_frames = data->get("min_clear_frames", 3);
    m_min_clear_duration_ms = static_cast<std::uint64_t>(data->get("min_clear_duration_ms", 600));
    if (!heads || heads->empty() || !sinks || sinks->empty() || m_head_present_threshold < 0.65 ||
        m_head_present_threshold > 0.95 || m_head_absent_threshold < 0.2 || m_head_absent_threshold > 0.55 ||
        m_head_present_threshold - m_head_absent_threshold < 0.1 || m_min_clear_frames < 3 || m_min_clear_frames > 20 ||
        m_min_clear_duration_ms < 500 || m_min_clear_duration_ms > 10'000) {
        return fail("Hunter head model or conservative temporal thresholds are invalid");
    }
    for (const auto& file : *heads) {
        auto image = hunter_vision::read_patch(folder, file);
        if (image.empty() || image.cols < 20 || image.rows < 20 || image.cols > 100 || image.rows > 100) {
            return fail("Hunter head patch is missing or has unsupported size");
        }
        m_head_templates.emplace_back(std::move(image));
    }
    for (const auto& value : *sinks) {
        const auto sink = hunter_vision::rect(value);
        if (!hunter_vision::inside(sink, canvas)) {
            return fail("Hunter sink rectangle is clipped");
        }
        m_sink_rois.emplace_back(sink);
    }
    m_loaded = true;
    return true;
}

void asst::blackflow::BlackFlowHunterPreludeAnalyzer::reset() noexcept
{
    m_battle_key.clear();
    m_last_sequence = 0;
    m_has_sequence = false;
    m_last_observable_frame.release();
    m_empty_spawn_frames = 0;
    m_tracks.clear();
    m_empty_lower_frames = 0;
    m_empty_lower_start_ms = 0;
    m_last_observed_at_ms = 0;
}

asst::blackflow::HunterPreludeObservation asst::blackflow::BlackFlowHunterPreludeAnalyzer::analyze(
    const cv::Mat& image,
    const HunterPreludeFrameContext& context)
{
    HunterPreludeObservation result;
    const auto unknown = [&](const std::string& why) {
        result.frame_observable = false;
        result.lower_presence = HunterPresence::Unknown;
        result.full_search_coverage_verified = false;
        result.all_hunters_cleared = false;
        result.bait_retreat_allowed = false;
        result.observed_tracks = static_cast<int>(m_tracks.size());
        result.tracks_last_seen_at_sink = static_cast<int>(
            std::ranges::count_if(m_tracks, [](const Track& track) { return track.last_seen_at_sink; }));
        for (auto& spawn : result.spawns) {
            spawn.presence = HunterPresence::Unknown;
        }
        m_empty_spawn_frames = 0;
        m_empty_lower_frames = 0;
        m_empty_lower_start_ms = 0;
        m_last_observable_frame.release();
        result.reasons.emplace_back(why);
        return result;
    };
    if (!m_loaded || image.empty() || image.cols != 1280 || image.rows != 720 || image.type() != CV_8UC3 ||
        context.battle_key.empty() || !context.normal_battle_confirmed || !context.stage_geometry_verified) {
        return unknown("Hunter frame, battle context or verified geometry is unavailable");
    }
    if (context.battle_key != m_battle_key) {
        reset();
        m_battle_key = context.battle_key;
    }
    if (m_has_sequence && context.frame_sequence <= m_last_sequence) {
        return unknown("Hunter frame sequence is stale or repeated");
    }
    m_has_sequence = true;
    m_last_sequence = context.frame_sequence;
    const auto& areas = context.spawn_rois;
    if (!areas.empty() && areas.size() != 2) {
        return unknown("Exactly two projected spawn rectangles are required");
    }
    const Rect lower = context.lower_search_roi.empty() ? m_lower_roi : context.lower_search_roi;
    if (!hunter_vision::inside(lower, image)) {
        return unknown("Hunter lower search region is clipped");
    }
    for (const auto& anchor : m_anchors) {
        const auto current = hunter_vision::patch(image, anchor.roi);
        if (hunter_vision::correlation(current, anchor.image) < anchor.min_correlation ||
            hunter_vision::luminance(current) / std::max(1.0, hunter_vision::luminance(anchor.image)) <
                anchor.min_luminance_ratio) {
            return unknown("Hunter scene anchor is obscured, dimmed, paused, selected or not this battle");
        }
    }
    for (std::size_t i = 0; i < 2; ++i) {
        result.spawns[i].roi = areas.empty() ? m_spawns[i].roi : areas[i];
        if (!hunter_vision::inside(result.spawns[i].roi, image)) {
            return unknown("Hunter projected spawn rectangle is clipped");
        }
        for (const auto& blocked : context.occlusion_rois) {
            if (hunter_vision::overlaps(result.spawns[i].roi, blocked) || hunter_vision::overlaps(lower, blocked)) {
                return unknown("Hunter observation is covered by an explicit overlay or occlusion");
            }
        }
    }
    result.frame_observable = true;
    for (std::size_t i = 0; i < 2; ++i) {
        auto& slot = result.spawns[i];
        const auto current = hunter_vision::patch(image, slot.roi);
        slot.present_score = hunter_vision::best_score(current, m_spawns[i].present);
        slot.absent_score = hunter_vision::best_score(current, m_spawns[i].absent);
        if (slot.present_score >= 0.82 && slot.present_score - slot.absent_score >= 0.12) {
            slot.presence = HunterPresence::Present;
        }
        else if (slot.absent_score >= 0.80 && slot.absent_score - slot.present_score >= 0.10) {
            slot.presence = HunterPresence::Absent;
        }
    }
    // The caller may widen the projected search envelope, but narrowing it cannot establish absence.
    const bool full_lower_roi = lower.x <= m_lower_roi.x && lower.y <= m_lower_roi.y &&
                                lower.x + lower.width >= m_lower_roi.x + m_lower_roi.width &&
                                lower.y + lower.height >= m_lower_roi.y + m_lower_roi.height;
    const auto& sinks = context.sink_head_rois.empty() ? m_sink_rois : context.sink_head_rois;
    for (const auto& sink : sinks) {
        if (!hunter_vision::inside(sink, image)) {
            return unknown("Hunter sink evidence rectangle is clipped");
        }
    }
    const auto search = hunter_vision::patch(image, lower);

    // A local face patch excludes animation-dependent legs and most floor texture.
    // Weak matches remain unknown; they are never converted into empty-field evidence.
    struct Candidate
    {
        Rect rect;
        double score;
    };

    std::vector<Candidate> candidates;
    for (const auto& reference : m_head_templates) {
        cv::Mat scores;
        cv::matchTemplate(search, reference, scores, cv::TM_CCOEFF_NORMED);
        for (int peak = 0; peak < 16; ++peak) {
            double score = -1;
            cv::Point point;
            cv::minMaxLoc(scores, nullptr, &score, nullptr, &point);
            if (!std::isfinite(score)) {
                break;
            }
            if (score > result.lower_best_candidate_score) {
                result.lower_best_candidate_score = score;
                result.lower_best_candidate_rect = { lower.x + point.x,
                                                     lower.y + point.y,
                                                     reference.cols,
                                                     reference.rows };
            }
            if (score < m_head_present_threshold) {
                break;
            }
            candidates.push_back({ { lower.x + point.x, lower.y + point.y, reference.cols, reference.rows }, score });
            cv::rectangle(
                scores,
                cv::Rect(
                    std::max(0, point.x - 35),
                    std::max(0, point.y - 35),
                    std::min(scores.cols, point.x + 36) - std::max(0, point.x - 35),
                    std::min(scores.rows, point.y + 36) - std::max(0, point.y - 35)),
                cv::Scalar(-1),
                cv::FILLED);
        }
    }
    std::ranges::sort(candidates, [](const auto& a, const auto& b) { return a.score > b.score; });
    for (const auto& candidate : candidates) {
        const auto center =
            Point { candidate.rect.x + candidate.rect.width / 2, candidate.rect.y + candidate.rect.height / 2 };
        if (std::ranges::any_of(context.lower_unit_occlusion_rois, [&](const Rect& blocked) {
                return hunter_vision::overlaps(candidate.rect, blocked);
            })) {
            continue;
        }
        if (std::ranges::none_of(result.lower_head_candidates, [&](const Rect& known) {
                return std::hypot(center.x - known.x - known.width / 2, center.y - known.y - known.height / 2) < 35;
            })) {
            result.lower_head_candidates.push_back(candidate.rect);
        }
    }

    struct Pair
    {
        double distance;
        std::size_t track;
        std::size_t detection;
    };

    std::vector<Pair> pairs;
    for (std::size_t ti = 0; ti < m_tracks.size(); ++ti) {
        if (m_tracks[ti].last_seen_at_sink && m_tracks[ti].missing_frames > 0) {
            continue;
        }
        for (std::size_t di = 0; di < result.lower_head_candidates.size(); ++di) {
            const auto& a = m_tracks[ti].last_head;
            const auto& b = result.lower_head_candidates[di];
            const double distance = std::hypot(a.x - b.x, a.y - b.y);
            if (distance <= 180) {
                pairs.push_back({ distance, ti, di });
            }
        }
    }
    std::ranges::sort(pairs, [](const auto& a, const auto& b) { return a.distance < b.distance; });
    std::vector<bool> assigned_tracks(m_tracks.size(), false),
        assigned_detections(result.lower_head_candidates.size(), false);
    const auto at_sink = [&](const Rect& head) {
        const int x = head.x + head.width / 2, y = head.y + head.height / 2;
        return std::ranges::any_of(sinks, [&](const Rect& sink) {
            return x >= sink.x && x < sink.x + sink.width && y >= sink.y && y < sink.y + sink.height;
        });
    };
    for (const auto& pair : pairs) {
        if (assigned_tracks[pair.track] || assigned_detections[pair.detection]) {
            continue;
        }
        assigned_tracks[pair.track] = true;
        assigned_detections[pair.detection] = true;
        auto& track = m_tracks[pair.track];
        track.last_head = result.lower_head_candidates[pair.detection];
        track.last_seen_at_sink = at_sink(track.last_head);
        track.missing_frames = 0;
    }
    for (std::size_t ti = 0; ti < assigned_tracks.size(); ++ti) {
        if (!assigned_tracks[ti]) {
            ++m_tracks[ti].missing_frames;
        }
    }
    for (std::size_t di = 0; di < assigned_detections.size(); ++di) {
        if (!assigned_detections[di]) {
            const auto& head = result.lower_head_candidates[di];
            m_tracks.push_back({ head, at_sink(head), 0 });
        }
    }
    if (m_tracks.size() > 64) {
        return unknown("Hunter track association is ambiguous or unbounded");
    }
    result.observed_tracks = static_cast<int>(m_tracks.size());
    result.tracks_last_seen_at_sink =
        static_cast<int>(std::ranges::count_if(m_tracks, [](const Track& t) { return t.last_seen_at_sink; }));
    const bool timestamp_valid =
        context.observed_at_ms > 0 && (m_last_observed_at_ms == 0 || context.observed_at_ms > m_last_observed_at_ms);
    if (m_last_observed_at_ms > 0 && context.observed_at_ms > m_last_observed_at_ms + 2500) {
        m_empty_spawn_frames = 0;
        m_empty_lower_frames = 0;
        m_empty_lower_start_ms = 0;
    }
    m_last_observed_at_ms = context.observed_at_ms;
    result.full_search_coverage_verified = full_lower_roi && context.lower_visibility_verified &&
                                           context.spawn_window_closed_verified && timestamp_valid &&
                                           context.lower_unit_occlusion_rois.empty();
    if (!result.lower_head_candidates.empty()) {
        result.lower_presence = HunterPresence::Present;
        result.reasons.emplace_back("Hunter local face detected in lower field");
    }
    else if (result.full_search_coverage_verified && result.lower_best_candidate_score <= m_head_absent_threshold) {
        result.lower_presence = HunterPresence::Absent;
    }
    else {
        result.reasons.emplace_back("Lower visibility, spawn window, timestamp or weak head evidence remains unknown");
    }
    const bool empty =
        std::ranges::all_of(result.spawns, [](const auto& slot) { return slot.presence == HunterPresence::Absent; });
    const bool distinct = m_last_observable_frame.empty() ||
                          cv::norm(image, m_last_observable_frame, cv::NORM_L1) / (image.total() * 3) > 0.10;
    m_empty_spawn_frames = empty ? m_empty_spawn_frames + (distinct ? 1 : 0) : 0;
    result.distinct_empty_spawn_frames = m_empty_spawn_frames;
    result.upper_spawns_empty_candidate = m_empty_spawn_frames >= 3;
    result.bait_retreat_allowed = context.spawn_window_closed_verified && m_empty_spawn_frames >= 2 &&
                                  result.lower_presence == HunterPresence::Present;
    const bool tracks_at_sink = !m_tracks.empty() && std::ranges::all_of(m_tracks, [](const Track& track) {
        return track.last_seen_at_sink && track.missing_frames > 0;
    });
    if (empty && result.lower_presence == HunterPresence::Absent && tracks_at_sink && distinct) {
        if (m_empty_lower_frames == 0) {
            m_empty_lower_start_ms = context.observed_at_ms;
        }
        ++m_empty_lower_frames;
    }
    else if (!empty || result.lower_presence != HunterPresence::Absent || !tracks_at_sink) {
        m_empty_lower_frames = 0;
        m_empty_lower_start_ms = 0;
    }
    result.distinct_empty_lower_frames = m_empty_lower_frames;
    result.all_hunters_cleared =
        result.full_search_coverage_verified && tracks_at_sink && m_empty_lower_frames >= m_min_clear_frames &&
        m_empty_spawn_frames >= m_min_clear_frames && context.observed_at_ms >= m_empty_lower_start_ms &&
        context.observed_at_ms - m_empty_lower_start_ms >= m_min_clear_duration_ms;
    m_last_observable_frame = image.clone();
    result.reasons.emplace_back(
        result.all_hunters_cleared
            ? "All observed hunter tracks ended at verified sinks; distinct timed visible frames confirm no residual "
              "heads"
            : "Hunter prelude remains blocked until complete visible and temporal sink evidence is available");
    return result;
}

