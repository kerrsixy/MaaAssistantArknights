#include <filesystem>
#include <iostream>

#include "Assistant.h"
#include "AsstCaller.h"
#include "Config/TaskData.h"
#include "MaaUtils/Encoding.h"
#include "Task/Roguelike/BlackFlow/BlackFlowRoutingTaskPlugin.h"
#include "Utils/Platform.hpp"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path repo(argv[1]), out(argv[2]);
    std::filesystem::create_directories(out);
    if (!AsstSetUserDir(MAA_NS::from_u16(out.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str())) {
        return 3;
    }
    const auto handle = AsstCreate();
    if (handle == nullptr) {
        return 5;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool value, const char* label) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    auto session = std::make_shared<BlackFlowSession>();
    expect(session->initialize("ending1"), "real session initializes without invented floor");
    expect(!session->current_floor().has_value(), "fresh restored session has unknown floor");
    auto config = std::make_shared<RoguelikeConfig>();
    BlackFlowRoutingTaskPlugin
        plugin({}, reinterpret_cast<Assistant*>(handle), "Roguelike", config, nullptr, session, nullptr);
    const json::value prepared = json::object {
        { "subtask", "ProcessTask" },
        { "details", json::object { { "task", "BlackFlow@Roguelike@MapPrepare-Ready" } } },
    };
    const auto prepare = [&]() {
        expect(plugin.verify(AsstMsg::SubTaskCompleted, prepared), "real map-prepared callback selects pending work");
        expect(plugin.run(), "map preparation redirects without controller or perception calls");
        return Task.get("BlackFlow@Roguelike@MapPrepare-ReadyAction");
    };
    auto task = prepare();
    expect(task && task->algorithm == AlgorithmType::JustReturn, "unknown floor uses a forwarding leaf");
    expect(
        task && task->next == TaskList { "BlackFlow@Roguelike@NextLevel" },
        "real NextLevel name receives lifecycle callback");
    expect(!session->current_floor().has_value(), "no floor is guessed from gameplay stage or saved metadata");
    expect(session->set_current_floor(1), "actual first-floor receipt accepted");
    task = prepare();
    expect(task && task->next == TaskList { "BlackFlow@Roguelike@Routing" }, "known floor continues normal routing");
    expect(session->set_current_floor(5), "actual fifth-floor receipt accepted");
    task = prepare();
    const auto fifth = Task.get("BlackFlow@Roguelike@MapPrepare-Floor5Swipe");
    expect(
        task && fifth && task->action == fifth->action && task->next == fifth->next,
        "fifth-floor viewport behavior retained");
    session->clear_current_floor();
    task = prepare();
    expect(
        task && task->next == TaskList { "BlackFlow@Roguelike@NextLevel" },
        "cleared identity is recognized again before routing");
    Task.set_task_base("BlackFlow@Roguelike@RoutingAction", "BlackFlow@Roguelike@MapViewportReset-Dispatch");
    task = Task.get("BlackFlow@Roguelike@RoutingAction");
    expect(
        task && task->algorithm == AlgorithmType::JustReturn,
        "viewport recovery alias cannot request a missing child template");
    expect(
        task && task->next == TaskList { "BlackFlow@Roguelike@MapViewportReset-Enter" },
        "actual map matcher is dispatched under its own name");
    const auto matcher = Task.get<MatchTaskInfo>("BlackFlow@Roguelike@MapViewportReset-Enter");
    expect(
        matcher && matcher->templ_names == std::vector<std::string> { "BlackFlow@Roguelike@MapZoomIn.png",
                                                                      "BlackFlow@Roguelike@MapZoomOut.png" },
        "map recovery retains actual zoom templates");
    AsstDestroy(handle);
    std::cout << "MAP_FLOOR checks=" << checks << " failures=" << failures << " connections=0 inputs=0\n";
    return failures ? 1 : 0;
}
