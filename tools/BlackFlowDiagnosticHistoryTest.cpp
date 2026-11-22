#include <filesystem>
#include <iostream>

#include <meojson/json.hpp>

#include "AsstCaller.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Task/Roguelike/BlackFlow/BlackFlowMapObservationSource.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    const std::filesystem::path repo(argv[1]), image_path(argv[2]), out(argv[3]);
    std::filesystem::create_directories(out);
    if (!AsstSetUserDir(MAA_NS::from_u16(out.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str())) {
        return 3;
    }
    const auto image = MAA_NS::imread(image_path);
    if (image.empty()) {
        return 4;
    }
    using namespace asst::blackflow;
    BlackFlowMapObservationSource source;
    BlackFlowObservationRequest observation_request;
    observation_request.floor = 1;
    BlackFlowMapObservation observation;
    FactStore facts;
    std::string error;
    if (!source.recognize(image, observation_request, observation, facts, &error)) {
        std::cerr << error << '\n';
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
    DiagnosticArtifactRequest request;
    request.artifact_set_id = "BF-A62-1";
    request.observation_id = observation.observation_id;
    request.include_images = false;
    request.snapshot = json::object { { "nodes", json::array { 1, 2 } }, { "edges", json::array { 3 } } };
    expect(source.persist_diagnostics(request, &error), "first graph artifact persists");
    request.artifact_set_id = "BF-A62-2";
    request.snapshot = json::object { { "planned_route_steps", json::array { 4 } } };
    expect(source.persist_diagnostics(request, &error), "later route artifact persists on the same floor");
    const auto floor = out / "BlackFlowEvidence/round-BF-A62/floor-1";
    const auto graph = json::open(floor / "snapshots/BF-A62-1.json");
    const auto route = json::open(floor / "snapshots/BF-A62-2.json");
    const auto latest = json::open(floor / "snapshot.json");
    expect(
        graph && graph->contains("nodes") && graph->contains("edges"),
        "later route does not overwrite the original graph");
    expect(route && route->contains("planned_route_steps"), "route is independently replayable");
    expect(
        latest && latest->contains("planned_route_steps") && !latest->contains("nodes"),
        "latest-view contract remains unchanged");
    expect(
        graph && graph->at("accepted_observation_id").as_string() == observation.observation_id,
        "graph is tied to its accepted observation");
    request.artifact_set_id = "BF-A62-3";
    request.observation_id = "stale-observation";
    expect(source.persist_diagnostics(request, &error), "stale request is skipped without a game input");
    expect(
        !std::filesystem::exists(floor / "snapshots/BF-A62-3.json"),
        "stale graph is not attributed to a later observation");
    request.artifact_set_id = "../invalid";
    expect(!source.persist_diagnostics(request, &error), "artifact path validation is retained");
    std::cout << "DIAGNOSTIC_HISTORY checks=" << checks << " failures=" << failures << " device_inputs=0\n";
    return failures ? 1 : 0;
}
