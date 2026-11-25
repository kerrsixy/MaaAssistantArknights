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
                           { "absent_score", slot.absent_score },
                           { "distinct_empty_frames", slot.distinct_empty_frames } });
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
    return prepare(template_manifest, raw_level_path, current_level, bait_plan_index, std::move(battle_key), 1, error);
}

bool asst::blackflow::BlackFlowHunterPreludeController::prepare(
    const std::filesystem::path& template_manifest,
    const std::filesystem::path& raw_level_path,
    const Map::Level& current_level,
    int bait_plan_index,
    std::string battle_key,
    int bait_attempt_limit,
    std::string* error)
{
    HunterOpening opening { .bait_plans = { HunterBaitPlan { .plan_index = bait_plan_index,
                                                             .location = hunter_control::BaitTile,
                                                             .required_targets = { { 2, 2 }, { 4, 2 } } } },
                            .attempt_limit = bait_attempt_limit };
    return prepare(template_manifest, raw_level_path, current_level, opening, std::move(battle_key), error);
}

bool asst::blackflow::BlackFlowHunterPreludeController::prepare(
    const std::filesystem::path& template_manifest,
    const std::filesystem::path& raw_level_path,
    const Map::Level& current_level,
    const HunterOpening& opening,
    std::string battle_key,
    std::string* error)
{
    *this = {};
    if (error) {
        error->clear();
    }
    m_failure_reason = "Hunter prelude is not prepared";
    if (!opening.valid() || battle_key.empty() || current_level.get_width() != 10 || current_level.get_height() != 7 ||
        current_level.key.levelId != "obt/roguelike/ro6/level_rogue6_2-5" || current_level.view.size() < 2) {
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
    if (manifest->contains("scoped_task_policy") && !m_task_policy.load(manifest->at("scoped_task_policy"), error)) {
        return hunter_control::fail(error, "Hunter scoped task policy is invalid");
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
            if (tile.tileKey == "tile_road" || tile.tileKey == "tile_start" || tile.tileKey == "tile_end" ||
                tile.tileKey == "tile_telin" || tile.tileKey == "tile_telout" || tile.tileKey == "tile_hole") {
                (native_row >= 4 ? m_lower_possible_tiles : m_upper_ground_tiles).emplace_back(column, native_row);
            }
        }
    }
    m_preparation_evidence = {
        { "scoped_task_policy_enabled", manifest->contains("scoped_task_policy") },
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
    json::array bait_evidence;
    for (const auto& plan : opening.bait_plans) {
        const auto tile = current_level.get_item(plan.location.y, plan.location.x);
        if (!calibrated.contains(plan.location) || tile.buildableType < 1 || tile.buildableType > 3) {
            return hunter_control::fail(error, m_failure_reason = "Hunter bait tile is not calibrated and buildable");
        }
        m_bait_plans.emplace(plan.plan_index, plan);
        json::array required, forbidden;
        for (Point target : plan.required_targets) {
            required.emplace_back(json::array { target.x, target.y });
        }
        for (Point target : plan.forbidden_targets) {
            forbidden.emplace_back(json::array { target.x, target.y });
        }
        bait_evidence.emplace_back(
            json::object { { "plan_index", plan.plan_index },
                           { "location", json::array { plan.location.x, plan.location.y } },
                           { "required_targets", std::move(required) },
                           { "forbidden_targets", std::move(forbidden) } });
    }
    m_preparation_evidence["bait_plans"] = std::move(bait_evidence);
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
    m_bait_plan_index = opening.bait_plans.front().plan_index;
    m_battle_key = std::move(battle_key);
    m_spawn_window_verified = true;
    m_prepared = true;
    m_failure_reason.clear();
    m_bait_attempt_limit = opening.attempt_limit;
    return true;
}

bool asst::blackflow::BlackFlowHunterPreludeController::allows_plan(int plan_index) const noexcept
{
    if (!m_prepared || m_contract_breached || !m_frame_verified || plan_index < 0) {
        return false;
    }
    if (m_main_released) {
        return !is_bait_plan(plan_index);
    }
    if (!is_bait_plan(plan_index)) {
        // Potential-plan gate only. Exact identity/skill/effect permission is
        // checked by candidate_allowed before a deployment can be attempted.
        return m_task_policy.has_plan(plan_index) && m_task_world.normal_verified && m_task_world.first_bait_consumed &&
               m_task_world.bait_removal_verified;
    }
    if (m_bait_attempts >= m_bait_attempt_limit) {
        return false;
    }
    if (m_bait_consumed && !m_replacement_bait_allowed) {
        return false;
    }
    return std::ranges::any_of(m_bait_plans.at(plan_index).required_targets, [&](Point tile) {
        return m_spawn_presence[tile.x == 2 ? 0 : 1] == HunterPresence::Present;
    });
}

bool asst::blackflow::BlackFlowHunterPreludeController::attack_range_allowed(
    int plan_index,
    const battle::AttackRange& range) const
{
    const auto it = m_bait_plans.find(plan_index);
    if (it == m_bait_plans.end()) {
        return true;
    }
    const auto covered = [&](Point target) {
        return std::ranges::find(range, target - it->second.location) != range.end();
    };
    return std::ranges::all_of(it->second.required_targets, covered) &&
           std::ranges::none_of(it->second.forbidden_targets, covered);
}

bool asst::blackflow::BlackFlowHunterPreludeController::bait_skill_allowed(
    const std::map<battle::OperNameTag, Point>& battlefield) const noexcept
{
    // Prelude range contracts approve basic attacks only. Active skills may change
    // range or add area damage; ordinary skill handling resumes after main release.
    static_cast<void>(battlefield);
    return false;
}

bool asst::blackflow::BlackFlowHunterPreludeController::candidate_allowed(
    int plan_index,
    const HunterTaskCandidate& candidate)
{
    // Legacy manifests opt out of additional runtime identity/effect admission.
    // Their static range qualification remains unchanged; live deployment still
    // passes allows_plan through the existing plan-exhaustion gate.
    if (m_prepared && !m_preparation_evidence.get("scoped_task_policy_enabled", false)) {
        return true;
    }
    if (!m_prepared || m_contract_breached || !m_frame_verified || !m_task_world.normal_verified) {
        return false;
    }
    if (m_main_released) {
        return !is_bait_plan(plan_index);
    }
    if (is_bait_plan(plan_index)) {
        return allows_plan(plan_index) && !m_task_policy.reserve_guard_identity(candidate);
    }
    const auto permit = m_task_policy.assess(plan_index, candidate, m_task_world);
    if (m_task_decisions.size() < 24) {
        m_task_decisions.emplace_back(
            json::object { { "frame_sequence", m_sequence },
                           { "plan_index", plan_index },
                           { "operator_id", candidate.operator_id },
                           { "elite", candidate.elite },
                           { "selected_skill", candidate.selected_skill },
                           { "allowed", permit.allowed },
                           { "accepted_aoe_uncertainty", permit.accepted_aoe_uncertainty },
                           { "reason", permit.reason },
                           { "all_hunters_cleared_claimed", false } });
    }
    if (permit.allowed) {
        m_task_permissions[{ plan_index, candidate.oper }] = candidate;
    }
    return permit.allowed;
}

std::optional<asst::blackflow::HunterTaskCandidate>
    asst::blackflow::BlackFlowHunterPreludeController::active_task(const battle::OperNameTag& oper) const
{
    const auto found = m_early_deployed.find(oper);
    return found == m_early_deployed.end() ? std::nullopt : std::optional(found->second.second);
}

bool asst::blackflow::BlackFlowHunterPreludeController::skill_allowed(const HunterTaskCandidate& candidate) const
{
    if (!m_prepared || m_contract_breached || !m_frame_verified || !m_task_world.normal_verified) {
        return false;
    }
    if (m_main_released) {
        return true;
    }
    const auto found = m_early_deployed.find(candidate.oper);
    if (!m_task_world.normal_verified || found == m_early_deployed.end() ||
        found->second.second.location != candidate.location || !m_task_policy.skill_known_allowed(candidate)) {
        return false;
    }
    // Admission and servicing an already committed unit are different actions.
    // Its own sprite may now occlude the scoped upper detector. Do not turn that
    // expected self-occlusion into another permanent no-skill loop. Risk-tier
    // service retains its explicit uncertainty; this never certifies clearance
    // or authorizes another deployment, and ends on actual field departure.
    return true;
}

void asst::blackflow::BlackFlowHunterPreludeController::deployed(
    int plan_index,
    const battle::OperNameTag& oper,
    Point tile)
{
    if (!m_prepared || m_contract_breached) {
        return;
    }
    if (!is_bait_plan(plan_index)) {
        const auto permission = m_task_permissions.find({ plan_index, oper });
        if (!m_main_released && permission != m_task_permissions.end() && permission->second.location == tile &&
            m_task_world.normal_verified &&
            m_task_policy.assess(plan_index, permission->second, m_task_world).allowed) {
            m_early_deployed[oper] = { plan_index, permission->second };
            m_task_permissions.erase(permission);
            return;
        }
        if (!m_main_released) {
            m_contract_breached = true;
            m_main_released = false;
            m_failure_reason = "An unapproved main plan deployed before hunter clearance";
        }
        return;
    }
    if (m_bait_consumed && !m_replacement_bait_allowed) {
        m_contract_breached = true;
        m_main_released = false;
        m_failure_reason = "A replacement bait deployed without current residual-hunter evidence";
        return;
    }
    if (!allows_plan(plan_index) || tile != m_bait_plans.at(plan_index).location || oper.name.empty() ||
        oper.role == battle::Role::Unknown) {
        m_contract_breached = true;
        m_main_released = false;
        m_failure_reason = "Bait deployment lacks approved frame, identity or calibrated tile";
        return;
    }
    m_bait = oper;
    m_bait_plan_index = plan_index;
    m_bait_tile = tile;
    m_bait_consumed = true;
    ++m_bait_attempts;
    m_bait_removal_verified = false;
    m_replacement_bait_allowed = false;
    m_retreat_requested = false;
}

asst::blackflow::HunterPreludeDecision asst::blackflow::BlackFlowHunterPreludeController::observe(
    const cv::Mat& image,
    bool normal_battle,
    const std::map<battle::OperNameTag, Point>& battlefield,
    const std::vector<battle::DeploymentOper>& cards,
    std::uint64_t observed_ms)
{
    HunterPreludeDecision decision;
    m_task_world = {};
    m_task_permissions.clear();
    m_replacement_bait_allowed = false;
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
                if (!m_bait_tile || tile != *m_bait_tile) {
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
    const bool spawn_only_probe = m_bait_consumed && bait_present && !bait_cooling;
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
        context.spawn_only_probe = spawn_only_probe;
        context.battlefield_search_roi = m_battlefield_roi;
        context.battlefield_unit_occlusion_rois = battlefield_occlusions;
        context.observed_at_ms =
            observed_ms == std::numeric_limits<std::uint64_t>::max() ? observed_ms : observed_ms + 1;
        context.spawn_rois.assign(m_spawn_rois.begin(), m_spawn_rois.end());
        context.lower_search_roi = m_lower_roi;
        context.sink_head_rois = m_approach_rois;
        context.lower_unit_occlusion_rois = lower_occlusions;
        visual = m_analyzer.analyze(image, context);
        for (std::size_t index = 0; index < m_spawn_presence.size(); ++index) {
            m_spawn_presence[index] = visual.spawns[index].presence;
        }
        m_frame_verified = visual.frame_observable;
        if (visual.frame_observable && current_removal) {
            m_bait_removal_verified = true;
        }
        std::erase_if(m_early_deployed, [&](const auto& entry) {
            const auto found = battlefield.find(entry.first);
            return found == battlefield.end() || found->second != entry.second.second.location;
        });
        m_task_world.normal_verified = visual.frame_observable && normal_battle && timestamp_fresh && battlefield_valid;
        m_task_world.spawn_contract_verified = m_spawn_window_verified;
        m_task_world.first_bait_consumed = m_bait_consumed;
        m_task_world.bait_removal_verified = removal_visible;
        m_task_world.spawns = visual.spawns;
        m_task_world.lower_possible_tiles = m_lower_possible_tiles;
        m_task_world.upper_visible = visual.task_upper_visibility_verified;
        m_task_world.upper_presence = visual.task_upper_presence;
        m_task_world.upper_empty_frames = visual.task_upper_empty_frames;
        m_task_world.lower_visible = lower_visible;
        m_task_world.lower_presence = visual.lower_presence;
        for (const auto& [oper, known] : m_early_deployed) {
            static_cast<void>(oper);
            m_task_world.guard_present =
                m_task_world.guard_present || m_task_policy.kind(known.first) == HunterTaskKind::Guard;
            m_task_world.medical_present =
                m_task_world.medical_present || m_task_policy.kind(known.first) == HunterTaskKind::Medical;
        }
        auto upper_heads = visual.battlefield_head_candidates;
        upper_heads.insert(
            upper_heads.end(),
            visual.task_upper_head_candidates.begin(),
            visual.task_upper_head_candidates.end());
        for (Point tile : m_upper_ground_tiles) {
            const auto pixel = m_tile_pixels[tile.y * 10 + tile.x];
            const Rect possible_head { pixel.x - 50, pixel.y - 140, 100, 140 };
            if (std::ranges::any_of(upper_heads, [&](const Rect& head) {
                    return hunter_control::overlaps(possible_head, head);
                })) {
                m_task_world.upper_positive_possible_tiles.push_back(tile);
            }
        }
        // A retry is a separate bait deployment, not permission to deploy the
        // main team. It needs positive upper-hunter evidence after the previous
        // bait's identity and removal have both been verified in current views.
        const bool only_approved_tasks = std::ranges::all_of(battlefield, [&](const auto& entry) {
            const auto known = m_early_deployed.find(entry.first);
            return known != m_early_deployed.end() && known->second.second.location == entry.second;
        });
        m_replacement_bait_allowed = visual.frame_observable && removal_visible && battlefield_valid &&
                                     only_approved_tasks && m_spawn_window_verified &&
                                     m_bait_attempts < m_bait_attempt_limit && !m_main_released &&
                                     std::ranges::any_of(
                                         visual.spawns,
                                         [](const auto& slot) { return slot.presence == HunterPresence::Present; });
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
        const bool targeted_spawns_empty =
            m_bait && m_bait_plans.contains(m_bait_plan_index) &&
            std::ranges::all_of(m_bait_plans.at(m_bait_plan_index).required_targets, [&](Point tile) {
                return visual.spawns[tile.x == 2 ? 0 : 1].distinct_empty_frames >= 2;
            });
        const bool bait_and_approved_tasks_only = std::ranges::all_of(battlefield, [&](const auto& entry) {
            if (m_bait && entry.first == *m_bait) {
                return true;
            }
            const auto known = m_early_deployed.find(entry.first);
            return known != m_early_deployed.end() && known->second.second.location == entry.second;
        });
        withdraw_for_unoccluded_observation = visual.frame_observable && m_spawn_window_verified &&
                                              m_hunter_positive_verified && targeted_spawns_empty && m_bait_consumed &&
                                              m_bait && bait_present && !bait_cooling && battlefield_valid &&
                                              bait_and_approved_tasks_only && !visual.bait_retreat_allowed;
        if (visual.frame_observable && m_bait_consumed && m_bait && bait_present && battlefield_valid &&
            (visual.bait_retreat_allowed || withdraw_for_unoccluded_observation)) {
            m_retreat_requested = true;
            decision.retreat_bait = m_bait_tile;
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
        else if (m_replacement_bait_allowed) {
            stage = "replacement_bait_allowed";
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
                             { "bait_location",
                               m_bait_tile ? json::value(json::array { m_bait_tile->x, m_bait_tile->y })
                                           : json::value(nullptr) },
                             { "bait_consumed", m_bait_consumed },
                             { "bait_attempts", m_bait_attempts },
                             { "bait_attempt_limit", m_bait_attempt_limit },
                             { "replacement_bait_allowed", m_replacement_bait_allowed },
                             { "bait_name", m_bait ? m_bait->name : std::string() },
                             { "bait_present", bait_present },
                             { "bait_cooling_verified", bait_cooling },
                             { "spawn_only_probe", spawn_only_probe },
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
    decision.diagnostics["scoped_task_decisions"] = std::move(m_task_decisions);
    m_task_decisions = {};
    decision.diagnostics["scoped_upper_visible"] = m_task_world.upper_visible;
    decision.diagnostics["scoped_upper_presence"] = static_cast<int>(m_task_world.upper_presence);
    decision.diagnostics["scoped_upper_empty_frames"] = m_task_world.upper_empty_frames;
    decision.diagnostics["approved_guard_present"] = m_task_world.guard_present;
    decision.diagnostics["approved_medical_present"] = m_task_world.medical_present;
    decision.diagnostics["scoped_deployed_count"] = static_cast<int>(m_early_deployed.size());
    return decision;
}
