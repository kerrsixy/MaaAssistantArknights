#include "OperBoxRecognitionTask.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <future>
#include <ranges>
#include <thread>

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "Task/ProcessTask.h"
#include "Utils/Logger.hpp"
#include "Vision/Oper/OperBoxImageAnalyzer.h"
#include "Vision/TemplDetOCRer.h"

bool asst::OperBoxRecognitionTask::_run()
{
    LogTraceFunction;

    bool ret = swipe_and_analyze();
    callback_analyze_result(true);
    return ret;
}

bool asst::OperBoxRecognitionTask::swipe_and_analyze()
{
    LogTraceFunction;
    std::string pre_pre_last_oper;
    std::size_t pre_pre_count = 0;
    std::string pre_last_oper;
    std::string pre_first_oper;
    int reswipe = 0;
    m_own_opers.clear();
    m_repeated_page_stop = false;

    // 游戏会记住上次的滚动位置，先把它拖回列表顶端，否则会从中间开始扫、少读一批干员
    // 回顶：每滑一次就看一眼首张卡的名字，连续两次没变化说明已经到头了，立即停，
    // 不要固定滑满 25 次（用户反馈「翻到头了还在滑」）。
    std::string head_oper;
    for (int i = 0; i < 25; ++i) {
        if (need_exit()) {
            return false;
        }
        if (m_page_guard && !m_page_guard()) {
            LogWarn << __FUNCTION__ << "Operator list navigation guard failed at head";
            return false;
        }
        OperBoxImageAnalyzer head_analyzer(ctrler()->get_image());
        if (head_analyzer.analyze()) {
            const auto& head_result = head_analyzer.get_result();
            if (!head_result.empty()) {
                const std::string& first_oper = head_result.front().name;
                if (!head_oper.empty() && first_oper == head_oper) {
                    LogInfo << __FUNCTION__ << "list head reached at page" << i << "first" << first_oper;
                    break;
                }
                head_oper = first_oper;
            }
        }
        ProcessTask(*this, { "OperBoxSlowlySwipeToTheLeft" }).run();
    }
    LogInfo << __FUNCTION__ << "back to the list head";

    bool first_page = true;
    while (!need_exit()) {
        // 先滑、再等画面稳定、最后分析：慢拖要 2 秒，边滑边分析会拿到拖动中的旧帧，
        // 连续几页读到同一张卡就会被误判成「到底了」，导致漏读大半个列表。
        if (!first_page) {
            swipe_page();
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
        }
        first_page = false;
        if (m_page_guard && !m_page_guard()) {
            LogWarn << __FUNCTION__ << "Operator list navigation guard failed before page analysis";
            return false;
        }

        // 同页多采：单帧常常只认出 6~13 张卡（边界卡、半张卡容易漏），
        // 连采两帧再把结果并起来，能把单页召回率拉满。
        std::vector<OperBoxInfo> opers_result;
        std::string page_last_oper;
        for (int shot = 0; shot < 2; ++shot) {
            if (shot > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
            OperBoxImageAnalyzer analyzer(ctrler()->get_image());
            if (!analyzer.analyze()) {
                continue;
            }
            const auto& one_shot = analyzer.get_result();
            if (one_shot.empty()) {
                continue;
            }
            if (page_last_oper != one_shot.back().name) {
                page_last_oper = one_shot.back().name;
            }
            for (const auto& box_info : one_shot) {
                const bool seen = std::any_of(opers_result.begin(), opers_result.end(), [&](const auto& item) {
                    return item.name == box_info.name;
                });
                if (!seen) {
                    opers_result.emplace_back(box_info);
                }
            }
        }
        if (opers_result.empty()) {
            break;
        }

        const std::string& last_oper = opers_result.back().name;
        const std::string& first_oper = opers_result.front().name;

        // 位移校验：慢拖偶尔会因为惯性/未吃住手势而原地不动，
        // 这时同页会被重复读三次，触发「到底」误判、少读一大段。
        // 做法是看首张卡有没有变化，没变化就重滑一次（最多两次），再不动才认到底。
        if (!pre_first_oper.empty() && first_oper == pre_first_oper) {
            if (reswipe < 2) {
                ++reswipe;
                LogInfo << __FUNCTION__ << "swipe did not move, retry" << reswipe << "first" << first_oper;
                swipe_page();
                std::this_thread::sleep_for(std::chrono::milliseconds(700));
                continue;
            }
        }
        reswipe = 0;
        pre_first_oper = first_oper;
        // 只在「连续几页都没有新干员」时才认为到底：单页偶尔滑不动不该结束扫描，
        // 否则会提前停在列表中段（实测会少读一半以上）。
        const std::size_t before_count = m_own_opers.size();
        if (last_oper == pre_last_oper && pre_last_oper == pre_pre_last_oper && before_count == pre_pre_count) {
            m_repeated_page_stop = true;
            LogInfo << __FUNCTION__ << "stop at the list tail" << last_oper << "unique" << before_count;
            break;
        }
        pre_pre_count = before_count;
        pre_pre_last_oper = pre_last_oper;
        pre_last_oper = last_oper;

        for (const auto& box_info : opers_result) {
            m_own_opers.emplace(box_info.name, box_info);
        }
        LogInfo << __FUNCTION__ << "page_last" << last_oper << "unique" << m_own_opers.size();
        if (m_page_visitor && !m_page_visitor(opers_result)) {
            LogWarn << __FUNCTION__ << "Page observer could not restore the verified operator list";
            return false;
        }
        callback_analyze_result(false);
        if (m_stop_after_page && m_stop_after_page()) {
            return true;
        }

        // 调试用：设了 MAA_OPERBOX_MAX_PAGES 就只扫这么多页，方便快速验证识别率
        char max_pages_buf[16] {};
        std::size_t max_pages_len = 0;
        if (getenv_s(&max_pages_len, max_pages_buf, sizeof(max_pages_buf), "MAA_OPERBOX_MAX_PAGES") == 0 &&
            max_pages_len > 0) {
            static int page_no = 0;
            if (++page_no >= std::atoi(max_pages_buf)) {
                LogInfo << __FUNCTION__ << "debug stop after pages" << page_no;
                break;
            }
        }
    }
    return !m_own_opers.empty();
}

void asst::OperBoxRecognitionTask::swipe_page()
{
    ProcessTask(*this, { "OperBoxSlowlySwipeToTheRight" }).run();
}

void asst::OperBoxRecognitionTask::callback_analyze_result(bool done)
{
    LogTraceFunction;
    // 获取所有干员名
    const auto& all_chars = BattleData.get_all_chars();

    json::value info = basic_info_with_what("OperBoxInfo");
    auto& details = info["details"];
    details["done"] = done;
    auto& all_opers = details["all_opers"].as_array();
    auto& own_opers = details["own_opers"].as_array();

    for (const auto& chars : all_chars | std::ranges::views::filter([](const auto& pair) {
                                 return pair.second->role != battle::Role::Drone;
                             })) {
        const auto& props = chars.second;
        bool own = m_own_opers.contains(props->name);
        all_opers.emplace_back(
            json::object {
                { "id", props ? props->id : "" },
                { "name", props->name },
                { "name_en", props ? props->name_en : "" },
                { "name_jp", props ? props->name_jp : "" },
                { "name_kr", props ? props->name_kr : "" },
                { "name_tw", props ? props->name_tw : "" },
                { "rarity", props ? props->rarity : 0 },
                { "own", own }, // 在m_own_opers中重复
            });
    }
    for (const auto& [name, box_info] : m_own_opers) {
        own_opers.emplace_back(
            json::object {
                { "id", box_info.id },
                { "name", box_info.name },
                { "own", box_info.own },
                { "elite", box_info.elite },
                { "level", box_info.level },
                { "potential", box_info.potential },
                { "rarity", box_info.rarity },
            });
    }

    callback(AsstMsg::SubTaskExtraInfo, info);
}
