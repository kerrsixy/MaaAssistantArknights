#pragma once

#include "SPAObservation.h"

#include <algorithm>
#include <array>
#include <string>
#include <tuple>
#include <vector>

namespace asst::spa
{
struct TacticalOffer
{
    std::string title;
    std::string description;
    int slot = -1;
};

inline bool tactical_elite_purchase(const TacticalOffer& offer)
{
    return offer.title == "升华" && offer.description == "你接下来购买的1名干员变为精锐状态";
}

inline std::optional<int> tactical_layer_gain(const std::string& description)
{
    const std::string marker = "层数+";
    std::optional<int> result;
    size_t start = 0;
    while ((start = description.find(marker, start)) != std::string::npos) {
        start += marker.size();
        size_t end = start;
        int value = 0;
        while (end < description.size() && description[end] >= '0' && description[end] <= '9') {
            if (end - start >= 3) {
                return std::nullopt;
            }
            value = value * 10 + description[end++] - '0';
        }
        const bool invalid_suffix =
            end < description.size() && ((static_cast<unsigned char>(description[end]) < 128 &&
                                          std::string(" ,;)]").find(description[end]) == std::string::npos) ||
                                         description.compare(end, std::string("％").size(), "％") == 0);
        if (end == start || value <= 0 || invalid_suffix || (result && *result != value)) {
            return std::nullopt;
        }
        result = value;
        start = end;
    }
    return result;
}

inline std::vector<TacticalOffer> observe_tactical_offers(const std::vector<TextObservation>& texts)
{
    std::vector<TacticalOffer> offers;
    constexpr std::array<int, 4> edges { 170, 470, 820, 1150 };
    for (int slot = 0; slot < 3; ++slot) {
        std::vector<TextObservation> titles, descriptions;
        for (const auto& text : texts) {
            if (text.score < .85 || text.x < edges[slot] || text.x >= edges[slot + 1]) {
                continue;
            }
            if (text.y >= 260 && text.y < 330) {
                titles.push_back(text);
            }
            else if (text.y >= 330 && text.y < 430) {
                descriptions.push_back(text);
            }
        }
        const auto join = [](auto parts) {
            std::sort(parts.begin(), parts.end(), [](const auto& a, const auto& b) {
                return std::tie(a.y, a.x) < std::tie(b.y, b.x);
            });
            std::string result;
            for (const auto& part : parts) {
                result += part.text;
            }
            std::erase(result, ' ');
            return result;
        };
        offers.push_back({ join(titles), join(descriptions), slot });
    }
    return offers;
}

// These are free friendly adjustments, not enemies with bounty difficulty.
// Unknown descriptions stay unscored rather than inheriting an enemy policy.
// Joint supplies use two rows and a turn owner. The three-card solo layout
// must not be reused as though its middle title band covered both rows.
inline std::optional<bool> supply_own_turn(const std::vector<TextObservation>& texts)
{
    bool own = false, other = false;
    for (const auto& text : texts) {
        if (text.score < .9 || text.x < 800 || text.x >= 1100 || text.y >= 90) {
            continue;
        }
        own = own || text.text == "当前轮到你决策";
        other = other || text.text.find("正在决策") != std::string::npos;
    }
    return own == other ? std::nullopt : std::optional(own);
}

inline std::vector<TacticalOffer> observe_supply_offers(const std::vector<TextObservation>& texts)
{
    if (!supply_own_turn(texts).has_value()) {
        return observe_tactical_offers(texts);
    }
    std::vector<TacticalOffer> offers;
    for (int row = 0; row < 2; ++row) {
        std::vector<TextObservation> shifted;
        for (auto text : texts) {
            if (text.y < (row == 0 ? 110 : 410) || text.y >= (row == 0 ? 280 : 580)) {
                continue;
            }
            text.y += row == 0 ? 150 : -150;
            shifted.push_back(std::move(text));
        }
        for (auto offer : observe_tactical_offers(shifted)) {
            offer.slot += row * 3;
            offers.push_back(std::move(offer));
        }
    }
    return offers;
}

inline bool
    supply_preparation_restored(bool joint, Page page, std::optional<int> funds_before, std::optional<int> funds_after)
{
    // Joint supplies precede the next preparation income. Its funds may
    // increase; this verifies the page transition, not item inventory or cash.
    return page == Page::Preparation && funds_after.has_value() &&
           (joint || (funds_before.has_value() && funds_after == funds_before));
}

inline int choose_tactical_adjustment(
    const std::vector<TacticalOffer>& offers,
    const std::string& primary,
    const std::vector<std::string>& active_pacts,
    int bench_occupied)
{
    int best = -1;
    int best_score = -1;
    for (int i = 0; i < static_cast<int>(offers.size()); ++i) {
        const auto& offer = offers[i];
        if (offer.slot < 0 || offer.slot > 2 || offer.title.empty()) {
            continue;
        }
        const auto has = [&](const std::string& value) {
            return offer.description.find(value) != std::string::npos;
        };
        int score = -1;
        if (offer.title == "补给" && has("2次刷新") && has("免费")) {
            score = 90;
        }
        else if (offer.title == "财富" && has("获得1资金")) {
            score = 45;
        }
        else if (offer.title.ends_with("驰援") && has("获得1名随机") && has("不超过当前调度中心等级")) {
            const bool main = !primary.empty() && offer.title.find(primary) != std::string::npos;
            score = main ? (bench_occupied < 8 ? 110 : 55) : 15;
        }
        else if (const auto gain = tactical_layer_gain(offer.description); offer.title.ends_with("盟誓") && gain) {
            score = 0;
            for (const auto& pact : active_pacts) {
                if (has(pact)) {
                    score += *gain * (pact == primary ? 10 : 3);
                }
            }
        }
        else if (offer.title == "列装" && has("获得2件随机1阶装备")) {
            score = 50;
        }
        else if (tactical_elite_purchase(offer)) {
            score = 85;
        }
        else if (offer.title == "整备" && has("进阶品质")) {
            score = 50;
        }
        else if (offer.title == "自愈" && has("受到伤害") && has("50")) {
            score = 80;
        }
        else if (offer.title == "无瑕" && has("生命值为满") && has("30%")) {
            score = 60;
        }
        else if (offer.title == "锐利" && has("整备区为空") && has("30%")) {
            score = bench_occupied == 0 ? 75 : 10;
        }
        else if (offer.title == "火力" && has("整备区至少有3") && has("20")) {
            score = bench_occupied >= 3 ? 75 : 10;
        }
        else if (offer.title == "征召" && has("一行") && has("3名干员") && has("再部署")) {
            score = 35;
        }
        if (score > best_score) {
            best = i;
            best_score = score;
        }
    }
    return best;
}

struct BountyOffer
{
    std::string token;
    int difficulty = 0;
    bool flying = false;
    int slot = -1;
    int reward = -1;
    std::string description;
};

inline std::optional<int> bounty_reward(const std::string& description)
{
    const auto start = description.find("获得");
    if (start == std::string::npos) {
        return std::nullopt;
    }
    const auto first = start + std::string("获得").size();
    size_t end = first;
    int amount = 0;
    while (end < description.size() && description[end] >= '0' && description[end] <= '9' && end - first < 2) {
        amount = amount * 10 + description[end++] - '0';
    }
    return end > first && amount > 0 && description.compare(end, std::string("资金").size(), "资金") == 0
               ? std::optional<int>(amount)
               : std::nullopt;
}

inline std::string normalize_bounty_title(std::string value)
{
    std::erase(value, ' ');
    for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>> { { "Ⅲ", "III" },
                                                                                     { "Ⅱ", "II" },
                                                                                     { "Ⅰ", "I" },
                                                                                     { "Ⅳ", "IV" },
                                                                                     { "Ⅴ", "V" },
                                                                                     { "Ⅵ", "VI" } }) {
        for (size_t pos = 0; (pos = value.find(from, pos)) != std::string::npos; pos += to.size()) {
            value.replace(pos, from.size(), to);
        }
    }
    return value;
}

inline int bounty_difficulty_from_title(const std::string& title)
{
    const auto normalized = normalize_bounty_title(title);
    const auto end = normalized.find_last_not_of("IVX");
    const auto suffix = normalized.substr(end == std::string::npos ? 0 : end + 1);
    const std::array<std::string, 6> numerals { "I", "II", "III", "IV", "V", "VI" };
    const auto found = std::find(numerals.begin(), numerals.end(), suffix);
    return found == numerals.end() ? 0 : static_cast<int>(found - numerals.begin()) + 1;
}

inline bool bounty_requires_anti_air(const std::string& title, const std::string& description)
{
    const auto text = title + description;
    // Unknown enemies remain unclassified, not proven ground units.
    return text.find("飞行") != std::string::npos || text.find("空中") != std::string::npos ||
           text.find("节日气球") != std::string::npos || text.find("开天汽水瓶") != std::string::npos;
}

inline int choose_bounty(const std::vector<BountyOffer>& offers, bool anti_air_verified)
{
    int best = -1;
    const auto risk = [&](const auto& offer) {
        // A named boss has no Roman suffix. Unknown must not tie all three
        // cards and silently select the leftmost boss. Reward is only the
        // conservative fallback when mechanics are not yet modeled.
        return std::tuple { !anti_air_verified && offer.flying ? 1 : 0,
                            offer.difficulty > 0 ? offer.difficulty : 7,
                            offer.reward > 0 ? offer.reward : 100 };
    };
    for (int i = 0; i < static_cast<int>(offers.size()); ++i) {
        if (offers[i].token.empty()) {
            continue;
        }
        if (best < 0 || risk(offers[i]) < risk(offers[best])) {
            best = i;
        }
    }
    return best;
}
}
