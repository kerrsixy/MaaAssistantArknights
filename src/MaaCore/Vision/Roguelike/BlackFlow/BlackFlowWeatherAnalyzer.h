#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <meojson/json.hpp>

#include "Common/AsstTypes.h"
#include "MaaUtils/NoWarningCVMat.hpp"

namespace asst::blackflow
{
struct WeatherCell
{
    int row = -1;
    int column = -1;
    bool operator==(const WeatherCell&) const noexcept = default;
};

struct WeatherNodeObservation
{
    WeatherCell cell;
    Rect icon_rect; // Icon only, excluding the text label, in native 1280x720 coordinates.
    std::string type;
    bool exists = false;
    // Current-run/current-floor active-node preview only, never map glow or a
    // historical cleared node. A missing label is unknown, not verified false.
    std::optional<bool> ideal_source_from_preview;
};

struct WeatherFrameContext
{
    std::string floor_key; // Include run ID and floor ID; reused IDs must not share temporal evidence.
    std::uint64_t frame_sequence = 0;
    int rows = 0;
    int columns = 0;
    bool map_geometry_verified = false;
    std::optional<int> verified_difficulty;
    std::vector<WeatherNodeObservation> nodes;
    // Both ID and body must have been read from the actual game tooltip. A glyph
    // candidate or a catalog description is not a substitute for these inputs.
    std::optional<std::string> verified_weather_id;
    std::optional<std::string> verified_weather_body;
    std::optional<std::string> verified_policy_id;
    std::optional<std::string> verified_policy_body;
};

enum class WeatherIdentityState
{
    Unknown,
    GlyphCandidate,
    TooltipConfirmed,
};

struct WeatherMetadata
{
    std::string id;
    std::string icon_id;
    std::string type;
    std::string category;
    std::string name;
    std::optional<int> level;
    std::optional<bool> positive;
    std::optional<int> minimum_difficulty;
    std::vector<int> difficulty_grades;
    std::optional<bool> destructible_source;
    std::string body;
    std::string activation;
    std::string scope_mode;
    std::string source_node_rule;
    json::array effects;
    bool level_from_verified_difficulty = false;
    bool body_from_catalog = true;
    bool tooltip_body_verified = false;
};

struct WeatherGlyphCandidate
{
    std::string icon_id;
    std::string category;
    double score = 0.0;
    Rect matched_rect;
};

struct WeatherIdentityObservation
{
    WeatherIdentityState state = WeatherIdentityState::Unknown;
    std::optional<WeatherMetadata> metadata;
    std::vector<WeatherGlyphCandidate> ranked_glyphs;
    double best_score = 0.0;
    double runner_up_score = 0.0;
    double margin = 0.0;
    std::string evidence_source;
    std::string reason;
};

enum class WeatherSourceState
{
    Unknown,
    RadialCandidate,
    TemporallyConsistentCandidate,
    PreviewConfirmed,
};

struct WeatherSourceObservation
{
    WeatherCell cell;
    Rect icon_rect;
    WeatherSourceState state = WeatherSourceState::Unknown;
    double ring_template_score = 0.0;
    int radial_sectors = 0;
    double annular_edge_density = 0.0;
    int distinct_consistent_frames = 0;
    bool ring_fully_visible = false;
    std::optional<bool> clearable;
    std::string evidence_source;
};

struct WeatherRecognitionResult
{
    bool map_verified = false;
    std::vector<double> map_anchor_scores;
    WeatherIdentityObservation main_weather;
    WeatherIdentityObservation policy;
    // Rejected probes are diagnostic evidence, never source assertions.
    std::vector<WeatherSourceObservation> source_probes;
    std::vector<WeatherSourceObservation> source_candidates;
    std::optional<WeatherSourceObservation> selected_source;
    // A single learned ring or repeated copies of one image never establish scope.
    bool scope_known = false;
    std::optional<int> radius;
    std::string range_pattern;
    std::vector<WeatherCell> candidate_affected_cells;
    std::vector<WeatherCell> affected_cells;
    std::string scope_evidence_source;
    std::vector<std::string> reasons;
    [[nodiscard]] json::value to_json() const;
};

// Pure image/data analysis. No OCR task mutation, input, networking or routing.
// Keep one instance per map observer; temporal evidence is not shared across threads.
class BlackFlowWeatherAnalyzer
{
public:
    bool load(
        const std::filesystem::path& catalog_path,
        const std::filesystem::path& template_manifest_path,
        std::string* error = nullptr);
    [[nodiscard]] WeatherRecognitionResult analyze(const cv::Mat& image, const WeatherFrameContext& context);
    void reset() noexcept;

    [[nodiscard]] bool loaded() const noexcept { return m_loaded; }

private:
    struct Glyph
    {
        std::string icon_id;
        std::string category;
        cv::Mat mask;
    };

    struct MapAnchor
    {
        cv::Mat mask;
        Rect search_roi;
        double threshold = 0.86;
    };

    struct Entry
    {
        WeatherMetadata metadata;
    };

    struct SourceHistory
    {
        std::string floor_key;
        WeatherCell cell;
        std::uint64_t frame_sequence = 0;
        std::uint64_t image_hash = 0;
        int distinct_frames = 0;
    };

    [[nodiscard]] std::optional<WeatherMetadata>
        metadata_for_icon(const std::string& icon_id, std::optional<int> difficulty) const;
    [[nodiscard]] WeatherIdentityObservation tooltip_observation(
        const std::optional<std::string>& id,
        const std::optional<std::string>& body,
        bool policy) const;

    std::vector<Entry> m_entries;
    std::vector<Glyph> m_glyphs;
    std::vector<MapAnchor> m_map_anchors;
    std::vector<WeatherCell> m_small_offsets;
    std::vector<WeatherCell> m_large_offsets;
    cv::Mat m_source_template;
    Rect m_glyph_roi;
    double m_glyph_minimum_score = 0.76;
    double m_glyph_minimum_margin = 0.15;
    SourceHistory m_history;
    bool m_loaded = false;
};
}
