#include "Task/MiniGame/SPA/SPAAcquisition.h"
#include "Task/MiniGame/SPA/SPABenchSalePolicy.h"
#include "Task/MiniGame/SPA/SPABossPolicy.h"
#include "Task/MiniGame/SPA/SPABountyPolicy.h"
#include "Task/MiniGame/SPA/SPACombatRole.h"
#include "Task/MiniGame/SPA/SPACombatStats.h"
#include "Task/MiniGame/SPA/SPADeploymentReview.h"
#include "Task/MiniGame/SPA/SPADifficultyPolicy.h"
#include "Task/MiniGame/SPA/SPAEconomyPolicy.h"
#include "Task/MiniGame/SPA/SPAEgirPolicy.h"
#include "Task/MiniGame/SPA/SPAEngagementPolicy.h"
#include "Task/MiniGame/SPA/SPAEquipmentCatalogue.h"
#include "Task/MiniGame/SPA/SPAEquipmentPolicy.h"
#include "Task/MiniGame/SPA/SPAGrowth.h"
#include "Task/MiniGame/SPA/SPAGuideRoutes.h"
#include "Task/MiniGame/SPA/SPALifecycle.h"
#include "Task/MiniGame/SPA/SPAModePolicy.h"
#include "Task/MiniGame/SPA/SPAOpeningControls.h"
#include "Task/MiniGame/SPA/SPAPlacementPolicy.h"
#include "Task/MiniGame/SPA/SPAPreparationDeadline.h"
#include "Task/MiniGame/SPA/SPAPreparationPolicy.h"
#include "Task/MiniGame/SPA/SPAReplacementPolicy.h"
#include "Task/MiniGame/SPA/SPAResultPolicy.h"
#include "Task/MiniGame/SPA/SPARunBatch.h"
#include "Task/MiniGame/SPA/SPASkillPolicy.h"
#include "Task/MiniGame/SPA/SPASpatialGrowthPolicy.h"
#include "Task/MiniGame/SPA/SPAStrategyCatalogue.h"
#include "Task/MiniGame/SPA/SPAStrategyEligibility.h"
#include "Task/MiniGame/SPA/SPASummonPolicy.h"
#include "Task/MiniGame/SPA/SPATerrainModel.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <opencv2/imgcodecs.hpp>
#include <tuple>

#ifdef _WIN32
#include <crtdbg.h>
#include <cstdlib>
#endif

#include "Task/MiniGame/SPA/SPAFormation.h"
#include "Task/MiniGame/SPA/SPAObservation.h"
#include "Task/MiniGame/SPA/SPAPlanner.h"
#include "Task/MiniGame/SPA/SPAProgress.h"
#include "Task/MiniGame/SPA/SPARouteProfile.h"

int main()
{
#ifdef _WIN32
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    using namespace asst::spa;
    assert(valid_run_count(1) && valid_run_count(99'999));
    assert(!valid_run_count(0) && !valid_run_count(100'000));
    json::value batch_result = json::object { { "return_home_verified", true },
                                              { "result_text_verified", true },
                                              { "contradictory", false },
                                              { "result_completed", false },
                                              { "result_interrupted", true } };
    assert(settled_run_returned(batch_result)); // A failed game counts once, not as a win.
    batch_result["result_completed"] = true;
    assert(!settled_run_returned(batch_result));
    batch_result["result_interrupted"] = false;
    batch_result["return_home_verified"] = false;
    assert(!settled_run_returned(batch_result));
    const auto fresh = fresh_run_parameters(
        json::object { { "mode", "quick" },
                       { "difficulty", -1 },
                       { "roster", json::array { "stale" } },
                       { "registered_grid", "stale" },
                       { "promotion_pending", "stale" },
                       { "selected_strategy_id", "stale" },
                       { "shop_level", 6 },
                       { "strategy_id", "band_mech" },
                       { "run_count", 99'999 } });
    assert(fresh.get("run_count", 0) == 1 && fresh.get("mode", std::string()) == "quick");
    assert(fresh.at("roster").as_array().empty() && !fresh.contains("registered_grid"));
    assert(
        !fresh.contains("promotion_pending") && !fresh.contains("selected_strategy_id") &&
        !fresh.contains("shop_level"));
    assert(fresh.get("strategy_id", std::string()) == "band_mech");
    const std::vector<TextObservation> spectator { { "返回战场", .99, 25, 188 },
                                                   { "回合", .99, 450, 20 },
                                                   { "准备就绪", .99, 1130, 112 },
                                                   { "剩余可放置角色：0", .99, 1090, 460 } };
    assert(observe_page(spectator, {}).page == Page::Spectating);
    assert(observe_page({ { "取消准备", .99, 1130, 110 } }, {}).page == Page::ReadyWaiting);
    assert(observe_page({ { "取消准备", .6, 1130, 110 } }, {}).page != Page::ReadyWaiting);
    for (int difficulty = 1; difficulty <= 20; ++difficulty) {
        const auto mode = season_mode_pact_rules("independent", difficulty);
        assert(mode && mode->id == "mode_single_" + std::to_string(difficulty));
        const bool inactive_egir =
            std::find(mode->inactive.begin(), mode->inactive.end(), "阿戈尔") != mode->inactive.end();
        const bool active_egir = std::find(mode->active.begin(), mode->active.end(), "阿戈尔") != mode->active.end();
        assert(inactive_egir == (difficulty <= 4) && active_egir == (difficulty >= 5));
    }
    assert(!season_mode_pact_rules("independent", 0));
    assert(!season_mode_pact_rules("independent", 21));
    assert(!season_mode_pact_rules("unknown", 14));
    const auto quick_mode = season_mode_pact_rules("quick", 1);
    assert(
        quick_mode &&
        std::find(quick_mode->inactive.begin(), quick_mode->inactive.end(), "阿戈尔") != quick_mode->inactive.end());
    const auto joint_standard = season_mode_pact_rules("joint", 1);
    const auto joint_danger = season_mode_pact_rules("joint", 2);
    assert(joint_standard && joint_standard->id == "mode_multi_funny" && joint_standard->inactive.size() == 8);
    assert(joint_danger && joint_danger->id == "mode_multi_normal" && joint_danger->inactive.empty());
    assert(!season_mode_pact_rules("joint", 5));
    const auto low_mode_bans = effective_mode_pact_bans("independent", 3, { "炎" }, { "谢拉格", "炎" });
    assert(std::count(low_mode_bans.begin(), low_mode_bans.end(), "炎") == 1);
    assert(std::find(low_mode_bans.begin(), low_mode_bans.end(), "阿戈尔") != low_mode_bans.end());
    const auto high_mode_bans = effective_mode_pact_bans("independent", 14, { "炎" }, { "谢拉格", "炎" });
    assert((high_mode_bans == std::vector<std::string> { "炎", "谢拉格" }));
    assert((effective_mode_pact_bans("independent", 0, {}, { "阿戈尔" }) == std::vector<std::string> { "阿戈尔" }));
    const std::vector<std::string> panel_names { "幽灵鲨", "歌蕾蒂娅" };
    assert(operator_panel_identity({ { "幽灵鲨", .99, 2, 185 } }, panel_names) == "幽灵鲨");
    assert(
        operator_panel_identity({ { "歌蕾蒂娅", .99, 2, 185 }, { "幽灵鲨", .99, 80, 275 } }, panel_names) ==
        "歌蕾蒂娅");
    assert(!operator_panel_identity({ { "幽灵鲨", .89, 2, 185 } }, panel_names));
    assert(!operator_panel_identity({ { "幽灵鲨", std::numeric_limits<double>::quiet_NaN(), 2, 185 } }, panel_names));
    assert(!operator_panel_identity({ { "Unknown", .99, 2, 185 } }, panel_names));
    assert(!operator_panel_identity({ { "幽灵鲨", .99, 2, 150 } }, panel_names));
    assert(!operator_panel_identity({ { "幽灵鲨", .99, 2, 185 }, { "歌蕾蒂娅", .99, 2, 205 } }, panel_names));
    assert(operator_panel_identity({ { "幽灵鲨", .99, 2, 185 }, { "幽灵鲨", .98, 2, 185 } }, panel_names) == "幽灵鲨");
    assert(
        operator_panel_identity({ { "幽灵", .999806, 15, 184 }, { "鲨", .993617, 54, 180 } }, panel_names) == "幽灵鲨");
    assert(!operator_panel_identity({ { "幽灵", .99, 15, 184 } }, panel_names));
    assert(!operator_panel_identity({ { "幽灵", .99, 15, 184 }, { "鲨", .99, 54, 202 } }, panel_names));
    assert(!operator_panel_identity({ { "幽灵", .99, 15, 184 }, { "鲨", .89, 54, 180 } }, panel_names));
    // Actual selected-panel OCR: normal Skadi LV1, promoted Chestnut LV50,
    // promoted Specter the Unchained LV60. Digits need the separate LV ROI.
    const std::vector<std::string> form_names { "斯卡蒂", "褐果", "归溟幽灵鲨", "幽灵鲨" };
    const auto normal_skadi =
        observe_operator_card({ { "斯卡蒂", .99, 2, 185 }, { "1", .994948, 111, 213 } }, form_names);
    assert(normal_skadi && !normal_skadi->elite && normal_skadi->training_level == 1);
    const auto elite_chestnut =
        observe_operator_card({ { "褐果", .99, 2, 185 }, { "50", .99626, 111, 213 } }, form_names);
    assert(elite_chestnut && elite_chestnut->elite && elite_chestnut->training_level == 50);
    const auto elite_shadow =
        observe_operator_card({ { "归溟幽灵鲨", .99, 2, 185 }, { "60", .999835, 111, 213 } }, form_names);
    assert(elite_shadow && elite_shadow->elite);
    assert(!observe_operator_card({ { "斯卡蒂", .99, 2, 185 }, { "17", .99, 111, 213 } }, form_names));
    assert(!observe_operator_card({ { "斯卡蒂", .99, 2, 185 }, { "1", .89, 111, 213 } }, form_names));
    assert(!observe_operator_card({ { "斯卡蒂", .99, 2, 185 }, { "1", .99, 111, 270 } }, form_names));
    assert(!observe_operator_card({ { "斯卡蒂", .99, 2, 185 }, { "LV1", .99, 111, 213 } }, form_names));
    assert(!observe_operator_card({ { "斯卡蒂", .99, 2, 185 }, { "O1", .99, 111, 213 } }, form_names));
    assert(!observe_operator_card({ { "斯卡蒂", .99, 2, 185 }, { "0", .99, 111, 213 } }, form_names));
    assert(!observe_operator_card({ { "1", .99, 111, 213 } }, form_names));
    assert(!observe_operator_card(
        { { "斯卡蒂", .99, 2, 185 }, { "归溟幽灵鲨", .99, 2, 185 }, { "1", .99, 111, 213 } },
        form_names));
    assert(!observe_operator_card(
        { { "斯卡蒂", .99, 2, 185 }, { "1", .99, 111, 213 }, { "60", .99, 111, 213 } },
        form_names));
    assert(!observe_operator_card(
        { { "斯卡蒂", .99, 2, 185 }, { "1", std::numeric_limits<double>::quiet_NaN(), 111, 213 } },
        form_names));
    const auto normal_ghost = observe_operator_card({ { "幽灵鲨", .99, 2, 185 }, { "55", .99, 111, 213 } }, form_names);
    assert(normal_ghost && !normal_ghost->elite);
    const auto elite_ghost = observe_operator_card({ { "幽灵鲨", .99, 2, 185 }, { "50", .99, 111, 213 } }, form_names);
    assert(elite_ghost && elite_ghost->elite);
    const ObservedOperatorInventory phone_before { true, { { "幽灵鲨", { 2, 1 } }, { "斯卡蒂", { 1, 0 } } } };
    const ObservedOperatorInventory phone_after { true, { { "幽灵鲨", { 0, 2 } }, { "斯卡蒂", { 1, 0 } } } };
    const auto promoted_phone = verify_pact_recruitment_delta(phone_before, phone_after, "阿戈尔", 1);
    assert(promoted_phone.verified);
    assert((promoted_phone.gained_initial_operators == std::vector<std::string> { "幽灵鲨" }));
    assert((promoted_phone.promoted_operators == std::vector<std::string> { "幽灵鲨" }));
    const ObservedOperatorInventory elite_only { true, { { "幽灵鲨", { 0, 1 } } } };
    const ObservedOperatorInventory mixed_forms { true, { { "幽灵鲨", { 1, 1 } } } };
    assert(verify_pact_recruitment_delta(elite_only, mixed_forms, "阿戈尔", 1).verified);
    const ObservedOperatorInventory double_phone_after { true, { { "幽灵鲨", { 1, 2 } }, { "斯卡蒂", { 1, 0 } } } };
    const auto double_phone = verify_pact_recruitment_delta(phone_before, double_phone_after, "阿戈尔", 2);
    assert(double_phone.verified && double_phone.gained_initial_operators.size() == 2);
    assert(double_phone.promoted_operators.size() == 1);
    const ObservedOperatorInventory two_names_after { true, { { "幽灵鲨", { 0, 2 } }, { "斯卡蒂", { 2, 0 } } } };
    assert(verify_pact_recruitment_delta(phone_before, two_names_after, "阿戈尔", 2).verified);
    const ObservedOperatorInventory empty_phone_inventory { true, {} };
    const ObservedOperatorInventory astesia_promoted { true, { { "星极", { 0, 1 } } } };
    assert(verify_pact_recruitment_delta(empty_phone_inventory, astesia_promoted, "奇迹", 2).verified);
    assert(!verify_pact_recruitment_delta(empty_phone_inventory, astesia_promoted, "阿戈尔", 2).verified);
    auto incomplete_phone = phone_after;
    incomplete_phone.complete = false;
    assert(!verify_pact_recruitment_delta(phone_before, incomplete_phone, "阿戈尔", 1).verified);
    assert(!verify_pact_recruitment_delta(incomplete_phone, phone_after, "阿戈尔", 1).verified);
    assert(!verify_pact_recruitment_delta(phone_before, phone_before, "阿戈尔", 1).verified);
    assert(!verify_pact_recruitment_delta(phone_after, phone_before, "阿戈尔", 1).verified);
    assert(!verify_pact_recruitment_delta(phone_before, phone_after, "阿戈尔", 2).verified);
    assert(!verify_pact_recruitment_delta(phone_before, phone_after, "", 1).verified);
    assert(!verify_pact_recruitment_delta(phone_before, phone_after, "阿戈尔", 0).verified);
    assert(!verify_pact_recruitment_delta(phone_before, phone_after, "阿戈尔", 3).verified);
    auto unresolved_phone = phone_after;
    unresolved_phone.members["幽灵鲨"] = { 3, 1 };
    assert(!verify_pact_recruitment_delta(phone_before, unresolved_phone, "阿戈尔", 1).verified);
    auto deleted_unrelated = phone_after;
    deleted_unrelated.members.erase("斯卡蒂");
    assert(!verify_pact_recruitment_delta(phone_before, deleted_unrelated, "阿戈尔", 1).verified);
    auto invalid_phone = phone_after;
    invalid_phone.members["幽灵鲨"].initial = -1;
    assert(!verify_pact_recruitment_delta(phone_before, invalid_phone, "阿戈尔", 1).verified);
    OwnedOperator exact_mixed;
    assert(set_owned_form_counts(exact_mixed, { 2, 1 }));
    assert(exact_mixed.copies == 3 && exact_mixed.elite);
    assert(add_initial_copies(exact_mixed));
    assert(exact_mixed.copies == 4 && owned_form_counts(exact_mixed)->initial == 3);
    assert(promote_initial_copies(exact_mixed, 3));
    assert(exact_mixed.copies == 2 && owned_form_counts(exact_mixed)->initial == 0);
    assert(owned_form_counts(exact_mixed)->elite == 2);
    std::map<std::string, OwnedOperator> recounted_roster { { "幽灵鲨", { 1, true, true } },
                                                            { "斯卡蒂", { 2, false, true } },
                                                            { "水月", { 2, false, false } },
                                                            { "安哲拉", { 1, false, false } },
                                                            { "空弦", { 1, false, false } } };
    // Two of three initial Ghost bodies occupied the field before synthesis.
    // Two remaining deployed bodies and seven total bodies imply five bench
    // bodies; subtracting two bench copies during synthesis would claim four.
    assert(bench_population_from_roster(recounted_roster, 2) == 5);
    assert(!bench_population_from_roster(recounted_roster, 8));
    assert(!bench_population_from_roster(recounted_roster, -1));
    assert(!bench_population_from_roster(recounted_roster, 9));
    recounted_roster["幽灵鲨"] = { 2, true, true };
    assert(!bench_population_from_roster(recounted_roster, 2));
    recounted_roster["幽灵鲨"].known_physical_copies = 2;
    assert(bench_population_from_roster(recounted_roster, 2) == 6);
    recounted_roster["幽灵鲨"] = { 19, true, true };
    assert(!bench_population_from_roster(recounted_roster, 2));
    assert(!promote_initial_copies(exact_mixed, 3));
    assert(!add_initial_copies(exact_mixed, 0));
    assert(!set_owned_form_counts(exact_mixed, { -1, 1 }));
    assert(exact_mixed.copies == 2 && owned_form_counts(exact_mixed)->elite == 2);
    OwnedOperator legacy_ambiguous { 2, true, true };
    assert(!owned_form_counts(legacy_ambiguous));
    assert(!add_initial_copies(legacy_ambiguous));
    assert(!promote_initial_copies(legacy_ambiguous, 3));
    auto mixed_row = owned_operator_snapshot("幽灵鲨", exact_mixed);
    assert(mixed_row.at("initial_copies").as_integer() == 0);
    assert(mixed_row.at("elite_copies").as_integer() == 2);
    assert(mixed_row.at("physical_copies").as_integer() == 2);
    OwnedOperator restored_forms;
    assert(restore_owned_operator(mixed_row, restored_forms));
    assert(restored_forms.copies == 2 && owned_form_counts(restored_forms)->elite == 2);
    assert(restore_owned_operator(owned_operator_snapshot("幽灵鲨", legacy_ambiguous), restored_forms));
    assert(restored_forms.copies == 2 && !owned_form_counts(restored_forms));
    assert(restore_owned_operator(
        json::object { { "initial_copies", 0 }, { "elite", true }, { "deployed", true } },
        restored_forms));
    assert(restored_forms.copies == 1 && owned_form_counts(restored_forms)->elite == 1);
    for (const auto& bad_row : std::vector<json::value> {
             json::array {},
             json::object { { "initial_copies", 1 }, { "elite", true } },
             json::object { { "initial_copies", -1 }, { "elite", false }, { "deployed", false } },
             json::object { { "initial_copies", 1.5 }, { "elite", false }, { "deployed", false } },
             json::object { { "initial_copies", "1" }, { "elite", false }, { "deployed", false } },
             json::object { { "initial_copies", 1 },
                            { "elite_copies", true },
                            { "elite", true },
                            { "deployed", false } },
             json::object { { "initial_copies", 1 }, { "elite_copies", 1 }, { "elite", false }, { "deployed", false } },
             json::object { { "initial_copies", 0 }, { "elite", false }, { "deployed", true } },
             json::object { { "initial_copies", 1 },
                            { "elite_copies", 1 },
                            { "physical_copies", 1 },
                            { "elite", true },
                            { "deployed", false } },
             json::object { { "initial_copies", 1 },
                            { "elite_copies", 1 },
                            { "form_counts_known", false },
                            { "elite", true },
                            { "deployed", false } },
             json::object { { "initial_copies", 20 },
                            { "elite_copies", 1 },
                            { "elite", true },
                            { "deployed", false } } }) {
        assert(!restore_owned_operator(bad_row, restored_forms));
        assert(restored_forms.copies == 1 && owned_form_counts(restored_forms)->elite == 1);
    }
    invalid_phone.members["幽灵鲨"] = { 0, 19 };
    assert(!verify_pact_recruitment_delta(phone_before, invalid_phone, "阿戈尔", 1).verified);
    invalid_phone.members["unknown"] = { 1, 0 };
    assert(!verify_pact_recruitment_delta(phone_before, invalid_phone, "阿戈尔", 1).verified);
    // Several configured carry candidates used to be labeled protected at
    // once, so a valid Ghost -> dollkeeper -> Skadi chain raised false alarms.
    std::vector<EgirEntity> current_egir_roles {
        { "ghost", "幽灵鲨", { 3, 4 }, 'E', true, 1, EgirIntent::Core, 100, 2 },
        { "dollkeeper", "归溟幽灵鲨", { 4, 4 }, 'E', true, 4, EgirIntent::Core, 200, 2 },
        { "skadi", "斯卡蒂", { 5, 4 }, 'E', true, 3, EgirIntent::Core, 300, 2 }
    };
    assert(!project_egir_mark_closure(current_egir_roles, true).problems.empty());
    assert(designate_egir_output(current_egir_roles) == "ghost");
    assert(current_egir_roles[1].intent == EgirIntent::Relay && current_egir_roles[2].intent == EgirIntent::Relay);
    assert(project_egir_mark_closure(current_egir_roles, true).problems.empty());
    current_egir_roles.push_back({ "terminal", "克莱门莎", { 2, 4 }, 'E', true, 6, EgirIntent::Relay, 500, 2 });
    assert(designate_egir_output(current_egir_roles) == "terminal");
    assert(current_egir_roles.front().intent == EgirIntent::Relay);
    assert(project_egir_mark_closure(current_egir_roles, true).problems.empty());
    current_egir_roles.push_back({ "terminal-copy", "克莱门莎", { 7, 3 }, 'E', true, 6, EgirIntent::Relay, 500, 2 });
    assert(!designate_egir_output(current_egir_roles));
    assert(std::none_of(current_egir_roles.begin(), current_egir_roles.end(), [](const auto& unit) {
        return unit.intent == EgirIntent::Core;
    }));
    auto dollkeeper_only = std::vector<EgirEntity> { current_egir_roles[1] };
    assert(!designate_egir_output(dollkeeper_only));
    // An executable Egir target has its own growth/core/material objective.
    // A ready target must be stable instead of inviting another rearrangement.
    std::vector<EgirPlacementUnit> egir_placement;
    const std::vector<EgirEntity> egir_placement_entities {
        { "Ghost", "Ghost", { 5, 4 }, 'W', true, 1, EgirIntent::Core, 100, 2 },
        { "Gladiia", "Gladiia", { 3, 4 }, 'N', true, 2, EgirIntent::Relay, 200, 1 },
        { "Skadi", "Skadi", { 4, 4 }, 'E', true, 3, EgirIntent::Relay, 300, 2 },
        { "Donor", "Donor", { 6, 4 }, 'W', false, 6, EgirIntent::Material, 400, 3 },
        { "Medic", "Medic", { 3, 3 }, 'E', false, 2, EgirIntent::ProtectedSupport, 80, 1 }
    };
    for (const auto& entity : egir_placement_entities) {
        EgirPlacementUnit input;
        input.entity = entity;
        auto& unit = input.deployment;
        unit.id = entity.id;
        unit.cell = entity.cell;
        unit.facing = entity.facing;
        unit.block_count = entity.block_count.value();
        unit.frontline = entity.id != "Medic";
        unit.healer = entity.id == "Medic";
        unit.damage_output = !unit.healer;
        unit.range_known = true;
        unit.local_range = unit.healer ? std::vector<BoardCell> { { 0, -1 }, { 1, -1 } }
                                       : std::vector<BoardCell> { { 0, 0 }, { 1, 0 } };
        for (int col = 3; col <= 9; ++col) {
            unit.placement_cells.emplace_back(col, 4);
        }
        if (unit.healer) {
            unit.placement_cells.push_back(entity.cell);
        }
        input.core_priority = entity.id == "Ghost" ? 10 : 1;
        input.expendable_material = entity.id == "Donor";
        input.same_row_recruitment_members = entity.id == "Gladiia" ? 3 : 0;
        egir_placement.push_back(std::move(input));
    }
    EgirFormationContext egir_request;
    egir_request.pact_active = true;
    egir_request.home = { 2, 4 };
    egir_request.lanes = { { "upper",
                             { { 9, 1 },
                               { 8, 1 },
                               { 7, 1 },
                               { 6, 1 },
                               { 5, 1 },
                               { 5, 2 },
                               { 5, 3 },
                               { 5, 4 },
                               { 4, 4 },
                               { 3, 4 },
                               { 2, 4 } } },
                           { "lower",
                             { { 9, 4 }, { 8, 4 }, { 7, 4 }, { 6, 4 }, { 5, 4 }, { 4, 4 }, { 3, 4 }, { 2, 4 } } } };
    const auto egir_target = plan_egir_formation(egir_placement, egir_request);
    assert(egir_target.input_complete && egir_target.found && !egir_target.current_season_semantics_verified);
    assert(egir_target.core == "Ghost" && egir_target.chain.size() == 4 && egir_target.chain.back() == "Donor");
    assert((egir_target.units.front().cell == BoardCell { 3, 4 }));
    assert(egir_target.conditional_consumption.conditional_unique_target_layers == 11);
    assert(egir_target.conditional_consumption.problems.empty());
    assert(range_covers(egir_target.units.back(), egir_target.units.front().cell));
    // Current failure: a dollkeeper relay outranked Ghost, leaving Ghost at
    // the red entrance. An explicit head prefix must survive that priority
    // conflict, extend beyond the first three, and refuse an unsafe fallback.
    auto ordered_egir = egir_placement;
    ordered_egir[1].core_priority = 1000;
    auto ordered_request = egir_request;
    ordered_request.defended_chain_prefix = { "Ghost", "Gladiia", "Skadi" };
    const auto ordered_plan = plan_egir_formation(ordered_egir, ordered_request);
    assert(ordered_plan.found && ordered_plan.core == "Ghost");
    assert((ordered_plan.chain == std::vector<std::string> { "Ghost", "Gladiia", "Skadi", "Donor" }));
    for (size_t index = 0; index < 3; ++index) {
        assert((ordered_plan.units[index].cell == BoardCell(3 + static_cast<int>(index), 4)));
        assert(ordered_plan.units[index].facing == 'E');
    }
    auto blocked_anchor = ordered_request;
    blocked_anchor.target_admissible = [](const auto&) {
        return false;
    };
    assert(!plan_egir_formation(ordered_egir, blocked_anchor).found);
    // M04's road ends after the second home-adjacent tile. The preferred
    // third member is reachable by turning north, not by occupying a gap.
    auto bent_egir = egir_placement;
    const std::vector<BoardCell> bent_initial { { 4, 4 }, { 5, 3 }, { 6, 3 }, { 7, 3 }, { 3, 3 } };
    for (size_t i = 0; i < bent_egir.size(); ++i) {
        auto& unit = bent_egir[i];
        unit.entity.cell = unit.deployment.cell = bent_initial[i];
        unit.entity.facing = unit.deployment.facing = 'E';
        unit.deployment.placement_cells.clear();
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 11; ++col) {
                if (terrain_allows_deployment("m04", { col, row }, unit.deployment.frontline)) {
                    unit.deployment.placement_cells.emplace_back(col, row);
                }
            }
        }
    }
    auto bent_request = egir_request;
    bent_request.lanes = terrain_layout_model("m04")->lanes;
    for (const std::string name : { "Ghost", "Gladiia", "Skadi" }) {
        assert(append_egir_defended_name(bent_request.defended_chain_names, bent_egir, name, bent_request.home));
    }
    const auto bent_plan = plan_egir_formation(bent_egir, bent_request);
    assert(bent_plan.found && bent_plan.core == "Ghost" && bent_plan.chain.size() == 4);
    assert((bent_plan.units[0].cell == BoardCell(3, 4)) && bent_plan.units[0].facing == 'E');
    assert((bent_plan.units[1].cell == BoardCell(4, 4)) && bent_plan.units[1].facing == 'N');
    assert((bent_plan.units[2].cell == BoardCell(4, 3)));
    assert(bent_plan.conditional_consumption.problems.empty());
    std::vector<DeploymentUnit> bent_current;
    for (const auto& unit : bent_egir) {
        bent_current.push_back(unit.deployment);
    }
    const auto bent_sequence = sequence_deployment(bent_current, bent_plan.units, 1500);
    assert(bent_sequence.complete && bent_sequence.steps.size() <= 16);
    auto mirrored_bent = bent_egir;
    auto mirrored_bent_request = bent_request;
    mirrored_bent_request.home.first = 10 - mirrored_bent_request.home.first;
    mirrored_bent_request.chain_facing = 'W';
    mirrored_bent_request.defended_chain_names.clear();
    for (auto& unit : mirrored_bent) {
        unit.entity.cell.first = 10 - unit.entity.cell.first;
        unit.deployment.cell = unit.entity.cell;
        unit.entity.facing = unit.deployment.facing = 'W';
        for (auto& cell : unit.deployment.placement_cells) {
            cell.first = 10 - cell.first;
        }
    }
    for (auto& lane : mirrored_bent_request.lanes) {
        for (auto& cell : lane.path) {
            cell.first = 10 - cell.first;
        }
    }
    for (const std::string name : { "Ghost", "Gladiia", "Skadi" }) {
        assert(append_egir_defended_name(
            mirrored_bent_request.defended_chain_names,
            mirrored_bent,
            name,
            mirrored_bent_request.home,
            'W'));
    }
    const auto mirrored_bent_plan = plan_egir_formation(mirrored_bent, mirrored_bent_request);
    assert(mirrored_bent_plan.found && mirrored_bent_plan.core == "Ghost");
    assert((mirrored_bent_plan.units[0].cell == BoardCell(7, 4)) && mirrored_bent_plan.units[0].facing == 'W');
    assert((mirrored_bent_plan.units[1].cell == BoardCell(6, 4)) && mirrored_bent_plan.units[1].facing == 'N');
    auto duplicate_bent = bent_egir;
    duplicate_bent[1].entity.cell = duplicate_bent[1].deployment.cell = { 8, 3 };
    duplicate_bent[2].entity.cell = duplicate_bent[2].deployment.cell = { 5, 3 };
    duplicate_bent[2].deployment.movable = false;
    auto fixed_corner_skadi = duplicate_bent[2];
    fixed_corner_skadi.entity.id = fixed_corner_skadi.deployment.id = "Skadi-corner-copy";
    fixed_corner_skadi.entity.cell = fixed_corner_skadi.deployment.cell = { 4, 3 };
    duplicate_bent.push_back(fixed_corner_skadi);
    auto duplicate_bent_request = bent_request;
    duplicate_bent_request.defended_chain_names.clear();
    for (const std::string name : { "Ghost", "Gladiia", "Skadi" }) {
        assert(append_egir_defended_name(
            duplicate_bent_request.defended_chain_names,
            duplicate_bent,
            name,
            duplicate_bent_request.home));
    }
    const auto duplicate_bent_plan = plan_egir_formation(duplicate_bent, duplicate_bent_request);
    assert(duplicate_bent_plan.found && duplicate_bent_plan.core == "Ghost");
    assert(duplicate_bent_plan.chain[2] == "Skadi-corner-copy");
    assert((duplicate_bent_plan.units[2].cell == BoardCell(5, 3)));
    assert((duplicate_bent_plan.units.back().cell == BoardCell(4, 3)));
    assert(duplicate_bent_plan.conditional_consumption.distinct_egir_members == 3);
    std::vector<DeploymentUnit> duplicate_bent_current;
    for (const auto& unit : duplicate_bent) {
        duplicate_bent_current.push_back(unit.deployment);
    }
    const auto duplicate_bent_sequence = sequence_deployment(duplicate_bent_current, duplicate_bent_plan.units, 1500);
    assert(duplicate_bent_sequence.complete && duplicate_bent_sequence.steps.size() <= 16);
    assert(
        std::none_of(duplicate_bent_sequence.steps.begin(), duplicate_bent_sequence.steps.end(), [](const auto& step) {
            return step.unit == "Skadi" || step.unit == "Skadi-corner-copy";
        }));
    // R8 live: two fixed Skadi copies were outside the preferred third
    // cell. Requiring the first copy there rejected every Ghost/relay chain.
    auto fixed_duplicate_egir = ordered_egir;
    fixed_duplicate_egir[2].entity.cell = fixed_duplicate_egir[2].deployment.cell = { 5, 3 };
    fixed_duplicate_egir[2].deployment.placement_cells.push_back({ 5, 3 });
    fixed_duplicate_egir[2].deployment.movable = false;
    auto duplicate_skadi = fixed_duplicate_egir[2];
    duplicate_skadi.entity.id = duplicate_skadi.deployment.id = "Skadi-copy";
    duplicate_skadi.entity.cell = duplicate_skadi.deployment.cell = { 7, 4 };
    fixed_duplicate_egir.push_back(duplicate_skadi);
    auto fixed_request = ordered_request;
    fixed_request.defended_chain_prefix.clear();
    assert(append_egir_defended_member(fixed_request.defended_chain_prefix, fixed_duplicate_egir, "Ghost", { 2, 4 }));
    assert(append_egir_defended_member(fixed_request.defended_chain_prefix, fixed_duplicate_egir, "Gladiia", { 2, 4 }));
    assert(!append_egir_defended_member(fixed_request.defended_chain_prefix, fixed_duplicate_egir, "Skadi", { 2, 4 }));
    const auto fixed_plan = plan_egir_formation(fixed_duplicate_egir, fixed_request);
    assert(fixed_plan.found && fixed_plan.core == "Ghost");
    assert((fixed_plan.units.front().cell == BoardCell(3, 4)));
    assert((fixed_plan.units[2].cell == BoardCell(5, 3)));
    assert((fixed_plan.units.back().cell == BoardCell(7, 4)));
    assert(fixed_plan.conditional_consumption.problems.empty());
    auto terminal_output = ordered_egir.front();
    terminal_output.entity.id = terminal_output.entity.name = terminal_output.deployment.id = "Terminal";
    terminal_output.entity.cell = terminal_output.deployment.cell = { 7, 4 };
    terminal_output.entity.facing = terminal_output.deployment.facing = 'E';
    terminal_output.core_priority = 2000;
    ordered_egir.front().entity.intent = EgirIntent::Relay;
    ordered_egir.push_back(terminal_output);
    ordered_request.defended_chain_prefix = { "Terminal", "Gladiia", "Skadi", "Ghost" };
    const auto terminal_plan = plan_egir_formation(ordered_egir, ordered_request);
    assert(terminal_plan.found && terminal_plan.core == "Terminal");
    assert((terminal_plan.units.back().cell == BoardCell(3, 4)) && terminal_plan.units.back().facing == 'E');
    assert((terminal_plan.chain == std::vector<std::string> { "Terminal", "Gladiia", "Skadi", "Ghost", "Donor" }));
    auto boss_short_chain = ordered_egir;
    for (auto& unit : boss_short_chain) {
        std::erase(unit.deployment.placement_cells, BoardCell(9, 4));
    }
    boss_short_chain.back().required_cell = BoardCell(7, 4);
    auto boss_prefix_request = ordered_request;
    boss_prefix_request.stationary_boss = true;
    boss_prefix_request.boss_cells = { { 8, 4 } };
    const auto boss_prefix_plan = plan_egir_formation(boss_short_chain, boss_prefix_request);
    assert(
        boss_prefix_plan.found && boss_prefix_plan.core == "Terminal" &&
        !boss_prefix_plan.attacking_core_separate_from_chain);
    assert(boss_prefix_plan.chain.front() == "Terminal" && boss_prefix_plan.chain.size() == 2);
    auto doll_only_head = egir_placement;
    for (auto& input : doll_only_head) {
        input.eligible_chain_head = false;
    }
    assert(!plan_egir_formation(doll_only_head, egir_request).found);
    auto egir_alternate_request = egir_request;
    egir_alternate_request.target_admissible = [](const auto& units) {
        return units.front().cell != BoardCell(3, 4);
    };
    const auto egir_alternate = plan_egir_formation(egir_placement, egir_alternate_request);
    assert(egir_alternate.found && egir_alternate.units.front().cell != BoardCell(3, 4));
    egir_alternate_request.target_admissible = [](const auto&) {
        return false;
    };
    assert(!plan_egir_formation(egir_placement, egir_alternate_request).found);
    auto unreachable_preferred_core = egir_placement;
    unreachable_preferred_core.front().entity.cell = { 9, 4 };
    unreachable_preferred_core.front().entity.facing = 'E';
    unreachable_preferred_core.front().deployment.cell = { 9, 4 };
    unreachable_preferred_core.front().deployment.facing = 'E';
    unreachable_preferred_core.front().deployment.movable = false;
    unreachable_preferred_core.front().deployment.placement_cells = { { 9, 4 } };
    const auto fallback_core = plan_egir_formation(unreachable_preferred_core, egir_request);
    assert(fallback_core.found && fallback_core.core != "Ghost");
    assert(fallback_core.evaluations <= egir_request.evaluation_budget);
    for (size_t index = 0; index < egir_placement.size(); ++index) {
        egir_placement[index].deployment = egir_target.units[index];
        egir_placement[index].entity.cell = egir_target.units[index].cell;
        egir_placement[index].entity.facing = egir_target.units[index].facing;
    }
    const auto egir_stable = plan_egir_formation(egir_placement, egir_request);
    assert(egir_stable.found && egir_stable.changed_units == 0);
    const auto egir_no_input = sequence_deployment(egir_target.units, egir_stable.units);
    assert(egir_no_input.complete && egir_no_input.steps.empty());
    auto secondary_output = egir_placement;
    auto side_source = egir_placement[1];
    side_source.entity.id = side_source.entity.name = side_source.deployment.id = "SideOutput";
    side_source.entity.cell = side_source.deployment.cell = { 4, 2 };
    side_source.entity.facing = side_source.deployment.facing = 'E';
    side_source.deployment.placement_cells = { { 4, 2 } };
    side_source.deployment.frontline = false;
    side_source.deployment.local_range = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } };
    side_source.core_priority = 0;
    side_source.same_row_recruitment_members = 0;
    secondary_output.push_back(side_source);
    auto side_donor = side_source;
    side_donor.entity.id = side_donor.entity.name = side_donor.deployment.id = "SideDonor";
    side_donor.entity.cell = side_donor.deployment.cell = { 5, 2 };
    side_donor.entity.facing = side_donor.deployment.facing = 'N';
    side_donor.deployment.placement_cells = { { 5, 2 } };
    side_donor.deployment.movable = side_donor.deployment.damage_output = false;
    secondary_output.push_back(side_donor);
    auto secondary_request = egir_request;
    secondary_request.lanes.front().path = { { 9, 2 }, { 8, 2 }, { 7, 2 }, { 6, 2 }, { 5, 2 },
                                             { 5, 3 }, { 5, 4 }, { 4, 4 }, { 3, 4 }, { 2, 4 } };
    const auto secondary_plan = plan_egir_formation(secondary_output, secondary_request);
    assert(secondary_plan.found && secondary_plan.units[5].facing == 'E');
    const auto side_consumption = std::find_if(
        secondary_plan.conditional_consumption.sources.begin(),
        secondary_plan.conditional_consumption.sources.end(),
        [](const auto& source) { return source.source == "SideOutput"; });
    assert(side_consumption != secondary_plan.conditional_consumption.sources.end());
    assert(
        std::find(side_consumption->marked_targets.begin(), side_consumption->marked_targets.end(), "SideDonor") !=
        side_consumption->marked_targets.end());
    auto egir_guarded = egir_placement;
    egir_guarded[3].entity.intent = EgirIntent::ProtectedSupport;
    const auto egir_protected = plan_egir_formation(egir_guarded, egir_request);
    assert(egir_protected.found && egir_protected.chain.size() == 3);
    for (const auto& victim : egir_protected.conditional_consumption.victims) {
        assert(victim.id != "Donor" && victim.id != "Medic");
    }
    auto egir_immovable = egir_placement;
    egir_immovable[0].deployment.movable = false;
    egir_immovable[0].deployment.facing = egir_immovable[0].entity.facing = 'W';
    assert(!plan_egir_core_chain(egir_immovable, egir_request, "Ghost").found);
    const auto fixed_core_fallback = plan_egir_formation(egir_immovable, egir_request);
    assert(fixed_core_fallback.found && fixed_core_fallback.core != "Ghost");
    assert(fixed_core_fallback.units.front().cell == egir_immovable.front().entity.cell);
    assert(fixed_core_fallback.units.front().facing == 'W');
    for (auto& unit : egir_immovable) {
        unit.deployment.movable = false;
        unit.deployment.facing = unit.entity.facing = 'W';
    }
    assert(!plan_egir_formation(egir_immovable, egir_request).found);
    auto egir_missing_identity = egir_placement;
    egir_missing_identity[1].entity.id = egir_missing_identity[0].entity.id;
    assert(!plan_egir_formation(egir_missing_identity, egir_request).input_complete);
    auto egir_tiny_budget = egir_request;
    egir_tiny_budget.evaluation_budget = 1;
    const auto egir_bounded = plan_egir_formation(egir_placement, egir_tiny_budget);
    assert(egir_bounded.evaluations == 1 && egir_bounded.budget_exhausted);
    auto egir_boss = egir_request;
    egir_boss.stationary_boss = true;
    assert(!plan_egir_formation(egir_placement, egir_boss).input_complete);
    egir_boss.boss_cells = { { 7, 4 } };
    const auto egir_boss_target = plan_egir_formation(egir_placement, egir_boss);
    assert(egir_boss_target.found && range_covers(egir_boss_target.units.front(), { 7, 4 }));
    auto egir_with_protected_output = egir_placement;
    auto boss_support = side_source;
    boss_support.entity.egir_member = false;
    boss_support.entity.intent = EgirIntent::ProtectedSupport;
    boss_support.deployment.local_range = { { 1, 0 } };
    boss_support.deployment.placement_cells = { { 4, 2 }, { 6, 2 } };
    boss_support.preferred_boss_output = true;
    egir_with_protected_output.push_back(boss_support);
    auto opening_egir_boss = egir_boss;
    opening_egir_boss.stationary_boss = false;
    opening_egir_boss.boss_opening_only = true;
    opening_egir_boss.lanes.clear();
    opening_egir_boss.boss_cells.push_back({ 7, 2 });
    const auto boss_support_plan = plan_egir_formation(egir_with_protected_output, opening_egir_boss);
    assert(boss_support_plan.found && boss_support_plan.boss_priority_attackers == 2);
    assert(boss_support_plan.units.back().cell == BoardCell(6, 2) && boss_support_plan.units.back().facing == 'E');
    auto independent_attacker = egir_placement;
    auto clem_attacker = independent_attacker.front();
    clem_attacker.entity.id = clem_attacker.entity.name = clem_attacker.deployment.id = "Clementia";
    clem_attacker.entity.cell = clem_attacker.deployment.cell = { 7, 4 };
    clem_attacker.entity.facing = clem_attacker.deployment.facing = 'E';
    clem_attacker.entity.tier = 6;
    clem_attacker.deployment.placement_cells = { { 7, 4 }, { 7, 3 } };
    const auto clem_window = conditional_skill_attack_window("克莱门莎", false, 120);
    assert(clem_window.possible && clem_window.first_activation_seconds == 13 && clem_window.active_seconds == 48);
    clem_attacker.deployment.local_range.assign(clem_window.attack_range.begin(), clem_window.attack_range.end());
    clem_attacker.core_priority = 100;
    independent_attacker.push_back(clem_attacker);
    auto separate_boss = egir_boss;
    separate_boss.boss_cells = { { 9, 3 }, { 10, 3 } };
    separate_boss.boss_cells_are_alternatives = true;
    const auto separate_plan = plan_egir_formation(independent_attacker, separate_boss);
    assert(
        separate_plan.found && separate_plan.core == "Clementia" && separate_plan.attacking_core_separate_from_chain);
    assert((separate_plan.units.back().cell == BoardCell { 7, 3 }) && separate_plan.units.back().facing == 'E');
    assert(separate_plan.chain.front() != "Clementia");
    auto observed_boss_area = separate_boss;
    observed_boss_area.boss_cells = { { 8, 1 }, { 9, 1 }, { 8, 3 }, { 9, 3 } };
    observed_boss_area.boss_cells_are_alternatives = false;
    const auto region_plan = plan_egir_formation(independent_attacker, observed_boss_area);
    assert(region_plan.found && region_plan.core == "Clementia");
    observed_boss_area.boss_cells_are_alternatives = true;
    assert(!plan_egir_formation(independent_attacker, observed_boss_area).found);
    for (const auto& victim : separate_plan.conditional_consumption.victims) {
        assert(victim.id != "Clementia");
    }
    independent_attacker.back().deployment.local_range = { { 0, 0 }, { 1, 0 } };
    assert(!plan_egir_formation(independent_attacker, separate_boss).found);
    assert(!conditional_skill_attack_window("克莱门莎", false, 10).possible);
    assert(!conditional_skill_attack_window("乌尔比安", false, 120).possible);
    // R14 m06: layer count is a health bonus, so a boss attacker must prefer
    // inherited attack over a larger count of off-chain growth victims.
    std::vector<EgirPlacementUnit> m06_boss_units;
    const std::vector<std::pair<std::string, BoardCell>> m06_boss_roster {
        { "乌尔比安", { 3, 4 } }, { "克莱门莎", { 6, 3 } }, { "哈洛德", { 5, 2 } },     { "夏栎", { 9, 4 } },
        { "安哲拉", { 6, 4 } },   { "幽灵鲨", { 4, 4 } },   { "归溟幽灵鲨", { 7, 4 } }, { "斯卡蒂", { 5, 4 } }
    };
    const auto m06_pacts = operator_pacts();
    for (const auto& [name, cell] : m06_boss_roster) {
        const auto stats = season_combat_stats(name, false);
        assert(stats);
        EgirPlacementUnit input;
        const auto& pacts = m06_pacts.at(name);
        const bool egir = std::find(pacts.begin(), pacts.end(), "阿戈尔") != pacts.end();
        const bool healer = name == "哈洛德";
        input.entity.id = input.entity.name = input.deployment.id = name;
        input.entity.cell = input.deployment.cell = cell;
        input.entity.facing = input.deployment.facing = 'E';
        input.entity.egir_member = egir;
        input.entity.intent = healer ? EgirIntent::ProtectedSupport : egir ? EgirIntent::Relay : EgirIntent::Material;
        input.entity.base_attack = stats->attack;
        input.entity.block_count = stats->block_count;
        const auto fact = std::find_if(OperatorCatalogue.begin(), OperatorCatalogue.end(), [&](const auto& item) {
            return item.name == name;
        });
        assert(fact != OperatorCatalogue.end());
        input.entity.tier = fact->tier;
        input.expendable_material = !egir && !healer;
        input.eligible_chain_head = egir && name != "归溟幽灵鲨";
        input.core_priority = name == "克莱门莎" ? 100 : name == "乌尔比安" ? 90 : 1;
        auto& unit = input.deployment;
        unit.healer = healer;
        unit.frontline = egir && name != "安哲拉";
        unit.damage_output = !healer;
        unit.range_known = true;
        unit.block_count = stats->block_count;
        unit.local_range.assign(stats->basic_range.begin(), stats->basic_range.end());
        if (name == "克莱门莎") {
            const auto skill = conditional_skill_attack_window(name, false, 120);
            unit.local_range.insert(unit.local_range.end(), skill.attack_range.begin(), skill.attack_range.end());
        }
        for (int row = 0; row <= 4; ++row) {
            for (int col = 0; col <= 10; ++col) {
                if (terrain_allows_deployment("m06", { col, row }, unit.frontline, true)) {
                    unit.placement_cells.emplace_back(col, row);
                }
            }
        }
        m06_boss_units.push_back(input);
    }
    EgirFormationContext m06_boss_request;
    m06_boss_request.pact_active = true;
    m06_boss_request.stationary_boss = true;
    m06_boss_request.home = { 2, 4 };
    m06_boss_request.boss_cells = { { 10, 3 } };
    m06_boss_request.lanes = terrain_layout_model("m06", true)->lanes;
    m06_boss_request.defended_chain_prefix = { "克莱门莎", "归溟幽灵鲨", "斯卡蒂", "安哲拉", "乌尔比安", "幽灵鲨" };
    const auto m06_boss_plan = plan_egir_formation(m06_boss_units, m06_boss_request);
    std::cerr << "M06 boss plan found=" << m06_boss_plan.found << " core=" << m06_boss_plan.core
              << " evaluations=" << m06_boss_plan.evaluations << " reason=" << m06_boss_plan.reason << '\n';
    assert(
        m06_boss_plan.found && m06_boss_plan.core == "克莱门莎" && !m06_boss_plan.attacking_core_separate_from_chain);
    const auto m06_attack = std::find_if(
        m06_boss_plan.conditional_consumption.sources.begin(),
        m06_boss_plan.conditional_consumption.sources.end(),
        [](const auto& source) { return source.source == "克莱门莎"; });
    assert(m06_attack != m06_boss_plan.conditional_consumption.sources.end() && m06_attack->inherited_base_attack);
    assert(
        *m06_attack->inherited_base_attack >=
        season_combat_stats("乌尔比安", false)->attack + season_combat_stats("斯卡蒂", false)->attack);
    // A mirrored boss board needs a west-facing output and food chain.
    // This is a logical planner test, not camera registration or coop proof.
    auto mirrored_m06_units = m06_boss_units;
    const auto mirror_facing = [](char facing) {
        return facing == 'E' ? 'W' : facing == 'W' ? 'E' : facing;
    };
    for (auto& unit : mirrored_m06_units) {
        unit.entity.cell.first = 10 - unit.entity.cell.first;
        unit.deployment.cell = unit.entity.cell;
        unit.entity.facing = mirror_facing(unit.entity.facing);
        unit.deployment.facing = mirror_facing(unit.deployment.facing);
        for (auto& cell : unit.deployment.placement_cells) {
            cell.first = 10 - cell.first;
        }
    }
    auto mirrored_m06_request = m06_boss_request;
    mirrored_m06_request.home.first = 10 - mirrored_m06_request.home.first;
    for (auto& lane : mirrored_m06_request.lanes) {
        for (auto& cell : lane.path) {
            cell.first = 10 - cell.first;
        }
    }
    for (auto& cell : mirrored_m06_request.boss_cells) {
        cell.first = 10 - cell.first;
    }
    const auto mirrored_m06_plan = plan_egir_formation(mirrored_m06_units, mirrored_m06_request);
    assert(
        mirrored_m06_plan.found && mirrored_m06_plan.core == "克莱门莎" &&
        !mirrored_m06_plan.attacking_core_separate_from_chain);
    const auto mirrored_core =
        std::find_if(mirrored_m06_plan.units.begin(), mirrored_m06_plan.units.end(), [](const auto& unit) {
            return unit.id == "克莱门莎";
        });
    assert(mirrored_core != mirrored_m06_plan.units.end() && mirrored_core->facing == 'W');
    auto vertical_egir = egir_placement;
    vertical_egir.pop_back(); // The four physical chain members only.
    for (size_t i = 0; i < vertical_egir.size(); ++i) {
        auto& unit = vertical_egir[i];
        unit.entity.cell = unit.deployment.cell = { 5, 1 + static_cast<int>(i) };
        unit.entity.facing = unit.deployment.facing = 'N';
        unit.deployment.placement_cells.clear();
        for (int row = 0; row < 5; ++row) {
            unit.deployment.placement_cells.emplace_back(5, row);
        }
    }
    vertical_egir.front().required_cell = BoardCell(5, 3);
    auto vertical_request = egir_request;
    vertical_request.stationary_boss = true;
    vertical_request.boss_cells = { { 5, 2 } };
    const auto north_chain = plan_egir_formation(vertical_egir, vertical_request);
    assert(north_chain.found && north_chain.core == "Ghost" && north_chain.units.front().facing == 'N');
    assert(north_chain.chain.size() == 4 && !north_chain.attacking_core_separate_from_chain);
    for (auto& unit : vertical_egir) {
        unit.entity.cell.second = 4 - unit.entity.cell.second;
        unit.deployment.cell = unit.entity.cell;
        unit.entity.facing = unit.deployment.facing = 'S';
    }
    vertical_egir.front().required_cell = BoardCell(5, 1);
    const auto south_chain = plan_egir_formation(vertical_egir, vertical_request);
    assert(south_chain.found && south_chain.core == "Ghost" && south_chain.units.front().facing == 'S');
    assert(south_chain.chain.size() == 4 && !south_chain.attacking_core_separate_from_chain);
    // Both directions overlap the observed attackable area, but the west
    // chain inherits three donors. Finding an east candidate first must not
    // prevent comparison with the stronger cardinal alternative.
    auto two_direction_egir = egir_placement;
    two_direction_egir.pop_back();
    for (size_t i = 0; i < two_direction_egir.size(); ++i) {
        auto& unit = two_direction_egir[i];
        unit.entity.cell = unit.deployment.cell = { i == 0 ? 6 : 2 + static_cast<int>(i), 3 };
        unit.entity.facing = unit.deployment.facing = 'E';
        unit.deployment.placement_cells.clear();
        for (int col = 3; col <= 7; ++col) {
            unit.deployment.placement_cells.emplace_back(col, 3);
        }
    }
    two_direction_egir.front().required_cell = BoardCell(6, 3);
    auto two_direction_request = vertical_request;
    two_direction_request.boss_cells = { { 6, 3 } };
    const auto stronger_west = plan_egir_formation(two_direction_egir, two_direction_request);
    assert(stronger_west.found && stronger_west.core == "Ghost" && stronger_west.units.front().facing == 'W');
    assert(stronger_west.chain.size() == 4 && !stronger_west.attacking_core_separate_from_chain);
    assert(season_default_skill("乌尔比安", false)->self_movement);
    assert(!default_skill_holds_position("乌尔比安", false));
    assert(!default_skill_holds_position("乌尔比安", true));
    assert(default_skill_holds_position("幽灵鲨", false));
    assert(!default_skill_holds_position("unidentified", false));
    auto relocating_head = egir_placement;
    relocating_head.front().deployment.holds_position = false;
    assert(!plan_egir_core_chain(relocating_head, egir_request, "Ghost").found);
    const auto stationary_fallback = plan_egir_formation(relocating_head, egir_request);
    assert(stationary_fallback.found && stationary_fallback.core != "Ghost");
    assert(SeasonDefaultSkills.size() == 286);
    assert(advertised_boss_family({ { "假想敌：管", .99, 1000, 370 } }) == "假想敌：管");
    assert(!advertised_boss_family({ { "管", .99, 1000, 370 } }));
    assert(!advertised_boss_family({ { "假想敌：管", .89, 1000, 370 } }));
    assert(!advertised_boss_family({ { "假想敌：管", .99, 100, 370 } }));
    assert(!advertised_boss_family({ { "假想敌：管", .99, 1000, 370 }, { "假想敌：铳", .99, 1000, 400 } }));
    const auto pipe_boss = boss_objective("假想敌：管", { 2, 4 }, 14);
    assert(pipe_boss.known && pipe_boss.stationary_route && !pipe_boss.variant_known);
    assert((pipe_boss.possible_cells == std::vector<BoardCell> { { 9, 3 }, { 10, 3 } }));
    const auto normal_pipe = boss_objective("假想敌：管", { 2, 4 }, 14, false);
    assert(normal_pipe.variant_known && normal_pipe.maximum_declared_hp == 2'520'000);
    assert((normal_pipe.possible_cells == std::vector<BoardCell> { { 10, 3 } }));
    assert(!boss_objective("假想敌：鳍", { 2, 4 }, 14).stationary_route);
    assert(!boss_objective("Unknown", { 2, 4 }, 14).known);
    assert(current_hidden_boss_enemy("enemy_9021_acduml_2"));
    assert(!current_hidden_boss_enemy("enemy_9021_acduml"));
    assert(season_combat_stats("拜松", false)->block_count == 3);
    assert(season_combat_stats("幽灵鲨", false)->block_count == 2);
    assert(season_combat_stats("幽灵鲨", true)->block_count == 3);
    assert(season_combat_stats("“暴躁铁皮”", false)->equipment_slots == 4);
    assert(season_combat_stats("“暴躁铁皮”", true)->equipment_slots == 6);
    assert(
        season_location_type("“暴躁铁皮”", asst::battle::LocationType::Invalid) == asst::battle::LocationType::Melee);
    assert(season_location_type("蓝毒", asst::battle::LocationType::Ranged) == asst::battle::LocationType::Ranged);
    assert(season_combat_stats("真理", false)->level == 60);
    assert(season_combat_stats("真理", false)->max_hp == 991);
    const auto istina_basic_range = season_combat_stats("真理", false)->basic_range;
    assert(istina_basic_range.size() == 12);
    assert(
        std::find(istina_basic_range.begin(), istina_basic_range.end(), std::pair { -1, 1 }) !=
        istina_basic_range.end());
    assert(!season_combat_stats("Unknown operator", false));
    DeploymentUnit capacity_tank;
    capacity_tank.id = "Bison";
    capacity_tank.cell = { 2, 0 };
    capacity_tank.frontline = true;
    capacity_tank.block_count = season_combat_stats("拜松", false)->block_count;
    capacity_tank.placement_cells = { { 2, 0 }, { 2, 2 } };
    auto capacity_duelist = capacity_tank;
    capacity_duelist.id = "Jack";
    capacity_duelist.cell = { 2, 2 };
    capacity_duelist.block_count = 1;
    DeploymentUnit capacity_medic;
    capacity_medic.id = "Medic";
    capacity_medic.cell = { 0, 1 };
    capacity_medic.healer = capacity_medic.range_known = true;
    capacity_medic.local_range = { { 2, 1 }, { 2, -1 } };
    capacity_medic.movable = false;
    capacity_medic.placement_cells = { capacity_medic.cell };
    auto capacity_output = capacity_medic;
    capacity_output.id = "Output";
    capacity_output.cell = { 1, 1 };
    capacity_output.healer = false;
    capacity_output.damage_output = true;
    capacity_output.local_range = { { 2, 1 }, { 3, 1 }, { 2, -1 }, { 3, -1 } };
    capacity_output.placement_cells = { capacity_output.cell };
    const std::vector<DeploymentLane> capacity_lanes {
        { "upper", { { 4, 0 }, { 3, 0 }, { 2, 0 }, { 1, 0 }, { 0, 0 } }, 2, 2, 1 },
        { "lower", { { 4, 2 }, { 3, 2 }, { 2, 2 }, { 1, 2 }, { 0, 2 } }, 2, 2, 2 }
    };
    const std::vector<BoardCell> capacity_legal { { 2, 0 }, { 2, 2 }, { 0, 1 }, { 1, 1 } };
    const std::vector<DeploymentUnit> capacity_board { capacity_tank,
                                                       capacity_duelist,
                                                       capacity_medic,
                                                       capacity_output };
    const auto capacity_before = review_deployment(capacity_board, capacity_lanes, capacity_legal);
    assert(capacity_before.static_constraints_met && capacity_before.unique_route_block_capacity == 4);
    assert(capacity_before.balanced_block_capacity == 30);
    const auto capacity_plan = propose_deployment(capacity_board, capacity_lanes, capacity_legal, 1000);
    assert(capacity_plan.improved && capacity_plan.blocking_capacity_improvement == 30);
    assert((capacity_plan.units.front().cell == BoardCell { 2, 2 }));
    capacity_tank.cell = { 2, 1 };
    const std::vector<DeploymentLane> shared_capacity_lanes {
        { "upper", { { 4, 0 }, { 3, 0 }, { 2, 0 }, { 2, 1 }, { 1, 1 }, { 0, 1 } }, 2, 2, 1 },
        { "lower", { { 4, 2 }, { 3, 2 }, { 2, 2 }, { 2, 1 }, { 1, 1 }, { 0, 1 } }, 2, 2, 2 }
    };
    const auto shared_capacity = review_deployment({ capacity_tank }, shared_capacity_lanes, { { 2, 1 } });
    assert(shared_capacity.complete && shared_capacity.unique_route_block_capacity == 3);
    assert(shared_capacity.lanes[0].block_capacity == 3 && shared_capacity.lanes[1].block_capacity == 3);
    assert(shared_capacity.balanced_block_capacity == 60); // Three shared slots, never six.
    auto moving_tank = capacity_tank;
    moving_tank.holds_position = false;
    const auto vacated_capacity = review_deployment({ moving_tank }, shared_capacity_lanes, { { 2, 1 } });
    assert(vacated_capacity.complete && !vacated_capacity.static_constraints_met);
    assert(vacated_capacity.unique_route_block_capacity == 0 && vacated_capacity.balanced_block_capacity == 0);
    assert(moving_tank.block_count == capacity_tank.block_count); // Physical consumption value is preserved.
    std::vector<EgirEntity> egir_chain {
        { "core", "Ghost", { 3, 4 }, 'E', true, 1, EgirIntent::Core, 100, 2, 10'000, 4500 },
        { "relay1", "Gladiia", { 4, 4 }, 'E', true, 2, EgirIntent::Relay, 200, 1, 6000, 4500 },
        { "relay2", "Skadi", { 5, 4 }, 'E', true, 3, EgirIntent::Relay, 300, 2, 6000, 4500 },
        { "material", "Donor", { 6, 4 }, 'W', false, 6, EgirIntent::Material, 400, 3, 6000, 4500 }
    };
    const auto egir_projection = project_egir_mark_closure(egir_chain, true);
    assert(egir_projection.complete && !egir_projection.current_season_semantics_verified);
    assert(egir_projection.distinct_egir_members == 3 && egir_projection.base_six_member_revive_limit == 0);
    assert(egir_projection.sources.front().source == "core");
    assert(egir_projection.sources.front().marked_targets.size() == 3);
    assert(egir_projection.sources.front().inherited_base_attack == 900);
    assert(egir_projection.sources.front().inherited_block_count == 6);
    assert(egir_projection.conditional_unique_target_layers == 11);
    assert(egir_projection.victims[0].marks == 1 && egir_projection.victims[0].defeated == false);
    assert(egir_projection.victims[1].marks == 2 && egir_projection.victims[1].defeated == true);
    assert(egir_projection.victims[2].marks == 3 && egir_projection.victims[2].defeated == true);
    egir_chain[2].damage_per_mark.reset();
    assert(!project_egir_mark_closure(egir_chain, true).victims[1].defeated.has_value());
    egir_chain[2].base_attack.reset();
    assert(!project_egir_mark_closure(egir_chain, true).sources.front().inherited_base_attack);
    egir_chain[1].facing = 0;
    assert(!project_egir_mark_closure(egir_chain, true).complete);
    assert(project_egir_mark_closure(egir_chain, false).conditional_unique_target_layers == 0);
    auto egir_cycle = std::vector<EgirEntity>(egir_chain.begin(), egir_chain.begin() + 2);
    egir_cycle[1].facing = 'W';
    const auto mutual_consumption = project_egir_mark_closure(egir_cycle, true);
    assert(mutual_consumption.complete && mutual_consumption.sources[0].marked_targets.size() == 1);
    assert(mutual_consumption.sources[1].marked_targets.empty());
    egir_cycle[1].name = egir_cycle[0].name;
    assert(project_egir_mark_closure(egir_cycle, true).distinct_egir_members == 1);
    egir_cycle[1].intent = EgirIntent::Core;
    assert(!project_egir_mark_closure(egir_cycle, true).problems.empty());
    auto six_egir_entities = egir_chain;
    six_egir_entities[1].facing = 'E';
    six_egir_entities.push_back({ "fifth", "Mizuki", { 7, 4 }, 'E', true, 4 });
    six_egir_entities.push_back({ "sixth", "Ulpianus", { 8, 4 }, 'E', true, 5 });
    six_egir_entities.push_back({ "seventh", "Clementia", { 9, 4 }, 'E', true, 6 });
    assert(project_egir_mark_closure(six_egir_entities, true).base_six_member_revive_limit == 3);
    const auto egir_bench_rule =
        project_egir_mark_closure(six_egir_entities, true, EgirSixMemberRule::ReplaceFromBench);
    assert(
        egir_bench_rule.base_six_member_revive_limit == 0 && egir_bench_rule.six_member_bench_replacement_limit == 3);
    auto harmony_egir = std::vector<EgirEntity>(six_egir_entities.begin(), six_egir_entities.begin() + 3);
    harmony_egir[2].name = "Harmony";
    harmony_egir[2].counts_for_literal_egir_milestone = false;
    const auto harmony_projection = project_egir_mark_closure(harmony_egir, true);
    assert(harmony_projection.distinct_egir_members == 2 && harmony_projection.sources.size() == 3);
    assert(equipment_added_pacts({ "阿戈尔重刃" }).empty());
    assert(equipment_added_pacts({ "变形同构体" }).empty());
    assert(equipment_added_pacts({ "阿戈尔重刃", "不屈弹射器" }).empty());
    assert((equipment_added_pacts({ "变形同构体", "阿戈尔重刃" }, true) == std::vector<std::string> { "阿戈尔" }));
    assert((equipment_added_pacts({ "进阶阿戈尔重刃", "变形同构体" }, true) == std::vector<std::string> { "阿戈尔" }));
    assert(equipment_added_pacts({ "变形同构体", "未知装备" }).empty());
    // Transferred Surtr retains both original pacts and becomes the third Egir.
    const auto transferred_surtr = evaluate_formation(
        { { "幽灵鲨", true, {}, false },
          { "安哲拉", true, {}, false },
          { "史尔特尔", true, equipment_added_pacts({ "变形同构体", "阿戈尔重刃" }, true), false } },
        operator_pacts(),
        pact_requirements(),
        { "阿戈尔", "突袭", "不屈" });
    assert(transferred_surtr.distinct_members.at("阿戈尔") == 3);
    assert(transferred_surtr.distinct_members.at("突袭") == 1);
    assert(transferred_surtr.distinct_members.at("不屈") == 1);
    assert(
        std::find(transferred_surtr.active_pacts.begin(), transferred_surtr.active_pacts.end(), "阿戈尔") !=
        transferred_surtr.active_pacts.end());
    assert(!purchase_confirmation_control({}, 2));
    assert(purchase_confirmation_control({ { "确认购买", .99, 640, 655 } }, 2));
    assert(!purchase_confirmation_control({ { "确认购买", .99, 960, 655 } }, 2));
    assert(!purchase_confirmation_control({ { "确认购买", .50, 640, 655 } }, 2));
    assert(!purchase_confirmation_control({ { "确认购买", .99, 640, 655 }, { "确认购买", .99, 645, 658 } }, 2));
    // R4 live regression: the first free-card tap missed; the fallback only
    // selected the reward panel. Only its subsequently visible button can
    // confirm the reward and reconcile the consumed source copies.
    assert(!promotion_confirmation_control({ { "深巡", .99, 646, 664 } }, "深巡", 2));
    assert(!promotion_confirmation_control({ { "确认购买", .99, 649, 657 } }, "深巡", 2));
    assert(promotion_confirmation_control({ { "深巡", .99, 14, 182 }, { "确认购买", .99, 649, 657 } }, "深巡", 2));
    assert(!promotion_confirmation_control({ { "伺夜", .99, 14, 182 }, { "确认购买", .99, 649, 657 } }, "深巡", 2));
    assert(!promotion_confirmation_control({ { "深巡", .99, 14, 182 }, { "确认购买", .99, 970, 657 } }, "深巡", 2));
    // Archived D1 R13 OCR: the exact short title was below the global 0.90
    // threshold on consecutive reward-confirmation frames.
    const std::vector<TextObservation> dusk_confirmation { { "夕", .716468, 12, 180 },
                                                           { "攻击范围", .999975, 126, 296 },
                                                           { "确认购买", .99, 649, 657 } };
    assert(!promotion_confirmation_control(dusk_confirmation, "夕", 2));
    assert(promotion_short_title_confirmation(dusk_confirmation, "夕", 2));
    const std::vector<TextObservation> d3_dusk_confirmation { { "夕", .574863, 12, 180 },
                                                              { "攻击范围", .999954, 125, 293 },
                                                              { "确认购买", .999993, 810, 657 } };
    assert(promotion_short_title_confirmation(d3_dusk_confirmation, "夕", 1));
    assert(operator_title_contains_rank_artifact({ { "余M", .952801, 10, 181 } }, "余"));
    assert(!operator_title_contains_rank_artifact({ { "余烬", .99, 10, 181 } }, "余"));
    assert(!operator_title_contains_rank_artifact({ { "余M", .95, 12, 665 } }, "余"));
    assert(!operator_title_contains_rank_artifact({ { "余M", .95, 10, 181 }, { "黍", .98, 10, 187 } }, "余"));

    assert(!promotion_short_title_confirmation(d3_dusk_confirmation, "夕", 2));

    assert(!promotion_short_title_confirmation(dusk_confirmation, "夕", 0));
    assert(!promotion_short_title_confirmation(dusk_confirmation, "令", 2));
    auto conflicting_dusk_confirmation = dusk_confirmation;
    conflicting_dusk_confirmation.emplace_back(TextObservation { "令", .86, 14, 182 });
    assert(!promotion_short_title_confirmation(conflicting_dusk_confirmation, "夕", 2));
    conflicting_dusk_confirmation = dusk_confirmation;
    conflicting_dusk_confirmation.erase(conflicting_dusk_confirmation.begin() + 1);
    assert(!promotion_short_title_confirmation(conflicting_dusk_confirmation, "夕", 2));
    const auto engagement_map = terrain_layout_model("m06");
    assert(engagement_map);
    const auto junction = engagement_junction(engagement_map->lanes);
    assert((junction == BoardCell { 7, 4 }));
    for (int opening_round : { 1, 2, 3 }) {
        const auto opening_lanes = active_preparation_lanes(*engagement_map, opening_round);
        assert(opening_lanes.size() == 1);
        assert((opening_lanes.front().path.front() == BoardCell { 10, 4 }));
        assert(
            std::find(opening_lanes.front().path.begin(), opening_lanes.front().path.end(), BoardCell { 7, 4 }) !=
            opening_lanes.front().path.end());
    }
    assert(active_preparation_lanes(*engagement_map, 4).size() == 2);
    assert(active_preparation_lanes(*engagement_map, 0).size() == 2);
    assert(engagement_map->lanes.size() == 2); // Future junction planning retains both routes.
    const std::vector<GridPlacementCell> engagement_cells { { 9, 4, 900, 440 },
                                                            { 7, 4, 740, 440 },
                                                            { 6, 3, 660, 370 },
                                                            { 8, 1, 820, 230 } };
    const auto front_preferences =
        engagement_placement_preferences(PlacementRole::Frontline, engagement_cells, engagement_map->lanes, {});
    const auto chosen_front = choose_role_placement(
        PlacementRole::Frontline,
        { { 900, 440 }, { 740, 440 } },
        {},
        {},
        nullptr,
        front_preferences);
    assert((chosen_front == std::pair { 740, 440 }));
    const std::vector<BoardCell> two_lane_range { { 1, -1 }, { 1, 0 }, { 1, 1 }, { 2, -1 }, { 2, 0 }, { 2, 1 } };
    const auto ranged_preferences = engagement_placement_preferences(
        PlacementRole::Ranged,
        engagement_cells,
        engagement_map->lanes,
        two_lane_range);
    char engagement_direction = '\0';
    const auto chosen_ranged = choose_role_placement(
        PlacementRole::Ranged,
        { { 660, 370 }, { 820, 230 } },
        {},
        two_lane_range,
        &engagement_direction,
        ranged_preferences,
        true);
    assert((chosen_ranged == std::pair { 660, 370 }));
    assert(engagement_direction == 'E');
    assert(
        engagement_position_value(PlacementRole::Ranged, { 6, 3 }, '\0', two_lane_range, engagement_map->lanes) == 0);
    DeploymentUnit merge_blocker;
    merge_blocker.id = "merge-blocker";
    merge_blocker.cell = { 9, 4 };
    merge_blocker.frontline = true;
    merge_blocker.block_count = 2;
    merge_blocker.placement_cells = { { 9, 4 }, { 7, 4 } };
    const auto merge_proposal = propose_deployment(
        { merge_blocker },
        engagement_map->lanes,
        engagement_map->buildable,
        100,
        [&](const std::vector<DeploymentUnit>& units) {
            return engagement_position_value(
                PlacementRole::Frontline,
                units.front().cell,
                units.front().facing,
                {},
                engagement_map->lanes);
        });
    assert(merge_proposal.input_complete && merge_proposal.improved);
    assert((merge_proposal.units.front().cell == BoardCell { 7, 4 }));
    assert(!engagement_junction({ engagement_map->lanes.front() }));
    assert(
        engagement_placement_preferences(PlacementRole::Ranged, engagement_cells, engagement_map->lanes, {}).empty());
    assert((bench_source_probe_order({ 700, 780, 860 }, 1279) == std::vector<std::size_t> { 2, 1, 0 }));
    assert((bench_source_probe_order({ 700, 780, 860 }, 780) == std::vector<std::size_t> { 1, 0, 2 }));
    assert((bench_source_probe_order({ 700, 780, 860 }) == std::vector<std::size_t> { 0, 1, 2 }));
    assert((bench_source_probe_order({ 842, 923 }, 0) == std::vector<std::size_t> { 0, 1 }));
    assert(bench_source_probe_order({}, 923).empty());
    assert(replacement_outgoing_identity_trackable(true, 1, 1));
    assert(replacement_outgoing_identity_trackable(false, 2, 2));
    assert(!replacement_outgoing_identity_trackable(false, 2, 1));
    assert(!replacement_outgoing_identity_trackable(true, 2, 2));
    assert(!replacement_outgoing_identity_trackable(true, 1, 0));
    assert(!replacement_outgoing_identity_trackable(false, 0, 0));
    // A dollkeeper is a ground relay, not a guaranteed interceptor. Keep
    // Ghost guarding both routes while replacing an off-route ranged filler.
    std::vector<ReplacementMember> doll_relay_field {
        { "幽灵鲨", PlacementRole::Frontline, { "阿戈尔" }, 4000, 470, 438, true },
        { "斯卡蒂", PlacementRole::Frontline, { "阿戈尔" }, 4000, 555, 438, true },
        { "安哲拉", PlacementRole::Ranged, { "阿戈尔" }, 4000, 555, 368, true },
        { "空弦", PlacementRole::Ranged, { "精准" }, 100, 720, 438, true }
    };
    for (auto& member : doll_relay_field) {
        member.route_membership_registered = true;
    }
    doll_relay_field.front().terrain_routes = { "upper", "lower" };
    ReplacementMember doll_relay { "归溟幽灵鲨", PlacementRole::Frontline, { "阿戈尔" }, 2000 };
    assert(!choose_replacement("阿戈尔", doll_relay_field, { doll_relay }).found);
    doll_relay.reliable_blocker = false;
    const auto insert_doll_relay = choose_replacement("阿戈尔", doll_relay_field, { doll_relay });
    assert(insert_doll_relay.found && insert_doll_relay.outgoing == "空弦");
    assert(!replacement_blocks_ground(doll_relay));
    auto sole_blocker_field = doll_relay_field;
    for (std::size_t index = 1; index < sole_blocker_field.size(); ++index) {
        sole_blocker_field[index].replaceable = false;
    }
    sole_blocker_field.front().value = 0;
    assert(!choose_replacement("阿戈尔", sole_blocker_field, { doll_relay }).found);
    auto reciprocal_field = doll_relay_field;
    reciprocal_field.back().name = "夏栎";
    reciprocal_field.back().pacts = { "助力" };
    reciprocal_field.back().value = 0;
    auto reciprocal_doll = doll_relay;
    reciprocal_doll.value = 2500;
    const auto full_context_value = [](const auto& lineup) {
        auto recomputed = lineup;
        for (auto& unit : recomputed) {
            // Reproduce the live helper whose value was positive only in
            // the context where it was still benched, not after insertion.
            if (unit.name == "夏栎") {
                unit.value = 0;
            }
            else if (unit.name == "归溟幽灵鲨") {
                unit.value = 2500;
            }
        }
        return replacement_lineup_value(recomputed, "阿戈尔");
    };
    const auto forward_once =
        choose_replacement("阿戈尔", reciprocal_field, { reciprocal_doll }, {}, full_context_value);
    assert(forward_once.found && forward_once.outgoing == "夏栎");
    auto reciprocal_helper = reciprocal_field.back();
    reciprocal_helper.value = 4000;
    reciprocal_doll.x = reciprocal_helper.x;
    reciprocal_doll.y = reciprocal_helper.y;
    reciprocal_doll.replaceable = true;
    reciprocal_doll.route_membership_registered = true;
    reciprocal_field.back() = reciprocal_doll;
    assert(choose_replacement("阿戈尔", reciprocal_field, { reciprocal_helper }).found);
    assert(!choose_replacement("阿戈尔", reciprocal_field, { reciprocal_helper }, {}, full_context_value).found);
    auto first_relay_field = doll_relay_field;
    first_relay_field[0].egir_material_tier = 1;
    first_relay_field[1].egir_material_tier = 3;
    first_relay_field[2].egir_material_tier = 3;
    first_relay_field.back().value = 0;
    first_relay_field.back().terrain_routes = { "lower" };
    auto spare_skadi = first_relay_field[1];
    auto first_doll = doll_relay;
    first_doll.value = 480;
    first_doll.egir_material_tier = 4;
    const auto first_relay_choice = choose_replacement("阿戈尔", first_relay_field, { spare_skadi, first_doll });
    assert(first_relay_choice.found && first_relay_choice.incoming == "归溟幽灵鲨");
    assert(replacement_primary_members(first_relay_field, "阿戈尔") == 3);
    const auto collapsed_counter =
        observe_deployment_counter({ { "剩余可放置角色：4", .99, 1105, 460 }, { "3", .99, 1255, 380 } });
    assert(collapsed_counter.value == 4 && collapsed_counter.label_y == 460 && !collapsed_counter.contradictory);
    const auto split_counter = observe_deployment_counter(
        { { "剩余可放置角色：", .99, 1105, 460 }, { "4", .99, 1255, 460 }, { "3", .99, 1255, 380 } });
    assert(split_counter.value == 4 && split_counter.label_y == 460);
    assert(!observe_deployment_counter({ { "3", .99, 1255, 380 } }).value);
    assert(
        observe_deployment_counter({ { "剩余可放置角色：4", .99, 1105, 460 }, { "3", .99, 1255, 460 } }).contradictory);
    const std::vector<PactHudAnchor> layer_anchors { { "精准", 440, 129 }, { "伊比利亚", 520, 130 } };
    const auto hud_layers = observe_pact_layer_numbers(
        layer_anchors,
        { { "123", .99, 439, 105 }, { "500", .97, 522, 106 }, { "9", .99, 500, 20 } });
    assert(hud_layers.at("精准") == 123 && hud_layers.at("伊比利亚") == 500 && hud_layers.size() == 2);
    assert(observe_pact_layer_numbers(layer_anchors, { { "500", .80, 522, 106 } }).empty());
    assert(observe_pact_layer_numbers(layer_anchors, { { "5O0", .99, 522, 106 } }).empty());
    assert(observe_pact_layer_numbers(layer_anchors, { { "500", .99, 522, 130 } }).empty());
    assert(observe_pact_layer_numbers({}, { { "500", .99, 522, 106 } }).empty());
    assert(observe_pact_layer_numbers(layer_anchors, { { "500", .99, 522, 106 }, { "5", .99, 522, 106 } }).empty());
    assert(observe_pact_layer_numbers({ { "精准", 440, 129 }, { "精准", 520, 130 } }, { { "50", .99, 440, 106 } })
               .empty());
    assert(observe_pact_layer_numbers({ { "精准", 440, 129 }, { "迅捷", 475, 130 } }, { { "50", .99, 455, 106 } })
               .empty());
    // The round-11 expanded shop repeats Aegir and several secondary pacts.
    // Only genuine top-bar duplicates may invalidate the visible 56 layers.
    const auto expanded_shop_layers = observe_pact_layer_numbers(
        { { "阿戈尔", 380, 115 },
          { "灵巧", 460, 113 },
          { "聚合", 780, 111 },
          { "阿戈尔", 320, 623 },
          { "聚合", 480, 623 },
          { "精准", 640, 623 },
          { "不屈", 800, 640 } },
        { { "56", .999992, 380, 99 }, { "0", .99, 460, 99 }, { "3", .99, 380, 530 } });
    assert(
        expanded_shop_layers.size() == 2 && expanded_shop_layers.at("阿戈尔") == 56 &&
        expanded_shop_layers.at("灵巧") == 0);
    assert(observe_pact_layer_numbers({ { "阿戈尔", 380, 623 } }, { { "56", .99, 380, 99 } }).empty());
    // Live R11 rendered 51 while detector OCR confidently returned 5. A
    // disagreeing full-counter read must leave the layer unknown.
    assert(
        observe_pact_layer_numbers({ { "阿戈尔", 379, 115 } }, { { "5", .999960, 377, 100 }, { "51", .99, 379, 98 } })
            .empty());
    PreparationPolicy free_reward_policy;
    free_reward_policy.dynamic_decision_mode = true;
    free_reward_policy.primary_pact = "阿戈尔";
    free_reward_policy.bench_occupied = 10;
    const auto reward_pacts = operator_pacts();
    const auto primary_reward =
        rank_promotion_rewards(free_reward_policy, { "隐德来希", "Mon3tr", "克莱门莎" }, reward_pacts);
    assert(primary_reward.size() == 3 && primary_reward.front().name == "克莱门莎");
    free_reward_policy.owned["克莱门莎"] = { 1, true, true };
    const auto elite_duplicate_reward =
        rank_promotion_rewards(free_reward_policy, { "克莱门莎", "Mon3tr", "浊心斯卡蒂" }, reward_pacts);
    assert(elite_duplicate_reward.front().name == "浊心斯卡蒂");
    free_reward_policy.primary_pact = "炎";
    free_reward_policy.owned = { { "星极", { 1, false, true } } };
    const auto chain_reward = rank_promotion_rewards(free_reward_policy, { "角峰", "星极", "泡泡" }, reward_pacts);
    assert(chain_reward.front().name == "星极");
    assert(promotion_reward_starts_chain(free_reward_policy, "星极"));
    assert(!promotion_reward_starts_chain(free_reward_policy, "角峰"));
    assert(promotion_reward_chain_observed(true, true, 3, { "角峰", "星极", "泡泡" }, { "伺夜", "忍冬", "空弦" }));
    assert(!promotion_reward_chain_observed(true, true, 3, { "角峰", "星极", "泡泡" }, { "泡泡", "星极", "角峰" }));
    assert(!promotion_reward_chain_observed(true, false, 3, { "角峰", "星极", "泡泡" }, { "伺夜", "忍冬", "空弦" }));
    assert(!promotion_reward_chain_observed(false, true, 3, { "角峰", "星极", "泡泡" }, { "伺夜", "忍冬", "空弦" }));
    assert(!promotion_reward_chain_observed(true, true, 1, { "角峰", "星极", "泡泡" }, { "伺夜", "忍冬", "空弦" }));
    free_reward_policy.owned.clear();
    free_reward_policy.early_board_scores = { { "褐果", 900 }, { "角峰", 500 }, { "泡泡", 500 } };
    assert(rank_promotion_rewards(free_reward_policy, { "角峰", "褐果", "泡泡" }, reward_pacts).front().name == "褐果");
    free_reward_policy.dynamic_decision_mode = false;
    assert(rank_promotion_rewards(free_reward_policy, { "角峰", "褐果", "泡泡" }, reward_pacts).front().name == "角峰");
    assert(rank_promotion_rewards(free_reward_policy, { "未知卡", "角峰", "角峰" }, reward_pacts).size() == 1);
    assert(StrategyCatalogue.size() == 61);
    const auto wrapped_bounty_titles = observe_tactical_offers(
        { { "斯·悬赏", .99, 282, 296 },
          { "大鲍勃·悬赏", .99, 626, 287 },
          { "杰斯顿·威廉姆", .99, 282, 273 },
          { "碎骨·悬赏", .99, 966, 285 },
          { "为自身下场作战添加1个杰斯顿·威廉", .99, 182, 357 },
          { "姆斯，将其击倒者获得4资金", .99, 184, 382 } });
    assert(wrapped_bounty_titles[0].title == "杰斯顿·威廉姆斯·悬赏");
    assert(wrapped_bounty_titles[1].title == "大鲍勃·悬赏" && wrapped_bounty_titles[2].title == "碎骨·悬赏");
    assert(wrapped_bounty_titles[0].description.find("悬赏") == std::string::npos);
    for (const auto& identity : strategy_identities()) {
        const auto entry = std::find_if(StrategyCatalogue.begin(), StrategyCatalogue.end(), [&](const auto& fact) {
            return fact.id == identity.band_id;
        });
        assert(entry != StrategyCatalogue.end() && entry->name == identity.name && entry->leader == identity.leader);
        assert(strategy_unlock_rule(identity.band_id));
    }
    assert(StrategyUnlockCatalogue.size() == StrategyCatalogue.size());
    const auto protect_unlock = strategy_unlock_rule("band_protect");
    assert(protect_unlock && protect_unlock->kind == StrategyUnlockKind::JointWin && protect_unlock->count == 5);
    assert(
        !strategy_available_in_mode("band_protect", "independent") &&
        strategy_available_in_mode("band_protect", "joint"));
    assert(!strategy_available_in_mode("band_mech", "quick") && strategy_available_in_mode("band_mech", "independent"));
    const auto clementia_unlock = strategy_unlock_rule("band_clementia");
    assert(
        clementia_unlock && clementia_unlock->kind == StrategyUnlockKind::SettlementRoster &&
        clementia_unlock->count == 5 && clementia_unlock->prerequisite == "阿戈尔");
    const auto gain_unlock = strategy_unlock_rule("band_gain");
    assert(
        gain_unlock && gain_unlock->kind == StrategyUnlockKind::JointDifficultyWin &&
        gain_unlock->prerequisite == "绝境模拟");
    OpeningStrategyContext opening_context;
    opening_context.mode = "quick";
    assert(!assess_opening_strategy("band_mech", opening_context).eligible);
    opening_context.mode.clear();
    assert(assess_opening_strategy("band_takecare", opening_context).eligible);
    assert(!assess_opening_strategy("band_siracusa", opening_context).eligible);
    opening_context.pact_availability_known = true;
    assert(assess_opening_strategy("band_siracusa", opening_context).eligible);
    opening_context.disabled_pacts = { "叙拉古", "不屈" };
    assert(!assess_opening_strategy("band_siracusa", opening_context).eligible);
    const auto no_redeploy_pact = assess_opening_strategy("band_qalaisa", opening_context);
    opening_context.disabled_pacts.clear();
    assert(assess_opening_strategy("band_qalaisa", opening_context).score > no_redeploy_pact.score);
    assert(!assess_opening_strategy("unknown-strategy", opening_context).known);
    const auto uncontested_siracusa = assess_opening_strategy("band_siracusa", opening_context);
    opening_context.occupied_strategy_ids = { "band_siracusa" };
    const auto contested_siracusa = assess_opening_strategy("band_siracusa", opening_context);
    assert(contested_siracusa.eligible && contested_siracusa.score == uncontested_siracusa.score - 200);
    const auto flexible_contested = assess_opening_strategy("band_damaztic", opening_context);
    opening_context.occupied_strategy_ids.clear();
    assert(flexible_contested.score == assess_opening_strategy("band_damaztic", opening_context).score);
    assert(
        assess_opening_strategy("band_weakness", opening_context).score >
        assess_opening_strategy("band_transfer", opening_context).score);
    assert(parse_shop_toggle("收起口") == true);
    assert(parse_shop_toggle("收起 □") == true);
    assert(parse_shop_toggle("展开^") == false);
    assert(!parse_shop_toggle("请选择奖励"));
    assert(!parse_shop_toggle("干员展开技能"));
    assert(observe_shop_toggle({ "展开口", .837645, 1153, 654 }) == false);
    assert(observe_shop_toggle({ "收起 □", .83, 1153, 655 }) == true);
    assert(!observe_shop_toggle({ "展开口", .79, 1153, 654 }));
    assert(!observe_shop_toggle({ "展开口", .99, 900, 654 }));
    assert(!observe_shop_toggle({ "展开口", .99, 1153, 500 }));
    assert(!observe_shop_toggle({ "请选择奖励", .99, 1153, 654 }));
    assert(observe_shop_freeze({ "解冻", .999827, 1044, 446 }) == true);
    assert(observe_shop_freeze({ "冻结", .999101, 1045, 446 }) == false);
    assert(observe_shop_freeze({ "已冻结", .99, 1045, 446 }) == true);
    assert(!observe_shop_freeze({ "冻结", .99, 600, 446 }));
    assert(!observe_shop_freeze({ "冻结", .80, 1045, 446 }));
    assert(sale_credit_from_panel({ { "+1", 0.99, 750, 310 } }, 730, 355) == 1);
    assert(sale_credit_from_panel({ { "+ 4", 0.99, 750, 310 } }, 730, 355) == 4);
    assert(!sale_credit_from_panel({ { "55", 0.99, 750, 310 } }, 730, 355));
    assert(!sale_credit_from_panel({ { "+1", 0.70, 750, 310 } }, 730, 355));
    assert(!sale_credit_from_panel({ { "+1", 0.99, 1200, 510 } }, 730, 355));
    assert(!sale_credit_from_panel({ { "+1", 0.99, 750, 310 }, { "+4", 0.99, 750, 290 } }, 730, 355));
    assert(!sale_credit_from_panel({ { "+0", 0.99, 750, 310 } }, 730, 355));
    PreparationPolicy full_bench;
    full_bench.primary_pact = "莱塔尼亚";
    full_bench.shop_level = 5;
    full_bench.bench_occupied = 10;
    full_bench.owned = { { "复奏", { 1, false, false } },   { "云迹", { 1, false, false } },
                         { "星源", { 1, false, false } },   { "杰克", { 2, false, true } },
                         { "地灵", { 1, true, false } },    { "吉星", { 1, false, false } },
                         { "波登可", { 1, false, false } }, { "伊桑", { 1, false, false } },
                         { "深巡", { 1, false, false } } };
    assert(choose_full_bench_sale(full_bench) == "深巡");
    PreparationPolicy victoria_full;
    victoria_full.primary_pact = "维多利亚";
    victoria_full.primary_locked_after_activation = true;
    victoria_full.dynamic_decision_mode = true;
    victoria_full.shop_level = 6;
    victoria_full.round = 12;
    victoria_full.bench_occupied = 10;
    victoria_full.deployed_population = 8;
    auto& victoria_roster = victoria_full.owned;
    victoria_roster = { { "哈洛德", { 2, false, true } },
                        { "慕斯", { 2, false, true } },
                        { "凯瑟琳", { 2, false, true } },
                        { "涤火杰西卡", { 2, false, true } },
                        { "灰喉", { 2, false, true } } };
    assert(add_purchased_operator(victoria_roster.at("哈洛德"), true));
    const std::map<std::string, int> victoria_field { { "哈洛德", 1 },
                                                      { "慕斯", 1 },
                                                      { "凯瑟琳", 1 },
                                                      { "涤火杰西卡", 1 },
                                                      { "灰喉", 2 } };
    const std::vector<ShopOffer> victoria_shop { { "哈洛德", 3, 4 },
                                                 { "灵知", 3, 3 },
                                                 { "慕斯", 2, 2 },
                                                 { "W", 3, 1 },
                                                 { "波登可", 3, 0 } };
    const auto overflow = choose_bench_capacity_trade(victoria_full, 13, victoria_shop, victoria_field, {});
    assert(overflow && overflow->spare_initial == "凯瑟琳");
    assert(overflow->purchase.name == "哈洛德" || overflow->purchase.name == "慕斯");
    const auto mixed_target =
        choose_bench_capacity_trade(victoria_full, 13, { victoria_shop.front() }, victoria_field, {});
    assert(mixed_target && mixed_target->purchase.name == "哈洛德");
    assert(!choose_bench_capacity_trade(victoria_full, 13, victoria_shop, {}, {}));
    assert(
        !choose_bench_capacity_trade(victoria_full, 13, { { "哈洛德", 3, 4 }, { "灵知", 3, 4 } }, victoria_field, {}));
    assert(!choose_bench_capacity_trade(victoria_full, 1, victoria_shop, victoria_field, {}));
    auto victoria_capacity = victoria_full;
    victoria_capacity.owned.emplace("深巡", OwnedOperator { 1, false, false });
    victoria_capacity.full_field_offer_improvements["水灯心"] = 900;
    const auto upgrade_trade =
        choose_bench_capacity_trade(victoria_capacity, 6, { { "水灯心", 3, 4 } }, victoria_field, {});
    assert(upgrade_trade && upgrade_trade->spare_initial == "深巡" && upgrade_trade->purchase.name == "水灯心");
    assert(!choose_bench_capacity_trade(victoria_capacity, 2, { { "水灯心", 3, 4 } }, victoria_field, {}));
    victoria_capacity.full_field_offer_improvements["水灯心"] = 0;
    assert(!choose_bench_capacity_trade(victoria_capacity, 11, { { "水灯心", 3, 4 } }, victoria_field, {}));

    auto victoria_support = victoria_full;
    victoria_support.derive_support_pacts = true;
    victoria_support.owned = { { "Miss.Christine", { 1, false, true } }, { "白铁", { 1, false, true } },
                               { "凯瑟琳", { 1, false, true } },         { "卡达", { 1, false, false } },
                               { "拉普兰德", { 1, false, true } },       { "奥斯塔", { 1, false, true } },
                               { "铃兰", { 1, false, true } },           { "贝洛内", { 1, false, true } } };
    const auto victoria_supports = planning_support_pacts(victoria_support, {}, operator_pacts());
    assert(victoria_supports == std::vector<std::string> { "奥术" });
    ReplacementMember christine, victoria_filler, arcane_bridge;
    christine.name = "Miss.Christine";
    christine.pacts = { "维多利亚", "奥术" };
    arcane_bridge.name = "卡达";
    arcane_bridge.pacts = { "奥术" };
    victoria_filler.name = "拉普兰德";
    victoria_filler.pacts = { "叙拉古" };
    assert(
        replacement_support_pact_value({ christine, arcane_bridge }, victoria_supports, {}) >
        replacement_support_pact_value({ christine, victoria_filler }, victoria_supports, {}));
    assert(
        replacement_support_pact_value({ christine, christine }, victoria_supports, {}) ==
        replacement_support_pact_value({ christine }, victoria_supports, {}));
    victoria_support.disabled_pacts = { "奥术" };
    assert(planning_support_pacts(victoria_support, {}, operator_pacts()).empty());

    assert(!choose_bench_capacity_trade(victoria_full, 13, victoria_shop, victoria_field, { "凯瑟琳", "涤火杰西卡" }));
    victoria_full.tactical_elite_purchase_pending = true;
    assert(!choose_bench_capacity_trade(victoria_full, 13, victoria_shop, victoria_field, {}));
    // A spare outside the established lineup does not become protected just
    // because its tier equals the current shop. Keep opening flexibility.
    PreparationPolicy spare_same_tier;
    spare_same_tier.primary_pact = "莱塔尼亚";
    spare_same_tier.shop_level = 3;
    spare_same_tier.bench_occupied = 10;
    spare_same_tier.owned = { { "深巡", { 1, false, false } } };
    assert(!choose_full_bench_sale(spare_same_tier));
    spare_same_tier.primary_locked_after_activation = true;
    assert(choose_full_bench_sale(spare_same_tier) == "深巡");
    spare_same_tier.bench_occupied = 9;
    assert(!choose_full_bench_sale(spare_same_tier));
    assert(choose_full_bench_sale(spare_same_tier, {}, 1) == "深巡");
    spare_same_tier.support_pacts = { "灵巧" };
    // Explicitly protected transaction identities always take precedence.
    assert(!choose_full_bench_sale(spare_same_tier, { "深巡" }, 1));
    assert(!choose_full_bench_sale(full_bench, { "深巡", "复奏" }));
    full_bench.bench_occupied = 9;
    assert(!choose_full_bench_sale(full_bench));
    full_bench.primary_locked_after_activation = true;
    assert(choose_full_bench_sale(full_bench, {}, 1) == "深巡");
    assert(!choose_full_bench_sale(full_bench, { "深巡" }, 1));
    full_bench.primary_locked_after_activation = false;
    assert(choose_full_bench_sale(full_bench, {}, 1) == "深巡");
    PreparationPolicy activation_funding;
    activation_funding.primary_pact = "阿戈尔";
    activation_funding.bench_occupied = 1;
    activation_funding.shop_level = 1;
    activation_funding.owned = { { "幽灵鲨", { 1, false, true } },
                                 { "深巡", { 1, false, true } },
                                 { "杰克", { 1, false, false } } };
    const auto activation_catalog = operator_pacts();
    const std::vector<ShopOffer> activation_shop { { "归溟幽灵鲨", 3, 0 } };
    const auto funding = choose_funding_purchase(activation_funding, 2, activation_shop, activation_catalog);
    assert(funding && funding->offer.name == "归溟幽灵鲨" && funding->deficit == 1);
    assert(choose_full_bench_sale(activation_funding, {}, funding->deficit) == "杰克");
    assert(!choose_funding_purchase(activation_funding, 3, activation_shop, activation_catalog));
    assert(!choose_full_bench_sale(activation_funding, { "杰克" }, funding->deficit));
    activation_funding.owned["杰克"].copies = 2;
    assert(!choose_full_bench_sale(activation_funding, {}, funding->deficit));
    activation_funding.owned.erase("深巡");
    assert(!choose_funding_purchase(activation_funding, 2, activation_shop, activation_catalog));
    activation_funding.owned["深巡"] = { 1, false, true };
    activation_funding.disabled_pacts = { "阿戈尔" };
    assert(!choose_funding_purchase(activation_funding, 2, activation_shop, activation_catalog));
    full_bench.bench_occupied = 10;
    full_bench.owned["深巡"].copies = 2;
    assert(!choose_full_bench_sale(full_bench));
    full_bench.owned["深巡"] = { 1, false, true };
    assert(!choose_full_bench_sale(full_bench));
    full_bench.owned["深巡"] = { 1, false, false };
    full_bench.carries = { "深巡" };
    assert(!choose_full_bench_sale(full_bench));
    // The round-13 live run had a full bench while the Olga/Jack swap
    // awaited verification. Reserve that pair, not every unrelated unit:
    // selling Frostleaf must still permit spending the remaining 11 funds.
    PreparationPolicy pending_swap_bench;
    pending_swap_bench.primary_pact = "乌萨斯";
    pending_swap_bench.shop_level = 6;
    pending_swap_bench.bench_occupied = 10;
    pending_swap_bench.owned = { { "乌啾", { 2, false, true } },     { "伊桑", { 1, false, false } },
                                 { "伺夜", { 1, false, false } },    { "冻河", { 1, false, true } },
                                 { "奥尔佳", { 1, false, false } },  { "承曦格雷伊", { 1, false, false } },
                                 { "折桠", { 2, false, true } },     { "杰克", { 1, true, true } },
                                 { "溯光星源", { 1, false, true } }, { "烈夏", { 2, false, true } },
                                 { "真理", { 1, false, true } },     { "维伊", { 2, false, true } },
                                 { "霜叶", { 1, false, false } } };
    const auto unrelated_sale = choose_full_bench_sale(pending_swap_bench, { "奥尔佳", "杰克" });
    assert(unrelated_sale && *unrelated_sale != "奥尔佳" && *unrelated_sale != "杰克");
    assert(!pending_swap_bench.owned.at(*unrelated_sale).deployed);
    auto all_reserved = pending_swap_bench;
    all_reserved.owned = { { "奥尔佳", { 1, false, false } }, { "杰克", { 1, true, true } } };
    assert(!choose_full_bench_sale(all_reserved, { "奥尔佳", "杰克" }));
    assert(normalize_bounty_title("悬赏·特异Ⅰ") == normalize_bounty_title("悬赏·特异I"));
    assert(normalize_bounty_title("悬赏·特异Ⅰ") != normalize_bounty_title("悬赏·特异Ⅱ"));
    assert(resolve_difficulty(KeepDifficulty, 9, { 1, 9, 14 }) == 9);
    assert(resolve_difficulty(MaximumDifficulty, 9, { 1, 9, 14 }) == 14);
    assert(resolve_difficulty(20, 9, { 1, 9, 14 }) == 14);
    assert(resolve_difficulty(9, 14, { 1, 9, 14 }) == 9);
    assert(resolve_difficulty(MaximumDifficulty, 1, { 1, 2, 3 }, 4) == 3);
    assert(!resolve_difficulty(20, 1, { 1, 2, 3 }, 4));
    assert(!resolve_difficulty(0, 1, { 1 }));
    assert(!resolve_difficulty(MaximumDifficulty, 1, {}));
    DifficultyTargetStability difficulty_target;
    assert(!difficulty_target.observe(5, { 995, 72, 190, 35 }));
    assert(!difficulty_target.observe(5, { 995, 200, 190, 35 })); // Scrolling continues.
    assert(difficulty_target.observe(5, { 998, 202, 185, 34 }));  // OCR jitter in a settled row.
    assert(!difficulty_target.observe(6, { 998, 202, 185, 34 }));
    difficulty_target.clear();
    assert(!difficulty_target.observe(6, { 998, 202, 185, 34 }));
    const std::vector<BountyOffer> bounties { { "flying", 2, true }, { "ground", 2, false }, { "hard", 3, false } };
    assert(choose_bounty(bounties, false) == 1);
    assert(choose_bounty({ { "flying", 1, true }, { "ground", 2, false } }, true) == 0);
    assert(choose_bounty({ { "flying", 3, true }, { "flying-weak", 2, true } }, false) == 1);
    assert(choose_bounty({ { "unknown", 0, false }, { "ground", 2, false } }, false) == 1);
    assert(choose_bounty({}, false) == -1);
    assert(bounty_reward("将其击倒者获得3资金") == 3);
    assert(!bounty_reward("获得资金"));
    assert(!bounty_reward("获得3层数"));
    const std::vector<BountyOffer> named_bosses { { "“邪魔的利刃”悬赏", 0, false, 0, 3 },
                                                  { "“复仇者”·悬赏", 0, false, 1, 4 },
                                                  { "弑君者·悬赏", 0, false, 2, 1 } };
    assert(choose_bounty(named_bosses, false) == 2);
    assert(reacquire_bench_source({ { 277, 450 }, { 357, 450 }, { 438, 450 } }, { 357, 450 }) == 1);
    // A new leftmost candidate shifts indices; the verified slot remains.
    assert(reacquire_bench_source({ { 196, 450 }, { 277, 450 }, { 359, 452 }, { 438, 450 } }, { 357, 450 }) == 2);
    assert(reacquire_bench_source({ { 357, 450 }, { 438, 450 } }, { 357, 450 }) == 0);
    assert(!reacquire_bench_source({ { 277, 450 }, { 438, 450 } }, { 357, 450 }));
    assert(!reacquire_bench_source({ { 357, 450 }, { 361, 450 } }, { 357, 450 }));
    assert(!reacquire_bench_source({}, { 357, 450 }));
    const std::vector<std::pair<int, int>> legal_placement { { 440, 280 }, { 600, 280 }, { 760, 280 },
                                                             { 440, 440 }, { 600, 440 }, { 760, 440 } };
    const auto frontline = choose_role_placement(PlacementRole::Frontline, legal_placement, {});
    assert((frontline == std::pair { 760, 280 }));
    const std::vector<PlacementMember> upper_blocker { { 760, 280, PlacementRole::Frontline } };
    const auto lower_blocker = choose_role_placement(PlacementRole::Frontline, legal_placement, upper_blocker);
    assert((lower_blocker == std::pair { 760, 440 }));
    const auto lower_with_melee_range =
        choose_role_placement(PlacementRole::Frontline, legal_placement, upper_blocker, { { 1, 0 } });
    assert((lower_with_melee_range == std::pair { 760, 440 }));
    const auto ranged_support = choose_role_placement(PlacementRole::Ranged, legal_placement, upper_blocker);
    assert(ranged_support && ranged_support->first < frontline->first && ranged_support->second == frontline->second);
    assert(!choose_role_placement(PlacementRole::Ranged, {}, {}));
    char facing = 'E';
    const auto short_support = choose_role_placement(
        PlacementRole::Ranged,
        { { 440, 280 }, { 680, 280 }, { 760, 350 } },
        upper_blocker,
        { { 1, 0 } },
        &facing);
    assert((short_support == std::pair { 680, 280 }) && facing == 'E');
    const auto side_healer =
        choose_role_placement(PlacementRole::Healer, { { 760, 350 } }, upper_blocker, { { 1, 0 } }, &facing);
    assert(side_healer && facing == 'N');
    // Perspective changes pixel spacing. The logical grid must still favor
    // a medic whose range reaches both lane blockers over three backliners.
    const std::vector<GridPlacementCell> grid_legal { { 5, 2, 540, 310 }, { 6, 4, 720, 450 } };
    const std::vector<GridPlacementMember> grid_friends { { 6, 1, PlacementRole::Frontline, "upper" },
                                                          { 6, 3, PlacementRole::Frontline, "lower" },
                                                          { 7, 4, PlacementRole::Ranged, "back-a" },
                                                          { 8, 4, PlacementRole::Ranged, "back-b" },
                                                          { 7, 3, PlacementRole::Ranged, "back-c" } };
    const std::vector<std::pair<int, int>> grid_medic_range { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 1, 1 }, { 1, -1 } };
    const auto registered_heal =
        range_placement_preferences(PlacementRole::Healer, grid_legal, grid_friends, grid_medic_range);
    const auto both_lanes = choose_role_placement(
        PlacementRole::Healer,
        { { 540, 310 }, { 720, 450 } },
        {},
        grid_medic_range,
        &facing,
        registered_heal,
        true);
    assert((both_lanes == std::pair { 540, 310 }) && facing == 'E');
    const auto berry = season_combat_stats("褐果", false);
    assert(berry);
    const std::vector<BoardCell> berry_range(berry->basic_range.begin(), berry->basic_range.end());
    const std::vector<GridPlacementMember> berry_patients {
        { 2, 1, PlacementRole::Frontline, "拜松" },
        { 4, 2, PlacementRole::Ranged, "矩" },
        { 8, 3, PlacementRole::Frontline, "羽毛笔", 'E', false },
        { 7, 3, PlacementRole::Frontline, "赫拉格", 'E', false },
    };
    auto berry_preferences =
        range_placement_preferences(PlacementRole::Healer, { { 5, 3, 720, 450 } }, berry_patients, berry_range);
    berry_preferences.push_back({ 720, 450, 'E', 20'000 });
    assert(choose_role_placement(
        PlacementRole::Healer,
        { { 720, 450 } },
        {},
        berry_range,
        &facing,
        berry_preferences,
        true));
    assert(facing == 'W');
    assert(choose_role_placement(
        PlacementRole::Healer,
        { { 560, 420 } },
        { { 640, 350, PlacementRole::Frontline, "拜松" } },
        { { 1, 1 } },
        &facing));
    assert(facing == 'E');
    // An asymmetric local +y offset points north when facing east. Reusing
    // screen-space y directly would incorrectly favor the southern target.
    const auto asymmetric = range_placement_preferences(
        PlacementRole::Ranged,
        { { 5, 2, 540, 310 } },
        { { 6, 1, PlacementRole::Frontline, "north-east" } },
        { { 1, 1 } });
    assert(asymmetric.size() == 1 && asymmetric.front().facing == 'E');
    const auto blocked_registered = choose_role_placement(
        PlacementRole::Healer,
        { { 540, 310 }, { 720, 450 } },
        { { 540, 310, PlacementRole::Frontline, "occupied" } },
        grid_medic_range,
        &facing,
        registered_heal,
        true);
    assert((blocked_registered == std::pair { 720, 450 }));
    // Reproduce a blocker placed ahead of a short-range rear unit: prefer
    // the legal intercept cell it can cover, not the farthest visible cell.
    const std::vector<GridPlacementCell> intercept_cells { { 5, 3, 650, 400 }, { 8, 3, 900, 400 } };
    const auto supported_intercept = frontline_support_preferences(
        intercept_cells,
        { { 4, 3, PlacementRole::Ranged, "短手后排", 'E' } },
        { { "短手后排", { { 1, 0 }, { 2, 0 } } } });
    assert(
        (choose_role_placement(
             PlacementRole::Frontline,
             { { 650, 400 }, { 900, 400 } },
             {},
             {},
             &facing,
             supported_intercept) == std::pair { 650, 400 }));
    assert(facing == 'E');
    assert(
        (choose_role_placement(PlacementRole::Frontline, { { 900, 400 } }, {}, {}, &facing, supported_intercept) ==
         std::pair { 900, 400 }));
    assert(frontline_support_preferences(
               intercept_cells,
               { { 4, 3, PlacementRole::Ranged, "短手后排" } },
               { { "短手后排", { { 1, 0 } } } })
               .empty());
    assert(frontline_support_preferences(
               intercept_cells,
               { { 4, 3, PlacementRole::Healer, "治疗", 'E' } },
               { { "治疗", { { 1, 0 } } } })
               .empty());
    for (const auto& [direction, col, row] :
         std::vector<std::tuple<char, int, int>> { { 'E', 5, 3 }, { 'W', 3, 3 }, { 'N', 4, 2 }, { 'S', 4, 4 } }) {
        assert(grid_range_covers(4, 3, direction, col, row, { { 1, 0 } }));
    }
    // A spatial helper may favor the correctly facing adjacent cell, but a
    // preference cannot make an unavailable or occupied cell deployable.
    const std::vector<PlacementPreference> front_growth { { 680, 280, 'E', 1200 } };
    const auto growth_support = choose_role_placement(
        PlacementRole::Ranged,
        { { 600, 280 }, { 680, 280 }, { 600, 440 } },
        upper_blocker,
        { { 1, 0 }, { 2, 0 }, { 3, 0 } },
        &facing,
        front_growth);
    assert((growth_support == std::pair { 680, 280 }) && facing == 'E');
    assert(
        (choose_role_placement(
             PlacementRole::Ranged,
             { { 600, 280 } },
             upper_blocker,
             { { 1, 0 } },
             &facing,
             front_growth) == std::pair { 600, 280 }));
    assert(growth_recipient_offset("渡桥") == 1);
    assert(growth_profiles().at("渡桥").normal_gain == 2);
    assert(growth_profiles().at("渡桥").elite_gain == 4);
    const std::vector<DeploymentLane> straight_approach { { "lane", { { 6, 2 }, { 5, 2 }, { 4, 2 }, { 3, 2 } } } };
    const std::vector<GridPlacementMember> marksman_screen { { 4, 2, PlacementRole::Frontline, "挡路", 'E' } };
    const std::vector<BoardCell> marksman_range { { 1, 0 }, { 2, 0 }, { 3, 0 } };
    const GridPlacementMember summon_owner { 3, 2, PlacementRole::Ranged, "渡桥", 'E' };
    const std::vector<BoardCell> summon_terrain { { 4, 2 }, { 5, 2 }, { 6, 2 }, { 7, 2 }, { 2, 2 }, { 4, 3 } };
    const auto summon_targets = summon_placement_cells(
        "樱桃三号",
        summon_owner,
        marksman_range,
        summon_terrain,
        marksman_screen,
        straight_approach);
    assert((summon_targets == std::vector<BoardCell> { { 6, 2 }, { 5, 2 } }));
    assert(summon_placement_cells("机动盾牌", summon_owner, marksman_range, summon_terrain, {}, straight_approach)
               .empty());
    auto shield_owner = summon_owner;
    shield_owner.name = "涤火杰西卡";
    assert(
        (summon_placement_cells("机动盾牌", shield_owner, {}, summon_terrain, {}, straight_approach) ==
         std::vector<BoardCell> { { 4, 2 } }));
    assert(summon_placement_cells("机动盾牌", shield_owner, {}, summon_terrain, marksman_screen, straight_approach)
               .empty());
    auto wolf_owner = summon_owner;
    wolf_owner.name = "伺夜";
    assert(deployable_summon_owner("狼群") == "伺夜");
    assert(summon_placement_cells("狼群", summon_owner, marksman_range, summon_terrain, {}, straight_approach).empty());
    assert(
        summon_placement_cells(
            "狼群",
            wolf_owner,
            marksman_range,
            summon_terrain,
            marksman_screen,
            straight_approach) == summon_targets);
    const GridPlacementMember weedy { 3, 2, PlacementRole::Frontline, "温蒂", '\0' };
    const auto cannon_targets =
        summon_placement_cells("工程蓄水炮", weedy, {}, summon_terrain, marksman_screen, straight_approach);
    assert(std::find(cannon_targets.begin(), cannon_targets.end(), BoardCell { 7, 2 }) != cannon_targets.end());
    assert(std::find(cannon_targets.begin(), cannon_targets.end(), BoardCell { 4, 2 }) == cannon_targets.end());
    assert(summon_placement_cells("工程蓄水炮", wolf_owner, {}, summon_terrain, {}, straight_approach).empty());
    assert(deployable_summon_owner("铁钳号·原型机") == "白铁");
    assert(deployable_summon_owner("风雪之眼").empty());

    DeploymentUnit strayed_medic;
    strayed_medic.id = "褐果";
    strayed_medic.healer = true;
    strayed_medic.cell = { 1, 4 };
    strayed_medic.facing = 'S';
    strayed_medic.local_range = { { 1, 0 }, { 2, 0 } };
    DeploymentUnit lane_tank;
    lane_tank.id = "拜松";
    lane_tank.cell = { 4, 2 };
    lane_tank.frontline = true;
    lane_tank.block_count = 3;
    const auto relocate_medic = repair_healing_coverage(
        strayed_medic,
        { strayed_medic, lane_tank },
        straight_approach,
        { { 1, 4 }, { 4, 4 }, { 6, 2 } });
    assert(relocate_medic && relocate_medic->kind == DeploymentStepKind::Move);
    assert((relocate_medic->to == BoardCell { 4, 4 } && relocate_medic->facing == 'N'));
    strayed_medic.cell = { 4, 4 };
    const auto turn_medic =
        repair_healing_coverage(strayed_medic, { strayed_medic, lane_tank }, straight_approach, { strayed_medic.cell });
    assert(turn_medic && turn_medic->kind == DeploymentStepKind::Face && turn_medic->facing == 'N');
    strayed_medic.range_known = true;
    strayed_medic.facing = 'N';
    DeploymentUnit missed_ally;
    missed_ally.id = "矩";
    missed_ally.cell = { 7, 2 };
    assert(uncovered_healable_allies({ strayed_medic, lane_tank, missed_ally }) == std::vector<std::string> { "矩" });
    missed_ally.cell = { 4, 3 };
    assert(uncovered_healable_allies({ strayed_medic, lane_tank, missed_ally }).empty());
    missed_ally.cell = { 7, 2 };
    missed_ally.accepts_direct_healing = false;
    assert(uncovered_healable_allies({ strayed_medic, lane_tank, missed_ally }).empty());

    assert(!repair_healing_coverage(
        strayed_medic,
        { strayed_medic, lane_tank },
        straight_approach,
        { strayed_medic.cell }));

    auto second_medic = strayed_medic;
    second_medic.id = "另一名医疗";
    second_medic.cell = { 6, 2 };
    second_medic.facing = 'W';
    auto disconnected_medic = strayed_medic;
    disconnected_medic.cell = { 1, 4 };
    disconnected_medic.facing = 'S';
    assert(!repair_healing_coverage(
        disconnected_medic,
        { disconnected_medic, lane_tank, second_medic },
        straight_approach,
        { disconnected_medic.cell, { 4, 4 } }));

    // Registered field from the third difficulty-3 run, round 5.
    auto berry_in_sand = strayed_medic;
    berry_in_sand.cell = { 7, 3 };
    berry_in_sand.facing = 'E';
    berry_in_sand.local_range = berry_range;
    const std::vector<GridPlacementMember> sand_allies {
        { 7, 2, PlacementRole::Ranged, "伺夜" },        { 6, 1, PlacementRole::Ranged, "卡达" },
        { 7, 1, PlacementRole::Frontline, "拉普兰德" }, { 8, 1, PlacementRole::Frontline, "泡泡" },
        { 6, 2, PlacementRole::Ranged, "灰喉" },        { 4, 4, PlacementRole::Ranged, "至简#4.4" },
        { 3, 4, PlacementRole::Ranged, "至简#3.4" },
    };
    std::vector<DeploymentUnit> sand_field { berry_in_sand };
    for (const auto& ally : sand_allies) {
        DeploymentUnit unit;
        unit.id = ally.name;
        unit.cell = { ally.col, ally.row };
        unit.frontline = ally.role == PlacementRole::Frontline;
        unit.block_count = ally.name == "泡泡" ? 3 : ally.name == "拉普兰德" ? 2 : 0;
        sand_field.push_back(std::move(unit));
    }
    const auto sand_terrain = terrain_layout_model("m01", false);
    assert(sand_terrain);
    const auto sand_lanes = active_preparation_lanes(*sand_terrain, 5);
    const auto uncovered_in_sand = uncovered_healable_allies(sand_field).size();
    const auto sand_repair =
        repair_healing_coverage(sand_field.front(), sand_field, sand_lanes, sand_terrain->buildable);
    assert(sand_repair);
    sand_field.front().cell = sand_repair->to;
    sand_field.front().facing = sand_repair->facing;
    assert(range_covers(sand_field.front(), { 7, 1 }) && range_covers(sand_field.front(), { 8, 1 }));
    assert(uncovered_healable_allies(sand_field).size() < uncovered_in_sand);

    assert(attack_approach_value({ 3, 2 }, 'E', marksman_screen, straight_approach, marksman_range, false) > 0);
    assert(attack_approach_value({ 3, 2 }, 'N', marksman_screen, straight_approach, marksman_range, false) == 0);
    assert(needs_screened_position("矩") && !needs_screened_position("拜松"));
    const auto ju_stats = season_combat_stats("矩", false);
    assert(ju_stats);
    const std::vector<BoardCell> ju_range(ju_stats->basic_range.begin(), ju_stats->basic_range.end());
    assert(!grid_range_covers(3, 2, 'E', 4, 2, ju_range));
    assert(grid_range_covers(3, 2, 'E', 5, 2, ju_range));
    assert(grid_range_covers(3, 2, 'E', 7, 2, ju_range));

    ReplacementMember forward, behind, siege_sniper;
    forward.name = "摆渡人";
    forward.role = PlacementRole::Frontline;
    forward.x = 877;
    forward.y = 234;
    forward.replaceable = true;
    behind.name = "灰喉";
    behind.role = PlacementRole::Ranged;
    behind.x = 553;
    behind.y = 438;
    behind.replaceable = true;
    siege_sniper.name = "矩";
    siege_sniper.role = PlacementRole::Ranged;
    siege_sniper.value = 10'000;
    siege_sniper.pacts = { "炎" };
    assert(!screened_replacement_cell(forward, { forward, behind }));
    std::vector<ReplacementMember> ju_replacement_field { forward, behind };
    assert(register_replacement_routes(
        ju_replacement_field,
        { { 5, 2, forward.role, forward.name }, { 3, 2, behind.role, behind.name } },
        straight_approach));
    assert(!screened_replacement_cell(ju_replacement_field[0], ju_replacement_field));
    assert(screened_replacement_cell(ju_replacement_field[1], ju_replacement_field));
    const auto ju_replacement = choose_replacement("炎", ju_replacement_field, { siege_sniper });
    assert(ju_replacement.found && ju_replacement.outgoing == behind.name);

    assert(is_front_growth_target("空弦", "斥罪", "叙拉古"));
    assert(!is_front_growth_target("空弦", "斥罪", "萨尔贡"));
    assert(is_front_growth_target("华法琳", "泡泡", "萨尔贡"));
    assert(!is_front_growth_target("华法琳", "伺夜", "叙拉古"));
    assert(is_front_growth_target("阿米娅", "伺夜", "叙拉古"));
    assert(!is_front_growth_target("阿米娅", "复奏", "叙拉古"));
    assert(is_front_growth_target("塞雷娅", "折光", "莱塔尼亚"));
    assert(!is_front_growth_target("塞雷娅", "地灵", "莱塔尼亚"));
    // Front and back are relative to the source's facing, not screen x.
    const std::vector<GridPlacementCell> spatial_cells { { 2, 1, 500, 250 },
                                                         { 3, 2, 590, 325 },
                                                         { 4, 1, 680, 250 },
                                                         { 3, 0, 590, 175 } };
    const std::vector<GridPlacementMember> growth_receiver { { 3, 1, PlacementRole::Frontline, "泡泡" } };
    const auto spatial_bonus = [](const auto& preferences, int x, int y, char direction) {
        int total = 0;
        for (const auto& preference : preferences) {
            if (preference.x == x && preference.y == y && preference.facing == direction) {
                total += preference.bonus;
            }
        }
        return total;
    };
    const auto forward_growth =
        spatial_growth_preferences("空弦", spatial_cells, growth_receiver, { "萨尔贡" }, "萨尔贡");
    assert(spatial_bonus(forward_growth, 500, 250, 'E') == 1200);
    assert(spatial_bonus(forward_growth, 590, 325, 'N') == 1200);
    assert(spatial_bonus(forward_growth, 590, 325, 'S') == 0);
    const auto back_growth = spatial_growth_preferences("青枳", spatial_cells, growth_receiver, { "萨尔贡" }, "萨尔贡");
    assert(spatial_bonus(back_growth, 500, 250, 'W') == 1200);
    assert(spatial_bonus(back_growth, 590, 325, 'S') == 1200);
    assert(spatial_bonus(back_growth, 500, 250, 'E') == 0);
    char swapped_helper_facing = 'N';
    assert(choose_role_placement(
        PlacementRole::Ranged,
        { { 500, 250 } },
        { { 590, 250, PlacementRole::Frontline, "泡泡" } },
        { { 1, 0 } },
        &swapped_helper_facing,
        forward_growth));
    assert(swapped_helper_facing == 'E');
    assert(replacement_growth_facing("空弦", { 2, 1, 500, 250 }, growth_receiver, { "萨尔贡" }, "萨尔贡", 'N') == 'E');
    assert(replacement_growth_facing("青枳", { 2, 1, 500, 250 }, growth_receiver, { "萨尔贡" }, "萨尔贡", 'E') == 'W');
    assert(replacement_growth_facing("空弦", { 2, 1, 500, 250 }, growth_receiver, {}, "萨尔贡", 'N') == 'N');
    assert(replacement_growth_facing("空弦", { 2, 1, 500, 250 }, growth_receiver, { "萨尔贡" }, "萨尔贡", 'E') == 'E');
    assert(
        replacement_growth_facing(
            "泡泡",
            { 3, 2, 590, 325 },
            { { 3, 1, PlacementRole::Ranged, "空弦", 'S' } },
            { "萨尔贡" },
            "萨尔贡",
            'W') == 'W');
    assert(spatial_growth_preferences("青枳", spatial_cells, growth_receiver, {}, "萨尔贡").empty());
    const auto secondary_growth =
        spatial_growth_preferences("青枳", spatial_cells, growth_receiver, { "坚守", "坚守" }, "萨尔贡");
    assert(spatial_bonus(secondary_growth, 500, 250, 'W') == 600);
    // A helper already deployed before its recipient retains its direction.
    const auto receiver_after_helper = spatial_growth_preferences(
        "泡泡",
        { { 3, 2, 590, 325 }, { 2, 1, 500, 250 } },
        { { 3, 1, PlacementRole::Ranged, "空弦", 'S' } },
        { "萨尔贡" },
        "萨尔贡");
    assert(spatial_bonus(receiver_after_helper, 590, 325, 'E') == 1200);
    assert(spatial_bonus(receiver_after_helper, 500, 250, 'E') == 0);
    assert(spatial_growth_preferences(
               "泡泡",
               spatial_cells,
               { { 3, 1, PlacementRole::Ranged, "空弦" } },
               { "萨尔贡" },
               "萨尔贡")
               .empty());
    // Row membership uses registered row indices, independent of perspective.
    const auto row_growth = spatial_growth_preferences(
        "罗比菈塔",
        { { 4, 1, 680, 265 }, { 4, 2, 680, 266 } },
        { { 1, 1, PlacementRole::Frontline, "泡泡" }, { 2, 1, PlacementRole::Frontline, "蛇屠箱" } },
        { "坚守" },
        "萨尔贡");
    assert(spatial_bonus(row_growth, 680, 265, 'E') == 1800);
    assert(spatial_bonus(row_growth, 680, 266, 'E') == 600);
    const auto add_row_member = spatial_growth_preferences(
        "泡泡",
        { { 4, 1, 680, 265 }, { 4, 2, 680, 266 } },
        { { 1, 1, PlacementRole::Frontline, "罗比菈塔", 'E' } },
        { "助力" },
        "萨尔贡");
    assert(spatial_bonus(add_row_member, 680, 265, 'E') == 600);
    assert(spatial_bonus(add_row_member, 680, 266, 'E') == 0);
    const std::vector<GridPlacementMember> population_grid { { 1, 1, PlacementRole::Ranged, "罗比菈塔" },
                                                             { 2, 1, PlacementRole::Frontline, "泡泡" },
                                                             { 3, 1, PlacementRole::Frontline, "蛇屠箱" },
                                                             { 1, 2, PlacementRole::Ranged, "罗比菈塔" },
                                                             { 2, 2, PlacementRole::Frontline, "慕斯" } };
    const auto row_populations = same_row_growth_populations(population_grid);
    assert(row_populations && row_populations->at("罗比菈塔") == 5);
    auto duplicate_population = population_grid;
    duplicate_population[4].col = 1;
    assert(!same_row_growth_populations(duplicate_population));
    duplicate_population = population_grid;
    duplicate_population[4].row = -1;
    assert(!same_row_growth_populations(duplicate_population));
    assert(!same_row_growth_populations({}));
    assert(spatial_growth_preferences("罗比菈塔", spatial_cells, growth_receiver, {}, "萨尔贡").empty());
    assert(spatial_growth_target("空弦", "空弦", "精准"));
    assert(!spatial_growth_target("凛御银灰", "角峰", "谢拉格"));
    assert(spatial_growth_target("凛御银灰", "圣聆初雪", "谢拉格"));
    assert(spatial_growth_target("Mon3tr", "冻河", "乌萨斯"));
    const auto iberia_sale_support = spatial_growth_preferences(
        "旅骨",
        { { 2, 1, 500, 250 }, { 3, 2, 590, 325 } },
        { { 3, 1, PlacementRole::Frontline, "温蒂" }, { 4, 2, PlacementRole::Ranged, "深靛" } },
        { "伊比利亚" },
        "伊比利亚");
    assert(spatial_bonus(iberia_sale_support, 500, 250, 'E') == 1800);
    assert(spatial_bonus(iberia_sale_support, 590, 325, 'E') == 1200);
    assert(spatial_bonus(iberia_sale_support, 500, 250, 'W') == 0);
    assert(
        spatial_formation_value(
            { { 2, 1, PlacementRole::Ranged, "旅骨", 'E' }, { 3, 1, PlacementRole::Frontline, "海沫", 'E' } },
            { "伊比利亚" },
            "伊比利亚") == 1800);
    assert(
        spatial_formation_value(
            { { 3, 2, PlacementRole::Ranged, "空弦", 'E' }, { 4, 2, PlacementRole::Frontline, "泡泡", 'E' } },
            { "萨尔贡", "萨尔贡" },
            "萨尔贡") == 1200);
    assert(
        spatial_formation_value(
            { { 3, 2, PlacementRole::Ranged, "空弦" }, { 4, 2, PlacementRole::Frontline, "泡泡", 'E' } },
            { "萨尔贡" },
            "萨尔贡") == 0);
    assert(
        spatial_formation_value(
            { { 3, 2, PlacementRole::Frontline, "罗比菈塔", 'E' },
              { 4, 2, PlacementRole::Frontline, "泡泡", 'E' },
              { 5, 2, PlacementRole::Frontline, "角峰", 'E' } },
            { "坚守", "助力" },
            "萨尔贡") == 3600);
    assert(!spatial_growth_target("Mon3tr", "冻河", "谢拉格"));
    assert(!spatial_growth_target("Mon3tr", "渡桥", "乌萨斯"));
    assert(!spatial_growth_target("Mon3tr", "空弦", "精准"));
    assert(!spatial_growth_target("Mon3tr", "可露希尔", "投资人"));
    const std::vector<GridPlacementCell> aura_cells { { 4, 2, 680, 320 },
                                                      { 4, 3, 680, 390 },
                                                      { 5, 2, 770, 320 },
                                                      { 5, 3, 770, 390 } };
    const auto add_battle_engine = spatial_growth_preferences(
        "冻河",
        aura_cells,
        { { 3, 2, PlacementRole::Healer, "Mon3tr" } },
        { "乌萨斯" },
        "乌萨斯");
    for (const char facing : { 'E', 'N', 'S', 'W' }) {
        assert(spatial_bonus(add_battle_engine, 680, 320, facing) == 1200);
        assert(spatial_bonus(add_battle_engine, 680, 390, facing) == 1200);
        assert(spatial_bonus(add_battle_engine, 770, 320, facing) == 0);
        assert(spatial_bonus(add_battle_engine, 770, 390, facing) == 0);
    }
    const auto add_cap_helper = spatial_growth_preferences(
        "Mon3tr",
        aura_cells,
        { { 3, 2, PlacementRole::Ranged, "冻河" } },
        { "乌萨斯" },
        "乌萨斯");
    assert(spatial_bonus(add_cap_helper, 680, 390, 'E') == 1200);
    assert(spatial_growth_preferences("冻河", aura_cells, { { 3, 2, PlacementRole::Healer, "Mon3tr" } }, {}, "乌萨斯")
               .empty());
    const std::map<std::string, std::vector<std::pair<int, int>>> rotation_ranges { { "空弦", { { 1, 0 } } } };
    const std::vector<GridPlacementMember> rotate_helper { { 3, 2, PlacementRole::Ranged, "空弦", 'E' },
                                                           { 3, 1, PlacementRole::Frontline, "泡泡", 'E' },
                                                           { 4, 2, PlacementRole::Frontline, "慕斯", 'E' } };
    const auto turn_north = choose_spatial_reorientation(rotate_helper, { "萨尔贡" }, "萨尔贡", rotation_ranges);
    assert(
        turn_north && turn_north->name == "空弦" && turn_north->col == 3 && turn_north->row == 2 &&
        turn_north->previous == 'E' && turn_north->direction == 'N' && turn_north->growth_improvement == 1200);
    auto already_aligned = rotate_helper;
    already_aligned.front().facing = 'N';
    assert(!choose_spatial_reorientation(already_aligned, { "萨尔贡" }, "萨尔贡", rotation_ranges));
    assert(!choose_spatial_reorientation(rotate_helper, {}, "萨尔贡", rotation_ranges));
    assert(!choose_spatial_reorientation(rotate_helper, { "萨尔贡" }, "萨尔贡", {}));
    auto unknown_direction = rotate_helper;
    unknown_direction.front().facing = '\0';
    assert(!choose_spatial_reorientation(unknown_direction, { "萨尔贡" }, "萨尔贡", rotation_ranges));
    auto duplicate_other = rotate_helper;
    duplicate_other.push_back({ 5, 3, PlacementRole::Frontline, "慕斯", 'E' });
    assert(choose_spatial_reorientation(duplicate_other, { "萨尔贡" }, "萨尔贡", rotation_ranges));
    auto duplicate_source = rotate_helper;
    duplicate_source.push_back({ 5, 3, PlacementRole::Ranged, "空弦", 'E' });
    assert(!choose_spatial_reorientation(duplicate_source, { "萨尔贡" }, "萨尔贡", rotation_ranges));
    auto overlapping_actors = rotate_helper;
    overlapping_actors.back().col = 3;
    overlapping_actors.back().row = 1;
    assert(!choose_spatial_reorientation(overlapping_actors, { "萨尔贡" }, "萨尔贡", rotation_ranges));
    // An extra front recipient does not justify losing two healing targets.
    const std::vector<GridPlacementMember> keep_healing { { 3, 2, PlacementRole::Healer, "华法琳", 'E' },
                                                          { 3, 1, PlacementRole::Frontline, "泡泡", 'E' },
                                                          { 4, 2, PlacementRole::Frontline, "慕斯", 'E' },
                                                          { 5, 2, PlacementRole::Frontline, "角峰", 'E' } };
    assert(
        !choose_spatial_reorientation(keep_healing, { "萨尔贡" }, "萨尔贡", { { "华法琳", { { 1, 0 }, { 2, 0 } } } }));
    // Rotating the source rotates its recipient. Diagonal/distant cells and
    // inactive recipient pacts must never receive an adjacency preference.
    const std::vector<GridPlacementCell> compass_cells { { 4, 2, 680, 320 }, { 2, 2, 500, 320 }, { 3, 1, 590, 250 },
                                                         { 3, 3, 590, 390 }, { 4, 3, 680, 390 }, { 5, 2, 770, 320 } };
    for (const auto& [direction, front, back] :
         std::vector<std::tuple<char, std::pair<int, int>, std::pair<int, int>>> {
             { 'E', { 680, 320 }, { 500, 320 } },
             { 'W', { 500, 320 }, { 680, 320 } },
             { 'N', { 590, 250 }, { 590, 390 } },
             { 'S', { 590, 390 }, { 590, 250 } } }) {
        const auto ahead = spatial_growth_preferences(
            "泡泡",
            compass_cells,
            { { 3, 2, PlacementRole::Ranged, "空弦", direction } },
            { "萨尔贡" },
            "萨尔贡");
        assert(spatial_bonus(ahead, front.first, front.second, 'E') == 1200);
        assert(spatial_bonus(ahead, back.first, back.second, 'E') == 0);
        assert(spatial_bonus(ahead, 680, 390, 'E') == 0);
        assert(spatial_bonus(ahead, 770, 320, 'E') == 0);
        const auto behind = spatial_growth_preferences(
            "泡泡",
            compass_cells,
            { { 3, 2, PlacementRole::Frontline, "青枳", direction } },
            { "萨尔贡" },
            "萨尔贡");
        assert(spatial_bonus(behind, back.first, back.second, 'E') == 1200);
        assert(spatial_bonus(behind, front.first, front.second, 'E') == 0);
    }
    assert(spatial_growth_preferences(
               "泡泡",
               compass_cells,
               { { 3, 2, PlacementRole::Ranged, "空弦", 'E' } },
               { "精准" },
               "精准")
               .empty());
    // Explicit growth-facing preferences can turn a ground helper north.
    const auto ground_helper =
        choose_role_placement(PlacementRole::Frontline, { { 590, 325 } }, {}, {}, &facing, { { 590, 325, 'N', 1200 } });
    assert(ground_helper && facing == 'N');
    const std::vector<GridPlacementMember> preview_field { { 4, 1, PlacementRole::Ranged, "灰喉", 'S' },
                                                           { 4, 2, PlacementRole::Ranged, "安哲拉", 'E' },
                                                           { 8, 4, PlacementRole::Healer, "夏栎", 'N' } };
    const std::map<std::string, bool> preview_forms { { "灰喉", false }, { "安哲拉", true }, { "夏栎", false } };
    const auto preview_failure =
        replacement_target_failure_key("m12", "寒檀", false, 1, { 4, 1 }, preview_field, preview_forms);
    assert(preview_failure);
    auto far_change = preview_field;
    far_change.back().col = 9;
    assert(
        replacement_target_failure_key("m12", "寒檀", false, 1, { 4, 1 }, far_change, preview_forms) ==
        preview_failure);
    auto near_change = preview_field;
    near_change[1].col = 5;
    assert(
        replacement_target_failure_key("m12", "寒檀", false, 1, { 4, 1 }, near_change, preview_forms) !=
        preview_failure);
    assert(
        replacement_target_failure_key("m12", "寒檀", true, 1, { 4, 1 }, preview_field, preview_forms) !=
        preview_failure);
    assert(!replacement_target_failure_key("m12", "寒檀", false, 1, { 4, 1 }, preview_field, {}));
    assert(!replacement_target_failure_key("m12", "寒檀", false, 1, { 3, 1 }, preview_field, preview_forms));
    const DeploymentStep failed_move { DeploymentStepKind::Move, "灰喉", "", { 4, 1 }, { 3, 2 }, 'E' };
    const auto green_failure = spatial_preview_failure_key("m12#normal", 13, failed_move, preview_field, preview_forms);
    assert(green_failure);
    auto turned_field = preview_field;
    turned_field[1].facing = 'N';
    assert(spatial_preview_failure_key("m12#normal", 13, failed_move, turned_field, preview_forms) == green_failure);
    assert(spatial_preview_failure_key("m12#normal", 14, failed_move, preview_field, preview_forms) != green_failure);
    assert(spatial_preview_failure_key("m12#normal", 13, failed_move, far_change, preview_forms) == green_failure);
    assert(spatial_preview_failure_key("m12#normal", 13, failed_move, near_change, preview_forms) != green_failure);
    auto moved_source = preview_field;
    moved_source[0].col = 6;
    auto moved_attempt = failed_move;
    moved_attempt.from = { 6, 1 };
    assert(spatial_preview_failure_key("m12#normal", 13, moved_attempt, moved_source, preview_forms) == green_failure);
    DeploymentSequence later_failure;
    later_failure.complete = true;
    later_failure.steps = { { DeploymentStepKind::Move, "夏栎", "", { 8, 4 }, { 9, 4 }, 'N' }, failed_move };
    int checked_steps = 0;
    assert(!spatial_sequence_admissible(later_failure, preview_field, [&](const auto& step, const auto& field) {
        ++checked_steps;
        return spatial_preview_failure_key("m12#normal", 13, step, field, preview_forms) != green_failure;
    }));
    assert(checked_steps == 2);
    assert(spatial_sequence_admissible(later_failure, preview_field, [](const auto&, const auto&) { return true; }));
    auto invalid_sequence = later_failure;
    invalid_sequence.steps.back().from = { 0, 0 };
    assert(
        !spatial_sequence_admissible(invalid_sequence, preview_field, [](const auto&, const auto&) { return true; }));
    const auto local_failure = spatial_action_failure_key("m12#normal", failed_move, preview_field, preview_forms);
    assert(local_failure);
    assert(spatial_action_failure_key("m12#normal", failed_move, far_change, preview_forms) == local_failure);
    assert(spatial_action_failure_key("m12#normal", failed_move, near_change, preview_forms) != local_failure);
    auto distant_recruits = preview_field;
    distant_recruits.push_back({ 3, 4, PlacementRole::Frontline, "野鬃", 'E' });
    distant_recruits.push_back({ 4, 4, PlacementRole::Ranged, "卡达", 'E' });
    assert(spatial_action_failure_key("m12#normal", failed_move, distant_recruits, preview_forms) == local_failure);
    assert(spatial_action_failure_key("m12#boss", failed_move, preview_field, preview_forms) != local_failure);
    auto promoted_forms = preview_forms;
    promoted_forms["灰喉"] = true;
    assert(spatial_action_failure_key("m12#normal", failed_move, preview_field, promoted_forms) != local_failure);
    auto changed_target = failed_move;
    changed_target.to = { 5, 2 };
    assert(spatial_action_failure_key("m12#normal", changed_target, preview_field, preview_forms) != local_failure);
    assert(!spatial_action_failure_key("m12#normal", failed_move, preview_field, {}));
    changed_target.from = { 4, 0 };
    assert(!spatial_action_failure_key("m12#normal", changed_target, preview_field, preview_forms));
    const DeploymentStep failed_face { DeploymentStepKind::Face, "灰喉", "", { 4, 1 }, { 4, 1 }, 'E' };
    const auto face_failure = spatial_action_failure_key("m12#normal", failed_face, preview_field, preview_forms);
    assert(face_failure && face_failure != local_failure);
    assert(spatial_action_failure_key("m12#normal", failed_face, distant_recruits, preview_forms) == face_failure);
    const auto failed_receiver = spatial_receiver_failure_key("m12#normal", failed_face, preview_field, preview_forms);
    assert(failed_receiver && failed_receiver != face_failure);
    for (char direction : std::string("SNW")) {
        auto changed_facing = failed_face;
        changed_facing.facing = direction;
        assert(spatial_action_failure_key("m12#normal", changed_facing, preview_field, preview_forms) != face_failure);
        assert(
            spatial_receiver_failure_key("m12#normal", changed_facing, preview_field, preview_forms) ==
            failed_receiver);
    }
    assert(spatial_receiver_failure_key("m12#normal", failed_face, distant_recruits, preview_forms) == failed_receiver);
    assert(spatial_receiver_failure_key("m12#normal", failed_face, near_change, preview_forms) != failed_receiver);
    assert(spatial_receiver_failure_key("m12#normal", failed_face, preview_field, promoted_forms) != failed_receiver);
    assert(spatial_receiver_failure_key("m12#boss", failed_face, preview_field, preview_forms) != failed_receiver);
    DeploymentStep rejected_drop { DeploymentStepKind::Move, "摆渡人", {}, { 8, 1 }, { 7, 1 }, 'E' };
    const auto drop_key = spatial_vacant_drop_failure_key("m01#normal", rejected_drop, false);
    assert(drop_key);
    rejected_drop.facing = 'S';
    assert(spatial_vacant_drop_failure_key("m01#normal", rejected_drop, false) == drop_key);
    assert(spatial_vacant_drop_failure_key("m01#normal", rejected_drop, true) != drop_key);
    assert(spatial_vacant_drop_failure_key("m01#boss", rejected_drop, false) != drop_key);
    rejected_drop.from = { 8, 3 };
    assert(spatial_vacant_drop_failure_key("m01#normal", rejected_drop, false) != drop_key);
    rejected_drop.kind = DeploymentStepKind::Swap;
    assert(!spatial_vacant_drop_failure_key("m01#normal", rejected_drop, false));
    const std::vector<ReplacementMember> transition_board {
        { "艾丝黛尔", PlacementRole::Frontline, { "萨尔贡" }, 500, 800, 235, true },
        { "泡泡", PlacementRole::Frontline, { "萨尔贡" }, 500, 880, 437, true },
        { "野鬃", PlacementRole::Frontline, { "卡西米尔" }, 120, 720, 235, true },
        { "野鬃", PlacementRole::Frontline, { "卡西米尔" }, 120, 720, 437, true },
        { "卡达", PlacementRole::Ranged, { "奥术" }, 300, 640, 300, true },
        { "褐果", PlacementRole::Healer, { "聚合" }, 300, 640, 370, true }
    };
    const auto activate_sargon =
        choose_replacement("萨尔贡", transition_board, { { "森蚺", PlacementRole::Frontline, { "萨尔贡" }, 1400 } });
    assert(activate_sargon.found && activate_sargon.incoming == "森蚺" && activate_sargon.outgoing == "野鬃");
    assert(is_strategy_core("band_mech", "“暴躁铁皮”"));
    assert(!is_strategy_core("band_mech", "森蚺"));
    assert(!is_strategy_core("band_cooperation", "“暴躁铁皮”"));
    auto core_field = transition_board;
    for (auto& member : core_field) {
        member.replaceable = false;
    }
    core_field[2].name = "“暴躁铁皮”";
    core_field[2].replaceable = true;
    core_field[2].strategy_core = true;
    const ReplacementMember faction_relay { "深巡", PlacementRole::Frontline, { "阿戈尔" }, 30'000 };
    assert(!choose_replacement("阿戈尔", core_field, { faction_relay }).found);
    auto missing_core_field = core_field;
    missing_core_field[2].name = "过渡干员";
    missing_core_field[2].strategy_core = false;
    auto missing_core = core_field[2];
    missing_core.value = 0;
    const auto restored_core = choose_replacement("阿戈尔", missing_core_field, { faction_relay, missing_core });
    assert(restored_core.found && restored_core.incoming == "“暴躁铁皮”" && restored_core.restores_strategy_core);
    assert(restored_core.improvement < 0); // Foundation restoration is not a fabricated positive faction score.
    missing_core_field[2].reliable_blocker = false;
    missing_core.reliable_blocker = false;
    // A strategy label cannot bypass the route-preservation constraint.
    auto sole_core_route = core_field;
    sole_core_route[2].strategy_core = false;
    sole_core_route[2].name = "唯一阻挡者";
    for (auto& member : sole_core_route) {
        member.route_membership_registered = true;
        member.terrain_routes.clear();
    }
    sole_core_route[2].terrain_routes = { "special" };
    assert(!choose_replacement("阿戈尔", sole_core_route, { missing_core }).found);
    auto rear_guard_board = transition_board;
    for (auto& member : rear_guard_board) {
        member.replaceable = false;
    }
    rear_guard_board.push_back({ "圣聆初雪", PlacementRole::Ranged, {}, 100, 471, 368, true });
    const std::vector<ReplacementMember> spare_guards { { "杰克", PlacementRole::Frontline, {}, 9000 } };
    assert(!choose_replacement("萨尔贡", rear_guard_board, spare_guards).found);
    rear_guard_board.back().route_membership_registered = true;
    assert(!choose_replacement("萨尔贡", rear_guard_board, spare_guards).found);
    rear_guard_board.back().terrain_routes = { "upper" };
    assert(choose_replacement("萨尔贡", rear_guard_board, spare_guards).found);
    auto elite_transition = transition_board;
    for (auto& member : elite_transition) {
        member.replaceable = false;
    }
    elite_transition[2].name = "杰克";
    elite_transition[2].value += 350;
    elite_transition[2].replaceable = replacement_outgoing_identity_trackable(true, 1, 1);
    const auto replace_elite =
        choose_replacement("萨尔贡", elite_transition, { { "森蚺", PlacementRole::Frontline, { "萨尔贡" }, 1400 } });
    assert(replace_elite.found && replace_elite.outgoing == "杰克" && replace_elite.incoming == "森蚺");
    elite_transition[2].replaceable = replacement_outgoing_identity_trackable(true, 2, 2);
    assert(!choose_replacement("萨尔贡", elite_transition, { { "森蚺", PlacementRole::Frontline, { "萨尔贡" }, 1400 } })
                .found);
    const auto alternate_target = choose_replacement(
        "萨尔贡",
        transition_board,
        { { "森蚺", PlacementRole::Frontline, { "萨尔贡" }, 1400 } },
        { { activate_sargon.incoming, activate_sargon.outgoing, activate_sargon.x, activate_sargon.y } });
    assert(
        alternate_target.found && (alternate_target.x != activate_sargon.x || alternate_target.y != activate_sargon.y));
    const ReplacementMember lappland { "拉普兰德", PlacementRole::Ranged, {}, 100, 797, 234, true };
    ReplacementMember ferryman { "摆渡人", PlacementRole::Ranged, {}, 300, 798, 235, true };
    const auto first_exchange = choose_replacement("维多利亚", { lappland }, { ferryman });
    assert(first_exchange.found && first_exchange.incoming == "摆渡人");
    ferryman.value = 50;
    auto improved_lappland = lappland;
    improved_lappland.value = 800;
    const ReplacementMember meteorite { "陨星", PlacementRole::Ranged, {}, 700 };
    const std::set<ReplacementLineup> visited { replacement_lineup_identity({ lappland }),
                                                replacement_lineup_identity({ ferryman }) };
    assert(choose_replacement("维多利亚", { ferryman }, { improved_lappland }).found);
    assert(!choose_replacement("维多利亚", { ferryman }, { improved_lappland }, {}, {}, visited).found);
    const auto fresh_exchange =
        choose_replacement("维多利亚", { ferryman }, { improved_lappland, meteorite }, {}, {}, visited);
    assert(fresh_exchange.found && fresh_exchange.incoming == "陨星");
    auto activated_board = transition_board;
    activated_board[2] = { "森蚺", PlacementRole::Frontline, { "萨尔贡" }, 1400, 720, 235, true };
    activated_board[0].replaceable = activated_board[1].replaceable = true;
    for (size_t index = 2; index < activated_board.size(); ++index) {
        activated_board[index].replaceable = false;
    }
    assert(!choose_replacement("萨尔贡", activated_board, { { "主输出", PlacementRole::Ranged, { "其他阵营" }, 9000 } })
                .found);
    const std::vector<ReplacementMember> two_blockers {
        { "上路", PlacementRole::Frontline, { "萨尔贡" }, 100, 800, 235, true },
        { "下路", PlacementRole::Frontline, { "萨尔贡" }, 100, 800, 437, true }
    };
    // The m05 road bends through (7,3). Both R6 Jack positions intercept it;
    // route evidence must permit replacing one while another remains.
    std::vector<ReplacementMember> route_replacement {
        { "烈夏", PlacementRole::Frontline, { "乌萨斯" }, 100, 719, 229, false },
        { "杰克", PlacementRole::Frontline, {}, 100, 814, 373, true },
        { "杰克", PlacementRole::Frontline, {}, 100, 982, 437, true },
        { "真理", PlacementRole::Ranged, { "乌萨斯" }, 600, 722, 299, false }
    };
    const auto city_route = terrain_layout_model("m05");
    assert(city_route);
    assert(register_replacement_routes(
        route_replacement,
        { { 6, 1, PlacementRole::Frontline, "烈夏" },
          { 7, 3, PlacementRole::Frontline, "杰克" },
          { 9, 4, PlacementRole::Frontline, "杰克" },
          { 6, 2, PlacementRole::Ranged, "真理" } },
        city_route->lanes));
    assert(route_replacement[1].terrain_routes.size() == 2);
    assert(route_replacement[2].terrain_routes.size() == 2);
    const auto keep_interceptor =
        choose_replacement("乌萨斯", route_replacement, { { "灰喉", PlacementRole::Ranged, { "乌萨斯" }, 1500 } });
    assert(keep_interceptor.found && keep_interceptor.x == 814 && keep_interceptor.y == 373);
    // Counterexample: a guard on a buildable fence does not secure the road,
    // even when perspective puts both guards in the same screen-height band.
    std::vector<ReplacementMember> fence_replacement {
        { "围栏近卫", PlacementRole::Frontline, { "乌萨斯" }, 100, 800, 360, false },
        { "通路近卫", PlacementRole::Frontline, {}, 100, 900, 420, true },
        { "真理", PlacementRole::Ranged, { "乌萨斯" }, 600, 600, 320, false }
    };
    assert(register_replacement_routes(
        fence_replacement,
        { { 6, 2, PlacementRole::Frontline, "围栏近卫" },
          { 9, 4, PlacementRole::Frontline, "通路近卫" },
          { 4, 2, PlacementRole::Ranged, "真理" } },
        city_route->lanes));
    assert(fence_replacement[0].terrain_routes.empty());
    assert(!choose_replacement("乌萨斯", fence_replacement, { { "灰喉", PlacementRole::Ranged, { "乌萨斯" }, 15'000 } })
                .found);
    // A real guard can inherit the same interception cell.
    assert(choose_replacement(
               "乌萨斯",
               fence_replacement,
               { { "凛御银灰", PlacementRole::Frontline, { "乌萨斯" }, 1500 } })
               .found);
    auto contradictory_route = route_replacement;
    assert(!register_replacement_routes(contradictory_route, {}, city_route->lanes));
    assert(std::none_of(contradictory_route.begin(), contradictory_route.end(), [](const auto& member) {
        return member.route_membership_registered;
    }));
    assert(
        combat_placement_role(asst::battle::Role::Warrior, asst::battle::SubRole::Warrior_Liberator) ==
        PlacementRole::Ranged);
    assert(
        combat_placement_role(asst::battle::Role::Pioneer, asst::battle::SubRole::Pioneer_Tactician) ==
        PlacementRole::Ranged);
    assert(
        combat_placement_role(asst::battle::Role::Warrior, asst::battle::SubRole::Warrior_Lord) ==
        PlacementRole::Frontline);
    assert(
        combat_placement_role(asst::battle::Role::Support, asst::battle::SubRole::Support_Craftsman) ==
        PlacementRole::Frontline);
    assert(
        combat_placement_role(asst::battle::Role::Special, asst::battle::SubRole::Special_Executor) ==
        PlacementRole::Frontline);
    assert(
        combat_placement_role(asst::battle::Role::Support, asst::battle::SubRole::Support_Bard) ==
        PlacementRole::Healer);
    assert(
        combat_placement_role(asst::battle::Role::Special, asst::battle::SubRole::Special_Stalker) ==
        PlacementRole::Ranged);
    assert(
        combat_placement_role(asst::battle::Role::Unknown, asst::battle::SubRole::Unknown) == PlacementRole::Unknown);
    const ReplacementMember liberator {
        "玛恩纳",
        combat_placement_role(asst::battle::Role::Warrior, asst::battle::SubRole::Warrior_Liberator),
        { "卡西米尔" },
        3000
    };
    // A strong liberator must not replace the last normal blocker in a lane.
    assert(!choose_replacement("萨尔贡", two_blockers, { liberator }).found);
    auto blocker_output_board = two_blockers;
    blocker_output_board.push_back({ "过渡输出", PlacementRole::Ranged, {}, 100, 640, 300, true });
    const auto liberator_upgrade = choose_replacement("萨尔贡", blocker_output_board, { liberator });
    assert(liberator_upgrade.found && liberator_upgrade.outgoing == "过渡输出");
    const std::vector<ReplacementMember> sustained_board {
        { "上路", PlacementRole::Frontline, {}, 100, 800, 235, false },
        { "下路", PlacementRole::Frontline, {}, 100, 800, 437, false },
        { "输出1", PlacementRole::Ranged, {}, 100, 640, 235, false },
        { "输出2", PlacementRole::Ranged, {}, 100, 640, 437, false },
        { "唯一治疗", PlacementRole::Healer, {}, 100, 640, 300, true }
    };
    assert(!choose_replacement("乌萨斯", sustained_board, { { "辅助叠层", PlacementRole::Ranged, {}, 1200 } }).found);
    // No blanket healer gate: a sufficiently strong output can still win
    // the trade, and direct healing has no value for ineligible recipients.
    assert(choose_replacement("乌萨斯", sustained_board, { { "终端", PlacementRole::Ranged, {}, 9000 } }).found);
    auto self_sustained = sustained_board;
    for (auto& member : self_sustained) {
        member.accepts_direct_healing = false;
    }
    assert(choose_replacement("乌萨斯", self_sustained, { { "辅助叠层", PlacementRole::Ranged, {}, 1200 } }).found);
    self_sustained.back().provides_regeneration = true;
    assert(!choose_replacement("乌萨斯", self_sustained, { { "辅助叠层", PlacementRole::Ranged, {}, 1200 } }).found);
    assert(!accepts_direct_healing(asst::battle::SubRole::Warrior_Reaper));
    assert(!accepts_direct_healing(asst::battle::SubRole::Warrior_Musha));
    assert(!accepts_direct_healing(asst::battle::SubRole::Tank_Unyield));
    assert(accepts_direct_healing(asst::battle::SubRole::Warrior_Lord));
    assert(!choose_replacement("萨尔贡", two_blockers, { { "射手", PlacementRole::Ranged, {}, 2000 } }).found);
    auto harmony_board = two_blockers;
    harmony_board.push_back({ "魔王", PlacementRole::Ranged, { "调和" }, 1000, 640, 300, true });
    assert(!choose_replacement("萨尔贡", harmony_board, { { "其他射手", PlacementRole::Ranged, {}, 9000 } }).found);
    std::vector<ReplacementMember> six_ursus { { "烈夏", PlacementRole::Frontline, { "乌萨斯" }, 100, 800, 235, false },
                                               { "折桠", PlacementRole::Frontline, { "乌萨斯" }, 100, 800, 437, false },
                                               { "乌啾", PlacementRole::Ranged, { "乌萨斯" }, 100, 640, 235, true },
                                               { "真理", PlacementRole::Ranged, { "乌萨斯" }, 100, 640, 437, false },
                                               { "维伊", PlacementRole::Ranged, { "乌萨斯" }, 100, 560, 235, false },
                                               { "冻河", PlacementRole::Ranged, { "乌萨斯" }, 100, 560, 437, false } };
    // A modest off-faction upgrade must not silently drop the distinct-six
    // damage/mitigation/temporary-layer effect. A major role upgrade remains
    // a trade the planner can take, rather than a hard six-member gate.
    assert(!choose_replacement("乌萨斯", six_ursus, { { "小幅升级", PlacementRole::Ranged, {}, 1000 } }).found);
    assert(choose_replacement("乌萨斯", six_ursus, { { "强力终端", PlacementRole::Ranged, {}, 9000 } }).found);
    auto five_ursus_with_duplicate = six_ursus;
    five_ursus_with_duplicate.back().name = "维伊";
    assert(replacement_primary_members(five_ursus_with_duplicate, "乌萨斯") == 5);
    std::vector<ReplacementMember> duplicate_egir_board {
        { "幽灵鲨", PlacementRole::Frontline, { "阿戈尔" }, 3000, 467, 438, false },
        { "归溟幽灵鲨", PlacementRole::Frontline, { "阿戈尔" }, 1600, 552, 438, false },
        { "斯卡蒂", PlacementRole::Frontline, { "阿戈尔" }, 2000, 637, 438, false },
        { "野鬃", PlacementRole::Frontline, { "卡西米尔" }, 120, 807, 438, true }
    };
    auto spare_shadow = duplicate_egir_board[1];
    spare_shadow.egir_material_tier = 4;
    const auto duplicate_egir_choice = choose_replacement("阿戈尔", duplicate_egir_board, { spare_shadow });
    assert(
        duplicate_egir_choice.found && duplicate_egir_choice.incoming == "归溟幽灵鲨" &&
        duplicate_egir_choice.outgoing == "野鬃");
    // Neither an unreviewed tier nor a second printed pact grants the same
    // physical-Egir growth preference to another faction.
    spare_shadow.egir_material_tier = 0;
    assert(!choose_replacement("阿戈尔", duplicate_egir_board, { spare_shadow }).found);
    duplicate_egir_board.back() = spare_shadow;
    assert(replacement_primary_members(duplicate_egir_board, "阿戈尔") == 3);
    assert(choose_replacement("乌萨斯", five_ursus_with_duplicate, { { "小幅升级", PlacementRole::Ranged, {}, 1000 } })
               .found);
    auto five_ursus_with_harmony = six_ursus;
    five_ursus_with_harmony.back() = { "魔王", PlacementRole::Ranged, { "调和" }, 100, 560, 437, false };
    assert(replacement_primary_members(five_ursus_with_harmony, "乌萨斯") == 5);
    assert(choose_replacement("乌萨斯", five_ursus_with_harmony, { { "小幅升级", PlacementRole::Ranged, {}, 1000 } })
               .found);
    assert(!choose_replacement("萨尔贡", transition_board, { { "低价值", PlacementRole::Unknown, {}, 10 } }).found);
    assert(lifecycle_next({ "preparation" }) == "MiniGame@SPA@LifecycleExecute");
    assert(preparation_countdown({ { "54", .999, 1144, 37 }, { "24", .999, 773, 18 } }) == 54);
    assert(preparation_countdown({ { "0", .999, 1144, 37 } }) == 0);
    PreparationDeadline preparation_deadline;
    const auto countdown_start = PreparationDeadline::Clock::time_point {};
    assert(!preparation_deadline.observe(1, std::nullopt, countdown_start));
    assert(preparation_deadline.observe(1, 54, countdown_start) == 54);
    assert(preparation_deadline.observe(1, std::nullopt, countdown_start + std::chrono::seconds(5)) == 49);
    assert(preparation_deadline.observe(1, 54, countdown_start + std::chrono::seconds(6)) == 48);
    assert(preparation_deadline.observe(1, std::nullopt, countdown_start + std::chrono::seconds(60)) == 0);
    assert(!preparation_deadline.observe(2, std::nullopt, countdown_start + std::chrono::seconds(120)));
    PreparationDeadline dropped_digit_deadline;
    assert(dropped_digit_deadline.observe(11, 92, countdown_start) == 92);
    assert(dropped_digit_deadline.observe(11, 6, countdown_start + std::chrono::seconds(6)) == 6);
    assert(dropped_digit_deadline.observe(11, 60, countdown_start + std::chrono::seconds(32)) == 60);
    // A genuinely shortened server deadline is accepted after two consistent
    // reads, and a later stale larger number still cannot extend it.
    assert(dropped_digit_deadline.observe(11, 7, countdown_start + std::chrono::seconds(33)) == 7);
    assert(dropped_digit_deadline.observe(11, 5, countdown_start + std::chrono::seconds(35)) == 5);
    assert(dropped_digit_deadline.observe(11, 54, countdown_start + std::chrono::seconds(36)) == 4);
    assert(!dropped_digit_deadline.observe(12, std::nullopt, countdown_start + std::chrono::seconds(60)));
    assert(!preparation_countdown({ { "54", .89, 1144, 37 }, { "24", .999, 773, 18 } }));
    const std::vector<TextObservation> collapsed_shop { { "准备就绪", .999, 1164, 103 },
                                                        { "8", .999, 489, 23 },
                                                        { "展开", .999, 1152, 652 } };
    assert(shop_expansion_control(collapsed_shop, 8));
    assert(!shop_expansion_control(collapsed_shop, 9));
    auto shop_in_battle = collapsed_shop;
    shop_in_battle.erase(shop_in_battle.begin());
    assert(!shop_expansion_control(shop_in_battle, 8));
    auto already_expanded_shop = collapsed_shop;
    already_expanded_shop.back().text = "收起";
    assert(!shop_expansion_control(already_expanded_shop, 8));
    auto ambiguous_shop_control = collapsed_shop;
    ambiguous_shop_control.push_back({ "收起", .999, 1152, 652 });
    assert(!shop_expansion_control(ambiguous_shop_control, 8));
    // Trial 6's unidentified bench drag started with 12 seconds and crossed
    // into battle; its named deployment with 22 seconds completed normally.
    assert(!deployment_fits_preparation(12, true));
    assert(deployment_fits_preparation(22, false));
    assert(!deployment_fits_preparation(22, true));
    assert(deployment_fits_preparation(30, true));
    assert(deployment_fits_preparation(std::nullopt, true));
    assert(verified_loading_page(
        { { "极速模拟", .999966, 145, 21 },
          { "全局作战目标", .999949, 353, 309 },
          { "消灭敌方领袖！", .966621, 350, 345 } }));
    assert(!verified_loading_page({ { "全局作战目标", .999949, 353, 309 } }));
    assert(!verified_loading_page({ { "全局作战目标", .999949, 353, 309 }, { "消灭敌方领袖！", .4, 350, 345 } }));
    assert(!verified_loading_page({ { "全局作战目标", .999949, 353, 609 }, { "消灭敌方领袖！", .966621, 350, 645 } }));
    assert(verified_loading_page(
        { { "OPERATION", .99983, 550, 281 }, { "AC-MP-2", .999562, 394, 329 }, { "险境模拟", .998439, 512, 443 } }));
    assert(verified_loading_page({ { "OPERATION", .999, 550, 281 }, { "Loading.", .999, 1100, 650 } }));
    assert(strategy_confirmation_enabled(cv::imread("spa-strategy-confirm-enabled.png")));
    assert(strategy_unavailable_label("已被选择"));
    assert(strategy_unavailable_label("该策略已被选择"));
    assert(strategy_unavailable_label("未解锁"));
    assert(strategy_unavailable_label("当前不可用"));
    assert(strategy_unavailable_label("已禁用"));
    assert(!strategy_unavailable_label("已选定策略"));
    assert(!strategy_unavailable_label("确认选择"));
    assert(!strategy_unavailable_label(""));
    assert(!strategy_confirmation_enabled(cv::imread("spa-strategy-confirm-taken.png")));
    assert(!strategy_confirmation_enabled(cv::Mat()));
    assert(!strategy_confirmation_enabled_on_frame(cv::Mat()));
    assert(!strategy_confirmation_enabled_on_frame(cv::Mat(720, 1280, CV_8UC1)));
    assert(verified_loading_page({ { "搜寻成功!", .969, 977, 606 }, { "正在连接中…", .997, 1006, 656 } }));
    assert(!verified_loading_page({ { "搜寻成功!", .969, 977, 606 } }));
    assert(!verified_loading_page({ { "搜寻成功!", .969, 977, 606 }, { "正在连接中…", .4, 1006, 656 } }));
    const std::vector<TextObservation> matching_ready { { "准备就绪！", .930, 500, 569 },
                                                        { "STRONGHOLD PROTOCOL", .988, 521, 643 },
                                                        { "PlayerA#1234", .99, 160, 485 },
                                                        { "PlayerB#2345", .99, 444, 480 },
                                                        { "PlayerC#3456", .99, 730, 483 } };
    assert(verified_loading_page(matching_ready));
    assert(!verified_loading_page({ { "准备就绪！", .99, 1120, 103 }, { "STRONGHOLD PROTOCOL", .99, 521, 643 } }));
    assert(verified_loading_page(
        { { "OPERATION", .999842, 549, 280 }, { "AC-SP-1", .999613, 418, 329 }, { "规程等级1", .999545, 497, 444 } }));
    assert(!verified_loading_page(
        { { "OPERATION", .999842, 549, 280 }, { "AC-SP-1", .999613, 418, 329 }, { "险境模拟", .999545, 497, 444 } }));
    // Actual quick4 loading observations, previously routed to FindLobby.
    const std::vector<TextObservation> quick_loading { { "OPERATION", .999821, 550, 281 },
                                                       { "AC-TB-1", .998997, 417, 333 },
                                                       { "极速模拟", .999795, 513, 443 } };
    assert(verified_loading_page(quick_loading));
    assert(!verified_loading_page(
        { { "OPERATION", .999821, 550, 281 }, { "AC-TB-1", .998997, 417, 333 }, { "险境模拟", .999795, 513, 443 } }));
    assert(!verified_loading_page(
        { { "OPERATION", .999821, 550, 281 }, { "AC-TB-1", .4, 417, 333 }, { "极速模拟", .999795, 513, 443 } }));
    assert(!verified_loading_page(
        { { "OPERATION", .999821, 550, 281 }, { "AC-TB-1", .998997, 417, 333 }, { "极速模拟", .999795, 145, 21 } }));
    assert(verified_loading_page({ { "协议启动！", .9115, 514, 480 }, { "STRONGHOLD PROTOCOL", .976814, 552, 554 } }));
    assert(!verified_loading_page({ { "协议启动！", .9115, 514, 480 } }));
    assert(!verified_loading_page({ { "协议启动！", .9115, 514, 480 }, { "STRONGHOLD PROTOCOL", .976814, 552, 154 } }));
    assert(!verified_loading_page({ { "OPERATION", .99983, 550, 281 }, { "险境模拟", .998439, 512, 443 } }));
    assert(!verified_loading_page(
        { { "OPERATION", .99983, 550, 281 }, { "AC-MP-2", .999562, 394, 329 }, { "险境模拟", .998439, 144, 21 } }));
    assert(!preparation_countdown({ { "54", .999, 1144, 37 }, { "7", .999, 1180, 40 } }));
    assert(!preparation_countdown({ { "准备就绪", .999, 1166, 103 }, { "999", .999, 1144, 37 } }));
    assert(lifecycle_next({ "tactical_decision" }) == "MiniGame@SPA@LifecycleExecute");
    assert(lifecycle_next({ "data_updated" }) == "MiniGame@SPA@LifecycleAckSync");
    assert(lifecycle_next({ "ready_confirmation" }) == "MiniGame@SPA@LifecycleExecute");
    assert(lifecycle_next({ "battle" }) == "MiniGame@SPA@LifecycleExecute");
    assert(lifecycle_next({ "authentication_expired" }) == "MiniGame@SPA@LifecycleLogin");
    assert(lifecycle_next({ "result", {}, false }) == "MiniGame@SPA@LifecycleWait");
    assert(lifecycle_next({ "result", {}, true }) == "MiniGame@SPA@ReturnResultHome");
    assert(lifecycle_next({ "lobby", {}, false, true }) == "MiniGame@SPA@LifecycleComplete");
    assert(lifecycle_next({ "lobby", {}, false, false, true, true }) == "MiniGame@SPA@ReconnectSolo");
    assert(lifecycle_next({ "lobby", {}, false, false, true, false }) == "MiniGame@SPA@LifecycleSelectPersonal");
    assert(lifecycle_next({ "lobby", {}, false, false, false, true }) == "MiniGame@SPA@OpenIndependentStart");
    assert(lifecycle_next({ "unknown", "2/3" }) == "MiniGame@SPA@SelectStrategy");
    assert(lifecycle_next({ "run_setup", "3/3" }) == "MiniGame@SPA@SelectFormation");
    assert(lifecycle_next({ "unknown" }) == "MiniGame@SPA@LifecycleFindLobby");
    LifecyclePage loading_page { "unknown" };
    loading_page.loading_verified = true;
    assert(lifecycle_next(loading_page) == "MiniGame@SPA@LifecycleWait");
    LifecycleLoadingGrace loading_grace;
    const auto loading_time = LifecycleLoadingGrace::Clock::now();
    assert(!loading_grace.should_wait({ "unknown" }, loading_time));
    assert(loading_grace.should_wait(loading_page, loading_time));
    assert(loading_grace.should_wait({ "unknown" }, loading_time + std::chrono::seconds(14)));
    assert(!loading_grace.should_wait({ "unknown" }, loading_time + std::chrono::seconds(15)));
    assert(!loading_grace.should_wait({ "unknown" }, loading_time - std::chrono::seconds(1)));
    assert(loading_grace.should_wait(loading_page, loading_time));
    assert(!loading_grace.should_wait({ "preparation" }, loading_time + std::chrono::seconds(1)));
    assert(!loading_grace.should_wait({ "unknown" }, loading_time + std::chrono::seconds(2)));
    LifecycleNavigationGuard navigation;
    for (int loading_frame = 0; loading_frame < 10; ++loading_frame) {
        assert(navigation.allow(loading_page, lifecycle_next(loading_page)));
    }
    LifecyclePage stalled_lobby { "lobby" };
    assert(navigation.allow(stalled_lobby, lifecycle_next(stalled_lobby)));
    assert(navigation.allow(stalled_lobby, lifecycle_next(stalled_lobby)));
    assert(!navigation.allow(stalled_lobby, lifecycle_next(stalled_lobby)));
    stalled_lobby.personal_selected = true;
    assert(navigation.allow(stalled_lobby, lifecycle_next(stalled_lobby)));
    for (int observation = 0; observation < 5; ++observation) {
        assert(navigation.allow({ "preparation" }, "MiniGame@SPA@LifecycleExecute"));
        assert(navigation.allow({ "unknown" }, "MiniGame@SPA@LifecycleWait"));
    }
    LifecyclePage quick { "lobby", {}, false, false, false, true };
    quick.mode = "quick";
    assert(lifecycle_next(quick) == "MiniGame@SPA@LifecycleQuickStart");
    quick.page = "difficulty";
    assert(lifecycle_next(quick) == "MiniGame@SPA@LifecycleDifficultyBack");
    quick.page = "unknown";
    quick.quick_start_available = true;
    assert(lifecycle_next(quick) == "MiniGame@SPA@LifecycleQuickConfirm");
    quick.page = "difficulty";
    assert(lifecycle_next(quick) == "MiniGame@SPA@LifecycleQuickConfirm");
    quick.page = "battle";
    assert(lifecycle_next(quick) == "MiniGame@SPA@LifecycleExecute");
    LifecyclePage solo_committed { "unknown", "3/3" };
    solo_committed.mode = "quick";
    solo_committed.joint_setup_committed = true;
    assert(lifecycle_next(solo_committed) == "MiniGame@SPA@LifecycleWait");
    solo_committed.joint_setup_committed = false;
    assert(lifecycle_next(solo_committed) == "MiniGame@SPA@SelectFormation");
    LifecyclePage joint { "lobby" };
    joint.mode = "joint";
    assert(lifecycle_next(joint) == "MiniGame@SPA@LifecycleSelectJoint");
    joint.joint_selected = true;
    assert(lifecycle_next(joint) == "MiniGame@SPA@LifecycleOpenJoint");
    joint.reconnect_available = true;
    assert(lifecycle_next(joint) == "MiniGame@SPA@ReconnectCurrent");
    joint.joint_selected = false;
    assert(lifecycle_next(joint) == "MiniGame@SPA@ReconnectCurrent");
    joint.page = "unknown";
    joint.setup_stage = "2/3";
    joint.joint_setup_committed = true;
    assert(lifecycle_next(joint) == "MiniGame@SPA@LifecycleWait");
    joint.setup_stage = "3/3";
    assert(lifecycle_next(joint) == "MiniGame@SPA@LifecycleWait");
    joint.joint_setup_committed = false;
    assert(lifecycle_next(joint) == "MiniGame@SPA@SelectFormation");
    const auto& routes = route_profiles();
    assert(routes.at("阿戈尔").minimum_shop_level == 3);
    assert(
        std::find(
            routes.at("阿戈尔").output_candidates.begin(),
            routes.at("阿戈尔").output_candidates.end(),
            "乌尔比安") != routes.at("阿戈尔").output_candidates.end());
    assert(routes.at("谢拉格").minimum_shop_level == 3);
    assert(
        std::find(
            routes.at("卡西米尔").battle_redeploy_units.begin(),
            routes.at("卡西米尔").battle_redeploy_units.end(),
            "远牙") != routes.at("卡西米尔").battle_redeploy_units.end());
    // Bought units occupy the bench; shop cards must never be deployment sources.
    assert(deployment_source_is_bench(920, 460));
    assert(deployment_source_is_bench(150, 460));
    assert(!deployment_source_is_bench(970, 665));
    assert(!deployment_source_is_bench(700, 350));
    assert(!deployment_source_is_bench(1100, 460));
    PreparationPolicy deployment_policy;
    deployment_policy.carries = { "野鬃" };
    deployment_policy.bench_occupied = 2;
    deployment_policy.owned = { { "野鬃", { 1, false, false } }, { "地灵", { 1, false, false } } };
    assert(choose_pending_deployment(deployment_policy, 8, { "地灵", "野鬃" }) == "野鬃");
    assert(!choose_pending_deployment(deployment_policy, 0, { "野鬃" }));
    assert(!choose_pending_deployment(deployment_policy, 8, {}));
    deployment_policy.owned["野鬃"].deployed = true;
    assert(choose_pending_deployment(deployment_policy, 7, { "地灵", "野鬃" }) == "地灵");
    deployment_policy.owned["地灵"].copies = 2;
    assert(choose_pending_deployment(deployment_policy, 7, { "地灵", "野鬃" }) == "地灵");
    deployment_policy.owned["地灵"].copies = 3;
    assert(!choose_pending_deployment(deployment_policy, 7, { "地灵" }));
    assert(!choose_pending_deployment(deployment_policy, 7, { "unknown" }));
    deployment_policy.owned["星极"] = { 1, true, false };
    assert(choose_pending_deployment(deployment_policy, 7, { "星极" }) == "星极");
    // A carry that is awaiting synthesis must not mask another real bench unit
    // at the earlier automatic target-selection stage.
    deployment_policy.owned["星极"] = { 3, false, false };
    assert(!bench_operator_can_deploy(deployment_policy, "星极"));
    deployment_policy.owned["野鬃"] = { 1, false, false };
    assert(bench_operator_can_deploy(deployment_policy, "野鬃"));
    assert(choose_pending_deployment(deployment_policy, 3, { "星极", "野鬃" }) == "野鬃");
    PreparationPolicy undiscovered_gift;
    undiscovered_gift.selected_strategy_id = "band_mech";
    undiscovered_gift.round = 8;
    assert(choose_pending_deployment(undiscovered_gift, 3, { "“暴躁铁皮”" }) == "“暴躁铁皮”");
    assert(undiscovered_gift.owned.empty() && undiscovered_gift.bench_occupied == 0);
    assert(!choose_pending_deployment(undiscovered_gift, 0, { "“暴躁铁皮”" }));
    assert(!choose_pending_deployment(undiscovered_gift, 3, {}));
    undiscovered_gift.selected_strategy_id = "band_takecare";
    assert(!choose_pending_deployment(undiscovered_gift, 3, { "“暴躁铁皮”" }));
    undiscovered_gift.selected_strategy_id = "band_mech";
    undiscovered_gift.owned["“暴躁铁皮”"] = { 1, false, true };
    assert(!choose_pending_deployment(undiscovered_gift, 3, { "“暴躁铁皮”" }));
    const std::vector<TextObservation> reconnect_lobby { { "个人模拟", 0.99, 950, 555 },
                                                         { "断线重连", 0.99, 1120, 655 },
                                                         { "放弃模拟", 0.99, 820, 645 },
                                                         { "功勋兑换处", 0.99, 90, 630 } };
    assert(observe_page(reconnect_lobby, {}).page == Page::Lobby);
    const std::vector<TextObservation> joint_reconnect_lobby { { "联合模拟", .999941, 1122, 550 },
                                                               { "已终止", .99944, 1157, 616 },
                                                               { "断线重连", .999855, 1125, 652 },
                                                               { "协议数据库", .999937, 116, 198 },
                                                               { "功勋兑换处", .999822, 96, 626 } };
    assert(observe_page(joint_reconnect_lobby, {}).page == Page::Lobby);
    const std::vector<TextObservation> ended_reconnect_lobby {
        { "个人模拟", 0.99, 950, 555 },  { "断线重连", 0.99, 1120, 655 },  { "已终止", 0.99, 1157, 616 },
        { "功勋兑换处", 0.99, 90, 630 }, { "协议数据库", 0.99, 116, 198 },
    };
    assert(observe_page(ended_reconnect_lobby, {}).page == Page::Lobby);
    assert(observe_page({ { "断线重连", 0.99, 100, 100 } }, {}).page == Page::Unknown);
    assert(observe_page({ { "登录认证已失效，请重新登录", 0.99, 470, 315 } }, {}).page == Page::AuthenticationExpired);
    assert(parse_shop_level("I") == 1);
    assert(parse_shop_level("Ⅱ") == 2);
    assert(parse_shop_level("III") == 3);
    assert(parse_shop_level(" IV ") == 4);
    assert(parse_shop_level("Ⅴ") == 5);
    assert(parse_shop_level("6") == 6);
    assert(!parse_shop_level("0") && !parse_shop_level("7") && !parse_shop_level("LEVEL") && !parse_shop_level("I川"));
    assert(can_queue_temporary_elite(1, true, true, false, true, false));
    assert(can_queue_temporary_elite(1, true, true, false, false, true));
    assert(!can_queue_temporary_elite(0, true, true, false, true, false));
    assert(!can_queue_temporary_elite(1, false, true, false, true, false));
    assert(!can_queue_temporary_elite(1, true, false, false, true, false));
    assert(!can_queue_temporary_elite(1, true, true, true, true, false));
    assert(!can_queue_temporary_elite(1, true, true, false, false, false));
    PreparationPolicy support_policy;
    support_policy.primary_pact = "莱塔尼亚";
    support_policy.support_pacts = { "奇迹" };
    support_policy.allow_refresh = false;
    support_policy.round = 4;
    support_policy.bench_occupied = 1;
    support_policy.owned = { { "星极", { 1, false, true } } };
    auto support_buy = choose_preparation(support_policy, 5, { { "云迹", 3, 0 } }, operator_pacts());
    auto full_support_policy = support_policy;
    full_support_policy.dynamic_decision_mode = true;
    full_support_policy.deployed_population = 8;
    full_support_policy.full_field_offer_improvements["云迹"] = 0;
    assert(
        choose_preparation(full_support_policy, 5, { { "云迹", 3, 0 } }, operator_pacts()).action !=
        PreparationAction::Buy);
    full_support_policy.full_field_offer_improvements["云迹"] = 250;
    assert(
        choose_preparation(full_support_policy, 5, { { "云迹", 3, 0 } }, operator_pacts()).action ==
        PreparationAction::Buy);
    full_support_policy.full_field_offer_improvements.clear();
    assert(
        choose_preparation(full_support_policy, 5, { { "云迹", 3, 0 } }, operator_pacts()).action ==
        PreparationAction::Buy);
    // Finishing an existing pair remains an independent acquisition route.
    full_support_policy.full_field_offer_improvements["云迹"] = 0;
    full_support_policy.early_board_scores["云迹"] = 100;
    full_support_policy.owned["云迹"] = { 2, false, false };
    assert(
        choose_preparation(full_support_policy, 5, { { "云迹", 3, 0 } }, operator_pacts()).action ==
        PreparationAction::Buy);
    assert(support_buy.action == PreparationAction::Buy && support_buy.name == "云迹");
    support_policy.owned["星极"].deployed = false;
    assert(
        choose_preparation(support_policy, 5, { { "云迹", 3, 0 } }, operator_pacts()).action == PreparationAction::Buy);
    support_policy.support_pacts.clear();
    assert(
        choose_preparation(support_policy, 5, { { "云迹", 3, 0 } }, operator_pacts()).action ==
        PreparationAction::Review);
    support_policy.support_pacts = { "奇迹" };
    support_policy.owned.clear();
    assert(
        choose_preparation(support_policy, 5, { { "云迹", 3, 0 } }, operator_pacts()).action ==
        PreparationAction::Review);
    support_policy.owned = { { "星极", { 1, false, true } }, { "云迹", { 1, false, true } } };
    assert(
        choose_preparation(support_policy, 5, { { "云迹", 3, 0 } }, operator_pacts()).action ==
        PreparationAction::Review);
    support_policy.owned = { { "野鬃", { 2, false, false } } };
    support_policy.support_pacts = { "助力" };
    assert(
        choose_preparation(support_policy, 5, { { "夏栎", 2, 0 } }, operator_pacts()).action ==
        PreparationAction::Review);
    support_policy.owned["野鬃"].deployed = true;
    assert(
        choose_preparation(support_policy, 5, { { "夏栎", 2, 0 } }, operator_pacts()).action == PreparationAction::Buy);
    support_policy.round = 7;
    assert(
        choose_preparation(support_policy, 5, { { "夏栎", 2, 0 } }, operator_pacts()).action == PreparationAction::Buy);
    RoundState state;
    assert(battle_entry_allowed(false, true, 0, false, ""));
    assert(battle_entry_allowed(false, true, 2, true, "special equipment"));
    assert(!battle_entry_allowed(false, true, 2, true, ""));
    assert(!battle_entry_allowed(false, true, 2, false, "ordinary"));
    assert(battle_entry_allowed(true, false, 2, false, ""));
    state.run_id = "solo-1";
    state.round = 1;
    state.funds = 3;
    state.remaining_rounds = 4;
    state.revision = 1;
    state.observation_complete = true;
    state.map_verified = true;
    state.lineup_verified = true;
    state.preparation = true;
    state.ready_verified = true;
    state.remaining_action_budget = 10;
    Candidate income { ActionKind::BuyEquipment, "next-round-income", 3, 0, 0, 2, true, true, true };
    auto decision = Planner::choose(state, { income });
    assert(decision.kind == ActionKind::BuyEquipment);
    assert(Planner::can_execute(state, decision));
    Candidate faction_core { ActionKind::BuyOperator, "Shirayuki-core", 2, 0, 20, 0, true, true, true };
    faction_core.requires_active_pacts = true;
    faction_core.minimum_shop_level = 4;
    state.shop_level = 1;
    assert(Planner::choose(state, { faction_core }).kind == ActionKind::Ready);
    faction_core.active_pacts_verified = true;
    state.shop_level = 4;
    assert(Planner::choose(state, { faction_core }).target == "Shirayuki-core");
    state.can_carry_funds = true;
    // A requested but unverified carry rule must not change ordinary cash handling.
    assert(Planner::choose(state, { income }).kind == ActionKind::BuyEquipment);
    state.carry_rule_verified = true;
    assert(Planner::choose(state, { income }).kind == ActionKind::Ready);
    state.can_carry_funds = false;
    state.remaining_rounds = 0;
    assert(Planner::choose(state, { income }).kind == ActionKind::Ready);
    state.remaining_rounds = 4;
    state.run_id = "solo-2";
    assert(!Planner::can_execute(state, decision));
    state.run_id = "solo-1";
    ++state.revision;
    assert(!Planner::can_execute(state, decision));
    state.survival_at_risk = true;
    Candidate layers { ActionKind::BuyOperator, "stack-engine", 2, 0, 100, 0, true, true, true };
    Candidate tank { ActionKind::BuyOperator, "frontline", 3, 1, 0, 0, true, true, true };
    assert(Planner::choose(state, { layers, income, tank }).target == "frontline");
    assert(Planner::choose(state, { layers, income }).kind == ActionKind::Observe);
    tank.cost = 4;
    assert(Planner::choose(state, { tank }).kind == ActionKind::Observe);
    tank.cost = 3;
    tank.survival_gain = std::numeric_limits<double>::quiet_NaN();
    assert(Planner::choose(state, { tank }).kind == ActionKind::Observe);
    state.survival_at_risk = false;
    state.map_verified = false;
    assert(Planner::choose(state, { layers }).kind == ActionKind::Observe);
    state.map_verified = true;
    state.remaining_action_budget = 0;
    assert(Planner::choose(state, { layers }).kind == ActionKind::Observe);

    ProgressLedger ledger;
    StrategyProgress progress { "band-1", 2, true, false, true };
    Settlement settlement { "run-1", "band-1", true, true, true };
    assert(ledger.apply(settlement, progress));
    assert(*progress.wins == 3);
    assert(!ledger.apply(settlement, progress));
    assert(!ProgressLedger::complete(progress));
    progress.badge_verified = true;
    assert(ProgressLedger::complete(progress));
    StrategyProgress unknown { "band-2", std::nullopt, true, false, true };
    settlement = { "run-2", "band-2", true, true, true };
    assert(!ledger.apply(settlement, unknown));
    StrategyProgress joint_only { "band-3", 2, true, false, false };
    StrategyProgress partial { "band-4", 1, true, false, true };
    assert(ProgressLedger::next_solo({ unknown, joint_only, progress, partial }) == "band-4");
    StrategyProgress claimed_partial { "band-claimed", 2, true, true, true };
    StrategyProgress unclaimed_new { "band-unclaimed", 0, true, false, true };
    assert(ProgressLedger::next_solo({ claimed_partial, unclaimed_new }) == "band-unclaimed");
    const StrategyProgress before_report { "band_amiya", 2, true, false, true };
    auto after_report = before_report;
    after_report.wins = 3;
    assert(ProgressLedger::next_report_confirms_win(before_report, after_report, "band_amiya", true));
    assert(!ProgressLedger::complete(after_report));
    assert(!ProgressLedger::next_report_confirms_win(before_report, after_report, "band_mech", true));
    assert(!ProgressLedger::next_report_confirms_win(before_report, after_report, "band_amiya", false));
    after_report.wins = 4;
    assert(!ProgressLedger::next_report_confirms_win(before_report, after_report, "band_amiya", true));
    after_report.wins = 2;
    assert(!ProgressLedger::next_report_confirms_win(before_report, after_report, "band_amiya", true));
    after_report.wins = 3;
    after_report.badge_verified = true;
    assert(ProgressLedger::complete(after_report));
    settlement = { "run-3", "band-4", true, false, true };
    assert(!ledger.apply(settlement, partial));
    settlement.victory = true;
    settlement.strategy_credit_verified = false;
    assert(!ledger.apply(settlement, partial));

    ProgressStore store("bilibili-account-A", "act3autochess");
    assert(store.reconcile({ "band-4", 1, true, false, true }));
    settlement = { "persistent-run", "band-4", true, true, true };
    assert(store.credit(settlement));
    auto checkpoint = store.snapshot();
    ProgressStore resumed("bilibili-account-A", "act3autochess");
    assert(resumed.restore(checkpoint));
    assert(!resumed.credit(settlement));
    assert(resumed.strategies().front().wins == 2);
    ProgressStore other_account("bilibili-account-B", "act3autochess");
    assert(!other_account.restore(checkpoint));
    ProgressStore other_season("bilibili-account-A", "other-season");
    assert(!other_season.restore(checkpoint));
    auto malformed = checkpoint;
    malformed["schema_version"] = 2;
    assert(!resumed.restore(malformed));
    assert(resumed.strategies().front().wins == 2);
    assert(!store.reconcile({ "invalid-badge", 2, true, true, true }));
    ProgressStore silver_store("bilibili-account-A", "act3autochess");
    assert(silver_store.reconcile({ "silver-band", 6, true, true, true }));
    ProgressStore silver_resumed("bilibili-account-A", "act3autochess");
    assert(silver_resumed.restore(silver_store.snapshot()));
    assert(silver_resumed.strategies().front().wins == 6);
    assert(ProgressLedger::complete(silver_resumed.strategies().front()));
    assert(!silver_resumed.reconcile({ "invalid-count", 7, true, false, true }));
    StrategyProgress silver_progress { "silver-band", 5, true, false, true };
    assert(ledger.apply({ "silver-win", "silver-band", true, true, true }, silver_progress));
    assert(silver_progress.wins == 6 && !ProgressLedger::complete(silver_progress));
    assert(store.reconcile({ "unknown-wins", std::nullopt, true, false, true }));
    settlement.band_id = "unknown-wins";
    settlement.run_id = "unknown-run";
    assert(!store.credit(settlement));

    auto file = std::filesystem::current_path();
    file /= "MAA-SPAProgress-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto temporary = file;
    temporary += ".tmp";
    auto backup = file;
    backup += ".bak";
    const bool saved = store.save(file);
    if (!saved) {
        std::cerr << "Failed to save checkpoint: " << file << " errno=" << errno << '\n';
    }
    assert(saved);
    ProgressStore disk("bilibili-account-A", "act3autochess");
    assert(disk.load(file));
    settlement = { "persistent-run", "band-4", true, true, true };
    assert(!disk.credit(settlement));
    settlement.run_id = "persistent-run-2";
    assert(disk.credit(settlement));
    assert(disk.save(file));
    assert(disk.load(file));
    assert(!disk.credit(settlement));
    assert(!other_account.load(file));
    assert(!other_account.save(file));
    // Simulate interruption after a complete new checkpoint is written but before promotion.
    std::filesystem::rename(file, temporary);
    assert(disk.load(file));
    assert(!disk.credit(settlement));
    std::filesystem::remove(temporary);
    assert(disk.load(file));
    // The remaining backup is the prior checkpoint, not a fabricated empty ledger.
    assert(disk.strategies().front().wins == 2);
    std::filesystem::remove(backup);
    assert(!disk.load(file));

    const auto identities = strategy_identities();
    assert(identities.size() == 61);
    std::vector<TextObservation> report { { "策略报告", 0.95, 700, 45 },
                                          { "初始生命值25", 0.95, 40, 440 },
                                          { "阿米娅", 0.95, 240, 390 },
                                          { "【众志合一】", 0.95, 40, 480 },
                                          { "已通过模拟2次", 0.95, 200, 160 } };
    assert(
        observe_page(
            { { "确认本局信息", 0.99, 450, 25 }, { "核心盟约", 0.99, 70, 140 }, { "准备就绪", 0.99, 1050, 650 } },
            identities)
            .page == Page::RunSetup);
    assert(
        observe_page({ { "请选择策略", 0.99, 940, 25 }, { "泛用型策略", 0.99, 720, 140 } }, identities).page ==
        Page::RunSetup);
    assert(
        observe_page(
            { { "悬赏决策", 0.99, 180, 30 },
              { "选定后将无法更改", 0.99, 370, 65 },
              { "为自身接下来两场作战添加1只敌人", 0.99, 180, 360 } },
            identities)
            .page == Page::BountySelection);
    assert(observe_page({ { "请选择奖励", 0.99, 1030, 105 } }, identities).page == Page::BountySelection);
    assert(acquired_operator_names().size() == 25);
    for (const auto& name : acquired_operator_names()) {
        assert(acquired_effects(name, false, 1) && acquired_effects(name, true, 6));
    }
    assert(!acquired_effects("角峰", false, 2));
    assert(!acquired_effects("矩", false));
    assert(acquired_effects("矩", true, 6)->at(0).amount == 12);
    const auto logos_acquired = acquired_effects("逻各斯", true);
    assert(logos_acquired->at(0).target == "奥术" && logos_acquired->at(0).amount == 20);
    assert(logos_acquired->at(1).target == "远见" && logos_acquired->at(1).amount == 10);
    assert(acquired_effects("极光", false)->at(0).requires_observed_amount);
    assert(acquired_effects("凯瑟琳", true)->at(0).randomized_quality);
    assert(acquired_effects("乌有", false)->at(0).kind == AcquiredEffectKind::Equipment);
    assert(acquired_effects("摆渡人", false)->size() == 2);
    for (const auto& [name, gift] : std::vector<std::pair<std::string, std::string>> { { "承曦格雷伊", "精准狙击镜" },
                                                                                       { "车尔尼", "莱塔尼亚小鼓" },
                                                                                       { "卡涅利安", "迅捷作战粮" },
                                                                                       { "耶拉", "谢拉格不融冰" },
                                                                                       { "乌有", "盟约之币" },
                                                                                       { "奥尔佳", "乌萨斯呼吸器" } }) {
        assert(acquired_equipment_hints(name, false) == std::vector<std::string> { gift });
        // Two elite gifts still produce one discovery key, not two verified items.
        assert(acquired_equipment_hints(name, true) == std::vector<std::string> { gift });
    }
    for (const auto* name : { "角峰", "矩", "凯瑟琳", "缪尔赛思", "极光" }) {
        assert(acquired_equipment_hints(name, false).empty());
        assert(acquired_equipment_hints(name, true).empty());
    }
    const auto result_page = observe_page(
        { { "模拟完成", 0.99, 160, 200 },
          { "通过回合", 0.99, 50, 280 },
          { "返回主页", 0.99, 1080, 650 },
          { "规程等级3", 0.99, 210, 150 },
          { "14", 0.99, 40, 297 } },
        identities);
    assert(
        result_page.page == Page::Result && result_page.result_completed && !result_page.result_interrupted &&
        result_page.passed_rounds == 14 && result_page.result_difficulty == 3);
    const auto partial_result = observe_page(
        { { "模拟完成", 0.99, 160, 200 },
          { "通过回合", 0.99, 50, 280 },
          { "返回主页", 0.99, 1080, 650 },
          { "13", 0.99, 40, 310 } },
        identities);
    assert(partial_result.page == Page::Result && partial_result.passed_rounds == 13);
    assert(!partial_result.result_difficulty);
    std::vector<TextObservation> quick_result_texts { { "极速模拟", .999, 208, 153 },
                                                      { "模拟完成", .999, 160, 189 },
                                                      { "通过回合", .999, 49, 270 },
                                                      { "返回主页", .999, 1025, 640 },
                                                      { "9", .999, 65, 305 } };
    const auto quick_result = observe_page(quick_result_texts, identities);
    assert(quick_result.page == Page::Result && quick_result.result_completed && quick_result.passed_rounds == 9);
    assert(
        !quick_result.result_joint && !quick_result.result_difficulty &&
        quick_result.result_difficulty_name == "极速模拟");
    quick_result_texts[0].score = .89;
    assert(observe_page(quick_result_texts, identities).result_difficulty_name.empty());
    quick_result_texts[0].score = .999;
    quick_result_texts[0].x = 900;
    assert(observe_page(quick_result_texts, identities).result_difficulty_name.empty());
    quick_result_texts[0].x = 208;
    quick_result_texts.push_back({ "规程等级3", .999, 210, 150 });
    assert(observe_page(quick_result_texts, identities).contradictory);
    std::vector<TextObservation> joint_summary_texts {
        { "险境模拟", .999, 198, 150 },    { "模拟完成", .999, 158, 187 }, { "通过回合", .999, 48, 270 },
        { "14", .999, 40, 297 },           { "下一步", .999, 1040, 639 },  { "55", .999, 1166, 52 },
        { "Dr.Test#1234", .999, 184, 467 }
    };
    const auto joint_summary = observe_page(joint_summary_texts, identities);
    assert(
        joint_summary.page == Page::ResultSummary && !joint_summary.result_completed && !joint_summary.passed_rounds);
    assert(joint_summary.personal_passed_rounds == 14 && joint_summary.personal_result_completed);
    assert(joint_summary.personal_result_identity == "Dr.Test#1234");
    assert(joint_summary.personal_result_difficulty_name == "险境模拟");
    auto conflicting_summary = joint_summary_texts;
    conflicting_summary.push_back({ "13", .999, 42, 299 });
    assert(observe_page(conflicting_summary, identities).contradictory);
    assert(!observe_page(conflicting_summary, identities).personal_passed_rounds);
    assert(lifecycle_next({ "result_summary" }) == "MiniGame@SPA@ResultSummaryNext");
    joint_summary_texts[1].text = "模拟中断";
    assert(observe_page(joint_summary_texts, identities).page == Page::ResultSummary);
    joint_summary_texts[4].score = .3;
    assert(observe_page(joint_summary_texts, identities).page != Page::ResultSummary);
    std::vector<TextObservation> joint_result_texts {
        { "模拟完成", .999, 24, 659 },  { "同盟通过回合", .999, 246, 611 },
        { "返回主页", .999, 865, 656 }, { "继续搜寻", .999, 1110, 651 },
        { "险境模拟", .999, 52, 611 },  { "14", .999, 270, 643 },
        { "通过回合", .999, 434, 411 }, { "4", .999, 532, 411 }
    };
    const auto joint_result = observe_page(joint_result_texts, identities);
    assert(joint_result.page == Page::Result && joint_result.result_joint && joint_result.result_completed);
    assert(joint_result.passed_rounds == 14 && joint_result.result_difficulty_name == "险境模拟");
    assert(!joint_result.result_difficulty); // Never credit this as solo regulation 2 or 14.
    joint_result_texts.push_back({ "13", .999, 270, 643 });
    const auto contradictory_joint_result = observe_page(joint_result_texts, identities);
    assert(contradictory_joint_result.contradictory && !contradictory_joint_result.passed_rounds);
    assert(contradictory_joint_result.result_difficulty_name.empty());
    const auto interrupted_result = observe_page(
        { { "模拟中断", 0.99, 160, 200 },
          { "通过回合", 0.99, 50, 280 },
          { "返回主页", 0.99, 1080, 650 },
          { "规程等级14", 0.99, 210, 150 },
          { "4", 0.99, 40, 297 } },
        identities);
    assert(
        interrupted_result.page == Page::Result && !interrupted_result.result_completed &&
        interrupted_result.result_interrupted && interrupted_result.passed_rounds == 4 &&
        interrupted_result.result_difficulty == 14);
    auto observed = observe_page(report, identities);
    assert(observed.page == Page::StrategyReport);
    assert(observed.band_id == "band_amiya");
    assert(observed.wins == 2);
    report.pop_back();
    assert(!observe_page(report, identities).wins);
    report.push_back({ "已通过模拟3次", 0.4, 200, 160 });
    assert(!observe_page(report, identities).wins);
    report.push_back({ "已通过模拟2次", 0.95, 200, 160 });
    report.push_back({ "已通过模拟3次", 0.95, 200, 160 });
    observed = observe_page(report, identities);
    assert(observed.contradictory && !observed.band_id && !observed.wins);
    const std::vector<TextObservation> badge_capture { { "策略报告", .999943, 749, 33 },
                                                       { "阿米娅", .999810, 242, 374 },
                                                       { "初始生命值25", .996868, 58, 434 },
                                                       { "【众志合一】", .973313, 48, 471 },
                                                       { "已通过模拟3次", .999938, 237, 156 } };
    const StrategyIdentity amiya { "band_amiya", "众志合一", "阿米娅" };
    assert(strategy_badge_report_text_verified(badge_capture, amiya, 3));
    auto wrong_badge = badge_capture;
    wrong_badge.back().x = 585;
    assert(!strategy_badge_report_text_verified(wrong_badge, amiya, 3));
    wrong_badge = badge_capture;
    wrong_badge.push_back({ "已通过模拟2次", .95, 237, 156 });
    assert(!strategy_badge_report_text_verified(wrong_badge, amiya, 3));
    wrong_badge = badge_capture;
    wrong_badge[1].text = "阿";
    assert(!strategy_badge_report_text_verified(wrong_badge, amiya, 3));
    auto silver_report = badge_capture;
    silver_report.back().text = "已通过模拟6次";
    observed = observe_page(silver_report, identities);
    assert(observed.page == Page::StrategyReport && observed.band_id == amiya.band_id && observed.wins == 6);
    assert(strategy_badge_report_text_verified(silver_report, amiya, 6));
    assert(!strategy_badge_report_text_verified(silver_report, amiya, 3));
    assert(!strategy_badge_report_text_verified(badge_capture, amiya, 6));
    silver_report.back().text = "已通过模拟5次";
    assert(observe_page(silver_report, identities).wins == 5);
    assert(!strategy_badge_report_text_verified(silver_report, amiya, 6));
    silver_report.back().text = "已通过模拟7次";
    assert(observe_page(silver_report, identities).contradictory);
    std::vector<TextObservation> difficulty { { "请选择模拟难度", 0.95, 900, 25 },
                                              { "开始模拟", 0.95, 1000, 650 },
                                              { "规程等级13", 0.95, 25, 650 },
                                              { "规程等级14", 0.95, 950, 230 } };
    observed = observe_page(difficulty, identities);
    assert(observed.page == Page::Difficulty && observed.selected_difficulty == 13);
    // OCR captured from a real joint selector parked on the training row.
    std::vector<TextObservation> joint_training {
        { "请选择模拟难度", .999424, 1021, 23 },
        { "险境模拟", .949041, 1009, 71 },
        { "标准模拟", .999943, 996, 189 },
        { "入门协议", .999948, 1005, 307 },
        { "当前选择", .999981, 770, 334 },
        { "开始训练", .999342, 1077, 637 },
        { "模拟强度", .999224, 157, 648 },
        { "入门协议", .980425, 29, 676 },
        { "用以熟悉协议内容的模式", .999874, 141, 682 },
    };
    observed = observe_page(joint_training, identities);
    assert(observed.page == Page::Difficulty && observed.selected_difficulty == JointTrainingDifficulty);
    assert(!valid_difficulty_request(JointTrainingDifficulty, 4));
    assert(joint_difficulty_from_label("标准模拟") == 1);
    assert(joint_difficulty_scroll(1, JointTrainingDifficulty, 0, 2) == DifficultyScroll::Hold);
    assert(joint_difficulty_scroll(1, JointTrainingDifficulty, 0, 0) == DifficultyScroll::Higher);
    assert(joint_difficulty_scroll(1, JointTrainingDifficulty, {}, {}) == DifficultyScroll::Higher);
    assert(joint_difficulty_scroll(1, 4, 2, 4) == DifficultyScroll::Lower);
    auto joint_standard_selector = joint_training;
    joint_standard_selector[5].text = "开始搜寻";
    joint_standard_selector[7].text = "标准模拟";
    observed = observe_page(joint_standard_selector, identities);
    assert(observed.page == Page::Difficulty && observed.selected_difficulty == 1);
    joint_training[7].text = "标准模拟";
    assert(!observe_page(joint_training, identities).selected_difficulty);
    joint_standard_selector.push_back({ "入门协议", .99, 29, 680 });
    observed = observe_page(joint_standard_selector, identities);
    assert(observed.contradictory && !observed.selected_difficulty);
    std::vector<TextObservation> preparation {
        { "准备就绪", 0.99, 1165, 103 }, { "刷新", 0.99, 1192, 446 },
        { "冻结", 0.99, 1044, 446 },     { "剩余可放置角色：6", 0.99, 1109, 378 },
        { "回合", 0.99, 445, 21 },       { "1", 0.99, 489, 23 },
        { "C", 0.99, 1192, 520 }
    };
    observed = observe_page(preparation, identities);
    assert(observed.page == Page::Preparation && observed.round == 1 && observed.remaining_deployment == 6);
    assert(!observed.funds);
    auto free_refresh_preparation = preparation;
    free_refresh_preparation.push_back({ "FREEx1", 0.99, 1153, 419 });
    assert(observe_page(free_refresh_preparation, identities).free_refreshes == 1);
    free_refresh_preparation.back().text = "FREE × 2";
    assert(observe_page(free_refresh_preparation, identities).free_refreshes == 2);
    free_refresh_preparation.back() = { "FREE", 0.99, 1153, 419 };
    free_refresh_preparation.push_back({ "3", 0.99, 1220, 419 });
    assert(observe_page(free_refresh_preparation, identities).free_refreshes == 3);
    auto distinct_refresh_counters = preparation;
    distinct_refresh_counters.push_back({ "FREEx1", .99, 1153, 419 });
    distinct_refresh_counters.push_back({ "6", .99, 1255, 378 });
    const auto separate_refresh = observe_page(distinct_refresh_counters, identities);
    assert(
        !separate_refresh.contradictory && separate_refresh.free_refreshes == 1 &&
        separate_refresh.remaining_deployment == 6);
    const auto refresh_fixture = json::open(std::filesystem::current_path() / "spa-free-refresh-conflict-fixture.json");
    assert(refresh_fixture);
    std::vector<TextObservation> refresh_fixture_texts;
    for (const auto& item : refresh_fixture->at("texts").as_array()) {
        refresh_fixture_texts.push_back(
            { item.at("text").as_string(),
              item.at("score").as_double(),
              item.at("x").as_integer(),
              item.at("y").as_integer() });
    }
    const auto actual_refresh_preparation = observe_page(refresh_fixture_texts, identities);
    assert(actual_refresh_preparation.page == Page::Preparation && !actual_refresh_preparation.contradictory);
    assert(actual_refresh_preparation.round == 8 && actual_refresh_preparation.funds == 6);
    assert(actual_refresh_preparation.remaining_deployment == 0 && actual_refresh_preparation.free_refreshes == 1);
    auto paid_refresh_preparation = preparation;
    std::vector<TextObservation> joint_bonus_preparation { { "险境模拟", .999, 144, 21 },
                                                           { "回合", .999, 445, 21 },
                                                           { "??", .829, 489, 23 },
                                                           { "准备就绪", .999, 1130, 110 },
                                                           { "剩余可放置角色", .999, 1100, 378 } };
    const auto joint_bonus = observe_page(joint_bonus_preparation, identities);
    assert(
        joint_bonus.page == Page::Preparation && joint_bonus.round == HiddenCoreRound &&
        joint_bonus.bonus_round == HiddenRoundMode::JointRisk);
    assert(!joint_bonus.result_completed && !joint_bonus.contradictory);
    auto conflicting_bonus = joint_bonus_preparation;
    conflicting_bonus.push_back({ "14", .999, 489, 23 });
    const auto contradictory_bonus = observe_page(conflicting_bonus, identities);
    assert(contradictory_bonus.contradictory && !contradictory_bonus.round && !contradictory_bonus.bonus_round);
    for (const auto& mode : { "规程等级5", "规程等级14", "规程等级20" }) {
        auto solo_bonus = joint_bonus_preparation;
        solo_bonus[0].text = mode;
        solo_bonus[2].text = "？？";
        const auto solo = observe_page(solo_bonus, identities);
        assert(
            solo.page == Page::Preparation && solo.round == HiddenCoreRound &&
            solo.bonus_round == HiddenRoundMode::Independent && !solo.contradictory);
        assert(!solo.result_completed);
        solo_bonus.push_back({ "标准模拟", .999, 145, 21 });
        assert(!observe_page(solo_bonus, identities).round);
    }
    for (const auto& mode : { "规程等级2", "规程等级4", "规程等级21", "极速模拟", "标准模拟" }) {
        auto other_mode_bonus = joint_bonus_preparation;
        other_mode_bonus[0].text = mode;
        assert(!observe_page(other_mode_bonus, identities).round);
    }
    auto invalid_bonus = joint_bonus_preparation;
    invalid_bonus[2].score = .79;
    assert(!observe_page(invalid_bonus, identities).round);
    invalid_bonus = joint_bonus_preparation;
    invalid_bonus[2].x = 800;
    assert(!observe_page(invalid_bonus, identities).bonus_round);
    invalid_bonus = joint_bonus_preparation;
    invalid_bonus[1].x = 900;
    assert(!observe_page(invalid_bonus, identities).bonus_round);
    paid_refresh_preparation.push_back({ "1", 0.99, 1180, 419 });
    paid_refresh_preparation.push_back({ "FREE x9", 0.99, 200, 640 });
    const auto paid_refresh_observed = observe_page(paid_refresh_preparation, identities);
    assert(paid_refresh_observed.free_refreshes == 0 && paid_refresh_observed.paid_refresh_price == 1);
    auto leveled_preparation = preparation;
    leveled_preparation.push_back({ "LEVEL", 0.99, 376, 558 });
    leveled_preparation.push_back({ "5", 0.99, 389, 530 });
    observed = observe_page(leveled_preparation, identities);
    assert(observed.page == Page::Preparation && !observed.shop_level); // Upgrade price is not book V.
    leveled_preparation.push_back({ "V", 0.99, 389, 580 });
    observed = observe_page(leveled_preparation, identities);
    assert(observed.page == Page::Preparation && observed.shop_level == 5);
    auto low_confidence_funds = preparation;
    low_confidence_funds.push_back({ "7", 0.455, 1180, 520 });
    assert(!observe_page(low_confidence_funds, identities).funds);
    low_confidence_funds.push_back({ "7", 0.65, 1180, 520 });
    assert(observe_page(low_confidence_funds, identities).funds == 7);
    low_confidence_funds.push_back({ "1", 0.99, 1180, 520 });
    assert(observe_page(low_confidence_funds, identities).contradictory);
    observed =
        observe_page({ { "回合", 0.99, 445, 20 }, { "/10", 0.99, 652, 18 }, { "0", 0.40, 645, 25 } }, identities);
    assert(observed.page == Page::Battle && observed.enemies_total == 10 && !observed.enemies_resolved);
    observed =
        observe_page({ { "回合", 0.99, 445, 20 }, { "/10", 0.99, 652, 18 }, { "1/11", 0.99, 640, 20 } }, identities);
    assert(observed.contradictory && !observed.enemies_total && !observed.enemies_resolved);
    const auto battle_counter_gap = observe_page({ { "回合", 0.99, 445, 20 }, { "54", 0.99, 1140, 31 } }, identities);
    assert(battle_counter_gap.page == Page::Battle && !battle_counter_gap.enemies_total);
    const std::vector<TextObservation> final_boss_hud { { "规程等级5", .99994, 138, 21 },
                                                        { "自己", .999966, 620, 604 },
                                                        { "中12/13", .857279, 462, 19 },
                                                        { "12/1", .992874, 489, 23 } };
    const auto boss_fight = observe_page(final_boss_hud, identities);
    assert(boss_fight.page == Page::Battle && boss_fight.enemies_resolved == 12 && boss_fight.enemies_total == 13);
    assert(!boss_fight.round && !boss_fight.contradictory);
    auto no_self_view = final_boss_hud;
    no_self_view.erase(no_self_view.begin() + 1);
    assert(observe_page(no_self_view, identities).page != Page::Battle);
    auto boss_with_ready = final_boss_hud;
    boss_with_ready.push_back({ "准备就绪", .99, 1160, 103 });
    assert(observe_page(boss_with_ready, identities).page != Page::Battle);
    // Actual quick batch child 2 R9 OCR. No ordinary round HUD is visible.
    std::vector<TextObservation> quick_boss_hud { { "极速模拟", .999930, 145, 19 },
                                                  { "7/25", .970192, 466, 19 },
                                                  { "自己", .999953, 620, 604 } };
    const auto quick_boss = observe_page(quick_boss_hud, identities);
    assert(quick_boss.page == Page::Battle && quick_boss.enemies_resolved == 7 && quick_boss.enemies_total == 25);
    assert(!quick_boss.round && !quick_boss.contradictory);
    quick_boss_hud.push_back({ "准备就绪", .99, 1165, 104 });
    assert(observe_page(quick_boss_hud, identities).page != Page::Battle);
    quick_boss_hud.pop_back();
    quick_boss_hud.pop_back();
    assert(observe_page(quick_boss_hud, identities).page != Page::Battle);
    std::vector<TextObservation> joint_boss_hud { { "险境模拟", .999, 144, 21 },
                                                  { "40/41", .999, 460, 21 },
                                                  { "全景", .999, 617, 603 },
                                                  { "返回战场", .999, 22, 267 } };
    const auto joint_boss = observe_page(joint_boss_hud, identities);
    assert(joint_boss.page == Page::Battle && joint_boss.enemies_resolved == 40 && joint_boss.enemies_total == 41);
    assert(!joint_boss.round && !joint_boss.contradictory);
    joint_boss_hud.back().y = 434; // Own avatar is fourth in the next live match.
    assert(observe_page(joint_boss_hud, identities).page == Page::Battle);
    joint_boss_hud.pop_back();
    assert(observe_page(joint_boss_hud, identities).page != Page::Battle);
    preparation.push_back({ "0", 0.99, 1180, 520 });
    assert(observe_page(preparation, identities).funds == 0);
    preparation.push_back({ "2", 0.99, 1180, 520 });
    observed = observe_page(preparation, identities);
    assert(observed.contradictory && !observed.funds);
    auto fixture_path = std::filesystem::current_path();
    fixture_path /= "spa-preparation-fixture.json";
    const auto fixture = json::open(fixture_path);
    assert(fixture);
    std::vector<TextObservation> live_texts;
    for (const auto& item : fixture->at("texts").as_array()) {
        live_texts.push_back(
            { item.at("text").as_string(),
              item.at("score").as_double(),
              item.at("x").as_integer(),
              item.at("y").as_integer() });
    }
    observed = observe_page(live_texts, identities);
    assert(observed.page == Page::Preparation && !observed.contradictory);
    assert(observed.round == 1 && observed.funds == 0 && observed.remaining_deployment == 6);
    live_texts.push_back({ "6", 0.847, 1240, 537 });
    observed = observe_page(live_texts, identities);
    assert(!observed.contradictory && observed.funds == 0);
    observed =
        observe_page({ { "回合", 0.99, 445, 20 }, { "1/10", 0.99, 640, 20 }, { "36", 0.99, 1180, 520 } }, identities);
    assert(observed.page == Page::Battle && observed.enemies_resolved == 1 && observed.enemies_total == 10);
    assert(!observed.funds);
    observed =
        observe_page({ { "回合", 0.99, 445, 20 }, { "/10", 0.99, 652, 18 }, { "0", 0.40, 645, 25 } }, identities);
    assert(observed.page == Page::Battle && observed.enemies_total == 10 && !observed.enemies_resolved);
    observed =
        observe_page({ { "回合", 0.99, 445, 20 }, { "/10", 0.99, 652, 18 }, { "1/11", 0.99, 640, 20 } }, identities);
    assert(observed.contradictory && !observed.enemies_total && !observed.enemies_resolved);
    observed =
        observe_page({ { "道具补给", 0.99, 150, 30 }, { "无需消耗资金，获得装备补给。", 0.99, 370, 30 } }, identities);
    assert(observed.page == Page::EquipmentSupply && !observed.funds);
    const auto secret_supply = observe_page(
        { { "机密商店", 0.99, 160, 30 },
          { "无需消耗资金，获得装备补给。", 0.99, 370, 30 },
          { "动能单手弩", 0.99, 280, 290 },
          { "盟约之币", 0.99, 620, 290 },
          { "M3茧甲", 0.99, 960, 290 } },
        identities);
    assert(secret_supply.page == Page::EquipmentSupply);
    assert(observe_page({ { "机密商店", 0.99, 160, 30 } }, identities).page != Page::EquipmentSupply);
    const auto tactical_fixture = json::open(std::filesystem::current_path() / "spa-tactical-decision-fixture.json");
    assert(observe_page({ { "数据已更新，即将进行同步", .999, 482, 317 } }, identities).page == Page::DataUpdated);
    assert(observe_page({ { "数据已更新，即将进行同步", .999, 482, 617 } }, identities).page != Page::DataUpdated);
    // Actual R12 modal OCR: its long warning starts at x184, outside the old
    // narrow modal ROI. It must dispatch back to the existing session.
    const std::vector<TextObservation> temporary_inventory_modal {
        { "确认准备就绪吗?当前仍处于临时整备区的干员和装备,将在进入战斗后自动销毁", .986556, 184, 287 },
        { "(召唤物仍会在下个休整期出现)", .995604, 461, 324 },
        { "取消", .99936, 313, 467 },
        { "确认", .999893, 941, 468 }
    };
    assert(observe_ready_confirmation(temporary_inventory_modal));
    assert(observe_page(temporary_inventory_modal, identities).page == Page::ReadyConfirmation);
    const auto review_again = ready_confirmation_control(temporary_inventory_modal, false);
    const auto ready_to_fight = ready_confirmation_control(temporary_inventory_modal, true);
    assert(review_again && review_again->text == "取消" && review_again->x == 313 && review_again->y == 467);
    assert(ready_to_fight && ready_to_fight->text == "确认" && ready_to_fight->x == 941 && ready_to_fight->y == 468);
    assert(!ready_confirmation_control(live_texts, true));
    assert(observe_ready_confirmation(
        { { "确认准备吗？", .99, 550, 290 }, { "取消", .99, 320, 467 }, { "确认", .99, 941, 468 } }));
    assert(observe_ready_confirmation(
        { { "剩余资金将在离开休整期时清空", .99, 460, 320 }, { "取消", .99, 320, 467 }, { "确认", .99, 941, 468 } }));
    for (std::size_t index = 0; index < temporary_inventory_modal.size(); ++index) {
        if (index == 1) {
            continue;
        }
        auto incomplete_modal = temporary_inventory_modal;
        incomplete_modal.erase(incomplete_modal.begin() + index);
        assert(!observe_ready_confirmation(incomplete_modal));
        assert(!ready_confirmation_control(incomplete_modal, false));
        assert(!ready_confirmation_control(incomplete_modal, true));
    }
    auto duplicate_modal = temporary_inventory_modal;
    duplicate_modal.push_back(temporary_inventory_modal.back());
    assert(!observe_ready_confirmation(duplicate_modal));
    assert(!ready_confirmation_control(duplicate_modal, true));
    auto misplaced_modal = temporary_inventory_modal;
    misplaced_modal.front().y = 600;
    assert(!observe_ready_confirmation(misplaced_modal));
    auto uncertain_modal = temporary_inventory_modal;
    uncertain_modal.front().score = .89;
    assert(!observe_ready_confirmation(uncertain_modal));
    assert(!observe_ready_confirmation(
        { { "确认放弃模拟吗？", .99, 550, 290 }, { "取消", .99, 320, 467 }, { "确认", .99, 941, 468 } }));
    assert(tactical_fixture);
    std::vector<TextObservation> tactical_texts;
    for (const auto& item : tactical_fixture->at("texts").as_array()) {
        tactical_texts.push_back(
            { item.at("text").as_string(),
              item.at("score").as_double(),
              item.at("x").as_integer(),
              item.at("y").as_integer() });
    }
    assert(observe_page(tactical_texts, identities).page == Page::TacticalDecision);
    assert(observe_page({ { "战术决策", .99, 176, 30 } }, identities).page != Page::TacticalDecision);
    const auto tactical_offers = observe_tactical_offers(tactical_texts);
    assert(
        tactical_offers.size() == 3 && tactical_offers[0].title == "锏的盟誓" &&
        tactical_offers[1].title == "阿戈尔驰援" && tactical_offers[2].title == "补给");
    assert(choose_tactical_adjustment(tactical_offers, "阿戈尔", { "阿戈尔", "迅捷" }, 8) == 2);
    assert(choose_tactical_adjustment(tactical_offers, "阿戈尔", { "阿戈尔" }, 4) == 1);
    assert(choose_tactical_adjustment(tactical_offers, "谢拉格", { "谢拉格" }, 8) == 0);
    assert(choose_tactical_adjustment({ { "补给", "unknown text", 2 } }, "阿戈尔", {}, 8) == -1);
    // Real round-9 tactical OCR for the stalled difficulty-2 purchase.
    const auto elite_offer = observe_tactical_offers(
        { { "升华", .99, 281, 285 }, { "你接下来购买的1名干员变为精锐状", .99, 184, 358 }, { "态", .99, 182, 381 } });
    assert(tactical_elite_purchase(elite_offer[0]));
    assert(!tactical_elite_purchase({ "升华", "已部署的1名干员变为精锐状态", 0 }));
    assert(!tactical_elite_purchase({ "整备", elite_offer[0].description, 0 }));
    assert(choose_tactical_adjustment(elite_offer, "维多利亚", {}, 3) == 0);
    const auto bench_harold =
        observe_operator_card({ { "哈洛德", .99, 12, 182 }, { "55", .99, 105, 214 } }, { "哈洛德" });
    assert(bench_harold && bench_harold->elite);
    OwnedOperator harold { 2, false, true };
    const auto has_harold_forms = [&](int initial, int elite) {
        const auto forms = owned_form_counts(harold);
        return forms && forms->initial == initial && forms->elite == elite;
    };
    assert(add_purchased_operator(harold, true));
    assert(harold.copies == 3 && harold.deployed);
    assert(has_harold_forms(2, 1));
    assert(!promote_initial_copies(harold, promotion_copies_required("哈洛德")));
    assert(add_purchased_operator(harold, false));
    assert(has_harold_forms(3, 1));
    assert(promote_initial_copies(harold, promotion_copies_required("哈洛德")));
    assert(has_harold_forms(0, 2));
    assert(tactical_layer_gain("【萨尔贡】【灵巧】层数+15，若存在其他队友则他们也获得") == 15);
    assert(!tactical_layer_gain("层数+0"));
    assert(!tactical_layer_gain("层数+1O"));
    assert(!tactical_layer_gain("层数+15%"));
    assert(!tactical_layer_gain("层数+15％"));
    assert(!tactical_layer_gain("层数+15.5"));
    assert(!tactical_layer_gain("层数+12，层数+15"));
    const auto layers15_fixture = json::open(std::filesystem::current_path() / "spa-tactical-layers15-fixture.json");
    assert(layers15_fixture);
    std::vector<TextObservation> layers15_texts;
    for (const auto& item : layers15_fixture->at("texts").as_array()) {
        layers15_texts.push_back(
            { item.at("text").as_string(),
              item.at("score").as_double(),
              item.at("x").as_integer(),
              item.at("y").as_integer() });
    }
    assert(observe_page(layers15_texts, identities).page == Page::TacticalDecision);
    const auto layers15_offers = observe_tactical_offers(layers15_texts);
    assert(layers15_offers[1].title == "佩佩的盟誓");
    assert(choose_tactical_adjustment(layers15_offers, "萨尔贡", { "萨尔贡" }, 8) == 1);
    assert(choose_tactical_adjustment(layers15_offers, "谢拉格", { "谢拉格" }, 8) == 2);
    assert(choose_tactical_adjustment(layers15_offers, "维多利亚", { "维多利亚" }, 8) == 0);
    auto tactical_without_description = tactical_texts;
    std::erase_if(tactical_without_description, [](const auto& text) { return text.y >= 330 && text.y < 430; });
    assert(choose_tactical_adjustment(observe_tactical_offers(tactical_without_description), "阿戈尔", {}, 8) == -1);
    observed = observe_page({ { "点击取消", 0.99, 400, 120 }, { "攻击范围", 0.99, 140, 250 } }, identities);
    assert(observed.page == Page::Placement && !observed.funds);
    observed = observe_page({ { "出售", 0.99, 620, 346 }, { "攻击范围", 0.99, 140, 250 } }, identities);
    assert(observed.page == Page::Placement && !observed.funds);
    assert(shop_column_from_right(973) == 0);
    assert(shop_column_from_right(645) == 2);
    assert(shop_column_from_right(318) == 4);
    assert(!shop_column_from_right(20));
    const auto catalog = operator_pacts();
    const auto requirements = pact_requirements();
    // The catalogue grows as current-season identities are added.  Keep a
    // lower-bound regression check instead of freezing the test to an old
    // snapshot size.
    assert(catalog.size() >= 143);
    const auto argo_candidates = choose_formation_candidates(
        "阿戈尔",
        { "幽灵鲨", "unknown-card", "归溟幽灵鲨", "歌蕾蒂娅", "幽灵鲨" },
        catalog,
        requirements);
    assert(
        (argo_candidates.valid && argo_candidates.core_pact == "阿戈尔" &&
         argo_candidates.operators == std::vector<std::string> { "幽灵鲨", "归溟幽灵鲨", "歌蕾蒂娅" }));
    const auto argo_incomplete =
        choose_formation_candidates("阿戈尔", { "幽灵鲨", "归溟幽灵鲨", "unknown-card" }, catalog, requirements);
    assert(!argo_incomplete.valid && argo_incomplete.operators.empty());
    const auto auto_candidates = choose_formation_candidates(
        "auto",
        { "慕斯", "Miss.Christine", "幽灵鲨", "归溟幽灵鲨", "歌蕾蒂娅" },
        catalog,
        requirements);
    assert(auto_candidates.valid && auto_candidates.core_pact == "阿戈尔");
    assert(!choose_formation_candidates("unknown-faction", { "幽灵鲨" }, catalog, requirements).valid);
    std::vector<RosterUnit> roster { { "慕斯", true, {} },           { "灰喉", true, {} }, { "拉普兰德", true, {} },
                                     { "Miss.Christine", true, {} }, { "卡达", true, {} }, { "褐果", true, {} },
                                     { "波登可", true, {} } };
    const std::vector<std::string> enabled { "维多利亚", "精准", "叙拉古", "奥术", "聚合", "迅捷" };
    assert(!evaluate_formation({}, catalog, requirements, { "unrecognized_pact" }).complete);
    PreparationPolicy preparation_policy;
    preparation_policy.primary_pact = "谢拉格";
    preparation_policy.carries = { "灵知" };
    preparation_policy.owned["角峰"] = { 1, false };
    const auto core_carry =
        choose_preparation(preparation_policy, 5, { { "角峰", 2, 0 }, { "灵知", 4, 1 }, { "灰喉", 2, 2 } }, catalog);
    assert(core_carry.action == PreparationAction::Buy && core_carry.name == "灵知");
    PreparationPolicy kazimierz_policy;
    kazimierz_policy.primary_pact = "卡西米尔";
    kazimierz_policy.carries = { "远牙" };
    kazimierz_policy.owned["砾"] = { 1, false, true };
    const auto kazimierz_choice = choose_preparation(kazimierz_policy, 4, { { "远牙", 2, 0 } }, catalog);
    assert(kazimierz_choice.action == PreparationAction::Buy && kazimierz_choice.name == "远牙");
    assert(
        choose_preparation(preparation_policy, 1, { { "灰喉", 2, 0 } }, catalog).action == PreparationAction::Review);
    assert(
        choose_preparation(preparation_policy, 3, { { "灰喉", 2, 0 } }, catalog).action == PreparationAction::Refresh);
    preparation_policy.owned["角峰"].copies = 2;
    preparation_policy.allow_refresh = false;
    assert(choose_preparation(preparation_policy, 10, { { "角峰", 2, 0 } }, catalog).action == PreparationAction::Buy);
    assert(
        choose_preparation(preparation_policy, 10, { { "灵知", 4, 0 }, { "角峰", 2, 0 } }, catalog).action ==
        PreparationAction::Review);
    preparation_policy.bench_occupied = 10;
    assert(
        choose_preparation(preparation_policy, 10, { { "灵知", 4, 0 } }, catalog).action == PreparationAction::Review);
    preparation_policy.bench_occupied = 2;
    preparation_policy.round = 7;
    preparation_policy.allow_refresh = true;
    preparation_policy.owned["角峰"] = { 0, true, true };
    preparation_policy.owned["雪猎"] = { 1, false, true };
    const auto late_core = choose_preparation(preparation_policy, 10, { { "灰喉", 2, 0 } }, catalog);
    assert(late_core.action == PreparationAction::Refresh);
    const auto late_pair = choose_preparation(preparation_policy, 4, { { "雪猎", 2, 0 } }, catalog);
    assert(late_pair.action == PreparationAction::Buy && late_pair.name == "雪猎");
    PreparationPolicy freeze_value;
    freeze_value.primary_pact = "谢拉格";
    freeze_value.primary_locked_after_activation = true;
    freeze_value.shop_level = 5;
    freeze_value.owned = { { "角峰", { 2, false, true } },
                           { "初雪", { 1, false, true } },
                           { "雪猎", { 2, false, true } } };
    assert(choose_preparation(freeze_value, 0, { { "角峰", 2, 0 } }, catalog).action != PreparationAction::Freeze);
    assert(choose_preparation(freeze_value, 0, { { "雪猎", 2, 0 } }, catalog).action != PreparationAction::Freeze);
    assert(choose_preparation(freeze_value, 0, { { "凛御银灰", 4, 0 } }, catalog).action == PreparationAction::Freeze);
    freeze_value.owned["凛御银灰"] = { 1, true, true };
    assert(choose_preparation(freeze_value, 0, { { "凛御银灰", 4, 0 } }, catalog).action != PreparationAction::Freeze);
    freeze_value.owned["初雪"].copies = 2;
    assert(choose_preparation(freeze_value, 0, { { "初雪", 3, 0 } }, catalog).action == PreparationAction::Freeze);
    const std::vector<ShopOffer> weak_frozen_shop { { "初雪", 3, 0 },
                                                    { "灰喉", 2, 1 },
                                                    { "拉普兰德", 2, 2 },
                                                    { "卡达", 2, 3 } };
    assert(choose_preparation(freeze_value, 0, weak_frozen_shop, catalog).action != PreparationAction::Freeze);
    freeze_value.owned.erase("凛御银灰");
    auto crucial_frozen_shop = weak_frozen_shop;
    crucial_frozen_shop.front() = { "凛御银灰", 4, 0 };
    assert(choose_preparation(freeze_value, 0, crucial_frozen_shop, catalog).action == PreparationAction::Freeze);
    assert(choose_preparation(freeze_value, 2, crucial_frozen_shop, catalog).action != PreparationAction::Freeze);
    assert(choose_preparation(freeze_value, 11, crucial_frozen_shop, catalog).action != PreparationAction::Freeze);
    freeze_value.allow_freeze = false;
    assert(choose_preparation(freeze_value, 0, crucial_frozen_shop, catalog).action != PreparationAction::Freeze);
    // The live R2 shop froze Indigo without owning any Iberia member;
    // R4 froze Skadi with only Specter. Neither preserves an executable
    // faction package, even if the card is named as a future growth engine.
    PreparationPolicy speculative_freeze;
    speculative_freeze.primary_pact = "伊比利亚";
    speculative_freeze.round = 2;
    speculative_freeze.shop_level = 2;
    speculative_freeze.owned = { { "烈夏", { 1, false, true } }, { "幽灵鲨", { 1, false, true } } };
    assert(
        choose_preparation(speculative_freeze, 1, { { "卡达", 2, 2 }, { "夏栎", 2, 1 }, { "深靛", 2, 0 } }, catalog)
            .action != PreparationAction::Freeze);
    speculative_freeze.primary_pact = "阿戈尔";
    speculative_freeze.round = 4;
    speculative_freeze.shop_level = 3;
    assert(
        choose_preparation(speculative_freeze, 1, { { "斯卡蒂", 3, 0 } }, catalog).action != PreparationAction::Freeze);
    // Two actual members plus a missing third carry are a real activation
    // package. A low round number alone must not forbid that preservation.
    speculative_freeze.owned["安哲拉"] = { 1, false, true };
    assert(
        choose_preparation(speculative_freeze, 1, { { "乌尔比安", 4, 0 } }, catalog).action ==
        PreparationAction::Freeze);
    PreparationPolicy leithanien_core;
    leithanien_core.primary_pact = "莱塔尼亚";
    leithanien_core.primary_locked_after_activation = true;
    leithanien_core.dynamic_decision_mode = true;
    leithanien_core.deployed_population = 8;
    leithanien_core.shop_level = 5;
    leithanien_core.support_pacts = { "奥术" };
    leithanien_core.owned = { { "地灵", { 1, false, true } },
                              { "折光", { 1, false, true } },
                              { "卡涅利安", { 1, false, true } } };
    leithanien_core.early_board_scores = { { "棘刺", 900 } };
    const auto carry_before_auxiliary =
        choose_preparation(leithanien_core, 4, { { "棘刺", 4, 0 }, { "艾雅法拉", 3, 1 } }, catalog);
    assert(carry_before_auxiliary.action == PreparationAction::Buy && carry_before_auxiliary.name == "艾雅法拉");
    auto zero_layer_auxiliary = leithanien_core;
    zero_layer_auxiliary.support_pacts.clear();
    zero_layer_auxiliary.derive_support_pacts = true;
    zero_layer_auxiliary.owned["艾雅法拉"] = { 1, false, true };
    assert(choose_preparation(zero_layer_auxiliary, 4, { { "棘刺", 4, 0 } }, catalog).action != PreparationAction::Buy);
    zero_layer_auxiliary.owned["霍尔海雅"] = { 1, false, true };
    // Hoolheyak plus Eyjafjalla already activate the two-member pact;
    // another unrelated member does not solve a missing activation.
    assert(choose_preparation(zero_layer_auxiliary, 4, { { "棘刺", 4, 0 } }, catalog).action != PreparationAction::Buy);
    zero_layer_auxiliary.owned.erase("霍尔海雅");
    assert(
        choose_preparation(zero_layer_auxiliary, 4, { { "霍尔海雅", 4, 0 } }, catalog).action ==
        PreparationAction::Buy);
    assert(choose_preparation(preparation_policy, 4, { { "灵知", 4, 0 } }, catalog).action == PreparationAction::Buy);
    PreparationPolicy yan_economy_policy;
    yan_economy_policy.primary_pact = "炎";
    yan_economy_policy.derive_support_pacts = true;
    yan_economy_policy.shop_level = 3;
    yan_economy_policy.owned["山"] = { 1, false, true };
    yan_economy_policy.owned["锡人"] = { 1, false, true };
    yan_economy_policy.owned["渡桥"] = { 1, false, true };
    const auto yan_support =
        choose_preparation(yan_economy_policy, 4, { { "可露希尔", 2, 0 }, { "寒檀", 2, 1 } }, catalog);
    assert(yan_support.action == PreparationAction::Buy && yan_support.name == "可露希尔");
    auto activation_first = preparation_policy;
    activation_first.round = 3;
    activation_first.owned.clear();
    activation_first.owned["角峰"] = { 1, false, true };
    activation_first.owned["圣聆初雪"] = { 1, false, true };
    assert(choose_preparation(activation_first, 4, { { "圣聆初雪", 4, 0 }, { "雪猎", 3, 1 } }, catalog).name == "雪猎");
    activation_first.owned["雪猎"] = { 1, false, true };
    activation_first.owned.erase("圣聆初雪");
    activation_first.owned["灵知"] = { 1, false, true };
    assert(
        choose_preparation(activation_first, 4, { { "灵知", 3, 0 }, { "圣聆初雪", 4, 1 } }, catalog).name ==
        "圣聆初雪");
    activation_first.owned["灵知"].deployed = false;
    const auto awaiting_deployment = choose_preparation(activation_first, 4, { { "圣聆初雪", 4, 0 } }, catalog);
    assert(awaiting_deployment.action == PreparationAction::Buy && awaiting_deployment.name == "圣聆初雪");
    PreparationPolicy dynamic_policy;
    dynamic_policy.primary_pact = "阿戈尔";
    dynamic_policy.enabled_core_pacts = { "阿戈尔", "维多利亚" };
    dynamic_policy.bench_occupied = 0;
    dynamic_policy.round = 2;
    const auto dynamic_choice = choose_dynamic_primary_pact(
        dynamic_policy,
        { { "幽灵鲨", 2, 0 }, { "慕斯", 2, 1 }, { "烛煌", 2, 2 } },
        catalog);
    assert(dynamic_choice.valid && dynamic_choice.primary_pact == "维多利亚");
    assert(dynamic_choice.reason.find("Pivot") != std::string::npos);
    auto specified_faction = dynamic_policy;
    specified_faction.requested_primary_pact = "阿戈尔";
    const std::vector<ShopOffer> faction_shop { { "幽灵鲨", 2, 0 }, { "慕斯", 2, 1 }, { "烛煌", 2, 2 } };
    assert(choose_dynamic_primary_pact(specified_faction, faction_shop, catalog).primary_pact == "阿戈尔");
    specified_faction.disabled_pacts = { "阿戈尔" };
    assert(choose_dynamic_primary_pact(specified_faction, faction_shop, catalog).primary_pact == "维多利亚");
    std::set<std::string> guide_ids;
    for (const auto& route : strategy_guide_routes()) {
        assert(!route.guide_id.empty() && !route.primary.empty() && !route.strategies.empty());
        assert(guide_ids.insert(route.guide_id).second);
    }
    auto guided_policy = dynamic_policy;
    // Unstable Elements still covers Ursus/Leithanien; Jessica now also
    // has a documented Victoria route, exercised separately below.
    guided_policy.selected_strategy_id = "band_mess";
    guided_policy.enabled_core_pacts = { "乌萨斯", "莱塔尼亚", "维多利亚" };
    guided_policy.primary_pact = "维多利亚";
    guided_policy.primary_locked_after_activation = true;
    const std::vector<ShopOffer> guided_shop { { "烈夏", 2, 0 }, { "烛煌", 2, 1 }, { "慕斯", 2, 2 } };
    const auto guided_choice = choose_dynamic_primary_pact(guided_policy, guided_shop, catalog, 6);
    assert(guided_choice.valid && guided_choice.guide_restricted && guided_choice.primary_pact == "乌萨斯");
    assert(std::none_of(guided_choice.ranking.begin(), guided_choice.ranking.end(), [](const auto& signal) {
        return signal.pact == "维多利亚";
    }));
    auto expanded_guide_policy = guided_policy;
    expanded_guide_policy.selected_strategy_id = "band_jesica";
    const auto expanded_guide = choose_dynamic_primary_pact(expanded_guide_policy, guided_shop, catalog, 6);
    assert(expanded_guide.valid && expanded_guide.guide_restricted && expanded_guide.primary_pact == "维多利亚");
    guided_policy.requested_primary_pact = "维多利亚";
    assert(choose_dynamic_primary_pact(guided_policy, guided_shop, catalog, 6).primary_pact == "维多利亚");
    guided_policy.requested_primary_pact.clear();
    guided_policy.disabled_pacts = { "乌萨斯", "莱塔尼亚" };
    const auto all_guide_routes_banned = choose_dynamic_primary_pact(guided_policy, guided_shop, catalog, 6);
    assert(all_guide_routes_banned.primary_pact == "维多利亚" && !all_guide_routes_banned.guide_restricted);
    guided_policy.disabled_pacts.clear();
    guided_policy.selected_strategy_id = "band_mech";
    const auto mixed_guide_choice = choose_dynamic_primary_pact(guided_policy, guided_shop, catalog, 6);
    assert(mixed_guide_choice.guide_restricted && !mixed_guide_choice.noncore_guide_pending);
    assert(mixed_guide_choice.primary_pact == "乌萨斯");
    const std::vector<ShopOffer> precision_shop { { "承曦格雷伊", 2, 0 }, { "空弦", 3, 1 }, { "慕斯", 2, 2 } };
    guided_policy.disabled_pacts = { "乌萨斯" };
    const auto precision_guide = choose_dynamic_primary_pact(guided_policy, precision_shop, catalog, 7);
    assert(precision_guide.valid && precision_guide.guide_restricted && precision_guide.primary_pact == "精准");
    assert(!precision_guide.noncore_guide_pending);
    assert(std::all_of(precision_guide.ranking.begin(), precision_guide.ranking.end(), [](const auto& signal) {
        return signal.pact == "精准";
    }));
    auto precision_purchase_policy = guided_policy;
    precision_purchase_policy.primary_pact = "auto";
    precision_purchase_policy.primary_locked_after_activation = false;
    precision_purchase_policy.early_board_ready = true;
    const auto precision_purchase = choose_preparation(precision_purchase_policy, 7, precision_shop, catalog);
    assert(precision_purchase.selected_pact == "精准" && precision_purchase.action == PreparationAction::Buy);
    assert(precision_purchase.name == "承曦格雷伊" || precision_purchase.name == "空弦");
    guided_policy.disabled_pacts.push_back("精准");
    const auto banned_mech_routes = choose_dynamic_primary_pact(guided_policy, precision_shop, catalog, 7);
    assert(banned_mech_routes.valid && !banned_mech_routes.guide_restricted);
    assert(banned_mech_routes.primary_pact == "维多利亚");
    guided_policy.disabled_pacts.clear();
    guided_policy.selected_strategy_id = "band_takecare";
    const auto unguided_precision = choose_dynamic_primary_pact(guided_policy, precision_shop, catalog, 7);
    assert(unguided_precision.primary_pact == "维多利亚" && !unguided_precision.guide_restricted);
    guided_policy.selected_strategy_id = "band_sell";
    const auto pending_miracle = choose_dynamic_primary_pact(guided_policy, precision_shop, catalog, 7);
    assert(pending_miracle.noncore_guide_pending && !pending_miracle.guide_restricted);
    const auto mech_guides = guided_primary_choices("band_mech");
    assert(mech_guides.pacts == std::vector<std::string>({ "乌萨斯", "精准" }));
    assert(!guided_primary_choices("band_takecare").guide_found);
    // The documented Ursus/Arcane route does not require a dual-pact bridge,
    // but still needs a distinct activation pair and its actual growth unit.
    PreparationPolicy ursus_arcane;
    ursus_arcane.primary_pact = "乌萨斯";
    ursus_arcane.selected_strategy_id = "band_jesica";
    ursus_arcane.shop_level = 4;
    ursus_arcane.derive_support_pacts = true;
    ursus_arcane.owned["霍尔海雅"] = { 1, false, true };
    const std::map<std::string, std::vector<std::string>> arcane_only { { "霍尔海雅", { "奥术" } },
                                                                        { "黑键", { "奥术" } } };
    assert(belongs_to_planned_lineup("黑键", ursus_arcane, arcane_only));
    ursus_arcane.selected_strategy_id = "band_takecare";
    assert(!belongs_to_planned_lineup("黑键", ursus_arcane, arcane_only));
    ursus_arcane.selected_strategy_id = "band_jesica";
    ursus_arcane.disabled_pacts = { "奥术" };
    assert(!belongs_to_planned_lineup("黑键", ursus_arcane, arcane_only));
    ursus_arcane.disabled_pacts.clear();
    ursus_arcane.owned.clear();
    ursus_arcane.owned["黑键"] = { 1, false, true };
    assert(!belongs_to_planned_lineup("黑键", ursus_arcane, arcane_only));
    assert(guided_support_choices("band_jesica", "精准").empty());
    const auto weakness_without_lethania = guided_primary_choices("band_weakness", { "莱塔尼亚" });
    assert(weakness_without_lethania.pacts == std::vector<std::string>({ "精准", "乌萨斯" }));
    const auto weakness_without_arcane = guided_primary_choices("band_weakness", { "莱塔尼亚", "奥术" });
    assert(weakness_without_arcane.pacts == std::vector<std::string>({ "乌萨斯" }));
    assert(guided_primary_choices("band_weakness", { "奥术" }).pacts == std::vector<std::string>({ "乌萨斯" }));
    assert(guided_primary_choices("band_weakness", { "奥术", "乌萨斯" }).pacts.empty());
    assert(guided_support_choices("band_weakness", "精准", { "奥术" }).empty());
    assert(inferred_route_contention("乌萨斯", { "band_jesica" }, {}) == 33);
    assert(inferred_route_contention("乌萨斯", { "band_jesica", "band_jesica" }, {}) == 33);
    assert(inferred_route_contention("乌萨斯", { "band_jesica" }, { "莱塔尼亚" }) == 50);
    assert(inferred_route_contention("精准", { "band_jesica", "unknown-strategy" }, {}) == 0);
    auto competition_policy = dynamic_policy;
    competition_policy.primary_pact = "auto";
    const auto no_competition = choose_dynamic_primary_pact(competition_policy, faction_shop, catalog);
    competition_policy.occupied_strategy_ids = { "band_lmlee" };
    const auto with_competition = choose_dynamic_primary_pact(competition_policy, faction_shop, catalog);
    // Inferred competition must never delete every legal fallback.
    assert(with_competition.valid && with_competition.ranking.size() == no_competition.ranking.size());
    for (const auto& signal : with_competition.ranking) {
        const auto before =
            std::find_if(no_competition.ranking.begin(), no_competition.ranking.end(), [&](const auto& s) {
                return s.pact == signal.pact;
            });
        assert(before != no_competition.ranking.end());
        assert(signal.score == before->score - signal.inferred_contention * 6);
        assert(signal.inferred_contention == 25);
    }
    // One already-owned transition card must not pin the route when a live
    // shop offer contains another faction's named stacker/output hand.
    auto transition_pivot_policy = dynamic_policy;
    transition_pivot_policy.owned["慕斯"] = { 1, false, true };
    transition_pivot_policy.enabled_core_pacts = { "维多利亚", "阿戈尔" };
    const auto transition_pivot = choose_dynamic_primary_pact(transition_pivot_policy, { { "幽灵鲨", 2, 0 } }, catalog);
    assert(transition_pivot.valid && transition_pivot.primary_pact == "阿戈尔");
    const auto transition_victoria =
        std::find_if(transition_pivot.ranking.begin(), transition_pivot.ranking.end(), [](const auto& signal) {
            return signal.pact == "维多利亚";
        });
    const auto transition_argo =
        std::find_if(transition_pivot.ranking.begin(), transition_pivot.ranking.end(), [](const auto& signal) {
            return signal.pact == "阿戈尔";
        });
    assert(transition_victoria != transition_pivot.ranking.end());
    assert(transition_argo != transition_pivot.ranking.end());
    assert(transition_argo->growth_engine_offers == 1 && transition_argo->output_offers == 1);
    assert(transition_argo->score > transition_victoria->score);
    auto live_ursus_transition = transition_pivot_policy;
    live_ursus_transition.primary_pact = "乌萨斯";
    live_ursus_transition.enabled_core_pacts = { "乌萨斯", "阿戈尔" };
    live_ursus_transition.owned = { { "折桠", { 1, false, true } } };
    const auto keep_transition = choose_dynamic_primary_pact(live_ursus_transition, { { "幽灵鲨", 2, 0 } }, catalog);
    assert(keep_transition.valid && keep_transition.primary_pact == "乌萨斯");
    const auto denser_argo = choose_dynamic_primary_pact(
        live_ursus_transition,
        { { "幽灵鲨", 2, 0 }, { "斯卡蒂", 3, 1 }, { "海沫", 3, 2 } },
        catalog);
    assert(denser_argo.valid && denser_argo.primary_pact == "阿戈尔");
    assert(is_primary_output_candidate("幽灵鲨", "阿戈尔"));
    assert(is_primary_growth_engine("幽灵鲨", "阿戈尔"));
    auto ussus_shop_policy = dynamic_policy;
    ussus_shop_policy.primary_pact = "维多利亚";
    ussus_shop_policy.enabled_core_pacts = { "乌萨斯", "维多利亚" };
    const auto ussus_shop_choice = choose_dynamic_primary_pact(
        ussus_shop_policy,
        { { "烈夏", 2, 0 }, { "折桠", 2, 1 }, { "夏栎", 2, 2 } },
        catalog);
    assert(ussus_shop_choice.valid && ussus_shop_choice.primary_pact == "乌萨斯");
    const auto ussus_shop_signal =
        std::find_if(ussus_shop_choice.ranking.begin(), ussus_shop_choice.ranking.end(), [](const auto& signal) {
            return signal.pact == "乌萨斯";
        });
    assert(ussus_shop_signal != ussus_shop_choice.ranking.end());
    assert(ussus_shop_signal->growth_engine_offers == 2 && ussus_shop_signal->role_signal);
    const auto density_only_choice = choose_dynamic_primary_pact(
        dynamic_policy,
        { { "慕斯", 2, 0 }, { "涤火杰西卡", 2, 1 }, { "白铁", 2, 2 } },
        catalog);
    assert(density_only_choice.valid && density_only_choice.primary_pact == "维多利亚");
    const auto density_signal =
        std::find_if(density_only_choice.ranking.begin(), density_only_choice.ranking.end(), [](const auto& signal) {
            return signal.pact == "维多利亚";
        });
    assert(density_signal != density_only_choice.ranking.end() && density_signal->distinct_members >= 3);
    assert(density_signal->activation_ready && density_signal->role_signal);
    auto below_victoria_window = dynamic_policy;
    below_victoria_window.enabled_core_pacts = { "维多利亚" };
    below_victoria_window.shop_level = 2;
    const auto below_window_choice = choose_dynamic_primary_pact(
        below_victoria_window,
        { { "慕斯", 2, 0 }, { "涤火杰西卡", 2, 1 }, { "白铁", 2, 2 } },
        catalog);
    assert(below_window_choice.valid && below_window_choice.primary_pact == "维多利亚");
    const auto below_window_signal =
        std::find_if(below_window_choice.ranking.begin(), below_window_choice.ranking.end(), [](const auto& signal) {
            return signal.pact == "维多利亚";
        });
    assert(below_window_signal != below_window_choice.ranking.end() && !below_window_signal->book_window_met);
    below_victoria_window.shop_level = 3;
    assert(choose_dynamic_primary_pact(
               below_victoria_window,
               { { "慕斯", 2, 0 }, { "涤火杰西卡", 2, 1 }, { "白铁", 2, 2 } },
               catalog)
               .valid);
    auto yan_density_policy = dynamic_policy;
    yan_density_policy.primary_pact = "阿戈尔";
    yan_density_policy.enabled_core_pacts = { "炎", "阿戈尔" };
    const auto yan_density_choice = choose_dynamic_primary_pact(
        yan_density_policy,
        { { "望", 2, 0 }, { "拜松", 2, 1 }, { "烛煌", 2, 2 } },
        catalog);
    assert(yan_density_choice.valid && yan_density_choice.primary_pact == "炎");
    auto owned_engine_policy = dynamic_policy;
    owned_engine_policy.owned["幽灵鲨"] = { 1, false, false };
    const auto owned_engine_choice = choose_dynamic_primary_pact(owned_engine_policy, { { "烛煌", 2, 0 } }, catalog);
    assert(owned_engine_choice.valid && !owned_engine_choice.ranking.empty());
    const auto argo_signal =
        std::find_if(owned_engine_choice.ranking.begin(), owned_engine_choice.ranking.end(), [](const auto& signal) {
            return signal.pact == "阿戈尔";
        });
    assert(argo_signal != owned_engine_choice.ranking.end() && argo_signal->growth_engine_owned == 1);
    assert(!argo_signal->activation_ready && argo_signal->role_signal);
    const auto argo_roles = assess_primary_roles("阿戈尔", { "幽灵鲨", "归溟幽灵鲨", "歌蕾蒂娅" }, catalog);
    assert(argo_roles.complete() && argo_roles.growth_engine_owned >= 1 && argo_roles.output_owned >= 1);
    const auto argo_late_roles = assess_primary_roles("阿戈尔", { "浊心斯卡蒂", "乌尔比安", "克莱门莎" }, catalog);
    assert(argo_late_roles.growth_engine_owned >= 1 && argo_late_roles.output_owned >= 2);
    const auto argo_density_without_roles = assess_primary_roles("阿戈尔", { "歌蕾蒂娅", "深巡", "水月" }, catalog);
    assert(!argo_density_without_roles.complete() && argo_density_without_roles.growth_engine_owned == 0);
    const auto victoria_roles = assess_primary_roles("维多利亚", { "慕斯", "水灯心", "涤火杰西卡" }, catalog);
    assert(victoria_roles.output_owned >= 2);
    const auto explicit_preference = [&] {
        auto policy = dynamic_policy;
        policy.dynamic_primary_pact = true;
        policy.primary_pact = "阿戈尔";
        policy.shop_level = 4;
        policy.allow_refresh = false;
        return choose_preparation(policy, 4, { { "慕斯", 2, 0 }, { "涤火杰西卡", 2, 1 }, { "白铁", 2, 2 } }, catalog);
    }();
    assert(
        explicit_preference.action == PreparationAction::Buy && explicit_preference.selected_pact == "维多利亚" &&
        explicit_preference.name == "涤火杰西卡");
    auto dynamic_buy = dynamic_policy;
    dynamic_buy.primary_pact = "auto";
    dynamic_buy.allow_refresh = false;
    const auto dynamic_preparation =
        choose_preparation(dynamic_buy, 4, { { "幽灵鲨", 2, 0 }, { "慕斯", 2, 1 }, { "烛煌", 2, 2 } }, catalog);
    assert(dynamic_preparation.action == PreparationAction::Buy && dynamic_preparation.selected_pact == "维多利亚");
    // The normal GUI always enables dynamic decisions while reusing the
    // guarded executor. The deterministic legacy test mode remains separate.
    auto dynamic_decision = dynamic_policy;
    dynamic_decision.basic_mode = true;
    dynamic_decision.dynamic_decision_mode = true;
    dynamic_decision.primary_pact = "auto";
    dynamic_decision.allow_refresh = false;
    const auto dynamic_decision_preparation =
        choose_preparation(dynamic_decision, 4, { { "幽灵鲨", 2, 0 }, { "慕斯", 2, 1 }, { "烛煌", 2, 2 } }, catalog);
    assert(
        dynamic_decision_preparation.action == PreparationAction::Buy &&
        dynamic_decision_preparation.selected_pact == "维多利亚" &&
        dynamic_decision_preparation.name == dynamic_preparation.name && dynamic_decision_preparation.name == "烛煌" &&
        dynamic_decision_preparation.reason.find("Phase one") == std::string::npos);
    auto upgrade_window = dynamic_decision;
    upgrade_window.primary_pact = "维多利亚";
    upgrade_window.shop_level = 2;
    assert(should_upgrade_dynamic_shop(upgrade_window, 5, 4, {}, catalog));
    assert(!should_upgrade_dynamic_shop(upgrade_window, 5, 4, { { "慕斯", 2, 0 } }, catalog));
    upgrade_window.shop_level = 3;
    assert(!should_upgrade_dynamic_shop(upgrade_window, 5, 4, {}, catalog));
    upgrade_window.owned["慕斯"] = { 1, false, true };
    upgrade_window.owned["Miss.Christine"] = { 1, false, true };
    upgrade_window.owned["水灯心"] = { 1, false, true };
    assert(dynamic_target_shop_level(upgrade_window, catalog) == 5);
    assert(should_upgrade_dynamic_shop(upgrade_window, 8, 4, {}, catalog));
    auto full_upgrade_window = upgrade_window;
    full_upgrade_window.shop_level = 4;
    full_upgrade_window.deployed_population = 8;
    for (const auto* name : { "泡泡", "地灵", "空弦", "褐果", "摆渡人" }) {
        full_upgrade_window.owned[name] = { 1, false, true };
    }
    assert(!should_upgrade_dynamic_shop(full_upgrade_window, 10, 8, { { "慕斯", 2, 0 } }, catalog));
    full_upgrade_window.full_field_offer_improvements["慕斯"] = 0;
    assert(should_upgrade_dynamic_shop(full_upgrade_window, 10, 8, { { "慕斯", 2, 0 } }, catalog));
    full_upgrade_window.full_field_offer_improvements["慕斯"] = 100;
    assert(!should_upgrade_dynamic_shop(full_upgrade_window, 10, 8, { { "慕斯", 2, 0 } }, catalog));
    full_upgrade_window.full_field_offer_improvements["慕斯"] = 0;
    full_upgrade_window.owned["慕斯"].copies = 2;
    assert(!should_upgrade_dynamic_shop(full_upgrade_window, 10, 8, { { "慕斯", 2, 0 } }, catalog));
    auto harmony_window = upgrade_window;
    harmony_window.owned.erase("水灯心");
    harmony_window.owned["魔王"] = { 1, false, true };
    assert(dynamic_target_shop_level(harmony_window, catalog) == 5);
    harmony_window.disabled_pacts = { "调和" };
    assert(dynamic_target_shop_level(harmony_window, catalog) == 3);
    PreparationPolicy egir_terminal_window;
    egir_terminal_window.primary_pact = "阿戈尔";
    egir_terminal_window.shop_level = 5;
    egir_terminal_window.owned = { { "幽灵鲨", { 1, false, true } }, { "安哲拉", { 1, true, true } },
                                   { "深巡", { 1, false, true } },   { "水月", { 1, false, true } },
                                   { "灰喉", { 1, false, true } },   { "纯烬艾雅法拉", { 1, false, true } } };
    assert(dynamic_target_shop_level(egir_terminal_window, catalog) == 6);
    egir_terminal_window.owned["乌尔比安"] = { 1, false, false };
    assert(dynamic_target_shop_level(egir_terminal_window, catalog) == 5);
    egir_terminal_window.owned.erase("乌尔比安");
    egir_terminal_window.owned["克莱门莎"] = { 1, false, true };
    assert(dynamic_target_shop_level(egir_terminal_window, catalog) == 5);
    assert(is_primary_output_candidate("克莱门莎", "阿戈尔"));
    assert(is_primary_output_candidate("幽灵鲨", "阿戈尔"));
    assert(is_primary_output_candidate("斯卡蒂", "阿戈尔"));
    assert(!is_terminal_output_candidate("幽灵鲨", "阿戈尔"));
    assert(!is_terminal_output_candidate("斯卡蒂", "阿戈尔"));
    assert(is_terminal_output_candidate("克莱门莎", "阿戈尔"));
    assert(is_terminal_output_candidate("乌尔比安", "阿戈尔"));
    assert(!is_primary_output_candidate("归溟幽灵鲨", "阿戈尔"));
    assert(!is_primary_output_candidate("浊心斯卡蒂", "阿戈尔"));
    auto egir_gift = egir_terminal_window;
    egir_gift.dynamic_decision_mode = true;
    egir_gift.owned.erase("克莱门莎");
    const auto egir_gift_ranking = rank_promotion_rewards(egir_gift, { "浊心斯卡蒂", "克莱门莎" }, catalog);
    assert(egir_gift_ranking.size() == 2 && egir_gift_ranking.front().name == "克莱门莎");
    upgrade_window.primary_pact = "auto";
    assert(!should_upgrade_dynamic_shop(upgrade_window, 5, 4, {}, catalog));
    upgrade_window.early_board_ready = true;
    upgrade_window.last_round_life_loss = 2;
    upgrade_window.shop_level = 2;
    assert(should_upgrade_dynamic_shop(upgrade_window, 5, 4, { { "慕斯", 2, 0 } }, catalog));
    assert(!should_upgrade_dynamic_shop(upgrade_window, 3, 4, {}, catalog));
    upgrade_window.last_round_life_loss = 6;
    assert(!should_upgrade_dynamic_shop(upgrade_window, 5, 4, {}, catalog));
    auto leave_first_shop = dynamic_decision;
    leave_first_shop.shop_level = 1;
    leave_first_shop.deployed_population = 2;
    assert(should_upgrade_dynamic_shop(leave_first_shop, 5, 3, {}, catalog));
    assert(!should_upgrade_dynamic_shop(leave_first_shop, 4, 3, {}, catalog));
    leave_first_shop.primary_pact = "萨尔贡";
    leave_first_shop.last_round_life_loss = 3;
    assert(!should_upgrade_dynamic_shop(leave_first_shop, 5, 4, {}, catalog));
    leave_first_shop.last_round_life_loss = 0;
    leave_first_shop.early_frontline_count = 2;
    assert(should_upgrade_dynamic_shop(leave_first_shop, 5, 4, {}, catalog));
    // Real D1 batch3 offers. Two standing bodies did not justify consuming
    // recruitment funds in both rounds just because R1 ended without loss.
    auto thin_opening = leave_first_shop;
    thin_opening.primary_pact = "维多利亚";
    thin_opening.round = 2;
    thin_opening.bench_occupied = 0;
    thin_opening.owned = { { "慕斯", { 1, false, true } }, { "拉普兰德", { 1, false, true } } };
    const std::vector<ShopOffer> d1_round_two { { "陨星", 2, 0 }, { "摆渡人", 2, 1 }, { "夏栎", 2, 2 } };
    assert(!should_upgrade_dynamic_shop(thin_opening, 5, 4, d1_round_two, catalog));
    assert(should_upgrade_dynamic_shop(thin_opening, 6, 4, d1_round_two, catalog));
    assert(should_upgrade_dynamic_shop(thin_opening, 5, 4, { { "慕斯", 2, 0 } }, catalog));
    thin_opening.bench_occupied = 2;
    assert(should_upgrade_dynamic_shop(thin_opening, 5, 4, d1_round_two, catalog));
    thin_opening.round = 3;
    thin_opening.bench_occupied = 0;
    thin_opening.shop_level = 2;
    const std::vector<ShopOffer> d1_round_three { { "褐果", 2, 0 },
                                                  { "角峰", 2, 1 },
                                                  { "陨星", 2, 2 },
                                                  { "奥斯塔", 3, 3 } };
    assert(!should_upgrade_dynamic_shop(thin_opening, 7, 7, d1_round_three, catalog));
    assert(should_upgrade_dynamic_shop(thin_opening, 7, 0, d1_round_three, catalog));
    // Run 10 R2: five funds, a two-cost Specter and a four-cost upgrade.
    // The clean opening must not spend away the first Egir chain head.
    auto egir_early_head = leave_first_shop;
    egir_early_head.primary_pact = "阿戈尔";
    egir_early_head.owned.clear();
    egir_early_head.bench_occupied = 0;
    assert(!should_upgrade_dynamic_shop(egir_early_head, 5, 4, { { "幽灵鲨", 2, 0 } }, catalog));
    assert(should_upgrade_dynamic_shop(egir_early_head, 5, 4, {}, catalog));
    assert(should_upgrade_dynamic_shop(egir_early_head, 5, 4, { { "幽灵鲨", 6, 0 } }, catalog));
    egir_early_head.bench_occupied = 10;
    assert(should_upgrade_dynamic_shop(egir_early_head, 5, 4, { { "幽灵鲨", 2, 0 } }, catalog));
    egir_early_head.bench_occupied = 0;
    egir_early_head.owned["幽灵鲨"] = { 1, false, true };
    assert(should_upgrade_dynamic_shop(egir_early_head, 5, 4, { { "幽灵鲨", 2, 0 } }, catalog));
    auto live_activation_window = dynamic_decision;
    live_activation_window.primary_pact = "莱塔尼亚";
    live_activation_window.shop_level = 2;
    live_activation_window.early_board_ready = true;
    live_activation_window.last_round_life_loss = 0;
    live_activation_window.deployed_population = 5;
    live_activation_window.owned = { { "地灵", { 1, false, true } } };
    const std::vector<ShopOffer> real_activation_shop { { "折光", 3, 1 },
                                                        { "复奏", 3, 3 },
                                                        { "艾丝黛尔", 2, 2 },
                                                        { "宴", 3, 0 } };
    assert(!should_upgrade_dynamic_shop(live_activation_window, 8, 6, real_activation_shop, catalog));
    assert(should_upgrade_dynamic_shop(live_activation_window, 8, 6, { { "折光", 3, 1 } }, catalog));
    // Two copies of one shop identity cannot stand in for two distinct
    // activation members, even though they may be useful for synthesis.
    assert(should_upgrade_dynamic_shop(live_activation_window, 8, 6, { { "折光", 3, 1 }, { "折光", 3, 3 } }, catalog));
    assert(should_upgrade_dynamic_shop(live_activation_window, 6, 0, real_activation_shop, catalog));
    leave_first_shop.owned["烈夏"] = { 2, false, true };
    assert(!should_upgrade_dynamic_shop(leave_first_shop, 5, 3, { { "烈夏", 2, 0 } }, catalog));
    auto offered_pair = dynamic_decision;
    offered_pair.shop_level = 1;
    offered_pair.deployed_population = 2;
    offered_pair.early_board_scores = { { "烈夏", 700 }, { "拉普兰德", 700 } };
    offered_pair.owned["拉普兰德"] = { 2, false, true };
    const auto paired = choose_preparation(offered_pair, 4, { { "烈夏", 2, 0 }, { "拉普兰德", 2, 1 } }, catalog);
    assert(paired.action == PreparationAction::Buy && paired.name == "拉普兰德");
    auto missing_ranged = offered_pair;
    missing_ranged.primary_pact = "阿戈尔";
    missing_ranged.primary_locked_after_activation = false;
    missing_ranged.deployed_population = 6;
    missing_ranged.early_board_scores = { { "卡达", 1600 }, { "野鬃", 500 } };
    const auto ranged_repair = choose_preparation(missing_ranged, 4, { { "野鬃", 2, 0 }, { "卡达", 2, 1 } }, catalog);
    assert(ranged_repair.action == PreparationAction::Buy && ranged_repair.name == "卡达");
    missing_ranged.owned["野鬃"] = { 2, false, true };
    const auto off_faction_promotion = choose_preparation(missing_ranged, 2, { { "野鬃", 2, 0 } }, catalog);
    assert(off_faction_promotion.action == PreparationAction::Buy && off_faction_promotion.name == "野鬃");
    auto confirmed_line = missing_ranged;
    confirmed_line.primary_locked_after_activation = true;
    confirmed_line.dynamic_primary_pact = false;
    assert(choose_preparation(confirmed_line, 4, { { "卡达", 2, 0 } }, catalog).action != PreparationAction::Buy);
    assert(choose_preparation(confirmed_line, 2, { { "野鬃", 2, 0 } }, catalog).action != PreparationAction::Buy);
    confirmed_line.support_pacts = { "助力" };
    confirmed_line.owned["安哲拉"] = { 1, false, true };
    confirmed_line.owned.erase("野鬃");
    assert(belongs_to_planned_lineup("野鬃", confirmed_line, catalog));
    assert(choose_preparation(confirmed_line, 2, { { "野鬃", 2, 0 } }, catalog).name == "野鬃");
    confirmed_line.disabled_pacts = { "助力" };
    assert(!belongs_to_planned_lineup("野鬃", confirmed_line, catalog));
    auto full_missing_healer = missing_ranged;
    full_missing_healer.deployed_population = 8;
    full_missing_healer.early_board_scores = { { "褐果", 900 }, { "野鬃", 500 } };
    const auto healer_repair =
        choose_preparation(full_missing_healer, 4, { { "野鬃", 2, 0 }, { "褐果", 3, 1 } }, catalog);
    assert(healer_repair.action == PreparationAction::Buy && healer_repair.name == "褐果");
    const auto full_field_pair = choose_preparation(full_missing_healer, 2, { { "野鬃", 2, 0 } }, catalog);
    assert(full_field_pair.action == PreparationAction::Buy && full_field_pair.name == "野鬃");
    auto full_field_no_pair = full_missing_healer;
    full_field_no_pair.owned["野鬃"].copies = 1;
    assert(choose_preparation(full_field_no_pair, 2, { { "野鬃", 2, 0 } }, catalog).action != PreparationAction::Buy);
    full_field_no_pair.owned["星极"] = { 1, false, false };
    assert(choose_preparation(full_field_no_pair, 2, { { "星极", 2, 0 } }, catalog).name == "星极");
    full_field_no_pair.bench_occupied = 10;
    assert(
        choose_preparation(full_field_no_pair, 2, { { "星极", 2, 0 } }, catalog).action == PreparationAction::Review);
    full_missing_healer.early_board_scores["褐果"] = 250;
    assert(choose_preparation(full_missing_healer, 4, { { "褐果", 3, 1 } }, catalog).action != PreparationAction::Buy);
    full_missing_healer.early_board_scores["褐果"] = 900;
    full_missing_healer.owned["褐果"] = { 1, false, false };
    assert(choose_preparation(full_missing_healer, 4, { { "褐果", 3, 1 } }, catalog).action != PreparationAction::Buy);
    PreparationPolicy helper_line;
    PreparationPolicy awaiting_faction_deployment;
    awaiting_faction_deployment.primary_pact = "炎";
    awaiting_faction_deployment.basic_mode = true;
    awaiting_faction_deployment.dynamic_decision_mode = true;
    awaiting_faction_deployment.early_board_ready = true;
    awaiting_faction_deployment.round = 11;
    awaiting_faction_deployment.shop_level = 5;
    awaiting_faction_deployment.deployed_population = 8;
    awaiting_faction_deployment.bench_occupied = 6;
    awaiting_faction_deployment.owned = { { "拜松", { 1, false, true } },
                                          { "仇白", { 1, false, true } },
                                          { "余", { 1, false, false } } };
    awaiting_faction_deployment.full_field_offer_improvements["望"] = 2000;
    const auto upgrade_while_member_benched =
        choose_preparation(awaiting_faction_deployment, 11, { { "望", 5, 0 } }, catalog);
    assert(upgrade_while_member_benched.action == PreparationAction::Buy && upgrade_while_member_benched.name == "望");
    const auto refresh_while_member_benched = choose_preparation(awaiting_faction_deployment, 11, {}, catalog);
    assert(refresh_while_member_benched.action == PreparationAction::Refresh);
    helper_line.primary_pact = "谢拉格";
    helper_line.owned = { { "角峰", { 1, false, true } }, { "初雪", { 1, false, true } } };
    assert(primary_growth_support_value("空弦", helper_line, catalog) == 0);
    assert(primary_growth_support_value("魔王", helper_line, catalog) > 0);
    const auto harmony_start = choose_preparation(helper_line, 4, { { "魔王", 4, 0 } }, catalog);
    assert(harmony_start.action == PreparationAction::Buy && harmony_start.name == "魔王");
    helper_line.disabled_pacts = { "调和" };
    assert(primary_growth_support_value("魔王", helper_line, catalog) == 0);
    helper_line.disabled_pacts.clear();
    helper_line.owned["耶拉"] = { 1, false, true };
    assert(primary_growth_support_value("空弦", helper_line, catalog) > 0);
    const auto external_growth = choose_preparation(helper_line, 3, { { "空弦", 3, 0 } }, catalog);
    assert(external_growth.action == PreparationAction::Buy && external_growth.name == "空弦");
    helper_line.disabled_pacts = { "谢拉格" };
    assert(primary_growth_support_value("空弦", helper_line, catalog) == 0);
    helper_line.disabled_pacts.clear();
    helper_line.owned["雪猎"] = { 1, false, true };
    helper_line.owned["空弦"] = { 1, false, true };
    assert(primary_growth_support_value("夏栎", helper_line, catalog) == 0);
    std::vector<RosterUnit> helper_roster;
    for (const auto& [name, owned] : helper_line.owned) {
        helper_roster.push_back({ name, owned.deployed, {}, owned.elite });
    }
    const auto helper_formation =
        evaluate_formation(helper_roster, catalog, pact_requirements(), enabled_formation_pacts({ "谢拉格" }, {}));
    assert(
        helper_formation.complete &&
        std::find(helper_formation.active_pacts.begin(), helper_formation.active_pacts.end(), "精准") !=
            helper_formation.active_pacts.end());
    for (const auto& pact : helper_formation.active_pacts) {
        helper_line.observed_pact_layers[pact] = 20;
    }
    helper_line.observed_pact_layers["谢拉格"] = 100;
    assert(primary_growth_support_value("夏栎", helper_line, catalog) > 0);
    assert(primary_growth_support_value("星源", helper_line, catalog) > 0);
    helper_line.observed_pact_layers["精准"] = 100;
    assert(primary_growth_support_value("星源", helper_line, catalog) == 0);
    helper_line.observed_pact_layers.erase("精准");
    assert(primary_growth_support_value("夏栎", helper_line, catalog) == 0);
    auto late_development = dynamic_policy;
    late_development.round = 9;
    late_development.shop_level = 4;
    const auto late_signal = choose_dynamic_primary_pact(late_development, { { "幽灵鲨", 2, 0 } }, catalog);
    assert(late_signal.valid);
    auto locked_dynamic = dynamic_policy;
    locked_dynamic.dynamic_primary_pact = true;
    locked_dynamic.primary_locked_after_activation = true;
    locked_dynamic.allow_refresh = false;
    const auto locked_preparation =
        choose_preparation(locked_dynamic, 4, { { "幽灵鲨", 2, 0 }, { "慕斯", 2, 1 }, { "烛煌", 2, 2 } }, catalog);
    assert(locked_preparation.action == PreparationAction::Buy && locked_preparation.selected_pact == "阿戈尔");
    PreparationPolicy active_transition;
    active_transition.primary_pact = "萨尔贡";
    active_transition.dynamic_primary_pact = true;
    active_transition.primary_locked_after_activation = true;
    active_transition.enabled_core_pacts = { "萨尔贡", "维多利亚" };
    active_transition.shop_level = 5;
    active_transition.owned = { { "艾丝黛尔", { 1, false, true } },
                                { "泡泡", { 1, false, true } },
                                { "莎草", { 1, false, true } },
                                { "慕斯", { 1, false, false } },
                                { "涤火杰西卡", { 1, false, false } } };
    active_transition.observed_pact_layers = { { "萨尔贡", 6 } };
    const std::vector<ShopOffer> funded_transition { { "水灯心", 4, 0 }, { "烛煌", 4, 1 } };
    const auto active_pivot = choose_dynamic_primary_pact(active_transition, funded_transition, catalog, 8);
    assert(active_pivot.valid && active_pivot.primary_pact == "维多利亚");
    assert(choose_dynamic_primary_pact(active_transition, funded_transition, catalog, 2).primary_pact == "萨尔贡");
    active_transition.bench_occupied = 10;
    assert(choose_dynamic_primary_pact(active_transition, funded_transition, catalog, 8).primary_pact == "萨尔贡");
    active_transition.bench_occupied = 2;
    active_transition.observed_pact_layers["萨尔贡"] = 200;
    assert(choose_dynamic_primary_pact(active_transition, funded_transition, catalog, 8).primary_pact == "萨尔贡");
    active_transition.observed_pact_layers.clear();
    assert(choose_dynamic_primary_pact(active_transition, funded_transition, catalog, 8).primary_pact == "萨尔贡");
    active_transition.observed_pact_layers["萨尔贡"] = 6;
    active_transition.disabled_pacts = { "维多利亚" };
    assert(choose_dynamic_primary_pact(active_transition, funded_transition, catalog, 8).primary_pact == "萨尔贡");
    auto early_dynamic = dynamic_policy;
    early_dynamic.shop_level = 1;
    const auto early_choice =
        choose_dynamic_primary_pact(early_dynamic, { { "幽灵鲨", 2, 0 }, { "慕斯", 2, 1 }, { "烛煌", 2, 2 } }, catalog);
    assert(early_choice.valid && early_choice.primary_pact == "维多利亚");
    const auto early_signal =
        std::find_if(early_choice.ranking.begin(), early_choice.ranking.end(), [](const auto& signal) {
            return signal.pact == "维多利亚";
        });
    assert(early_signal != early_choice.ranking.end() && !early_signal->book_window_met);
    const auto no_pool = choose_dynamic_primary_pact(dynamic_policy, {}, catalog);
    assert(!no_pool.valid && no_pool.primary_pact.empty());
    // Live R7: both routes have three members, but Aegir already has its
    // native consumption engine. Another Iberia output must not keep the
    // controller investing in both routes and remove Andreana next round.
    PreparationPolicy live_dual_route;
    live_dual_route.primary_pact = "伊比利亚";
    live_dual_route.primary_locked_after_activation = true;
    live_dual_route.round = 7;
    live_dual_route.shop_level = 4;
    live_dual_route.enabled_core_pacts = { "伊比利亚", "阿戈尔" };
    live_dual_route.observed_pact_layers = { { "伊比利亚", 6 }, { "阿戈尔", 17 } };
    live_dual_route.owned = { { "幽灵鲨", { 1, false, true } }, { "斯卡蒂", { 1, false, true } },
                              { "安哲拉", { 1, false, true } }, { "深靛", { 1, false, true } },
                              { "旅骨", { 1, false, true } },   { "蓝毒", { 1, false, true } },
                              { "瑰盐", { 1, false, true } } };
    const auto converge_native = choose_dynamic_primary_pact(live_dual_route, {}, catalog, 0);
    assert(converge_native.primary_pact == "阿戈尔");
    live_dual_route.dynamic_primary_pact = true;
    const auto converged_purchase =
        choose_preparation(live_dual_route, 5, { { "乌尔比安", 5, 0 }, { "蓝毒", 3, 1 } }, catalog);
    assert(converged_purchase.selected_pact == "阿戈尔" && converged_purchase.name == "乌尔比安");
    const std::vector<ReplacementMember> native_growth_board {
        { "幽灵鲨", PlacementRole::Frontline, { "阿戈尔" }, 500, 875, 235, false },
        { "斯卡蒂", PlacementRole::Frontline, { "阿戈尔" }, 900, 720, 438, false },
        { "安哲拉", PlacementRole::Ranged, { "阿戈尔", "助力" }, 700, 719, 234, true },
        { "深靛", PlacementRole::Ranged, { "伊比利亚" }, 500, 820, 449, false },
        { "旅骨", PlacementRole::Ranged, { "伊比利亚", "精准" }, 700, 901, 371, false },
        { "蓝毒", PlacementRole::Ranged, { "伊比利亚" }, 900, 910, 448, false }
    };
    assert(!choose_replacement(
                converge_native.primary_pact,
                native_growth_board,
                { { "铃兰", PlacementRole::Ranged, { "叙拉古" }, 9000 } })
                .found);
    // Do not pivot away from a genuinely more developed alternative solely
    // because three Aegir names happen to exist.
    live_dual_route.observed_pact_layers["伊比利亚"] = 150;
    assert(choose_dynamic_primary_pact(live_dual_route, {}, catalog, 0).primary_pact == "伊比利亚");
    live_dual_route.observed_pact_layers["伊比利亚"] = 6;
    live_dual_route.owned["安哲拉"].deployed = false;
    assert(choose_dynamic_primary_pact(live_dual_route, {}, catalog, 0).primary_pact == "伊比利亚");
    live_dual_route.owned["安哲拉"].deployed = true;
    live_dual_route.disabled_pacts = { "阿戈尔" };
    assert(choose_dynamic_primary_pact(live_dual_route, {}, catalog, 0).primary_pact == "伊比利亚");
    auto sargon_shop = dynamic_decision;
    sargon_shop.primary_pact = "萨尔贡";
    sargon_shop.dynamic_primary_pact = false;
    sargon_shop.primary_locked_after_activation = true;
    sargon_shop.shop_level = 5;
    sargon_shop.owned = { { "艾丝黛尔", { 1, false, true } },
                          { "泡泡", { 1, false, true } },
                          { "莎草", { 1, false, true } } };
    // The guard/executor flag must not erase the dynamic engine/carry score.
    // A real late-game engine beats another cheap copy of an early member.
    auto sargon_buy = choose_preparation(sargon_shop, 4, { { "莎草", 2, 0 }, { "佩佩", 4, 1 } }, catalog);
    assert(sargon_buy.action == PreparationAction::Buy && sargon_buy.name == "佩佩");
    sargon_shop.owned["泡泡"].copies = 2;
    sargon_buy = choose_preparation(sargon_shop, 4, { { "泡泡", 2, 0 }, { "佩佩", 4, 1 } }, catalog);
    // A missing terminal takes priority over promoting a transition tank.
    assert(sargon_buy.name == "佩佩");
    sargon_shop.owned["鸿雪"] = { 1, false, true };
    sargon_buy = choose_preparation(sargon_shop, 4, { { "泡泡", 2, 0 }, { "佩佩", 4, 1 } }, catalog);
    assert(sargon_buy.name == "泡泡");
    auto harmony_shop = dynamic_decision;
    harmony_shop.primary_pact = "维多利亚";
    harmony_shop.dynamic_primary_pact = false;
    harmony_shop.shop_level = 6;
    harmony_shop.derive_support_pacts = true;
    harmony_shop.owned = { { "慕斯", { 1, false, true } }, { "Miss.Christine", { 1, false, true } } };
    const auto harmony_buy = choose_preparation(harmony_shop, 4, { { "魔王", 4, 0 } }, catalog);
    assert(harmony_buy.action == PreparationAction::Buy && harmony_buy.name == "魔王");
    harmony_shop.disabled_pacts = { "调和" };
    harmony_shop.support_pacts = { "调和" };
    assert(choose_preparation(harmony_shop, 4, { { "魔王", 4, 0 } }, catalog).action == PreparationAction::Review);
    const std::vector<BoardCell> medic_range { { 0, 1 }, { 1, 1 }, { 2, 1 },  { 3, 1 },  { 0, 0 },  { 1, 0 },
                                               { 2, 0 }, { 3, 0 }, { 0, -1 }, { 1, -1 }, { 2, -1 }, { 3, -1 } };
    const std::vector<BoardCell> chain_range { { -1, 1 }, { 0, 1 }, { 1, 1 },   { 2, 1 },  { -1, 0 }, { 0, 0 },
                                               { 1, 0 },  { 2, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 }, { 2, -1 } };
    DeploymentUnit test_medic { "medic", { 2, 1 }, 'N', 1, false, false, true, true, medic_range };
    assert(range_covers(test_medic, { 2, 0 }) && !range_covers(test_medic, { 2, 3 }));
    DeploymentUnit test_chain { "chain", { 1, 1 }, 'S', 1, false, false, true, true, chain_range };
    assert(range_covers(test_chain, { 2, 0 }) && range_covers(test_chain, { 2, 3 }));
    test_chain.cell = { 0, 1 };
    assert(!range_covers(test_chain, { 2, 0 }) && !range_covers(test_chain, { 2, 3 }));
    std::vector<BoardCell> legal_cells;
    for (int column = 0; column < 10; ++column) {
        for (int row = 0; row < 4; ++row) {
            legal_cells.emplace_back(column, row);
        }
    }
    std::vector<DeploymentLane> route_cases { { "upper", {}, 4, 2 }, { "lower", {}, 4, 2 } };
    for (int column = 9; column >= 0; --column) {
        route_cases[0].path.emplace_back(column, 0);
        route_cases[1].path.emplace_back(column, 3);
    }
    std::vector<DeploymentUnit> layout_case {
        { "upper-guard", { 4, 0 }, 'E', 3, true, false, false, false, {} },
        { "lower-guard", { 4, 3 }, 'E', 3, true, false, false, false, {} },
        { "upper-damage", { 3, 0 }, 'E', 0, false, true, false, true, medic_range },
        { "lower-damage", { 3, 3 }, 'E', 0, false, true, false, true, medic_range },
        { "healer", { 3, 1 }, 'S', 1, false, false, true, true, chain_range }
    };
    const auto valid_layout = review_deployment(layout_case, route_cases, legal_cells);
    assert(valid_layout.complete && valid_layout.static_constraints_met);
    std::vector<GridPlacementMember> exposure_members { { 4, 0, PlacementRole::Frontline, "guard", 'E' } };
    std::vector<std::vector<BoardCell>> exposure_routes { route_cases[0].path };
    assert(support_precedes_frontline({ 5, 0 }, exposure_members, exposure_routes));
    assert(!support_precedes_frontline({ 3, 0 }, exposure_members, exposure_routes));
    assert(!support_precedes_frontline({ 5, 1 }, exposure_members, exposure_routes));
    assert(!support_precedes_frontline({ 5, 0 }, {}, exposure_routes));
    auto unhealable_layout = layout_case;
    unhealable_layout[0].accepts_direct_healing = false;
    const auto ordinary_healing = review_deployment(unhealable_layout, route_cases, legal_cells);
    assert(ordinary_healing.complete && !ordinary_healing.static_constraints_met);
    assert(ordinary_healing.lanes[0].healers.empty() && !ordinary_healing.lanes[1].healers.empty());
    unhealable_layout[4].provides_regeneration = true;
    const auto regeneration = review_deployment(unhealable_layout, route_cases, legal_cells);
    assert(regeneration.static_constraints_met && regeneration.lanes[0].healers.size() == 1);
    auto bad_layout = layout_case;
    bad_layout[0].cell = { 4, 1 }; // A guard below the upper route cannot block it.
    assert(!review_deployment(bad_layout, route_cases, legal_cells).static_constraints_met);
    bad_layout = layout_case;
    bad_layout[0].frontline = false;
    bad_layout[0].healer = true;
    bad_layout[0].range_known = true;
    bad_layout[0].local_range = medic_range;
    assert(!review_deployment(bad_layout, route_cases, legal_cells).static_constraints_met);
    bad_layout = layout_case;
    bad_layout[4].cell = { 1, 1 };
    assert(!review_deployment(bad_layout, route_cases, legal_cells).static_constraints_met);
    bad_layout = layout_case;
    bad_layout[2].facing = 'W';
    assert(!review_deployment(bad_layout, route_cases, legal_cells).static_constraints_met);
    bad_layout = layout_case;
    bad_layout[0].cell = { 2, 0 };
    assert(!review_deployment(bad_layout, route_cases, legal_cells).static_constraints_met);
    bad_layout = layout_case;
    bad_layout[4].range_known = false;
    assert(!review_deployment(bad_layout, route_cases, legal_cells).complete);
    bad_layout = layout_case;
    bad_layout[2].cell = { 5, 0 };
    bad_layout[2].block_count = 1;
    assert(!review_deployment(bad_layout, route_cases, legal_cells).static_constraints_met);
    assert(review_deployment(bad_layout, route_cases, legal_cells).exposed_support_units == 1);
    for (auto& unit : bad_layout) {
        unit.placement_cells = legal_cells;
    }
    const auto protected_caster = propose_deployment(bad_layout, route_cases, legal_cells, 4000, [](const auto& units) {
        return units[2].cell == BoardCell(5, 0) ? 1'000'000 : 0;
    });
    assert(protected_caster.input_complete && protected_caster.improved);
    assert(review_deployment(protected_caster.units, route_cases, legal_cells).exposed_support_units == 0);
    const ResultEvidence normal_result { true,  true, true,  true, 13,   true,    14, true, "enemy_9037_aceel",
                                         false, true, false, true, true, "amiya", 3 };
    const auto valid_result = assess_result(normal_result, 13, "amiya");
    assert(valid_result.valid_victory && valid_result.strategy_win_eligible && !valid_result.hidden_boss_victory);
    auto hidden_result = normal_result;
    hidden_result.boss_id = "enemy_9037_aceel_2";
    hidden_result.hidden_boss_marker = true;
    assert(assess_result(hidden_result, 13, "amiya").hidden_boss_victory);
    auto contradictory_result = normal_result;
    contradictory_result.contradictory = true;
    assert(!assess_result(contradictory_result, 13, "amiya").valid_victory);
    auto no_badge = normal_result;
    no_badge.strategy_report_badge = false;
    assert(!assess_result(no_badge, 13, "amiya").medal_eligible);
    auto interrupted_evidence = normal_result;
    interrupted_evidence.result_completed = false;
    assert(!assess_result(interrupted_evidence, 13, "amiya").valid_victory);
    assert(equipment_profiles().size() == 70);
    const auto toy_profile =
        *std::find_if(equipment_profiles().begin(), equipment_profiles().end(), [](const auto& item) {
            return item.name == "见钱眼开玩偶";
        });
    const auto blade_profile =
        *std::find_if(equipment_profiles().begin(), equipment_profiles().end(), [](const auto& item) {
            return item.name == "阿戈尔重刃";
        });
    EquipmentContext toy_buy;
    toy_buy.funds = 2;
    toy_buy.round = 13;
    toy_buy.boss_round = 14;
    toy_buy.target_deployed = true;
    toy_buy.shop_level = 3;
    assert(equipment_next_round_cash_gain("见钱眼开玩偶", "装备时销毁，下回合额外获得2资金") == 2);
    assert(equipment_next_round_cash_gain("见钱眼开玩偶", "装备时销毁，下回合额外获得4资金") == 4);
    assert(!equipment_next_round_cash_gain("见钱眼开玩偶", "下回合额外获得2资金"));
    assert(!equipment_next_round_cash_gain("见钱眼开玩偶", "装备时销毁，下回合额外获得2资金或下回合额外获得4资金"));
    assert(equipment_next_round_cash_gain("精打细算玩偶", "装备时销毁，之后每回合开始时额外获得1资金") == 1);
    assert(equipment_next_round_cash_gain("精打细算玩偶", "装备时销毁，之后每回合开始时额外获得2资金") == 2);
    assert(!equipment_next_round_cash_gain("精打细算玩偶", "装备时销毁，下回合额外获得2资金"));
    assert(profitable_next_round_cash_offer(toy_buy, toy_profile));
    assert(choose_equipment(toy_buy, { toy_profile }).buy);
    auto expensive_toy = toy_profile;
    expensive_toy.price = 2;
    assert(!choose_equipment(toy_buy, { expensive_toy }).buy);
    toy_buy.round = 14;
    assert(!choose_equipment(toy_buy, { toy_profile }).buy);
    const auto recurring_toy =
        *std::find_if(equipment_profiles().begin(), equipment_profiles().end(), [](const auto& item) {
            return item.name == "精打细算玩偶";
        });
    auto recurring_budget = toy_buy;
    recurring_budget.funds = 3;
    recurring_budget.round = 11;
    assert(!profitable_next_round_cash_offer(recurring_budget, recurring_toy));
    recurring_budget.round = 10;
    assert(profitable_next_round_cash_offer(recurring_budget, recurring_toy));
    toy_buy.round = 8;
    toy_buy.target_is_carry = true;
    toy_buy.primary_active = true;
    assert(choose_equipment(toy_buy, { toy_profile, blade_profile }).name == "阿戈尔重刃");
    EquipmentRecipient equipment_tank;
    equipment_tank.name = "泡泡";
    equipment_tank.slots_known = true;
    equipment_tank.frontline = true;
    equipment_tank.attacks = true;
    equipment_tank.skill_growth_engine = true;
    equipment_tank.active_own_pact = true;
    EquipmentRecipient equipment_sniper;
    equipment_sniper.name = "空弦";
    equipment_sniper.slots_known = true;
    equipment_sniper.attacks = true;
    equipment_sniper.carry = true;
    equipment_sniper.max_range_distance = 3;
    EquipmentRecipient equipment_medic;
    equipment_medic.name = "夜莺";
    equipment_medic.slots_known = true;
    equipment_medic.healer = true;
    EquipmentAssignmentContext assignment_context;
    assignment_context.field_has_healer = true;
    const std::vector<EquipmentRecipient> equipment_team { equipment_sniper, equipment_tank, equipment_medic };
    assert(choose_equipment_assignment(assignment_context, { "乌萨斯呼吸器" }, equipment_team).recipient == "泡泡");
    assert(choose_equipment_assignment(assignment_context, { "护盾无人机" }, equipment_team).recipient == "夜莺");
    assert(choose_equipment_assignment(assignment_context, { "精准狙击镜" }, equipment_team).recipient == "空弦");
    auto egir_output_gear = equipment_sniper;
    egir_output_gear.name = "克莱门莎";
    egir_output_gear.carry = true;
    auto egir_donor_gear = egir_output_gear;
    egir_donor_gear.name = "乌尔比安";
    egir_donor_gear.carry = false;
    assert(
        choose_equipment_assignment(assignment_context, { "阿戈尔重刃" }, { egir_donor_gear, egir_output_gear })
            .recipient == "克莱门莎");
    assert(!choose_equipment_assignment(assignment_context, { "阿戈尔重刃" }, { egir_donor_gear }).found);
    assert(choose_equipment_assignment(assignment_context, { "萨尔贡浓茶" }, equipment_team).recipient == "泡泡");
    assert(!choose_equipment_assignment(assignment_context, { "虹彩活血饮料" }, equipment_team).found);
    assert(!choose_equipment_assignment(assignment_context, { "盟约之币", "信标", "源石溶剂" }, equipment_team).found);
    auto unknown_slots = equipment_sniper;
    unknown_slots.slots_known = false;
    assert(!choose_equipment_assignment(assignment_context, { "维式重锤" }, { unknown_slots }).found);
    auto full_slots = equipment_sniper;
    full_slots.equipped = { "维式重锤", "精准狙击镜" };
    assert(!choose_equipment_assignment(assignment_context, { "加速维式重锤" }, { full_slots }).found);
    assert(equipment_capacity("“暴躁铁皮”", false) == 4);
    assert(equipment_capacity("“暴躁铁皮”", true) == 6);
    assert(equipment_capacity("空弦", true) == 2);
    assert(equipment_capacity("unknown", false) == 0);
    auto mech_equipment = full_slots;
    mech_equipment.name = "“暴躁铁皮”";
    mech_equipment.frontline = true;
    assert(choose_equipment_assignment(assignment_context, { "加速维式重锤" }, { mech_equipment }).found);
    mech_equipment.equipped = { "维式重锤", "精准狙击镜", "M3茧甲", "炎国短刀" };
    assert(!choose_equipment_assignment(assignment_context, { "加速维式重锤" }, { mech_equipment }).found);
    mech_equipment.elite = true;
    assert(choose_equipment_assignment(assignment_context, { "加速维式重锤" }, { mech_equipment }).found);
    mech_equipment.equipped.push_back("乌萨斯呼吸器");
    mech_equipment.equipped.push_back("萨尔贡浓茶");
    assert(!choose_equipment_assignment(assignment_context, { "加速维式重锤" }, { mech_equipment }).found);
    auto same_item = equipment_sniper;
    same_item.equipped = { "维式重锤" };
    assert(!choose_equipment_assignment(assignment_context, { "维式重锤" }, { same_item }).found);
    assert(!choose_equipment_assignment(assignment_context, { "多功能补剂" }, { equipment_sniper }).found);
    assert(!choose_equipment_assignment(assignment_context, { "驱逐者警棍" }, equipment_team).found);
    auto redeploy_tank = equipment_tank;
    redeploy_tank.deliberate_redeploy = true;
    assert(!choose_equipment_assignment(assignment_context, { "M3茧甲" }, { redeploy_tank }).found);
    EquipmentRecipient gravel_equipment;
    gravel_equipment.name = "砾";
    gravel_equipment.slots_known = true;
    gravel_equipment.attacks = true;
    gravel_equipment.active_own_pact = true;
    gravel_equipment.deliberate_redeploy = is_redeployment_growth_engine("砾", { "不屈" }, false);
    assert(choose_equipment_assignment(assignment_context, { "源石溶剂" }, { gravel_equipment }).recipient == "砾");
    assert(choose_equipment_assignment(assignment_context, { "不屈弹射器" }, { gravel_equipment }).recipient == "砾");
    auto wrong_redeploy = gravel_equipment;
    wrong_redeploy.name = "远牙";
    wrong_redeploy.deliberate_redeploy = is_redeployment_growth_engine("远牙", { "卡西米尔", "精准" }, false);
    assert(!choose_equipment_assignment(assignment_context, { "源石溶剂", "不屈弹射器" }, { wrong_redeploy }).found);
    wrong_redeploy = gravel_equipment;
    wrong_redeploy.deliberate_redeploy = is_redeployment_growth_engine("砾", {}, false);
    assert(!choose_equipment_assignment(assignment_context, { "源石溶剂", "不屈弹射器" }, { wrong_redeploy }).found);
    auto boss_gear_context = assignment_context;
    boss_gear_context.growth_battle_available = false;
    assert(!choose_equipment_assignment(boss_gear_context, { "源石溶剂", "不屈弹射器" }, { gravel_equipment }).found);
    assert(EquipmentCatalogue.size() == equipment_profiles().size());
    const auto equipment_fact = [](const std::string& name) -> const EquipmentProfile& {
        const auto found =
            std::find_if(equipment_profiles().begin(), equipment_profiles().end(), [&](const auto& item) {
                return item.name == name;
            });
        assert(found != equipment_profiles().end());
        return *found;
    };
    assert(equipment_fact("精打细算玩偶").consumable);
    assert(equipment_fact("定期存折").consumable);
    assert(equipment_fact("寻呼模块").consumable);
    assert(equipment_fact("博士投影").consumable);
    assert(equipment_fact("信标").consumable);
    assert(!equipment_fact("萨尔贡浓茶").consumable);
    assert(equipment_fact("萨尔贡浓茶").role == EquipmentRole::SkillRecovery);
    assert(equipment_fact("乌萨斯呼吸器").role == EquipmentRole::Defense);
    assert(equipment_fact("多功能补剂").role == EquipmentRole::Growth);
    assert(equipment_fact("多功能补剂").normal_effect.find("临时层数") != std::string::npos);
    assert(equipment_cash_gain("盟约之币", "装备时销毁，并获得1资金") == 1);
    const std::string mech_recipient = "“暴躁铁皮”";
    const std::vector<TextObservation> empty_mech_panel { { mech_recipient, .99, 15, 190 } };
    const auto empty_mech_items = equipment_panel_items(empty_mech_panel, mech_recipient);
    assert(empty_mech_items && empty_mech_items->empty());
    assert(!equipment_panel_items({}, mech_recipient));
    assert(!equipment_panel_items(empty_mech_panel, "泡泡"));
    auto shield_mech_panel = empty_mech_panel;
    shield_mech_panel.push_back({ "坚守盾牌", .99, 107, 520 });
    const auto shield_mech_items = equipment_panel_items(shield_mech_panel, mech_recipient);
    assert(shield_mech_items && *shield_mech_items == std::vector<std::string>({ "坚守盾牌" }));
    shield_mech_panel.push_back({ "维式重锤", .99, 110, 430 });
    shield_mech_panel.push_back({ "阿戈尔重刃", .99, 940, 650 });
    assert(equipment_panel_items(shield_mech_panel, mech_recipient) == shield_mech_items);
    shield_mech_panel[0].score = .3;
    assert(!equipment_panel_items(shield_mech_panel, mech_recipient));
    assert(equipment_cash_gain("盟约之币", "装备时销毁，并获得2资金") == 2);
    assert(equipment_title_matches("盟约之币", "进阶盟约之币"));
    assert(!equipment_title_matches("盟约之币", "未进阶盟约之币"));
    assert(!equipment_title_matches("盟约之币", "盟约之币兑换"));
    assert(!equipment_cash_gain("盟约之币", "获得1资金"));
    assert(!equipment_cash_gain("驮兽储蓄罐", "装备时销毁，并获得1资金"));
    assert(!equipment_cash_gain("盟约之币", "装备时销毁，获得1资金或获得2资金"));
    const auto tier_six = std::find_if(equipment_profiles().begin(), equipment_profiles().end(), [](const auto& item) {
        return item.name == "变形同构体";
    });
    assert(tier_six != equipment_profiles().end() && tier_six->tier == 6);
    const EquipmentContext equipment_context { 3, 7, 14, true, true, true, true, true };
    assert(safe_pact_recruitment_recipient("幽灵鲨", "阿戈尔", true, {}));
    assert(safe_pact_recruitment_recipient("歌蕾蒂娅", "阿戈尔", true, {}));
    assert(!safe_pact_recruitment_recipient("斯卡蒂", "阿戈尔", true, {}));
    assert(!safe_pact_recruitment_recipient("深巡", "阿戈尔", true, {}));
    assert(!safe_pact_recruitment_recipient("幽灵鲨", "阿戈尔", false, {}));
    assert(!safe_pact_recruitment_recipient("幽灵鲨", "阿戈尔", true, { "维式重锤" }));
    assert(!safe_pact_recruitment_recipient("幽灵鲨", "", true, {}));
    assert(!safe_pact_recruitment_recipient("幽灵鲨", "炎", true, {}));
    assert(pact_recruitment_offer_eligible("幽灵鲨", "阿戈尔", 1));
    assert(pact_recruitment_offer_eligible("斯卡蒂", "阿戈尔", 3));
    assert(!pact_recruitment_offer_eligible("斯卡蒂", "阿戈尔", 2));
    assert(!pact_recruitment_offer_eligible("克莱门莎", "阿戈尔", 5));
    assert(pact_recruitment_offer_eligible("克莱门莎", "阿戈尔", 6));
    assert(pact_recruitment_offer_eligible("深巡", "坚守", 3));
    assert(!pact_recruitment_offer_eligible("艾丝黛尔", "阿戈尔", 6));
    assert(!pact_recruitment_offer_eligible("不存在的干员", "阿戈尔", 6));
    assert(!pact_recruitment_offer_eligible("幽灵鲨", "阿戈尔", 0));
    auto free_reward_context = equipment_context;
    free_reward_context.funds = 0;
    free_reward_context.free_supply = true;
    const std::vector<EquipmentProfile> free_rewards { { "动能单手弩", 0, EquipmentRole::Damage, false, false, 24, 4 },
                                                       { "盟约之币", 0, EquipmentRole::Economy, true, false, 22, 1 } };
    assert(choose_equipment(free_reward_context, free_rewards).buy);
    const auto supply_fixture = json::open(std::filesystem::current_path() / "spa-supply-duplicate-fixture.json");
    assert(supply_fixture);
    std::vector<TextObservation> supply_texts;
    for (const auto& item : supply_fixture->at("texts").as_array()) {
        supply_texts.push_back(
            { item.at("text").as_string(),
              item.at("score").as_double(),
              item.at("x").as_integer(),
              item.at("y").as_integer() });
    }
    assert(observe_page(supply_texts, identities).page == Page::EquipmentSupply);
    const auto real_supply_cards = observe_tactical_offers(supply_texts);
    // Joint-risk R9 capture: identical titles occupy separate rows/columns.
    std::vector<TextObservation> joint_supply_texts {
        { "当前轮到你决策", .967, 829, 31 }, { "维式重锤", .999, 284, 137 },   { "维式重锤", .999, 625, 137 },
        { "乌萨斯呼吸器", .999, 969, 138 },  { "坚守盾牌", .999, 282, 435 },   { "坚守盾牌", .999, 625, 435 },
        { "炎国短刀", .999, 968, 435 },      { "攻击力+15%", .999, 184, 209 }, { "防御力+20%", .999, 182, 506 }
    };
    assert(supply_own_turn(joint_supply_texts) == true);
    const auto joint_supply_cards = observe_supply_offers(joint_supply_texts);
    assert(joint_supply_cards.size() == 6);
    assert(joint_supply_cards[0].title == "维式重锤" && joint_supply_cards[0].slot == 0);
    assert(joint_supply_cards[3].title == "坚守盾牌" && joint_supply_cards[3].slot == 3);
    assert(joint_supply_cards[4].title == "坚守盾牌" && joint_supply_cards[4].slot == 4);
    assert(joint_supply_cards[5].title == "炎国短刀" && joint_supply_cards[5].slot == 5);
    assert(joint_supply_cards[3].description == "防御力+20%");
    joint_supply_texts[0] = { "正在决策…", .999, 868, 45 };
    assert(supply_own_turn(joint_supply_texts) == false);
    joint_supply_texts.push_back({ "当前轮到你决策", .999, 829, 31 });
    assert(!supply_own_turn(joint_supply_texts));
    assert(!supply_own_turn(supply_texts));
    assert(observe_supply_offers(supply_texts).size() == 3);
    assert(supply_preparation_restored(true, Page::Preparation, 4, 11));
    assert(!supply_preparation_restored(false, Page::Preparation, 4, 11));
    assert(supply_preparation_restored(false, Page::Preparation, 4, 4));
    assert(!supply_preparation_restored(true, Page::EquipmentSupply, 4, 4));
    assert(!supply_preparation_restored(true, Page::Preparation, 4, std::nullopt));
    assert(
        real_supply_cards.size() == 3 && real_supply_cards[0].title == "汲取者之心" &&
        real_supply_cards[1].title == "变形同构体" && real_supply_cards[2].title == "变形同构体");
    std::vector<EquipmentProfile> duplicate_supply_offers;
    for (const auto& card : real_supply_cards) {
        const auto found =
            std::find_if(equipment_profiles().begin(), equipment_profiles().end(), [&](const auto& item) {
                return item.name == card.title;
            });
        assert(found != equipment_profiles().end());
        auto item = *found;
        item.price = 0;
        duplicate_supply_offers.push_back(std::move(item));
    }
    const auto real_supply_choice = choose_equipment(free_reward_context, duplicate_supply_offers);
    assert(real_supply_choice.buy && real_supply_choice.name == "汲取者之心");
    assert(equipment_supply_slot(284) == 0 && equipment_supply_slot(628) == 1 && equipment_supply_slot(969) == 2);
    assert(!equipment_supply_slot(100) && !equipment_supply_slot(1150));
    auto unusable_free_context = free_reward_context;
    unusable_free_context.target_deployed = false;
    unusable_free_context.healer_covers = false;
    unusable_free_context.shop_level = 1;
    const auto free_tradeoff =
        choose_equipment(unusable_free_context, { { "源石溶剂", 0, EquipmentRole::Tradeoff, false, true, 36, 6 } });
    assert(free_tradeoff.buy && free_tradeoff.score < 0);
    const auto repeated_free = choose_equipment(
        unusable_free_context,
        { duplicate_supply_offers[1], duplicate_supply_offers[1], duplicate_supply_offers[1] });
    assert(repeated_free.buy && repeated_free.name == "变形同构体");
    free_reward_context.free_supply = false;
    assert(!choose_equipment(free_reward_context, free_rewards).buy);
    const auto equipment = choose_equipment(
        equipment_context,
        { { "源石溶剂", 1, EquipmentRole::Tradeoff, false, true, 36 },
          { "随身身份牌", 1, EquipmentRole::Growth, true, false, 42 },
          { "坚守盾牌", 1, EquipmentRole::Defense, false, false, 30 } });
    assert(equipment.buy && equipment.name == "随身身份牌");
    auto unsafe = equipment_context;
    unsafe.healer_covers = false;
    assert(choose_equipment(unsafe, { { "源石溶剂", 1, EquipmentRole::Tradeoff, false, true, 36 } }).buy == false);
    auto inactive = equipment_context;
    inactive.primary_active = false;
    assert(choose_equipment(inactive, { { "随身身份牌", 1, EquipmentRole::Growth, true, false, 42 } }).buy);
    auto no_window = equipment_context;
    no_window.round = 14;
    assert(choose_equipment(no_window, { { "维式重锤", 1, EquipmentRole::Damage, false, false, 30 } }).buy);
    assert(!choose_equipment(no_window, { { "定期存折", 2, EquipmentRole::Economy, true, false, 20 } }).buy);
    auto bank_before_boss = equipment_context;
    bank_before_boss.round = 13;
    bank_before_boss.funds = 1;
    assert(choose_equipment(bank_before_boss, { { "盟约之币", 1, EquipmentRole::Economy, true, false, 22 } }).buy);
    bank_before_boss.round = 14;
    assert(!choose_equipment(bank_before_boss, { { "盟约之币", 1, EquipmentRole::Economy, true, false, 22 } }).buy);
    assert(
        choose_equipment(
            equipment_context,
            { { "价格低", 1, EquipmentRole::Damage, false, false, 30 },
              { "价格高", 2, EquipmentRole::Damage, false, false, 30 } })
            .name == "价格低");
    assert(skill_profiles().size() == 6);
    assert(!choose_skill({ "卡达", "同步索敌", 30, 0, 0, true, false, true }, true, 2).issue);
    const auto hold = choose_skill({ "乌啾", "捉迷藏", 25, 0, 0, true, true, true }, true, 1);
    assert(!hold.issue);
    const auto click = choose_skill({ "卡达", "同步索敌", 30, 0, 0, true, false, true }, true, 2);
    assert(!click.issue && !click.automatic_trigger_expected);
    SkillState automatic_ready { "卡达", "同步索敌", 30, 0, 0, true, false, true, true, true };
    const auto automatic = choose_skill(automatic_ready, true, 2);
    assert(!automatic.issue && automatic.automatic_trigger_expected);
    automatic_ready.automatic_condition_met = false;
    assert(!choose_skill(automatic_ready, true, 2).automatic_trigger_expected);
    const auto automatic_non_growth =
        choose_skill({ "乌啾", "捉迷藏", 25, 0, 0, true, true, true, true, true }, true, 1);
    assert(!automatic_non_growth.issue && automatic_non_growth.automatic_trigger_expected);
    const auto heal = choose_skill({ "乌啾", "捉迷藏", 25, 0, 0, true, false, true }, false, 1);
    assert(!heal.issue);
    const auto cooldown = choose_skill({ "卡达", "同步索敌", 30, 1, 0, true, false, true }, true, 3);
    assert(!cooldown.issue);
    const auto cap = choose_skill({ "卡达", "同步索敌", 30, 0, 2, true, false, true }, true, 3);
    assert(!cap.issue);
    assert(growth_profiles().size() == 74);
    for (const auto& [name, profile] : growth_profiles()) {
        assert(name == profile.name && catalog.contains(name));
        for (const auto& pact : profile.pacts) {
            assert(std::any_of(requirements.begin(), requirements.end(), [&](const auto& item) {
                return item.name == pact;
            }));
        }
        if (profile.kind == GrowthKind::FixedPhase || profile.kind == GrowthKind::CappedConditional) {
            assert(profile.normal_gain > 0 && profile.elite_gain >= profile.normal_gain);
        }
        if (profile.kind == GrowthKind::CappedConditional) {
            assert(profile.normal_cap >= profile.normal_gain && profile.elite_cap >= profile.elite_gain);
        }
    }

    assert(is_primary_growth_engine("圣聆初雪", "谢拉格"));
    assert(is_primary_growth_engine("初雪", "谢拉格"));
    assert(!is_primary_growth_engine("凛御银灰", "谢拉格"));
    // Conversion hands are valid live route signals even though they are
    // accounted separately from fixed/capped projections.
    assert(is_primary_growth_engine("折桠", "乌萨斯"));
    assert(!is_primary_growth_engine("雪猎", "谢拉格"));
    assert(is_primary_growth_engine("佩佩", "萨尔贡"));
    assert(!is_primary_growth_engine("佩佩", "炎"));
    assert(is_primary_growth_engine("复奏", "莱塔尼亚"));
    assert(is_primary_growth_engine("涤火杰西卡", "维多利亚"));
    assert(is_primary_growth_engine("罗比菈塔", "助力"));
    assert(!is_primary_growth_engine("罗比菈塔", "维多利亚"));
    assert(growth_profiles().at("罗比菈塔").phase == GrowthPhase::PreparationEnd);
    assert(growth_profiles().at("涤火杰西卡").normal_gain == 2);
    assert(is_front_growth_target("塞雷娅", "罗比菈塔", "坚守"));
    // A printed per-member gain is a real engine signal, but without the
    // observed eligible population it must not invent a fixed layer yield.
    const auto population_unknown = project_primary_growth("维多利亚", 7, 14, true, { { "涤火杰西卡", false } });
    assert(
        population_unknown.unmodeled_sources == 1 && population_unknown.fixed_phase_additions == 0 &&
        population_unknown.conditional_preparation_cap_additions == 0);
    const auto equipped_population =
        project_primary_growth("维多利亚", 7, 14, true, { { "涤火杰西卡", false, true, 3 } });
    assert(
        equipped_population.population_phase_additions_if_unchanged == 48 &&
        equipped_population.fixed_phase_additions == 0 && equipped_population.unmodeled_sources == 0);
    assert(
        project_primary_growth("维多利亚", 7, 14, true, { { "涤火杰西卡", true, true, 3 } })
            .population_phase_additions_if_unchanged == 96);
    assert(
        project_primary_growth("维多利亚", 7, 14, true, { { "涤火杰西卡", false, true, 0 } })
            .population_phase_additions_if_unchanged == 0);
    assert(
        project_primary_growth("维多利亚", 7, 14, false, { { "涤火杰西卡", true, true, 3 } })
            .population_phase_additions_if_unchanged == 0);
    assert(
        project_primary_growth("维多利亚", 7, 14, true, { { "涤火杰西卡", true, false, 3 } })
            .population_phase_additions_if_unchanged == 0);
    assert(
        project_primary_growth("坚守", 7, 14, true, { { "罗比菈塔", false, true, row_populations->at("罗比菈塔") } })
            .population_phase_additions_if_unchanged == 80);
    assert(!project_primary_growth("坚守", 7, 14, true, { { "罗比菈塔", false, true, -1 } }).valid);
    assert(is_redeployment_growth_engine("砾", { "卡西米尔" }, false));
    assert(is_redeployment_growth_engine("砾", { "不屈" }, true));
    assert(is_redeployment_growth_engine("瑕光", { "卡西米尔" }, false));
    assert(!is_redeployment_growth_engine("幽灵鲨", { "阿戈尔" }, false));
    assert(!is_redeployment_growth_engine("远牙", { "卡西米尔", "精准" }, true));
    assert(!is_redeployment_growth_engine("砾", { "炎" }, false));
    assert(is_primary_growth_engine("风絮", "炎"));
    assert(is_primary_growth_engine("夕", "炎"));
    assert(is_primary_growth_engine("安洁莉娜", "叙拉古"));
    const auto purchased_growth = project_primary_growth("炎", 13, 14, true, { { "风絮", false }, { "夕", true } });
    assert(purchased_growth.fixed_phase_additions == 0 && purchased_growth.conditional_cap_additions == 0);
    assert(purchased_growth.conditional_preparation_cap_additions == (16 + 48) * 2);
    assert(
        project_primary_growth("叙拉古", 13, 14, true, { { "安洁莉娜", false } })
            .conditional_preparation_cap_additions == 16);
    assert(
        project_primary_growth("奇迹", 13, 14, true, { { "安洁莉娜", false } }).conditional_preparation_cap_additions ==
        8);
    assert(
        project_primary_growth("奇迹", 13, 14, true, { { "安洁莉娜", true } }).conditional_preparation_cap_additions ==
        16);
    assert(project_primary_growth("远见", 13, 14, true, { { "空弦", false } }).fixed_phase_additions == 3);
    assert(project_primary_growth("远见", 13, 14, true, { { "渡桥", false } }).fixed_phase_additions == 4);
    assert(project_primary_growth("远见", 13, 14, true, { { "寒檀", false } }).fixed_phase_additions == 9);
    assert(project_primary_growth("远见", 14, 14, true, { { "空弦", false } }).fixed_phase_additions == 0);
    assert(project_primary_growth("远见", 13, 14, true, { { "空弦", false, false } }).fixed_phase_additions == 0);
    assert(is_primary_growth_engine("旅骨", "伊比利亚"));
    assert(is_primary_growth_engine("旅骨", "精准"));
    assert(is_primary_growth_engine("棘刺", "伊比利亚"));
    assert(is_primary_output_candidate("蓝毒", "伊比利亚"));
    assert(is_front_growth_target("旅骨", "初雪", "谢拉格"));
    const auto sale_growth = project_primary_growth("伊比利亚", 13, 14, true, { { "棘刺", true } });
    assert(sale_growth.sale_price_sources == 1 && sale_growth.fixed_phase_additions == 0);
    assert(sale_growth.conditional_cap_additions == 0 && sale_growth.conditional_preparation_cap_additions == 48);
    const auto front_sale_growth = project_primary_growth("伊比利亚", 7, 14, true, { { "旅骨", false } });
    assert(front_sale_growth.sale_price_sources == 1 && front_sale_growth.unmodeled_sources == 1);
    assert(
        front_sale_growth.fixed_phase_additions == 0 && front_sale_growth.conditional_preparation_cap_additions == 0);
    assert(
        project_primary_growth("伊比利亚", 13, 14, false, { { "棘刺", true } }).conditional_preparation_cap_additions ==
        0);
    assert(project_primary_growth("伊比利亚", 13, 14, true, { { "棘刺", true, false } }).sale_price_sources == 0);
    const auto bench_refresh_growth = project_primary_growth("莱塔尼亚", 8, 14, true, { { "复奏", false, false } });
    assert(
        bench_refresh_growth.conditional_cap_additions == 0 &&
        bench_refresh_growth.conditional_preparation_cap_additions == 14);
    assert(
        project_primary_growth("莱塔尼亚", 8, 14, false, { { "复奏", true, false } })
            .conditional_preparation_cap_additions == 0);
    assert(project_primary_growth("莱塔尼亚", 8, 14, true, { { "折光", false, false } }).fixed_phase_additions == 0);
    assert(project_primary_growth("莱塔尼亚", 8, 14, true, { { "星源", false, false } }).unmodeled_sources == 1);
    const auto pepe_growth = project_primary_growth("萨尔贡", 8, 14, true, { { "佩佩", false } });
    assert(pepe_growth.conditional_cap_additions == 54 && pepe_growth.unmodeled_sources == 1);
    assert(project_primary_growth("萨尔贡", 8, 14, false, { { "佩佩", true } }).conditional_cap_additions == 0);
    const auto harmony_roles = assess_primary_roles("萨尔贡", { "魔王" }, catalog);
    assert(harmony_roles.growth_engine_owned == 1 && harmony_roles.output_owned == 0);
    const auto fixed_growth = project_primary_growth("维多利亚", 7, 14, true, { { "慕斯", true } });
    assert(fixed_growth.valid && fixed_growth.preparation_ends == 8 && fixed_growth.fixed_phase_additions == 16);
    const auto freezing_growth = project_primary_growth("谢拉格", 7, 14, true, { { "圣聆初雪", true } });
    assert(freezing_growth.fixed_phase_additions == 0 && freezing_growth.conditional_cap_additions == 336);
    assert(project_primary_growth("谢拉格", 7, 14, false, { { "圣聆初雪", true } }).conditional_cap_additions == 0);
    const auto conversion_growth =
        project_primary_growth("乌萨斯", 7, 14, true, { { "折桠", true }, { "烈夏", true } });
    assert(
        conversion_growth.fixed_phase_additions == 0 && conversion_growth.conversion_sources == 1 &&
        conversion_growth.temporary_sources == 1);
    assert(!project_primary_growth("谢拉格", 15, 14, true, {}).valid);
    assert(!project_primary_growth("谢拉格", 7, 14, true, { { "圣聆初雪", false }, { "圣聆初雪", true } }).valid);
    auto search_layout = layout_case;
    for (auto& unit : search_layout) {
        unit.placement_cells = legal_cells;
    }
    search_layout[0].cell = { 2, 0 };
    search_layout[1].cell = { 2, 3 };
    const auto proposal = propose_deployment(search_layout, route_cases, legal_cells);
    assert(proposal.input_complete && proposal.found && proposal.evaluations > 1);
    assert(proposal.improved && proposal.score_improvement > 0);
    auto no_healer_layout = search_layout;
    std::erase_if(no_healer_layout, [](const auto& unit) { return unit.healer; });
    const auto partial_layout_proposal = propose_deployment(no_healer_layout, route_cases, legal_cells);
    assert(
        partial_layout_proposal.input_complete && partial_layout_proposal.improved && !partial_layout_proposal.found &&
        partial_layout_proposal.score_improvement > 0);
    auto fixed_layout = search_layout;
    for (auto& unit : fixed_layout) {
        unit.movable = false;
    }
    const auto fixed_proposal = propose_deployment(fixed_layout, route_cases, legal_cells);
    assert(fixed_proposal.input_complete && !fixed_proposal.improved);
    auto unsupported_guard = layout_case.front();
    unsupported_guard.cell = { 4, 1 };
    unsupported_guard.placement_cells = { { 4, 1 }, { 4, 0 } };
    const auto early_blocker = propose_deployment({ unsupported_guard }, { route_cases.front() }, legal_cells);
    assert(early_blocker.input_complete && early_blocker.improved && !early_blocker.found);
    assert(early_blocker.unblocked_lanes_improvement == 1 && early_blocker.units.front().cell == BoardCell(4, 0));
    // Missing both healer and ranged support is no reason to leave the
    // enemy road open, even for a very large spatial preference elsewhere.
    const auto keep_road =
        propose_deployment(early_blocker.units, { route_cases.front() }, legal_cells, 4000, [](const auto& units) {
            return units.front().cell == BoardCell(4, 1) ? 1'000'000 : 0;
        });
    assert(keep_road.input_complete && !keep_road.improved && keep_road.units.front().cell == BoardCell(4, 0));
    assert(review_deployment(proposal.units, route_cases, legal_cells).static_constraints_met);
    const auto combat_first =
        propose_deployment(search_layout, route_cases, legal_cells, 20'000, [](const auto& units) {
            return units.front().cell == BoardCell { 2, 0 } ? 100'000 : 0;
        });
    assert(combat_first.input_complete && combat_first.found && combat_first.score_improvement > 0);
    auto trait_only_layout = proposal.units;
    const auto spare_cell = std::find_if(legal_cells.begin(), legal_cells.end(), [&](const auto& cell) {
        return std::none_of(trait_only_layout.begin(), trait_only_layout.end(), [&](const auto& unit) {
            return unit.cell == cell;
        });
    });
    assert(spare_cell != legal_cells.end());
    DeploymentUnit helper_turn;
    helper_turn.id = "helper-turn";
    helper_turn.cell = *spare_cell;
    helper_turn.placement_cells = { *spare_cell };
    trait_only_layout.push_back(helper_turn);
    const auto trait_only =
        propose_deployment(trait_only_layout, route_cases, legal_cells, 4000, [](const auto& units) {
            return units.back().facing == 'N' ? 600 : 0;
        });
    assert(
        trait_only.found && trait_only.improved && trait_only.score_improvement == 0 &&
        trait_only.preference_improvement == 600);
    const auto trait_sequence = sequence_deployment(trait_only_layout, trait_only.units);
    assert(deployment_change_worth_input(trait_only, trait_sequence));
    DeploymentSequence marginal_move;
    marginal_move.complete = true;
    marginal_move.steps = { { DeploymentStepKind::Move, "ranged", "", { 3, 1 }, { 4, 2 }, 'S' },
                            { DeploymentStepKind::Face, "ranged", "", { 4, 2 }, { 4, 2 }, 'S' } };
    assert(deployment_preference_input_cost(marginal_move) == 250);
    auto marginal_proposal = trait_only;
    marginal_proposal.preference_improvement = 200;
    assert(!deployment_change_worth_input(marginal_proposal, marginal_move));
    marginal_proposal.preference_improvement = 100;
    assert(!deployment_change_worth_input(marginal_proposal, marginal_move));
    marginal_proposal.preference_improvement = 600;
    assert(deployment_change_worth_input(marginal_proposal, marginal_move));
    marginal_move.steps.front().kind = DeploymentStepKind::Swap;
    marginal_move.steps.front().displaced = "healer";
    marginal_move.steps.push_back({ DeploymentStepKind::Face, "healer", "", { 3, 1 }, { 3, 1 }, 'S' });
    assert(deployment_preference_input_cost(marginal_move) == 350);
    marginal_proposal.preference_improvement = -5000;
    marginal_proposal.score_improvement = 1000;
    assert(deployment_change_worth_input(marginal_proposal, marginal_move));
    marginal_proposal.score_improvement = 0;
    marginal_proposal.unblocked_lanes_improvement = 1;
    assert(deployment_change_worth_input(marginal_proposal, marginal_move));
    marginal_move.complete = false;
    assert(!deployment_change_worth_input(marginal_proposal, marginal_move));
    assert(!propose_deployment(layout_case, route_cases, legal_cells).input_complete);
    const auto limited = propose_deployment(search_layout, route_cases, legal_cells, 1);
    assert(limited.input_complete && !limited.found && limited.budget_exhausted);
    for (auto& unit : search_layout) {
        unit.placement_cells = { unit.cell };
    }
    const auto immobile = propose_deployment(search_layout, route_cases, legal_cells);
    assert(immobile.input_complete && !immobile.found);
    auto moving = layout_case;
    for (auto& unit : moving) {
        unit.placement_cells = legal_cells;
    }
    auto destinations = moving;
    destinations[0].cell = moving[1].cell;
    destinations[1].cell = moving[2].cell;
    destinations[2].cell = moving[0].cell;
    destinations[3].facing = 'N';
    const auto sequence = sequence_deployment(moving, destinations);
    assert(sequence.complete && !sequence.steps.empty());
    auto protected_moving = moving;
    protected_moving.front().movable = false;
    assert(!sequence_deployment(protected_moving, destinations).complete);
    auto protected_facing = protected_moving;
    protected_facing.front().facing = 'N';
    assert(!sequence_deployment(protected_moving, protected_facing).complete);
    auto simulated = moving;
    for (const auto& step : sequence.steps) {
        auto unit =
            std::find_if(simulated.begin(), simulated.end(), [&](const auto& item) { return item.id == step.unit; });
        assert(unit != simulated.end() && unit->cell == step.from);
        if (step.kind == DeploymentStepKind::Swap) {
            auto other = std::find_if(simulated.begin(), simulated.end(), [&](const auto& item) {
                return item.id == step.displaced;
            });
            assert(other != simulated.end() && other->cell == step.to);
            other->cell = step.from;
        }
        unit->cell = step.to;
        if (step.kind == DeploymentStepKind::Face) {
            unit->facing = step.facing;
        }
        for (const auto& item : simulated) {
            assert(
                std::find(item.placement_cells.begin(), item.placement_cells.end(), item.cell) !=
                item.placement_cells.end());
            assert(std::count_if(simulated.begin(), simulated.end(), [&](const auto& other) {
                       return other.cell == item.cell;
                   }) == 1);
        }
    }
    for (size_t index = 0; index < simulated.size(); ++index) {
        assert(
            simulated[index].cell == destinations[index].cell && simulated[index].facing == destinations[index].facing);
    }
    assert(!sequence_deployment(moving, destinations, 1).complete);
    auto incomplete_moving = moving;
    incomplete_moving[0].placement_cells.clear();
    assert(!sequence_deployment(incomplete_moving, destinations).complete);
    std::vector<DeploymentUnit> buffer_case {
        { "A", { 0, 0 }, 'E', 0, false, false, false, false, {}, { { 0, 0 }, { 1, 0 } } },
        { "B", { 1, 0 }, 'E', 0, false, false, false, false, {}, { { 1, 0 }, { 2, 0 } } }
    };
    auto buffer_goals = buffer_case;
    buffer_goals[0].cell = { 1, 0 };
    buffer_goals[1].cell = { 2, 0 };
    const auto buffer_sequence = sequence_deployment(buffer_case, buffer_goals);
    assert(buffer_sequence.complete && buffer_sequence.steps[0].unit == "B");
    buffer_case[1].placement_cells = { { 1, 0 } };
    assert(!sequence_deployment(buffer_case, buffer_goals).complete);
    assert(!terrain_layout_model("unknown-map"));
    assert(!terrain_allows_deployment("m05", { 8, 1 }, true));
    assert(terrain_allows_deployment("m05", { 8, 1 }, true, true));
    assert(terrain_allows_deployment("m06", { 7, 1 }, true));
    assert(!terrain_allows_deployment("m05", { -1, 1 }, false, true));
    for (const auto& terrain : BossBoardTerrains) {
        const auto model = terrain_layout_model(std::string(terrain.id), true);
        assert(model && !model->lanes.empty());
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 11; ++col) {
                if (terrain.rows[row][col] == 'R') {
                    assert(!terrain_allows_deployment(std::string(terrain.id), { col, row }, true, true));
                    assert(terrain_allows_deployment(std::string(terrain.id), { col, row }, false, true));
                }
            }
        }
    }
    for (const auto& terrain : BoardTerrains) {
        const auto model = terrain_layout_model(std::string(terrain.id));
        assert(model && !model->lanes.empty() && !model->ground_route.empty());
        for (const auto& cell : model->ground_route) {
            const char tile = terrain.rows[cell.second][cell.first];
            assert(tile == 'G' || tile == 'M');
            assert(std::find(model->buildable.begin(), model->buildable.end(), cell) != model->buildable.end());
        }
        for (const auto& lane : model->lanes) {
            assert(terrain.rows[lane.path.front().second][lane.path.front().first] == 'S');
            assert(terrain.rows[lane.path.back().second][lane.path.back().first] == 'E');
            for (std::size_t index = 1; index < lane.path.size(); ++index) {
                assert(
                    std::abs(lane.path[index].first - lane.path[index - 1].first) +
                        std::abs(lane.path[index].second - lane.path[index - 1].second) ==
                    1);
                const char tile = terrain.rows[lane.path[index].second][lane.path[index].first];
                assert(tile != 'F' && tile != 'R' && tile != 'X' && tile != ' ');
            }
        }
    }
    const auto mire_terrain = terrain_layout_model("m06");
    const auto crate_terrain = terrain_layout_model("m04");
    assert(crate_terrain);
    for (const auto& cell : { BoardCell(3, 1), BoardCell(4, 1), BoardCell(8, 1) }) {
        assert(
            std::find(crate_terrain->buildable.begin(), crate_terrain->buildable.end(), cell) ==
            crate_terrain->buildable.end());
        assert(
            std::find(crate_terrain->ground_route.begin(), crate_terrain->ground_route.end(), cell) ==
            crate_terrain->ground_route.end());
    }
    assert(
        std::find(mire_terrain->ground_route.begin(), mire_terrain->ground_route.end(), BoardCell { 7, 1 }) !=
        mire_terrain->ground_route.end());
    const auto guards = evaluate_formation(
        { { "角峰", true, {} }, { "摆渡人", true, {} }, { "车尔尼", true, {} } },
        catalog,
        requirements,
        { "坚守" });
    assert(guards.complete && guards.advanced_pacts == std::vector<std::string> { "坚守" });
    const auto two_solo_copies =
        evaluate_formation({ { "艾丝黛尔", true, {} }, { "艾丝黛尔", true, {} } }, catalog, requirements, { "独行" });
    assert(two_solo_copies.complete && two_solo_copies.active_pacts.empty());
    const auto harmony = evaluate_formation(
        { { "慕斯", true, {} }, { "Miss.Christine", true, {} }, { "魔王", true, {} } },
        catalog,
        requirements,
        { "维多利亚", "调和" });
    assert(harmony.complete && harmony.distinct_members.at("维多利亚") == 2);
    assert(harmony.activation_members.at("维多利亚") == 3 && harmony.advanced_pacts.empty());
    assert(
        std::find(harmony.active_pacts.begin(), harmony.active_pacts.end(), "维多利亚") != harmony.active_pacts.end());
    const auto high_book_auto = choose_formation_candidates(
        "auto",
        { "浊心斯卡蒂", "乌尔比安", "克莱门莎", "慕斯", "水灯心", "涤火杰西卡" },
        catalog,
        requirements);
    // Both factions have a valid activation set. A newly modeled engine may
    // change their ranking; preserve the functional contract, not an opening
    // faction preference that permanently pins this shared card pool.
    assert(high_book_auto.valid && high_book_auto.operators.size() == 3);
    assert(assess_primary_roles(high_book_auto.core_pact, high_book_auto.operators, catalog).complete());
    std::vector<RosterUnit> chosen_high_book;
    for (const auto& name : high_book_auto.operators) {
        chosen_high_book.push_back({ name, true, {} });
    }
    const auto high_book_formation =
        evaluate_formation(chosen_high_book, catalog, requirements, { high_book_auto.core_pact });
    assert(
        std::find(
            high_book_formation.active_pacts.begin(),
            high_book_formation.active_pacts.end(),
            high_book_auto.core_pact) != high_book_formation.active_pacts.end());
    // An opening Argo preference must yield to the live 3/3 card density when
    // Argo has no activation set but Victoria does.
    const auto live_pivot =
        choose_formation_candidates("auto", { "乌尔比安", "慕斯", "水灯心", "涤火杰西卡" }, catalog, requirements);
    assert(live_pivot.valid && live_pivot.core_pact == "维多利亚");
    const auto hard_argo =
        choose_formation_candidates("阿戈尔", { "乌尔比安", "慕斯", "水灯心", "涤火杰西卡" }, catalog, requirements);
    assert(!hard_argo.valid);
    const auto all_elites = evaluate_formation(
        { { "角峰", true, {}, true }, { "慕斯", true, {}, true }, { "灰喉", false, {}, true } },
        catalog,
        requirements,
        { "绝技" });
    assert(all_elites.complete && all_elites.activation_members.at("绝技") == 2 && all_elites.active_pacts.size() == 1);
    const auto available_pacts = enabled_formation_pacts({ "维多利亚", "炎" }, { "炎", "奇迹" });
    assert(std::find(available_pacts.begin(), available_pacts.end(), "调和") != available_pacts.end());
    assert(std::find(available_pacts.begin(), available_pacts.end(), "炎") == available_pacts.end());
    assert(std::find(available_pacts.begin(), available_pacts.end(), "奇迹") == available_pacts.end());
    assert(std::find(available_pacts.begin(), available_pacts.end(), "阿戈尔") == available_pacts.end());
    const std::vector<RosterUnit> supported_core { { "慕斯", true, {}, false },
                                                   { "Miss.Christine", true, {}, false },
                                                   { "魔王", true, {}, false },
                                                   { "渡桥", false, {}, false },
                                                   { "空弦", false, {}, false } };
    const auto supported_activation = evaluate_formation(supported_core, catalog, requirements, available_pacts);
    assert(supported_activation.complete && supported_activation.activation_members.at("维多利亚") == 3);
    assert(
        std::find(supported_activation.active_pacts.begin(), supported_activation.active_pacts.end(), "远见") !=
        supported_activation.active_pacts.end());
    const auto no_harmony_activation =
        evaluate_formation(supported_core, catalog, requirements, enabled_formation_pacts({ "维多利亚" }, { "调和" }));
    assert(no_harmony_activation.activation_members.at("维多利亚") == 2);
    assert(
        std::find(no_harmony_activation.active_pacts.begin(), no_harmony_activation.active_pacts.end(), "维多利亚") ==
        no_harmony_activation.active_pacts.end());
    assert(!evaluate_formation({ { "角峰", true, {} } }, catalog, requirements, { "绝技" }).complete);
    auto formation = evaluate_formation(roster, catalog, requirements, enabled);
    assert(formation.complete && formation.active_pacts.size() == 2 && formation.amiya_bonus_percent == 0);
    assert(formation.distinct_members.at("迅捷") == 1);
    roster.push_back({ "慕斯", true, {} });
    assert(evaluate_formation(roster, catalog, requirements, enabled).distinct_members.at("维多利亚") == 2);
    roster.back() = { "陨星", true, {} };
    formation = evaluate_formation(roster, catalog, requirements, enabled);
    assert(formation.active_pacts.size() == 3 && formation.amiya_bonus_percent == 20);
    roster[1] = { "凯瑟琳", true, {} };
    formation = evaluate_formation(roster, catalog, requirements, enabled);
    assert(roster.size() == 8 && formation.active_pacts.size() == 4 && formation.amiya_bonus_percent == 30);
    auto with_vision = enabled;
    with_vision.push_back("远见");
    roster.push_back({ "寒檀", false, {} });
    formation = evaluate_formation(roster, catalog, requirements, with_vision);
    assert(formation.active_pacts.size() == 5 && formation.amiya_bonus_percent == 40);
    assert(formation.distinct_members.at("远见") == 2);
    roster.push_back({ "unknown-custom-unit", true, {} });
    assert(!evaluate_formation(roster, catalog, requirements, enabled).amiya_bonus_percent);
    assert(!evaluate_formation(roster, catalog, requirements, {}).amiya_bonus_percent);
    auto incomplete_activation = evaluate_formation(
        { { "角峰", true, {} }, { "unknown-custom-unit", true, {} } },
        catalog,
        requirements,
        { "坚守" });
    assert(!incomplete_activation.complete && incomplete_activation.active_pacts.empty());
    PromotionWindow promotion { 2, 8, 2, 4, true, false };
    auto promotion_plan = plan_promotion(promotion);
    assert(
        promotion_plan && promotion_plan->order == PromotionOrder::UpgradeThenBuyThird &&
        promotion_plan->reward_tier == 4);
    promotion.immediate_promotion_required = true;
    assert(plan_promotion(promotion)->order == PromotionOrder::BuyThird);
    promotion.immediate_promotion_required = false;
    promotion.upgrade_preserves_target_verified = false;
    assert(plan_promotion(promotion)->order == PromotionOrder::BuyThird);
    promotion.upgrade_preserves_target_verified = true;
    promotion.funds = 5;
    assert(plan_promotion(promotion)->reward_tier == 3);
    promotion.funds = 8;
    promotion.shop_level = 5;
    assert(plan_promotion(promotion)->order == PromotionOrder::BuyThird && plan_promotion(promotion)->reward_tier == 6);
    promotion.shop_level = 0;
    assert(!plan_promotion(promotion));
    promotion.shop_level = 2;
    promotion.funds = 1;
    assert(!plan_promotion(promotion));
    assert(promotion_copies_required("普通干员") == 3);
    assert(promotion_copies_required("星极") == 2);
    assert(promotion_copies_required_from_trait("未来干员", "只需两名本干员即可合成精锐") == 2);
    assert(promotion_copies_required_from_trait("未来干员", "需要四张同名干员") == 4);
    assert(promotion_copies_required_from_trait("未来干员", "获得时使盟约层数+2") == 3);
    assert(battle_time_limit_seconds(1, false) == 45);
    assert(battle_time_limit_seconds(5, false) == 55);
    assert(battle_time_limit_seconds(6, false) == 70);
    assert(battle_time_limit_seconds(8, false) == 85);
    assert(battle_time_limit_seconds(10, false) == 95);
    assert(battle_time_limit_seconds(13, false) == 115);
    assert(battle_time_limit_seconds(20, true) == 120);
    assert(battle_time_limit_seconds(0, false) == 0);

    const std::vector<PromotionOption> promotion_options {
        { "普通输出", 3, 0, true },
        { "主输出", 4, 1, true },
        { "被禁用", 4, 2, true },
        { "未核验", 5, 3, false },
    };
    const auto selected_promotion = choose_promotion_option(promotion_options, { "主输出", "叠层手" }, { "被禁用" }, 4);
    assert(
        selected_promotion.found && selected_promotion.option.name == "主输出" && selected_promotion.option.tier == 4);
    const auto no_promotion = choose_promotion_option(promotion_options, {}, { "主输出", "被禁用" }, 5);
    assert(!no_promotion.found);

    auto economy = choose_economy_action({ 9, 8, 3, 4, 1, 4, 2500, false, false, false, false, true, false });
    assert(economy.action == EconomyAction::UpgradeShop && economy.spend == 4);
    economy = choose_economy_action({ 10, 3, 4, 0, 1, 0, 1500, false, true, false, false, true, false });
    assert(economy.action == EconomyAction::BuyCore);
    economy = choose_economy_action({ 11, 1, 4, 0, 1, 0, 1400, false, false, false, true, false, false });
    assert(economy.action == EconomyAction::BankViaOneCostTrade);
    economy = choose_economy_action({ 11, 1, 4, 0, 1, 0, 500, false, false, false, true, false, true });
    assert(economy.action == EconomyAction::ClearRemainder);
    economy = choose_economy_action({ 12, 2, 4, 0, 1, 2, 1500, false, false, false, false, true, false });
    assert(economy.action == EconomyAction::ClearRemainder);
    economy = choose_economy_action({ 12, 2, 4, 0, 1, 0, 1500, true, false, false, false, false, false });
    assert(economy.action == EconomyAction::PreserveByPact);
    EconomyContext free_refresh {};
    free_refresh.round = 3;
    free_refresh.funds = 0;
    free_refresh.shop_level = 2;
    free_refresh.refresh_cost = 1;
    free_refresh.purchase_deadline_ms = 1200;
    free_refresh.allow_refresh = true;
    free_refresh.free_refreshes = 1;
    economy = choose_economy_action(free_refresh);
    assert(economy.action == EconomyAction::Refresh && economy.spend == 0);
    const auto four_gold = round_balance_retention("band_cannot", 4);
    const auto five_gold = round_balance_retention("band_cannot", 5);
    const auto large_balance = round_balance_retention("band_cannot", 15);
    const auto ordinary_balance = round_balance_retention("band_amiya", 15);
    assert(four_gold && four_gold->carries && four_gold->retained == 4 && four_gold->interest == 0);
    assert(five_gold && five_gold->carries && five_gold->retained == 5 && five_gold->interest == 1);
    assert(large_balance && large_balance->retained == 15 && large_balance->interest == 1);
    assert(
        ordinary_balance && !ordinary_balance->carries && ordinary_balance->retained == 0 &&
        ordinary_balance->interest == 0);
    assert(!round_balance_retention("band_random", 5));
    assert(!round_balance_retention("", 5));
    PreparationPolicy reserve;
    reserve.selected_strategy_id = "band_cannot";
    reserve.primary_pact = "炎";
    reserve.round = 7;
    reserve.economy_scenario_horizon_round = 14;
    reserve.dynamic_decision_mode = true;
    reserve.growth_primary_active_verified = true;
    reserve.deployed_population = 8;
    reserve.last_round_life_loss = 0;
    assert(choose_preparation(reserve, 5, {}, {}).action == PreparationAction::Review);
    reserve.free_refreshes = 1;
    assert(choose_preparation(reserve, 5, {}, {}).free_refresh);
    reserve.free_refreshes = 0;
    reserve.last_round_life_loss = 1;
    assert(choose_preparation(reserve, 5, {}, {}).action == PreparationAction::Refresh);

    const std::vector<StrategyCandidate> strategies {
        { "unclaimed", "Unclaimed", { "莱塔尼亚" }, { "精准" }, { "灵巧" }, { "奥术" }, 1, false },
        { "claimed", "Claimed", { "乌萨斯" }, {}, {}, {}, 50, true },
    };
    auto eligibility = evaluate_strategy_eligibility(strategies[0], { "奥术", {}, {}, {}, 3 });
    assert(!eligibility.eligible);
    eligibility = evaluate_strategy_eligibility(strategies[0], { "奥术", {}, {}, {}, 1, false, false, true });
    assert(eligibility.eligible);
    eligibility = evaluate_strategy_eligibility(strategies[0], { "奥术", { "精准" }, {}, {}, 1, false, false, true });
    assert(!eligibility.eligible);
    auto opening_only = strategies[0];
    opening_only.requires_active_primary_pact = false;
    eligibility = evaluate_strategy_eligibility(opening_only, { "奥术", {}, {}, {}, 3 });
    assert(eligibility.eligible);
    eligibility = evaluate_strategy_eligibility(strategies[0], { "奥术", {}, {}, { "莱塔尼亚" }, 3 });
    assert(eligibility.eligible && eligibility.unclaimed_priority);
    eligibility = evaluate_strategy_eligibility(strategies[0], { "奥术", { "灵巧" }, {}, { "莱塔尼亚" }, 3 });
    assert(!eligibility.eligible);
    eligibility = evaluate_strategy_eligibility(strategies[0], { "奥术", { "精准" }, {}, { "莱塔尼亚" }, 3 });
    assert(!eligibility.eligible);
    auto gated = strategies[0];
    gated.requires_core_roles = true;
    auto gated_context = StrategyEligibilityContext { "奥术", {}, {}, { "莱塔尼亚" }, 3, false, false };
    assert(!evaluate_strategy_eligibility(gated, gated_context).eligible);
    gated_context.core_layer_operator_verified = true;
    assert(evaluate_strategy_eligibility(gated, gated_context).eligible == false);
    gated_context.damage_carry_verified = true;
    assert(evaluate_strategy_eligibility(gated, gated_context).eligible);
    const auto setup = choose_setup_pacts("阿戈尔", { "灵巧", "突袭" }, { "阿戈尔", "炎" }, { "灵巧" }, { "炎" });
    assert(setup.valid && setup.core_pact == "阿戈尔" && setup.support_pacts.size() == 1);
    assert(!choose_setup_pacts("炎", {}, { "阿戈尔" }, {}, {}).valid);
    const auto chosen = choose_strategy_with_fallback(strategies, { "奥术", {}, {}, { "莱塔尼亚" }, 3 });
    assert(chosen.found && chosen.candidate.id == "unclaimed");
    auto strongly_supported_claimed = strategies;
    strongly_supported_claimed[1].observed_support = 50'000;
    const auto unclaimed_first =
        choose_strategy_with_fallback(strongly_supported_claimed, { "奥术", {}, {}, { "莱塔尼亚", "乌萨斯" }, 3 });
    assert(unclaimed_first.found && unclaimed_first.candidate.id == "unclaimed");
    const auto fallback = choose_strategy_with_fallback(strategies, { "", { "精准" }, {}, { "乌萨斯" }, 3 });
    assert(fallback.found && fallback.candidate.id == "claimed");

    // A roster that activates a new pact later must not inherit the first
    // nonempty modeled list as a permanent external assertion. Withdrawals
    // also remove eligibility without modifying the caller's evidence.
    const StrategyEligibilityContext live_assertions { "奥术", {}, {}, {}, 9 };
    const auto early_context = strategy_context_for_formation(live_assertions, true, { "迅捷" });
    assert(!evaluate_strategy_eligibility(strategies[0], early_context).eligible);
    const auto later_context = strategy_context_for_formation(live_assertions, true, { "莱塔尼亚", "精准" });
    assert(evaluate_strategy_eligibility(strategies[0], later_context).eligible);
    assert(live_assertions.active_pacts.empty());
    const auto withdrawn_context = strategy_context_for_formation(live_assertions, true, {});
    assert(!evaluate_strategy_eligibility(strategies[0], withdrawn_context).eligible);
    const auto incomplete_context = strategy_context_for_formation(live_assertions, false, { "莱塔尼亚" });
    assert(!evaluate_strategy_eligibility(strategies[0], incomplete_context).eligible);
    const auto asserted_context = strategy_context_for_formation(gated_context, true, { "迅捷" });
    assert(asserted_context.active_pacts == gated_context.active_pacts);

    assert(prefer_higher_tier_strategy_refresh("band_support", 4, 4, 0, true));
    assert(prefer_higher_tier_strategy_refresh("band_support", 5, 1, 2, true));
    assert(!prefer_higher_tier_strategy_refresh("band_support", 4, 4, 3, true));
    assert(!prefer_higher_tier_strategy_refresh("band_support", 6, 4, 0, true));
    assert(!prefer_higher_tier_strategy_refresh("band_support", 0, 4, 0, true));
    assert(!prefer_higher_tier_strategy_refresh("band_support", 4, 0, 0, true));
    assert(!prefer_higher_tier_strategy_refresh("band_support", 4, 4, 0, false));
    assert(!prefer_higher_tier_strategy_refresh("band_high", 4, 4, 0, true));

    // Opening selection must act on badge evidence, even when the completed
    // card has a much brighter portrait. Banned cards cannot win this preference.
    std::vector<OpeningStrategyCard> opening_cards {
        { 0, true, true, true, 150 },
        { 1, true, true, false, 40 },
        { 2, false, true, false, 200 },
    };
    assert(choose_opening_strategy_card(opening_cards) == 1);
    assert(choose_opening_strategy_card(opening_cards, false) == 0);
    opening_cards[1].available = false;
    assert(choose_opening_strategy_card(opening_cards) == 0);
    opening_cards.push_back({ 3, true, false, false, 20 });
    assert(choose_opening_strategy_card(opening_cards) == 3);
    opening_cards.push_back({ 4, true, true, false, 10 });
    assert(choose_opening_strategy_card(opening_cards) == 4);
    assert(choose_opening_strategy_card({}) == -1);
    assert(choose_opening_strategy_card({ { 2, true, true, false, 1 }, { 18, true, true, false, 7 } }) == 18);
    assert(choose_opening_strategy_card({ { 2, true, true, false, 1 }, { 18, false, true, false, 7 } }) == 2);
    assert(choose_opening_strategy_card({ { 0, false, true, false, 100 } }) == -1);

    PreparationPolicy promotion_roster;
    promotion_roster.bench_occupied = 3;
    promotion_roster.owned["安哲拉"] = { 3, false, true };
    assert(reconcile_promotion_reward(promotion_roster, "安哲拉", "空弦"));
    assert(promotion_roster.owned.at("安哲拉").copies == 1);
    assert(promotion_roster.owned.at("安哲拉").elite);
    assert(promotion_roster.owned.at("安哲拉").deployed);
    assert(promotion_roster.owned.at("空弦").copies == 1);
    assert(!promotion_roster.owned.at("空弦").elite && !promotion_roster.owned.at("空弦").deployed);
    assert(promotion_roster.bench_occupied == 2);
    assert(reconcile_promotion_reward(promotion_roster, "", "旅骨"));
    assert(!promotion_roster.owned.at("旅骨").elite);
    assert(promotion_roster.owned.at("安哲拉").elite && promotion_roster.bench_occupied == 3);
    assert(!reconcile_promotion_reward(promotion_roster, "missing", "空弦"));
    assert(promotion_roster.owned.at("空弦").copies == 1);
    promotion_roster.owned["星极"] = { 2, false, true };
    assert(reconcile_promotion_reward(promotion_roster, "星极", "灰喉"));
    assert(promotion_roster.owned.at("星极").elite && promotion_roster.owned.at("星极").copies == 1);
    assert(promotion_roster.bench_occupied == 3);
    PreparationPolicy chained_roster;
    chained_roster.bench_occupied = 5;
    chained_roster.owned["幽灵鲨"] = { 3, false, true };
    chained_roster.owned["空弦"] = { 2, false, false };
    assert(promotion_reward_starts_chain(chained_roster, "空弦"));
    assert(reconcile_promotion_reward(chained_roster, "幽灵鲨", "空弦"));
    assert(chained_roster.owned.at("空弦").copies == 3 && !chained_roster.owned.at("空弦").elite);
    assert(reconcile_promotion_reward(chained_roster, "空弦", "棘刺"));
    assert(chained_roster.owned.at("幽灵鲨").elite && chained_roster.owned.at("幽灵鲨").deployed);
    assert(chained_roster.owned.at("空弦").elite && !chained_roster.owned.at("空弦").deployed);
    assert(chained_roster.owned.at("空弦").copies == 1 && chained_roster.bench_occupied == 3);
    assert(!chained_roster.owned.at("棘刺").elite && !promotion_reward_starts_chain(chained_roster, "空弦"));
    PreparationPolicy mixed_promotion;
    mixed_promotion.bench_occupied = 6;
    assert(set_owned_form_counts(mixed_promotion.owned["幽灵鲨"], { 3, 1 }));
    mixed_promotion.owned["幽灵鲨"].deployed = true;
    assert(set_owned_form_counts(mixed_promotion.owned["空弦"], { 2, 1 }));
    assert(promotion_reward_starts_chain(mixed_promotion, "空弦"));
    assert(reconcile_promotion_reward(mixed_promotion, "幽灵鲨", "空弦"));
    assert(owned_form_counts(mixed_promotion.owned.at("幽灵鲨"))->elite == 2);
    assert(owned_form_counts(mixed_promotion.owned.at("幽灵鲨"))->initial == 0);
    assert(owned_form_counts(mixed_promotion.owned.at("空弦"))->elite == 1);
    assert(owned_form_counts(mixed_promotion.owned.at("空弦"))->initial == 3);
    assert(reconcile_promotion_reward(mixed_promotion, "空弦", "棘刺"));
    assert(owned_form_counts(mixed_promotion.owned.at("空弦"))->elite == 2);
    assert(mixed_promotion.owned.at("空弦").copies == 2);
    mixed_promotion.owned["深巡"] = { 2, true, false };
    const auto source_before_failed_reward = owned_operator_snapshot("幽灵鲨", mixed_promotion.owned.at("幽灵鲨"));
    assert(!reconcile_promotion_reward(mixed_promotion, "幽灵鲨", "深巡"));
    assert(owned_operator_snapshot("幽灵鲨", mixed_promotion.owned.at("幽灵鲨")) == source_before_failed_reward);
}
