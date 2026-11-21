#include "RoguelikeCustomStartTaskPlugin.h"

#include <array>

#include "Config/GeneralConfig.h"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "Task/ProcessTask.h"
#include "Utils/Logger.hpp"
#include "Vision/Matcher.h"
#include "Vision/Miscellaneous/PipelineAnalyzer.h"
#include "Vision/OCRer.h"
#include "Vision/Roguelike/RoguelikeRecruitImageAnalyzer.h"

namespace asst
{
// Apply the existing task-specific replacements and required-word rules to one
// unfiltered capture. The object and results never survive the current UI frame.
class RoguelikeSquadFrameOCRer final : public OCRer
{
public:
    using OCRer::OCRer;

    bool shares_capture_with(const RoguelikeSquadFrameOCRer& source) const
    {
        return m_image.data == source.m_image.data && m_image.size() == source.m_image.size() &&
               m_image.step == source.m_image.step && m_roi == source.m_roi &&
               m_params.use_char_model == source.m_params.use_char_model &&
               m_params.without_det == source.m_params.without_det && m_params.use_raw == source.m_params.use_raw &&
               m_params.bin_threshold_lower == source.m_params.bin_threshold_lower &&
               m_params.bin_threshold_upper == source.m_params.bin_threshold_upper;
    }

    ResultsVecOpt filter_same_frame(const RoguelikeSquadFrameOCRer& source, const ResultsVec& raw_results) const
    {
        if (!shares_capture_with(source)) {
            // Configuration changes to the ROI/model must retain fresh OCR.
            return analyze();
        }
        ResultsVec results;
        for (auto result : raw_results) {
            postproc_trim_(result);
            postproc_replace_(result);
            if (filter_and_replace_by_required_(result)) {
                results.emplace_back(std::move(result));
            }
        }
        if (results.empty()) {
            return std::nullopt;
        }
        switch (m_params.order_by) {
        case ResultOrderBy::Horizontal:
            sort_by_horizontal_(results);
            break;
        case ResultOrderBy::Vertical:
            sort_by_vertical_(results);
            break;
        case ResultOrderBy::Score:
            sort_by_score_(results);
            break;
        case ResultOrderBy::None:
            break;
        }
        return results;
    }
};
}

bool asst::RoguelikeCustomStartTaskPlugin::verify(AsstMsg msg, const json::value& details) const
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
    std::string_view task_view = task;
    if (task_view.starts_with(roguelike_name)) {
        task_view.remove_prefix(roguelike_name.length());
    }
    // A passive RecruitMain delegates the voucher click to this plugin. Themes
    // retaining ClickSelf have already clicked and must not be clicked again.
    if (msg == AsstMsg::SubTaskCompleted && (task_view == "Roguelike@RecruitMain" || task_view == "RecruitMain") &&
        details.get("details", "action", "") == "DoNothing") {
        m_waiting_to_run = RoguelikeCustomType::Voucher;
        return true;
    }
    if (msg == AsstMsg::SubTaskCompleted && task_view.ends_with("InitialRecruitOperatorList") &&
        details.get("details", "action", "") == "DoNothing") {
        m_waiting_to_run = RoguelikeCustomType::Voucher;
        return true;
    }
    static const std::array<std::tuple<AsstMsg, std::string_view, RoguelikeCustomType>, 3> TaskMap = {
        std::make_tuple(AsstMsg::SubTaskCompleted, "Roguelike@Squad-EnterPoint", RoguelikeCustomType::Squad),
        std::make_tuple(AsstMsg::SubTaskStart, "Roguelike@LastReward-EnterPoint", RoguelikeCustomType::Reward),
        std::make_tuple(AsstMsg::SubTaskCompleted, "Roguelike@RolesDefault", RoguelikeCustomType::Roles),
    };

    m_waiting_to_run = RoguelikeCustomType::None;
    for (const auto& [t_msg, t_task, t] : TaskMap) {
        if (t_msg != msg) {
            continue;
        }
        // ProcessTask reports some task names without the "Roguelike@" prefix, while the
        // TaskMap entries carry it. Accept both spellings so the squad hijack still runs.
        constexpr std::string_view roguelike_prefix = "Roguelike@";
        const bool full_match = task_view.ends_with(t_task);
        const bool short_match =
            t_task.starts_with(roguelike_prefix) && task_view == t_task.substr(roguelike_prefix.length());
        if (full_match || short_match) {
            m_waiting_to_run = t;
            break;
        }
    }

    if (m_waiting_to_run == RoguelikeCustomType::None) {
        return false;
    }
    if (m_waiting_to_run == RoguelikeCustomType::Reward) {
        // 黑流的支援页按策略顺序选，由黑流生命周期插件处理
        return m_config->get_theme() != RoguelikeTheme::BlackFlow;
    }
    if (m_waiting_to_run == RoguelikeCustomType::Squad) {
        if (m_config->get_run_for_collectible()) { // 烧水分队
        }
        else {                                     // 开局分队
        }
        return true;
    }
    if (m_config->get_mode() == RoguelikeMode::BlackFlowStartRun && !m_config->get_run_for_collectible() &&
        !m_config->get_blackflow_start_utopia()) {
        // 目标局在选择支援页由黑流生命周期插件判断后就结束，不选职业组和开局干员；
        // 凹第一层实托邦时目标局要开局进第一层，照常选。
        return false;
    }
    // Roles
    return !m_config->get_start_roles().empty();
}

bool asst::RoguelikeCustomStartTaskPlugin::load_params(const json::value& params)
{
    m_squad = params.get("squad", "");
    if (m_config->get_mode() == RoguelikeMode::Collectible ||
        m_config->get_mode() == RoguelikeMode::BlackFlowStartRun) {
        m_collectible_mode_squad = params.get("collectible_mode_squad", m_squad);
    }

    m_config->set_start_opers(RoguelikeConfig::parse_start_opers(params));           // 开局干员顺位列表
    m_config->set_start_roles(params.get("roles", ""));                              // 开局职业组
    m_config->set_use_nonfriend_support(params.get("use_nonfriend_support", false)); // 是否可以是非好友助战干员

    if (auto select_list = params.find<json::object>("collectible_mode_start_list"); select_list) {
        RoguelikeStartSelect list;
        list.hot_water = select_list->get("hot_water", false);
        list.shield = select_list->get("shield", false);
        list.ingot = select_list->get("ingot", false);
        list.hope = select_list->get("hope", false);
        list.random = select_list->get("random", false);
        if (m_config->get_theme() == RoguelikeTheme::Mizuki) {
            list.key = select_list->get("key", false);
            list.dice = select_list->get("dice", false);
        }
        else if (m_config->get_theme() == RoguelikeTheme::Sarkaz) {
            list.ideas = select_list->get("ideas", false);
        }
        else if (m_config->get_theme() == RoguelikeTheme::JieGarden) {
            list.ticket = select_list->get("ticket", false);
        }
        m_start_select = list;
    }

    return true;
}

bool asst::RoguelikeCustomStartTaskPlugin::_run()
{
    const std::unordered_map<RoguelikeCustomType, std::function<bool(void)>> TypeActuator = {
        { RoguelikeCustomType::Squad, std::bind(&RoguelikeCustomStartTaskPlugin::hijack_squad, this) },
        { RoguelikeCustomType::Reward, std::bind(&RoguelikeCustomStartTaskPlugin::hijack_reward, this) },
        { RoguelikeCustomType::Roles, std::bind(&RoguelikeCustomStartTaskPlugin::hijack_roles, this) },
        { RoguelikeCustomType::Voucher, std::bind(&RoguelikeCustomStartTaskPlugin::select_initial_voucher, this) },
    };

    auto it = TypeActuator.find(m_waiting_to_run);
    if (it == TypeActuator.cend()) {
        return false;
    }

    return it->second();
}

bool asst::RoguelikeCustomStartTaskPlugin::select_initial_voucher()
{
    skip_incompatible_start_opers(m_config->get_start_roles());
    const auto image = ctrler()->get_image();

    // RecruitMain is also used as a passive marker on the operator-list page
    // immediately after the opening voucher is chosen.  Do not send that page
    // back through the voucher OCR loop: the regular ChooseOper plugin must
    // select the requested operator (for example, EW) from the visible list.
    {
        RoguelikeRecruitImageAnalyzer operator_analyzer(image);
        if (operator_analyzer.analyze() && operator_analyzer.get_result().size() >= 2) {
            LogInfo << __FUNCTION__ << "operator list detected; leave page to outer ChooseOper task";
            return true;
        }
    }

    if (const auto* requested = m_config->get_current_start_oper()) {
        static const std::unordered_map<battle::Role, std::string> RoleNames = {
            { battle::Role::Pioneer, "先锋" }, { battle::Role::Warrior, "近卫" }, { battle::Role::Tank, "重装" },
            { battle::Role::Sniper, "狙击" },  { battle::Role::Caster, "术师" },  { battle::Role::Medic, "医疗" },
            { battle::Role::Support, "辅助" }, { battle::Role::Special, "特种" },
        };
        const auto role = BattleData.get_first_role(requested->name);
        if (auto name = RoleNames.find(role); name != RoleNames.end()) {
            OCRer analyzer(image);
            analyzer.set_task_info("RoguelikeCustom-HijackRoles");
            analyzer.set_roi({ 0, 100, 1280, 550 });
            analyzer.set_required({ name->second });
            if (!analyzer.analyze()) {
                LogWarn << __FUNCTION__ << "Requested opening voucher not recognized" << requested->name;
                return false;
            }
            LogInfo << __FUNCTION__ << "Choose requested opening voucher" << requested->name << name->second;
            ctrler()->click(analyzer.get_result().front().rect);
            return true;
        }
    }
    Matcher analyzer(image);
    analyzer.set_task_info(m_config->get_theme() + "@Roguelike@RecruitMain");
    if (const auto result = analyzer.analyze()) {
        ctrler()->click(result->rect);
        return true;
    }
    return false;
}

bool asst::RoguelikeCustomStartTaskPlugin::hijack_squad()
{
    std::string squad = !m_config->get_run_for_collectible() ? m_squad : m_collectible_mode_squad;
    if (squad.empty()) {
        // 未显式选择分队时，优先使用资源 opening.default_squad 声明的分队，
        // 而不是直接走 SquadDefault / Squad-Random 随机。哪个分队作为默认由资源决定。
        std::string default_squad = RoguelikeRecruit.get_default_squad(m_config->get_theme());
        if (default_squad.empty()) {
            // 资源没有声明默认分队，保持原有行为
            return ProcessTask(
                       *this,
                       { m_config->get_theme() + "@Roguelike@SquadDefault",
                         m_config->get_theme() + "@Roguelike@Squad-Random" })
                .run();
        }
        Log.info(__FUNCTION__, "| no squad configured, use resource default_squad:", default_squad);
        squad = std::move(default_squad);
    }

    constexpr size_t SwipeTimes = 7;
    const auto reset_squad_scroll = [&]() {
        // One reverse swipe only moves one card-width.  After scanning a
        // locked squad the list may be several pages to the right, so reset
        // all the way before trying the default squad.
        for (size_t i = 0; i != SwipeTimes; ++i) {
            if (need_exit()) {
                return;
            }
            ProcessTask(*this, { "SwipeToTheLeft" }).run();
        }
    };
    // 按名字在分队列表里找一次；找不到就滑回最左，留给调用方决定要不要换名字再来。
    const auto select_squad = [&](const std::string& name) -> bool {
        for (size_t i = 0; i != SwipeTimes; ++i) {
            if (need_exit()) {
                return false;
            }
            auto image = ctrler()->get_image();
            RoguelikeSquadFrameOCRer capture(image);
            capture.set_task_info("RoguelikeCustom-HijackSquad");
            capture.set_required({});
            capture.set_replace({});
            const auto raw_results = capture.analyze();
            RoguelikeSquadFrameOCRer analyzer(image);
            analyzer.set_task_info("RoguelikeCustom-HijackSquad");
            // The first card is often clipped after the horizontal list is
            // reset.  OCR then returns only the visible tail (for example
            // "勤分队") instead of the full squad name.  Accept those
            // stable aliases only for the built-in default squad.
            if (name == "特勤分队") {
                analyzer.set_required({ name, "勤分队", "特勤" });
            }
            else {
                analyzer.set_required({ name });
            }

            const auto names = raw_results ? analyzer.filter_same_frame(capture, *raw_results) : std::nullopt;
            if (!names) {
                ProcessTask(*this, { "Roguelike@SquadSlowlySwipeToTheRight" }).run();
                sleep(Task.get("RoguelikeCustom-HijackSquad")->post_delay);
                continue;
            }
            const auto& rect = names->front().rect;
            // A locked squad still exposes its name in the card.  Check the same
            // row for the unlock label before clicking, otherwise the game keeps
            // the previous/default squad while MAA records the locked name.
            RoguelikeSquadFrameOCRer locked_analyzer(image);
            locked_analyzer.set_task_info("RoguelikeCustom-HijackSquadLocked");
            RoguelikeSquadFrameOCRer locked_ocr_analyzer(image);
            locked_ocr_analyzer.set_task_info("RoguelikeCustom-HijackSquadLockedOCR");
            const auto is_same_card = [&](const auto& locked) {
                return std::abs(locked.rect.y - rect.y) < 90 && std::abs(locked.rect.x - rect.x) < 360;
            };
            const auto normal_locks = locked_analyzer.filter_same_frame(capture, *raw_results);
            const auto raw_locks = locked_ocr_analyzer.filter_same_frame(capture, *raw_results);
            const bool locked_by_normal_ocr = normal_locks && std::ranges::any_of(*normal_locks, is_same_card);
            const bool locked_by_raw_ocr = raw_locks && std::ranges::any_of(*raw_locks, is_same_card);
            if (locked_by_normal_ocr || locked_by_raw_ocr) {
                LogWarn << __FUNCTION__ << "configured squad is locked" << name;
                reset_squad_scroll();
                return false;
            }
            ctrler()->click(rect);

            m_config->set_squad(name);
            return true;
        }
        reset_squad_scroll();
        return false;
    };

    if (select_squad(squad)) {
        Log.info(__FUNCTION__, "| squad selected:", squad);
        return true;
    }

    // 配置的分队在本主题里找不到（例如沿用了其它主题的分队名）。此时若直接放弃，
    // 分队会由游戏默认值决定，开局职业组合与策略预期不符；回退到资源声明的默认分队。
    const std::string fallback = RoguelikeRecruit.get_default_squad(m_config->get_theme());
    if (fallback.empty() || fallback == squad) {
        return false;
    }
    Log.info(__FUNCTION__, "| configured squad not found, fallback to default_squad:", fallback);
    return select_squad(fallback);
}

bool asst::RoguelikeCustomStartTaskPlugin::hijack_reward()
{
    const auto& list = get_select_list();
    if (list.empty()) {
        // 执行默认选择顺序
        ProcessTask(*this, { m_config->get_theme() + "@Roguelike@LastReward-Strategy" }).run();
        return true;
    }

    // 处理选择顺序
    PipelineAnalyzer analyzer(ctrler()->get_image());
    analyzer.set_tasks(list);
    if (auto ret = analyzer.analyze(); !ret) {
        // 未获取到期望物品，设置烧水flag，重开
        m_config->set_run_for_collectible(true);
        m_control_ptr->exit_then_stop(true);
    }
    else if (m_config->get_start_with_elite_two() || m_config->get_first_floor_foldartal()) {
        // 之后还要凹开局精二或第一层密文板，不停止任务，继续探索
        ctrler()->click(ret->rect);
        sleep(Config.get_options().task_delay);
    }
    else {
        m_control_ptr->exit_then_stop(false);
        m_task_ptr->set_enable(false);
    }

    return true;
}

bool asst::RoguelikeCustomStartTaskPlugin::hijack_roles()
{
    constexpr size_t SwipeTimes = 7;
    const std::string required_role = resolve_start_roles();
    m_config->set_start_roles(required_role);
    skip_incompatible_start_opers(required_role);

    for (size_t i = 0; i != SwipeTimes; ++i) {
        if (need_exit()) {
            return false;
        }

        auto image = ctrler()->get_image();
        OCRer analyzer(image);
        analyzer.set_task_info("RoguelikeCustom-HijackRoles");
        analyzer.set_required({ required_role });

        if (analyzer.analyze()) {
            const auto& rect = analyzer.get_result().front().rect;
            ctrler()->click(rect);
            return true;
        }

        ProcessTask(*this, { "Roguelike@SquadSlowlySwipeToTheRight" }).run();
        sleep(Task.get("RoguelikeCustom-HijackRoles")->post_delay);
    }

    ProcessTask(*this, { "SwipeToTheLeft" }).run();

    // 兜底：指定干员对应的券没刷到时，不跳过初始招募，而是按「可靠职业优先」从当前券里挑一张。
    // 顺序依据：先保证站得住（近卫/重装），再保证打得动（狙击/术师），医疗次之，其余职业最后。
    static const std::array<std::string, 8> fallback_order = { "近卫", "重装", "狙击", "术师",
                                                               "医疗", "先锋", "辅助", "特种" };
    LogWarn << __FUNCTION__ << "requested opening role" << required_role
            << "not found on voucher page; fallback to a reliable role voucher instead of skipping";

    auto image = ctrler()->get_image();
    OCRer analyzer(image);
    analyzer.set_task_info("RoguelikeCustom-HijackRoles");
    if (analyzer.analyze()) {
        const auto& results = analyzer.get_result();
        for (const auto& role_name : fallback_order) {
            for (const auto& res : results) {
                if (res.text.find(role_name) == std::string::npos) {
                    continue;
                }
                LogInfo << __FUNCTION__ << "fallback pick" << role_name << "voucher at" << res.rect.x << res.rect.y;
                ctrler()->click(res.rect);
                return true;
            }
        }
    }

    LogError << __FUNCTION__ << "no usable role voucher found on the opening page";
    return false;
}

std::string asst::RoguelikeCustomStartTaskPlugin::resolve_start_roles() const
{
    const std::string configured = m_config->get_start_roles();
    if (!configured.empty()) {
        if (const RoguelikeStartOper* start_oper = m_config->get_current_start_oper(); start_oper != nullptr) {
            const battle::Role role = BattleData.get_first_role(start_oper->name);
            if (!role_group_contains(configured, role)) {
                LogInfo << __FUNCTION__ << "opening roles" << configured << "do not contain" << start_oper->name
                        << "; treat the requested start oper as unselected";
            }
        }
        return configured;
    }

    if (const RoguelikeStartOper* start_oper = m_config->get_current_start_oper(); start_oper != nullptr) {
        const battle::Role role = BattleData.get_first_role(start_oper->name);
        LogInfo << __FUNCTION__ << "no opening roles configured for" << start_oper->name << "role"
                << static_cast<int>(role);
    }

    const std::string recommended = RoguelikeRecruit.get_opening_roles(m_config->get_theme(), m_config->get_squad());
    if (!recommended.empty()) {
        LogInfo << __FUNCTION__ << "use resource opening roles" << recommended << "for squad" << m_config->get_squad();
        return recommended;
    }
    return configured;
}

bool asst::RoguelikeCustomStartTaskPlugin::role_group_contains(std::string_view roles, battle::Role role)
{
    if (roles == "先手必胜") {
        return role == battle::Role::Pioneer || role == battle::Role::Sniper || role == battle::Role::Special;
    }
    if (roles == "稳扎稳打") {
        return role == battle::Role::Tank || role == battle::Role::Caster || role == battle::Role::Sniper;
    }
    if (roles == "取长补短") {
        return role == battle::Role::Warrior || role == battle::Role::Support || role == battle::Role::Medic;
    }
    return false;
}

void asst::RoguelikeCustomStartTaskPlugin::skip_incompatible_start_opers(const std::string& roles)
{
    while (const RoguelikeStartOper* start_oper = m_config->get_current_start_oper()) {
        const battle::Role role = BattleData.get_first_role(start_oper->name);
        if (role_group_contains(roles, role)) {
            return;
        }
        LogInfo << __FUNCTION__ << "skip incompatible requested start oper" << start_oper->name << "for roles" << roles;
        m_config->advance_start_oper_index();
    }
}

std::vector<std::string> asst::RoguelikeCustomStartTaskPlugin::get_select_list() const
{
    if (m_config->get_mode() != RoguelikeMode::Collectible ||
        m_config->get_run_for_collectible() /* 正在烧水，使用默认策略 */ ||
        m_config->get_only_start_with_elite_two() /* 只凹精二没有奖励，但第一次开时可能有之前的奖励 */) {
        return {};
    }

    std::vector<std::string> list;
    if (m_start_select.hot_water) {
        list.emplace_back(m_config->get_theme() + "@Roguelike@LastReward"); // 热水壶
    }
    if (m_start_select.shield) {
        list.emplace_back(m_config->get_theme() + "@Roguelike@LastReward2"); // 盾；傀影没盾，是生命
    }
    if (m_start_select.ingot) {
        list.emplace_back(m_config->get_theme() + "@Roguelike@LastReward3"); // 源石锭
    }
    if (m_start_select.hope) {
        list.emplace_back(m_config->get_theme() + "@Roguelike@LastReward4"); // 希望
    }

    if (m_start_select.random) {
        list.emplace_back(m_config->get_theme() + "@Roguelike@LastRewardRand"); // 随机奖励
    }
    if (m_config->get_theme() == RoguelikeTheme::Mizuki) {
        if (m_start_select.key) {
            list.emplace_back("Mizuki@Roguelike@LastReward5"); // 钥匙
        }
        if (m_start_select.dice) {
            list.emplace_back("Mizuki@Roguelike@LastReward6"); // 骰子
        }
    }
    else if (m_config->get_theme() == RoguelikeTheme::Sarkaz && m_start_select.ideas) {
        list.emplace_back("Sarkaz@Roguelike@LastReward5"); // 构想
    }
    else if (m_config->get_theme() == RoguelikeTheme::JieGarden && m_start_select.ticket) {
        list.emplace_back("JieGarden@Roguelike@LastReward5"); // 票券
    }

    return list;
}
