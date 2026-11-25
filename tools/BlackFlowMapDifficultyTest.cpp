#include "AsstCaller.h"
#include "Config/TaskData.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Utils/Platform.hpp"
#include "Vision/Roguelike/BlackFlow/BlackFlowMapDifficultyAnalyzer.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path repo(argv[1]), candidate(argv[2]), out(argv[3]);
    std::filesystem::create_directories(out);
    if (!AsstSetUserDir(MAA_NS::from_u16(out.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str()) ||
        !Task.load(candidate / utils::path("resources/task_overlay.json"))) {
        return 3;
    }
    auto fixture = json::open(candidate / utils::path("fixtures/maps.json"));
    if (!fixture) {
        return 4;
    }
    int checks = 0, failures = 0;
    std::uint64_t seq = 0;
    json::array reads;
    const auto ck = [&](bool v, const char* label) {
        ++checks;
        if (!v) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    cv::Mat last;
    for (const auto& f : fixture->as_array()) {
        const auto image = MAA_NS::imread(utils::path(f.at("path").as_string()));
        if (image.empty()) {
            return 5;
        }
        const auto before = std::chrono::steady_clock::now();
        const auto observation = observe_map_difficulty(image, true, ++seq, 0);
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - before).count();
        auto row = observation.to_json().as_object();
        row["source"] = f.at("source");
        row["actual_original"] = true;
        row["ocr_us"] = elapsed;
        reads.emplace_back(row);
        ck(observation.level == f.at("level").as_integer(), "real map badge actually reads9");
        ck(observation.prior_menu_level == 0 && observation.source_disagreement,
           "prior menu0 remains distinct and reported");
        ck(observation.frame_sequence == seq && observation.current_map_verified, "current frame provenance retained");
        const auto no_map = observe_map_difficulty(image, false, ++seq, 9);
        ck(!no_map.level, "visible9 without positive current map never authorizes grade");
        auto mask = image.clone();
        const auto task = Task.get<OcrTaskInfo>("BlackFlow@Roguelike@MapDifficultyBadge");
        mask(make_rect<cv::Rect>(task->roi)).setTo(cv::Scalar::all(0));
        const auto hidden = observe_map_difficulty(mask, true, ++seq, 9);
        ck(!hidden.level, "missing badge does not borrow prior menu9");
        last = image;
    }
    const auto same = observe_map_difficulty(last, true, ++seq, 9);
    ck(same.level == 9 && !same.source_disagreement, "matching old menu is diagnostic agreement");
    const auto without_menu = observe_map_difficulty(last, true, ++seq, std::nullopt);
    ck(without_menu.level == 9 && !without_menu.prior_menu_level,
       "actual badge works without configured or prior difficulty");
    ck(!observe_map_difficulty(last, true, 0, 9).level, "missing frame identity unknown");
    ck(!observe_map_difficulty({}, true, ++seq, 9).level, "empty image unknown despite prior9");
    auto clipped = last(cv::Rect(0, 0, 1279, 720));
    ck(!observe_map_difficulty(clipped, true, ++seq, 9).level, "wrong-size image cannot use fixed native badge ROI");
    ck(weather_floor_key(2, 3) == "BF-R2-F3", "run and floor identity both represented");
    ck(weather_floor_key(2, 3) != weather_floor_key(3, 3), "same floor new run has different temporal context");
    ck(weather_floor_key(2, 3) != weather_floor_key(2, 4), "same run new floor has different context");
    ck(weather_floor_key(0, 3).empty() && weather_floor_key(2, 0).empty() && weather_floor_key(2, 6).empty(),
       "unverified identity does not get invented key");
    std::ofstream(out / utils::path("badge_results.json")) << json::value(reads).format();
    std::cout << "MAP130 checks=" << checks << " failures=" << failures << " device_inputs=0 scope_claimed=false\n";
    return failures ? 1 : 0;
}
