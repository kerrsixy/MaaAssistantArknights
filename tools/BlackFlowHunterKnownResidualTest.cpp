#include <filesystem>
#include <fstream>
#include <iostream>

#include "MaaUtils/ImageIo.h"
#include "Utils/Platform.hpp"
#include "Vision/Roguelike/BlackFlow/BlackFlowHunterPreludeAnalyzer.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path repo(argv[1]), cases_file(argv[2]), output(argv[3]);
    const auto cases = json::open(cases_file);
    if (!cases) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    json::array results;
    for (const auto& entry : cases->as_array()) {
        BlackFlowHunterPreludeAnalyzer analyzer;
        std::string error;
        expect(
            analyzer.load(
                repo / utils::path("resource/template/Roguelike/BlackFlow/HunterPrelude/manifest.json"),
                &error),
            "unchanged production visual manifest loads");
        const auto run = entry.at("run").as_string();
        for (const auto& frame : entry.at("frames").as_array()) {
            const auto record = json::open(utils::path(frame.at("record_file").as_string()));
            const auto& prior = record->at("details").at("observation");
            const auto image = MAA_NS::imread(utils::path(frame.at("image_file").as_string()));
            expect(!image.empty(), "known residual frame remains available");
            if (image.empty()) {
                return 4;
            }
            HunterPreludeFrameContext context;
            context.battle_key = run;
            context.frame_sequence = frame.at("sequence").as_integer();
            context.normal_battle_confirmed = prior.at("detector").get("frame_observable", false);
            context.stage_geometry_verified = true;
            context.spawn_window_closed_verified = prior.get("spawn_window_closed_verified", false);
            context.lower_visibility_verified = prior.get("lower_visibility_verified", false);
            context.battlefield_visibility_verified = prior.get("battlefield_visibility_verified", false);
            context.spawn_only_probe = prior.get("spawn_only_probe", false);
            context.observed_at_ms = prior.at("observed_ms").as_integer();
            const auto actual = analyzer.analyze(image, context);
            expect(!actual.all_hunters_cleared, "actual residual or transition sequence never releases the main team");
            if (run.find("run33") != std::string::npos && context.frame_sequence == 36) {
                expect(
                    actual.distinct_empty_lower_frames == 0,
                    "foreground-occluded true hunter cannot accumulate joint absence");
            }
            json::array spawns, heads;
            for (const auto& spawn : actual.spawns) {
                spawns.emplace_back(
                    json::object { { "presence", static_cast<int>(spawn.presence) },
                                   { "absent", spawn.absent_score },
                                   { "present", spawn.present_score } });
            }
            for (const auto& head : actual.battlefield_head_candidates) {
                heads.emplace_back(json::array { head.x, head.y, head.width, head.height });
            }
            results.emplace_back(
                json::object { { "run", run },
                               { "sequence", context.frame_sequence },
                               { "field_presence", static_cast<int>(actual.battlefield_presence) },
                               { "field_score", actual.battlefield_best_candidate_score },
                               { "joint_absence_frames", actual.distinct_empty_lower_frames },
                               { "cleared", actual.all_hunters_cleared },
                               { "spawns", spawns },
                               { "heads", heads } });
        }
    }
    std::filesystem::create_directories(output);
    std::ofstream(output / utils::path("results.json")) << json::value(results).format();
    std::cout << "KNOWN_RESIDUAL checks=" << checks << " failures=" << failures << " frames=" << results.size()
              << " unchanged_profiles=true connections=0 inputs=0\n";
    return failures ? 1 : 0;
}
