#include "RoguelikeRecruitTaskPlugin.h"
#include "BlackFlow/BlackFlowRecruitmentTerminalDuty.h"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/BlackFlow/BlackFlowAccountProfile.h"
#include "RoguelikeOperEvidence.h"
#include "Vision/Roguelike/RoguelikeParameterAnalyzer.h"

#include <cctype>
#include <utility>

#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "MaaUtils/Time.hpp"
#include "Task/ProcessTask.h"
#include "Task/Roguelike/BlackFlow/BlackFlowModifierEvidenceProducer.h"
#include "Task/Roguelike/BlackFlow/BlackFlowRecruitmentDuty.h"
#include "Task/Roguelike/BlackFlow/BlackFlowRecruitmentOutputEvidence.h"
#include "Task/Roguelike/BlackFlow/BlackFlowRecruitmentPageObservation.h"
#include "Utils/Logger.hpp"
#include "Utils/WorkingDir.hpp"
#include "Vision/Matcher.h"
#include "Vision/OCRer.h"
#include "Vision/RegionOCRer.h"
#include "Vision/Roguelike/RoguelikeRecruitImageAnalyzer.h"
#include "Vision/Roguelike/RoguelikeRecruitSupportAnalyzer.h"

using namespace asst::battle::roguelike;

bool asst::RoguelikeRecruitTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    if (details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }

    if (!RoguelikeConfig::is_valid_theme(m_config->get_theme())) {
        Log.error("Roguelike name doesn't exist!");
        return false;
    }
    const std::string roguelike_name = m_config->get_theme() + "@";
    const std::string& task = details.get("details", "task", "");
    if ((task.ends_with("Roguelike@VoucherStashResume") || task == "VoucherStashResume") &&
        !m_stash_resume_source.empty()) {
        if (auto process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
            process->remove_override_next(m_stash_resume_source);
        }
        m_stash_resume_source.clear();
        return false;
    }
    const auto is_task = [&](std::string_view name) {
        return task == name || task.ends_with("@" + std::string(name));
    };
    const bool cancelled = task.find("GiveUp") != std::string::npos ||
                           task.find("ReserveRecruitmentVoucher") != std::string::npos ||
                           task.find("VoucherStashConfirm") != std::string::npos || is_task("StashedRecruitLeave");
    if (m_pending_recruitment && (msg == AsstMsg::SubTaskError || (msg == AsstMsg::SubTaskStart && cancelled))) {
        m_recruitment_event = RecruitmentEvent::Discard;
        return true;
    }
    if (msg != AsstMsg::SubTaskCompleted) {
        return false;
    }
    if (is_task("StartExplore")) {
        m_initail_recruit = true;
    }
    if (is_task("EnterAfterRecruit")) {
        m_initail_recruit = false;
    }
    if (m_pending_recruitment) {
        const auto algorithm = details.get("details", "algorithm", std::string());
        const auto action = details.get("details", "action", std::string());
        const bool recognized = !algorithm.empty() && algorithm != "JustReturn";
        if (recognized && (action == "ClickSelf" || action == "ClickRect") &&
            (is_task("ChooseOperConfirm") || is_task("RecruitSupportConfirm"))) {
            m_recruitment_event = RecruitmentEvent::Confirm;
            return true;
        }
        // These are positively recognized successors, not a blind click or disappearance of the confirm button.
        if (recognized && (is_task("RecruitSkip") || is_task("RecruitOther") || is_task("EnterAfterRecruit") ||
                           is_task("StashedRecruitReceipt") || is_task("StashedRecruitOptionsReceipt") ||
                           is_task("StashedRecruitPortraitAdvance") ||
                           (m_config->get_theme() == RoguelikeTheme::BlackFlow &&
                            (is_task("DropsFlag") || is_task("RecruitShopReturn"))))) {
            m_recruitment_event = RecruitmentEvent::Receipt;
            return true;
        }
    }
    std::string_view task_view = task;
    if (task_view.starts_with(roguelike_name)) {
        task_view.remove_prefix(roguelike_name.length());
    }
    if (task_view.ends_with("Roguelike@ChooseOper") || task_view == "ChooseOper") {
        m_recruitment_event = RecruitmentEvent::Select;
        return true;
    }
    else {
        return false;
    }
}

bool asst::RoguelikeRecruitTaskPlugin::load_params(const json::value& params)
{
    m_blackflow_strategy = params.get("blackflow_strategy", std::string());
    return true;
}

std::unordered_set<std::string> asst::RoguelikeRecruitTaskPlugin::calculate_condition_oper(
    const RecruitPriorityOffset& condition,
    const std::unordered_map<std::string, RoguelikeOper>& chars_map)
{
    std::unordered_set<std::string> opers; // 符合这个策略组的干员
    for (const auto& [oper_name, oper_level] : chars_map) {
        if (condition.opers.contains(oper_name)) {
            opers.insert(oper_name);
        }
    }
    return opers;
}

bool asst::RoguelikeRecruitTaskPlugin::_run()
{
    LogTraceFunction;
    m_origin_ledger.bind(m_config->account_client(), m_config->account_uid(), m_config->status().run_id);

    const auto event = std::exchange(m_recruitment_event, RecruitmentEvent::Select);
    if (event == RecruitmentEvent::Discard) {
        discard_pending_recruitment("cancelled_or_failed");
        return true;
    }
    if (event == RecruitmentEvent::Confirm) {
        if (m_pending_recruitment) {
            m_pending_recruitment->confirmed = true;
            const auto* process = dynamic_cast<ProcessTask*>(m_task_ptr);
            if (process && process->get_last_task_name().starts_with("StashedRecruit@")) {
                const auto& name = m_pending_recruitment->oper.name;
                std::vector<std::string> names { name };
                if (const auto properties = BattleData.find_first_oper(battle::Role::Unknown, name, true);
                    properties && !properties->name_en.empty()) {
                    std::string english = properties->name_en;
                    std::ranges::transform(english, english.begin(), [](unsigned char c) {
                        return static_cast<char>(std::toupper(c));
                    });
                    names.emplace_back(std::move(english));
                }
                for (const auto task_name : { "BlackFlow@Roguelike@StashedRecruitPortraitObserve",
                                              "BlackFlow@Roguelike@StashedRecruitPortraitAdvance" }) {
                    const auto task = Task.get<OcrTaskInfo>(task_name);
                    if (!task) {
                        m_task_ptr->set_enable(false);
                        return false;
                    }
                    task->text = names;
                }
            }
        }
        return true;
    }
    if (event == RecruitmentEvent::Receipt) {
        return commit_pending_recruitment();
    }
    discard_pending_recruitment("new_recruitment_page");

    if (m_voucher_stash.pending()) {
        // A retry may verify the previous click, but must never click a second voucher.
        return verify_voucher_stash();
    }

    ++m_recruit_count;

    auto& theme = m_config->get_theme();
    auto mode = m_config->get_mode();
    auto& squad = m_config->get_squad();
    auto difficulty = m_config->get_difficulty();

    if (theme == RoguelikeTheme::Sarkaz && mode == RoguelikeMode::Investment) {
        if (squad == "点刺成锭分队") {
            ProcessTask(*this, { "Sarkaz@RoguelikeRecruit-GiveUp" }).run();
            return true;
        }
        if (squad == "蓝图测绘分队" && m_recruit_count > 1) {
            lazy_recruit();
            return true;
        }
    }

    // 精二机械师保证了结构性原理，整局都不必打架，多招干员只会拖慢开局。刷等级指定了结局策略就不飞，照常招募；
    // 刷开局只看准备局，目标局按自己的开局配置招募。
    if (theme == RoguelikeTheme::BlackFlow && m_initail_recruit && m_recruit_count > 1 &&
        ((mode == RoguelikeMode::Exp && m_blackflow_strategy.empty()) || mode == RoguelikeMode::Investment ||
         (mode == RoguelikeMode::BlackFlowStartRun && m_config->get_run_for_collectible())) &&
        m_config->get_core_char() == "机械师" && (squad == "特勤分队" || squad == "堡垒战术分队") &&
        (m_config->get_start_roles() == "稳扎稳打" || m_config->get_start_roles() == "坚不可摧")) {
        ProcessTask(*this, { "BlackFlow@RoguelikeRecruit-GiveUp" }).run();
        return true;
    }

    // 时光之末的特殊用法
    if (theme == RoguelikeTheme::JieGarden && squad == "指挥分队" && difficulty >= 3) {
        if (mode == RoguelikeMode::Investment ||
            (mode == RoguelikeMode::Collectible && m_config->get_run_for_collectible())) {
            if (m_config->get_skip_recruit_in_fast_pass()) {
                ProcessTask(*this, { "JieGarden@RoguelikeRecruit-GiveUp" }).run();
                return true;
            }
        }
    }

    // 刷常乐节点保存路上招募券
    // 只在非开局招募时保存招募券
    if (theme == RoguelikeTheme::JieGarden && mode == RoguelikeMode::FindPlaytime && !m_initail_recruit) {
        bool ret = ProcessTask(*this, { "JieGarden@Roguelike@ReserveRecruitmentVoucher" }).run();
        if (ret) {
            // 判断当前界面是否存在尚未消失的确认招募按钮，等待其消失
            Matcher analyzer;
            analyzer.set_task_info("JieGarden@Roguelike@ChooseOperConfirm");
            for (int i = 0; i < 5; ++i) {
                analyzer.set_image(ctrler()->get_image());
                if (analyzer.analyze().has_value()) {
                    Log.info(__FUNCTION__, "| Waiting for confirm button to disappear...");
                    sleep(200);
                }
                else {
                    return true;
                }
            }
            // 没有消失的话就继续招募流程
            Log.warn(__FUNCTION__, "| Confirm button did not disappear in time, continue recruitment process.");
        }
    }

    if (m_initail_recruit) {
        // 指定项保留到职业匹配的招募券，不能在第一张不兼容券上消耗。
        const RoguelikeStartOper* start_oper = m_config->get_current_start_oper();
        if (start_oper != nullptr) {
            const auto requested_role = BattleData.get_first_role(start_oper->name);
            RoguelikeRecruitImageAnalyzer voucher_analyzer(ctrler()->get_image());
            const bool compatible = requested_role != battle::Role::Unknown && voucher_analyzer.analyze() &&
                                    std::ranges::any_of(voucher_analyzer.get_result(), [&](const auto& candidate) {
                                        return BattleData.get_first_role(candidate.name) == requested_role;
                                    });
            if (compatible) {
                const int max_refresh = Task.get("RoguelikeRefreshSupportBtnOcr")->special_params.front();
                const bool recruited = start_oper->use_support ? recruit_support_char(start_oper->name, max_refresh)
                                                               : recruit_own_char(start_oper->name);
                if (m_task_ptr != nullptr && !m_task_ptr->get_enable()) {
                    // The owner was deliberately interrupted; do not fall through
                    // into generic recruitment or a confirmation task.
                    return true;
                }
                if (recruited) {
                    if (m_pending_recruitment) {
                        m_pending_recruitment->advance_start = true;
                    }
                    return true;
                }
            }
            else {
                // 指定核心尚未兑现，前置职业券优先使用低星补位，保留招募核心的希望。
                LogInfo << __FUNCTION__ << "Defer requested opening operator until a compatible voucher"
                        << start_oper->name;
            }
        }
    }

    bool team_full_without_rookie = m_config->status().team_full_without_rookie;
    // Log.info("team_full_without_rookie", team_full_without_rookie);

    // 编队信息 (已有角色)
    const auto& chars_map = m_config->status().opers;

    if (!m_starts_complete) {
        for (const auto& oper : chars_map) {
            auto& recruit_info = RoguelikeRecruit.get_oper_info(m_config->get_theme(), oper.first);
            if (recruit_info.is_start) {
                m_starts_complete = true;
            }
        }
    }

    if (!m_team_complete) {
        bool complete = true;
        int complete_count = 0;
        const auto& team_complete_condition = RoguelikeRecruit.get_team_complete_info(m_config->get_theme());
        for (const auto& condition : team_complete_condition) { // 每个完备度的策略组
            std::unordered_set<std::string> opers =
                calculate_condition_oper(condition, chars_map); // 符合这个策略组的干员

            complete_count += int(opers.size());
            if (int(opers.size()) < condition.threshold) {
                complete = false;
            }

            Log.trace("__FUNCTION__", "groups:", condition.groups);
            Log.trace("__FUNCTION__", "opers:", opers);
        }
        m_team_complete = complete;
        if (complete_count <= RoguelikeRecruit.get_team_complete_require(m_config->get_theme()) / 2 &&
            m_recruit_count >= 10) {
            // 如果第10次招募还没拿到半队key干员，说明账号阵容不齐，放开招募限制，有啥用啥吧
            m_team_complete = true;
        }
        Log.trace("__FUNCTION__", "complete_count:", complete_count, "m_team_complete:", m_team_complete);
    }

    if (m_recruit_count >= 3 && !m_starts_complete) {
        // 如果第3次招募还没拿到start干员，说明账号练度低且阵容不齐，放开招募限制，有啥用啥吧
        m_starts_complete = true;
        m_team_complete = true;
    }

    // 候选干员
    std::vector<RoguelikeRecruitInfo> recruit_list;

    // 干员名字的识别位置
    std::unordered_map<std::string, Rect> last_oper_rects;

    int SwipeTimes = Task.get("RoguelikeRecruitSwipeMaxTime")->max_times;
    int i = 0;
    std::unordered_set<std::string> pre_oper_names;
    const bool raw_paging = theme == RoguelikeTheme::BlackFlow;
    std::optional<RoguelikeRecruitPageGeometry> previous_geometry;
    int analyzed_pages = 0;
    int last_observed_step = -1;
    int forward_swipe_calls = 0;
    std::string paging_stop_reason = SwipeTimes > 0 ? "observation_limit" : "invalid_observation_budget";
    const auto report_scan = [&] {
        if (!raw_paging) {
            return;
        }
        auto scan = basic_info_with_what("BlackFlowRecruitmentScanCompleted");
        scan["details"] = json::object {
            { "client", m_config->account_client() },
            { "uid", m_config->account_uid() },
            { "run_id", m_config->status().run_id },
            { "recruit_index", m_recruit_count },
            { "max_page_observations", SwipeTimes },
            { "analyzed_pages", analyzed_pages },
            { "last_observed_step", last_observed_step >= 0 ? json::value(last_observed_step) : json::value(nullptr) },
            { "navigation_step", forward_swipe_calls },
            { "forward_swipe_calls", forward_swipe_calls },
            { "stop_reason", paging_stop_reason },
            { "physical_end_verified", nullptr },
            { "all_pages_complete", nullptr },
            { "observed_set_scope", "bounded_raw_geometry_steps_not_distinct_physical_pages" },
            { "selection_not_yet_clicked", true },
        };
        callback(AsstMsg::SubTaskExtraInfo, scan);
    };

    // 开始识别前先往前翻两页，是方舟的bug，有可能进招募界面时不是从最左边开始
    swipe_to_the_left_of_operlist();

    auto image = ctrler()->get_image();

    const int cur_hope = read_current_hope(image);

    blackflow::BlackFlowModifierEvidenceProducer modifier_catalog;
    if (theme == RoguelikeTheme::BlackFlow && mode == RoguelikeMode::Exp) {
        modifier_catalog.load(ResDir.get() / utils::path("roguelike/BlackFlow/modifier_components.json"));
    }
    blackflow::BlackFlowRecruitmentDutyCatalog duty_catalog;
    std::vector<blackflow::RecruitmentDutyOffer> duty_offers;
    if (theme == RoguelikeTheme::BlackFlow && mode == RoguelikeMode::Exp) {
        duty_catalog.load(
            ResDir.get() / "roguelike/BlackFlow/recruitment_duties.json",
            ResDir.get() / "roguelike/BlackFlow/operator_training.json",
            ResDir.get() / "battle_data.json");
    }

    // 翻页找出所有候选干员
    for (; i != SwipeTimes; ++i) {
        if (need_exit()) {
            paging_stop_reason = "interrupted";
            report_scan();
            return false;
        }
        image = ctrler()->get_image();
        RoguelikeRecruitImageAnalyzer analyzer(image);
        const auto frame_read_completed_at = theme == RoguelikeTheme::BlackFlow ? MAA_NS::format_now() : std::string {};
        const bool analyzed = analyzer.analyze();
        if (theme == RoguelikeTheme::BlackFlow) {
            auto observation = basic_info_with_what("BlackFlowRecruitmentPageObserved");
            const blackflow::RecruitmentPageObservationContext context {
                .client = m_config->account_client(),
                .uid = m_config->account_uid(),
                .run_id = m_config->status().run_id,
                .frame_read_completed_at_local = frame_read_completed_at,
                .evidence_directory = UserDir.empty() ? std::filesystem::path {}
                                                      : UserDir.get() / utils::path("debug/blackflow/recruitment"),
                .recruit_index = m_recruit_count,
                .page_index = i,
                .analysis_succeeded = analyzed,
                .legacy_filter_hope = cur_hope >= 0 ? std::optional<int>(cur_hope) : std::nullopt,
                .initial_recruitment = m_initail_recruit,
            };
            observation["details"] = blackflow::make_recruitment_page_observation(image, analyzer, context);
            callback(AsstMsg::SubTaskExtraInfo, observation);
        }
        if (!analyzed) {
            Log.trace(__FUNCTION__, "| Page", i, "recruit list analyse failed");
            // 还没滑动就识别失败，通常是招募界面为空，视为招募成功并退出
            paging_stop_reason = "analysis_failed";
            if (i == 0) {
                report_scan();
                return true;
            }
            // 已经滑动过，识别失败可能是干员不可选
            break;
        }

        ++analyzed_pages;
        last_observed_step = i;
        auto current_geometry = raw_paging ? std::make_optional(analyzer.paging_geometry()) : std::nullopt;

        if (theme == RoguelikeTheme::BlackFlow && mode == RoguelikeMode::Exp) {
            std::vector<std::string> attribute_names;
            for (const auto& oper : analyzer.get_result()) {
                attribute_names.push_back(oper.name);
            }
            for (const auto& [name, oper] : chars_map) {
                attribute_names.push_back(name);
            }
            // The UID-bound profile is immutable during this exploration. The
            // final refind calls the same adapter and matches projected facts.
            const auto modifier_parts = modifier_catalog.observe(
                blackflow::ModifierEvidenceRequest {
                    .profile = m_config->account_profile().get(),
                    .client = m_config->account_client(),
                    .uid = m_config->account_uid(),
                    .run_id = m_config->status().run_id,
                    .observed_at = frame_read_completed_at,
                    .operator_names = attribute_names,
                    .configured_squad = m_config->get_squad(),
                    .observed_purchase_names = m_config->status().collections,
                    .life = m_config->account_profile() ? m_config->account_profile()->life_modifier_evidence()
                                                        : std::nullopt,
                });
            const auto output_evidence = blackflow::recruitment_profile_output_evidence(
                m_config->account_profile().get(),
                m_config->account_client(),
                m_config->account_uid(),
                m_config->status().run_id,
                attribute_names,
                modifier_parts.comparison_receipt);
            auto attribute_observation = basic_info_with_what("BlackFlowRecruitmentAttributeEvidence");
            attribute_observation["details"] = blackflow::recruitment_profile_output_to_json(output_evidence);
            attribute_observation["details"]["stage"] = "initial_page";
            attribute_observation["details"]["modifier_parts"] =
                blackflow::modifier_evidence_parts_to_json(modifier_parts);
            callback(AsstMsg::SubTaskExtraInfo, attribute_observation);
            const blackflow::RecruitmentDutyContext context {
                .client = m_config->account_client(),
                .uid = m_config->account_uid(),
                .run_id = m_config->status().run_id,
                .observed_at = frame_read_completed_at,
                .page_index = i,
                .hope = cur_hope,
                .mode = static_cast<int>(mode),
                .initial = m_initail_recruit,
                .output_evidence = &output_evidence,
            };
            auto page_offers = blackflow::observe_recruitment_duties(
                analyzer,
                duty_catalog,
                m_config->account_profile() ? &*m_config->account_profile() : nullptr,
                chars_map,
                context,
                &m_origin_ledger);
            duty_offers.insert(
                duty_offers.end(),
                std::make_move_iterator(page_offers.begin()),
                std::make_move_iterator(page_offers.end()));
        }

        std::unordered_set<std::string> oper_names;
        const auto& oper_list = analyzer.get_result();
        bool stop_swipe = false;
        const auto& owned_collection = m_config->status().collections;

        int max_oper_x = 0;
        for (const auto& oper_info : oper_list) {
            oper_names.emplace(oper_info.name);

            max_oper_x = std::max(max_oper_x, oper_info.rect.x);
            // 查询上次识别位置
            const auto& rect_it = last_oper_rects.find(oper_info.name);
            if (rect_it == last_oper_rects.cend()) {
                last_oper_rects.emplace(oper_info.name, oper_info.rect);
            }
            else {
                const auto x_offset = abs(rect_it->second.x - oper_info.rect.x);
                const auto y_offset = abs(rect_it->second.y - oper_info.rect.y);

                // 偏移低于阈值，代表划动没有效果，已经到达屏幕最右侧
                if (!raw_paging && x_offset < 20 && y_offset < 5) {
                    stop_swipe = true;
                    Log.trace(
                        __FUNCTION__,
                        "| Page",
                        i,
                        "oper",
                        oper_info.name,
                        "last rect:",
                        rect_it->second.to_string(),
                        "current rect:",
                        oper_info.rect.to_string(),
                        " - stop swiping");
                    break;
                }

                rect_it->second = oper_info.rect;
            }

            if (!can_afford(oper_info, cur_hope)) {
                continue;
            }
            // 查询招募配置
            if (RoguelikeRecruit.is_oper_excluded(theme, oper_info.name)) {
                LogInfo << "Excluded roguelike recruitment candidate" << theme << oper_info.name;
                continue;
            }
            const auto& recruit_info = RoguelikeRecruit.get_oper_info(m_config->get_theme(), oper_info.name);
            int priority = 0;

            // 查询编队是否已持有该干员
            if (const auto& char_opt = chars_map.find(oper_info.name); char_opt != chars_map.end()) {
                // 干员已在编队中，又出现在招募列表，只有待晋升和预备干员两种情况
                if (recruit_info.is_alternate) {
                    // 预备干员可以重复招募，但是最好不要重复招募预备干员占用编队位置
                    priority = recruit_info.recruit_priority_when_team_full;
                }
                else {
                    // 干员待晋升
                    priority = team_full_without_rookie ? recruit_info.promote_priority_when_team_full
                                                        : recruit_info.promote_priority;
                    if (oper_info.elite < 2) { // 若未精二，则晋升无意义
                        priority = 0;
                    }
                }
            }
            else {
                // 干员待招募，检查练度
                if (oper_info.elite == 2) {
                    // 先判断新成员是否适合当前队伍，精二收益在条件修正后计入。
                    priority = team_full_without_rookie ? recruit_info.recruit_priority_when_team_full
                                                        : recruit_info.recruit_priority;
                }
                else if (is_recruitment_strength_sufficient(oper_info.elite, oper_info.level)) {
                    // 精一55级以上
                    // 等级是 0 一般是识别错了，多发生于外服，一般都是一错全错，先凑合着招一个吧，比不招人强 orz
                    priority = team_full_without_rookie ? recruit_info.recruit_priority_when_team_full
                                                        : recruit_info.recruit_priority;
                }
                else {
                    // 精一55级以下，默认不招募
                    Log.trace(
                        __FUNCTION__,
                        "| Ignored low level oper:",
                        oper_info.name,
                        oper_info.elite,
                        oper_info.level);

                    // REFACTOR ME: 不招募情况没有对 oper_list 进行处理
                    // 若遇到 offset ，最终 priority 可能为正，会导致练度不够也招募 @Daydreamer114 @Saratoga-Official
                    // 水月高难且招募机会少：低练度干员也强于空手，改为轻微惩罚而非直接放弃
                    priority -= (theme == RoguelikeTheme::Mizuki) ? 300 : 114'514;
                    // 水月：即使被扣成非正数，也保底可选中，避免"没人可招"直接把队伍停在 4 人
                    if (theme == RoguelikeTheme::Mizuki && priority <= 0) {
                        priority = 1;
                    }
                }

                // 只在初始招募页面使用开局分，且沿用普通招募的练度门槛。
                // 局内的前几次招募、低练度候选均不能被开局分覆盖。
                const bool opening_eligible = is_recruitment_strength_sufficient(oper_info.elite, oper_info.level);
                if (m_initail_recruit && opening_eligible && recruit_info.start_priority > 0) {
                    Log.trace(
                        __FUNCTION__,
                        "| start phase: use start_priority",
                        recruit_info.start_priority,
                        "for",
                        oper_info.name,
                        "(was",
                        priority,
                        ")");
                    priority = recruit_info.start_priority;
                }

                if (!recruit_info.is_alternate) {
                    for (const auto& priority_offset : recruit_info.recruit_priority_offsets) {
                        std::unordered_set<std::string> opers = // 符合这个策略组的干员
                            calculate_condition_oper(priority_offset, chars_map);
                        if (priority_offset.is_less) {
                            if (int(opers.size()) <= priority_offset.threshold) {
                                priority += priority_offset.offset;
                            }
                        }
                        else {
                            if (int(opers.size()) >= priority_offset.threshold) {
                                priority += priority_offset.offset;
                            }
                        }
                    }

                    // 黑流树海第三层首领的持续伤害会迅速耗尽没有治疗的队伍。
                    // 当队伍尚未拥有医疗干员时，提高所有医疗候选的优先级，
                    // 让随机招募优先补齐最基本的续航能力。
                    if (theme == RoguelikeTheme::BlackFlow &&
                        BattleDataConfig::get_instance().get_first_role(oper_info.name) == battle::Role::Medic) {
                        const bool has_medic = std::ranges::any_of(chars_map, [](const auto& entry) {
                            return BattleDataConfig::get_instance().get_first_role(entry.first) == battle::Role::Medic;
                        });
                        if (!has_medic) {
                            priority += 450;
                            Log.trace(__FUNCTION__, "| BlackFlow no medic yet, boost", oper_info.name, priority);
                        }
                    }

                    // 同理，队伍里一个地面阻挡都没有时（坦克/近卫/先锋都算），
                    // 第一个阻挡阵亡就会变成「全员放高台、门口的怪直接走进去」，
                    // 因此优先补齐能站在道路上的干员。
                    if (theme == RoguelikeTheme::BlackFlow) {
                        const auto is_blocker_role = [](battle::Role role) {
                            return role == battle::Role::Tank || role == battle::Role::Warrior ||
                                   role == battle::Role::Pioneer;
                        };
                        if (is_blocker_role(BattleDataConfig::get_instance().get_first_role(oper_info.name))) {
                            const bool has_blocker = std::ranges::any_of(chars_map, [&](const auto& entry) {
                                return is_blocker_role(BattleDataConfig::get_instance().get_first_role(entry.first));
                            });
                            if (!has_blocker) {
                                priority += 450;
                                Log.trace(__FUNCTION__, "| BlackFlow no blocker yet, boost", oper_info.name, priority);
                            }
                        }
                    }

                    for (const auto& priority_offset : recruit_info.collection_priority_offsets) {
                        auto iter =
                            std::find(owned_collection.begin(), owned_collection.end(), priority_offset.collection);
                        if (iter != owned_collection.end()) {
                            priority += priority_offset.offset;
                        }
                    }
                    // 初始三张免费券由 start_priority 决定开局结构；不能再用 is_start/is_key
                    // 把同一批候选全部扣成负分，否则会出现第一张招到人、第二张整券跳过。
                    if (!m_initail_recruit && !m_starts_complete) {
                        if (!recruit_info.is_start) {
                            priority -= 1000;
                        }
                    }
                    else if (!m_initail_recruit && !m_team_complete) {
                        if (!recruit_info.is_key) {
                            priority -= 1000;
                        }
                    }
                }

                // 晋升价值不能抵消当前队伍的招募否决；开局沿用独立的开局分。
                if (oper_info.elite == 2 && !(m_initail_recruit && recruit_info.start_priority > 0)) {
                    const int promotion_value = team_full_without_rookie ? recruit_info.promote_priority_when_team_full
                                                                         : recruit_info.promote_priority;
                    priority = score_elite_two_recruit(priority, promotion_value);
                }
            }

            if (theme == RoguelikeTheme::BlackFlow && recruit_info.account_module && !recruit_info.is_alternate) {
                const auto& policy = *recruit_info.account_module;
                const auto& account = m_config->account_profile();
                const auto evidence =
                    account ? account->module_evidence(oper_info.name, policy.module_id) : std::nullopt;
                const bool ready =
                    account && account->module_available(oper_info.name, policy.module_id, policy.minimum_level);
                const int before = priority;
                priority = policy.adjust_priority(priority, chars_map.contains(oper_info.name), ready);
                auto decision = basic_info_with_what("BlackFlowRecruitmentModuleGate");
                decision["details"] = json::object {
                    { "client", m_config->account_client() },
                    { "uid", m_config->account_uid() },
                    { "operator", oper_info.name },
                    { "module_id", policy.module_id },
                    { "known", evidence.has_value() },
                    { "module_level", evidence ? evidence->level : -1 },
                    { "ready", ready },
                    { "priority_before", before },
                    { "priority_after", priority },
                };
                callback(AsstMsg::SubTaskExtraInfo, decision);
            }

            if (theme == RoguelikeTheme::BlackFlow) {
                const auto& account = m_config->account_profile();
                const auto owned = account ? account->find_operator(oper_info.name) : std::nullopt;
                const auto training = owned && owned->get().training ? &*owned->get().training : nullptr;
                const auto effective_level = [&](int skill) {
                    if (!account || oper_info.temporary) {
                        return -1;
                    }
                    return account->skill_level_for_run(oper_info.name, skill, oper_info.elite, oper_info.level)
                        .value_or(-1);
                };
                auto observed = basic_info_with_what("BlackFlowRecruitmentTrainingObserved");
                observed["details"] = json::object {
                    { "client", m_config->account_client() },
                    { "uid", m_config->account_uid() },
                    { "operator", oper_info.name },
                    { "temporary_offer", oper_info.temporary },
                    { "observed_offer_elite", oper_info.elite },
                    { "observed_offer_level", oper_info.level },
                    { "account_training_known", training != nullptr },
                    { "account_base_skill_level", training ? training->base_skill_level : -1 },
                    { "configured_skill", recruit_info.skill },
                    { "configured_skill_effective_level", effective_level(recruit_info.skill) },
                    { "alternate_skill", recruit_info.alternate_skill },
                    { "alternate_skill_effective_level", effective_level(recruit_info.alternate_skill) },
                    { "temporary_offer_uses_account_training", false },
                    { "priority_adjusted", false },
                    { "unknown_is_not_maximum_training", true },
                };
                callback(AsstMsg::SubTaskExtraInfo, observed);
            }

            const auto tier = classify_recruit_candidate(
                oper_info.elite == 2,
                priority > 0,
                oper_info.hope_cost.has_value() && *oper_info.hope_cost == 0,
                oper_info.temporary,
                BattleData.get_rarity(battle::Role::Unknown, oper_info.name));
            if (tier == RoguelikeRecruitTier::Skip) {
                continue;
            }

            // 添加到候选名单
            auto existing_it = std::ranges::find_if(recruit_list, [&](const RoguelikeRecruitInfo& pri) -> bool {
                return pri.name == recruit_info.name;
            });
            if (existing_it == recruit_list.cend()) {
                RoguelikeRecruitInfo info;
                info.name = recruit_info.name;
                info.priority = priority;
                info.tier = tier;
                info.is_alternate = recruit_info.is_alternate;
                info.page_index = i;
                recruit_list.emplace_back(info);
            }
            Log.info(__FUNCTION__, "| Operator", recruit_info.name, "priority:", priority);
        }

        Log.info(__FUNCTION__, "| Page", i, "oper list:", oper_names);

        if (raw_paging) {
            paging_stop_reason =
                RoguelikeRecruitImageAnalyzer::paging_stop_reason(*current_geometry, previous_geometry, i, SwipeTimes);
            if (!paging_stop_reason.empty()) {
                LogInfo << __FUNCTION__ << "Recruitment scan stopped" << paging_stop_reason << "step" << i << "usable"
                        << oper_list.size() << "raw names" << current_geometry->named_cards.size() << "currency anchors"
                        << current_geometry->currency_anchors.size();
                break;
            }
            previous_geometry = std::move(current_geometry);
        }
        else {
            if (stop_swipe) {
                break;
            }

            // 每列4个干员，未滑动时可以显示2列，滑动后至少可以显示1列
            const size_t oper_count = oper_list.size();
            if ((i == 0 && oper_count < 8) || (oper_count != 4 && oper_count != 8)) {
                Log.trace(__FUNCTION__, "| Page", i, "oper count:", oper_count, "- stop swiping");
                break;
            }
            if (pre_oper_names == oper_names) {
                Log.trace(__FUNCTION__, "| Oper list not changed, stop swiping");
                break;
            }
            pre_oper_names = std::move(oper_names);
        }

        // 向右滑动
        LogTrace << __FUNCTION__ << "Recruitment scan continuing" << "step" << i << "usable" << oper_list.size();
        slowly_swipe(false, max_oper_x - 200);
        ++forward_swipe_calls;
        sleep(Task.get("RoguelikeCustom-HijackCoChar")->post_delay);
    }

    report_scan();

    // 没有候选干员，进入后备逻辑
    if (recruit_list.empty()) {
        return finish_without_recruitment();
    }

    // 候选已通过适用性筛选，统一排序；固定希望阈值不能阻断免费券或有效晋升。

    // 选择优先级最高的干员
    // 希望宽裕时给六星加权。依据：n6 下六星要 6~7 希望（实测峰值 4~10），
    // 而优先级表里 5★群奶白面鸮 810 只比 6★群奶夜莺 808 高 2 分，
    // 不加权就永远轮不到六星（都带 6★ 的上限效益也拿不到）。
    constexpr int SixStarBonus = 50;
    constexpr int HopeRichThreshold = 5; // 2026-10-26 由 6 降为 5：后期阵容强度不足，提前抢六星
    const bool hope_rich = cur_hope >= HopeRichThreshold;
    // 初始招募页面：职业尚未覆盖的候选加分，鼓励开局职业互补。
    // 加分数值来自资源 opening.role_diversity_bonus（缺省 500），C++ 不硬编码玩法策略。
    const bool opening_phase = m_initail_recruit;
    const int role_bonus = RoguelikeRecruit.get_opening_role_bonus(theme);
    auto oper_score = [hope_rich, opening_phase, role_bonus, &theme, this](const RoguelikeRecruitInfo& info) {
        int bonus = 0;
        if (hope_rich && BattleDataConfig::get_instance().get_rarity(battle::Role::Unknown, info.name) >= 5) {
            bonus = SixStarBonus;
        }
        if (opening_phase && role_bonus > 0) {
            // role 取自 battle_data.json：recruitment.json 的干员条目没有 role 字段，
            // 用 RoguelikeRecruit.get_oper_info(...).role 会恒为 Unknown。
            const battle::Role role = BattleDataConfig::get_instance().get_first_role(info.name);
            if (role != battle::Role::Unknown && !m_opening_roles.contains(role)) {
                bonus += role_bonus;
            }
        }
        return info.priority + bonus;
    };
    auto selected_oper = std::ranges::max_element(
        recruit_list,
        [&oper_score](const RoguelikeRecruitInfo& a, const RoguelikeRecruitInfo& b) {
            if (a.tier != b.tier) {
                return a.tier > b.tier;
            }
            return oper_score(a) < oper_score(b);
        });
    if (selected_oper == recruit_list.cend()) {
        Log.trace(__FUNCTION__, "| No opers in recruit list.");
        return false;
    }

    std::optional<blackflow::RecruitmentDutyOffer> duty_intent;
    if (theme == RoguelikeTheme::BlackFlow && mode == RoguelikeMode::Exp) {
        std::vector<std::pair<std::string, int>> eligible;
        for (const auto& candidate : recruit_list) {
            eligible.emplace_back(candidate.name, candidate.page_index);
        }
        const auto legacy_name = selected_oper->name;
        const int legacy_page = selected_oper->page_index;
        const auto duty_choice =
            blackflow::choose_recruitment_duty(duty_offers, eligible, legacy_name, legacy_page, m_initail_recruit);
        if (duty_choice.selected_offer) {
            const auto& desired = duty_offers[*duty_choice.selected_offer];
            const auto replacement = std::ranges::find_if(recruit_list, [&](const auto& entry) {
                return entry.name == desired.name && entry.page_index == desired.page_index;
            });
            if (replacement != recruit_list.end()) {
                selected_oper = replacement;
                duty_intent = desired;
                duty_intent->intrinsic_reference_used = duty_choice.intrinsic_reference_used;
            }
        }
        json::array offers;
        for (const auto& offer : duty_offers) {
            offers.emplace_back(blackflow::recruitment_duty_to_json(offer));
        }
        auto decision = basic_info_with_what("BlackFlowRecruitmentDutyDecision");
        decision["details"] = json::object {
            { "client", m_config->account_client() },
            { "uid", m_config->account_uid() },
            { "run_id", m_config->status().run_id },
            { "recruit_index", m_recruit_count },
            { "legacy_name", legacy_name },
            { "legacy_page", legacy_page },
            { "selected_name", selected_oper->name },
            { "reason", duty_choice.reason },
            { "priority_adjusted", duty_intent.has_value() },
            { "selection_adjusted", duty_intent.has_value() },
            { "legacy_numeric_priority_unchanged", true },
            { "legacy_priority_and_tier_preserved_as_fallback", true },
            { "fixed_opening", m_initail_recruit },
            { "visible_set_complete", nullptr },
            { "all_pages_complete", nullptr },
            { "hope_budget", cur_hope },
            { "hope_budget_source", "pre_page_scan_read_current_hope;revalidated_before_click" },
            { "offers", offers },
            { "intrinsic_reference_used", duty_choice.intrinsic_reference_used },
            { "scope",
              duty_choice.intrinsic_reference_used
                  ? "estimated_outside_intrinsic_reference_not_current_run_dominance"
                  : "remaining_current_roster_duties_not_global_strength_or_future_lifetime" },
        };
        callback(AsstMsg::SubTaskExtraInfo, decision);
    }

    std::optional<blackflow::TerminalDutyIntent> terminal_intent;
    if (theme == RoguelikeTheme::BlackFlow && mode == RoguelikeMode::Exp && m_blackflow_strategy == "ending1" &&
        !m_initail_recruit && !duty_intent) {
        std::vector<blackflow::TerminalDutyEligible> eligible;
        for (const auto& candidate : recruit_list) {
            eligible.push_back({ candidate.name, candidate.page_index, oper_score(candidate) });
        }
        const blackflow::TerminalDutyContext context {
            .strategy = m_blackflow_strategy,
            .client = m_config->account_client(),
            .uid = m_config->account_uid(),
            .run_id = m_config->status().run_id,
            .core = m_config->get_core_char(),
            .hope = cur_hope,
            .fixed_opening = m_initail_recruit,
            .account = m_config->account_profile().get(),
            .origins = &m_origin_ledger,
        };
        const auto choice = blackflow::choose_terminal_output_duty(
            duty_catalog,
            chars_map,
            context,
            duty_offers,
            eligible,
            selected_oper->name,
            selected_oper->page_index);
        if (choice.selected_offer && choice.intent) {
            const auto& offer = duty_offers[*choice.selected_offer];
            const auto chosen = std::ranges::find_if(recruit_list, [&](const auto& candidate) {
                return candidate.name == offer.name && candidate.page_index == offer.page_index;
            });
            if (chosen != recruit_list.end()) {
                selected_oper = chosen;
                duty_intent = offer;
                terminal_intent = *choice.intent;
            }
        }
        auto receipt = basic_info_with_what("BlackFlowRecruitmentTerminalDutyDecision");
        receipt["details"] = json::object {
            { "reason", choice.reason },
            { "adjusted", terminal_intent.has_value() },
            { "selected", selected_oper->name },
            { "client", context.client },
            { "uid", context.uid },
            { "run_id", context.run_id },
            { "evidence", choice.evidence },
            { "click_attempted", false },
        };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
    }

    Log.info(
        __FUNCTION__,
        "| Six-star bonus applied:",
        hope_rich,
        "hope:",
        cur_hope,
        "rarity:",
        BattleDataConfig::get_instance().get_rarity(battle::Role::Unknown, selected_oper->name),
        "base_priority:",
        selected_oper->priority);
    std::string char_name = selected_oper->name;
    LogInfo << __FUNCTION__ << "Selected recruitment tier" << static_cast<int>(selected_oper->tier) << char_name;
    Log.trace(
        __FUNCTION__,
        "| Top priority oper:",
        char_name,
        selected_oper->priority,
        "page",
        selected_oper->page_index,
        "/",
        i);

    // 滑动方向
    // 页码大于一半: 从右往左划动
    // 页码小于一半: 划回最左边再往右
    bool is_rtl = false;

    // 如果划动过，判断目标角色离哪个方向更近
    if (i != 0) {
        is_rtl = (selected_oper->page_index * 2) >= i;
        if (!is_rtl) {
            // 从左往右需要先划回最左边
            swipe_to_the_left_of_operlist(i + 1);
        }
    }

    const bool recruited = recruit_appointed_char(
        char_name,
        is_rtl,
        false,
        duty_intent ? &*duty_intent : nullptr,
        &duty_catalog,
        terminal_intent ? &*terminal_intent : nullptr);
    return duty_intent ? recruited : (m_task_ptr != nullptr && !m_task_ptr->get_enable()) || recruited;
}

bool asst::RoguelikeRecruitTaskPlugin::finish_without_recruitment()
{
    LogInfo << __FUNCTION__ << "No suitable recruitment candidate";
    if (m_config->get_theme() != RoguelikeTheme::BlackFlow || m_initail_recruit) {
        return true;
    }

    const auto before_image = ctrler()->get_image();
    const auto before = blackflow::BlackFlowVoucherStash(before_image).observe();
    if (!before.can_store()) {
        auto info = basic_info_with_what("BlackFlowVoucherStashDecision");
        info["details"] = json::object {
            { "status", "unavailable" },
            { "reason", "no_suitable_candidate" },
            { "recruitment_page", before.recruitment_page },
            { "enabled_button", before.enabled_button },
            { "stored", before.count ? before.count->used : -1 },
            { "capacity", before.count ? before.count->capacity : -1 },
        };
        callback(AsstMsg::SubTaskExtraInfo, info);
        return true;
    }

    if (!m_voucher_stash.begin(before)) {
        return verify_voucher_stash();
    }
    // The resource chain owns the one button click and optional confirmation.
    // Completion is established below, never from ProcessTask returning true.
    ProcessTask request(*this, { "BlackFlow@Roguelike@ReserveRecruitmentVoucher" });
    // Reuse the page whose enabled button and capacity were verified together.
    // The pulsing button can fail matching on the very next animation frame.
    request.set_reusable_image(before_image);
    request.set_retry_times(2);
    const bool action_completed = request.run();
    LogInfo << __FUNCTION__ << "Voucher storage action finished" << action_completed;
    return verify_voucher_stash();
}

bool asst::RoguelikeRecruitTaskPlugin::verify_voucher_stash()
{
    if (!m_voucher_stash.pending()) {
        return false;
    }
    constexpr int VerificationFrames = 5;
    for (int i = 0; i < VerificationFrames && !need_exit(); ++i) {
        const auto after = blackflow::BlackFlowVoucherStash(ctrler()->get_image()).observe();
        const auto proof = blackflow::BlackFlowVoucherStash::verify(*m_voucher_stash.before(), after);
        if (proof != blackflow::VoucherStashProof::Pending) {
            auto process = dynamic_cast<ProcessTask*>(m_task_ptr);
            const auto source = process && process->get_last_hit() ? process->get_last_hit()->task_ptr : nullptr;
            const bool queued =
                source && process->override_next(source->name, { "BlackFlow@Roguelike@VoucherStashResume" });
            if (queued) {
                m_stash_resume_source = source->name;
            }
            auto info = basic_info_with_what("BlackFlowVoucherStashDecision");
            info["details"] = json::object {
                { "status", "confirmed" },
                { "reason", "no_suitable_candidate" },
                { "proof",
                  proof == blackflow::VoucherStashProof::CounterIncreased ? "counter_increased" : "successor_page" },
                { "stored_before", m_voucher_stash.before()->count->used },
                { "stored_after", after.count ? after.count->used : -1 },
                { "capacity", m_voucher_stash.before()->count->capacity },
                { "successor_page", after.successor_page },
                { "continuation_queued", queued },
            };
            callback(AsstMsg::SubTaskExtraInfo, info);
            m_voucher_stash.reset();
            if (!queued) {
                LogError << __FUNCTION__ << "Stored voucher but cannot queue safe continuation";
                if (m_task_ptr != nullptr) {
                    m_task_ptr->set_enable(false);
                }
            }
            return queued;
        }
        sleep(250);
    }

    auto info = basic_info_with_what("BlackFlowVoucherStashDecision");
    info["details"] = json::object {
        { "status", "unconfirmed" },
        { "reason", "no_suitable_candidate" },
        { "stored_before", m_voucher_stash.before()->count->used },
        { "capacity", m_voucher_stash.before()->count->capacity },
        { "repeat_click_permitted", false },
    };
    callback(AsstMsg::SubTaskExtraInfo, info);
    LogWarn << __FUNCTION__ << "Voucher storage outcome remains unverified; stop without discarding another voucher";
    if (m_task_ptr != nullptr) {
        m_task_ptr->set_enable(false);
    }
    return false;
}

void asst::RoguelikeRecruitTaskPlugin::reset_in_run_variables()
{
    if (auto process = dynamic_cast<ProcessTask*>(m_task_ptr); process && !m_stash_resume_source.empty()) {
        process->remove_override_next(m_stash_resume_source);
    }
    m_recruit_count = 0;
    m_starts_complete = false;
    m_team_complete = false;
    m_opening_roles.clear();
    m_voucher_stash.reset();
    m_stash_resume_source.clear();
    discard_pending_recruitment("run_reset");
    m_origin_ledger.clear();
    m_recruitment_event = RecruitmentEvent::Select;
    m_config->reset_start_oper_index();
}

bool asst::RoguelikeRecruitTaskPlugin::lazy_recruit()
{
    LogTraceFunction;

    ProcessTask(*this, { "RoguelikeRecruitLazyClick1" }).run();
    return true;
}

bool asst::RoguelikeRecruitTaskPlugin::resolve_elite_two_start_observation(Recruitment& selected)
{
    if (selected.elite >= 0) {
        return true;
    }
    // Reobserve the same card once without navigating. A failed reading is
    // neither a positive non-E2 result nor permission to abandon exploration.
    if (!sleep(Task.get("RoguelikeCustom-HijackCoChar")->post_delay)) {
        return false;
    }
    RoguelikeRecruitImageAnalyzer refreshed(ctrler()->get_image());
    if (refreshed.analyze()) {
        const auto candidate = std::ranges::find_if(refreshed.get_result(), [&](const auto& value) {
            return value.name == selected.name && value.rect == selected.rect && value.elite >= 0;
        });
        if (candidate != refreshed.get_result().end()) {
            selected = *candidate;
            return true;
        }
    }
    auto incomplete = basic_info_with_what("RoguelikeRecruitmentIncomplete");
    incomplete["details"] = json::object {
        { "name", selected.name },
        { "elite", -1 },
        { "reason", "elite_two_start_observation_unresolved" },
        { "same_card_reobservations", 1 },
        { "recruited", false },
        { "abandon_requested", false },
    };
    callback(AsstMsg::SubTaskExtraInfo, incomplete);
    if (m_task_ptr != nullptr) {
        m_task_ptr->set_enable(false);
    }
    return false;
}

bool asst::RoguelikeRecruitTaskPlugin::recruit_appointed_char(
    const std::string& char_name,
    bool is_rtl,
    bool require_strength,
    const blackflow::RecruitmentDutyOffer* duty_intent,
    const blackflow::BlackFlowRecruitmentDutyCatalog* known_catalog,
    const blackflow::TerminalDutyIntent* terminal_intent)
{
    LogTraceFunction;
    if (terminal_intent &&
        (m_config->get_theme() != RoguelikeTheme::BlackFlow || m_config->get_mode() != RoguelikeMode::Exp ||
         m_blackflow_strategy != "ending1" || m_initail_recruit)) {
        return false;
    }
    // 最大滑动次数
    int SwipeTimes = Task.get("RoguelikeRecruitSwipeMaxTime")->max_times;
    std::unordered_set<std::string> pre_oper_names;
    bool has_been_same = false;
    int i = 0;
    // 是否凹直升
    bool start_with_elite_two = m_config->get_start_with_elite_two();
    bool only_start_with_elite_two = m_config->get_only_start_with_elite_two();

    blackflow::BlackFlowModifierEvidenceProducer modifier_catalog;
    if (m_config->get_theme() == RoguelikeTheme::BlackFlow && m_config->get_mode() == RoguelikeMode::Exp) {
        modifier_catalog.load(ResDir.get() / utils::path("roguelike/BlackFlow/modifier_components.json"));
    }
    blackflow::BlackFlowRecruitmentDutyCatalog fallback_catalog;
    const auto* catalog = known_catalog;
    if (m_config->get_theme() == RoguelikeTheme::BlackFlow && m_config->get_mode() == RoguelikeMode::Exp &&
        (!catalog || !catalog->valid())) {
        fallback_catalog.load(
            ResDir.get() / utils::path("roguelike/BlackFlow/recruitment_duties.json"),
            ResDir.get() / utils::path("roguelike/BlackFlow/operator_training.json"),
            ResDir.get() / utils::path("battle_data.json"));
        catalog = &fallback_catalog;
    }

    for (; i != SwipeTimes; ++i) {
        if (need_exit()) {
            return false;
        }
        auto image = ctrler()->get_image();
        RoguelikeRecruitImageAnalyzer analyzer(image);

        int max_oper_x = 700;
        // 只处理识别成功的情况，失败(无任何结果)时继续滑动
        if (analyzer.analyze()) {
            const auto& chars = analyzer.get_result();
            max_oper_x = std::ranges::max(chars | std::views::transform([&](const auto& x) { return x.rect.x; }));
            auto it = std::ranges::find_if(chars, [&](const battle::roguelike::Recruitment& oper) -> bool {
                return oper.name == char_name;
            });

            std::unordered_set<std::string> oper_names;
            std::ranges::transform(
                chars,
                std::inserter(oper_names, oper_names.end()),
                std::mem_fn(&battle::roguelike::Recruitment::name));
            Log.info(__FUNCTION__, "| Oper list:", oper_names);

            if (it != chars.cend()) {
                auto selected = *it;
                std::optional<blackflow::RecruitmentDutyOffer> final_origin;
                if (m_config->get_theme() == RoguelikeTheme::BlackFlow && m_config->get_mode() == RoguelikeMode::Exp) {
                    std::vector<std::string> attribute_names;
                    for (const auto& oper : chars) {
                        attribute_names.push_back(oper.name);
                    }
                    for (const auto& [name, oper] : m_config->status().opers) {
                        attribute_names.push_back(name);
                    }
                    const auto modifier_parts = modifier_catalog.observe(
                        blackflow::ModifierEvidenceRequest {
                            .profile = m_config->account_profile().get(),
                            .client = m_config->account_client(),
                            .uid = m_config->account_uid(),
                            .run_id = m_config->status().run_id,
                            .observed_at = MAA_NS::format_now(),
                            .operator_names = attribute_names,
                            .configured_squad = m_config->get_squad(),
                            .observed_purchase_names = m_config->status().collections,
                            .life = m_config->account_profile() ? m_config->account_profile()->life_modifier_evidence()
                                                                : std::nullopt,
                        });
                    const auto output_evidence = blackflow::recruitment_profile_output_evidence(
                        m_config->account_profile().get(),
                        m_config->account_client(),
                        m_config->account_uid(),
                        m_config->status().run_id,
                        attribute_names,
                        modifier_parts.comparison_receipt);
                    auto attribute_observation = basic_info_with_what("BlackFlowRecruitmentAttributeEvidence");
                    attribute_observation["details"] = blackflow::recruitment_profile_output_to_json(output_evidence);
                    attribute_observation["details"]["stage"] = "final_refind";
                    attribute_observation["details"]["modifier_parts"] =
                        blackflow::modifier_evidence_parts_to_json(modifier_parts);
                    callback(AsstMsg::SubTaskExtraInfo, attribute_observation);
                    const blackflow::RecruitmentDutyContext context {
                        .client = m_config->account_client(),
                        .uid = m_config->account_uid(),
                        .run_id = m_config->status().run_id,
                        .observed_at = MAA_NS::format_now(),
                        .page_index = i,
                        .hope = read_current_hope(image),
                        .mode = static_cast<int>(m_config->get_mode()),
                        .initial = m_initail_recruit,
                        .output_evidence = &output_evidence,
                    };
                    const auto actual = blackflow::observe_recruitment_duties(
                        analyzer,
                        *catalog,
                        m_config->account_profile() ? &*m_config->account_profile() : nullptr,
                        m_config->status().opers,
                        context,
                        &m_origin_ledger);
                    const auto final = blackflow::resolve_recruitment_final_offer(
                        analyzer,
                        actual,
                        selected,
                        duty_intent ? std::optional<blackflow::RecruitmentDutyOffer>(*duty_intent) : std::nullopt);
                    final_origin = final.observed;
                    auto refind = basic_info_with_what(
                        duty_intent ? "BlackFlowRecruitmentDutyRefind" : "BlackFlowRecruitmentFinalOfferObserved");
                    refind["details"] = json::object {
                        { "client", context.client },
                        { "uid", context.uid },
                        { "run_id", context.run_id },
                        { "recruit_index", m_recruit_count },
                        { "intended",
                          duty_intent ? blackflow::recruitment_duty_to_json(*duty_intent) : json::value(nullptr) },
                        { "actual",
                          final.observed ? blackflow::recruitment_duty_to_json(*final.observed)
                                         : json::value(nullptr) },
                        { "matched", final.intent_matches },
                        { "reason", final.reason },
                        { "legacy_choice_unchanged", duty_intent == nullptr },
                        { "click_attempted", false },
                    };
                    callback(AsstMsg::SubTaskExtraInfo, refind);
                    if (duty_intent && !final.intent_matches) {
                        return false;
                    }
                    if (terminal_intent) {
                        const blackflow::TerminalDutyContext terminal_context {
                            .strategy = m_blackflow_strategy,
                            .client = context.client,
                            .uid = context.uid,
                            .run_id = context.run_id,
                            .core = m_config->get_core_char(),
                            .hope = context.hope,
                            .fixed_opening = m_initail_recruit,
                            .account = m_config->account_profile().get(),
                            .origins = &m_origin_ledger,
                        };
                        std::string reason;
                        const bool matched = final.observed && blackflow::terminal_output_refind_matches(
                                                                   *terminal_intent,
                                                                   *final.observed,
                                                                   *catalog,
                                                                   m_config->status().opers,
                                                                   terminal_context,
                                                                   &reason);
                        auto terminal_refind = basic_info_with_what("BlackFlowRecruitmentTerminalDutyRefind");
                        terminal_refind["details"] = json::object {
                            { "matched", matched },       { "reason", reason },          { "uid", context.uid },
                            { "run_id", context.run_id }, { "selected", selected.name }, { "click_attempted", false },
                        };
                        callback(AsstMsg::SubTaskExtraInfo, terminal_refind);
                        if (!matched) {
                            return false;
                        }
                    }
                }
                const bool requires_elite_two_observation =
                    start_with_elite_two && (!m_config->get_run_for_collectible() || only_start_with_elite_two) &&
                    m_recruit_count == 1;
                if (requires_elite_two_observation && !resolve_elite_two_start_observation(selected)) {
                    return false;
                }
                if (require_strength && !is_recruitment_strength_sufficient(selected.elite, selected.level)) {
                    Log.info(
                        __FUNCTION__,
                        "| Own oper does not meet recruitment strength gate:",
                        selected.name,
                        "elite",
                        selected.elite,
                        "level",
                        selected.level);
                    return false;
                }
                // !get_run_for_collectible() 即当前没有在烧开水/水已经烧好了
                // 需要凹直升且，要么已经烧好了水，要么只需要凹直升不需要烧水
                // 凹直升只对第一位招募的干员生效
                if (start_with_elite_two && (!m_config->get_run_for_collectible() || only_start_with_elite_two) &&
                    m_recruit_count == 1) {
                    if (selected.elite == 2) {
                        if (m_config->get_first_floor_foldartal()) {
                            return select_oper(
                                selected,
                                final_origin); // Continue only after a candidate was actually selected.
                        }
                        else {
                            m_task_ptr->set_enable(false); // 仅当之后没有东西要凹了才停止
                        }
                    }
                    else {
                        // 非只凹直升时，重新烧水
                        if (!only_start_with_elite_two) {
                            m_config->set_run_for_collectible(true);
                        }
                        m_control_ptr->exit_then_stop();
                    }
                }
                else {
                    return select_oper(selected, final_origin);
                }
                return true;
            }
            if (pre_oper_names == oper_names) {
                if (has_been_same) {
                    Log.trace(__FUNCTION__, "| Oper list not changed for three times, stop swiping");
                    break;
                }
                else {
                    has_been_same = true;
                }
            }
            else {
                has_been_same = false;
                pre_oper_names = std::move(oper_names);
            }
        }

        // 没识别到目标干员，可能不在这一页，继续划动
        if (is_rtl) {
            slowly_swipe(true);
        }
        else {
            slowly_swipe(false, max_oper_x - 200);
        }
        sleep(Task.get("RoguelikeCustom-HijackCoChar")->post_delay);
    }
    Log.info(__FUNCTION__, "| Cannot find oper `" + char_name + "`");
    swipe_to_the_left_of_operlist(i + 1);
    return false;
}

bool asst::RoguelikeRecruitTaskPlugin::recruit_support_char(const std::string& name, const int max_refresh)
{
    if (RoguelikeRecruit.is_oper_excluded(m_config->get_theme(), name)) {
        return false;
    }
    LogTraceFunction;

    // 判断是否存在“选择助战”按钮，存在则点击
    auto screen_choose = ctrler()->get_image();
    RoguelikeRecruitSupportAnalyzer analyzer_choose(screen_choose);
    analyzer_choose.set_mode(SupportAnalyzeMode::ChooseSupportBtn);
    if (!analyzer_choose.analyze()) { // 非开局招募，无助战按钮
        return false;
    }
    const auto& choose_btn_rect = analyzer_choose.get_result_choose_support();
    Log.info(__FUNCTION__, "| check choose support btn ", choose_btn_rect);
    ctrler()->click(choose_btn_rect);
    ProcessTask(*this, { "RoguelikeRecruitSupportEnterFlag" }).run(); // 等待页面加载

    // 识别所有干员，应该最多两页
    const int MaxPageCnt = 2;

    std::vector<RecruitSupportCharInfo> satisfied_chars;
    for (int retry = 0; retry <= max_refresh; ++retry) {
        for (int page = 0; page < MaxPageCnt; ++page) {
            auto screen_char = ctrler()->get_image();
            RoguelikeRecruitSupportAnalyzer analyzer_char(screen_char);
            analyzer_char.set_mode(SupportAnalyzeMode::AnalyzeChars);
            analyzer_char.set_required({ name });
            if (analyzer_char.analyze()) {
                auto& chars_page = analyzer_char.get_result_char();

                bool use_nonfriend_support = m_config->get_use_nonfriend_support();
                auto check_satisfy = [&use_nonfriend_support](const RecruitSupportCharInfo& chara) {
                    return chara.is_friend || use_nonfriend_support;
                };
                std::copy_if(
                    chars_page.begin(),
                    chars_page.end(),
                    std::inserter(satisfied_chars, std::begin(satisfied_chars)),
                    check_satisfy);

                if (satisfied_chars.size()) {
                    break;
                }
            }
            if (page != MaxPageCnt - 1) {
                ProcessTask(*this, { "RoguelikeSupportSwipeRight" }).run();
            }
        }
        if (satisfied_chars.size()) {
            break;
        }

        // 刷新助战
        if (retry >= max_refresh) {
            break;
        }
        auto screen_refresh = ctrler()->get_image();
        RoguelikeRecruitSupportAnalyzer analyzer_refresh(screen_refresh);
        analyzer_refresh.set_mode(SupportAnalyzeMode::RefreshSupportBtn);
        if (!analyzer_refresh.analyze()) {
            click_return_button();
            return false;
        }
        auto& refresh_info = analyzer_refresh.get_result_refresh();
        if (refresh_info.in_cooldown) {
            sleep(refresh_info.remain_secs * 1000);
        }
        ctrler()->click(refresh_info.rect);
        sleep(Task.get("RoguelikeRefreshSupportBtnOcr")->post_delay);
        ProcessTask(*this, { "RoguelikeSupportSwipeLeft" }).run();
    }
    if (satisfied_chars.empty()) {
        // 找不到需要的助战干员，返回正常招募逻辑
        Log.info(__FUNCTION__, "| can't find support char `", name, "`");
        click_return_button();
        return false;
    }

    // 点击干员并记录信息
    auto& satisfied_char = satisfied_chars.front();
    if (!select_oper(satisfied_char.oper_info)) {
        click_return_button();
        return false;
    }

    sleep(500);

    // 确认选择
    ProcessTask(*this, { "Roguelike@RecruitSupportConfirm" }).set_retry_times(20).run();
    return true;
}

bool asst::RoguelikeRecruitTaskPlugin::recruit_own_char(const std::string& name)
{
    if (RoguelikeRecruit.is_oper_excluded(m_config->get_theme(), name)) {
        return false;
    }
    LogTraceFunction;

    return recruit_appointed_char(name, false, true);
}

int asst::RoguelikeRecruitTaskPlugin::read_current_hope(const cv::Mat& image)
{
    if (m_config->get_theme() == RoguelikeTheme::BlackFlow) {
        const auto hope = RoguelikeParameterAnalyzer::read_blackflow_hope(
            image,
            RoguelikeParameterAnalyzer::BlackFlowHopeLayout::RecruitmentHeader);
        const int current = hope.value_or(-1);
        // Invalidate a stale balance when this exact page is unreadable.
        m_config->status().hope = current;
        LogInfo << __FUNCTION__ << "Recruitment current hope" << current << "source"
                << "anchored yellow current-value glyphs";
        return current;
    }
    int cur_hope = -1;
    {
        // 1280x720 下实测：色条右侧的当前希望数字位于 x≈600..640、y≈18..52。
        // 数字为 1~2 位，48px 宽度足够；两个候选 roi 依次尝试以提高识别率。
        static const std::vector<Rect> HopeRois = {
            Rect { 599, 16, 48, 38 },
            Rect { 592, 12, 62, 44 },
            // 希望为两位数（如 10 / 19）时数字右移，靠前两个 roi 会截断，补一个更宽的
            Rect { 594, 10, 100, 42 },
        };
        // 数字是黄色、底色是深棕，自动二值化的阈值波动较大；多档 bin_expansion 各试一次。
        static const std::vector<int> HopeBinExpansions = { 1, 2, 3 };
        for (const Rect& roi : HopeRois) {
            for (int bin_expansion : HopeBinExpansions) {
                RegionOCRer hope_analyzer(image);
                hope_analyzer.set_task_info("NumberOcrReplace");
                hope_analyzer.set_roi(roi);
                hope_analyzer.set_bin_expansion(bin_expansion);
                if (!hope_analyzer.analyze()) {
                    continue;
                }
                const std::string hope_text = hope_analyzer.get_result().text;
                std::string digits;
                bool started = false;
                for (char c : hope_text) {
                    if (std::isdigit(static_cast<unsigned char>(c))) {
                        started = true;
                        digits.push_back(c);
                    }
                    else if (started) {
                        break;
                    }
                }
                if (!digits.empty() && digits.size() <= 2) {
                    cur_hope = std::stoi(digits);
                    Log.info(
                        __FUNCTION__,
                        "| Current hope:",
                        cur_hope,
                        "raw:",
                        hope_text,
                        "roi:",
                        roi.x,
                        roi.y,
                        "bin:",
                        bin_expansion);
                    break;
                }
                Log.info(__FUNCTION__, "| Current hope raw not digit:", hope_text, "bin:", bin_expansion);
            }
            if (cur_hope >= 0) {
                break;
            }
        }
        if (cur_hope < 0) {
            Log.info(__FUNCTION__, "| Current hope remains unknown");
        }
        else {
            // Share this observed budget with the shop; a failed OCR never invents a new balance.
            m_config->status().hope = cur_hope;
        }
    }

    return cur_hope;
}

bool asst::RoguelikeRecruitTaskPlugin::can_afford(const Recruitment& oper, int hope) const
{
    if (oper.hope_cost && (*oper.hope_cost < 0 || (hope >= 0 && *oper.hope_cost > hope))) {
        LogInfo << __FUNCTION__ << "Recruitment candidate exceeds verified budget" << oper.name << *oper.hope_cost
                << hope;
        return false;
    }
    if (m_config->get_theme() == RoguelikeTheme::BlackFlow && (!oper.hope_cost || (hope < 0 && *oper.hope_cost != 0))) {
        LogInfo << __FUNCTION__ << "Recruitment affordability remains unknown" << oper.name
                << oper.hope_cost.value_or(-1) << hope;
        return false;
    }
    return true;
}

bool asst::RoguelikeRecruitTaskPlugin::select_oper(const Recruitment& oper)
{
    return select_oper(oper, std::nullopt);
}

bool asst::RoguelikeRecruitTaskPlugin::select_oper(
    const Recruitment& oper,
    const std::optional<blackflow::RecruitmentDutyOffer>& origin)
{
    if (!can_afford(oper, read_current_hope(ctrler()->get_image()))) {
        return false;
    }
    LogInfo << __FUNCTION__ << "Select pending candidate" << oper.name << oper.elite << oper.level;
    const bool clicked = ctrler()->click(oper.rect);
    if (m_config->get_theme() == RoguelikeTheme::BlackFlow) {
        auto execution = basic_info_with_what("BlackFlowRecruitmentSelectionInput");
        execution["details"] = json::object {
            { "client", m_config->account_client() },
            { "uid", m_config->account_uid() },
            { "recruit_index", m_recruit_count },
            { "name", oper.name },
            { "elite", oper.elite },
            { "level", oper.level },
            { "hope_cost", oper.hope_cost ? json::value(*oper.hope_cost) : json::value(nullptr) },
            { "click_rect", json::array { oper.rect.x, oper.rect.y, oper.rect.width, oper.rect.height } },
            { "click_attempted", true },
            { "controller_accepted", clicked },
            { "recruitment_committed", false },
        };
        callback(AsstMsg::SubTaskExtraInfo, execution);
    }
    if (!clicked) {
        return false;
    }
    std::optional<blackflow::RecruitmentDutyOffer> exact_origin;
    if (origin && origin->name == oper.name && origin->elite == oper.elite && origin->level == oper.level &&
        origin->hope_cost == oper.hope_cost && origin->id == RoguelikeTraining.unique_identity(oper.name)) {
        exact_origin = origin;
    }
    m_pending_recruitment = PendingRecruitment { oper,
                                                 false,
                                                 false,
                                                 m_initail_recruit,
                                                 std::move(exact_origin),
                                                 m_config->account_client(),
                                                 m_config->account_uid(),
                                                 m_config->status().run_id };
    return true;
}

void asst::RoguelikeRecruitTaskPlugin::discard_pending_recruitment(std::string_view reason)
{
    if (m_pending_recruitment) {
        LogInfo << __FUNCTION__ << "Discard uncommitted candidate" << m_pending_recruitment->oper.name << reason;
        m_pending_recruitment.reset();
    }
}

bool asst::RoguelikeRecruitTaskPlugin::commit_pending_recruitment()
{
    if (!m_pending_recruitment || !m_pending_recruitment->confirmed) {
        return true;
    }
    const auto pending = std::exchange(m_pending_recruitment, std::nullopt);
    const auto& oper = pending->oper;
    if (m_config->get_theme() == RoguelikeTheme::BlackFlow &&
        (pending->client != m_config->account_client() || pending->uid != m_config->account_uid() ||
         pending->run_id != m_config->status().run_id)) {
        m_origin_ledger.clear();
        LogWarn << __FUNCTION__ << "Discard confirmed candidate from a different account or exploration" << oper.name;
        return false;
    }
    const auto previous = m_config->status().opers.find(oper.name);
    const auto before =
        previous == m_config->status().opers.end() ? std::nullopt : std::optional<RoguelikeOper>(previous->second);
    if (!merge_current_run_operator(m_config->status().opers, oper.name, oper.elite, oper.level)) {
        LogWarn << __FUNCTION__ << "Invalid confirmed recruitment observation" << oper.name << oper.elite << oper.level;
        return true;
    }
    if (oper.elite < 0 || oper.level <= 0) {
        LogWarn << __FUNCTION__ << "Confirmed recruitment has incomplete strength" << oper.name << oper.elite
                << oper.level;
    }
    if (m_config->get_theme() == RoguelikeTheme::BlackFlow) {
        const bool bound =
            m_origin_ledger.bind(m_config->account_client(), m_config->account_uid(), m_config->status().run_id);
        const bool source_matches = pending->origin && pending->origin->name == oper.name &&
                                    pending->origin->elite == oper.elite && pending->origin->level == oper.level &&
                                    pending->origin->hope_cost == oper.hope_cost;
        const bool origin_recorded =
            bound && source_matches && m_origin_ledger.confirm(*pending->origin, before, true, m_recruit_count);
        const auto identity = RoguelikeTraining.unique_identity(oper.name);
        if (!origin_recorded) {
            m_origin_ledger.forget(identity);
        }
        const auto& committed = m_config->status().opers.at(oper.name);
        const auto origin = m_origin_ledger.find(
            m_config->account_client(),
            m_config->account_uid(),
            m_config->status().run_id,
            identity,
            committed);
        auto source_receipt = basic_info_with_what("BlackFlowRecruitmentOriginConfirmed");
        source_receipt["details"] = json::object {
            { "client", m_config->account_client() },
            { "uid", m_config->account_uid() },
            { "run_id", m_config->status().run_id },
            { "name", oper.name },
            { "recruit_index", m_recruit_count },
            { "confirmed", true },
            { "origin_recorded", origin_recorded },
            { "origin", origin ? blackflow::recruitment_origin_to_json(*origin) : json::value(nullptr) },
            { "unknown_is_not_ordinary", true },
            { "account_cache_written", false },
        };
        callback(AsstMsg::SubTaskExtraInfo, source_receipt);
        auto receipt = basic_info_with_what("BlackFlowRecruitmentObserved");
        const auto& stored = m_config->status().opers.at(oper.name);
        receipt["details"] = json::object {
            { "client", m_config->account_client() },
            { "uid", m_config->account_uid() },
            { "recruit_index", m_recruit_count },
            { "run_id", m_config->status().run_id },
            { "name", oper.name },
            { "candidate_elite", oper.elite },
            { "candidate_level", oper.level },
            { "candidate_elite_known", oper.elite >= 0 },
            { "candidate_level_known", oper.level > 0 },
            { "current_run_elite", stored.elite },
            { "current_run_level", stored.level },
            { "account_strength_used", false },
            { "confirmed", true },
        };
        callback(AsstMsg::SubTaskExtraInfo, receipt);
    }
    if (pending->advance_start) {
        m_config->advance_start_oper_index();
        m_starts_complete = true;
    }
    if (pending->opening) {
        const auto role = BattleData.get_first_role(oper.name);
        if (role != battle::Role::Unknown) {
            m_opening_roles.insert(role);
        }
    }
    LogInfo << __FUNCTION__ << "Committed confirmed recruitment receipt" << oper.name << oper.elite << oper.level;
    return true;
}

void asst::RoguelikeRecruitTaskPlugin::swipe_to_the_left_of_operlist(int loop_times)
{
    for (int i = 0; i != loop_times; ++i) {
        ProcessTask(*this, { "RoguelikeRecruitOperListSwipeToTheLeft" }).run();
    }
    ProcessTask(*this, { "SleepAfterOperListQuickSwipe" }).run();
}

void asst::RoguelikeRecruitTaskPlugin::slowly_swipe(bool to_left, int swipe_dist)
{
    std::string swipe_task_name =
        to_left ? "RoguelikeRecruitOperListSlowlySwipeToTheLeft" : "RoguelikeRecruitOperListSlowlySwipeToTheRight";
    if (!ControlFeat::support(
            ctrler()->support_features(),
            ControlFeat::PRECISE_SWIPE)) { // 不能精准滑动时不使用 swipe_dist 参数
        ProcessTask(*this, { swipe_task_name }).run();
        return;
    }

    if (!to_left) {
        swipe_dist = -swipe_dist;
    }
    auto swipe_task = Task.get(swipe_task_name);
    const Rect& StartPoint = swipe_task->specific_rect;
    ctrler()->swipe(
        StartPoint,
        { StartPoint.x + swipe_dist - StartPoint.width, StartPoint.y, StartPoint.width, StartPoint.height },
        swipe_task->special_params.empty() ? 0 : swipe_task->special_params.at(0),
        (swipe_task->special_params.size() < 2) ? SwipeExtraDirection::None
                                                : to_swipe_extra_direction(swipe_task->special_params.at(1)),
        (swipe_task->special_params.size() < 3) ? 1 : swipe_task->special_params.at(2) / 10.0,
        (swipe_task->special_params.size() < 4) ? 1 : swipe_task->special_params.at(3) / 10.0);
    sleep(swipe_task->post_delay);
}
