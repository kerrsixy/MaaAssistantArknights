#include "SPAObservation.h"
#include "SPACombatStats.h"
#include "SPADifficultyPolicy.h"
#include <algorithm>
#include <cmath>
#include <regex>

bool asst::spa::is_round_badge_position(int x, int y)
{
    return x >= 480 && x < 530 && y >= 0 && y < 60;
}

bool asst::spa::is_hidden_round_badge(const TextObservation& text)
{
    if (!std::isfinite(text.score) || text.score < .80 || !is_round_badge_position(text.x, text.y)) {
        return false;
    }
    auto label = text.text;
    std::erase(label, ' ');
    return label == "??" || label == "？？";
}

std::optional<asst::spa::HiddenRoundMode> asst::spa::observe_hidden_round(const std::vector<TextObservation>& texts)
{
    if (!std::any_of(texts.begin(), texts.end(), is_hidden_round_badge)) {
        return std::nullopt;
    }
    bool round_label = false;
    std::optional<std::string> difficulty;
    for (const auto& item : texts) {
        if (!std::isfinite(item.score) || item.score < .85 || item.y < 0 || item.y >= 65) {
            continue;
        }
        round_label = round_label || (item.x >= 410 && item.x < 480 && item.text == "回合");
        if (item.x < 90 || item.x >= 250) {
            continue;
        }
        auto label = item.text;
        std::erase(label, ' ');
        if (!label.starts_with("规程等级") && label != "极速模拟" && !joint_difficulty_from_label(label)) {
            continue;
        }
        if (difficulty && *difficulty != label) {
            return std::nullopt;
        }
        difficulty = std::move(label);
    }
    if (!round_label || !difficulty) {
        return std::nullopt;
    }
    if (*difficulty == "险境模拟") {
        return HiddenRoundMode::JointRisk;
    }
    static const std::regex eligible_regulation("规程等级(?:[5-9]|1[0-9]|20)");
    if (std::regex_match(*difficulty, eligible_regulation)) {
        return HiddenRoundMode::Independent;
    }
    return std::nullopt;
}

bool asst::spa::verified_pact_setup_page(const std::vector<TextObservation>& texts)
{
    bool stage_one = false, confirmation = false, ready = false;
    bool core_heading = false, enemy_heading = false, banned_heading = false, pact_instruction = false;
    for (const auto& item : texts) {
        if (!std::isfinite(item.score) || item.score < .85 || item.x < 0 || item.x >= 1280 || item.y < 0 ||
            item.y >= 720) {
            continue;
        }
        auto text = item.text;
        std::erase_if(text, [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; });
        if (item.x >= 390 && item.x < 920 && item.y < 90) {
            stage_one = stage_one || text.find("1/3") != std::string::npos;
            confirmation = confirmation || text.find("确认本局信息") != std::string::npos;
        }
        ready = ready || (text == "准备就绪" && item.x >= 950 && item.y >= 590 && item.y < 700);
        core_heading = core_heading || (text == "核心盟约" && item.x < 300 && item.y >= 100 && item.y < 220);
        enemy_heading = enemy_heading || (text == "敌方领袖" && item.x >= 950 && item.y >= 310 && item.y < 390);
        banned_heading = banned_heading || (text == "本局禁用干员" && item.x < 300 && item.y >= 100 && item.y < 700);
        // The actual scrolled frame splits “请选择一” from this suffix. Do
        // not concatenate unrelated OCR boxes or require an intact sentence.
        pact_instruction = pact_instruction || (text.find("本次模拟中激活的盟约") != std::string::npos &&
                                                item.x >= 100 && item.x < 600 && item.y >= 100 && item.y < 220);
    }
    return stage_one && confirmation && ready &&
           (core_heading || (enemy_heading && (pact_instruction || banned_heading)));
}

bool asst::spa::verified_loading_page(const std::vector<TextObservation>& texts)
{
    bool operation = false, loading = false, joint_stage = false, difficulty = false;
    bool solo_stage = false, regulation = false, quick_stage = false, quick_mode = false;
    bool protocol_start = false, protocol_start_footer = false;
    bool search_success = false, connecting = false, team_ready = false, protocol_footer = false;
    bool objective_header = false, defeat_leader = false;
    int player_names = 0;
    for (const auto& item : texts) {
        if (!std::isfinite(item.score) || item.score < .9) {
            continue;
        }
        objective_header = objective_header || (item.text == "全局作战目标" && item.x >= 300 && item.x < 650 &&
                                                item.y >= 285 && item.y < 335);
        defeat_leader = defeat_leader || ((item.text == "消灭敌方领袖！" || item.text == "消灭敌方领袖!") &&
                                          item.x >= 300 && item.x < 650 && item.y >= 335 && item.y < 385);
        operation =
            operation || (item.text == "OPERATION" && item.x >= 400 && item.x < 800 && item.y >= 230 && item.y < 350);
        loading = loading || (item.text.starts_with("Loading") && item.x >= 1000 && item.y >= 620);
        joint_stage = joint_stage || (item.text.starts_with("AC-MP-") && item.x >= 300 && item.x < 900 &&
                                      item.y >= 300 && item.y < 420);
        solo_stage = solo_stage || (std::regex_match(item.text, std::regex("AC-SP-[0-9]+")) && item.x >= 300 &&
                                    item.x < 900 && item.y >= 300 && item.y < 420);
        quick_stage =
            quick_stage || (item.text == "AC-TB-1" && item.x >= 300 && item.x < 900 && item.y >= 300 && item.y < 420);
        quick_mode =
            quick_mode || (item.text == "极速模拟" && item.x >= 400 && item.x < 850 && item.y >= 420 && item.y < 520);
        protocol_start = protocol_start || ((item.text == "协议启动！" || item.text == "协议启动!") && item.x >= 400 &&
                                            item.x < 800 && item.y >= 450 && item.y < 520);
        protocol_start_footer = protocol_start_footer || (item.text == "STRONGHOLD PROTOCOL" && item.x >= 400 &&
                                                          item.x < 850 && item.y >= 530 && item.y < 590);
        regulation = regulation || (std::regex_match(item.text, std::regex("规程等级([1-9]|1[0-9]|20)")) &&
                                    item.x >= 400 && item.x < 850 && item.y >= 420 && item.y < 520);
        difficulty = difficulty || ((item.text == "标准模拟" || item.text == "险境模拟" || item.text == "绝境模拟" ||
                                     item.text == "终极模拟") &&
                                    item.x >= 400 && item.x < 850 && item.y >= 420 && item.y < 520);
        search_success =
            search_success || (item.text.starts_with("搜寻成功") && item.x >= 900 && item.y >= 580 && item.y < 640);
        connecting =
            connecting || (item.text.starts_with("正在连接中") && item.x >= 900 && item.y >= 640 && item.y < 700);
        team_ready = team_ready || ((item.text == "准备就绪！" || item.text == "准备就绪!") && item.x >= 400 &&
                                    item.x < 800 && item.y >= 540 && item.y < 610);
        protocol_footer = protocol_footer || (item.text == "STRONGHOLD PROTOCOL" && item.x >= 400 && item.x < 850 &&
                                              item.y >= 620 && item.y < 680);
        if (item.x >= 100 && item.x < 1200 && item.y >= 450 && item.y < 510 &&
            std::regex_match(item.text, std::regex(".+#[0-9]+"))) {
            ++player_names;
        }
    }
    return (operation &&
            (loading || (joint_stage && difficulty) || (solo_stage && regulation) || (quick_stage && quick_mode))) ||
           (protocol_start && protocol_start_footer) || (search_success && connecting) ||
           (team_ready && protocol_footer && player_names >= 3) || (objective_header && defeat_leader);
}

std::optional<asst::spa::OperatorCardObservation> asst::spa::observe_operator_card(
    const std::vector<TextObservation>& texts,
    const std::vector<std::string>& known_names,
    std::string_view verified_panel_name)
{
    auto name = operator_panel_identity(texts, known_names);
    if (!verified_panel_name.empty()) {
        if (name && *name != verified_panel_name) {
            return std::nullopt;
        }
        if (!name && std::find(known_names.begin(), known_names.end(), verified_panel_name) != known_names.end()) {
            // The caller verified the full title on this same selection. A
            // cropped, truncated title can corroborate it, but a different
            // anchored catalogue name still invalidates the observation.
            const auto anchored = [](const auto& text) {
                return std::isfinite(text.score) && text.score >= .90 && text.x >= 0 && text.x < 40 && text.y >= 175 &&
                       text.y <= 215;
            };
            const bool conflict = std::any_of(texts.begin(), texts.end(), [&](const auto& text) {
                return anchored(text) && text.text != verified_panel_name &&
                       std::find(known_names.begin(), known_names.end(), text.text) != known_names.end();
            });
            if (!conflict && std::any_of(texts.begin(), texts.end(), [&](const auto& text) {
                    return anchored(text) && text.text.size() >= 6 && verified_panel_name.starts_with(text.text);
                })) {
                name = std::string(verified_panel_name);
            }
        }
    }
    if (!name) {
        return std::nullopt;
    }
    std::optional<int> level;
    for (const auto& text : texts) {
        if (!std::isfinite(text.score) || text.score < .90 || text.x < 103 || text.x > 165 || text.y < 210 ||
            text.y > 245 || text.text.empty() || text.text.size() > 2 ||
            !std::all_of(text.text.begin(), text.text.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) {
            continue;
        }
        int value = 0;
        for (const auto digit : text.text) {
            value = value * 10 + digit - '0';
        }
        if (value <= 0 || (level && *level != value)) {
            return std::nullopt;
        }
        level = value;
    }
    const auto initial = season_combat_stats(*name, false);
    const auto elite = season_combat_stats(*name, true);
    if (!level || !initial || !elite || initial->level == elite->level ||
        (*level != initial->level && *level != elite->level)) {
        return std::nullopt;
    }
    return OperatorCardObservation { *name, *level, *level == elite->level };
}

std::optional<int> asst::spa::preparation_countdown(const std::vector<TextObservation>& texts)
{
    std::optional<int> seconds;
    for (const auto& item : texts) {
        if (!std::isfinite(item.score) || item.score < 0.95 || item.x < 1120 || item.x > 1210 || item.y < 15 ||
            item.y > 85 || item.text.empty() || item.text.size() > 3 ||
            !std::all_of(item.text.begin(), item.text.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            continue;
        }
        int value = 0;
        for (const auto c : item.text) {
            value = value * 10 + c - '0';
        }
        if (value > 180 || (seconds && *seconds != value)) {
            return std::nullopt;
        }
        seconds = value;
    }
    return seconds;
}

std::optional<std::string> asst::spa::operator_panel_identity(
    const std::vector<TextObservation>& texts,
    const std::vector<std::string>& known_names)
{
    std::optional<std::string> result;
    for (const auto& text : texts) {
        if (!std::isfinite(text.score) || text.score < .9 || text.x < 0 || text.x >= 40 || text.y < 175 ||
            text.y > 215 || std::find(known_names.begin(), known_names.end(), text.text) == known_names.end()) {
            continue;
        }
        if (result && *result != text.text) {
            return std::nullopt;
        }
        result = text.text;
    }
    if (result) {
        return result;
    }
    // The detector can split a single title, e.g. Ghost's Chinese name into
    // two boxes. Join only confident fragments on the anchored title line;
    // accepting a catalogue prefix alone would misidentify the bench card.
    std::vector<TextObservation> fragments;
    for (const auto& text : texts) {
        if (std::isfinite(text.score) && text.score >= .9 && text.x >= 0 && text.x < 240 && text.y >= 175 &&
            text.y <= 215 && !text.text.empty()) {
            fragments.push_back(text);
        }
    }
    std::stable_sort(fragments.begin(), fragments.end(), [](const auto& left, const auto& right) {
        return left.x < right.x;
    });
    if (fragments.size() < 2 || fragments.front().x >= 40) {
        return std::nullopt;
    }
    std::string joined;
    const int title_y = fragments.front().y;
    for (const auto& fragment : fragments) {
        if (std::abs(fragment.y - title_y) <= 8) {
            joined += fragment.text;
        }
    }
    return std::find(known_names.begin(), known_names.end(), joined) != known_names.end() ? std::optional(joined)
                                                                                          : std::nullopt;
}

bool asst::spa::operator_title_contains_rank_artifact(
    const std::vector<TextObservation>& texts,
    const std::string& expected)
{
    if (expected.size() != 3 || (static_cast<unsigned char>(expected.front()) & 0xf0) != 0xe0) {
        return false;
    }
    int matching = 0;
    for (const auto& text : texts) {
        if (!std::isfinite(text.score) || text.score < .75 || text.x < 0 || text.x >= 40 || text.y < 175 ||
            text.y > 207) {
            continue;
        }
        if (!text.text.starts_with(expected)) {
            return false;
        }
        const std::string_view suffix = std::string_view(text.text).substr(expected.size());
        if (suffix.empty() || suffix.size() > 4 || suffix.find_first_not_of(" MILVX") != std::string_view::npos) {
            return false;
        }
        ++matching;
    }
    return matching == 1;
}

std::optional<double>
    asst::spa::single_operator_title_confidence(const std::vector<TextObservation>& texts, const std::string& expected)
{
    if (expected.size() != 3 || (static_cast<unsigned char>(expected.front()) & 0xf0) != 0xe0) {
        return std::nullopt;
    }
    std::optional<double> result;
    for (const auto& text : texts) {
        if (!std::isfinite(text.score) || text.score < .70 || text.x < 0 || text.x >= 40 || text.y < 175 ||
            text.y > 207 || text.text.empty()) {
            continue;
        }
        if (text.text != expected || result) {
            return std::nullopt;
        }
        result = text.score;
    }
    return result;
}

bool asst::spa::bench_replacement_title_needs_settle(
    const std::vector<TextObservation>& texts,
    const std::string& expected_name)
{
    return !expected_name.empty() && std::any_of(texts.begin(), texts.end(), [&](const auto& text) {
        return text.text == expected_name && std::isfinite(text.score) && text.score >= .80 && text.score < .90 &&
               text.x >= 0 && text.x < 40 && text.y >= 175 && text.y <= 215;
    });
}

bool asst::spa::bench_capacity_warning(const TextObservation& text)
{
    return std::isfinite(text.score) && text.score >= .85 && text.x >= 440 && text.x <= 850 && text.y >= 475 &&
           text.y <= 515 &&
           (text.text.find("整备区已达上限") != std::string::npos || text.text.find("干员已满") != std::string::npos ||
            text.text.find("无法继续调度干员") != std::string::npos);
}

bool asst::spa::equipment_capacity_warning(const TextObservation& text)
{
    return std::isfinite(text.score) && text.score >= .85 && text.x >= 440 && text.x <= 850 && text.y >= 475 &&
           text.y <= 515 &&
           (text.text.find("装备已满") != std::string::npos || text.text.find("无法继续购买装备") != std::string::npos);
}

bool asst::spa::selected_purchase_unavailable(const std::vector<TextObservation>& texts, int slot)
{
    return slot >= 0 && slot <= 5 && !purchase_confirmation_control(texts, slot) &&
           std::count_if(texts.begin(), texts.end(), [slot](const auto& text) {
               return text.text == "无法购买" && std::isfinite(text.score) && text.score >= .85 && text.y >= 620 &&
                      text.y <= 710 && std::abs(text.x - (1010 - slot * 162)) <= 80;
           }) == 1;
}

std::optional<asst::spa::TextObservation>
    asst::spa::purchase_confirmation_control(const std::vector<TextObservation>& texts, int slot)
{
    if (slot < 0 || slot > 5) {
        return std::nullopt;
    }
    std::optional<TextObservation> result;
    for (const auto& text : texts) {
        if (text.text != "确认购买" || !std::isfinite(text.score) || text.score < .85 || text.y < 620 || text.y > 710) {
            continue;
        }
        if (result) {
            return std::nullopt;
        }
        result = text;
    }
    if (!result || std::abs(result->x - (1010 - slot * 162)) > 100) {
        return std::nullopt;
    }
    return result;
}

std::optional<asst::spa::TextObservation> asst::spa::promotion_confirmation_control(
    const std::vector<TextObservation>& texts,
    const std::string& expected_name,
    int slot)
{
    if (expected_name.empty() || operator_panel_identity(texts, { expected_name }) != expected_name) {
        return std::nullopt;
    }
    return purchase_confirmation_control(texts, slot);
}

std::optional<asst::spa::TextObservation> asst::spa::promotion_short_title_confirmation(
    const std::vector<TextObservation>& texts,
    const std::string& expected_name,
    int slot)
{
    // One three-byte UTF-8 game character, for example 夕. Preserve the
    // normal title rules for longer names and for every ordinary purchase.
    if (expected_name.size() != 3 || (static_cast<unsigned char>(expected_name[0]) & 0xf0) != 0xe0) {
        return std::nullopt;
    }
    int titles = 0;
    bool range = false;
    for (const auto& text : texts) {
        if (!std::isfinite(text.score)) {
            continue;
        }
        // The chosen free card was read before the tap. The caller confirms
        // twice on the same selected slot, paired with the operator panel.
        if (text.score >= .55 && text.x >= 0 && text.x < 40 && text.y >= 175 && text.y <= 215) {
            if (text.text != expected_name) {
                return std::nullopt;
            }
            ++titles;
        }
        range = range || (text.score >= .90 && text.text == "攻击范围" && text.x >= 110 && text.x < 210 &&
                          text.y >= 280 && text.y <= 325);
    }
    return titles == 1 && range ? purchase_confirmation_control(texts, slot) : std::nullopt;
}

bool asst::spa::strategy_report_identity_text_verified(
    const std::vector<TextObservation>& texts,
    const StrategyIdentity& selected)
{
    if (selected.band_id.empty() || selected.leader.empty() || selected.name.empty()) {
        return false;
    }
    int headers = 0, leaders = 0, strategy_names = 0;
    for (const auto& text : texts) {
        if (!std::isfinite(text.score) || text.score < .90) {
            continue;
        }
        headers += text.x >= 690 && text.x < 860 && text.y >= 20 && text.y < 75 && text.text == "策略报告";
        leaders += text.x >= 120 && text.x < 390 && text.y >= 350 && text.y < 418 && text.text == selected.leader;
        strategy_names += text.x >= 20 && text.x < 300 && text.y >= 455 && text.y < 499 &&
                          (text.text == "【" + selected.name + "】" || text.text == selected.name);
    }
    return headers == 1 && leaders == 1 && strategy_names == 1;
}

bool asst::spa::strategy_report_progress_text_verified(
    const std::vector<TextObservation>& texts,
    const StrategyIdentity& selected,
    int wins)
{
    if (wins < 0 || wins > 6 || !strategy_report_identity_text_verified(texts, selected)) {
        return false;
    }
    const auto completed = "已通过模拟" + std::to_string(wins) + "次";
    int win_counters = 0;
    for (const auto& text : texts) {
        if (!std::isfinite(text.score) || text.score < .90) {
            continue;
        }
        if (text.x >= 220 && text.x < 355 && text.y >= 135 && text.y < 195 && text.text.starts_with("已通过模拟")) {
            if (text.text != completed) {
                return false;
            }
            ++win_counters;
        }
    }
    return win_counters == 1;
}

bool asst::spa::strategy_badge_report_text_verified(
    const std::vector<TextObservation>& texts,
    const StrategyIdentity& selected,
    int badge_wins)
{
    return badge_wins >= 3 && strategy_report_progress_text_verified(texts, selected, badge_wins);
}

bool asst::spa::observe_ready_confirmation(const std::vector<TextObservation>& texts)
{
    bool warning = false;
    int confirms = 0;
    int cancels = 0;
    for (const auto& item : texts) {
        if (!std::isfinite(item.score) || item.score < .90) {
            continue;
        }
        if (item.x >= 150 && item.x < 1050 && item.y >= 250 && item.y < 380) {
            warning = warning || item.text.starts_with("确认准备吗") || item.text.starts_with("确认准备就绪吗") ||
                      item.text.starts_with("剩余资金将在离开休整期时清空");
        }
        if (item.text == "确认" && item.x > 850 && item.x < 1100 && item.y > 400 && item.y < 600) {
            ++confirms;
        }
        if (item.text == "取消" && item.x >= 250 && item.x < 500 && item.y > 400 && item.y < 600) {
            ++cancels;
        }
    }
    return warning && confirms == 1 && cancels == 1;
}

std::optional<asst::spa::TextObservation>
    asst::spa::ready_confirmation_control(const std::vector<TextObservation>& texts, bool spending_reviewed)
{
    if (!observe_ready_confirmation(texts)) {
        return std::nullopt;
    }
    for (const auto& text : texts) {
        if (text.score >= .90 && text.y > 400 && text.y < 600 &&
            ((spending_reviewed && text.text == "确认" && text.x > 850 && text.x < 1100) ||
             (!spending_reviewed && text.text == "取消" && text.x >= 250 && text.x < 500))) {
            return text;
        }
    }
    return std::nullopt;
}

std::map<std::string, int> asst::spa::observe_pact_layer_numbers(
    const std::vector<PactHudAnchor>& anchors,
    const std::vector<TextObservation>& centered_numbers)
{
    std::map<std::string, int> result;
    const auto in_hud = [](const PactHudAnchor& anchor) {
        return !anchor.pact.empty() && anchor.center_x > 300 && anchor.center_x < 1050 && anchor.label_y >= 105 &&
               anchor.label_y < 145;
    };
    for (const auto& anchor : anchors) {
        // Expanded shop cards repeat pact names below the board. They are
        // not competing HUD anchors, even if their x coordinates coincide.
        if (!in_hud(anchor) || std::count_if(anchors.begin(), anchors.end(), [&](const auto& other) {
                                   return in_hud(other) && (other.pact == anchor.pact ||
                                                            std::abs(other.center_x - anchor.center_x) <= 40);
                               }) != 1) {
            continue;
        }
        std::optional<int> value;
        bool conflict = false;
        for (const auto& token : centered_numbers) {
            if (!std::isfinite(token.score) || token.score < .90 || std::abs(token.x - anchor.center_x) > 20 ||
                token.y < anchor.label_y - 38 || token.y >= anchor.label_y - 4 || token.text.empty() ||
                token.text.size() > 4 || !std::all_of(token.text.begin(), token.text.end(), [](unsigned char c) {
                    return c >= '0' && c <= '9';
                })) {
                continue;
            }
            int parsed = 0;
            for (const auto c : token.text) {
                parsed = parsed * 10 + c - '0';
            }
            conflict = conflict || (value && *value != parsed);
            value = parsed;
        }
        if (value && !conflict) {
            result.emplace(anchor.pact, *value);
        }
    }
    return result;
}

asst::spa::DeploymentCounterObservation asst::spa::observe_deployment_counter(const std::vector<TextObservation>& texts)
{
    DeploymentCounterObservation result;
    const std::string keyword = "剩余可放置角色";
    const auto assign = [&](int number) {
        if (number < 0 || number > 8) {
            return;
        }
        if (result.value && *result.value != number) {
            result.contradictory = true;
        }
        result.value = number;
    };
    for (const auto& item : texts) {
        if (item.score < .85 || item.x < 1000 || item.y < 350 || item.y >= 510) {
            continue;
        }
        auto text = item.text;
        std::erase_if(text, [](unsigned char c) { return c == ' ' || c == '\t'; });
        const auto position = text.find(keyword);
        if (position == std::string::npos) {
            continue;
        }
        if (result.label_y >= 0 && std::abs(result.label_y - item.y) > 25) {
            result.contradictory = true;
        }
        result.label_y = item.y;
        auto suffix = text.substr(position + keyword.size());
        if (suffix.starts_with("：")) {
            suffix.erase(0, std::string("：").size());
        }
        else if (suffix.starts_with(':')) {
            suffix.erase(0, 1);
        }
        if (suffix.size() == 1 && suffix[0] >= '0' && suffix[0] <= '8') {
            assign(suffix[0] - '0');
        }
    }
    if (result.label_y >= 0) {
        for (const auto& item : texts) {
            if (item.score >= .85 && item.x >= 1240 && item.x < 1280 && std::abs(item.y - result.label_y) <= 16 &&
                item.text.size() == 1 && item.text[0] >= '0' && item.text[0] <= '8') {
                assign(item.text[0] - '0');
            }
        }
    }
    if (result.contradictory) {
        result.value.reset();
    }
    return result;
}

std::optional<bool> asst::spa::parse_shop_toggle(const std::string& text)
{
    auto label = text;
    std::erase_if(label, [](unsigned char c) { return c == ' ' || c == '\n' || c == '\r'; });
    // The arrow next to the label is sometimes recognized as a trailing
    // square/letter (for example 收起口). The two-character action prefix is
    // stable; matching it inside the dedicated control ROI avoids mistaking
    // that harmless glyph for a failed post-exchange reconciliation.
    if (label.starts_with("收起")) {
        return true;
    }
    if (label.starts_with("展开")) {
        return false;
    }
    return std::nullopt;
}

std::optional<bool> asst::spa::observe_shop_toggle(const TextObservation& text)
{
    // The arrow can lower the whole token's confidence although both label
    // characters remain readable. Keep this tolerance local to its HUD control.
    if (text.score < .80 || text.x < 1120 || text.x > 1230 || text.y < 640 || text.y > 690) {
        return std::nullopt;
    }
    return parse_shop_toggle(text.text);
}

std::optional<bool> asst::spa::observe_shop_freeze(const TextObservation& text)
{
    if (text.score < .85 || text.x <= 950 || text.x >= 1120 || text.y < 380 || text.y >= 520) {
        return std::nullopt;
    }
    if (text.text == "冻结") {
        return false;
    }
    if (text.text == "解冻" || text.text == "已冻结") {
        return true;
    }
    return std::nullopt;
}

std::optional<asst::spa::TextObservation>
    asst::spa::shop_expansion_control(const std::vector<TextObservation>& texts, int expected_round)
{
    if (expected_round < 1 || expected_round > 20) {
        return std::nullopt;
    }
    bool ready = false, round = false;
    std::optional<TextObservation> control;
    for (const auto& text : texts) {
        if (!std::isfinite(text.score) || text.score < .9) {
            continue;
        }
        ready = ready || (text.text == "准备就绪" && text.x >= 1100 && text.y >= 80 && text.y < 150);
        if (text.x >= 480 && text.x < 530 && text.y >= 0 && text.y < 60 &&
            std::regex_match(text.text, std::regex("[0-9]{1,2}"))) {
            if (text.text != std::to_string(expected_round)) {
                return std::nullopt;
            }
            round = true;
        }
        if (const auto expanded = observe_shop_toggle(text); expanded) {
            if (*expanded || control) {
                return std::nullopt;
            }
            control = text;
        }
    }
    return ready && round ? control : std::nullopt;
}

std::optional<int> asst::spa::parse_shop_level(const std::string& text)
{
    std::string value = text;
    std::erase_if(value, [](unsigned char c) { return c == ' ' || c == '\n' || c == '\r'; });
    const std::vector<std::vector<std::string>> levels {
        { "1", "I", "Ⅰ" },  { "2", "II", "Ⅱ" }, { "3", "III", "Ⅲ" },
        { "4", "IV", "Ⅳ" }, { "5", "V", "Ⅴ" },  { "6", "VI", "Ⅵ" },
    };
    for (size_t index = 0; index < levels.size(); ++index) {
        if (std::find(levels[index].begin(), levels[index].end(), value) != levels[index].end()) {
            return static_cast<int>(index) + 1;
        }
    }
    return std::nullopt;
}

bool asst::spa::deployment_source_is_bench(int x, int y)
{
    // Purchased operators appear as sprites in the lower map-edge bench
    // strip. The bottom card row is still the shop and must never be used as
    // a deployment source.
    // The bench strip has ten slots on the current SPA layout.  The leftmost
    // slot can start around x=150 when the row is full; keeping the old 180px
    // guard silently rejected that card after the pixel scanner found it.
    return x >= 140 && x < 1000 && y >= 420 && y < 510;
}

std::optional<int> asst::spa::shop_column_from_right(int name_x)
{
    constexpr int left_edge = 125, slot_width = 162, slots = 6;
    if (name_x < left_edge || name_x >= left_edge + slot_width * slots) {
        return std::nullopt;
    }
    return slots - 1 - (name_x - left_edge) / slot_width;
}

std::optional<int> asst::spa::equipment_supply_slot(int title_x)
{
    if (title_x < 175 || title_x >= 1150) {
        return std::nullopt;
    }
    return title_x < 470 ? 0 : title_x < 820 ? 1 : 2;
}

bool asst::spa::settlement_text_verified(const PageObservation& observed)
{
    return observed.page == Page::Result && !observed.contradictory &&
           observed.result_completed != observed.result_interrupted && observed.passed_rounds.has_value() &&
           (observed.result_difficulty.has_value() ||
            (observed.result_joint && !observed.result_difficulty_name.empty()) ||
            (!observed.result_joint && observed.result_difficulty_name == "极速模拟"));
}

asst::spa::PageObservation
    asst::spa::observe_page(const std::vector<TextObservation>& texts, const std::vector<StrategyIdentity>& strategies)
{
    PageObservation result;
    std::string left;
    std::string all;
    std::string lower_left;
    for (const auto& item : texts) {
        if (!std::isfinite(item.score) || item.score < 0.65 || item.x < 0 || item.y < 0 || item.x >= 1280 ||
            item.y >= 720) {
            continue;
        }
        std::string text = item.text;
        std::erase_if(text, [](unsigned char c) { return c == ' ' || c == '\n' || c == '\r'; });
        all += text + "\n";
        if (item.x < 530) {
            left += text + "\n";
            if (item.y > 550) {
                lower_left += text + "\n";
            }
        }
    }
    const auto has = [&](const std::string& text) {
        return all.find(text) != std::string::npos;
    };
    if (std::any_of(texts.begin(), texts.end(), [](const auto& text) {
            return std::isfinite(text.score) && text.score >= .9 && text.x >= 400 && text.x < 800 && text.y >= 280 &&
                   text.y < 380 && text.text == "数据已更新，即将进行同步";
        })) {
        result.page = Page::DataUpdated;
        return result;
    }
    if (has("登录认证已失效") || has("请重新登录")) {
        // Account/session expiry is an external blocker, never a game result.
        result.page = Page::AuthenticationExpired;
        return result;
    }
    if (observe_ready_confirmation(texts)) {
        result.page = Page::ReadyConfirmation;
        return result;
    }
    if (std::any_of(
            texts.begin(),
            texts.end(),
            [](const auto& text) {
                return std::isfinite(text.score) && text.score >= .9 && text.text == "返回战场" && text.x >= 0 &&
                       text.x < 110 && text.y >= 120 && text.y < 490;
            }) &&
        !has("全景")) {
        // Teammate observation still exposes Ready and deployment counters.
        // Never admit that board to the local shopping/placement executor.
        result.page = Page::Spectating;
        return result;
    }
    if (std::any_of(texts.begin(), texts.end(), [](const auto& text) {
            return std::isfinite(text.score) && text.score >= .9 && text.text == "取消准备" && text.x >= 1100 &&
                   text.y >= 80 && text.y < 150;
        })) {
        result.page = Page::ReadyWaiting;
        return result;
    }
    const bool tactical_title = std::any_of(texts.begin(), texts.end(), [](const auto& text) {
        return std::isfinite(text.score) && text.score >= .85 && text.text == "战术决策" && text.x >= 140 &&
               text.x < 380 && text.y < 90;
    });
    if (tactical_title && (has("选定后将无法更改") || has("倒计时结束仍未选定将自动分配")) && has("进行协同调整")) {
        result.page = Page::TacticalDecision;
        return result;
    }
    // The B-server reward page intermittently drops the small “两场作战”
    // subtitle from OCR while retaining the title and the immutable-choice
    // warning.  Those two anchors are sufficient and are more stable than
    // the optional subtitle.
    if ((has("悬赏决策") || has("悬赏決策")) && has("选定后将无法更改")) {
        result.page = Page::BountySelection;
        return result;
    }
    // The inter-round reward overlay often renders only the compact
    // “请选择奖励” header on the first frame. It is still the bounty page;
    // waiting for the longer title made the native controller re-enter the
    // refresh decision and spend its whole action budget on the same overlay.
    if (has("请选择奖励")) {
        result.page = Page::BountySelection;
        return result;
    }
    if (verified_pact_setup_page(texts) || (has("确认本局信息") && has("核心盟约") && has("准备就绪")) ||
        (has("请选择策略") && has("泛用型策略")) || (has("确定自选编队") && has("准备就绪"))) {
        result.page = Page::RunSetup;
        return result;
    }
    // Joint settlement first shows the local roster, followed by the team
    // result. Do not mistake "passed rounds" or roster levels for battle HUD.
    const auto summary_anchor = [&](const std::string& value, int left, int top, int right, int bottom) {
        return std::any_of(texts.begin(), texts.end(), [&](const auto& text) {
            return std::isfinite(text.score) && text.score >= .9 && text.text == value && text.x >= left &&
                   text.x < right && text.y >= top && text.y < bottom;
        });
    };
    if ((summary_anchor("模拟完成", 0, 150, 430, 250) || summary_anchor("模拟中断", 0, 150, 430, 250)) &&
        summary_anchor("通过回合", 0, 240, 220, 300) && summary_anchor("下一步", 900, 590, 1280, 710)) {
        result.page = Page::ResultSummary;
        result.personal_result_completed = summary_anchor("模拟完成", 0, 150, 430, 250);
        result.personal_result_interrupted = summary_anchor("模拟中断", 0, 150, 430, 250);
        result.contradictory = result.personal_result_completed == result.personal_result_interrupted;
        for (const auto& item : texts) {
            if (!std::isfinite(item.score) || item.score < .9) {
                continue;
            }
            if (item.x < 180 && item.y >= 290 && item.y < 390 &&
                std::regex_match(item.text, std::regex("[0-9]{1,2}"))) {
                const int rounds = std::stoi(item.text);
                if (rounds > 20 || (result.personal_passed_rounds && *result.personal_passed_rounds != rounds)) {
                    result.contradictory = true;
                }
                result.personal_passed_rounds = rounds;
            }
            if (item.x >= 100 && item.x < 430 && item.y >= 130 && item.y < 180 &&
                (item.text == "标准模拟" || item.text == "险境模拟" || item.text == "绝境模拟" ||
                 item.text == "终极模拟")) {
                if (!result.personal_result_difficulty_name.empty() &&
                    result.personal_result_difficulty_name != item.text) {
                    result.contradictory = true;
                }
                result.personal_result_difficulty_name = item.text;
            }
            if (item.x >= 100 && item.x < 430 && item.y >= 440 && item.y < 495 &&
                std::regex_match(item.text, std::regex("Dr\\..+#[0-9]+"))) {
                if (!result.personal_result_identity.empty() && result.personal_result_identity != item.text) {
                    result.contradictory = true;
                }
                result.personal_result_identity = item.text;
            }
        }
        if (result.contradictory) {
            result.personal_passed_rounds.reset();
        }
        return result; // Team settlement and return-home are verified separately.
    }
    // Settlement does not by itself prove a valid strategy victory.
    // Both successful and interrupted simulations use the same settlement
    // layout. An interruption is still a result page, but it must never be
    // treated as a victory by the result policy.
    if ((has("模拟完成") || has("模拟中断")) && has("通过回合") && has("返回主页")) {
        result.page = Page::Result;
        result.result_completed = has("模拟完成");
        result.result_interrupted = has("模拟中断");
        result.result_joint = std::any_of(texts.begin(), texts.end(), [](const auto& text) {
            return text.score >= .9 && text.text == "同盟通过回合" && text.x >= 220 && text.x < 420 && text.y >= 590 &&
                   text.y < 640;
        });
        if (result.result_completed == result.result_interrupted) {
            // Both banners at once (or neither after OCR normalization) are
            // contradictory and must never be interpreted as a victory.
            result.contradictory = true;
        }
        for (const auto& item : texts) {
            if (!std::isfinite(item.score) || item.score < 0.85) {
                continue;
            }
            std::smatch match;
            std::optional<int>* target = nullptr;
            std::string digits;
            if (!result.result_joint && item.score >= .9 && item.text == "极速模拟" && item.x >= 100 && item.x < 350 &&
                item.y >= 130 && item.y < 180) {
                result.result_difficulty_name = item.text;
            }
            if (result.result_joint && item.x < 220 && item.y >= 590 && item.y < 650 &&
                (item.text == "标准模拟" || item.text == "险境模拟" || item.text == "绝境模拟" ||
                 item.text == "终极模拟")) {
                if (!result.result_difficulty_name.empty() && result.result_difficulty_name != item.text) {
                    result.contradictory = true;
                }
                result.result_difficulty_name = item.text;
            }
            if (result.result_joint && item.x >= 230 && item.x < 400 && item.y >= 640 && item.y < 710 &&
                std::regex_match(item.text, std::regex("[0-9]{1,2}"))) {
                target = &result.passed_rounds;
                digits = item.text;
            }
            else if (
                !result.result_joint && item.x < 350 && item.y >= 120 && item.y < 200 &&
                std::regex_match(item.text, match, std::regex("规程等级([0-9]{1,2})"))) {
                target = &result.result_difficulty;
                digits = match[1].str();
            }
            else if (
                !result.result_joint && item.x < 180 && item.y >= 290 && item.y < 390 &&
                std::regex_match(item.text, std::regex("[0-9]{1,2}"))) {
                target = &result.passed_rounds;
                digits = item.text;
            }
            if (target) {
                int value = 0;
                for (char c : digits) {
                    value = value * 10 + c - '0';
                }
                if (value > 20 || (target == &result.result_difficulty && value == 0) ||
                    (*target && **target != value)) {
                    result.contradictory = true;
                }
                else {
                    *target = value;
                }
            }
        }
        if (!result.result_difficulty_name.empty() && !result.result_joint && result.result_difficulty) {
            result.contradictory = true;
        }
        if (result.contradictory) {
            result.passed_rounds.reset();
            result.result_difficulty.reset();
            result.result_difficulty_name.clear();
            result.result_completed = false;
            result.result_interrupted = false;
        }
        return result;
    }
    // B-server uses “出售” for the selected deployed-operator panel, while
    // some builds show “点击取消” during placement. Both expose the same
    // direction/attack-range interaction surface.
    if ((has("点击取消") || has("出售")) && has("攻击范围")) {
        result.page = Page::Placement;
        return result;
    }
    if ((has("道具补给") || has("机密商店")) && has("无需消耗资金") && has("获得装备补给")) {
        result.page = Page::EquipmentSupply;
        return result;
    }
    // The final boss HUD replaces the round label with an enemy counter at
    // x~460. The bottom self-view label and top difficulty title jointly
    // distinguish this from a 2/3 setup header. A clipped impossible ratio
    // is not a second valid counter, and never becomes the round number.
    const auto anchored = [&](const std::string& value, int left_x, int right_x, int top_y, int bottom_y) {
        return std::any_of(texts.begin(), texts.end(), [&](const auto& item) {
            return std::isfinite(item.score) && item.score >= .85 && item.text.starts_with(value) && item.x >= left_x &&
                   item.x < right_x && item.y >= top_y && item.y < bottom_y;
        });
    };
    const bool solo_difficulty_hud = anchored("规程等级", 90, 250, 0, 70) || anchored("极速模拟", 90, 250, 0, 70);
    const bool solo_boss_view = anchored("自己", 520, 720, 560, 650) && solo_difficulty_hud;
    const bool joint_difficulty_hud = anchored("标准模拟", 90, 250, 0, 70) || anchored("险境模拟", 90, 250, 0, 70) ||
                                      anchored("绝境模拟", 90, 250, 0, 70) || anchored("终极模拟", 90, 250, 0, 70);
    // Joint boss battles can automatically enter the panoramic team view.
    // Its counter uses the same left HUD slot, without a round/self label.
    const bool joint_boss_view =
        joint_difficulty_hud && anchored("全景", 520, 720, 560, 650) && anchored("返回战场", 0, 200, 120, 490);
    if (!has("准备就绪") && !has("刷新") && (solo_boss_view || joint_boss_view)) {
        for (const auto& item : texts) {
            if (!std::isfinite(item.score) || item.score < .85 || item.x < 420 || item.x >= 565 || item.y < 0 ||
                item.y >= 65) {
                continue;
            }
            std::smatch match;
            if (!std::regex_search(item.text, match, std::regex("([0-9]{1,3})/([0-9]{1,3})$")) ||
                match.position() > 6) {
                continue;
            }
            const auto number = [](const std::string& digits) {
                int value = 0;
                for (const char digit : digits) {
                    value = value * 10 + digit - '0';
                }
                return value;
            };
            const int resolved = number(match[1].str()), total = number(match[2].str());
            if (total <= 0 || resolved > total) {
                continue;
            }
            if ((result.enemies_total && *result.enemies_total != total) ||
                (result.enemies_resolved && *result.enemies_resolved != resolved)) {
                result.contradictory = true;
            }
            result.enemies_total = total;
            result.enemies_resolved = resolved;
            result.page = Page::Battle;
        }
        if (result.contradictory) {
            result.enemies_total.reset();
            result.enemies_resolved.reset();
        }
        if (result.page == Page::Battle || result.contradictory) {
            return result;
        }
    }
    if (has("回合") && !has("准备就绪") && !has("刷新")) {
        for (const auto& item : texts) {
            if (!std::isfinite(item.score) || item.score < 0.85 || item.y < 0 || item.x < 560 || item.x > 720 ||
                item.y > 70) {
                continue;
            }
            std::smatch match;
            const bool complete = std::regex_match(item.text, match, std::regex("([0-9]{1,3})/([0-9]{1,3})"));
            std::optional<int> resolved;
            int total = 0;
            const auto number = [](const std::string& digits) {
                int value = 0;
                for (char c : digits) {
                    value = value * 10 + c - '0';
                }
                return value;
            };
            if (complete) {
                resolved = number(match[1].str());
                total = number(match[2].str());
            }
            else if (std::regex_match(item.text, match, std::regex("/([0-9]{1,3})"))) {
                total = number(match[1].str());
            }
            else {
                continue;
            }
            if (total <= 0 || (resolved && *resolved > total) ||
                (result.enemies_total && *result.enemies_total != total) ||
                (resolved && result.enemies_resolved && *result.enemies_resolved != *resolved)) {
                result.contradictory = true;
            }
            else {
                result.page = Page::Battle;
                if (resolved) {
                    result.enemies_resolved = resolved;
                }
                result.enemies_total = total;
            }
        }
        if (result.contradictory) {
            result.enemies_resolved.reset();
            result.enemies_total.reset();
        }
        // On the B-server battle HUD the enemy counter can disappear for a
        // short OCR interval while the round title and countdown remain
        // visible.  Treat that frame as battle with unknown enemy counts so
        // RoundTask keeps polling instead of reporting a false battle
        // failure.  The timer is restricted to the top-right HUD and the
        // preparation markers are explicitly absent, which keeps setup/shop
        // pages out of this fallback.
        if (result.page == Page::Unknown && !result.contradictory) {
            const bool countdown_visible = std::any_of(texts.begin(), texts.end(), [](const auto& item) {
                return std::isfinite(item.score) && item.score >= 0.85 && item.x >= 1080 && item.y < 130 &&
                       std::regex_match(item.text, std::regex("[0-9]{1,3}"));
            });
            if (countdown_visible) {
                result.page = Page::Battle;
            }
        }
        if (result.page == Page::Battle || result.contradictory) {
            return result;
        }
    }
    // The map can cover or collapse the freeze/refresh labels (especially on
    // B-server scale), while the preparation state still exposes the ready
    // button, round counter and remaining deployment count.  Accept that
    // stable triad as an equivalent preparation signature.
    if ((has("准备就绪") && has("刷新") && has("冻结") && has("剩余可放置角色")) ||
        (has("准备就绪") && has("剩余可放置角色") && has("回合"))) {
        result.page = Page::Preparation;
        const auto assign_number = [&](std::optional<int>& target, const std::string& number) {
            if (number.empty() || number.size() > 3 ||
                !std::all_of(number.begin(), number.end(), [](char c) { return c >= '0' && c <= '9'; })) {
                return;
            }
            int value = 0;
            for (char c : number) {
                value = value * 10 + c - '0';
            }
            if (target && *target != value) {
                result.contradictory = true;
            }
            target = value;
        };
        for (const auto& item : texts) {
            if (!std::isfinite(item.score) || item.score < 0.65) {
                continue;
            }
            auto text = item.text;
            std::erase(text, ' ');
            if (item.x >= 480 && item.x <= 530 && item.y < 60) {
                assign_number(result.round, text);
            }
            if (item.x >= 1160 && item.x < 1235 && item.y >= 500 && item.y <= 555) {
                assign_number(result.funds, text);
            }
            // Some pacts/operators expose free refresh charges as a small
            // number beside the refresh control.  Prefer an explicit OCR
            // label, then accept only a nearby numeric token; never treat the
            // paid refresh price itself as a free charge.
            const bool english_free =
                std::regex_match(text, std::regex("FREE(?:x|×|\\*)?[0-9]{0,2}", std::regex::icase));
            if (item.x > 980 && item.y >= 350 && item.y < 520 &&
                (text.find("免费") != std::string::npos || english_free)) {
                std::smatch match;
                if (std::regex_search(text, match, std::regex("([0-9]{1,2})"))) {
                    assign_number(result.free_refreshes, match[1].str());
                }
            }
            if (item.x >= 1250 && item.x < 1280 && item.y >= 360 && item.y <= 420) {
                assign_number(result.remaining_deployment, text);
            }
            std::smatch match;
            if (item.x > 1000 && std::regex_search(text, match, std::regex("剩余可放置角色(?:：|:)([0-9]+)"))) {
                assign_number(result.remaining_deployment, match[1].str());
            }
        }
        std::vector<std::pair<int, int>> free_labels;
        for (const auto& item : texts) {
            auto label = item.text;
            std::erase(label, ' ');
            const bool english_free =
                std::regex_match(label, std::regex("FREE(?:x|×|\\*)?[0-9]{0,2}", std::regex::icase));
            if (item.score >= 0.65 && (label.find("免费") != std::string::npos || english_free) && item.x > 980 &&
                item.y >= 350 && item.y < 520) {
                // Inline FREE x1 already supplies its own count. A nearby
                // deployment-counter zero must not become a second free
                // count and invalidate the entire preparation observation.
                if (std::none_of(label.begin(), label.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) {
                    free_labels.emplace_back(item.x, item.y);
                }
            }
        }
        if (!free_labels.empty()) {
            for (const auto& item : texts) {
                if (item.score < 0.65 || item.text.empty() || item.text.size() > 2 ||
                    !std::all_of(item.text.begin(), item.text.end(), [](char c) { return c >= '0' && c <= '9'; }) ||
                    item.x < 1000 || item.y < 350 || item.y >= 520) {
                    continue;
                }
                for (const auto& label : free_labels) {
                    if (item.x >= label.first && item.x - label.first <= 160 && std::abs(item.y - label.second) <= 20) {
                        assign_number(result.free_refreshes, item.text);
                        break;
                    }
                }
            }
        }
        // Real B-server archives show the paid 1 badge at (1181..1184,
        // 422..423), directly above 刷新 at (1192..1193,447). Reuse these
        // same-frame tokens: no missing FREE label, nearby deployment zero,
        // or ordinary one-fund assumption can establish this evidence.
        if (!result.free_refreshes && !result.contradictory) {
            const TextObservation* refresh = nullptr;
            int refresh_labels = 0;
            bool free_hint = false;
            for (const auto& item : texts) {
                if (!std::isfinite(item.score)) {
                    continue;
                }
                if (item.score >= .9 && item.text == "刷新" && item.x >= 1160 && item.x <= 1240 && item.y >= 435 &&
                    item.y <= 470) {
                    refresh = &item;
                    ++refresh_labels;
                }
                if (item.score >= .65 && item.x > 980 && item.y >= 350 && item.y < 520) {
                    auto label = item.text;
                    std::erase(label, ' ');
                    free_hint = free_hint || label.find("免费") != std::string::npos ||
                                std::regex_match(label, std::regex("FREE(?:x|×|\\*)?[0-9]{0,2}", std::regex::icase));
                }
            }
            if (refresh_labels == 1 && !free_hint) {
                int nearby_digits = 0;
                bool paid_one = false;
                bool freeze_anchor = false;
                for (const auto& item : texts) {
                    if (!std::isfinite(item.score) || item.score < .65) {
                        continue;
                    }
                    freeze_anchor = freeze_anchor || (item.score >= .9 && item.text == "冻结" && item.x >= 1000 &&
                                                      item.x <= 1100 && std::abs(item.y - refresh->y) <= 5);
                    if (item.x < 1170 || item.x > 1210 || item.y < 410 || item.y > 438 || item.x - refresh->x < -25 ||
                        item.x - refresh->x > 5 || refresh->y - item.y < 15 || refresh->y - item.y > 35 ||
                        item.text.empty() ||
                        !std::all_of(item.text.begin(), item.text.end(), [](char c) { return c >= '0' && c <= '9'; })) {
                        continue;
                    }
                    ++nearby_digits;
                    paid_one = item.text == "1" && item.score >= .95;
                }
                if (freeze_anchor && nearby_digits == 1 && paid_one) {
                    result.paid_refresh_price = 1;
                    result.free_refreshes = 0;
                }
            }
        }
        // The current book glyph is BELOW LEVEL. The gold badge above it
        // is the next upgrade price and must never become the book level.
        std::optional<int> level_x;
        for (const auto& item : texts) {
            if (item.score >= 0.85 && item.text == "LEVEL" && item.y >= 530 && item.y < 610) {
                level_x = item.x;
                break;
            }
        }
        if (level_x) {
            for (const auto& item : texts) {
                if (item.score < 0.90 || item.x < *level_x - 25 || item.x > *level_x + 55 || item.y < 568 ||
                    item.y >= 618) {
                    continue;
                }
                if (const auto level = parse_shop_level(item.text)) {
                    if (result.shop_level && result.shop_level != level) {
                        result.shop_level.reset();
                        break;
                    }
                    result.shop_level = level;
                }
            }
        }
        if (const auto hidden = observe_hidden_round(texts)) {
            if (result.round) {
                result.contradictory = true;
            }
            else {
                result.round = HiddenCoreRound;
                result.bonus_round = hidden;
            }
        }
        if (result.contradictory) {
            result.round.reset();
            result.bonus_round.reset();
            result.remaining_deployment.reset();
            result.funds.reset();
            result.free_refreshes.reset();
            result.paid_refresh_price.reset();
        }
        return result;
    }
    if ((has("物资调配处") && has("功勋兑换处") && (has("独立模拟") || has("搜寻队友") || has("同盟模拟"))) ||
        ((has("个人模拟") || has("联合模拟")) && has("断线重连") && has("功勋兑换处") &&
         (has("放弃模拟") || has("协议数据库")))) {
        result.page = Page::Lobby;
        return result;
    }
    // Some current-client layouts render the lower-right start button as an
    // icon/partial label while the title and selected difficulty remain
    // visible.  The title is the stable page anchor; the action plugin still
    // requires a selected target (or a guarded start-mode fallback).
    const bool training_start = anchored("开始训练", 990, 1280, 590, 720);
    if (has("请选择模拟难度") && (has("开始模拟") || has("开始搜寻") || has("当前选择") || training_start)) {
        result.page = Page::Difficulty;
        if (has("开始搜寻") || training_start) {
            for (const auto& item : texts) {
                if (!std::isfinite(item.score) || item.score < .85 || item.x < 0 || item.x >= 530 || item.y <= 550 ||
                    item.y >= 720) {
                    continue;
                }
                const auto difficulty = joint_difficulty_from_label(item.text);
                if (!difficulty) {
                    continue;
                }
                if (result.selected_difficulty && *result.selected_difficulty != *difficulty) {
                    result.contradictory = true;
                    break;
                }
                result.selected_difficulty = difficulty;
            }
            if (result.contradictory || (training_start && result.selected_difficulty != JointTrainingDifficulty)) {
                result.selected_difficulty.reset();
            }
            return result;
        }
        const std::regex level("规程等级([0-9]{1,2})");
        for (auto it = std::sregex_iterator(lower_left.begin(), lower_left.end(), level); it != std::sregex_iterator();
             ++it) {
            const auto digits = (*it)[1].str();
            const int value = digits.size() == 1 ? digits[0] - '0' : (digits[0] - '0') * 10 + digits[1] - '0';
            if (value < 1 || value > 20 || (result.selected_difficulty && *result.selected_difficulty != value)) {
                result.contradictory = true;
            }
            else {
                result.selected_difficulty = value;
            }
        }
        if (result.contradictory) {
            result.selected_difficulty.reset();
        }
        return result;
    }
    if (!has("策略报告") || left.find("初始生命值") == std::string::npos) {
        return result;
    }
    result.page = Page::StrategyReport;
    for (const auto& strategy : strategies) {
        if (left.find(strategy.leader) != std::string::npos && left.find(strategy.name) != std::string::npos) {
            if (result.band_id && *result.band_id != strategy.band_id) {
                result.contradictory = true;
            }
            result.band_id = strategy.band_id;
        }
    }
    const std::regex wins("已通过模拟([0-9]+)次");
    for (auto it = std::sregex_iterator(left.begin(), left.end(), wins); it != std::sregex_iterator(); ++it) {
        const auto digits = (*it)[1].str();
        if (digits.size() != 1 || digits[0] < '0' || digits[0] > '6') {
            result.contradictory = true;
            continue;
        }
        const int value = digits[0] - '0';
        if (result.wins && *result.wins != value) {
            result.contradictory = true;
        }
        result.wins = value;
    }
    if (result.contradictory) {
        result.band_id.reset();
        result.wins.reset();
        result.selected_difficulty.reset();
    }
    return result;
}
