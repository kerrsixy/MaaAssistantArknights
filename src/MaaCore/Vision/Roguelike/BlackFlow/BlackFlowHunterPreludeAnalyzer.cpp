#include "BlackFlowHunterPreludeAnalyzer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <span>

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
} // namespace asst::blackflow::hunter_vision

bool asst::blackflow::BlackFlowHunterPreludeAnalyzer::load(
    const std::filesystem::path& manifest_path,
    std::string* error)
{
    m_loaded = false;
    m_anchors.clear();
    m_head_profiles.clear();
    m_approach_rois.clear();
    m_spawns = {};
    reset();
    const auto fail = [error](const std::string& why) {
        if (error) {
            *error = why;
        }
        return false;
    };
    const auto data = json::open(manifest_path);
    if (!data || data->get("schema_version", 0) != 3 || data->get("native_width", 0) != 1280 ||
        data->get("native_height", 0) != 720) {
        return fail("Hunter reference manifest or native dimensions are invalid");
    }
    const auto spawns = data->find<json::array>("spawn_models");
    const auto anchors = data->find<json::array>("anchors");
    if (!spawns || spawns->size() != 2 || !anchors || anchors->size() < 2) {
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
    m_task_upper_roi = hunter_vision::rect(data->get("task_upper_roi", json::array {}));
    m_lower_roi = hunter_vision::rect(data->get("lower_roi", json::array {}));
    if (!hunter_vision::inside(m_lower_roi, canvas)) {
        return fail("Hunter lower search region or candidate threshold is invalid");
    }
    m_battlefield_roi = hunter_vision::rect(data->get("battlefield_roi", json::array {}));
    if (!hunter_vision::inside(m_battlefield_roi, canvas) || m_battlefield_roi.x > m_lower_roi.x ||
        m_battlefield_roi.y > m_lower_roi.y ||
        m_battlefield_roi.x + m_battlefield_roi.width < m_lower_roi.x + m_lower_roi.width ||
        m_battlefield_roi.y + m_battlefield_roi.height < m_lower_roi.y + m_lower_roi.height) {
        return fail("Hunter whole-field envelope does not cover the complete lower field");
    }
    const auto heads = data->find<json::array>("head_profiles");
    auto sinks = data->find<json::array>("approach_head_rois");
    if (!sinks) {
        sinks = data->find<json::array>("sink_head_rois");
    }
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
    bool upper_model = false, lower_model = false;
    for (const auto& value : *heads) {
        const auto id = value.get("id", std::string());
        const auto domain = value.get("domain", std::string());
        auto image = hunter_vision::read_patch(folder, value.get("file", std::string()));
        const auto mask_file = value.get("mask", std::string());
        cv::Mat mask;
        if (!mask_file.empty() && hunter_vision::simple_file(mask_file)) {
            mask = MAA_NS::imread(folder / MAA_NS::path(mask_file), cv::IMREAD_GRAYSCALE);
        }
        const auto scales = value.get("scales", std::vector<double> { 1.0 });
        const bool mirror = value.get("mirror", false);
        const double threshold = value.get("present_threshold", m_head_present_threshold);
        if (id.empty() || (domain != "upper" && domain != "lower") || image.empty() || image.cols < 20 ||
            image.rows < 20 || image.cols > 128 || image.rows > 128 || scales.empty() || scales.size() > 5 ||
            threshold < m_head_present_threshold || threshold > 0.95 ||
            (!mask_file.empty() &&
             (mask.empty() || mask.size() != image.size() || cv::countNonZero(mask) < image.total() / 5))) {
            return fail("Hunter domain-specific head or mask profile is invalid");
        }
        upper_model |= domain == "upper";
        lower_model |= domain == "lower";
        for (const double scale : scales) {
            if (scale < 0.65 || scale > 1.35) {
                return fail("Hunter head profile scale is invalid");
            }
            cv::Mat resized, resized_mask;
            cv::resize(image, resized, {}, scale, scale, cv::INTER_AREA);
            if (!mask.empty()) {
                cv::resize(mask, resized_mask, resized.size(), 0, 0, cv::INTER_NEAREST);
            }
            m_head_profiles.push_back({ id, domain, resized, resized_mask, threshold });
            if (mirror) {
                cv::Mat flipped, flipped_mask;
                cv::flip(resized, flipped, 1);
                if (!resized_mask.empty()) {
                    cv::flip(resized_mask, flipped_mask, 1);
                }
                m_head_profiles.push_back({ id + "-mirrored", domain, flipped, flipped_mask, threshold });
            }
        }
    }
    if (!upper_model || !lower_model) {
        return fail("Both upper and lower hunter appearance domains are required");
    }
    for (const auto& value : *sinks) {
        const auto sink = hunter_vision::rect(value);
        if (!hunter_vision::inside(sink, canvas)) {
            return fail("Hunter sink rectangle is clipped");
        }
        m_approach_rois.emplace_back(sink);
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
    m_empty_spawn_frames_by_slot = {};
    m_tracks.clear();
    m_empty_lower_frames = 0;
    m_task_upper_empty_frames = 0;
    m_empty_lower_start_ms = 0;
    m_last_observed_at_ms = 0;
}

asst::blackflow::HunterPreludeObservation asst::blackflow::BlackFlowHunterPreludeAnalyzer::analyze(
    const cv::Mat& image,
    const HunterPreludeFrameContext& context)
{
    HunterPreludeObservation result;
    const auto unknown = [&](const std::string& reason) {
        result.frame_observable = false;
        result.task_upper_covers_spawn_rois = false;
        result.task_upper_scope_roi = {};
        result.lower_presence = HunterPresence::Unknown;
        result.battlefield_presence = HunterPresence::Unknown;
        result.full_search_coverage_verified = false;
        result.all_hunters_cleared = false;
        result.bait_retreat_allowed = false;
        for (auto& spawn : result.spawns) {
            spawn.presence = HunterPresence::Unknown;
        }
        result.observed_tracks = static_cast<int>(m_tracks.size());
        result.tracks_last_seen_at_approach =
            static_cast<int>(std::ranges::count_if(m_tracks, [](const Track& t) { return t.last_seen_at_approach; }));
        result.tracks_last_seen_at_sink = result.tracks_last_seen_at_approach;
        m_empty_spawn_frames = 0;
        m_empty_spawn_frames_by_slot = {};
        m_empty_lower_frames = 0;
        m_task_upper_empty_frames = 0;
        m_empty_lower_start_ms = 0;
        m_last_observable_frame.release();
        result.reasons.push_back(reason);
        return result;
    };
    if (!m_loaded || image.empty() || image.cols != 1280 || image.rows != 720 || image.type() != CV_8UC3 ||
        context.battle_key.empty() || !context.normal_battle_confirmed || !context.stage_geometry_verified) {
        return unknown("Hunter image, normal battle or stage geometry is unverified");
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
    if (!context.spawn_rois.empty() && context.spawn_rois.size() != 2) {
        return unknown("Exactly two projected hunter spawn rectangles are required");
    }
    const Rect lower = context.lower_search_roi.empty() ? m_lower_roi : context.lower_search_roi;
    const Rect field = context.battlefield_search_roi.empty() ? m_battlefield_roi : context.battlefield_search_roi;
    if (!hunter_vision::inside(lower, image) || !hunter_vision::inside(field, image)) {
        return unknown("Hunter search envelope is clipped");
    }
    for (const auto& anchor : m_anchors) {
        const auto current = hunter_vision::patch(image, anchor.roi);
        if (hunter_vision::correlation(current, anchor.image) < anchor.min_correlation ||
            hunter_vision::luminance(current) / std::max(1.0, hunter_vision::luminance(anchor.image)) <
                anchor.min_luminance_ratio) {
            return unknown(
                "Hunter static wood anchor is dimmed, selected, obscured "
                "or from another stage");
        }
    }
    for (std::size_t i = 0; i < 2; ++i) {
        auto& spawn = result.spawns[i];
        spawn.roi = context.spawn_rois.empty() ? m_spawns[i].roi : context.spawn_rois[i];
        if (!hunter_vision::inside(spawn.roi, image)) {
            return unknown("Hunter spawn rectangle is clipped");
        }
        for (const auto& cover : context.occlusion_rois) {
            if (hunter_vision::overlaps(spawn.roi, cover) || hunter_vision::overlaps(field, cover)) {
                return unknown("Hunter search is hidden by explicit UI occlusion");
            }
        }
        const auto crop = hunter_vision::patch(image, spawn.roi);
        spawn.present_score = hunter_vision::best_score(crop, m_spawns[i].present);
        spawn.absent_score = hunter_vision::best_score(crop, m_spawns[i].absent);
        if (spawn.present_score >= 0.82 && spawn.present_score - spawn.absent_score >= 0.12) {
            spawn.presence = HunterPresence::Present;
        }
        else if (spawn.absent_score >= 0.80 && spawn.absent_score - spawn.present_score >= 0.10) {
            spawn.presence = HunterPresence::Absent;
        }
    }
    result.frame_observable = true;
    const auto contains = [](const Rect& outer, const Rect& inner) {
        return outer.x <= inner.x && outer.y <= inner.y && outer.x + outer.width >= inner.x + inner.width &&
               outer.y + outer.height >= inner.y + inner.height;
    };
    const auto masked = [](const std::vector<Rect>& masks, const Rect& roi) {
        return std::ranges::any_of(masks, [&](const Rect& mask) { return hunter_vision::overlaps(mask, roi); });
    };
    const auto& approaches = context.sink_head_rois.empty() ? m_approach_rois : context.sink_head_rois;
    for (const auto& approach : approaches) {
        if (!hunter_vision::inside(approach, image)) {
            return unknown("Hunter approach zone is clipped");
        }
    }

    // The scoped upper search is independent of whole-field absence. Known
    // allied overlap still vetoes this domain; no pixel is inpainted or hidden.
    if (!m_task_upper_roi.empty() && hunter_vision::inside(m_task_upper_roi, image) &&
        !masked(context.battlefield_unit_occlusion_rois, m_task_upper_roi) &&
        !masked(context.lower_unit_occlusion_rois, m_task_upper_roi)) {
        bool positive = false;
        for (const auto& profile : m_head_profiles) {
            if (m_task_upper_roi.width < profile.image.cols || m_task_upper_roi.height < profile.image.rows) {
                continue;
            }
            cv::Mat scoped_scores;
            cv::matchTemplate(
                hunter_vision::patch(image, m_task_upper_roi),
                profile.image,
                scoped_scores,
                cv::TM_CCOEFF_NORMED,
                profile.mask);
            cv::patchNaNs(scoped_scores, -1);
            scoped_scores.setTo(-1, (scoped_scores > 1.01) | (scoped_scores < -1.01));
            double score = -1;
            cv::Point point;
            cv::minMaxLoc(scoped_scores, nullptr, &score, nullptr, &point);
            if (std::isfinite(score)) {
                result.task_upper_best_score = std::max(result.task_upper_best_score, score);
                if (score >= profile.present_threshold) {
                    positive = true;
                    result.task_upper_head_candidates.emplace_back(
                        m_task_upper_roi.x + point.x,
                        m_task_upper_roi.y + point.y,
                        profile.image.cols,
                        profile.image.rows);
                }
            }
        }
        result.task_upper_visibility_verified = result.task_upper_best_score >= 0;
        if (result.task_upper_visibility_verified) {
            result.task_upper_scope_roi = m_task_upper_roi;
            result.task_upper_covers_spawn_rois = std::ranges::all_of(result.spawns, [&](const auto& spawn) {
                return !spawn.roi.empty() && contains(m_task_upper_roi, spawn.roi);
            });
        }
        if (positive) {
            result.task_upper_presence = HunterPresence::Present;
        }
        else if (result.task_upper_visibility_verified && result.task_upper_best_score <= m_head_absent_threshold) {
            result.task_upper_presence = HunterPresence::Absent;
        }
    }

    struct Candidate
    {
        Rect rect;
        double score;
    };

    std::vector<Candidate> candidates;
    std::span<const HeadProfile> search_profiles = m_head_profiles;
    if (context.spawn_only_probe) {
        search_profiles = {};
    }
    for (const auto& profile : search_profiles) {
        const int top = profile.domain == "upper" ? field.y : std::max(field.y, lower.y - 20);
        const int bottom =
            profile.domain == "upper" ? std::min(field.y + field.height, lower.y + 60) : field.y + field.height;
        const Rect domain { field.x, top, field.width, bottom - top };
        if (!hunter_vision::inside(domain, image) || domain.width < profile.image.cols ||
            domain.height < profile.image.rows) {
            return unknown("Hunter appearance domain is clipped or too small");
        }
        cv::Mat scores;
        cv::matchTemplate(
            hunter_vision::patch(image, domain),
            profile.image,
            scores,
            cv::TM_CCOEFF_NORMED,
            profile.mask);
        cv::patchNaNs(scores, -1);
        scores.setTo(-1, (scores > 1.01) | (scores < -1.01));
        const int begin = std::clamp(lower.y - domain.y - profile.image.rows / 2, 0, scores.rows);
        const int end = std::clamp(lower.y + lower.height - domain.y - profile.image.rows / 2, 0, scores.rows);
        if (end > begin) {
            double best = -1;
            cv::Point point;
            cv::minMaxLoc(scores(cv::Rect(0, begin, scores.cols, end - begin)), nullptr, &best, nullptr, &point);
            if (best > result.lower_best_candidate_score) {
                result.lower_best_candidate_score = best;
                result.lower_best_candidate_rect = { domain.x + point.x,
                                                     domain.y + begin + point.y,
                                                     profile.image.cols,
                                                     profile.image.rows };
            }
        }
        for (int peak = 0; peak < 16; ++peak) {
            double score = -1;
            cv::Point point;
            cv::minMaxLoc(scores, nullptr, &score, nullptr, &point);
            if (!std::isfinite(score)) {
                break;
            }
            if (score > result.battlefield_best_candidate_score) {
                result.battlefield_best_candidate_score = score;
                result.battlefield_best_candidate_rect = { domain.x + point.x,
                                                           domain.y + point.y,
                                                           profile.image.cols,
                                                           profile.image.rows };
            }
            if (score < profile.present_threshold) {
                break;
            }
            candidates.push_back(
                { { domain.x + point.x, domain.y + point.y, profile.image.cols, profile.image.rows }, score });
            const int left = std::max(0, point.x - 35), y = std::max(0, point.y - 35);
            cv::rectangle(
                scores,
                cv::Rect(left, y, std::min(scores.cols, point.x + 36) - left, std::min(scores.rows, point.y + 36) - y),
                cv::Scalar(-1),
                cv::FILLED);
        }
    }
    std::ranges::sort(candidates, [](const auto& a, const auto& b) { return a.score > b.score; });
    for (const auto& candidate : candidates) {
        if (masked(context.battlefield_unit_occlusion_rois, candidate.rect) ||
            masked(context.lower_unit_occlusion_rois, candidate.rect)) {
            continue;
        }
        const Point center { candidate.rect.x + candidate.rect.width / 2,
                             candidate.rect.y + candidate.rect.height / 2 };
        // A high-confidence foreground head can remain recognizable when
        // overlapping hunters change the full spawn patch. Require the complete
        // positive rectangle inside a projected spawn region; this adds presence
        // evidence only.
        for (auto& spawn : result.spawns) {
            if (contains(spawn.roi, candidate.rect)) {
                spawn.presence = HunterPresence::Present;
                spawn.present_score = std::max(spawn.present_score, candidate.score);
            }
        }
        if (std::ranges::none_of(result.battlefield_head_candidates, [&](const Rect& known) {
                return std::hypot(center.x - known.x - known.width / 2, center.y - known.y - known.height / 2) < 35;
            })) {
            result.battlefield_head_candidates.push_back(candidate.rect);
            if (center.x >= lower.x && center.x < lower.x + lower.width && center.y >= lower.y &&
                center.y < lower.y + lower.height) {
                result.lower_head_candidates.push_back(candidate.rect);
            }
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
        if (m_tracks[ti].last_seen_at_approach && m_tracks[ti].missing_frames > 0) {
            continue;
        }
        for (std::size_t di = 0; di < result.battlefield_head_candidates.size(); ++di) {
            const auto& old = m_tracks[ti].last_head;
            const auto& now = result.battlefield_head_candidates[di];
            const double distance = std::hypot(old.x - now.x, old.y - now.y);
            const bool teleport = old.y + old.height / 2 < lower.y && now.y + now.height / 2 >= lower.y &&
                                  context.spawn_window_closed_verified;
            if (distance <= (teleport ? 500 : 180)) {
                pairs.push_back({ distance, ti, di });
            }
        }
    }
    std::ranges::sort(pairs, [](const auto& a, const auto& b) { return a.distance < b.distance; });
    std::vector<bool> assigned(m_tracks.size(), false), matched(result.battlefield_head_candidates.size(), false);
    const auto at_approach = [&](const Rect& head) {
        const int x = head.x + head.width / 2, y = head.y + head.height / 2;
        return std::ranges::any_of(approaches, [&](const Rect& r) {
            return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
        });
    };
    for (const auto& pair : pairs) {
        if (assigned[pair.track] || matched[pair.detection]) {
            continue;
        }
        assigned[pair.track] = true;
        matched[pair.detection] = true;
        auto& track = m_tracks[pair.track];
        track.last_head = result.battlefield_head_candidates[pair.detection];
        track.last_seen_at_approach = at_approach(track.last_head);
        track.missing_frames = 0;
    }
    for (std::size_t ti = 0; ti < assigned.size(); ++ti) {
        if (!context.spawn_only_probe && !assigned[ti]) {
            ++m_tracks[ti].missing_frames;
        }
    }
    for (std::size_t di = 0; di < matched.size(); ++di) {
        if (!matched[di]) {
            const auto& head = result.battlefield_head_candidates[di];
            m_tracks.push_back({ head, at_approach(head), 0 });
        }
    }
    if (m_tracks.size() > 64) {
        return unknown("Hunter track association is ambiguous or unbounded");
    }
    result.observed_tracks = static_cast<int>(m_tracks.size());
    result.tracks_last_seen_at_approach =
        static_cast<int>(std::ranges::count_if(m_tracks, [](const Track& t) { return t.last_seen_at_approach; }));
    result.tracks_last_seen_at_sink = result.tracks_last_seen_at_approach;
    const bool timestamp =
        context.observed_at_ms > 0 && (m_last_observed_at_ms == 0 || context.observed_at_ms > m_last_observed_at_ms);
    if (m_last_observed_at_ms > 0 && context.observed_at_ms > m_last_observed_at_ms + 2500) {
        m_empty_spawn_frames = 0;
        m_empty_spawn_frames_by_slot = {};
    }
    m_last_observed_at_ms = context.observed_at_ms;
    const bool lower_covered =
        !context.spawn_only_probe && contains(lower, m_lower_roi) && context.lower_visibility_verified && timestamp &&
        !masked(context.lower_unit_occlusion_rois, lower) && !masked(context.battlefield_unit_occlusion_rois, lower);
    result.full_search_coverage_verified =
        !context.spawn_only_probe && contains(field, m_battlefield_roi) && context.battlefield_visibility_verified &&
        context.spawn_window_closed_verified && timestamp && !masked(context.lower_unit_occlusion_rois, field) &&
        !masked(context.battlefield_unit_occlusion_rois, field);
    if (!result.lower_head_candidates.empty()) {
        result.lower_presence = HunterPresence::Present;
    }
    else if (lower_covered && result.lower_best_candidate_score <= m_head_absent_threshold) {
        result.lower_presence = HunterPresence::Absent;
    }
    if (!result.battlefield_head_candidates.empty()) {
        result.battlefield_presence = HunterPresence::Present;
    }
    else if (
        result.full_search_coverage_verified && result.battlefield_best_candidate_score <= m_head_absent_threshold) {
        result.battlefield_presence = HunterPresence::Absent;
    }
    const bool empty =
        std::ranges::all_of(result.spawns, [](const auto& slot) { return slot.presence == HunterPresence::Absent; });
    const bool distinct = m_last_observable_frame.empty() ||
                          cv::norm(image, m_last_observable_frame, cv::NORM_L1) / (image.total() * 3) > 0.10;
    if (result.task_upper_visibility_verified && result.task_upper_presence == HunterPresence::Absent) {
        m_task_upper_empty_frames += distinct ? 1 : 0;
    }
    else {
        m_task_upper_empty_frames = 0;
    }
    result.task_upper_empty_frames = m_task_upper_empty_frames;
    for (std::size_t index = 0; index < result.spawns.size(); ++index) {
        auto& spawn = result.spawns[index];
        m_empty_spawn_frames_by_slot[index] =
            spawn.presence == HunterPresence::Absent ? m_empty_spawn_frames_by_slot[index] + (distinct ? 1 : 0) : 0;
        spawn.distinct_empty_frames = m_empty_spawn_frames_by_slot[index];
    }
    m_empty_spawn_frames = empty ? m_empty_spawn_frames + (distinct ? 1 : 0) : 0;
    result.distinct_empty_spawn_frames = m_empty_spawn_frames;
    result.upper_spawns_empty_candidate = m_empty_spawn_frames >= 3;
    result.bait_retreat_allowed = context.spawn_window_closed_verified && m_empty_spawn_frames >= 2 &&
                                  result.lower_presence == HunterPresence::Present;
    // Track segments can split across blink or selection gaps. Their approach
    // positions are diagnostics, never a death list. Only fresh whole-field
    // absence can release.
    const bool hunter_history = !m_tracks.empty();
    // A static background below its absolute template threshold stays Unknown.
    // It may support an independent full-field negative only when its existing
    // contrast margin favors absence; conflicting foreground evidence vetoes it.
    const bool spawn_backgrounds_support_absence = std::ranges::all_of(result.spawns, [](const auto& spawn) {
        return spawn.presence != HunterPresence::Present && spawn.absent_score >= 0.0 && spawn.present_score >= 0.0 &&
               spawn.absent_score - spawn.present_score >= 0.10;
    });
    const bool whole_field_absent = result.full_search_coverage_verified &&
                                    result.battlefield_presence == HunterPresence::Absent &&
                                    spawn_backgrounds_support_absence;
    if (whole_field_absent && hunter_history && distinct) {
        if (m_empty_lower_frames == 0) {
            m_empty_lower_start_ms = context.observed_at_ms;
        }
        ++m_empty_lower_frames;
    }
    else if (!whole_field_absent || !hunter_history) {
        m_empty_lower_frames = 0;
        m_empty_lower_start_ms = 0;
    }
    result.distinct_empty_lower_frames = m_empty_lower_frames;
    result.all_hunters_cleared = result.full_search_coverage_verified && hunter_history &&
                                 m_empty_lower_frames >= m_min_clear_frames && spawn_backgrounds_support_absence &&
                                 context.observed_at_ms >= m_empty_lower_start_ms &&
                                 context.observed_at_ms - m_empty_lower_start_ms >= m_min_clear_duration_ms;
    m_last_observable_frame = image.clone();
    result.reasons.push_back(
        result.all_hunters_cleared ? "Independent whole-field visible timed absence confirms no "
                                     "residual heads; approach is not a death "
                                     "certificate"
                                   : "Hunter prelude remains blocked until whole-field visibility and "
                                     "residual-hunter evidence are complete");
    return result;
}
