#include "AsstCaller.h"
#include "Config/TaskData.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Vision/Miscellaneous/PipelineAnalyzer.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 9) {
        return 2;
    }
    using namespace asst;
    const std::filesystem::path root(argv[1]), original(argv[2]), current(argv[3]), lobby(argv[4]), battle(argv[5]),
        black(argv[6]), monthly(argv[7]), output(argv[8]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(root.native()).c_str())) {
        return 3;
    }
    for (const auto& path : { original, current, lobby, battle, black }) {
        if (MAA_NS::imread(path).empty()) {
            std::cout << "FAIL required real fixture could not be loaded\n";
            return 5;
        }
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool result, const char* label) {
        ++checks;
        if (!result) {
            ++failures;
            std::cout << "FAIL " << label << '\n';
        }
    };
    const std::string name = "BlackFlow@Roguelike@TerminalDailySupplyDismiss";
    const auto entry = Task.get("BlackFlow@Terminal-Entry");
    const auto daily = Task.get<OcrTaskInfo>(name);
    if (!entry || !daily) {
        return 4;
    }
    const auto analyze = [&](const std::filesystem::path& path, const TaskList& candidates) {
        const auto frame = MAA_NS::imread(path);
        if (frame.empty()) {
            return PipelineAnalyzer::ResultOpt {};
        }
        PipelineAnalyzer analyzer(frame);
        analyzer.set_tasks(candidates);
        return analyzer.analyze();
    };
    auto legacy = entry->on_error_next;
    std::erase(legacy, name);
    std::erase(legacy, "BlackFlow@Roguelike@TerminalMonthlySignInDismiss");
    expect(!analyze(original, legacy), "actual prior terminal fallback reproduces daily supply failure");
    const auto original_result = analyze(original, entry->on_error_next);
    expect(
        original_result && original_result->task_ptr->name == name,
        "real run72 terminal frame selects daily supply recovery");
    const auto current_result = analyze(current, entry->on_error_next);
    expect(
        current_result && current_result->task_ptr->name == name,
        "fresh exact-device daily frame selects same recovery");
    expect(
        daily->full_match && daily->text == std::vector<std::string> { "今日配给" },
        "recovery requires complete positive title");
    expect(
        daily->action == ProcessTaskAction::ClickRect && daily->specific_rect == Rect { 600, 600, 80, 80 },
        "recognized recovery keeps existing confirmation target");
    expect(daily->next == TaskList { "BlackFlow@Terminal-Entry" }, "recovery re-enters pending terminal transaction");
    expect(
        daily->max_times == 2 && daily->exceeded_next == TaskList { "BlackFlow@Roguelike@RecoveryFailed" },
        "recovery is bounded and fails explicitly after limit");
    expect(
        std::ranges::find(legacy, "BlackFlow@Roguelike@TerminalAnnouncementDismiss") != legacy.end(),
        "existing announcement fallback remains present");
    expect(!analyze(lobby, { name }), "real exploration lobby cannot trigger reward confirmation");
    expect(!analyze(battle, { name }), "real battle cannot trigger reward confirmation");
    expect(!analyze(black, { name }), "actual black cached frame cannot trigger reward confirmation");
    const std::string monthly_name = "BlackFlow@Roguelike@TerminalMonthlySignInDismiss";
    const auto monthly_gate = Task.get(monthly_name);
    const auto monthly_result = analyze(monthly, entry->on_error_next);
    expect(
        monthly_result && monthly_result->task_ptr->name == monthly_name,
        "actual post-supply monthly frame enters its recovery");
    expect(
        monthly_gate && monthly_gate->required_page == "BlackFlow@Roguelike@AccountMonthlySignInPage",
        "monthly recovery retains independent regular-supply page gate");
    expect(
        monthly_gate && monthly_gate->max_times == 2 && monthly_gate->next == TaskList { "BlackFlow@Terminal-Entry" },
        "monthly recovery is bounded and returns to pending terminal");
    expect(!analyze(original, { monthly_name }), "daily reward cannot trigger monthly close");
    expect(!analyze(battle, { monthly_name }), "battle cannot trigger monthly close");
    std::ofstream(output / "login_recovery_probe.json") << json::value(
        json::object { { "checks", checks },
                       { "failures", failures },
                       { "gameplay_inputs", 0 },
                       { "old_graph_failure_reproduced", !analyze(original, legacy).has_value() },
                       { "current_daily_page_verified", current_result && current_result->task_ptr->name == name },
                       { "positive_page", name },
                       { "source_frame", MAA_NS::from_u16(current.native()) },
                       { "live_recovery_executed", false },
                       { "saved_exploration_proven", false } });
    std::cout << "LOGIN_RECOVERY113 checks=" << checks << " failures=" << failures << " inputs=0\n";
    return failures ? 1 : 0;
}
