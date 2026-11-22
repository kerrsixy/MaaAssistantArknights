#include "Task/MiniGame/SPA/SPALifecycle.h"
#include "Task/MiniGame/SPA/SPARunBatch.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace asst::spa;
    for (const bool assisted : { false, true }) {
        const auto fresh = fresh_run_parameters(
            json::object { { "mode", "quick" },
                           { "assisted_run", assisted },
                           { "require_fresh_independent", true },
                           { "roster", json::array { json::object { { "name", "old operator" } } } },
                           { "bench_occupied", 7 },
                           { "tactical_elite_purchase_pending", true },
                           { "unknown_inventory_state", json::object { { "item", "old equipment" } } },
                           { "cash_equipment_pending", json::object { { "unknown", true } } },
                           { "field_positions", json::array { json::object { { "x", 500 } } } } });
        assert(fresh.find<bool>("assisted_run").has_value());
        assert(fresh.get("assisted_run", !assisted) == assisted);
        assert(fresh.get("require_fresh_independent", false));
        assert(fresh_run_parameters(fresh).get("assisted_run", !assisted) == assisted);
        assert(fresh.get("mode", std::string()) == "quick");
        assert(fresh.at("roster").as_array().empty());
        assert(fresh.get("bench_occupied", -1) == 0);
        assert(!fresh.contains("tactical_elite_purchase_pending"));
        assert(!fresh.contains("unknown_inventory_state"));
        assert(!fresh.contains("cash_equipment_pending"));
        assert(!fresh.contains("field_positions"));
    }
    assert(!fresh_run_parameters(json::object {}).contains("assisted_run"));
    const auto next_badge_run = fresh_run_parameters(
        json::object { { "mode", "independent" },
                       { "schedule_strategy_badges", true },
                       { "selected_strategy_id", "band_amiya" } });
    assert(next_badge_run.get("schedule_strategy_badges", false));
    assert(!next_badge_run.contains("selected_strategy_id"));
    for (const bool send_emotes : { false, true }) {
        const auto next_joint_run = fresh_run_parameters(
            json::object { { "mode", "joint" },
                           { "joint_emote_probe", send_emotes },
                           { "joint_commend_teammates", !send_emotes } });
        assert(next_joint_run.find<bool>("joint_emote_probe") == send_emotes);
        assert(next_joint_run.find<bool>("joint_commend_teammates") == !send_emotes);
    }
    json::value watched_settlement = json::object { { "page", "result" },
                                                    { "battle_seen", true },
                                                    { "result_text_verified", true },
                                                    { "result_frame_saved", true },
                                                    { "passed_rounds_known", true },
                                                    { "contradictory", false },
                                                    { "result_joint", false },
                                                    { "result_completed", false },
                                                    { "result_interrupted", true },
                                                    { "return_home_verified", false } };
    assert(round_settlement_can_be_retained(watched_settlement));
    assert(!settled_run_returned(watched_settlement));
    watched_settlement["result_frame_saved"] = false;
    assert(!round_settlement_can_be_retained(watched_settlement));
    watched_settlement["result_frame_saved"] = true;
    watched_settlement["result_joint"] = true;
    assert(!round_settlement_can_be_retained(watched_settlement));
    auto victory = watched_settlement;
    victory["result_joint"] = false;
    victory["result_interrupted"] = false;
    victory["result_completed"] = true;
    victory["return_home_verified"] = true;
    victory["passed_rounds"] = 14;
    victory["result_difficulty"] = 3;
    victory["result_difficulty_known"] = true;
    assert(verified_solo_victory(victory, "independent", 3));
    assert(!verified_solo_victory(victory, "independent", 4));
    assert(!verified_solo_victory(victory, "joint", 3));
    for (const auto* flag : { "battle_seen", "return_home_verified", "result_frame_saved", "passed_rounds_known" }) {
        auto incomplete = victory;
        incomplete[flag] = false;
        assert(!verified_solo_victory(incomplete, "independent", 3));
    }
    victory["passed_rounds"] = 15;
    assert(!verified_solo_victory(victory, "independent", 3));
    victory["result_difficulty"] = 20;
    assert(verified_solo_victory(victory, "independent", 20));
    victory["result_difficulty_known"] = false;
    victory["result_difficulty_name"] = "极速模拟";
    victory["passed_rounds"] = 9;
    assert(verified_solo_victory(victory, "quick", 0));

    assert(!lifecycle_login_start_game_enabled("joint"));
    assert(lifecycle_login_start_game_enabled("quick"));
    assert(lifecycle_login_start_game_enabled("independent"));
    LifecyclePage strict;
    strict.issued_start_required = true;
    strict.personal_selected = true;
    strict.page = "lobby";
    assert(lifecycle_next(strict) == "MiniGame@SPA@OpenIndependentStart");
    strict.reconnect_available = true;
    assert(lifecycle_next(strict).empty());
    strict.reconnect_available = false;
    for (const auto* page : { "run_setup", "preparation", "battle", "result" }) {
        strict.page = page;
        assert(lifecycle_next(strict).empty());
    }
    strict.page = "preparation";
    strict.issued_start_required = false;
    assert(lifecycle_next(strict) == "MiniGame@SPA@LifecycleExecute");
    for (const auto* mode : { "independent", "quick", "joint" }) {
        LifecyclePage page;
        page.mode = mode;
        // Disabling joint process startup must not disable UI login recovery
        // or replace the existing-match reconnect path with a fresh search.
        page.page = "authentication_expired";
        assert(lifecycle_next(page) == "MiniGame@SPA@LifecycleLogin");
        page.page = "data_updated";
        assert(lifecycle_next(page) == "MiniGame@SPA@LifecycleAckSync");
        page.page = "lobby";
        page.personal_selected = true;
        page.joint_selected = true;
        const auto first_start = lifecycle_next(page);
        assert(
            first_start == (page.mode == "joint"   ? "MiniGame@SPA@LifecycleOpenJoint"
                            : page.mode == "quick" ? "MiniGame@SPA@LifecycleQuickStart"
                                                   : "MiniGame@SPA@OpenIndependentStart"));

        // Missing settlement cannot silently become another attempt, even
        // when the selected mode changed or a tab's label was not readable.
        page.battle_seen = true;
        assert(lifecycle_next(page).empty());
        page.personal_selected = false;
        page.joint_selected = false;
        assert(lifecycle_next(page).empty());

        // A genuine reconnect restores the same match, never a new search.
        page.reconnect_available = true;
        if (page.mode == "joint") {
            assert(lifecycle_next(page) == "MiniGame@SPA@ReconnectCurrent");
        }
        else {
            assert(lifecycle_next(page) == "MiniGame@SPA@LifecycleSelectPersonal");
            page.personal_selected = true;
            assert(lifecycle_next(page) == "MiniGame@SPA@ReconnectSolo");
        }
        page.reconnect_available = false;

        // A failed game is a settled attempt, not a win. Only its verified
        // settlement followed by the lobby permits the batch to continue.
        json::value result = json::object { { "result_text_verified", true },
                                            { "contradictory", false },
                                            { "result_completed", false },
                                            { "result_interrupted", true },
                                            { "return_home_verified", false } };
        assert(!settled_run_returned(result));
        page.result_recorded = true;
        assert(lifecycle_next(page) == "MiniGame@SPA@LifecycleComplete");
        result["return_home_verified"] = true;
        assert(settled_run_returned(result));
        result["contradictory"] = true;
        assert(!settled_run_returned(result));

        page.page = "result";
        page.result_verified = false;
        assert(lifecycle_next(page) == "MiniGame@SPA@LifecycleWait");
        page.result_verified = true;
        assert(lifecycle_next(page) == "MiniGame@SPA@ReturnResultHome");

        // Taking over an ongoing battle still enters the same executor.
        page.page = "battle";
        page.result_recorded = false;
        assert(lifecycle_next(page) == "MiniGame@SPA@LifecycleExecute");
    }
    std::cout << "SPA lifecycle batch settlement guards passed\n";
}
