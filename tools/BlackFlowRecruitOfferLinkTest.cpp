#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <tuple>

#include "AsstCaller.h"
#include "Config/TaskData.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Task/Roguelike/BlackFlow/BlackFlowRecruitmentPageObservation.h"
#include "Vision/Roguelike/RoguelikeRecruitImageAnalyzer.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    const std::filesystem::path resources(argv[1]), fixtures(argv[2]), output(argv[3]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(resources.native()).c_str())) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto check = [&](bool condition, const std::string& name) {
        ++checks;
        if (!condition) {
            ++failures;
            std::cout << "FAIL " << name << '\n';
        }
    };
    const auto rect_json = [](const Rect& rect) {
        return json::array { rect.x, rect.y, rect.width, rect.height };
    };
    json::array results;
    const std::vector<std::tuple<std::string, std::string, std::string>> samples {
        { "prior_run21", "prior_run21_recruit_list.png", "prior_run21_legacy_page.json" },
        { "current_core112_reward_page", "live112_first_page.png", "live112_first_page.json" },
    };
    for (const auto& [label, image_name, expected_name] : samples) {
        const auto frame = MAA_NS::imread(fixtures / MAA_NS::to_u16(image_name));
        const auto expected = json::open(fixtures / MAA_NS::to_u16(expected_name));
        if (frame.empty() || !expected) {
            return 3;
        }
        RoguelikeRecruitImageAnalyzer analyzer(frame);
        const bool analyzed = analyzer.analyze();
        check(analyzed, label + " actual native analyzer succeeds");
        const auto& legacy = analyzer.get_result();
        const auto sidecar = analyzer.get_candidate_evidence();
        const auto& prices = analyzer.get_hope_cost_evidence();
        const blackflow::RecruitmentPageObservationContext context {
            .client = expected->get("client", ""),
            .uid = expected->get("uid", ""),
            .run_id = expected->get("run_id", ""),
            .frame_read_completed_at_local = expected->get("frame_read_completed_at_local", ""),
            .evidence_directory = output / L"frames" / MAA_NS::to_u16(label),
            .analysis_succeeded = analyzed,
        };
        const auto page = blackflow::make_recruitment_page_observation(frame, analyzer, context);
        check(page.get("same_analysis_sidecar_used", false), label + " actual helper consumer uses new typed sidecar");
        check(
            page.at("candidate_evidence_count").as_integer() == static_cast<int>(sidecar.size()),
            label + " helper preserves all named observations including excluded rows");
        check(
            page.at("visible_set_complete").is_null() && page.at("all_pages_complete").is_null(),
            label + " exact links do not invent full set proof");
        check(
            page.get("run_id", "invalid") == expected->get("run_id", ""),
            label + " empty source run ID remains empty not invented");
        const auto& old = expected->at("usable_candidates_before_consumer_filters").as_array();
        check(legacy.size() == old.size(), label + " legacy result count matches actual Core112 producer");
        json::array rows;
        for (std::size_t index = 0; index < legacy.size(); ++index) {
            const auto& current = legacy[index];
            if (index < old.size()) {
                const auto& before = old.at(index);
                check(
                    current.name == before.get("name", "") && current.elite == before.get("raw_elite", -9) &&
                        current.level == before.get("raw_level", -9) &&
                        current.temporary == before.get("analyzer_temporary_flag", !current.temporary) &&
                        rect_json(current.rect) == before.at("selection_rect").as_array(),
                    label + " old name rect phase level temporary and ordering unchanged");
                const auto& price = before.at("hope_cost");
                check(
                    price.is_null() ? !current.hope_cost : current.hope_cost == price.as_integer(),
                    label + " old actual hope cost unchanged");
            }
            const auto paired =
                std::ranges::find_if(sidecar, [&](const auto& value) { return value.usable_result_index == index; });
            check(paired != sidecar.end(), label + " each usable result has indexed sidecar");
            if (paired == sidecar.end()) {
                continue;
            }
            check(
                paired->name == current.name && paired->name_rect == current.rect && paired->elite == current.elite &&
                    paired->level == current.level,
                label + " same analysis candidate fields match legacy");
            const auto exact = analyzer.resolve_candidate_hope_cost(*paired);
            check(exact.has_value() && paired->exact_price_link, label + " exact same-analysis flag and index resolve");
            if (exact) {
                check(
                    exact->get().currency_rect == paired->currency_flag_rect &&
                        exact->get().hope_cost == current.hope_cost,
                    label + " linked price belongs to this currency anchor");
                const auto& emitted = page.at("usable_candidates_before_consumer_filters").as_array().at(index);
                check(
                    emitted.get("exact_same_analysis_price_link", false) &&
                        emitted.at("price_evidence_index").as_integer() ==
                            static_cast<int>(*paired->hope_cost_evidence_index),
                    label + " emitted usable candidate carries exact price index");
                if (exact->get().operation_score >= 0.9 &&
                    (exact->get().operation == "招募" || exact->get().operation == "进阶")) {
                    check(
                        emitted.get("card_operation", "") == exact->get().operation,
                        label + " actual helper emits operation from this exact same card");
                }
            }
            if (current.temporary) {
                check(
                    paired->temporary == true && paired->temporary_badge_rect && paired->temporary_badge_score,
                    label + " positive temporary has badge rect and score");
            }
            else {
                check(
                    !paired->temporary && !paired->temporary_badge_rect,
                    label + " raw false does not manufacture permanent identity or inherit another card badge");
            }
        }
        for (const auto& entry : sidecar) {
            const auto exact = analyzer.resolve_candidate_hope_cost(entry);
            rows.emplace_back(
                json::object {
                    { "analysis_id", entry.analysis_id },
                    { "observation_index", entry.observation_index },
                    { "usable_result_index",
                      entry.usable_result_index ? json::value(*entry.usable_result_index) : json::value(nullptr) },
                    { "name", entry.name },
                    { "name_rect", rect_json(entry.name_rect) },
                    { "currency_flag_rect", rect_json(entry.currency_flag_rect) },
                    { "price_evidence_index",
                      entry.hope_cost_evidence_index ? json::value(*entry.hope_cost_evidence_index)
                                                     : json::value(nullptr) },
                    { "exact_link", exact.has_value() },
                    { "operation", exact ? json::value(exact->get().operation) : json::value(nullptr) },
                    { "hope_cost",
                      exact && exact->get().hope_cost ? json::value(*exact->get().hope_cost) : json::value(nullptr) },
                    { "temporary", entry.temporary ? json::value(*entry.temporary) : json::value(nullptr) },
                    { "temporary_reason", entry.temporary_reason },
                    { "usability_reason", entry.usability_reason },
                });
        }

        if (sidecar.size() >= 2) {
            auto bad = sidecar.at(0);
            bad.hope_cost_evidence_index = sidecar.at(1).hope_cost_evidence_index;
            check(!analyzer.resolve_candidate_hope_cost(bad), label + " another card price index rejected");
            bad = sidecar.at(0);
            ++bad.currency_flag_rect.x;
            check(!analyzer.resolve_candidate_hope_cost(bad), label + " shifted currency anchor rejected");
            bad = sidecar.at(0);
            bad.observation_index = sidecar.at(1).observation_index;
            check(!analyzer.resolve_candidate_hope_cost(bad), label + " wrong candidate index rejected");
            bad = sidecar.at(0);
            bad.name = sidecar.at(1).name;
            check(!analyzer.resolve_candidate_hope_cost(bad), label + " same-name or swapped-name pairing rejected");
            bad = sidecar.at(0);
            bad.hope_cost_evidence_index = prices.size() + 100;
            check(!analyzer.resolve_candidate_hope_cost(bad), label + " out-of-range price index rejected");
        }
        if (!sidecar.empty()) {
            const auto stale = sidecar.at(0);
            check(analyzer.analyze(), label + " repeated same-frame analysis succeeds");
            check(
                !analyzer.resolve_candidate_hope_cost(stale),
                label + " stale analysis evidence rejected even at same rect");
        }
        results.emplace_back(
            json::object {
                { "sample", label },
                { "source_frame", MAA_NS::path_to_utf8_string(fixtures / MAA_NS::to_u16(image_name)) },
                { "source_UID", expected->get("uid", "") },
                { "source_run_id", expected->get("run_id", "") },
                { "source_run_id_empty_is_unknown", expected->get("run_id", "").empty() },
                { "scope", "offline_reanalysis_of_frozen_actual_Core112_or_prior_run21_frame_not_live113_output" },
                { "rows", std::move(rows) },
            });

        if (label == "prior_run21") {
            const auto& fresh = analyzer.get_candidate_evidence();
            const auto typhon = std::ranges::find_if(fresh, [](const auto& value) { return value.name == "提丰"; });
            if (typhon != fresh.end() && analyzer.resolve_candidate_hope_cost(*typhon)) {
                const auto& price = analyzer.resolve_candidate_hope_cost(*typhon)->get();
                check(
                    price.operation == "进阶" && price.hope_cost == 2,
                    "real Typhon promotion operation and hope2 exactly associated");
                auto masked = frame.clone();
                masked(make_rect<cv::Rect>(price.price_roi)).setTo(cv::Scalar::all(0));
                RoguelikeRecruitImageAnalyzer no_price(masked);
                check(no_price.analyze(), "price-masked actual page still has candidates");
                const auto missing = std::ranges::find_if(no_price.get_candidate_evidence(), [](const auto& value) {
                    return value.name == "提丰";
                });
                check(missing != no_price.get_candidate_evidence().end(), "price-masked candidate sidecar survives");
                if (missing != no_price.get_candidate_evidence().end()) {
                    const auto linked = no_price.resolve_candidate_hope_cost(*missing);
                    check(
                        linked && !linked->get().hope_cost,
                        "exact card association does not invent an unreadable price");
                }
            }
            const auto& fresh_level = analyzer.get_candidate_evidence();
            const auto level_target =
                std::ranges::find_if(fresh_level, [](const auto& value) { return value.name == "提丰"; });
            if (level_target != fresh_level.end()) {
                auto level_masked = frame.clone();
                const auto level_roi = level_target->name_rect.move(Task.get("RoguelikeRecruitLevel")->rect_move);
                level_masked(make_rect<cv::Rect>(level_roi)).setTo(cv::Scalar::all(0));
                RoguelikeRecruitImageAnalyzer no_level(level_masked);
                check(no_level.analyze(), "level-masked actual page still has candidates");
                const auto missing_level =
                    std::ranges::find_if(no_level.get_candidate_evidence(), [](const auto& value) {
                        return value.name == "提丰";
                    });
                check(missing_level != no_level.get_candidate_evidence().end(), "level-masked named sidecar survives");
                if (missing_level != no_level.get_candidate_evidence().end()) {
                    const auto linked = no_level.resolve_candidate_hope_cost(*missing_level);
                    check(
                        missing_level->level <= 0 && linked && linked->get().hope_cost == 2,
                        "missing level does not erase independently proven exact price2 link");
                    if (missing_level->level < 0) {
                        check(
                            !missing_level->usable_result_index,
                            "legacy-excluded level row stays excluded from get_result");
                    }
                }
            }
            auto masked_badge = frame.clone();
            masked_badge(make_rect<cv::Rect>(Task.get("Roguelike@TempRecruitFlag")->roi)).setTo(cv::Scalar::all(0));
            RoguelikeRecruitImageAnalyzer no_badge(masked_badge);
            check(no_badge.analyze(), "badge-masked actual page remains recognizable");
            for (const auto& entry : no_badge.get_candidate_evidence()) {
                check(!entry.temporary, "missing badge is unknown for every recognized card, not false");
            }
        }
    }
    const json::value report = json::object {
        { "checks", checks },
        { "failures", failures },
        { "actual_native_analyzer_called", true },
        { "new_sidecar_resolver_called", true },
        { "current_live113_output_claimed", false },
        { "priority_adjusted", false },
        { "gameplay_inputs", 0 },
        { "samples", std::move(results) },
    };
    std::ofstream stream(output / L"native_report.json");
    stream << report;
    std::cout << "RECRUIT_OFFER_LINK113 checks=" << checks << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
