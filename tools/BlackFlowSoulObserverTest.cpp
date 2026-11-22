#include <chrono>
#include <filesystem>
#include <iostream>

#include <meojson/json.hpp>

#include "MaaUtils/ImageIo.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Utils/Platform.hpp"
#include "Vision/Roguelike/BlackFlow/BlackFlowSoulObserver.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path root(argv[1]);
    const auto fixtures = json::open(root / "fixtures.json");
    if (!fixtures) {
        return 3;
    }
    BlackFlowSoulObserver observer;
    std::string error;
    if (!observer.load(root / "assets", &error)) {
        std::cerr << error << '\n';
        return 4;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool value, const std::string& label) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    cv::setNumThreads(1);
    const auto started = std::chrono::steady_clock::now();
    for (const auto& test : fixtures->at("cases").as_array()) {
        auto image = MAA_NS::imread(utils::path(test.at("path").as_string()));
        if (image.size() == cv::Size(640, 360)) {
            cv::resize(image, image, { 1280, 720 }, 0, 0, cv::INTER_LINEAR);
        }
        const auto result = observer.boss(image);
        expect(result.alive() == test.at("alive_expected").as_boolean(), test.at("label").as_string());
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    for (const auto& test : fixtures->at("selected_cases").as_array()) {
        auto image = MAA_NS::imread(utils::path(test.at("path").as_string()));
        if (image.size() == cv::Size(640, 360)) {
            cv::resize(image, image, { 1280, 720 }, 0, 0, cv::INTER_LINEAR);
        }
        const auto evidence = observer.boss(image, true);
        if (evidence.alive()) {
            std::cout << "SELECTED_ALIVE " << test.at("label").as_string() << " score=" << evidence.score << '\n';
        }
        expect(evidence.alive() == test.at("alive_expected").as_boolean(), test.at("label").as_string());
        if (test.at("label").as_string() == "run40_f12") {
            image(cv::Rect(510, 435, 32, 9)).setTo(cv::Scalar(30, 80, 100));
            expect(
                !observer.boss(image, true).alive(),
                "body plus foot marker without real HP cannot authorize retreat");
        }
    }
    const auto soul_image = MAA_NS::imread(utils::path(fixtures->at("soul_frame").as_string()));
    std::vector<SoulSite> sites;
    for (const auto& entry : fixtures->at("sites").as_array()) {
        sites.push_back(
            { { entry.at("tile")[0].as_integer(), entry.at("tile")[1].as_integer() },
              { entry.at("pixel")[0].as_integer(), entry.at("pixel")[1].as_integer() } });
    }
    const auto souls = observer.souls(soul_image, sites);
    for (const auto& entry : fixtures->at("pause_cases").as_array()) {
        const auto image = MAA_NS::imread(utils::path(entry.at("path").as_string()));
        expect(
            observer.paused(image) == entry.at("paused").as_boolean(),
            "real paused play icon is distinct from running and selected views");
    }
    expect(souls == std::vector<Point> { { 7, 5 } }, "known soul proposes its actual projected tile");
    expect(observer.souls(soul_image, {}).empty(), "unqualified sites cannot propose a selection");
    expect(!observer.boss({}).alive(), "missing image never proves alive");
    std::cout << "SOUL_OBSERVER checks=" << checks << " failures=" << failures
              << " device_inputs=0 elapsed_ms=" << elapsed << '\n';
    return failures ? 1 : 0;
}
