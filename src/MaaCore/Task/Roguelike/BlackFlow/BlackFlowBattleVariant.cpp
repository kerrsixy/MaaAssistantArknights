#include "BlackFlowBattleVariant.h"

#include <algorithm>
#include <charconv>

#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Vision/Battle/BattlefieldMatcher.h"

namespace asst::blackflow
{
std::optional<std::pair<std::string, int>> parse_battle_variant_key(const std::string& key)
{
    const auto marker = key.rfind("#total");
    if (marker == std::string::npos || marker == 0) {
        return std::nullopt;
    }
    const auto suffix = key.substr(marker + 6);
    int total = 0;
    const auto converted = std::from_chars(suffix.data(), suffix.data() + suffix.size(), total);
    if (converted.ec != std::errc {} || converted.ptr != suffix.data() + suffix.size() || total < 1 || total > 10'000 ||
        suffix != std::to_string(total)) {
        return std::nullopt;
    }
    return std::pair { key.substr(0, marker), total };
}

BattleVariantObservation recognize_battle_variant_total(const cv::Mat& image, std::uint64_t capture_sequence)
{
    BattleVariantObservation observation { .capture_sequence = capture_sequence };
    if (image.empty() || capture_sequence == 0) {
        return observation;
    }
    // A new matcher has neither prior-frame cache nor a total-kills prompt.
    // Never consume BattleHelper's monotonic/cached m_total_kills here.
    BattlefieldMatcher matcher(image);
    matcher.set_object_of_interest({ .kills = true });
    const auto result = matcher.analyze();
    if (!result || result->kills.status != BattlefieldMatcher::MatchStatus::Success) {
        return observation;
    }
    const auto [kills, total] = result->kills.value;
    observation.kills = kills;
    observation.total = total;
    observation.fresh_success = total > 0 && total <= 10'000 && kills >= 0 && kills <= total;
    return observation;
}

BattleVariantReceipt consume_predeploy_battle_variant(
    const RoguelikeCopilotConfig& config,
    const std::string& base_key,
    bool deployment_started,
    VariantDeployPlan& active_plan,
    const std::function<BattleVariantObservation()>& capture)
{
    BattleVariantReceipt receipt { .selected_key = base_key, .reason = "no_declared_variants" };
    const auto variants = config.get_stage_variant_keys(base_key);
    if (variants.empty()) {
        return receipt;
    }
    if (deployment_started) {
        receipt.reason = "deployment_already_started_keep_current_plan";
        return receipt;
    }
    std::uint64_t latest_sequence = 0;
    std::optional<int> previous;
    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto observation = capture();
        receipt.observations.push_back(observation);
        if (!observation.capture_success || !observation.fresh_success ||
            observation.capture_sequence <= latest_sequence || observation.total < 1 || observation.total > 10'000 ||
            observation.kills < 0 || observation.kills > observation.total) {
            latest_sequence = std::max(latest_sequence, observation.capture_sequence);
            previous.reset();
            continue;
        }
        latest_sequence = observation.capture_sequence;
        if (!previous || *previous != observation.total) {
            previous = observation.total;
            continue;
        }
        receipt.stable_total = observation.total;
        const auto variant =
            std::ranges::find_if(variants, [&](const auto& entry) { return entry.first == observation.total; });
        if (variant == variants.end()) {
            receipt.reason = "stable_total_without_declared_variant_keep_base";
            return receipt;
        }
        const auto plan = config.get_stage_data(variant->second);
        if (!plan || plan->deploy_plan.empty()) {
            receipt.reason = "selected_variant_unavailable_keep_base";
            return receipt;
        }
        active_plan = plan->deploy_plan;
        receipt.selected_key = variant->second;
        receipt.applied = true;
        receipt.reason = "two_fresh_independent_HUD_totals_agree_predeploy";
        return receipt;
    }
    receipt.reason = "four_capture_budget_exhausted_keep_base";
    return receipt;
}
}
