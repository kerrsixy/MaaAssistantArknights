#include "Task/MiniGame/SPA/SPACashEquipmentEvidence.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <opencv2/imgcodecs.hpp>

int main()
{
    using namespace asst::spa;
    const auto before = cv::imread("inventory_freeze_before_live.png");
    const auto after = cv::imread("inventory_freeze_after_live.png");
    assert(!before.empty() && !after.empty());
    const auto old_points = equipment_inventory_candidates(before);
    const auto new_points = equipment_inventory_candidates(after);
    assert(old_points.size() == 5 && new_points.size() == 6);
    const cv::Point coin(873, 394);
    const auto old_match = stable_cash_source(coin, old_points);
    const auto new_match = stable_cash_source(coin, new_points);
    assert(old_match && new_match);
    assert(old_points[*old_match] == coin && new_points[*new_match] == coin);

    auto missing = new_points;
    missing.erase(std::remove(missing.begin(), missing.end(), coin), missing.end());
    assert(!stable_cash_source(coin, missing));
    auto shifted = old_points;
    shifted[*old_match].x += 20;
    assert(shifted.size() == old_points.size());
    assert(!stable_cash_source(coin, shifted));
    auto ambiguous = new_points;
    ambiguous.emplace_back(coin.x + 5, coin.y);
    assert(!stable_cash_source(coin, ambiguous));
    assert(!stable_cash_source({ 963, 466 }, { { 963, 466 } }));
    assert(!stable_cash_source(coin, {}));
    assert(stable_cash_source(coin, { { 875, 396 } }));

    assert(cash_source_retry_allowed(1, false, false, "source_identity"));
    assert(cash_source_retry_allowed(1, false, false, "cash_source_reacquisition"));
    assert(!cash_source_retry_allowed(2, false, false, "source_identity"));
    assert(!cash_source_retry_allowed(1, true, false, "source_identity"));
    assert(!cash_source_retry_allowed(1, false, true, "cash_source_reacquisition"));
    assert(!cash_source_retry_allowed(1, false, false, "held_target"));
    assert(!cash_source_retry_allowed(1, false, false, "effect_verification"));
    assert(cash_effect_verified(true, true, 8, 8, 1, 1, 2));
    assert(!cash_effect_verified(false, true, 8, 8, 1, 1, 2));
    assert(!cash_effect_verified(true, false, 8, 8, 1, 1, 2));
    assert(!cash_effect_verified(true, true, 8, 9, 1, 1, 2));
    assert(!cash_effect_verified(true, true, 8, 8, 1, 0, 1));
    assert(!cash_effect_verified(true, true, 8, 8, 1, 1, 3));
    assert(cash_effect_verified(true, true, 8, 8, 2, 5, 7, "定期存折"));
    assert(!cash_effect_verified(true, true, 8, 8, 2, 5, 6, "定期存折"));
    assert(!cash_effect_verified(true, true, 8, 8, 2, 5, 7, "盟约之币"));
    assert(!cash_effect_verified(true, true, 8, 8, 2, 5, 7, "见钱眼开玩偶"));
    json::object pending { { "equipment", "盟约之币" },
                           { "source_round", 8 },
                           { "funds_before", 1 },
                           { "cash_gain_quote", 1 },
                           { "contact_release_attempted", true },
                           { "source_and_target_verified", true } };
    json::value state = json::object { { "page", "preparation" }, { "round_known", true }, { "round", 8 },
                                       { "funds_known", true },   { "funds", 1 },          { "contradictory", false } };
    assert(pending_cash_observation(pending, state, false) == CashReconciliation::ObserveAgain);
    pending["wallet_observations"] = 1;
    assert(pending_cash_observation(pending, state, false) == CashReconciliation::CloseUnknown);
    pending["observation_checked"] = true;
    state["funds"] = 2; // A later one-credit sale must not resolve the old coin.
    assert(pending_cash_observation(pending, state, false) == CashReconciliation::AlreadyClosed);
    pending["observation_checked"] = false;
    assert(pending_cash_observation(pending, state, false) == CashReconciliation::Verified);
    auto passbook_pending = pending;
    passbook_pending["equipment"] = "定期存折";
    passbook_pending["cash_gain_quote"] = 5;
    auto passbook_state = state;
    passbook_state["funds"] = 6;
    assert(pending_cash_observation(passbook_pending, passbook_state, false) == CashReconciliation::Verified);
    passbook_state["round"] = 9;
    assert(pending_cash_observation(passbook_pending, passbook_state, false) == CashReconciliation::CloseUnknown);
    const auto restored = restored_cash_pending(pending);
    assert(restored.get("observation_checked", false));
    assert(restored.get("attribution_closed_reason", std::string()) == "restored_contact_unknown");
    assert(restored.get("funds_before", -1) == 1);
    assert(restored.get("cash_gain_quote", -1) == 1);
    assert(pending_cash_observation(restored, state, false) == CashReconciliation::AlreadyClosed);
    assert(!cash_new_source_allowed(restored, 8, 8, false));
    assert(!cash_new_source_allowed(restored, 9, -1, false));
    assert(!cash_new_source_allowed(restored, 9, 8, false));
    assert(!cash_new_source_allowed(restored, 9, 9, true));
    assert(cash_new_source_allowed(restored, 9, 9, false));
    assert(!cash_new_source_allowed(pending, 9, 9, false));
    assert(
        pending_cash_observation(restored_cash_pending(restored), state, false) == CashReconciliation::AlreadyClosed);
    const auto unknown_restored = restored_cash_pending(json::object { { "unknown", true } });
    assert(unknown_restored.get("unknown", false));
    assert(pending_cash_observation(unknown_restored, state, false) == CashReconciliation::AlreadyClosed);
    assert(!cash_new_source_allowed(unknown_restored, 9, 9, false));
    auto unknown_observed = unknown_restored;
    unknown_observed["closed_observation_round"] = 8;
    assert(!cash_new_source_allowed(unknown_observed, 8, 8, false));
    assert(cash_new_source_allowed(unknown_observed, 9, 9, false));
    state["round"] = 9; // Neither can the next round's ordinary income.
    assert(pending_cash_observation(pending, state, false) == CashReconciliation::CloseUnknown);
    state["round"] = 8;
    state["round_known"] = false;
    assert(pending_cash_observation(pending, state, false) == CashReconciliation::CloseUnknown);
    state["round_known"] = true;
    state["contradictory"] = true;
    assert(pending_cash_observation(pending, state, false) == CashReconciliation::CloseUnknown);
    state["contradictory"] = false;
    pending["source_and_target_verified"] = false;
    assert(pending_cash_observation(pending, state, false) == CashReconciliation::CloseUnknown);
    std::cout << "Stable cash source: real 5-to-6 inventory fixtures and negative cases passed\n";
    std::cout << "Cash attribution: closed windows reject later sale and round income\n";
}
