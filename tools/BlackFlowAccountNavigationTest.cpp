#include "AsstCaller.h"
#include "Config/TaskData.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Vision/Miscellaneous/PipelineAnalyzer.h"
#include <filesystem>
#include <iostream>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 6) {
        return 2;
    }
    const std::filesystem::path repo(argv[1]), baseline(argv[2]), overlay(argv[3]), run(argv[4]), output(argv[5]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str()) || !asst::Task.load(baseline)) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto check = [&](const cv::Mat& frame, const std::string& expected, const char* label) {
        asst::PipelineAnalyzer analyzer(frame);
        analyzer.set_tasks(asst::Task.get("BlackFlow@Roguelike@AccountBegin")->next);
        const auto result = analyzer.analyze();
        const auto actual = result ? result->task_ptr->name : "";
        ++checks;
        failures += actual != expected;
        std::cout << label << " expected=" << expected << " actual=" << actual << '\n';
    };
    const auto initial = MAA_NS::imread(run / "initial_screen.png");
    const auto final = MAA_NS::imread(run / "final_screen.png");
    if (initial.empty() || final.empty()) {
        return 4;
    }
    const std::string back = "BlackFlow@Roguelike@AccountHome@ReturnButton";
    const std::string quick = "BlackFlow@Roguelike@AccountHome@QuickSwitch@ToHome@Open";
    check(initial, back, "OriginalLobbyBackPriority");
    // The outer chain already finds QuickHome here; the lobby had entered
    // ReturnButton's nested loop and could no longer reach this recovery.
    check(final, quick, "OriginalGalleryOuterChainCanRecover");
    if (!asst::Task.load(overlay)) {
        return 5;
    }
    check(initial, quick, "FixedLobbyQuickHomePriority");
    check(final, quick, "FixedGalleryQuickHomePriority");
    check(cv::Mat::zeros(initial.size(), initial.type()), "", "BlankDoesNotNavigate");
    std::cout << "NAV83 checks=" << checks << " failures=" << failures << " device_inputs=0 live_verified=0\n";
    return failures ? 1 : 0;
}
