#include "BlackFlowHunterPreludeController.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string_view>

#include "Arknights-Tile-Pos/TileCalc2.hpp"
#include "Utils/Logger.hpp"
#include "Utils/Platform.hpp"

namespace asst::blackflow::hunter_control
{
constexpr Point BaitTile { 3, 4 };
constexpr std::string_view HunterId = "enemy_2135_shkrbt";

bool fail(std::string* error, std::string message)
{
    LogWarn << __FUNCTION__ << message;
    if (error) {
        *error = std::move(message);
    }
    return false;
}

std::optional<int> height_code(std::string_view name)
{
    if (name == "LOWLAND") {
        return 0;
    }
    if (name == "HIGHLAND") {
        return 1;
    }
    return std::nullopt;
}

std::optional<int> buildable_code(std::string_view name)
{
    if (name == "NONE") {
        return 0;
    }
    if (name == "MELEE") {
        return 1;
    }
    if (name == "RANGED") {
        return 2;
    }
    if (name == "ALL") {
        return 3;
    }
    return std::nullopt;
}

Rect read_rect(const json::value& value)
{
    if (!value.is_array() || value.as_array().size() != 4) {
        return {};
    }
    const auto x = value.find<int>(0), y = value.find<int>(1), w = value.find<int>(2), h = value.find<int>(3);
    if (!x || !y || !w || !h || *x < 0 || *y < 0 || *w <= 0 || *h <= 0 || *x > 1280 - *w || *y > 720 - *h) {
        return {};
    }
    return { *x, *y, *w, *h };
}

bool overlaps(const Rect& a, const Rect& b)
{
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

bool zero_delay(const json::value& object, std::string_view key)
{
    const auto value = object.find<double>(std::string(key));
    return value && std::isfinite(*value) && *value == 0.0;
}

bool hunter_action(const json::value& value)
{
    return value.is_object() && value.get("actionType", std::string()) == "SPAWN" &&
           value.get("key", std::string()).starts_with(HunterId);
}

int count_hunter_actions(const json::value& value)
{
    int count = hunter_action(value) ? 1 : 0;
    if (value.is_object()) {
        for (const auto& [key, child] : value.as_object()) {
            count += count_hunter_actions(child);
        }
    }
    else if (value.is_array()) {
        for (const auto& child : value.as_array()) {
            count += count_hunter_actions(child);
        }
    }
    return count;
}

bool spawn_contract(const json::value& raw, json::object& evidence, std::string* error)
{
    const auto waves = raw.find<json::array>("waves");
    const auto routes = raw.find<json::array>("routes");
    if (!waves || waves->empty() || !routes) {
        return fail(error, "Hunter waves or routes are missing");
    }
    int certified = 0;
    std::set<Point> native_spawns;
    const auto& initial_wave = (*waves)[0];
    if (!zero_delay(initial_wave, "preDelay")) {
        return fail(error, "Initial wave is delayed or unknown");
    }
    const auto fragments = initial_wave.find<json::array>("fragments");
    if (!fragments) {
        return fail(error, "Initial wave fragments are missing");
    }
    bool initial_time_verified = true;
    json::array paths;
    for (std::size_t fi = 0; fi < fragments->size(); ++fi) {
        const auto& fragment = (*fragments)[fi];
        initial_time_verified = initial_time_verified && zero_delay(fragment, "preDelay");
        const auto actions = fragment.find<json::array>("actions");
        if (!actions) {
            return fail(error, "Initial fragment actions are missing");
        }
        for (std::size_t ai = 0; ai < actions->size(); ++ai) {
            const auto& action = (*actions)[ai];
            if (!hunter_action(action)) {
                continue;
            }
            const auto count = action.find<int>("count");
            const auto route_index = action.find<int>("routeIndex");
            const auto hidden = action.find("hiddenGroup");
            if (!initial_time_verified || action.get("key", std::string()) != HunterId ||
                !zero_delay(action, "preDelay") || !count || *count <= 0 ||
                (*count > 1 && !zero_delay(action, "interval")) || !route_index || *route_index < 0 ||
                static_cast<std::size_t>(*route_index) >= routes->size() || !hidden || !hidden->is_null() ||
                action.get("managedByScheduler", false) != true) {
                return fail(error, "A hunter spawn is delayed, conditional, unsupported or has an unknown route");
            }
            const auto& route = (*routes)[*route_index];
            const auto start = route.find<json::object>("startPosition");
            if (!start) {
                return fail(error, "Hunter route start is missing");
            }
            const json::value position(*start);
            const auto row = position.find<int>("row"), column = position.find<int>("col");
            if (!row || !column || *row < 0 || *row >= 7 || *column < 0 || *column >= 10) {
                return fail(error, "Hunter route start is invalid");
            }
            const Point tile { *column, 6 - *row };
            if (tile != Point(2, 2) && tile != Point(4, 2)) {
                return fail(error, "Hunter spawns outside the two calibrated upper tiles");
            }
            native_spawns.insert(tile);
            ++certified;
            paths.emplace_back("/waves/0/fragments/" + std::to_string(fi) + "/actions/" + std::to_string(ai));
        }
        // Even an optional treasure activation in fragment zero must explicitly
        // be non-blocking before a later fragment can be certified at t=0.
        for (const auto& action : *actions) {
            const auto blocks = action.find<bool>("blockFragment");
            if (!blocks || *blocks) {
                initial_time_verified = false;
            }
        }
    }
    if (certified == 0 || certified != count_hunter_actions(raw) || native_spawns.size() != 2) {
        return fail(error, "Later waves, branches or unsupported structures may add hunters");
    }
    evidence["hunter_action_paths"] = std::move(paths);
    evidence["hunter_spawn_actions_at_t0"] = certified;
    evidence["initial_spawn_tiles"] = json::array { json::array { 2, 2 }, json::array { 4, 2 } };
    evidence["later_hunter_spawn_absent_verified"] = true;
    evidence["hunter_count_inferred"] = false;
    return true;
}

json::object detector_json(const HunterPreludeObservation& result)
{
    json::array slots;
    for (const auto& slot : result.spawns) {
        slots.emplace_back(
            json::object { { "presence", static_cast<int>(slot.presence) },
                           { "present_score", slot.present_score },
                           { "absent_score", slot.absent_score } });
    }
    return { { "frame_observable", result.frame_observable },
             { "spawns", std::move(slots) },
             { "lower_presence", static_cast<int>(result.lower_presence) },
             { "battlefield_presence", static_cast<int>(result.battlefield_presence) },
             { "battlefield_best_candidate_score", result.battlefield_best_candidate_score },
             { "battlefield_head_candidates", result.battlefield_head_candidates.size() },
             { "lower_best_candidate_score", result.lower_best_candidate_score },
             { "distinct_empty_spawn_frames", result.distinct_empty_spawn_frames },
             { "bait_retreat_allowed", result.bait_retreat_allowed },
             { "observed_tracks", result.observed_tracks },
             { "tracks_last_seen_at_approach", result.tracks_last_seen_at_approach },
             { "distinct_empty_lower_frames", result.distinct_empty_lower_frames },
             { "full_search_coverage_verified", result.full_search_coverage_verified },
             { "all_hunters_cleared", result.all_hunters_cleared },
             { "reasons", result.reasons } };
}
}

bool asst::blackflow::BlackFlowHunterPreludeController::prepare(
    const std::filesystem::path& template_manifest,
    const std::filesystem::path& raw_level_path,
    const Map::Level& current_level,
    int bait_plan_index,
    std::string battle_key,
    std::string* error)
{
    *this = {};
    if (error) {
        error->clear();
    }
    m_failure_reason = "Hunter prelude is not prepared";
    if (bait_plan_index < 0 || battle_key.empty() || current_level.get_width() != 10 ||
        current_level.get_height() != 7 || current_level.key.levelId != "obt/roguelike/ro6/level_rogue6_2-5" ||
        current_level.view.size() < 2) {
        return hunter_control::fail(
            error,
            m_failure_reason = "Hunter stage identity, dimensions, view or bait plan is invalid");
    }
    for (const auto& view : current_level.view) {
        if (!std::isfinite(view.x) || !std::isfinite(view.y) || !std::isfinite(view.z)) {
            return hunter_control::fail(error, m_failure_reason = "Hunter camera projection is not finite");
        }
    }
    const auto raw = json::open(raw_level_path), manifest = json::open(template_manifest);
    if (!raw || !manifest) {
        return hunter_control::fail(error, m_failure_reason = "Hunter level or template manifest could not be read");
    }
    const auto map_data = raw->find<json::object>("mapData");
    if (!map_data) {
        return hunter_control::fail(error, m_failure_reason = "Raw hunter mapData is missing");
    }
    const json::value map(*map_data);
    const auto rows = map.find<json::array>("map"), tiles = map.find<json::array>("tiles");
    if (!rows || rows->size() != 7 || !tiles) {
        return hunter_control::fail(error, m_failure_reason = "Raw hunter map dimensions are invalid");
    }
    for (int native_row = 0; native_row < 7; ++native_row) {
        const auto& row = (*rows)[native_row];
        if (!row.is_array() || row.as_array().size() != 10) {
            return hunter_control::fail(error, m_failure_reason = "Raw hunter map row width is invalid");
        }
        for (int column = 0; column < 10; ++column) {
            const auto index = row.find<int>(column);
            if (!index || *index < 0 || static_cast<std::size_t>(*index) >= tiles->size()) {
                return hunter_control::fail(error, m_failure_reason = "Raw hunter tile index is invalid");
            }
            const auto& raw_tile = (*tiles)[*index];
            const auto height = hunter_control::height_code(raw_tile.get("heightType", std::string()));
            const auto buildable = hunter_control::buildable_code(raw_tile.get("buildableType", std::string()));
            const auto tile = current_level.get_item(native_row, column);
            if (!height || !buildable || tile.heightType != *height || tile.buildableType != *buildable ||
                tile.tileKey != raw_tile.get("tileKey", std::string())) {
                return hunter_control::fail(error, m_failure_reason = "Native and reversed raw terrain differ");
            }
            const auto pixel = Map::TileCalc2::get_tile_screen_pos(current_level, native_row, column, false);
            if (pixel.x < 0 || pixel.x >= 1280 || pixel.y < 0 || pixel.y >= 720) {
                return hunter_control::fail(
                    error,
                    m_failure_reason = "Hunter tile projection is outside the native image");
            }
            m_tile_pixels.emplace_back(pixel.x, pixel.y);
        }
    }
    m_preparation_evidence = {
        { "raw_level_path", utils::path_to_utf8_string(raw_level_path) },
        { "template_manifest", utils::path_to_utf8_string(template_manifest) },
        { "native_level_id", current_level.key.levelId },
        { "terrain_tiles_verified", 70 },
        { "raw_row_transform",
          "map[row] already indexes bottom-up raw tile storage; routes use native_row=6-raw_row" }
    };
    if (!hunter_control::spawn_contract(*raw, m_preparation_evidence, error)) {
        m_failure_reason = error ? *error : "Hunter spawn contract is not verified";
        return false;
    }
    // The manifest must tie pixel calibration to actual native tile projection,
    // rather than mistaking a sprite head rectangle for a tile center.
    const auto geometry = manifest->find<json::object>("native_stage_geometry");
    if (!geometry) {
        return hunter_control::fail(error, m_failure_reason = "Hunter manifest lacks native tile projection evidence");
    }
    const json::value calibration(*geometry);
    const auto canvas = calibration.find<json::array>("canvas");
    const int tolerance = calibration.get("pixel_tolerance", -1);
    if (calibration.get("projection", std::string()) != "normal" || !canvas || canvas->size() != 2 ||
        (*canvas)[0] != json::value(1280) || (*canvas)[1] != json::value(720) || tolerance < 0 || tolerance > 3) {
        return hunter_control::fail(
            error,
            m_failure_reason = "Hunter projection mode, native canvas or pixel tolerance is unsupported");
    }
    const auto projected = calibration.find<json::array>("tile_projections");
    if (!projected || projected->size() < 6) {
        return hunter_control::fail(error, m_failure_reason = "Hunter tile projection calibration is incomplete");
    }
    std::set<Point> calibrated;
    std::map<Point, Point> reference_centers;
    for (const auto& item : *projected) {
        const auto x = item.find<int>("x"), y = item.find<int>("y"), px = item.find<int>("pixel_x"),
                   py = item.find<int>("pixel_y");
        if (!x || !y || !px || !py || *x < 0 || *x >= 10 || *y < 0 || *y >= 7 || !calibrated.emplace(*x, *y).second ||
            std::abs(m_tile_pixels[*y * 10 + *x].x - *px) > tolerance ||
            std::abs(m_tile_pixels[*y * 10 + *x].y - *py) > tolerance) {
            return hunter_control::fail(
                error,
                m_failure_reason = "Hunter native tile projection disagrees with template calibration");
        }
        reference_centers.emplace(Point(*x, *y), Point(*px, *py));
    }
    for (const auto tile : { Point(2, 2), Point(4, 2), Point(3, 4), Point(7, 4), Point(8, 5), Point(7, 3) }) {
        if (!calibrated.contains(tile)) {
            return hunter_control::fail(
                error,
                m_failure_reason = "Hunter spawn, bait or sink projection is unverified");
        }
    }
    const auto spawns = manifest->find<json::array>("spawn_models");
    auto approaches = manifest->find<json::array>("approach_head_rois");
    const bool legacy_approaches = !approaches;
    if (legacy_approaches) {
        approaches = manifest->find<json::array>("sink_head_rois");
    }
    const auto lower = manifest->find("lower_roi");
    const auto battlefield_region = manifest->find("battlefield_roi");
    if (!spawns || spawns->size() != 2 || !approaches || approaches->empty() || !lower || !battlefield_region) {
        return hunter_control::fail(error, m_failure_reason = "Hunter calibrated pixel regions are incomplete");
    }
    m_lower_roi = hunter_control::read_rect(*lower);
    m_battlefield_roi = hunter_control::read_rect(*battlefield_region);
    const auto offsets = calibration.find<json::array>("spawn_roi_offsets_from_ground_center");
    if (!offsets || offsets->size() != 2) {
        return hunter_control::fail(error, m_failure_reason = "Hunter spawn-to-template offsets are unverified");
    }
    for (std::size_t index = 0; index < 2; ++index) {
        const auto rectangle = (*spawns)[index].find("roi");
        if (!rectangle || (m_spawn_rois[index] = hunter_control::read_rect(*rectangle)).empty()) {
            return hunter_control::fail(error, m_failure_reason = "Hunter spawn pixel region is invalid");
        }
        const Point tile(index == 0 ? 2 : 4, 2);
        const auto reference = reference_centers.at(tile);
        const auto& offset = (*offsets)[index];
        if (!offset.is_array() || offset.as_array().size() != 4 ||
            offset.find<int>(0) != m_spawn_rois[index].x - reference.x ||
            offset.find<int>(1) != m_spawn_rois[index].y - reference.y ||
            offset.find<int>(2) != m_spawn_rois[index].width || offset.find<int>(3) != m_spawn_rois[index].height) {
            return hunter_control::fail(
                error,
                m_failure_reason = "Hunter template rectangle is not tied to its spawn tile projection");
        }
        const auto actual = m_tile_pixels[tile.y * 10 + tile.x];
        m_spawn_rois[index].x += actual.x - reference.x;
        m_spawn_rois[index].y += actual.y - reference.y;
    }
    for (const auto& approach : *approaches) {
        const auto rectangle = hunter_control::read_rect(approach);
        if (rectangle.empty()) {
            return hunter_control::fail(error, m_failure_reason = "Hunter approach pixel region is invalid");
        }
        m_approach_rois.push_back(rectangle);
    }
    if (m_lower_roi.empty() || m_battlefield_roi.empty() || !m_analyzer.load(template_manifest, error)) {
        m_failure_reason = error ? *error : "Hunter detector could not be prepared";
        return false;
    }
    m_preparation_evidence["approach_roi_source"] = legacy_approaches ? "legacy_sink_head_rois" : "approach_head_rois";
    m_preparation_evidence["approach_zones_are_not_death_proof"] = true;
    m_preparation_evidence["tile_projection_verified"] = true;
    m_preparation_evidence["projection_pixel_tolerance"] = tolerance;
    m_preparation_evidence["spawn_roi_projected_from_current_level"] = true;
    m_preparation_evidence["spawn_window_source"] = "raw_all_hunter_spawn_actions_zero_time_no_later_waves_or_branches";
    m_bait_plan_index = bait_plan_index;
    m_battle_key = std::move(battle_key);
    m_spawn_window_verified = true;
    m_prepared = true;
    m_failure_reason.clear();
    return true;
}

bool asst::blackflow::BlackFlowHunterPreludeController::allows_plan(int plan_index) const noexcept
{
    if (!m_prepared || m_contract_breached || !m_frame_verified || plan_index < 0) {
        return false;
    }
    if (m_main_released) {
        return plan_index != m_bait_plan_index;
    }
    return plan_index == m_bait_plan_index && !m_bait_consumed;
}

void asst::blackflow::BlackFlowHunterPreludeController::deployed(
    int plan_index,
    const battle::OperNameTag& oper,
    Point tile)
{
    if (!m_prepared || m_contract_breached) {
        return;
    }
    if (plan_index != m_bait_plan_index) {
        if (!m_main_released) {
            m_contract_breached = true;
            m_main_released = false;
            m_failure_reason = "An unapproved main plan deployed before hunter clearance";
        }
        return;
    }
    if (m_bait_consumed) {
        m_contract_breached = true;
        m_main_released = false;
        m_failure_reason = "The bait plan was deployed more than once";
        return;
    }
    if (!allows_plan(plan_index) || tile != hunter_control::BaitTile || oper.name.empty() ||
        oper.role == battle::Role::Unknown) {
        m_contract_breached = true;
        m_main_released = false;
        m_failure_reason = "Bait deployment lacks approved frame, identity or calibrated tile";
        return;
    }
    m_bait = oper;
    m_bait_consumed = true;
}

asst::blackflow::HunterPreludeDecision asst::blackflow::BlackFlowHunterPreludeController::observe(
    const cv::Mat& image,
    bool normal_battle,
    const std::map<battle::OperNameTag, Point>& battlefield,
    const std::vector<battle::DeploymentOper>& cards,
    std::uint64_t observed_ms)
{
    HunterPreludeDecision decision;
    bool withdraw_for_unoccluded_observation = false;
    std::string retreat_reason;
    const bool timestamp_fresh = !m_last_observed_ms || observed_ms > *m_last_observed_ms;
    if (timestamp_fresh) {
        m_last_observed_ms = observed_ms;
    }
    bool bait_present = false, bait_cooling = false, battlefield_valid = true;
    std::vector<Rect> lower_occlusions;
    std::vector<Rect> battlefield_occlusions;
    int matching_cards = 0;
    if (m_prepared && !m_contract_breached) {
        for (const auto& [oper, tile] : battlefield) {
            if (tile.x < 0 || tile.x >= 10 || tile.y < 0 || tile.y >= 7) {
                battlefield_valid = false;
                continue;
            }
            if (m_bait && oper == *m_bait) {
                bait_present = true;
                if (tile != hunter_control::BaitTile) {
                    battlefield_valid = false;
                }
            }
            const auto pixel = m_tile_pixels[tile.y * 10 + tile.x];
            // Conservative unit envelope; never interpret a unit-covered face
            // search as empty. Bait remains an occlusion until its cooldown and
            // removal are both read from the actor's current observation.
            Rect envelope { std::max(0, pixel.x - 50), std::max(0, pixel.y - 140), 100, 165 };
            envelope.width = std::min(envelope.width, 1280 - envelope.x);
            envelope.height = std::min(envelope.height, 720 - envelope.y);
            if (hunter_control::overlaps(envelope, m_lower_roi)) {
                lower_occlusions.push_back(envelope);
            }
            if (hunter_control::overlaps(envelope, m_battlefield_roi)) {
                battlefield_occlusions.push_back(envelope);
            }
        }
        if (m_bait) {
            for (const auto& card : cards) {
                if (card.name == m_bait->name && card.role == m_bait->role) {
                    ++matching_cards;
                    bait_cooling = card.cooling && !card.available;
                }
            }
            bait_cooling = matching_cards == 1 && bait_cooling;
        }
    }
    if (bait_present) {
        m_bait_removal_verified = false;
    }
    const bool current_removal = m_bait_consumed && bait_cooling && !bait_present && battlefield_valid;
    // A previously verified removal does not expire when cooldown finishes.
    // Still require the uniquely identified hand card in the current frame;
    // neither an absent card nor stale battlefield bookkeeping proves visibility.
    const bool removal_visible =
        (current_removal || m_bait_removal_verified) && matching_cards == 1 && !bait_present && battlefield_valid;
    const bool lower_visible = removal_visible && lower_occlusions.empty();
    const bool battlefield_visible = lower_visible && battlefield_occlusions.empty();
    HunterPreludeObservation visual;
    if (m_prepared && !m_contract_breached) {
        HunterPreludeFrameContext context;
        context.battle_key = m_battle_key;
        context.frame_sequence = ++m_sequence;
        context.normal_battle_confirmed = normal_battle && timestamp_fresh;
        context.stage_geometry_verified = true;
        context.spawn_window_closed_verified = m_spawn_window_verified;
        context.lower_visibility_verified = lower_visible;
        context.battlefield_visibility_verified = battlefield_visible;
        context.battlefield_search_roi = m_battlefield_roi;
        context.battlefield_unit_occlusion_rois = battlefield_occlusions;
        context.observed_at_ms =
            observed_ms == std::numeric_limits<std::uint64_t>::max() ? observed_ms : observed_ms + 1;
        context.spawn_rois.assign(m_spawn_rois.begin(), m_spawn_rois.end());
        context.lower_search_roi = m_lower_roi;
        context.sink_head_rois = m_approach_rois;
        context.lower_unit_occlusion_rois = lower_occlusions;
        visual = m_analyzer.analyze(image, context);
        m_frame_verified = visual.frame_observable;
        if (visual.frame_observable && current_removal) {
            m_bait_removal_verified = true;
        }
        if (visual.frame_observable && (visual.battlefield_presence == HunterPresence::Present ||
                                        visual.lower_presence == HunterPresence::Present ||
                                        std::ranges::any_of(visual.spawns, [](const auto& slot) {
                                            return slot.presence == HunterPresence::Present;
                                        }))) {
            m_hunter_positive_verified = true;
        }
        // Removing the sole known bait is an observation-recovery action. A
        // skipped lower-lane sample is not proof of clearance, so this branch
        // never changes full-field visibility or the main-deployment gate.
        withdraw_for_unoccluded_observation =
            visual.frame_observable && m_spawn_window_verified && m_hunter_positive_verified &&
            visual.distinct_empty_spawn_frames >= 2 && m_bait_consumed && m_bait && bait_present && !bait_cooling &&
            battlefield_valid && battlefield.size() == 1 && !visual.bait_retreat_allowed;
        if (visual.frame_observable && m_bait_consumed && m_bait && bait_present && battlefield_valid &&
            (visual.bait_retreat_allowed || withdraw_for_unoccluded_observation)) {
            m_retreat_requested = true;
            decision.retreat_bait = hunter_control::BaitTile;
            retreat_reason = withdraw_for_unoccluded_observation ? "withdraw_for_unoccluded_observation"
                                                                 : "detector_bait_retreat_allowed";
        }
        if (m_main_released && visual.frame_observable &&
            (visual.battlefield_presence == HunterPresence::Present ||
             std::ranges::any_of(visual.spawns, [](const auto& slot) {
                 return slot.presence == HunterPresence::Present;
             }))) {
            m_main_released = false;
        }
        if (visual.frame_observable && battlefield_visible && visual.full_search_coverage_verified &&
            visual.all_hunters_cleared) {
            m_main_released = true;
        }
        decision.main_allowed = m_frame_verified && m_main_released;
    }
    else {
        m_frame_verified = false;
    }
    std::string stage = "unprepared";
    if (m_prepared) {
        if (m_contract_breached) {
            stage = "contract_breached";
        }
        else if (!m_frame_verified) {
            stage = "frame_unknown";
        }
        else if (m_main_released) {
            stage = "main_allowed";
        }
        else if (!m_bait_consumed) {
            stage = "bait_allowed";
        }
        else if (decision.retreat_bait) {
            stage = "retreat_bait_requested";
        }
        else if (!bait_present && !removal_visible) {
            stage = "bait_absence_unverified";
        }
        else if (lower_visible) {
            stage = "awaiting_visual_clearance";
        }
        else if (m_retreat_requested) {
            stage = "awaiting_retreat_confirmation";
        }
        else {
            stage = "awaiting_hunter_jump";
        }
    }
    std::vector<std::string> reasons;
    if (!m_failure_reason.empty()) {
        reasons.push_back(m_failure_reason);
    }
    if (!normal_battle) {
        reasons.emplace_back("Normal battle is not verified");
    }
    if (!timestamp_fresh) {
        reasons.emplace_back("Capture timestamp is stale or repeated");
    }
    if (m_bait_consumed && !bait_present && !removal_visible) {
        reasons.emplace_back("Bait absence lacks verified removal or a unique current hand card");
    }
    if (bait_present && bait_cooling) {
        reasons.emplace_back("Bait is simultaneously reported on field and on cooldown");
    }
    if (!battlefield_valid) {
        reasons.emplace_back("Battlefield coordinates or bait position are inconsistent");
    }
    if (!lower_occlusions.empty()) {
        reasons.emplace_back("Known allied sprites can obscure the lower search");
    }
    if (!battlefield_occlusions.empty()) {
        reasons.emplace_back("Known allied sprites can obscure full-battlefield residual search");
    }
    decision.diagnostics = { { "stage", stage },
                             { "battle_key", m_battle_key },
                             { "sequence", m_sequence },
                             { "observed_ms", observed_ms },
                             { "capture_timestamp_fresh", timestamp_fresh },
                             { "prepared", m_prepared },
                             { "failure_reason", m_failure_reason },
                             { "controller_reasons", reasons },
                             { "preparation", m_preparation_evidence },
                             { "bait_plan_index", m_bait_plan_index },
                             { "bait_consumed", m_bait_consumed },
                             { "bait_name", m_bait ? m_bait->name : std::string() },
                             { "bait_present", bait_present },
                             { "bait_cooling_verified", bait_cooling },
                             { "bait_matching_card_count", matching_cards },
                             { "retreat_requested", m_retreat_requested },
                             { "retreat_reason", retreat_reason },
                             { "withdraw_for_unoccluded_observation", withdraw_for_unoccluded_observation },
                             { "hunter_positive_verified", m_hunter_positive_verified },
                             { "retreat_input_confirmed", false },
                             { "bait_removal_verified", removal_visible },
                             { "bait_removal_history_verified", m_bait_removal_verified },
                             { "bait_branch", "ordinary_ranged_attack" },
                             { "lower_unit_occlusion_count", lower_occlusions.size() },
                             { "battlefield_unit_occlusion_count", battlefield_occlusions.size() },
                             { "battlefield_visibility_verified", battlefield_visible },
                             { "battlefield_coordinates_valid", battlefield_valid },
                             { "lower_visibility_verified", lower_visible },
                             { "spawn_window_closed_verified", m_spawn_window_verified },
                             { "main_allowed", decision.main_allowed },
                             { "main_released", m_main_released },
                             { "detector", hunter_control::detector_json(visual) } };
    return decision;
}
