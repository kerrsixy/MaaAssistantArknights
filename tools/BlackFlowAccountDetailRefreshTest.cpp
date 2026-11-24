#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "AsstCaller.h"
#include "Config/TaskData.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Vision/Miscellaneous/PipelineAnalyzer.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowAccountTargetLocator.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowAccountTrainingAnalyzer.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path resources(argv[1]), candidate(argv[2]), output(argv[3]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(resources.native()).c_str()) ||
        !Task.load(candidate / L"resource/tasks/Roguelike/BlackFlow.json")) {
        return 3;
    }
    const auto fixtures = json::open(candidate / L"fixture_manifest.json");
    if (!fixtures) {
        return 3;
    }
    int checks = 0, failures = 0, recovered = 0;
    const auto check = [&](bool condition, const std::string& label) {
        ++checks;
        if (!condition) {
            ++failures;
            std::cout << "FAIL " << label << '\n';
        }
    };
    const auto elapsed_ms = [](const auto& start) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    json::array detail_rows, good_rows, locator_rows;
    const auto training_equal = [](const AccountTrainingEvidence& a, const AccountTrainingEvidence& b) {
        return a.operator_id == b.operator_id && a.operator_name == b.operator_name && a.elite == b.elite &&
               a.level == b.level && a.base_skill_level == b.base_skill_level && a.mastery_levels == b.mastery_levels &&
               a.availability == b.availability;
    };
    const auto require_atomic = [&](const auto& detail, const std::string& id, const std::string& name) {
        check(
            detail.strength.operator_id == id && detail.training.operator_id == id &&
                detail.strength.operator_name == name && detail.training.operator_name == name &&
                detail.strength.elite == detail.training.elite && detail.strength.level == detail.training.level,
            "atomic detail and training exact identity and phase agree");
    };
    for (const auto& fixture : fixtures->at("failed_run71").as_array()) {
        const auto id = fixture.at("id").as_string();
        const auto name = fixture.at("name").as_string();
        const auto frame = MAA_NS::imread(candidate / MAA_NS::to_u16(fixture.at("frozen_frame").as_string()));
        const auto start = std::chrono::steady_clock::now();
        const auto detail = BlackFlowAccountTrainingAnalyzer(frame).analyze_detail(id, name);
        const double duration = elapsed_ms(start);
        recovered += detail.has_value() ? 1 : 0;
        check(
            detail.has_value() == fixture.get("offline_new_reader_recovers", false),
            "run71 real failed frame retains109 identity outcome " + id);
        if (detail) {
            require_atomic(*detail, id, name);
            check(
                detail->training.elite == fixture.get("elite", -1) &&
                    detail->training.level == fixture.get("level", -1) &&
                    detail->training.base_skill_level == fixture.get("base_skill_level", -1),
                "real failed frame numeric training agrees with109 corpus " + id);
        }
        detail_rows.emplace_back(
            json::object {
                { "id", id },
                { "known", detail.has_value() },
                { "duration_ms", duration },
                { "base_skill_level", detail ? detail->training.base_skill_level : -1 },
            });
    }
    check(
        recovered == fixtures->get("reference_109_recovered", -1),
        "all22 real failed frames retain19 known and3 unknown");
    const auto ew_path = candidate / L"fixtures/good109/char_1035_wisdel/detail-strength.png";
    const auto ew_frame = MAA_NS::imread(ew_path);
    for (const auto& fixture : fixtures->at("good109").as_array()) {
        if (fixture.get("kind", "") != "detail-strength.png") {
            continue;
        }
        const auto id = fixture.at("id").as_string();
        const auto name = fixture.at("name").as_string();
        const auto frame = MAA_NS::imread(candidate / MAA_NS::to_u16(fixture.at("frozen").as_string()));
        const auto start = std::chrono::steady_clock::now();
        const auto atomic = BlackFlowAccountTrainingAnalyzer(frame).analyze_detail(id, name);
        const double atomic_ms = elapsed_ms(start);
        const auto legacy = BlackFlowAccountTrainingAnalyzer(frame).analyze(id, name);
        check(
            atomic && legacy && training_equal(atomic->training, *legacy),
            "legacy training API retains good109 result " + id);
        if (atomic) {
            require_atomic(*atomic, id, name);
        }
        good_rows.emplace_back(
            json::object { { "id", id }, { "known", atomic.has_value() }, { "atomic_ms", atomic_ms } });
    }
    const auto ew = BlackFlowAccountTrainingAnalyzer(ew_frame).analyze_detail("char_1035_wisdel", "维什戴尔");
    check(
        ew && ew->training.elite == 2 && ew->training.level == 90 && ew->training.base_skill_level == 7 &&
            ew->training.mastery_levels == std::array<int, 3> { 3, 3, 3 },
        "actual outside EW E2L90 rank7 andM3 preserved");
    check(
        !BlackFlowAccountTrainingAnalyzer(ew_frame).analyze_detail("char_4039_horn", "号角"),
        "wrong exact operator cannot borrow strength or training");
    auto identity = Task.get<OcrTaskInfo>("BlackFlowAccountScan@DetailName");
    const auto mastery_task = Task.get("OperProgress@CurrentSkill1MasterLevel");
    if (!identity || !mastery_task || ew_frame.empty()) {
        return 4;
    }
    const auto original_names = identity->text;
    identity->text = std::vector<std::string> { "not_the_current_operator" };
    const auto page_matches = [&](const std::string& task) {
        PipelineAnalyzer pipeline(ew_frame);
        pipeline.set_tasks(std::vector<std::string> { task });
        return pipeline.analyze().has_value();
    };
    check(
        page_matches("BlackFlowAccountScan@TrainingDetail"),
        "optional training outer gate is detail anchor without duplicate name OCR");
    check(!page_matches("BlackFlowAccountScan@Detail"), "module detail still requires exact configured identity");
    identity->text = BlackFlowDetailStrengthAnalyzer::expected_labels("char_1035_wisdel", "维什戴尔");
    check(page_matches("BlackFlowAccountScan@Detail"), "module detail original positive identity gate retained");
    identity->text = original_names;
    auto missing_mastery = ew_frame.clone();
    const auto mastery_roi = mastery_task->roi;
    missing_mastery(make_rect<cv::Rect>(mastery_roi)).setTo(cv::Scalar::all(0));
    const auto fresh = BlackFlowAccountTrainingAnalyzer(missing_mastery).analyze_detail("char_1035_wisdel", "维什戴尔");
    check(
        fresh && ew && fresh->strength.level == ew->strength.level && fresh->training.base_skill_level == 7 &&
            fresh->training.mastery_levels[0] < 0,
        "unchanged level does not reuse cached M3 when current mastery is unreadable");

    bool locator_exercised = false;
    for (const auto& fixture : fixtures->at("good109").as_array()) {
        if (locator_exercised || fixture.get("kind", "") != "list-before.png") {
            continue;
        }
        const auto id = fixture.at("id").as_string();
        const auto name = fixture.at("name").as_string();
        const auto frame = MAA_NS::imread(candidate / MAA_NS::to_u16(fixture.at("frozen").as_string()));
        OperBoxImageAnalyzer full(frame);
        const auto start_full = std::chrono::steady_clock::now();
        check(full.analyze(), "actual operator list full baseline recognizes");
        const double full_ms = elapsed_ms(start_full);
        const auto page = full.get_result();
        const auto found =
            std::ranges::find_if(page, [&](const auto& item) { return item.id == id && item.name == name; });
        if (found == page.end()) {
            continue;
        }
        const auto footer = Task.get("BlackFlowAccountScan@TrainingSelect")->specific_rect;
        const auto start_fast = std::chrono::steady_clock::now();
        const auto fast = BlackFlowAccountTargetLocator(frame).locate(id, name, page, footer);
        const double fast_ms = elapsed_ms(start_fast);
        check(
            fast.target && fast.used_verified_page_geometry && !fast.full_page_scanned && fast.page_anchor_verified,
            "fresh list anchor plus exact crop reuses only verified same-page coordinates");
        if (fast.target) {
            check(
                fast.target->oper.id == id && fast.target->recognized_name == name && fast.target->name_score >= 0.95,
                "fast location has exact expected identity text and original confidence floor");
        }
        auto stale_geometry = page;
        for (auto& item : stale_geometry) {
            if (item.id == id && item.name == name) {
                item.rect.x = frame.cols + 5;
            }
        }
        const auto fallback = BlackFlowAccountTargetLocator(frame).locate(id, name, stale_geometry, footer);
        check(
            fallback.target && fallback.full_page_scanned && !fallback.used_verified_page_geometry,
            "bad cached geometry falls back to full fresh scan rather than clicking old coordinates");
        check(
            !BlackFlowAccountTargetLocator(ew_frame).locate(id, name, page, footer).target,
            "a detail frame cannot borrow cached list target coordinates");
        check(
            !BlackFlowAccountTargetLocator(frame).locate("char_4039_horn", "维什戴尔", page, footer).target,
            "ID-name mismatch cannot enter reuse or fallback selection");
        locator_rows.emplace_back(
            json::object {
                { "id", id },
                { "full_baseline_ms", full_ms },
                { "fresh_gate_crop_ms", fast_ms },
                { "fast_geometry_used", fast.used_verified_page_geometry },
                { "fallback_full_scan", fallback.full_page_scanned },
            });
        locator_exercised = true;
    }
    check(locator_exercised, "real list-before fixture exercised target locator");
    std::ofstream(output / L"native_report.json") << json::value(
        json::object {
            { "checks", checks },
            { "failures", failures },
            { "real_failed_frames", detail_rows },
            { "good109", good_rows },
            { "locator", locator_rows },
            { "offline_recovered", recovered },
            { "gameplay_inputs", 0 },
            { "workflow_execution_exercised", false },
            { "cache_written", false },
            { "live115_verified", false },
        });
    std::cout << "ACCOUNT_DETAIL115 checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
