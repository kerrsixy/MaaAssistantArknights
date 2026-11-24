#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "AsstCaller.h"
#include "Config/TaskData.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Task/Roguelike/BlackFlow/BlackFlowRecruitmentPageObservation.h"
#include "Vision/Roguelike/RoguelikeRecruitImageAnalyzer.h"

// Offline only: use the frozen prior run21 page, never claim run72 or screenshot UID proof.
int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path resource_root(argv[1]), fixture(argv[2]), output(argv[3]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(resource_root.native()).c_str())) {
        return 3;
    }
    const auto frame = MAA_NS::imread(fixture);
    if (frame.empty()) {
        return 3;
    }
    int checks = 0;
    int failures = 0;
    const auto check = [&](bool condition, const std::string& detail) {
        ++checks;
        if (!condition) {
            ++failures;
            std::cout << "FAIL " << detail << '\n';
        }
    };
    const auto write = [&](const std::string& name, const json::value& value) {
        std::ofstream stream(output / MAA_NS::to_u16(name));
        stream << value;
    };
    RecruitmentPageObservationContext context {
        .client = "Official",
        .uid = "",
        .run_id = "offline_prior_run21_fixture_UID_unverified",
        .frame_read_completed_at_local = "offline_fixture_no_live_capture_time",
        .evidence_directory = output / L"frames",
        .recruit_index = 1,
        .page_index = 0,
        .analysis_succeeded = false,
        .legacy_filter_hope = std::nullopt,
        .initial_recruitment = false,
    };
    RoguelikeRecruitImageAnalyzer analyzer(frame);
    context.analysis_succeeded = analyzer.analyze();
    check(context.analysis_succeeded, "actual native analyzer recognizes prior run21 recruitment page");
    const auto& usable = analyzer.get_result();
    const auto& prices = analyzer.get_hope_cost_evidence();
    const auto page = make_recruitment_page_observation(frame, usable, prices, context);
    write("real_prior_run21_page.json", page);
    check(page.at("uid").as_string().empty(), "fixture UID is not invented from current account");
    check(
        page.at("usable_candidate_count").as_integer() == static_cast<int>(usable.size()),
        "usable result count serialized");
    check(
        page.at("price_anchor_count").as_integer() == static_cast<int>(prices.size()),
        "all price anchor evidence retained");
    check(
        page.at("visible_set_complete").is_null() && page.at("all_pages_complete").is_null(),
        "partial right cards never become full set proof");
    check(
        !page.get("valuation_consumer_enabled", true) && !page.get("priority_adjusted", true),
        "observation cannot activate valuation");
    check(page.get("gameplay_inputs", -1) == 0, "helper performs no game input");
    check(page.get("frame_saved", false), "actual source frame persisted");
    if (page.get("frame_saved", false)) {
        const auto restored = MAA_NS::imread(page.at("frame_path").as_string());
        check(
            !restored.empty() && restored.size() == frame.size() && cv::norm(restored, frame, cv::NORM_INF) == 0,
            "persisted frame preserves source pixels");
    }
    bool temporary_found = false;
    bool promotion_found = false;
    for (const auto& candidate : usable) {
        if (candidate.name == "子月") {
            temporary_found = candidate.temporary && candidate.hope_cost == 0;
            check(candidate.elite == 1 && candidate.level == 70, "visually reviewed temporary Ziyue E1 LV70");
        }
        else if (candidate.name == "提丰") {
            promotion_found = candidate.elite == 2 && candidate.level == 90 && candidate.hope_cost == 2;
        }
        else if (candidate.rect.x < 1150) {
            check(
                candidate.elite == 1 && candidate.level == 80 && candidate.hope_cost == 4,
                "other fully recognized cards E1 LV80 actual hope4");
        }
    }
    check(temporary_found, "visually reviewed Ziyue temporary zero hope");
    check(promotion_found, "visually reviewed Typhon E2 LV90 actual hope2");
    bool operation_found = false;
    for (const auto& evidence : prices) {
        operation_found =
            operation_found || (evidence.name == "提丰" && evidence.operation == "进阶" && evidence.hope_cost == 2);
    }
    check(operation_found, "native raw operation and associated price prove promotion without roster guess");

    // Preserve an actual independent price anchor after intentionally removing one usable result.
    // This tests the helper contract, not a claim that the original analyzer failed on this page.
    auto partial = usable;
    std::erase_if(partial, [](const auto& candidate) { return candidate.name == "提丰"; });
    const auto unpaired = make_recruitment_page_observation(frame, partial, prices, context);
    check(unpaired.get("unpaired_price_evidence_count", 0) > 0, "price evidence survives an unpaired usable result");
    check(unpaired.at("all_pages_complete").is_null(), "unpaired evidence cannot become completion");
    write("controlled_unpaired_result.json", unpaired);

    // Deterministic unknown-state fixture: supplied fields remain unknown, not maximum/E0/free.
    battle::roguelike::Recruitment unknown;
    unknown.name = "fixture_unknown_operator";
    unknown.elite = -1;
    unknown.level = 0;
    unknown.rect = { 420, 120, 100, 30 };
    unknown.temporary = false;
    const auto unknown_page = make_recruitment_page_observation(frame, { unknown }, {}, context);
    const auto& unknown_row = unknown_page.at("usable_candidates_before_consumer_filters").as_array().at(0);
    check(unknown_row.at("operator_id").is_null(), "unknown identity is not guessed from account");
    check(
        unknown_row.at("elite").is_null() && unknown_row.get("raw_elite", 0) == -1,
        "unknown elite preserved separately from real E0");
    check(
        unknown_row.at("level").is_null() && unknown_row.at("hope_cost").is_null(),
        "unknown level and cost do not become max or free");
    check(
        unknown_row.at("temporary").is_null() && !unknown_row.get("analyzer_temporary_flag", true),
        "raw false badge output does not prove permanent identity");
    unknown.elite = 0;
    unknown.level = 1;
    unknown.hope_cost = 0;
    const auto e0_page = make_recruitment_page_observation(frame, { unknown }, {}, context);
    const auto& e0_row = e0_page.at("usable_candidates_before_consumer_filters").as_array().at(0);
    check(
        e0_row.get("elite", -1) == 0 && e0_row.get("hope_cost", -1) == 0,
        "provided real E0 and observed zero cost remain distinct");

    auto missing_directory = context;
    missing_directory.evidence_directory.clear();
    const auto no_directory = make_recruitment_page_observation(frame, usable, prices, missing_directory);
    check(
        !no_directory.get("frame_saved", true) && no_directory.at("frame_path").is_null(),
        "missing output directory remains frame evidence failure");
    check(
        no_directory.at("usable_candidate_count").as_integer() == static_cast<int>(usable.size()),
        "frame write failure does not discard existing observations");
    auto failed_analysis = context;
    failed_analysis.analysis_succeeded = false;
    const auto failed = make_recruitment_page_observation(cv::Mat {}, {}, {}, failed_analysis);
    check(
        !failed.get("analysis_succeeded", true) && failed.at("all_pages_complete").is_null(),
        "failed analysis is not empty successful full set");
    check(!failed.get("frame_saved", true), "empty frame does not become saved evidence");
    write("unknown_state.json", unknown_page);
    write("frame_failure.json", no_directory);
    write("analysis_failure.json", failed);
    write(
        "native_report.json",
        json::object {
            { "scope", "prior_run21_real_UI_and_controlled_helper_fixtures_not_run72" },
            { "screenshot_UID_verified", false },
            { "checks", checks },
            { "failures", failures },
            { "gameplay_inputs", 0 },
            { "actual_native_analyzer_called", true },
            { "actual_native_helper_called", true },
        });
    std::cout << "RECRUITMENT_PAGE112 checks=" << checks << " failures=" << failures << " inputs=0\n";
    return failures == 0 ? 0 : 1;
}
