#pragma once

#include <meojson/json.hpp>

namespace asst::spa
{
inline bool valid_run_count(int count)
{
    return count >= 1 && count <= 99'999;
}

// Only a verified settlement followed by a verified lobby return closes an
// attempt. This counts games, never wins, badges or acceptance streaks.
inline bool settled_run_returned(const json::value& receipt)
{
    return receipt.get("return_home_verified", false) && receipt.get("result_text_verified", false) &&
           !receipt.get("contradictory", true) &&
           (receipt.get("result_completed", false) != receipt.get("result_interrupted", false));
}

inline bool round_settlement_can_be_retained(const json::value& receipt)
{
    return receipt.get("page", std::string()) == "result" && receipt.get("battle_seen", false) &&
           receipt.get("result_frame_saved", false) && receipt.get("result_text_verified", false) &&
           receipt.get("passed_rounds_known", false) && !receipt.get("contradictory", true) &&
           !receipt.get("result_joint", true) &&
           (receipt.get("result_completed", false) != receipt.get("result_interrupted", false));
}

// Preserve configuration across games, not the preceding game's inventory,
// board coordinates, bans, money, transaction checkpoints or chosen strategy.
inline json::value fresh_run_parameters(const json::value& initial)
{
    json::value fresh = json::object {};
    for (const auto* key : { "mode",
                             "assisted_run",
                             "require_fresh_independent",
                             "difficulty",
                             "expected_boss_round",
                             "client_type",
                             "primary_pact",
                             "requested_primary_pact",
                             "dynamic_primary_pact",
                             "prefer_unclaimed_strategy",
                             "schedule_strategy_badges",
                             "strategy_id",
                             "enabled_core_pacts",
                             "carries",
                             "allow_refresh",
                             "derive_support_pacts",
                             "support_pacts",
                             "primary_deadline_round",
                             "minimum_recruit_price",
                             "basic_mode",
                             "dynamic_decision_mode",
                             "basic_upgrade_rounds",
                             "promotion_copies",
                             "auto_deploy",
                             "auto_deploy_direction",
                             "max_actions",
                             "egir_chain_validation_mode" }) {
        if (initial.contains(key)) {
            fresh[key] = initial.at(key);
        }
    }
    fresh["run_count"] = 1;
    fresh["manage_lifecycle"] = true;
    fresh["auto_run"] = true;
    fresh["opening_strategy_selection"] = true;
    fresh["roster"] = json::array {};
    fresh["bench_occupied"] = 0;
    return fresh;
}
}
