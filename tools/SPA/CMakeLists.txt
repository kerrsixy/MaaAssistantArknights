cmake_minimum_required(VERSION 3.28)
project(SPAPlannerTests LANGUAGES CXX)

enable_testing()
add_executable(SPAPromotionPoseTest SPAPromotionPoseTest.cpp)
target_include_directories(SPAPromotionPoseTest PRIVATE ../../src/MaaCore)
target_compile_features(SPAPromotionPoseTest PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(SPAPromotionPoseTest PRIVATE /UNDEBUG /utf-8 /permissive-)
else()
    target_compile_options(SPAPromotionPoseTest PRIVATE -UNDEBUG)
endif()
add_test(NAME SPAPromotionPoseRetention COMMAND SPAPromotionPoseTest)

add_executable(SPALifecycleBatchTest SPALifecycleBatchTest.cpp)
target_include_directories(SPALifecycleBatchTest PRIVATE ../../src/MaaCore ../../src/MaaUtils/include)
target_compile_features(SPALifecycleBatchTest PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(SPALifecycleBatchTest PRIVATE /UNDEBUG /utf-8 /permissive-)
else()
    target_compile_options(SPALifecycleBatchTest PRIVATE -UNDEBUG)
endif()
add_test(NAME SPALifecycleBatchSettlement COMMAND SPALifecycleBatchTest)

add_executable(SPADeploymentPlanCacheTest
    SPADeploymentPlanCacheTest.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPADeploymentReview.cpp)
target_include_directories(SPADeploymentPlanCacheTest PRIVATE ../../src/MaaCore ../../src/MaaUtils/include)
target_compile_features(SPADeploymentPlanCacheTest PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(SPADeploymentPlanCacheTest PRIVATE /UNDEBUG /utf-8 /permissive-)
else()
    target_compile_options(SPADeploymentPlanCacheTest PRIVATE -UNDEBUG)
endif()
add_test(NAME SPADeploymentPlanContinuation COMMAND SPADeploymentPlanCacheTest)

add_executable(SPASpatialActionDeadlineTest SPASpatialActionDeadlineTest.cpp)
target_include_directories(SPASpatialActionDeadlineTest PRIVATE ../../src/MaaCore)
target_compile_features(SPASpatialActionDeadlineTest PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(SPASpatialActionDeadlineTest PRIVATE /UNDEBUG /utf-8 /permissive-)
else()
    target_compile_options(SPASpatialActionDeadlineTest PRIVATE -UNDEBUG)
endif()
add_test(NAME SPASpatialActionDeadline COMMAND SPASpatialActionDeadlineTest)

add_executable(SPABossDeploymentTest
    SPABossDeploymentTest.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPADeploymentReview.cpp)
target_include_directories(SPABossDeploymentTest PRIVATE ../../src/MaaCore ../../src/MaaUtils/include)
target_compile_features(SPABossDeploymentTest PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(SPABossDeploymentTest PRIVATE /UNDEBUG /utf-8 /permissive-)
else()
    target_compile_options(SPABossDeploymentTest PRIVATE -UNDEBUG)
endif()
add_test(NAME SPAFixedBossDeployment COMMAND SPABossDeploymentTest)

add_executable(SPAFirstPactRecruitmentTest
    SPAFirstPactRecruitmentTest.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAFirstPactRecruitment.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAPlanner.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAFormationData.cpp)
target_include_directories(SPAFirstPactRecruitmentTest PRIVATE ../../src/MaaCore ../../src/MaaUtils/include)
target_compile_features(SPAFirstPactRecruitmentTest PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(SPAFirstPactRecruitmentTest PRIVATE /UNDEBUG /utf-8 /permissive-)
else()
    target_compile_options(SPAFirstPactRecruitmentTest PRIVATE -UNDEBUG)
endif()
add_test(NAME SPAFirstPactRecruitmentEvidence COMMAND SPAFirstPactRecruitmentTest)

add_executable(SPAFixedGrowthReservationTest
    SPAFixedGrowthReservationTest.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAPreparationPolicy.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAEconomyPolicy.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAPlanner.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAAcquisition.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAFormation.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAFormationData.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAGrowth.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAGrowthData.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPARouteProfile.cpp)
target_include_directories(SPAFixedGrowthReservationTest PRIVATE ../../src/MaaCore ../../src/MaaUtils/include)
target_compile_features(SPAFixedGrowthReservationTest PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(SPAFixedGrowthReservationTest PRIVATE /UNDEBUG /utf-8 /permissive-)
else()
    target_compile_options(SPAFixedGrowthReservationTest PRIVATE -UNDEBUG)
endif()
add_test(NAME SPAFixedGrowthPurchaseBudget COMMAND SPAFixedGrowthReservationTest)

add_executable(SPAOneCoinBankTest
    SPAOneCoinBankTest.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAPreparationPolicy.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAEconomyPolicy.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAPlanner.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAAcquisition.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAFormation.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAFormationData.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAGrowth.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAGrowthData.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPARouteProfile.cpp)
target_include_directories(SPAOneCoinBankTest PRIVATE ../../src/MaaCore ../../src/MaaUtils/include)
target_compile_features(SPAOneCoinBankTest PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(SPAOneCoinBankTest PRIVATE /UNDEBUG /utf-8 /permissive-)
else()
    target_compile_options(SPAOneCoinBankTest PRIVATE -UNDEBUG)
endif()
add_test(NAME SPAOneCoinBankFunding COMMAND SPAOneCoinBankTest)

add_executable(SPAConsumablePolicyTest SPAConsumablePolicyTest.cpp)
target_include_directories(SPAConsumablePolicyTest PRIVATE ../../src/MaaCore ../../src/MaaUtils/include)
target_compile_features(SPAConsumablePolicyTest PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(SPAConsumablePolicyTest PRIVATE /UNDEBUG /utf-8 /permissive-)
else()
    target_compile_options(SPAConsumablePolicyTest PRIVATE -UNDEBUG)
endif()
add_test(NAME SPAConsumablePromotionEvidence COMMAND SPAConsumablePolicyTest)

add_executable(SPADirectPromotionPolicyTest
    SPADirectPromotionPolicyTest.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAEquipmentPolicy.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAObservation.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAGrowth.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAGrowthData.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAFormationData.cpp)
target_include_directories(SPADirectPromotionPolicyTest PRIVATE ../../src/MaaCore ../../src/MaaUtils/include)
target_compile_features(SPADirectPromotionPolicyTest PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(SPADirectPromotionPolicyTest PRIVATE /UNDEBUG /utf-8 /permissive-)
else()
    target_compile_options(SPADirectPromotionPolicyTest PRIVATE -UNDEBUG)
endif()
add_test(NAME SPADirectPromotionPurchaseAndEvidence COMMAND SPADirectPromotionPolicyTest)

add_executable(SPAStoredCashQuoteTest SPAStoredCashQuoteTest.cpp)
target_include_directories(SPAStoredCashQuoteTest PRIVATE ../../src/MaaCore)
target_compile_features(SPAStoredCashQuoteTest PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(SPAStoredCashQuoteTest PRIVATE /UNDEBUG /utf-8 /permissive-)
else()
    target_compile_options(SPAStoredCashQuoteTest PRIVATE -UNDEBUG)
endif()
add_test(NAME SPAStoredCashQuoteEvidence COMMAND SPAStoredCashQuoteTest)

add_executable(SPAPaidRefreshObservationTest SPAPaidRefreshObservationTest.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAObservation.cpp)
target_include_directories(SPAPaidRefreshObservationTest PRIVATE ../../src/MaaCore ../../src/MaaUtils/include)
target_compile_features(SPAPaidRefreshObservationTest PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(SPAPaidRefreshObservationTest PRIVATE /UNDEBUG /utf-8 /permissive-)
else()
    target_compile_options(SPAPaidRefreshObservationTest PRIVATE -UNDEBUG)
endif()
configure_file(fixtures/paid_refresh_archived.json spa-paid-refresh-archived-fixture.json COPYONLY)
add_test(NAME SPAPaidRefreshEvidence COMMAND SPAPaidRefreshObservationTest)
set_tests_properties(SPAPaidRefreshEvidence PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

configure_file(fixtures/preparation.json spa-preparation-fixture.json COPYONLY)
configure_file(fixtures/strategy_confirm_enabled.png spa-strategy-confirm-enabled.png COPYONLY)
configure_file(fixtures/strategy_confirm_taken.png spa-strategy-confirm-taken.png COPYONLY)
configure_file(fixtures/free_refresh_counter_conflict.json spa-free-refresh-conflict-fixture.json COPYONLY)
configure_file(fixtures/tactical_decision.json spa-tactical-decision-fixture.json COPYONLY)
configure_file(fixtures/tactical_layers15.json spa-tactical-layers15-fixture.json COPYONLY)
configure_file(fixtures/supply_duplicate_titles.json spa-supply-duplicate-fixture.json COPYONLY)
add_executable(SPAPlannerTest
    SPAPlannerTest.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAPlanner.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAProgress.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAObservation.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAFormation.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAFormationData.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAData.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAAcquisition.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAGrowth.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAGrowthData.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAEgirPolicy.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPASkillPolicy.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAEquipmentPolicy.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAEconomyPolicy.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAStrategyEligibility.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAResultPolicy.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPARouteProfile.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAPreparationPolicy.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPADeploymentReview.cpp)
target_include_directories(SPAPlannerTest PRIVATE ../../src/MaaCore ../../src/MaaUtils/include)
target_compile_features(SPAPlannerTest PRIVATE cxx_std_20)
find_package(OpenCV REQUIRED)
target_include_directories(SPAPlannerTest PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(SPAPlannerTest PRIVATE ${OpenCV_LIBS})
if(MSVC)
    target_compile_options(SPAPlannerTest PRIVATE /UNDEBUG /utf-8 /permissive-)
else()
    target_compile_options(SPAPlannerTest PRIVATE -UNDEBUG)
endif()
add_test(NAME SPAPlannerInvariants COMMAND SPAPlannerTest)

get_target_property(SPA_POLICY_TEST_SOURCES SPAPlannerTest SOURCES)
list(REMOVE_ITEM SPA_POLICY_TEST_SOURCES SPAPlannerTest.cpp)
add_executable(SPAEquipmentPactTest SPAEquipmentPactTest.cpp ${SPA_POLICY_TEST_SOURCES})
target_include_directories(SPAEquipmentPactTest PRIVATE
    ../../src/MaaCore ../../src/MaaUtils/include ${OpenCV_INCLUDE_DIRS})
target_link_libraries(SPAEquipmentPactTest PRIVATE ${OpenCV_LIBS})
target_compile_features(SPAEquipmentPactTest PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(SPAEquipmentPactTest PRIVATE /UNDEBUG /utf-8 /permissive-)
else()
    target_compile_options(SPAEquipmentPactTest PRIVATE -UNDEBUG)
endif()
add_test(NAME SPAEquipmentPactConversion COMMAND SPAEquipmentPactTest)
if(WIN32)
    set_tests_properties(SPAEquipmentPactConversion PROPERTIES
        ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/../bin/Debug;$ENV{PATH}")
endif()
add_executable(SPAAcceptanceTest
    SPAAcceptanceTest.cpp
    ../../src/MaaCore/Task/MiniGame/SPA/SPAResultPolicy.cpp)
target_include_directories(SPAAcceptanceTest PRIVATE
    ../../src/MaaCore ../../src/MaaUtils/include ${OpenCV_INCLUDE_DIRS})
target_compile_features(SPAAcceptanceTest PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(SPAAcceptanceTest PRIVATE /UNDEBUG /utf-8 /permissive-)
else()
    target_compile_options(SPAAcceptanceTest PRIVATE -UNDEBUG)
endif()
add_test(NAME SPAAcceptanceEvidence COMMAND SPAAcceptanceTest)
if(WIN32)
    set_tests_properties(SPAPlannerInvariants PROPERTIES
        ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/../bin/Debug;$ENV{PATH}"
    )
endif()


option(SPA_WITH_IMAGE_TESTS "Build native camera registration tests with OpenCV" OFF)
if(SPA_WITH_IMAGE_TESTS)
    configure_file(fixtures/boss_direction_overlay_live.png spa-boss-direction-overlay-live.png COPYONLY)
    configure_file(fixtures/hidden_eel_preparation_bar_live.png spa-hidden-eel-preparation-bar-live.png COPYONLY)
    configure_file(fixtures/boss_battle_hud_live.png spa-boss-battle-hud-live.png COPYONLY)
    configure_file(fixtures/life_boss_pending_header.png spa-life-boss-pending-header.png COPYONLY)
    configure_file(fixtures/life_40_header.png spa-life-40-header.png COPYONLY)
    configure_file(fixtures/life_27_header.png spa-life-27-header.png COPYONLY)
    configure_file(fixtures/boss_urbi_source_clean.png spa-boss-urbi-source-clean.png COPYONLY)
    configure_file(fixtures/boss_urbi_source_held.png spa-boss-urbi-source-held.png COPYONLY)
    configure_file(fixtures/boss_mire_target_clean.png spa-boss-mire-target-clean.png COPYONLY)
    configure_file(fixtures/boss_mire_target_held.png spa-boss-mire-target-held.png COPYONLY)
    configure_file(fixtures/ghost_blue_gate_direction.png spa-ghost-blue-gate-direction.png COPYONLY)
    configure_file(fixtures/manual_boss_attackable_preview.png spa-manual-boss-attackable-preview.png COPYONLY)
    configure_file(fixtures/ghost_blue_gate_empty_clean.png spa-ghost-blue-gate-empty-clean.png COPYONLY)
    configure_file(fixtures/ghost_blue_gate_empty_held.png spa-ghost-blue-gate-empty-held.png COPYONLY)
    configure_file(fixtures/inventory_operator_hints_r12.png spa-inventory-operator-hints-r12.png COPYONLY)
    configure_file(fixtures/inventory_operator_hints_r13.png spa-inventory-operator-hints-r13.png COPYONLY)
    configure_file(fixtures/skadi_elite_unknown_facing_live.png spa-skadi-elite-unknown-facing-live.png COPYONLY)
    configure_file(fixtures/replacement_lappland_occluded_initial.png spa-replacement-lappland-occluded-initial.png COPYONLY)
    configure_file(fixtures/replacement_lappland_occluded_before.png spa-replacement-lappland-occluded-before.png COPYONLY)
    configure_file(fixtures/replacement_lappland_occluded_held.png spa-replacement-lappland-occluded-held.png COPYONLY)
    configure_file(fixtures/bench_elite_shadow_live.png spa-bench-elite-shadow-live.png COPYONLY)
    configure_file(fixtures/bench_two_hidden_badges_live.png spa-bench-two-hidden-badges-live.png COPYONLY)
    configure_file(fixtures/bench_wide_gap_live.png spa-bench-wide-gap-live.png COPYONLY)
    configure_file(../../resource/template/MiniGame@SPA@ShopBookOne.png spa-shop-book-one-reference.png COPYONLY)
    configure_file(fixtures/shop_book_one_live.png spa-shop-book-one-live.png COPYONLY)
    configure_file(fixtures/shop_book_one_white_live.png spa-shop-book-one-white-live.png COPYONLY)
    configure_file(fixtures/truth_deploy_held.png spa-truth-deploy-held.png COPYONLY)
    configure_file(fixtures/truth_deploy_collapsed.png spa-truth-deploy-collapsed.png COPYONLY)
    configure_file(fixtures/truth_deploy_direction.png spa-truth-deploy-direction.png COPYONLY)
    configure_file(fixtures/direction_bison_target.png spa-direction-bison-target.png COPYONLY)
    configure_file(fixtures/direction_bison_held.png spa-direction-bison-held.png COPYONLY)
    configure_file(fixtures/held_industrial_shadow.png spa-held-industrial-shadow.png COPYONLY)
    configure_file(fixtures/direction_occluded_top.png spa-direction-occluded-top.png COPYONLY)
    configure_file(fixtures/direction_occluded_top_held.png spa-direction-occluded-top-held.png COPYONLY)
    configure_file(fixtures/held_crate_overlay.png spa-held-crate-overlay.png COPYONLY)
    configure_file(fixtures/reference_m10_initial.png spa-reference-m10-initial.png COPYONLY)
    configure_file(fixtures/reference_m10_crowded.png spa-reference-m10-crowded.png COPYONLY)
    configure_file(fixtures/shadow_empty_cell_clean.png spa-shadow-empty-cell-clean.png COPYONLY)
    configure_file(fixtures/shadow_empty_cell_held.png spa-shadow-empty-cell-held.png COPYONLY)
    configure_file(../../resource/template/MiniGame@SPA@RetreatSelected.png spa-retreat-control.png COPYONLY)
    configure_file(fixtures/field_selected_scaled.png spa-field-selected-scaled.png COPYONLY)
    configure_file(fixtures/bench_selected.png spa-bench-selected.png COPYONLY)
    configure_file(fixtures/gear_preview_normal.png spa-gear-preview-normal.png COPYONLY)
    configure_file(fixtures/gear_preview_held.png spa-gear-preview-held.png COPYONLY)
    configure_file(fixtures/equipment_inventory.png spa-equipment-inventory.png COPYONLY)
    configure_file(fixtures/equipment_inventory_city.png spa-equipment-inventory-city.png COPYONLY)
    configure_file(fixtures/equipment_inventory_empty.png spa-equipment-inventory-empty.png COPYONLY)
    configure_file(fixtures/equipment_inventory_raised_row.png spa-equipment-inventory-raised-row.png COPYONLY)
    configure_file(fixtures/pact_availability_top.png spa-pact-availability-top.png COPYONLY)
    configure_file(fixtures/pact_availability_scroll.png spa-pact-availability-scroll.png COPYONLY)
    configure_file(fixtures/held_full_field.png spa-held-full-field.png COPYONLY)
    configure_file(fixtures/unheld_full_field.png spa-unheld-full-field.png COPYONLY)
    configure_file(fixtures/strategy_grid_full.png spa-strategy-grid-full.png COPYONLY)
    configure_file(fixtures/strategy_grid_detail.png spa-strategy-grid-detail.png COPYONLY)
    configure_file(fixtures/strategy_report_badge_corners_live.png spa-strategy-report-badge-corners-live.png COPYONLY)
    configure_file(../../resource/template/MiniGame@SPA@StrategyCompletedMark.png spa-strategy-report-orange.png COPYONLY)
    configure_file(../../resource/template/MiniGame@SPA@StrategySilverMark.png spa-strategy-report-silver.png COPYONLY)
    configure_file(fixtures/held_ice_board.png spa-held-ice-board.png COPYONLY)
    configure_file(fixtures/held_fence_board.png spa-held-fence-board.png COPYONLY)
    configure_file(fixtures/held_ambiguous_board.png spa-held-ambiguous-board.png COPYONLY)
    configure_file(fixtures/held_fence_occupied.png spa-held-fence-occupied.png COPYONLY)
    configure_file(fixtures/medic_range_normal.png spa-medic-range-normal.png COPYONLY)
    configure_file(fixtures/medic_range_held.png spa-medic-range-held.png COPYONLY)
    configure_file(fixtures/replacement_healer_north.png spa-replacement-healer-north.png COPYONLY)
    configure_file(fixtures/occupied_drop_held.png spa-occupied-drop-held.png COPYONLY)
    configure_file(fixtures/occupied_drop_direction.png spa-occupied-drop-direction.png COPYONLY)
    configure_file(fixtures/right_road_direction.png spa-right-road-direction.png COPYONLY)
    configure_file(fixtures/replacement_right_edge_live_held.png spa-replacement-right-edge-live-held.png COPYONLY)
    configure_file(fixtures/replacement_right_edge_live_direction.png spa-replacement-right-edge-live-direction.png COPYONLY)
    configure_file(fixtures/green_hint_label_overlay.png spa-green-hint-overlay.png COPYONLY)
    configure_file(fixtures/deployed_wildmane_east.png spa-deployed-east.png COPYONLY)
    configure_file(fixtures/deployed_astesi_east.png spa-deployed-astesi-east.png COPYONLY)
    configure_file(fixtures/deployed_ctrail_east.png spa-deployed-ctrail-east.png COPYONLY)
    configure_file(fixtures/held_unknown_facing_live.png spa-held-unknown-facing-live.png COPYONLY)
    configure_file(fixtures/boss_preparation_pipe_live.png spa-boss-preparation-pipe-live.png COPYONLY)
    configure_file(fixtures/boss_held_pipe_live.png spa-boss-held-pipe-live.png COPYONLY)
    configure_file(fixtures/meteor_move_held_live.png spa-meteor-move-held-live.png COPYONLY)
    configure_file(fixtures/meteor_move_clean_live.png spa-meteor-move-clean-live.png COPYONLY)
    configure_file(fixtures/attack_range_outline_live.png spa-attack-range-outline-live.png COPYONLY)
    configure_file(fixtures/ghost_direction_live.png spa-ghost-direction-live.png COPYONLY)
    configure_file(fixtures/full_focus_ready.png spa-full-focus-ready.png COPYONLY)
    configure_file(fixtures/full_focus_used.png spa-full-focus-used.png COPYONLY)
    configure_file(fixtures/pact_hud_single_digit_live.png spa-pact-hud-single-digit-live.png COPYONLY)
    find_package(OpenCV REQUIRED)
    add_executable(SPAProjectionTest SPAProjectionTest.cpp
        ../../src/MaaCore/Task/MiniGame/SPA/SPABoardGeometry.cpp
        ../../src/MaaCore/Task/MiniGame/SPA/SPAProjection.cpp)
    target_include_directories(SPAProjectionTest PRIVATE ../../src/MaaCore ../../src/MaaUtils/include ${OpenCV_INCLUDE_DIRS})
    target_compile_features(SPAProjectionTest PRIVATE cxx_std_20)
    target_link_libraries(SPAProjectionTest PRIVATE ${OpenCV_LIBS})
    if(WIN32)
        target_sources(SPAProjectionTest PRIVATE ../../src/MaaCore/Utils/Platform/PlatformWin32.cpp)
        target_compile_definitions(SPAProjectionTest PRIVATE UNICODE _UNICODE)
        target_link_libraries(SPAProjectionTest PRIVATE Psapi Advapi32)
    endif()
    if(MSVC)
        target_compile_options(SPAProjectionTest PRIVATE /UNDEBUG /utf-8 /permissive-)
    else()
        target_compile_options(SPAProjectionTest PRIVATE -UNDEBUG)
    endif()
    add_test(NAME SPACameraRegistration COMMAND SPAProjectionTest)
    get_target_property(SPA_INVENTORY_TEST_SOURCES SPAProjectionTest SOURCES)
    list(REMOVE_ITEM SPA_INVENTORY_TEST_SOURCES SPAProjectionTest.cpp)
    add_executable(SPABenchHitboxTest SPABenchHitboxTest.cpp ${SPA_INVENTORY_TEST_SOURCES})
    target_include_directories(SPABenchHitboxTest PRIVATE
        ../../src/MaaCore ../../src/MaaUtils/include ${OpenCV_INCLUDE_DIRS})
    target_compile_features(SPABenchHitboxTest PRIVATE cxx_std_20)
    target_link_libraries(SPABenchHitboxTest PRIVATE ${OpenCV_LIBS})
    if(WIN32)
        target_compile_definitions(SPABenchHitboxTest PRIVATE UNICODE _UNICODE)
        target_link_libraries(SPABenchHitboxTest PRIVATE Psapi Advapi32)
    endif()
    if(MSVC)
        target_compile_options(SPABenchHitboxTest PRIVATE /UNDEBUG /utf-8 /permissive-)
    else()
        target_compile_options(SPABenchHitboxTest PRIVATE -UNDEBUG)
    endif()
    configure_file(fixtures/bench_sale_head_occluded_live.png bench_sale_head_occluded_live.png COPYONLY)
    configure_file(fixtures/bench_elite_rightmost_occluded_live.png bench_elite_rightmost_occluded_live.png COPYONLY)
    configure_file(fixtures/bench_christine_with_operators_live.png bench_christine_with_operators_live.png COPYONLY)
    configure_file(fixtures/bench_christine_alone_live.png bench_christine_alone_live.png COPYONLY)
    configure_file(fixtures/bench_tier_three_glyph_live.png bench_tier_three_glyph_live.png COPYONLY)
    add_test(NAME SPABenchSaleHitbox COMMAND SPABenchHitboxTest)
    set_tests_properties(SPABenchSaleHitbox PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/../bin/Debug;$ENV{PATH}")
    add_executable(SPASetupObservationTest SPASetupObservationTest.cpp
        ../../src/MaaCore/Task/MiniGame/SPA/SPAObservation.cpp ${SPA_INVENTORY_TEST_SOURCES})
    target_include_directories(SPASetupObservationTest PRIVATE
        ../../src/MaaCore ../../src/MaaUtils/include ${OpenCV_INCLUDE_DIRS})
    target_compile_features(SPASetupObservationTest PRIVATE cxx_std_20)
    target_link_libraries(SPASetupObservationTest PRIVATE ${OpenCV_LIBS})
    if(WIN32)
        target_compile_definitions(SPASetupObservationTest PRIVATE UNICODE _UNICODE)
        target_link_libraries(SPASetupObservationTest PRIVATE Psapi Advapi32)
    endif()
    if(MSVC)
        target_compile_options(SPASetupObservationTest PRIVATE /UNDEBUG /utf-8 /permissive-)
    else()
        target_compile_options(SPASetupObservationTest PRIVATE -UNDEBUG)
    endif()
    configure_file(fixtures/setup_scrolled_archived.json setup_scrolled_archived.json COPYONLY)
    configure_file(fixtures/setup_selected_top.png setup_selected_top.png COPYONLY)
    configure_file(fixtures/setup_selected_clipped.png setup_selected_clipped.png COPYONLY)
    configure_file(fixtures/setup_scrolled_banned.png setup_scrolled_banned.png COPYONLY)
    add_test(NAME SPASetupScrolledSelection COMMAND SPASetupObservationTest)
    set_tests_properties(SPASetupScrolledSelection PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/../bin/Debug;$ENV{PATH}")
    add_executable(SPAInventoryHudTest SPAInventoryHudTest.cpp ${SPA_INVENTORY_TEST_SOURCES})
    target_include_directories(SPAInventoryHudTest PRIVATE
        ../../src/MaaCore ../../src/MaaUtils/include ${OpenCV_INCLUDE_DIRS})
    target_compile_features(SPAInventoryHudTest PRIVATE cxx_std_20)
    target_link_libraries(SPAInventoryHudTest PRIVATE ${OpenCV_LIBS})
    if(WIN32)
        target_compile_definitions(SPAInventoryHudTest PRIVATE UNICODE _UNICODE)
        target_link_libraries(SPAInventoryHudTest PRIVATE Psapi Advapi32)
    endif()
    if(MSVC)
        target_compile_options(SPAInventoryHudTest PRIVATE /UNDEBUG /utf-8 /permissive-)
    else()
        target_compile_options(SPAInventoryHudTest PRIVATE -UNDEBUG)
    endif()
    configure_file(fixtures/inventory_freeze_before_live.png inventory_freeze_before_live.png COPYONLY)
    configure_file(fixtures/inventory_freeze_after_live.png inventory_freeze_after_live.png COPYONLY)
    configure_file(fixtures/equipment_inventory.png equipment_inventory.png COPYONLY)
    configure_file(fixtures/equipment_inventory_city.png equipment_inventory_city.png COPYONLY)
    configure_file(fixtures/equipment_inventory_empty.png equipment_inventory_empty.png COPYONLY)
    add_test(NAME SPAInventoryHudIsolation COMMAND SPAInventoryHudTest)
    set_tests_properties(SPAInventoryHudIsolation PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/../bin/Debug;$ENV{PATH}")
    add_executable(SPASelectedPanelEvidenceTest SPASelectedPanelEvidenceTest.cpp)
    target_include_directories(SPASelectedPanelEvidenceTest PRIVATE
        ../../src/MaaCore ../../src/MaaUtils/include ${OpenCV_INCLUDE_DIRS})
    target_compile_features(SPASelectedPanelEvidenceTest PRIVATE cxx_std_20)
    target_link_libraries(SPASelectedPanelEvidenceTest PRIVATE ${OpenCV_LIBS})
    if(MSVC)
        target_compile_options(SPASelectedPanelEvidenceTest PRIVATE /UNDEBUG /utf-8 /permissive-)
    else()
        target_compile_options(SPASelectedPanelEvidenceTest PRIVATE -UNDEBUG)
    endif()
    configure_file(fixtures/selected_panel_title_live.png selected_panel_title_live.png COPYONLY)
    configure_file(fixtures/selected_panel_closed_live.png selected_panel_closed_live.png COPYONLY)
    add_test(NAME SPASelectedPanelEvidence COMMAND SPASelectedPanelEvidenceTest
        selected_panel_title_live.png selected_panel_closed_live.png)
    set_tests_properties(SPASelectedPanelEvidence PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/../bin/Debug;$ENV{PATH}")
    add_executable(SPAGreenCellTargetTest SPAGreenCellTargetTest.cpp ${SPA_INVENTORY_TEST_SOURCES})
    target_include_directories(SPAGreenCellTargetTest PRIVATE
        ../../src/MaaCore ../../src/MaaUtils/include ${OpenCV_INCLUDE_DIRS})
    target_compile_features(SPAGreenCellTargetTest PRIVATE cxx_std_20)
    target_link_libraries(SPAGreenCellTargetTest PRIVATE ${OpenCV_LIBS})
    if(WIN32)
        target_compile_definitions(SPAGreenCellTargetTest PRIVATE UNICODE _UNICODE)
        target_link_libraries(SPAGreenCellTargetTest PRIVATE Psapi Advapi32)
    endif()
    if(MSVC)
        target_compile_options(SPAGreenCellTargetTest PRIVATE /UNDEBUG /utf-8 /permissive-)
    else()
        target_compile_options(SPAGreenCellTargetTest PRIVATE -UNDEBUG)
    endif()
    configure_file(fixtures/green_cell_interiors.json green_cell_interiors.json COPYONLY)
    configure_file(fixtures/green_cell_r3_live.png green_cell_r3_live.png COPYONLY)
    configure_file(fixtures/green_cell_r4_live.png green_cell_r4_live.png COPYONLY)
    add_test(NAME SPAGreenCellInterior COMMAND SPAGreenCellTargetTest)
    set_tests_properties(SPAGreenCellInterior PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/../bin/Debug;$ENV{PATH}")
    add_executable(SPACashEquipmentEvidenceTest SPACashEquipmentEvidenceTest.cpp ${SPA_INVENTORY_TEST_SOURCES})
    target_include_directories(SPACashEquipmentEvidenceTest PRIVATE
        ../../src/MaaCore ../../src/MaaUtils/include ${OpenCV_INCLUDE_DIRS})
    target_compile_features(SPACashEquipmentEvidenceTest PRIVATE cxx_std_20)
    target_link_libraries(SPACashEquipmentEvidenceTest PRIVATE ${OpenCV_LIBS})
    if(WIN32)
        target_compile_definitions(SPACashEquipmentEvidenceTest PRIVATE UNICODE _UNICODE)
        target_link_libraries(SPACashEquipmentEvidenceTest PRIVATE Psapi Advapi32)
    endif()
    if(MSVC)
        target_compile_options(SPACashEquipmentEvidenceTest PRIVATE /UNDEBUG /utf-8 /permissive-)
    else()
        target_compile_options(SPACashEquipmentEvidenceTest PRIVATE -UNDEBUG)
    endif()
    add_test(NAME SPACashEquipmentEvidence COMMAND SPACashEquipmentEvidenceTest)
    set_tests_properties(SPACashEquipmentEvidence PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/../bin/Debug;$ENV{PATH}")
    # CTest may execute a configuration-specific binary from Debug/ or
    # Release/.  Fixtures are copied to the common test binary directory, so
    # pin the working directory there instead of relying on the runner's cwd.
    set_tests_properties(
        SPACameraRegistration
        PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        # The native image test links the Debug OpenCV build used by MaaCore.
        # CTest does not inherit the IDE's Debug DLL search path, so make the
        # dependency explicit instead of relying on whichever shell launched it.
        ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/../bin/Debug;$ENV{PATH}"
    )
endif()
