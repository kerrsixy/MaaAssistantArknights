#include "RoguelikeBattleTaskPlugin.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <future>

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Controller/Controller.h"
#include "MaaUtils/ImageIo.h"
#include "Task/Roguelike/BlackFlow/BlackFlowSummonRetreat.h"
#include "Utils/Logger.hpp"
#include "Utils/Platform.hpp"
#include "Utils/WorkingDir.hpp"
#include "Vision/Roguelike/BlackFlow/BlackFlowSoulObserver.h"

bool asst::RoguelikeBattleTaskPlugin::observe_blackflow_souls()
{
    using namespace blackflow;
    using namespace battle;
    if (!m_soul_observer || need_exit()) {
        LogInfo << __FUNCTION__ << "Soul guard diagnostic early return"
                << (m_soul_observer ? "exit_requested" : "observer_unavailable");
        return false;
    }
    const auto timestamp = [] {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
    };
    const auto key = m_config->status().run_id + ":" + m_stage_name;
    const OperNameTag owner { Role::Sniper, "维什戴尔" };
    const auto deployed = m_battlefield_opers.find(owner);
    const auto direction = m_observed_deploy_directions.find(owner);
    const auto elite = m_oper_elite.find(owner.name);
    const bool cooling = std::ranges::any_of(m_cur_deployment_opers, [&](const auto& card) {
        return card.name == owner.name && card.role == owner.role && card.cooling;
    });
    const bool owner_present = deployed != m_battlefield_opers.end() && !cooling;
    const bool diagnostic_owner_cached = deployed != m_battlefield_opers.end();
    const std::optional<Point> diagnostic_owner_tile =
        diagnostic_owner_cached ? std::optional<Point>(deployed->second) : std::nullopt;
    const std::optional<int64_t> diagnostic_elite =
        elite == m_oper_elite.end() ? std::nullopt : std::optional<int64_t>(elite->second);
    const std::optional<DeployDirection> diagnostic_direction = direction == m_observed_deploy_directions.end()
                                                                    ? std::nullopt
                                                                    : std::optional<DeployDirection>(direction->second);
    std::optional<BossBodyEvidence> diagnostic_boss;
    std::uint64_t diagnostic_boss_sequence = 0;
    std::size_t diagnostic_speculative_soul_count = 0;
    bool diagnostic_speculative_souls_discarded = false;
    std::optional<SummonRetreatObservation> diagnostic_verification;
    std::vector<Point> diagnostic_allowed_sites;
    bool diagnostic_allowed_sites_known = false;
    const auto diagnose = [&](const char* reason,
                              const SummonRetreatObservation& observation,
                              const SummonRetreatDecision& decision,
                              const cv::Mat& frame,
                              std::optional<bool> input_accepted = std::nullopt) {
        json::array allowed_sites, candidates;
        for (const auto& tile : diagnostic_allowed_sites) {
            allowed_sites.emplace_back(json::array { tile.x, tile.y });
        }
        for (const auto& tile : observation.candidates) {
            candidates.emplace_back(json::array { tile.x, tile.y });
        }
        const auto age = observation.received_ms >= observation.observed_ms && observation.observed_ms != 0
                             ? static_cast<std::int64_t>(observation.received_ms - observation.observed_ms)
                             : -1;
        const auto action_name = [](SummonRetreatAction action) {
            switch (action) {
            case SummonRetreatAction::Select:
                return "Select";
            case SummonRetreatAction::CancelSelection:
                return "CancelSelection";
            case SummonRetreatAction::RetreatSelection:
                return "RetreatSelection";
            default:
                return "None";
            }
        };
        json::value details = json::object {
            { "reason", reason },
            { "boss_execution_policy", "one const task, async or deferred; joined before permission" },
            { "speculative_soul_count", diagnostic_speculative_soul_count },
            { "speculative_souls_discarded", diagnostic_speculative_souls_discarded },
            { "stage", m_stage_name },
            { "battle_key", key },
            { "sequence", observation.sequence },
            { "observed_ms", observation.observed_ms },
            { "received_ms", observation.received_ms },
            { "age_ms", age },
            { "capture_clock_source",
              "existing pre-get_image steady timestamp; "
              "engine frame time unverified" },
            { "owner_in_battlefield_cache", diagnostic_owner_cached },
            { "owner_cooling", cooling },
            { "owner_present", owner_present },
            { "elite_known", diagnostic_elite.has_value() },
            { "elite", diagnostic_elite.value_or(-1) },
            { "direction_known", diagnostic_direction.has_value() },
            { "direction", diagnostic_direction ? static_cast<int>(*diagnostic_direction) : -1 },
            { "owner_tile",
              !diagnostic_owner_tile ? json::array {}
                                     : json::array { diagnostic_owner_tile->x, diagnostic_owner_tile->y } },
            { "allowed_sites_known_in_this_invocation", diagnostic_allowed_sites_known },
            { "allowed_sites", allowed_sites },
            { "candidates", candidates },
            { "boss_state", static_cast<int>(observation.boss) },
            { "selection_view", observation.selection_view },
            { "normal_view", observation.normal_view },
            { "user_paused", observation.user_paused },
            { "selection_owned", m_soul_selection_owned },
            { "selected_name", observation.selected_name },
            { "selected_id", observation.selected_id },
            { "retreat_button_present", observation.retreat_button_present },
            { "decision", action_name(decision.action) },
            { "decision_tile", decision.tile ? json::array { decision.tile->x, decision.tile->y } : json::array {} },
            { "frame_available", !frame.empty() },
            { "input_result_known", input_accepted.has_value() },
            { "input_accepted", input_accepted.value_or(false) },
            { "removal_confirmed", false },
        };
        if (diagnostic_boss) {
            details["boss_evidence"] = json::object {
                { "sequence", diagnostic_boss_sequence },
                { "same_observation", diagnostic_boss_sequence == observation.sequence },
                { "body_score", diagnostic_boss->score },
                { "positive_hp", diagnostic_boss->positive_hp },
                { "body",
                  json::array { diagnostic_boss->body.x,
                                diagnostic_boss->body.y,
                                diagnostic_boss->body.width,
                                diagnostic_boss->body.height } },
                { "hp_fill",
                  json::array { diagnostic_boss->hp_fill.x,
                                diagnostic_boss->hp_fill.y,
                                diagnostic_boss->hp_fill.width,
                                diagnostic_boss->hp_fill.height } },
            };
        }
        if (diagnostic_verification) {
            details["verification_before_cleanup"] = json::object {
                { "sequence", diagnostic_verification->sequence },
                { "observed_ms", diagnostic_verification->observed_ms },
                { "received_ms", diagnostic_verification->received_ms },
                { "boss_state", static_cast<int>(diagnostic_verification->boss) },
                { "selected_name", diagnostic_verification->selected_name },
                { "selected_id", diagnostic_verification->selected_id },
                { "retreat_button_present", diagnostic_verification->retreat_button_present },
            };
        }
        // Evidence IO is only reached on a rejected path or after its actual input
        // receipt. The diagnostic clock labels the file; it never replaces an
        // observation timestamp.
        const auto recorded_ms = timestamp();
        details["evidence_recorded_ms"] = recorded_ms;
        const auto directory = UserDir.get() / utils::path("BlackFlowEvidence/soul_guard_diagnostics");
        const auto stem = std::to_string(recorded_ms) + "-" + std::to_string(observation.sequence) + "-" + reason;
        const bool write_rejected_frame = !frame.empty() && !input_accepted.has_value();
        if (input_accepted.has_value()) {
            const auto original_frame =
                UserDir.get() / utils::path("BlackFlowEvidence/soul_retreat") /
                utils::path(
                    std::to_string(observation.observed_ms) + "-" + std::to_string(observation.sequence) + ".png");
            details["png_source"] = "existing_action_record";
            details["png_path"] = utils::path_to_utf8_string(original_frame);
        }
        else {
            details["png_source"] = frame.empty() ? "unavailable_no_new_capture" : "rejected_observation";
            details["png_path"] = write_rejected_frame
                                      ? utils::path_to_utf8_string(directory / utils::path(stem + ".png"))
                                      : std::string();
        }
        details["additional_png_write_attempted"] = write_rejected_frame;
        LogInfo << __FUNCTION__ << "Soul guard diagnostic" << details.to_string();
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (!error) {
            std::ofstream(directory / utils::path(stem + ".json"), std::ios::binary) << details.format();
            // Input paths reuse record()'s same-frame PNG; never encode it twice.
            if (write_rejected_frame) {
                MAA_NS::imwrite(directory / utils::path(stem + ".png"), frame);
            }
        }
    };
    const auto record =
        [&](const char* event, const SummonRetreatObservation& observation, bool accepted, const cv::Mat& frame) {
            auto info = basic_info_with_what("BlackFlowSoulRetreat");
            info["details"] = json::object {
                { "stage", m_stage_name },
                { "event", event },
                { "sequence", observation.sequence },
                { "observed_ms", observation.observed_ms },
                { "received_ms", observation.received_ms },
                { "boss_alive", observation.boss == BossVitalState::ConfirmedAlive },
                { "selected_name", observation.selected_name },
                { "selected_id", observation.selected_id },
                { "input_accepted", accepted },
                { "removal_confirmed", false },
            };
            callback(AsstMsg::SubTaskExtraInfo, info);
            LogInfo << __FUNCTION__ << info["details"].to_string();
            const auto directory = UserDir.get() / "BlackFlowEvidence/soul_retreat";
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (!error) {
                const auto stem = std::to_string(observation.observed_ms) + "-" + std::to_string(observation.sequence);
                std::ofstream(directory / (stem + ".json"), std::ios::binary) << info.format();
                MAA_NS::imwrite(directory / (stem + ".png"), frame);
            }
        };
    const auto observe = [&](const cv::Mat& frame,
                             std::uint64_t captured,
                             bool selected,
                             bool cleanup,
                             const std::vector<SoulSite>* normal_sites = nullptr) {
        SummonRetreatObservation observation;
        observation.battle_key = key;
        observation.sequence = ++m_soul_sequence;
        observation.observed_ms = captured;
        observation.owner_present = owner_present;
        observation.user_paused = m_soul_observer->paused(frame);
        observation.selection_view = selected;
        observation.normal_view = !selected;
        diagnostic_speculative_soul_count = 0;
        diagnostic_speculative_souls_discarded = false;
        if (!observation.user_paused && !cleanup) {
            const auto* observer = m_soul_observer.get();
            // A cv::Mat value retains this exact frame through join. Only the
            // immutable observer calculation may run off the battle thread.
            // Deferred execution is a safe serial fallback if no worker is available.
            auto boss_future = std::async(std::launch::async | std::launch::deferred, [observer, frame, selected] {
                return observer->boss(frame, selected);
            });
            std::vector<Point> speculative_souls;
            if (selected) {
                observation.selected_name = analyze_detail_page_oper_name(frame, Role::Unknown);
                const auto ids = BattleData.get_ids(Role::Drone, observation.selected_name);
                if (ids.size() == 1) {
                    observation.selected_id = ids.front();
                }
                observation.retreat_button_present = m_soul_observer->retreat_button(frame, m_retreat_button_pos);
            }
            else if (normal_sites != nullptr) {
                speculative_souls = m_soul_observer->souls(frame, *normal_sites);
                diagnostic_speculative_soul_count = speculative_souls.size();
            }
            // No observation, authorization or input is published before get().
            // Future destruction also waits if main-thread perception unwinds.
            diagnostic_boss = boss_future.get();
            diagnostic_boss_sequence = observation.sequence;
            observation.boss = diagnostic_boss->alive() ? BossVitalState::ConfirmedAlive : BossVitalState::Unknown;
            if (!selected && observation.boss == BossVitalState::ConfirmedAlive) {
                observation.candidates = std::move(speculative_souls);
            }
            else if (!selected) {
                diagnostic_speculative_souls_discarded = !speculative_souls.empty();
            }
        }
        // This is completion time of both same-frame computations, not a new
        // observation origin. The pre-get_image observed_ms is unchanged.
        observation.received_ms = timestamp();
        return observation;
    };
    if (!m_soul_selection_owned) {
        if (!owner_present || direction == m_observed_deploy_directions.end() || elite == m_oper_elite.end() ||
            elite->second < 0 || elite->second > 2 ||
            BattleData.get_range(owner.role, owner.name, elite->second) == BattleDataConfig::EmptyRange) {
            SummonRetreatObservation metadata;
            metadata.battle_key = key;
            metadata.sequence = m_soul_sequence;
            diagnose(
                !owner_present                                    ? "owner_missing_or_cooling"
                : direction == m_observed_deploy_directions.end() ? "direction_unknown"
                : elite == m_oper_elite.end()                     ? "elite_unknown"
                : elite->second < 0 || elite->second > 2          ? "elite_out_of_range"
                                                                  : "range_unknown",
                metadata,
                {},
                {});
            return false;
        }
        DeploymentOper oper;
        oper.name = owner.name;
        oper.role = owner.role;
        std::vector<SoulSite> sites;
        std::vector<Point> allowed;
        for (const auto& relative : get_attack_range(oper, direction->second)) {
            const auto tile = deployed->second + relative;
            const auto position = m_normal_tile_info.find(tile);
            if (tile == deployed->second || position == m_normal_tile_info.end() ||
                position->second.buildable == LocationType::None ||
                position->second.buildable == LocationType::Invalid) {
                continue;
            }
            sites.push_back({ tile, position->second.pos });
            allowed.emplace_back(tile);
        }
        diagnostic_allowed_sites = allowed;
        diagnostic_allowed_sites_known = true;
        const auto captured = timestamp();
        const auto frame = ctrler()->get_image();
        auto observation = observe(frame, captured, false, false, &sites);
        if (observation.user_paused) {
            m_waiting_for_manual_resume = true;
            diagnose("normal_user_paused", observation, {}, frame);
            return true;
        }
        if (observation.boss != BossVitalState::ConfirmedAlive) {
            diagnose("normal_boss_not_confirmed_alive", observation, {}, frame);
            return false;
        }
        if (observation.candidates.empty()) {
            diagnose("normal_no_soul_candidates", observation, {}, frame);
            return false;
        }
        m_soul_retreat = std::make_unique<BlackFlowSummonRetreat>(
            key,
            "魂灵之影",
            "token_10035_wisdel_wward",
            deployed->second,
            std::move(allowed));
        const auto decision = m_soul_retreat->observe(observation);
        if (decision.action != SummonRetreatAction::Select || !decision.tile ||
            !m_soul_retreat->authorized(decision.action) || need_exit()) {
            diagnose("normal_policy_rejected", observation, decision, frame);
            return false;
        }
        // Direct selection records the controller receipt; the older helper
        // ignores this receipt. No pause toggle is part of this transaction.
        const bool accepted = ctrler()->click(m_normal_tile_info.at(*decision.tile).pos);
        m_soul_retreat->input_result(decision.action, accepted);
        m_soul_selection_owned = accepted;
        record("select", observation, accepted, frame);
        diagnose("select_input_receipt", observation, decision, frame, accepted);
        if (!accepted || !sleep(200) || need_exit()) {
            return accepted;
        }
    }
    const auto captured = timestamp();
    const auto frame = ctrler()->get_image();
    auto evidence_frame = frame;
    auto observation = observe(frame, captured, true, false);
    auto decision = m_soul_retreat->observe(observation);
    if (observation.user_paused || need_exit()) {
        // Keep ownership so a later running frame can finish or cancel. Never
        // dismiss the selection or resume a pause introduced by the user.
        diagnose("selected_user_paused_or_exit", observation, decision, frame);
        return true;
    }
    if (decision.action == SummonRetreatAction::None) {
        // OCR failure or an expired analysis grants no input. Reobserve a fresh
        // frame with Unknown Boss evidence to cancel only our own selection.
        diagnostic_verification = observation;
        const auto cleanup_captured = timestamp();
        const auto cleanup = ctrler()->get_image();
        evidence_frame = cleanup;
        observation = observe(cleanup, cleanup_captured, true, true);
        decision = m_soul_retreat->observe(observation);
        if (observation.user_paused || need_exit()) {
            diagnose("cleanup_user_paused_or_exit", observation, decision, evidence_frame);
            return true;
        }
    }
    if (!m_soul_retreat->authorized(decision.action)) {
        diagnose("selected_action_not_authorized", observation, decision, evidence_frame);
        return true;
    }
    if (decision.action == SummonRetreatAction::RetreatSelection) {
        // The current exact-name selection is already verified. Selecting the
        // tile again or using click_retreat() would introduce extra toggles.
        const bool accepted = ctrler()->click(m_retreat_button_pos);
        m_soul_retreat->input_result(decision.action, accepted);
        record("retreat_current_selection", observation, accepted, evidence_frame);
        diagnose("retreat_input_receipt", observation, decision, evidence_frame, accepted);
        if (accepted) {
            m_soul_selection_owned = false;
            m_soul_retreat.reset();
            SummonRetreatObservation after_observation;
            after_observation.sequence = ++m_soul_sequence;
            after_observation.observed_ms = timestamp();
            const auto after = ctrler()->get_image();
            after_observation.received_ms = timestamp();
            record("after_input_unverified", after_observation, accepted, after);
        }
    }
    else if (decision.action == SummonRetreatAction::CancelSelection) {
        const bool accepted = cancel_oper_selection();
        m_soul_retreat->input_result(decision.action, accepted);
        record("cancel_owned_selection", observation, accepted, evidence_frame);
        diagnose("cancel_input_receipt", observation, decision, evidence_frame, accepted);
        if (accepted) {
            m_soul_selection_owned = false;
            m_soul_retreat.reset();
        }
    }
    return true;
}
