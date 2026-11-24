#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "AsstCaller.h"
#include "Config/TaskData.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Vision/Miscellaneous/PipelineAnalyzer.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowAccountTrainingAnalyzer.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowDetailStrengthAnalyzer.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 6) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path root(argv[1]), failed_frame(argv[2]), good_frames(argv[3]), ew_frame(argv[4]),
        output(argv[5]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(root.native()).c_str())) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool result, const char* label) {
        ++checks;
        if (!result) {
            ++failures;
            std::cout << "FAIL " << label << '\n';
        }
    };
    const auto frame = MAA_NS::imread(failed_frame);
    auto page = Task.get<OcrTaskInfo>("BlackFlowAccountScan@DetailName");
    if (frame.empty() || !page) {
        return 4;
    }
    const auto original = page->text;
    const auto matches = [&]() {
        PipelineAnalyzer analyzer(frame);
        analyzer.set_tasks({ "BlackFlowAccountScan@TrainingDetail" });
        return analyzer.analyze().has_value();
    };
    page->text = { "余" };
    expect(!matches(), "actual old Chinese-only gate reproduces failed frame");
    const auto labels = BlackFlowDetailStrengthAnalyzer::expected_labels("char_2026_yu", "余");
    expect(labels == std::vector<std::string> { "余", "Yu" }, "only canonical labels are admitted");
    page->text = labels;
    expect(matches(), "actual detail page passes with canonical English label");
    const auto detail = BlackFlowDetailStrengthAnalyzer(frame).analyze("char_2026_yu", "余");
    expect(detail && detail->recognized_name == "Yu", "same-frame reader verifies the English title");
    expect(detail && detail->elite == 2 && detail->level == 90, "actual Yu E2L90 preserved");
    const auto training = BlackFlowAccountTrainingAnalyzer(frame).analyze("char_2026_yu", "余");
    expect(
        training && training->base_skill_level == 7 && training->mastery_levels == std::array<int, 3> { 0, 3, 3 },
        "outside Yu training is observed rather than borrowed");
    page->text = BlackFlowDetailStrengthAnalyzer::expected_labels("char_4039_horn", "号角");
    expect(!matches(), "wrong canonical operator still fails page identity");
    expect(
        !BlackFlowDetailStrengthAnalyzer(frame).analyze("char_4039_horn", "号角"),
        "wrong detail identity is rejected");
    expect(
        BlackFlowDetailStrengthAnalyzer::expected_labels("char_2012_typhon", "余").empty(),
        "ID-name mismatch has no labels");
    expect(
        BlackFlowDetailStrengthAnalyzer::expected_labels("token_2026_yu", "余").empty(),
        "token cannot acquire character identity");
    page->text = original;
    for (const auto& [id, name] :
         std::vector<std::pair<std::string, std::string>> { { "char_2012_typhon", "提丰" },
                                                            { "char_4039_horn", "号角" },
                                                            { "char_206_gnosis", "灵知" },
                                                            { "char_437_mizuki", "水月" },
                                                            { "char_4230_mcnist", "机械师" } }) {
        const auto good = MAA_NS::imread(good_frames / MAA_NS::to_u16(id) / L"detail-strength.png");
        expect(
            BlackFlowDetailStrengthAnalyzer(good).analyze(id, name).has_value(),
            "existing real detail identity retained");
    }
    const auto ew = BlackFlowAccountTrainingAnalyzer(MAA_NS::imread(ew_frame)).analyze("char_1035_wisdel", "维什戴尔");
    expect(ew.has_value(), "live outside EW detail reads without entering exploration");
    expect(ew && ew->elite == 2 && ew->level == 90, "live outside EW E2L90 observed");
    expect(
        ew && ew->base_skill_level == 7 && ew->mastery_levels == std::array<int, 3> { 3, 3, 3 },
        "live outside EW rank and all three mastery markers observed");
    expect(ew && account_skill_level_for_run(*ew, 2, 1, 80) == 7, "outside M3 is capped for run E1");
    expect(ew && !account_skill_level_for_run(*ew, 3, 1, 80), "outside M3 does not unlock run E1 S3");
    expect(ew && account_skill_level_for_run(*ew, 3, 2, 90) == 10, "outside EW S3 M3 is available for run E2");
    std::ofstream(output / "detail_identity_probe.json") << json::value(
        json::object {
            { "checks", checks },
            { "failures", failures },
            { "gameplay_inputs", 0 },
            { "old_gate_reproduced", true },
            { "source_frame", MAA_NS::from_u16(failed_frame.native()) },
            { "ew_outside_frame", MAA_NS::from_u16(ew_frame.native()) },
            { "ew_outside_training_verified", ew.has_value() },
            { "live_scan_hotpatched", false },
            { "real_UI_retry_verified", false },
        });
    std::cout << "ACCOUNT_DETAIL_IDENTITY109 checks=" << checks << " failures=" << failures << " inputs=0\n";
    return failures ? 1 : 0;
}
