#include "RoguelikeBattleTaskPlugin.h"

#include <algorithm>
#include <chrono>
#include <fstream>

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
    const auto observe = [&](const cv::Mat& frame, std::uint64_t captured, bool selected, bool cleanup) {
        SummonRetreatObservation observation;
        observation.battle_key = key;
        observation.sequence = ++m_soul_sequence;
        observation.observed_ms = captured;
        observation.owner_present = owner_present;
        observation.user_paused = m_soul_observer->paused(frame);
        observation.selection_view = selected;
        observation.normal_view = !selected;
        if (!observation.user_paused && !cleanup) {
            observation.boss = m_soul_observer->boss(frame, selected).alive() ? BossVitalState::ConfirmedAlive
                                                                              : BossVitalState::Unknown;
        }
        if (selected && !cleanup && !observation.user_paused) {
            observation.selected_name = analyze_detail_page_oper_name(frame, Role::Unknown);
            const auto ids = BattleData.get_ids(Role::Drone, observation.selected_name);
            if (ids.size() == 1) {
                observation.selected_id = ids.front();
            }
            observation.retreat_button_present = m_soul_observer->retreat_button(frame, m_retreat_button_pos);
        }
        observation.received_ms = timestamp();
        return observation;
    };
    if (!m_soul_selection_owned) {
        if (!owner_present || direction == m_observed_deploy_directions.end() || elite == m_oper_elite.end() ||
            elite->second < 0 || elite->second > 2 ||
            BattleData.get_range(owner.role, owner.name, elite->second) == BattleDataConfig::EmptyRange) {
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
        const auto captured = timestamp();
        const auto frame = ctrler()->get_image();
        auto observation = observe(frame, captured, false, false);
        if (observation.user_paused) {
            m_waiting_for_manual_resume = true;
            return true;
        }
        if (observation.boss != BossVitalState::ConfirmedAlive) {
            return false;
        }
        observation.candidates = m_soul_observer->souls(frame, sites);
        observation.received_ms = timestamp();
        if (observation.candidates.empty()) {
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
            return false;
        }
        // Direct selection records the controller receipt; the older helper
        // ignores this receipt. No pause toggle is part of this transaction.
        const bool accepted = ctrler()->click(m_normal_tile_info.at(*decision.tile).pos);
        m_soul_retreat->input_result(decision.action, accepted);
        m_soul_selection_owned = accepted;
        record("select", observation, accepted, frame);
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
        return true;
    }
    if (decision.action == SummonRetreatAction::None) {
        // OCR failure or an expired analysis grants no input. Reobserve a fresh
        // frame with Unknown Boss evidence to cancel only our own selection.
        const auto cleanup_captured = timestamp();
        const auto cleanup = ctrler()->get_image();
        evidence_frame = cleanup;
        observation = observe(cleanup, cleanup_captured, true, true);
        decision = m_soul_retreat->observe(observation);
        if (observation.user_paused || need_exit()) {
            return true;
        }
    }
    if (!m_soul_retreat->authorized(decision.action)) {
        return true;
    }
    if (decision.action == SummonRetreatAction::RetreatSelection) {
        // The current exact-name selection is already verified. Selecting the
        // tile again or using click_retreat() would introduce extra toggles.
        const bool accepted = ctrler()->click(m_retreat_button_pos);
        m_soul_retreat->input_result(decision.action, accepted);
        record("retreat_current_selection", observation, accepted, evidence_frame);
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
        if (accepted) {
            m_soul_selection_owned = false;
            m_soul_retreat.reset();
        }
    }
    return true;
}
