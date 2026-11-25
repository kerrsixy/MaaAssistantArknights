#include "AsstCaller.h"
#include "Config/TaskData.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Task/Roguelike/BlackFlow/BlackFlowMapObservationSource.h"
#include "Utils/Platform.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 5) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path repo(argv[1]), candidate(argv[2]), frame(argv[3]), out(argv[4]);
    std::filesystem::create_directories(out);
    if (!AsstSetUserDir(MAA_NS::from_u16(out.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str()) ||
        !Task.load(candidate / utils::path("resources/task_overlay.json"))) {
        return 3;
    }
    const auto image = MAA_NS::imread(frame);
    if (image.empty()) {
        return 4;
    }
    json::array results;
    int passed = 0;
    for (bool masked : { false, true }) {
        const auto scene = out / (masked ? utils::path("masked") : utils::path("positive"));
        std::filesystem::create_directories(scene);
        if (!AsstSetUserDir(MAA_NS::from_u16(scene.native()).c_str())) {
            return 5;
        }
        auto actual = image.clone();
        if (masked) {
            const auto task = Task.get<OcrTaskInfo>("BlackFlow@Roguelike@MapDifficultyBadge");
            if (!task) {
                return 6;
            }
            actual(make_rect<cv::Rect>(task->roi)).setTo(cv::Scalar::all(0));
        }
        BlackFlowMapObservationSource source;
        source.configure_diagnostics({ DiagnosticLevel::Full, 2 });
        std::string error;
        BlackFlowObservationRequest request;
        request.floor = 4;
        request.run_revision = 2;
        request.verified_difficulty = masked ? 9 : 0;
        BlackFlowMapObservation observation;
        FactStore facts;
        bool recognized = source.prepare(&error) && source.recognize(actual, request, observation, facts, &error);
        DiagnosticArtifactRequest artifact;
        artifact.artifact_set_id = "BF-A130-1";
        artifact.observation_id = observation.observation_id;
        artifact.include_images = true;
        artifact.snapshot = { { "scenario", masked ? "badge_masked" : "actual_badge" } };
        const bool persisted = recognized && source.persist_diagnostics(artifact, &error);
        auto saved = json::open(scene / utils::path("BlackFlowEvidence/round-BF-A130/floor-4/snapshot.json"));
        bool valid = false;
        if (persisted && saved) {
            const auto& d = saved->at("map_difficulty_observation");
            const auto& weather = saved->at("weather_recognition");
            const auto& metadata = weather.at("main_weather").at("metadata");
            const bool map_still_recognized = observation.recognition_ok &&
                                              observation.current_marker_temporary_id >= 0 &&
                                              weather.at("map_verified").as_boolean();
            const bool stays_unconfirmed = !observation.floor_buff_scope_confirmed &&
                                           observation.floor_buff_id.empty() &&
                                           !weather.at("scope").at("known").as_boolean() &&
                                           weather.at("main_weather").at("state").as_string() == "glyph_candidate";
            const bool bound_frame = d.at("frame_sequence").as_integer() == static_cast<int>(observation.sequence) &&
                                     d.at("current_map_verified").as_boolean();
            if (masked) {
                valid = map_still_recognized && stays_unconfirmed && bound_frame && d.at("level").is_null() &&
                        d.at("prior_menu_level").as_integer() == 9 && !d.at("source_disagreement").as_boolean() &&
                        !metadata.is_null() && metadata.at("level").is_null() &&
                        metadata.at("body").as_string().empty();
            }
            else {
                valid = map_still_recognized && stays_unconfirmed && bound_frame && d.at("level").as_integer() == 9 &&
                        d.at("prior_menu_level").as_integer() == 0 && d.at("source_disagreement").as_boolean() &&
                        !metadata.is_null() && metadata.at("id").as_string() == "rogue_6_weather_8_b" &&
                        metadata.at("level").as_integer() == 2 &&
                        metadata.at("level_from_verified_difficulty").as_boolean() &&
                        !metadata.at("body").as_string().empty();
            }
        }
        results.emplace_back(
            json::object { { "scene", masked ? "masked_badge" : "actual_badge" },
                           { "recognized", recognized },
                           { "persisted", persisted },
                           { "passed", valid },
                           { "error", error },
                           { "actual_snapshot", saved ? *saved : json::value(nullptr) },
                           { "expected_floor_key_contract", weather_floor_key(request.run_revision, request.floor) },
                           { "floor_key_contract_is_not_runtime_serialized_receipt", true } });
        passed += valid ? 1 : 0;
        source.release();
    }
    std::ofstream(out / utils::path("map_source_results.json")) << json::value(results).format();
    std::cout << "MAPSOURCE130 scenes=2 passed=" << passed << " failures=" << (2 - passed)
              << " device_inputs=0 scope_claimed=false\n";
    return passed == 2 ? 0 : 1;
}
