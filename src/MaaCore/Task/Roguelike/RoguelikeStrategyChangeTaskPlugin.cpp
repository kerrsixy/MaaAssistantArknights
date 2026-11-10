#include "RoguelikeStrategyChangeTaskPlugin.h"

#include <cstdlib>
#include <map>

#include "Controller/Controller.h"
#include "Vision/RegionOCRer.h"
#include "Vision/OCRer.h"

#include "Config/TaskData.h"
#include "Utils/Logger.hpp"

bool asst::RoguelikeStrategyChangeTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    if ((msg != AsstMsg::SubTaskStart && msg != AsstMsg::SubTaskCompleted) || details.get("subtask", std::string()) != "ProcessTask") {
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
    if (task_view == "Roguelike@StrategyChange") {
        // 2026-10-28 修正：ProcessTask::run_task() 在匹配成功后会先构造
        // info["details"]["result"] = hits.reco_detail，再同时用于 SubTaskStart 与
        // SubTaskCompleted 两次回调。所以两个阶段都有可能拿到 OCR 文本，
        // 之前只在 SubTaskCompleted 取值，一旦该阶段 result 为空就永远切换不了策略。
        // 现在改成「哪个阶段拿到非空 result 就用哪个」，并把收到的内容打进日志便于核对。
        json::value result = details.get("details", "result", json::object());
        Log.info(
            __FUNCTION__, "| StrategyChange", msg == AsstMsg::SubTaskStart ? "start" : "completed",
            "result:", result.to_string());
        if (!result.is_object() || result.as_object().empty()) {
            return false;
        }
        // 2026-10-29 阶段C：回调去重。同一个 ProcessTask 命中会先后发 SubTaskStart 与
        // SubTaskCompleted 两次，两次的 result 内容相同。只在 Start 写一次，
        // 避免同一层被重复消费（重复执行 set_task_base 会让日志出现两次切换记录）。
        if (msg == AsstMsg::SubTaskStart) {
            m_strategy = result.as_object().get("text", "");
            Log.info(__FUNCTION__, "| strategy captured:", m_strategy);
            return true;
        }
        // Completed：内容与 Start 相同，不重复写状态，也不触发 _run()
        return false;
    }
    // 地图阶段：每经过一个节点都会跑 Mizuki@Roguelike@Stages，借用它读一次「目标生命值」。
    // 这里刻意把 m_result 置空，让 _run() 只做血量采样、跳过策略切换逻辑。
    // 地图阶段：实测 `Roguelike@Stages` 是聚合父任务、不发 SubTaskStart，
    // 真正发事件的是命中的子模板（StageCombatOps / StageEmergencyOps / StageBoskyPassage 等），
    // 故用前缀匹配；每经过一个地图节点都会读一次「目标生命值」。
    if (task_view.starts_with("Roguelike@Stage") && task_view != "Roguelike@Stages" && !task_view.starts_with("Roguelike@Stages_")) {
        // 2026-10-29 阶段C：只登记「本帧可采血量」，不再清空任何与策略相关的状态。
        m_observe_hp = true;
        // 只把「选中战斗节点」的那一帧标记为可采样：Enter/Confirm 之后的画面已经不是地图，
        // 左上角显示的可能是别的 UI，采信会污染血量状态。
        m_on_battle_node = !task_view.ends_with("Enter") && !task_view.ends_with("Confirm") &&
                           (task_view.ends_with("Ops") || task_view.ends_with("Passage") ||
                            task_view.ends_with("Boss") || task_view.ends_with("Duel"));
        return true;
    }
    else {
        return false;
    }
}

bool asst::RoguelikeStrategyChangeTaskPlugin::_run()
{
    LogTraceFunction;

    if (m_on_battle_node && m_config->get_theme() == RoguelikeTheme::Phantom) {
        const auto map_image = ctrler()->get_image();
        OCRer effects(map_image);
        effects.set_task_info("Phantom@Roguelike@FloorEffectsOcr");
        if (effects.analyze()) {
            static const std::array<std::string, 6> FloorNames = {
                "迷雾歧途", "落阳庭院", "故土残躯", "噩梦之始", "血月长廊", "渴欲大厅",
            };
            std::string floor_name;
            std::vector<std::string> labels;
            for (const auto& item : effects.get_result()) {
                if (item.score < 0.9) {
                    continue;
                }
                if (std::ranges::find(FloorNames, item.text) != FloorNames.end()) {
                    floor_name = item.text;
                }
                else {
                    labels.emplace_back(item.text);
                }
            }
            if (!floor_name.empty()) {
                auto& status = m_config->status();
                status.phantom_observed_floor_name = std::move(floor_name);
                status.phantom_floor_effect_observation = std::move(labels);
                LogInfo << __FUNCTION__ << "Phantom floor effect observation" << status.phantom_observed_floor_name
                        << status.phantom_floor_effect_observation;
            }
        }
        OCRer playbill(map_image);
        playbill.set_task_info("Phantom@Roguelike@CurrentPlaybillOcr");
        if (playbill.analyze()) {
            const auto& result = playbill.get_result();
            const bool has_label = std::ranges::any_of(result, [](const auto& item) {
                return item.text.find("当前剧目") != std::string::npos;
            });
            if (has_label) {
                std::vector<std::string> observed;
                for (const auto& item : result) {
                    if (item.text.find("当前剧目") == std::string::npos && !item.text.empty()) {
                        observed.emplace_back(item.text);
                    }
                }
                if (!observed.empty()) {
                    m_config->status().phantom_playbill_observation = std::move(observed);
                    LogInfo << __FUNCTION__ << "Phantom current playbill observation"
                            << m_config->status().phantom_playbill_observation;
                }
            }
        }
    }

    // 地图界面读取「目标生命值」并统计本次探索的掉血，供后续判断关卡风险。
    // 多 roi + 多 bin 采样后投票：实测同一时刻不同参数会给出 3 / 7 / 33 这种互相矛盾的值，
    // 单次取值不可信；这里取出现次数最多的值，平票时取与上一次读数最接近的，避免噪声写进状态。
    if (m_observe_hp && m_on_battle_node) {
        const cv::Mat image = ctrler()->get_image();
        static const std::vector<Rect> HpRois = {
            Rect { 180, 22, 90, 44 },
            Rect { 150, 15, 190, 50 },
            Rect { 140, 8, 220, 64 },
            Rect { 120, 0, 260, 80 },
        };
        static const std::vector<int> HpBins = { 1, 2, 3 };
        std::map<int, int> votes;
        for (const Rect& roi : HpRois) {
            for (int bin : HpBins) {
                RegionOCRer hp_ocr(image);
                hp_ocr.set_task_info("Mizuki@Roguelike@HpRecognition");
                hp_ocr.set_roi(roi);
                hp_ocr.set_bin_expansion(bin);
                if (!hp_ocr.analyze()) {
                    continue;
                }
                // 「6/9」这种形式只取正斜杠前的当前值
                std::string digits;
                bool started = false;
                for (char c : hp_ocr.get_result().text) {
                    if (std::isdigit(static_cast<unsigned char>(c))) {
                        started = true;
                        digits.push_back(c);
                    }
                    else if (started) {
                        break;
                    }
                }
                if (digits.empty() || digits.size() > 2) {
                    continue;
                }
                const int value = std::stoi(digits);
                if (value > 20) {
                    continue; // 目标生命值上限不会超过 20，超出即误识别
                }
                ++votes[value];
            }
        }
        if (!votes.empty()) {

            const int hp_old = m_config->status().hp;
            int best_value = votes.begin()->first;
            int best_count = 0;
            for (const auto& [value, count] : votes) {
                const bool better =
                    count > best_count ||
                    (count == best_count && hp_old >= 0 && std::abs(value - hp_old) < std::abs(best_value - hp_old));
                if (better) {
                    best_value = value;
                    best_count = count;
                }
            }
            // 单票且跳变过大时大概率是误识别，丢弃这一帧
            if (best_count < 2 && hp_old >= 0 && std::abs(best_value - hp_old) > 6) {
                Log.info("Roguelike HP dropped noisy sample:", best_value, "prev:", hp_old, "votes:", best_count);
            }
            else {
            if (best_value != hp_old) {
                Log.info("Roguelike HP changed:", hp_old, "->", best_value, "loss:", hp_old - best_value);
            }
        if (best_value != m_pending_hp) {
            m_pending_hp = best_value;
            Log.info("Roguelike HP pending confirm:", best_value);
            m_observe_hp = false;
            return true;
        }

            m_config->status().hp = best_value;
            Log.info("Roguelike HP sampled:", best_value, "votes:", best_count, "distinct:", votes.size());
            }
        }
    }

    m_observe_hp = false;

    const std::string theme = m_config->get_theme();
    const std::string stages_task_name = theme + "@Roguelike@Stages";
    const std::string current_strategy = m_strategy;
    if (current_strategy.empty() || current_strategy.find("_SKIP_") != std::string::npos) {
        Log.info("Skip strategy change, current strategy is", current_strategy);
        return true;
    }
    const std::string strategy_task_name = stages_task_name + current_strategy;

    if (Task.get(strategy_task_name) == nullptr) [[unlikely]] {
        Log.error("Strategy task", strategy_task_name, "doesn't exist!");
        return false;
    }

    // 2026-10-29 阶段C：结构化决策日志。run_id + decision_seq 组成 decision_id，
    // 配合 strategy/floor 两个输入，可从终局日志反查「当时为什么切到这个策略」。
    const std::string decision_id =
        m_config->status().run_id + "-d" + std::to_string(++m_config->status().decision_seq);
    LogInfo << "[roguelike-decision] id=" << decision_id << " kind=strategy_change"
            << " input.strategy=" << current_strategy << " target=" << strategy_task_name;

    Task.set_task_base(stages_task_name, strategy_task_name);
    // 2026-10-29 阶段C：这一次策略已经落到 task 上，清掉待消费值。
    // 否则后续任何一次 _run()（比如血量采样帧）都会拿同一个策略名重复 set_task_base。
    m_strategy.clear();
    // 切换成功必须有日志，否则只能靠「没打 Skip」反推，排查时无法确认策略真的落到了哪个后缀。
    Log.info(__FUNCTION__, "| current strategy is", current_strategy, "applied to", stages_task_name);

    return true;
}
