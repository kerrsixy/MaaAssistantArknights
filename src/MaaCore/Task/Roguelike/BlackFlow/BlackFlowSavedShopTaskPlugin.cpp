#include "BlackFlowSavedShopTaskPlugin.h"

#ifdef ASST_DEBUG
#include <algorithm>
#include <climits>
#include <utility>

#include "BlackFlowResumeRoster.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "Utils/Logger.hpp"
#include "Vision/Matcher.h"
#include "Vision/OCRer.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowShopShelf.h"

namespace asst::blackflow
{
std::optional<SavedShopServiceRequest> parse_saved_shop_service_request(const json::value& params, std::string* error)
{
    const auto reject = [error](const char* reason) -> std::optional<SavedShopServiceRequest> {
        if (error) {
            *error = reason;
        }
        return std::nullopt;
    };
    if (!params.get("blackflow_saved_shop_service_only", false) || params.get("theme", "") != "BlackFlow" ||
        params.get("starts_count", -1) != 0 || params.get("scan_account_modules", true) ||
        params.get("blackflow_life_game_only", false) || params.get("stashed_recruit_service_only", false) ||
        params.get("formation_only", false) || params.get("duel_prepare_only", false) ||
        !params.get("battle_resume_stage", std::string {}).empty() ||
        !params.get("duel_battle_resume", std::string {}).empty()) {
        return reject("saved shop service requires explicit BlackFlow saved-only cache mode");
    }
    // This service must finish at MapAck. The generic investment plugin can
    // call exit_then_stop(true) when its finite count/full-stop policy fires.
    // Refuse those configurations before any account navigation or service input.
    if (params.get("mode", 0) != 0 || params.get("stop_when_investment_full", false) ||
        params.get("investments_count", INT_MAX) != INT_MAX) {
        return reject("saved shop service forbids investment stop policies or nonstandard mode");
    }
    SavedShopServiceRequest request;
    request.floor = params.get("blackflow_saved_shop_service_floor", 0);
    request.roster_file = params.get("blackflow_resume_roster_file", std::string {});
    if (request.floor < 1 || request.floor > 6 || request.roster_file.empty()) {
        return reject("saved shop service requires observed floor and assisted roster evidence");
    }
    const auto snapshot = json::open(utils::path(request.roster_file));
    if (!snapshot || snapshot->get("schema", 0) != 1 || snapshot->get("acceptance_eligible", true) ||
        !snapshot->get("requires_fresh_same_uid_verification_before_import", false)) {
        return reject("saved shop service has no valid assisted roster declaration");
    }
    request.uid = snapshot->get("uid", std::string {});
    request.client = snapshot->get("client", std::string {});
    if (request.uid.empty() || (request.client != "Official" && request.client != "Bilibili")) {
        return reject("saved shop service account identity is unknown");
    }
    return request;
}

std::optional<int> recognize_saved_shop_service(const cv::Mat& image, int expected_floor)
{
    if (image.empty() || expected_floor < 1 || expected_floor > 6) {
        return std::nullopt;
    }
    Matcher leave(image), coin(image), modal(image);
    leave.set_task_info("BlackFlow@Roguelike@StageTraderLeave");
    coin.set_task_info("BlackFlow@Roguelike@TraderRandomShopping");
    modal.set_task_info("BlackFlow@Roguelike@TraderRandomShoppingConfirm");
    if (!leave.analyze() || !coin.analyze() || modal.analyze() || recognize_saved_shop_service_map(image)) {
        return std::nullopt;
    }
    const auto floor_task = Task.get<OcrTaskInfo>("BlackFlow@Roguelike@NextLevel");
    if (!floor_task || floor_task->text.size() != 6) {
        return std::nullopt;
    }
    OCRer title(image);
    title.set_task_info("BlackFlow@Roguelike@NextLevel");
    const auto titles = title.analyze();
    if (!titles || std::ranges::none_of(*titles, [&](const auto& value) {
            return value.text == floor_task->text.at(static_cast<std::size_t>(expected_floor - 1));
        })) {
        return std::nullopt;
    }
    // Positive title is used only after both shop anchors; it never proves Map.
    return expected_floor;
}

bool recognize_saved_shop_service_map(const cv::Mat& image)
{
    if (image.empty()) {
        return false;
    }
    Matcher map(image);
    map.set_task_info("BlackFlow@Roguelike@SavedShopMapAck");
    return map.analyze().has_value();
}

bool BlackFlowSavedShopTaskPlugin::load_params(const json::value& params)
{
    m_account_verified = false;
    m_started = false;
    m_finished = false;
    m_pending = PendingWork::None;
    m_request = parse_saved_shop_service_request(params);
    return m_request.has_value();
}

bool BlackFlowSavedShopTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    if (!m_request || details.get("subtask", "") != "ProcessTask") {
        return false;
    }
    const auto task = details.get("details", "task", "");
    if (msg == AsstMsg::SubTaskCompleted && task == "BlackFlow@Roguelike@AccountRead") {
        m_pending = PendingWork::Account;
        return true;
    }
    if (msg == AsstMsg::SubTaskStart && task == "BlackFlow@Roguelike@SavedShopReady") {
        m_pending = PendingWork::Ready;
        return true;
    }
    if (msg == AsstMsg::SubTaskCompleted && task == "BlackFlow@Roguelike@SavedShopMapAck") {
        m_pending = PendingWork::Map;
        return true;
    }
    return false;
}

bool BlackFlowSavedShopTaskPlugin::_run()
{
    const auto work = std::exchange(m_pending, PendingWork::None);
    const auto reject = [&](const std::string& reason) {
        LogError << __FUNCTION__ << reason;
        m_task_ptr->set_enable(false);
        return false;
    };
    if (!m_request || !m_session || m_finished) {
        return reject("saved shop service is not active");
    }
    if (work == PendingWork::Account) {
        // AccountRead's existing plugin ran fresh AccountSnapshotTask before this
        // outer completion. Re-check its exact current-run roster, never maxima.
        const auto& account = m_config->account_profile();
        const auto snapshot = json::open(utils::path(m_request->roster_file));
        std::string error;
        const auto roster = snapshot && account ? parse_resume_roster(*snapshot, *account, &error) : std::nullopt;
        if (!account || m_config->account_uid() != m_request->uid || m_config->account_client() != m_request->client ||
            account->uid() != m_request->uid || account->client() != m_request->client || !roster ||
            roster->size() != m_config->status().opers.size()) {
            return reject("saved shop service account or restored roster was not verified");
        }
        for (const auto& [name, strength] : *roster) {
            const auto found = m_config->status().opers.find(name);
            if (found == m_config->status().opers.end() || found->second.elite != strength.elite ||
                found->second.level != strength.level) {
                return reject("saved shop service roster differs from confirmed run evidence");
            }
        }
        m_account_verified = true;
        return true;
    }
    if (!m_account_verified) {
        return reject("saved shop service has no fresh same-UID gate");
    }
    if (work == PendingWork::Ready) {
        if (m_started) {
            return true;
        }
        const auto floor = recognize_saved_shop_service(ctrler()->get_image(), m_request->floor);
        std::string error;
        if (!floor || !m_session->begin_debug_shop_service(*floor, true, true, &error)) {
            return reject(error.empty() ? "saved shop frame is not confirmed" : error);
        }
        m_started = true;
        auto info = basic_info_with_what("BlackFlowSavedShopServiceStarted");
        info["details"] = json::object {
            { "client", m_request->client }, { "uid", m_request->uid },  { "floor", *floor },
            { "map_move", false },           { "node_id_known", false }, { "unattended_acceptance_eligible", false }
        };
        callback(AsstMsg::SubTaskExtraInfo, info);
        return true;
    }
    if (work == PendingWork::Map) {
        std::string error;
        if (!m_started ||
            !m_session->finish_debug_shop_service(recognize_saved_shop_service_map(ctrler()->get_image()), &error)) {
            return reject(error.empty() ? "saved shop service has no actual map acknowledgement" : error);
        }
        m_finished = true;
        auto info = basic_info_with_what("BlackFlowSavedShopServiceCompleted");
        info["details"] = json::object { { "client", m_request->client },
                                         { "uid", m_request->uid },
                                         { "floor", m_request->floor },
                                         { "ack_task", "BlackFlow@Roguelike@SavedShopMapAck" },
                                         { "actual_map_ack", true },
                                         { "settlement", false },
                                         { "unattended_acceptance_eligible", false } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        return true;
    }
    return reject("saved shop service has no recognized work");
}
}
#endif
