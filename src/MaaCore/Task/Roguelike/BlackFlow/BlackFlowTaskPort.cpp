#include "BlackFlowTaskPort.h"
#include "Task/Roguelike/RoguelikeConfig.h"

#include "BlackFlowInventoryCleanup.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "BlackFlowMovePreviewAnalyzer.h"
#include "BlackFlowMovementRecognition.h"
#include "MaaUtils/ImageIo.h"
#include "Utils/WorkingDir.hpp"
#include <fstream>

#include "Vision/Roguelike/BlackFlow/BlackFlowFloor.h"

#include "Config/Roguelike/BlackFlow/BlackFlowNodeExecutionConfig.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "Task/AbstractTask.h"
#include "Task/ProcessTask.h"
#include "Utils/Logger.hpp"
#include "Utils/StringMisc.hpp"
#include "Vision/OCRer.h"
#include "Vision/Roguelike/RoguelikeParameterAnalyzer.h"

namespace asst::blackflow
{
namespace
{
constexpr std::string_view CurrentActionPointsTask = "BlackFlow@Roguelike@CurrentActionPoints";
constexpr std::string_view CurrentIngotsTask = "BlackFlow@Roguelike@CurrentIngots";
constexpr std::string_view CurrentMovementCapacityTask = "BlackFlow@Roguelike@CurrentMovementCapacity";
constexpr std::string_view MovePreviewWaitTask = "BlackFlow@Roguelike@MovePreviewWait";
constexpr std::string_view MovePreviewEnterWalkTask = "BlackFlow@Roguelike@MovePreviewEnterWalk";
constexpr std::string_view MovePreviewEnterCraftedTask = "BlackFlow@Roguelike@MovePreviewEnterCrafted";
constexpr std::string_view MovePreviewCannotEnterTask = "BlackFlow@Roguelike@MovePreviewCannotEnter";
constexpr std::string_view MovePreviewCostTask = "BlackFlow@Roguelike@MovePreviewCost";
constexpr std::string_view MovePreviewCostCraftedTask = "BlackFlow@Roguelike@MovePreviewCostCrafted";
constexpr std::string_view MovePreviewDisplayedNameTask = "BlackFlow@Roguelike@MovePreviewDisplayedName";
constexpr std::string_view MovePreviewIdealSourceTask = "BlackFlow@Roguelike@MovePreviewIdealSource";
constexpr std::string_view MovePreviewConfirmTask = "BlackFlow@Roguelike@MovePreviewConfirm";
constexpr std::string_view MovePreviewConfirmCompletedTask = "BlackFlow@Roguelike@MovePreviewConfirmCompleted";
constexpr std::string_view EnteredPageClassificationTask = "BlackFlow@Roguelike@EnteredPageClassification";
constexpr std::string_view EnteredPageClassificationEncounterPrepareTask =
    "BlackFlow@Roguelike@EnteredPageClassificationEncounterPrepare";
constexpr std::string_view StageEncounterOcrTask = "BlackFlow@Roguelike@StageEncounterOcr";

void set_error(std::string* error, std::string message)
{
    if (error != nullptr) {
        *error = std::move(message);
    }
}

std::optional<int> recognize_integer(const cv::Mat& image, std::string_view task_name)
{
    const auto task = Task.get<OcrTaskInfo>(task_name);
    if (task == nullptr) {
        return std::nullopt;
    }
    OCRer analyzer(image);
    analyzer.set_task_info(task);
    const auto results = analyzer.analyze();
    if (!results.has_value() || results->size() != 1) {
        return std::nullopt;
    }
    int value = 0;
    if (!utils::chars_to_number(results->front().text, value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::pair<int, int>> recognize_movement_capacity_from_text(std::string text)
{
    std::erase_if(text, [](unsigned char value) { return std::isspace(value) != 0; });
    const std::size_t slash = text.find('/');
    if (slash == std::string::npos) {
        return std::nullopt;
    }
    int occupied = 0;
    int capacity = 0;
    if (!utils::chars_to_number(text.substr(0, slash), occupied) ||
        !utils::chars_to_number(text.substr(slash + 1), capacity) || occupied < 0 || capacity <= 0 ||
        occupied > capacity || capacity > 64) {
        return std::nullopt;
    }
    return std::pair { occupied, capacity };
}

std::optional<std::string>
    recognize_text(const cv::Mat& image, std::string_view task_name, const std::vector<std::string>& required = {})
{
    const auto task = Task.get<OcrTaskInfo>(task_name);
    if (task == nullptr) {
        return std::nullopt;
    }
    OCRer analyzer(image);
    analyzer.set_task_info(task);
    if (!required.empty()) {
        analyzer.set_required(required);
    }
    const auto results = analyzer.analyze();
    if (!results.has_value()) {
        return std::nullopt;
    }
    const std::vector<std::string>& whitelist = required.empty() ? task->text : required;
    // 按 OCR 返回顺序取首个合法结果。
    for (const auto& result : *results) {
        if (std::ranges::find(whitelist, result.text) != whitelist.end()) {
            return result.text;
        }
    }
    return std::nullopt;
}

Rect shrink_to_center(const Rect& rect, double ratio)
{
    const int width = std::max(1, static_cast<int>(rect.width * ratio));
    const int height = std::max(1, static_cast<int>(rect.height * ratio));
    return Rect { rect.x + (rect.width - width) / 2, rect.y + (rect.height - height) / 2, width, height };
}

} // namespace

class BlackFlowTaskPort::ProcessTaskContext final : public AbstractTask, public BlackFlowInventoryContext
{
public:
    ProcessTaskContext(const AsstCallback& callback, Assistant* inst, std::string_view task_chain) :
        AbstractTask(callback, inst, task_chain)
    {
        set_retry_times(0);
    }

    bool execute(std::vector<std::string> tasks, std::string* error) override
    {
        m_tasks = std::move(tasks);
        m_last_task.clear();
        m_last_image.reset();
        const bool succeeded = AbstractTask::run();
        if (!succeeded) {
            set_error(error, "ProcessTask did not reach a successful terminal task");
        }
        return succeeded;
    }

    [[nodiscard]] const std::string& last_task() const noexcept override { return m_last_task; }

    [[nodiscard]] std::shared_ptr<cv::Mat> last_image() const noexcept { return m_last_image; }

    void report_cleanup_status(std::string_view status, std::string detail) override
    {
        auto info = basic_info_with_what("BlackFlowInventoryCleanup");
        json::object status_details { { "status", std::string(status) } };
        if (!detail.empty()) {
            status_details["name"] = std::move(detail);
        }
        info["details"] = std::move(status_details);
        callback(AsstMsg::SubTaskExtraInfo, info);
    }

    [[nodiscard]] cv::Mat capture() const override { return ctrler()->get_image(); }

    bool click(const Rect& rect) const override { return ctrler()->click(rect); }

    bool interrupted() const override { return need_exit(); }

    void wait(unsigned milliseconds) const override { sleep(milliseconds); }

    bool precise_swipe_supported() const override
    {
        return ControlFeat::support(ctrler()->support_features(), ControlFeat::PRECISE_SWIPE);
    }

    // 起点、时长、缓动仍取自任务配置，只有终点由调用方按当前画面计算。
    bool swipe_by(std::string_view task_name, int distance, std::string* error) override
    {
        const auto task = Task.get(std::string(task_name));
        if (task == nullptr) {
            set_error(error, "swipe task is missing: " + std::string(task_name));
            return false;
        }
        const Rect& start = task->specific_rect;
        const Rect end { start.x + distance, start.y, start.width, start.height };
        const auto& params = task->special_params;
        const bool swiped = ctrler()->swipe(
            start,
            end,
            params.empty() ? 0 : params.at(0),
            params.size() < 2 ? SwipeExtraDirection::None : to_swipe_extra_direction(params.at(1)),
            params.size() < 3 ? 1 : params.at(2) / 10.0,
            params.size() < 4 ? 1 : params.at(3) / 10.0);
        if (!swiped) {
            set_error(error, "swipe failed: " + std::string(task_name));
            return false;
        }
        sleep(task->post_delay);
        return true;
    }

protected:
    bool _run() override
    {
        ProcessTask process(*this, m_tasks);
        const bool succeeded = process.run();
        m_last_task = process.get_last_task_name();
        if (const auto& hit = process.get_last_hit(); hit != nullptr && hit->image != nullptr) {
            m_last_image = hit->image;
        }
        return succeeded;
    }

private:
    std::vector<std::string> m_tasks;
    std::string m_last_task;
    std::shared_ptr<cv::Mat> m_last_image;
};

void bind_move_preview_identity_frame(
    MovePreview& preview,
    RoguelikeConfig* account,
    std::string frame_key,
    std::string frame_path,
    bool saved)
{
    preview.identity_frame_key = std::move(frame_key);
    preview.identity_frame_path = std::move(frame_path);
    preview.identity_frame_saved = saved;
    preview.identity_same_frame = !preview.identity_frame_key.empty();
    preview.identity_client = account ? account->account_client() : std::string {};
    preview.identity_uid = account ? account->account_uid() : std::string {};
    preview.identity_run_id = account ? account->status().run_id : std::string {};
}

BlackFlowTaskPort::BlackFlowTaskPort(
    const AsstCallback& callback,
    Assistant* inst,
    std::string_view task_chain,
    std::shared_ptr<IBlackFlowMapObservationSource> map_source,
    std::shared_ptr<RoguelikeConfig> account_config) :
    m_task_context(std::make_unique<ProcessTaskContext>(callback, inst, task_chain)),
    m_inventory_cleanup(std::make_unique<BlackFlowInventoryCleanup>()),
    m_map_source(std::move(map_source)),
    m_account_config(std::move(account_config))
{
}

BlackFlowTaskPort::~BlackFlowTaskPort() = default;

bool BlackFlowTaskPort::refresh(
    const BlackFlowObservationRequest& request,
    BlackFlowPerceptionSnapshot& snapshot,
    std::string* error)
{
    try {
        if (m_map_source == nullptr || m_task_context == nullptr) {
            set_error(error, "BlackFlow map observation source is not attached");
            return false;
        }
        if (!perception::floor_profile(request.floor).has_value()) {
            set_error(error, "NextLevel recognized an unsupported floor: " + std::to_string(request.floor));
            return false;
        }

        const auto capture_start = std::chrono::steady_clock::now();
        const cv::Mat image = m_task_context->capture();
        BlackFlowObservationRequest current_request = request;
        current_request.capture_us =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - capture_start)
                .count();

        BlackFlowPerceptionSnapshot next;
        std::string perception_error;
        if (!m_map_source
                 ->recognize(image, current_request, next.observation, next.observed_facts, &perception_error)) {
            snapshot = std::move(next);
            set_error(
                error,
                "map_model_failed_after_next_level_floor: floor " + std::to_string(request.floor) + ": " +
                    perception_error);
            LogWarn << __FUNCTION__ << "BlackFlow map model failed after NextLevel floor recognition"
                    << "floor" << request.floor << "error" << perception_error;
            return false;
        }
        if (const auto action_points = recognize_action_points(image); action_points.has_value()) {
            next.run.action_points = *action_points;
            next.observation.hud_action_points = *action_points;
        }
        if (const auto ingots = recognize_ingots(image); ingots.has_value()) {
            next.run.ingots = *ingots;
        }
        if (const auto capacity = recognize_movement_capacity(image); capacity.has_value()) {
            next.run.movement_slots_used = capacity->first;
            next.run.movement_capacity = capacity->second;
        }
        if (const auto hope = RoguelikeParameterAnalyzer::read_blackflow_hope(image); hope.has_value() && *hope >= 0) {
            next.run.hope = *hope;
        }
        if (const auto movement = recognize_loaded_movement(image); movement.has_value()) {
            next.run.active_movement = *movement;
        }
        snapshot = std::move(next);
        return true;
    }
    catch (const std::exception& exception) {
        set_error(error, "BlackFlow map refresh failed: " + std::string(exception.what()));
        return false;
    }
    catch (...) {
        set_error(error, "BlackFlow map refresh failed: unknown exception");
        return false;
    }
}

bool BlackFlowTaskPort::preview(
    const MoveCandidate& candidate,
    const ViewportObservation& viewport,
    MovePreview& preview,
    bool& panel_open,
    std::string* error)
{
    panel_open = false;
    if (m_task_context == nullptr) {
        set_error(error, "BlackFlow ProcessTask context is not attached");
        return false;
    }
    const auto click_rect =
        viewport.clickable_rect(candidate.target, viewport.map_revision(), viewport.viewport_revision());
    if (!click_rect.has_value()) {
        set_error(error, "target node has no current viewport rectangle");
        return false;
    }
    // 缩到图标框中心一半再点，边缘落空会让预览面板弹不出来。
    if (!m_task_context->click(shrink_to_center(*click_rect, 0.5))) {
        set_error(error, "target node click failed");
        return false;
    }
    panel_open = true;

    if (!m_task_context->execute({ std::string(MovePreviewWaitTask) }, error)) {
        return false;
    }
    const std::string& matched = m_task_context->last_task();
    if (matched.ends_with(MovePreviewCannotEnterTask)) {
        preview.reachability = PreviewReachability::Blocked;
        preview.exact_action_point_cost = candidate.predicted_action_point_cost;
        return true;
    }
    std::string_view cost_task;
    if (matched.ends_with(MovePreviewEnterWalkTask)) {
        cost_task = MovePreviewCostTask;
    }
    else if (matched.ends_with(MovePreviewEnterCraftedTask)) {
        cost_task = MovePreviewCostCraftedTask;
    }
    else {
        set_error(error, "move preview did not identify a reachable or blocked state");
        return false;
    }

    const std::shared_ptr<cv::Mat> matched_image = m_task_context->last_image();
    const cv::Mat image =
        matched_image != nullptr && !matched_image->empty() ? *matched_image : m_task_context->capture();
    const auto displayed_cost = recognize_integer(image, cost_task);
    if (!displayed_cost.has_value() || *displayed_cost > 0 || *displayed_cost < -9) {
        set_error(error, "move preview action point cost OCR failed");
        return false;
    }
    // 节点预览中，“作战－未知的凶戾”只识别“作战”。
    // 地图中已有的隐藏作战身份由 Session 保留，不依赖预览识别具体隐藏标题。
    if (!recognize_move_preview_identity(image, preview, error)) {
        return false;
    }
    preview.reachability = PreviewReachability::Reachable;
    preview.exact_action_point_cost = -*displayed_cost;
    const auto displayed_type = std::optional<NodeType>(preview.displayed_type);
    // Persist the exact panel frame, not the map-source's previous map frame.
    const auto preview_dir = UserDir.get() / utils::path("debug/blackflow/preview");
    std::error_code ec;
    std::filesystem::create_directories(preview_dir, ec);
    const auto frame_id = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto frame_path = preview_dir / utils::path("preview-" + frame_id + ".png");
    const bool frame_saved = !ec && MAA_NS::imwrite(frame_path, image);
    bind_move_preview_identity_frame(
        preview,
        m_account_config.get(),
        frame_id,
        utils::path_to_utf8_string(frame_path),
        frame_saved);
    const Rect clicked = shrink_to_center(*click_rect, 0.5);
    json::object facts {
        { "action_id", candidate.action_id },
        { "source", candidate.source },
        { "target", candidate.target },
        { "map_revision", viewport.map_revision() },
        { "viewport_revision", viewport.viewport_revision() },
        { "clicked_rect", json::array { clicked.x, clicked.y, clicked.width, clicked.height } },
        { "effective_entry_type", std::string(to_string(preview.displayed_type)) },
        { "category", preview.displayed_name },
        { "stage_title", preview.stage_title },
        { "occupation_confirmed", preview.occupation.has_value() },
        { "base_map_type", preview.occupation ? std::string(to_string(preview.occupation->base_type)) : "unknown" },
        { "cleared_type", preview.occupation ? std::string(to_string(preview.occupation->cleared_type)) : "unknown" },
        { "exact_cost", preview.exact_action_point_cost },
        { "same_frame_kind_title_body", true },
        { "identity_client", preview.identity_client },
        { "identity_uid", preview.identity_uid },
        { "identity_run_id", preview.identity_run_id },
        { "identity_frame_key", preview.identity_frame_key },
        { "frame_saved", frame_saved },
        { "frame_path", utils::path_to_utf8_string(frame_path) }
    };
    std::ofstream(preview_dir / utils::path("preview-" + frame_id + ".json")) << json::value(facts);
    LogInfo << __FUNCTION__ << "BlackFlow move preview identity evidence" << json::value(facts).to_string();
    // 理想源只在 Buff 范围内的普通作战节点有规划意义；险路恶敌明确跳过，
    // 避免把其详情页的相似文案误记成可收集目标。
    if (*displayed_type == NodeType::BattleNormal) {
        const auto ideal_source = recognize_text(image, MovePreviewIdealSourceTask, { "理想源", "希望的沃土" });
        if (ideal_source.has_value()) {
            preview.ideal_source_known = true;
            preview.ideal_source = *ideal_source == "理想源" ? *ideal_source : std::string();
            LogInfo << __FUNCTION__ << "BlackFlow battle preview ideal source"
                    << (preview.ideal_source.empty() ? std::string_view("none")
                                                     : std::string_view(preview.ideal_source));
        }
        else {
            // 逐个普通战斗节点探查；没有命中时保持 unknown，不能把 OCR 未命中
            // 当成“没有理想源”，否则会在下一次重规划时丢掉尚未探查完的节点。
            LogInfo << __FUNCTION__ << "BlackFlow battle preview ideal source" << "unknown";
        }
    }
    else if (*displayed_type == NodeType::BattleSavage) {
        // 理想源不会出现在险路恶敌。即使详情页出现相似文字，也不能写入节点事实。
        LogInfo << __FUNCTION__ << "BlackFlow savage preview ideal source" << "skipped";
    }
    return true;
}

MoveConfirmationStatus BlackFlowTaskPort::confirm(
    const MoveTransaction& transaction,
    EnteredPageObservation& entered_page,
    std::string* error)
{
    if (m_task_context == nullptr || transaction.stage() != MoveTransactionStage::Previewed ||
        !transaction.preview().has_value() || transaction.preview()->reachability != PreviewReachability::Reachable) {
        set_error(error, "move confirmation requires a reachable previewed transaction");
        return MoveConfirmationStatus::Failed;
    }
    if (!m_task_context->execute({ std::string(MovePreviewConfirmTask) }, error)) {
        return MoveConfirmationStatus::Failed;
    }
    const std::string confirmation_task = m_task_context->last_task();
    const MoveConfirmationTaskDisposition disposition = resolve_move_confirmation_task(
        confirmation_task,
        [this](std::string* cleanup_error) {
            if (m_inventory_cleanup == nullptr) {
                set_error(cleanup_error, "inventory cleanup component is not attached");
                return false;
            }
            return m_inventory_cleanup->run(*m_task_context, cleanup_error);
        },
        error);
    if (disposition == MoveConfirmationTaskDisposition::InventoryCleaned) {
        return MoveConfirmationStatus::InventoryCleaned;
    }
    if (disposition == MoveConfirmationTaskDisposition::NeedsDismiss) {
        return MoveConfirmationStatus::NeedsDismiss;
    }
    if (disposition == MoveConfirmationTaskDisposition::Failed) {
        return MoveConfirmationStatus::Failed;
    }
    // 成功末端单独执行，继续保留默认任务间隔和既有的 600 毫秒页面稳定等待。
    if (!m_task_context->execute({ std::string(MovePreviewConfirmCompletedTask) }, error)) {
        return MoveConfirmationStatus::Failed;
    }

    entered_page = {};
    if (transaction.preview()->identity_revealed || transaction.preview()->displayed_type == NodeType::HideBattle) {
        // 已揭示节点不需要 OCR 页面标题，但仍必须把预览得到的节点类型
        // 传给会话，否则 page_context 为空，路由会误以为已回到地图并
        // 在商店/战斗页面上触发地图识别。
        entered_page.classified_type = transaction.preview()->displayed_type;
        if (!transaction.preview()->displayed_name.empty()) {
            entered_page.matched_texts.push_back(transaction.preview()->displayed_name);
        }
        return MoveConfirmationStatus::Succeeded;
    }
    return classify_entered_page(m_task_context->capture(), entered_page, error) ? MoveConfirmationStatus::Succeeded
                                                                                 : MoveConfirmationStatus::Failed;
}

void BlackFlowTaskPort::configure_diagnostics(const DiagnosticSettings& settings)
{
    if (m_map_source != nullptr) {
        m_map_source->configure_diagnostics(settings);
    }
}

bool BlackFlowTaskPort::persist_diagnostics(const DiagnosticArtifactRequest& request, std::string* error)
{
    if (m_map_source == nullptr) {
        set_error(error, "BlackFlow diagnostic persistence has no map observation source");
        return false;
    }
    return m_map_source->persist_diagnostics(request, error);
}

std::optional<int> BlackFlowTaskPort::recognize_action_points(const cv::Mat& image) const
{
    const auto value = recognize_integer(image, CurrentActionPointsTask);
    if (!value.has_value() || *value < 0 || *value > 64) {
        return std::nullopt;
    }
    return value;
}

std::optional<int> BlackFlowTaskPort::recognize_ingots(const cv::Mat& image) const
{
    const auto value = recognize_integer(image, CurrentIngotsTask);
    if (!value.has_value() || *value < 0) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::pair<int, int>> BlackFlowTaskPort::recognize_movement_capacity(const cv::Mat& image) const
{
    const auto task = Task.get<OcrTaskInfo>(CurrentMovementCapacityTask);
    if (task == nullptr) {
        return std::nullopt;
    }
    OCRer analyzer(image);
    analyzer.set_task_info(task);
    const auto results = analyzer.analyze();
    if (!results.has_value()) {
        return std::nullopt;
    }
    for (const auto& result : *results) {
        if (const auto capacity = recognize_movement_capacity_from_text(result.text); capacity.has_value()) {
            return capacity;
        }
    }
    return std::nullopt;
}

bool BlackFlowTaskPort::classify_entered_page(
    const cv::Mat& image,
    EnteredPageObservation& observation,
    std::string* error) const
{
    const auto task = Task.get<OcrTaskInfo>(EnteredPageClassificationTask);
    if (task == nullptr) {
        set_error(error, "entered-page OCR task is missing");
        return false;
    }

    OCRer analyzer(image);
    analyzer.set_task_info(task);
    const auto results = analyzer.analyze();
    std::vector<std::string> matched_texts;
    if (results.has_value()) {
        matched_texts.reserve(results->size());
        for (const auto& result : *results) {
            matched_texts.emplace_back(result.text);
        }
    }
    observation = classify_entered_page_texts(std::move(matched_texts));
    if (observation.classified_type.has_value() || observation.classification_conflict) {
        return true;
    }

    if (!m_task_context->execute({ std::string(EnteredPageClassificationEncounterPrepareTask) }, error)) {
        return false;
    }
    const auto encounter_title = recognize_text(m_task_context->capture(), StageEncounterOcrTask);
    if (encounter_title.has_value()) {
        observation.matched_texts.emplace_back(*encounter_title);
        observation = classify_entered_page_texts(std::move(observation.matched_texts));
        if (!observation.classified_type.has_value() && !observation.classification_conflict) {
            observation.classified_type = NodeType::Incident;
        }
    }
    return true;
}
} // namespace asst::blackflow
