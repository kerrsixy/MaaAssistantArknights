#include "BlackFlowLifeGameTaskPlugin.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <opencv2/imgcodecs.hpp>

#include "BlackFlowModifierEvidenceProducer.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "Task/ProcessTask.h"
#include "Utils/Logger.hpp"
#include "Utils/Platform.hpp"
#include "Utils/WorkingDir.hpp"

namespace asst::blackflow
{
bool BlackFlowLifeGameTaskPlugin::load_params(const json::value& params)
{
    m_enabled = params.get("blackflow_life_game_enabled", true);
    m_only = params.get("blackflow_life_game_only", false);
    set_retry_times(0);
    return m_config->get_theme() == RoguelikeTheme::BlackFlow && (m_enabled || m_only);
}

bool BlackFlowLifeGameTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    if (m_config->get_theme() != RoguelikeTheme::BlackFlow || msg != AsstMsg::SubTaskStart ||
        details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }
    const std::string task = details.get("details", "task", "");
    return (m_only && task == "BlackFlow@Roguelike@LifeGameRun") ||
           (!m_only && m_enabled && task == "BlackFlow@Roguelike@LifeGameLobby");
}

bool BlackFlowLifeGameTaskPlugin::resource_action(std::string_view task)
{
    if (need_exit() || !Task.get(task)) {
        return false;
    }
    return ProcessTask(*this, { std::string(task) }).set_retry_times(0).run();
}

bool BlackFlowLifeGameTaskPlugin::select(const Rect& rect)
{
    const auto action = Task.get("BlackFlow@Roguelike@LifeGameSelect");
    if (!action || action->action != ProcessTaskAction::ClickRect || rect.empty()) {
        return false;
    }
    const Rect previous = action->specific_rect;
    action->specific_rect = rect;
    const bool ok = resource_action("BlackFlow@Roguelike@LifeGameSelect");
    action->specific_rect = previous;
    return ok;
}

std::optional<LifeGameScreenObservation> BlackFlowLifeGameTaskPlugin::observe_stable(bool details)
{
    BlackFlowLifeGameAnalyzer first(ctrler()->get_image());
    auto before = first.analyze(m_planner.nodes(), details);
    const auto valid_balance = [this](const LifeGameScreenObservation& observation) {
        if (!observation.page_verified) {
            return false;
        }
        if (observation.collection_complete) {
            return (!observation.collected && !observation.total) ||
                   (observation.collected && observation.total && observation.collected == observation.total);
        }
        return observation.points && observation.collected && observation.total &&
               *observation.total == static_cast<int>(m_planner.nodes().size());
    };
    if (!valid_balance(before)) {
        return std::nullopt;
    }
    if (!resource_action("BlackFlow@Roguelike@LifeGameObserve")) {
        return std::nullopt;
    }
    const auto stable_image = ctrler()->get_image();
    BlackFlowLifeGameAnalyzer second(stable_image);
    auto after = second.analyze(m_planner.nodes(), details);
    if (!valid_balance(after) || before.collection_complete != after.collection_complete ||
        before.points != after.points || before.collected != after.collected || before.total != after.total ||
        before.selected_name != after.selected_name || before.selected_state != after.selected_state ||
        before.selected_cost != after.selected_cost) {
        return std::nullopt;
    }
    m_last_observation_stamp = std::to_string(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
    m_last_evidence_file.clear();
    // Preserve this exact validated image; no additional screenshot or UI action.
    if (!UserDir.empty() && !m_config->account_uid().empty() && !m_config->account_client().empty()) {
        std::error_code ec;
        const auto directory = UserDir.get() / utils::path("debug/blackflow/life_game");
        std::filesystem::create_directories(directory, ec);
        if (!ec) {
            const auto file = directory / utils::path(
                                              "life_" + m_config->account_client() + "_" + m_config->account_uid() +
                                              "_" + m_last_observation_stamp + ".png");
            try {
                if (cv::imwrite(utils::path_to_utf8_string(file), stable_image)) {
                    m_last_evidence_file = file;
                }
            }
            catch (const cv::Exception& error) {
                LogWarn << __FUNCTION__ << "Optional same-frame Life Game evidence could not be saved" << error.what();
            }
        }
    }
    m_last_observation = after;
    m_evidence.available_points = after.points;
    return after;
}

bool BlackFlowLifeGameTaskPlugin::seek_first()
{
    // Boundary tasks must positively recognize a known end viewport. Missing arrows
    // or a failed navigation action alone never establish complete tree coverage.
    for (int steps = 0; steps < 32 && !need_exit(); ++steps) {
        if (resource_action("BlackFlow@Roguelike@LifeGameAtFirst")) {
            return true;
        }
        if (!resource_action("BlackFlow@Roguelike@LifeGamePrevious")) {
            return false;
        }
    }
    return false;
}

bool
    BlackFlowLifeGameTaskPlugin::record_selected(const LifeGameScreenObservation& observation, const Location& location)
{
    if (observation.selected_name.empty() || observation.selected_state == LifeGameSelectedState::Unknown ||
        (observation.selected_state == LifeGameSelectedState::Unpurchased && !observation.selected_cost)) {
        return false;
    }
    const auto ids = BlackFlowLifeGameAnalyzer::selected_node_ids(observation, m_planner.nodes());
    if (ids.size() != 1) {
        // Same name, effect and cost may occur at several positions. Without a
        // verified viewport-to-row/order mapping, none of those IDs is asserted.
        ++m_ambiguous_selections;
        return true;
    }
    const auto& id = ids.front();
    auto& current = observation.selected_state == LifeGameSelectedState::Active ? m_evidence.purchased_ids
                                                                                : m_evidence.unpurchased_ids;
    const auto& opposite = observation.selected_state == LifeGameSelectedState::Active ? m_evidence.unpurchased_ids
                                                                                       : m_evidence.purchased_ids;
    if (std::ranges::find(opposite, id) != opposite.end()) {
        return false;
    }
    if (std::ranges::find(current, id) == current.end()) {
        current.push_back(id);
    }
    m_locations.insert_or_assign(id, location);
    return true;
}

bool BlackFlowLifeGameTaskPlugin::scan_viewport(int viewport)
{
    auto screen = observe_stable();
    if (!screen || screen->selection_candidates.empty() || screen->selection_candidates.size() > 14) {
        return false;
    }
    // Selecting a node opens a detail panel and moves the tree. The first choice
    // is followed by a fresh detection; subsequent choices use that fresh layout.
    auto targets = screen->selection_candidates;
    std::vector<Rect> visited;
    for (std::size_t step = 0; step < 14 && visited.size() < targets.size(); ++step) {
        const auto next = std::ranges::find_if(targets, [&visited](const Rect& candidate) {
            return std::ranges::none_of(visited, [&candidate](const Rect& done) {
                return std::abs(candidate.x - done.x) < 35 && std::abs(candidate.y - done.y) < 35;
            });
        });
        if (next == targets.end() || !select(*next)) {
            return false;
        }
        const Rect selected_before = *next;
        auto detail = observe_stable();
        if (!detail || detail->selection_candidates.size() != targets.size()) {
            return false;
        }
        const auto selected = detail->selected_rect;
        if (!selected || std::abs(selected->x - selected_before.x) > 30 ||
            std::abs(selected->y - selected_before.y) > 60) {
            return false;
        }
        if (visited.empty()) {
            targets = detail->selection_candidates;
        }
        if (!record_selected(*detail, { viewport, *selected })) {
            return false;
        }
        visited.push_back(*selected);
        targets = detail->selection_candidates;
    }
    return visited.size() == targets.size();
}

bool BlackFlowLifeGameTaskPlugin::scan_tree()
{
    if (!seek_first()) {
        return false;
    }
    for (int viewport = 0; viewport < 32 && !need_exit(); ++viewport) {
        if (!scan_viewport(viewport)) {
            return false;
        }
        if (resource_action("BlackFlow@Roguelike@LifeGameAtLast")) {
            return true;
        }
        if (!resource_action("BlackFlow@Roguelike@LifeGameNext")) {
            return false;
        }
    }
    return false;
}

bool BlackFlowLifeGameTaskPlugin::go_to(const Location& location)
{
    if (location.viewport < 0 || location.viewport >= 32 || !seek_first()) {
        return false;
    }
    for (int i = 0; i < location.viewport; ++i) {
        if (!resource_action("BlackFlow@Roguelike@LifeGameNext")) {
            return false;
        }
    }
    const auto screen = observe_stable();
    if (!screen || screen->selection_candidates.empty()) {
        return false;
    }
    const auto found = std::ranges::min_element(screen->selection_candidates, {}, [&location](const Rect& rect) {
        const int dx = rect.x - location.rect.x;
        const int dy = rect.y - location.rect.y;
        return dx * dx + dy * dy;
    });
    return found != screen->selection_candidates.end() && std::abs(found->x - location.rect.x) <= 35 &&
           std::abs(found->y - location.rect.y) <= 60 && select(*found);
}

void BlackFlowLifeGameTaskPlugin::report(std::string status, std::string reason, bool unknown)
{
    auto info = basic_info_with_what("BlackFlowLifeGame");
    const auto plan = m_planner.next(m_evidence);
    json::array unknown_ids;
    for (const auto& id : plan.unknown_ids) {
        unknown_ids.emplace_back(id);
    }
    std::optional<LifeModifierReceipt> life_receipt;
    bool profile_rebound = false;
    std::string modifier_quality = "not_a_complete_UI_receipt_or_no_bound_profile";
    if (!unknown && status == "complete" && m_config->account_profile()) {
        BlackFlowModifierEvidenceProducer modifiers;
        if (modifiers.load(ResDir.get() / utils::path("roguelike/BlackFlow/modifier_components.json"))) {
            life_receipt = modifiers.completed_life_game(
                m_last_observation,
                m_planner.nodes(),
                m_config->account_client(),
                m_config->account_uid(),
                m_last_observation_stamp,
                utils::path_to_utf8_string(m_last_evidence_file),
                &modifier_quality);
            if (life_receipt) {
                auto updated = m_config->account_profile()->with_life_modifier_evidence(*life_receipt);
                if (updated) {
                    profile_rebound =
                        m_config->bind_account_profile(std::make_shared<BlackFlowAccountProfile>(std::move(*updated)));
                    modifier_quality = profile_rebound ? "complete_lit_UI_and_exact_source_set_bound"
                                                       : "profile_rebind_identity_changed";
                }
                else {
                    modifier_quality = "profile_receipt_identity_or_fields_invalid";
                }
            }
        }
        else {
            modifier_quality = "optional_modifier_source_unavailable";
        }
    }
    info["details"] = json::object {
        { "status", std::move(status) },
        { "reason", std::move(reason) },
        { "unknown", unknown },
        { "standalone", m_only },
        { "client", m_config->account_client() },
        { "uid", m_config->account_uid() },
        { "points", m_last_observation.points ? json::value(*m_last_observation.points) : json::value(nullptr) },
        { "collected",
          m_last_observation.collected ? json::value(*m_last_observation.collected) : json::value(nullptr) },
        { "total", m_last_observation.total ? json::value(*m_last_observation.total) : json::value(nullptr) },
        { "verified_upgrades", m_verified_upgrades },
        { "terminal_upgrades", m_terminal_upgrades },
        { "debit_verified", m_debit_verified },
        { "debit_unobservable", m_debit_unobservable },
        { "collection_complete", m_last_observation.collection_complete },
        { "verified_active_ids", m_evidence.purchased_ids.size() },
        { "verified_unpurchased_ids", m_evidence.unpurchased_ids.size() },
        { "ambiguous_selections", m_ambiguous_selections },
        { "unknown_ids", std::move(unknown_ids) },
        { "retained_page", unknown || m_only },
        { "observation_stamp", m_last_observation_stamp },
        { "frame_saved", !m_last_evidence_file.empty() },
        { "evidence_file", utils::path_to_utf8_string(m_last_evidence_file) },
        { "account_modifier_profile_rebound", profile_rebound },
        { "modifier_data_quality", modifier_quality },
        { "source_conditioned_all_active_ids", life_receipt ? static_cast<int>(life_receipt->active_ids.size()) : 0 },
        { "modifier_source_hash", life_receipt ? life_receipt->source_hash : "" },
    };
    callback(AsstMsg::SubTaskExtraInfo, info);
}

bool BlackFlowLifeGameTaskPlugin::finish(std::string status, std::string reason, bool unknown)
{
    if (unknown) {
        LogWarn << "BlackFlow Life Game observation is incomplete; retaining page:" << reason;
        if (m_task_ptr) {
            m_task_ptr->set_enable(false);
        }
    }
    else if (!m_only && !resource_action("BlackFlow@Roguelike@LifeGameClose")) {
        return finish("unknown", "Could not verify the return to the roguelike lobby", true);
    }
    report(std::move(status), std::move(reason), unknown);
    // A handled unknown must not trigger the generic plugin retry loop or resume
    // the enclosing StartExplore chain. The owning ProcessTask was disabled above.
    return true;
}

bool BlackFlowLifeGameTaskPlugin::_run()
{
    m_evidence = {};
    m_locations.clear();
    m_last_observation = {};
    m_last_observation_stamp.clear();
    m_last_evidence_file.clear();
    m_verified_upgrades = 0;
    m_terminal_upgrades = 0;
    m_debit_verified = false;
    m_debit_unobservable = false;
    m_ambiguous_selections = 0;
    std::string error;
    const auto catalog = json::open(ResDir.get() / "roguelike" / "BlackFlow" / "life_game.json", true, true);
    if (!catalog || !m_planner.load(*catalog, &error)) {
        return finish("unknown", "Life Game resource unavailable: " + error, true);
    }
    BlackFlowLifeGameAnalyzer initial(ctrler()->get_image());
    if (!initial.analyze(m_planner.nodes(), false).page_verified &&
        !resource_action("BlackFlow@Roguelike@LifeGameOpen")) {
        return finish("unknown", "Could not verify the Life Game entry page", true);
    }
    auto screen = observe_stable(false);
    if (!screen) {
        return finish("unknown", "Point balance or collected count is not stable and verified", true);
    }
    if (screen->collection_complete) {
        return finish(
            "complete",
            "The UI explicitly reports collection complete; numeric balance is not assumed",
            false);
    }
    if (*screen->points == 0) {
        return finish("no_points", "Verified zero points; no tree scanning or upgrade attempted", false);
    }
    if (!scan_tree()) {
        return finish("unknown", "Bounded viewport scan or node detail recognition did not complete", true);
    }
    for (std::size_t purchases = 0; purchases < m_planner.nodes().size() && !need_exit(); ++purchases) {
        const auto plan = m_planner.next(m_evidence);
        if (plan.status == LifeGamePlanStatus::NoPoints || plan.status == LifeGamePlanStatus::Complete) {
            return finish(plan.status == LifeGamePlanStatus::NoPoints ? "no_points" : "complete", plan.reason, false);
        }
        if (plan.status == LifeGamePlanStatus::InsufficientPoints && plan.unknown_ids.empty()) {
            return finish("insufficient_points", plan.reason, false);
        }
        if (plan.status != LifeGamePlanStatus::Ready || !plan.next || !m_locations.contains(plan.next->id)) {
            return finish("unknown", plan.reason, true);
        }
        const auto& node = *plan.next;
        if (!go_to(m_locations.at(node.id))) {
            return finish("unknown", "Could not reacquire the planned node viewport", true);
        }
        const auto before = observe_stable();
        if (!before ||
            BlackFlowLifeGameAnalyzer::selected_node_ids(*before, m_planner.nodes()) !=
                std::vector<std::string> { node.id } ||
            before->selected_state != LifeGameSelectedState::Unpurchased || before->selected_cost != node.cost ||
            !before->points || *before->points < node.cost) {
            return finish("unknown", "Planned node identity, cost, or available points changed", true);
        }
        if (!resource_action("BlackFlow@Roguelike@LifeGameUpgrade")) {
            return finish("unknown", "Upgrade resource action did not complete", true);
        }
        const auto after = observe_stable();
        if (after && after->collection_complete && !after->points) {
            if (!BlackFlowLifeGameAnalyzer::verifies_terminal_upgrade(
                    *before,
                    *after,
                    node,
                    static_cast<int>(m_planner.nodes().size()))) {
                return finish("unknown", "Terminal completion lacks the full count or planned node activation", true);
            }
            std::erase(m_evidence.unpurchased_ids, node.id);
            m_evidence.purchased_ids.push_back(node.id);
            ++m_terminal_upgrades;
            m_debit_verified = false;
            m_debit_unobservable = true;
            return finish(
                "complete",
                "Full collected count and planned node activation verified; final debit is hidden",
                false);
        }
        if (!after || !BlackFlowLifeGameAnalyzer::verifies_upgrade(*before, *after, node)) {
            return finish("unknown", "Upgrade not proven by active state, count +1 and exact point debit", true);
        }
        std::erase(m_evidence.unpurchased_ids, node.id);
        m_evidence.purchased_ids.push_back(node.id);
        ++m_verified_upgrades;
        m_debit_verified = true;
        m_debit_unobservable = false;
        report("upgrade_verified", node.id, false);
        if (after->collection_complete) {
            return finish("complete", "Collection complete after an observed exact debit", false);
        }
    }
    return finish("unknown", "Life Game upgrade iteration bound reached or task interrupted", true);
}
}
