#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "Arknights-Tile-Pos/TileCalc2.hpp"
#include "MaaUtils/ImageIo.h"
#include "Task/Roguelike/BlackFlow/BlackFlowHunterPreludeController.h"
#include "Utils/Platform.hpp"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 6) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path repo(argv[1]), records(argv[2]), stage_file(argv[3]), output(argv[4]);
    const bool require_release = std::wstring_view(argv[5]) == L"release";
    const auto stage = json::open(stage_file);
    const auto level_json = json::open(
        repo / utils::path(
                   "resource/Arknights-Tile-Pos/"
                   "ro6_n_2_5-obt-roguelike-ro6-level_rogue6_2-5.json"));
    if (!stage || !level_json) {
        return 3;
    }
    std::string error;
    const auto opening = HunterOpening::parse(*stage, &error);
    if (!opening) {
        std::cerr << error;
        return 4;
    }
    std::vector<std::pair<int, std::filesystem::path>> files;
    for (const auto& entry : std::filesystem::directory_iterator(records)) {
        if (entry.path().extension() != L".json") {
            continue;
        }
        const auto value = json::open(entry.path());
        if (value && value->get("what", "") == "BlackFlowHunterPrelude") {
            files.emplace_back(value->at("details").at("sequence").as_integer(), entry.path());
        }
    }
    std::ranges::sort(files);
    if (files.empty()) {
        return 5;
    }
    const Map::Level level(*level_json);
    const auto manifest = repo / utils::path("resource/template/Roguelike/BlackFlow/HunterPrelude/manifest.json");
    const auto raw = repo / utils::path("resource/roguelike/BlackFlow/stage_mechanics/branch_trap.json");
    int checks = 0, failures = 0;
    const auto expect = [&](bool result, const char* label) {
        ++checks;
        if (!result) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    json::array results;
    const auto replay = [&](bool paused) {
        BlackFlowHunterPreludeController controller;
        if (!controller.prepare(manifest, raw, level, *opening, paused ? "paused-replay" : "actual-replay", &error)) {
            std::cerr << error;
            return -1;
        }
        int attempts = 0, first_release = 0;
        for (const auto& [sequence, path] : files) {
            const auto value = json::open(path);
            const auto& observation = value->at("details").at("observation");
            const int recorded_attempts = observation.get("bait_attempts", 0);
            const std::string name = observation.get("bait_name", "");
            const auto role = name == "机械师" ? battle::Role::Tank
                              : name == "白雪" ? battle::Role::Sniper
                                               : battle::Role::Caster;
            const battle::OperNameTag bait { role, name };
            const int index = observation.get("bait_plan_index", 0);
            const auto tile_json = observation.find<json::array>("bait_location");
            const Point tile =
                tile_json ? Point((*tile_json)[0].as_integer(), (*tile_json)[1].as_integer()) : Point(3, 4);
            if (recorded_attempts > attempts) {
                if (!paused) {
                    controller.deployed(index, bait, tile);
                }
                attempts = recorded_attempts;
            }
            std::map<battle::OperNameTag, Point> field;
            if (observation.get("bait_present", false)) {
                field.emplace(bait, tile);
            }
            std::vector<battle::DeploymentOper> cards;
            const int matches = observation.get("bait_matching_card_count", 0);
            for (int i = 0; i < matches; ++i) {
                const bool cooling = observation.get("bait_cooling_verified", false);
                cards.push_back({ .role = role, .available = !cooling, .cooling = cooling, .name = name });
            }
            auto image_path = path;
            image_path.replace_extension(L".png");
            const auto image = MAA_NS::imread(image_path);
            if (image.empty()) {
                std::cerr << "Missing replay input " << path << '\n';
                return -1;
            }
            const auto result = controller.observe(
                image,
                !paused,
                field,
                cards,
                static_cast<std::uint64_t>(observation.at("observed_ms").as_integer()));
            const auto& detector = result.diagnostics.at("detector");
            const bool positive =
                detector.get("battlefield_presence", 0) == static_cast<int>(HunterPresence::Present) ||
                std::ranges::any_of(detector.at("spawns").as_array(), [](const auto& spawn) {
                    return spawn.get("presence", 0) == static_cast<int>(HunterPresence::Present);
                });
            expect(!positive || !result.main_allowed, "positive hunter evidence always vetoes clearance");
            expect(!paused || !result.main_allowed, "paused observations never authorize main team");
            if (result.main_allowed && first_release == 0) {
                first_release = sequence;
            }
            if (!paused) {
                results.emplace_back(
                    json::object { { "sequence", sequence },
                                   { "main_allowed", result.main_allowed },
                                   { "diagnostics", result.diagnostics } });
                std::cout << "SEQ " << sequence << " release=" << result.main_allowed << '\n';
            }
        }
        return first_release;
    };
    const int first_release = replay(false);
    expect(first_release >= 0, "all actual paired native frames exist and prepare");
    expect(
        require_release ? first_release > 0 : first_release == 0,
        "specified real sequence releases only when expected");
    if (require_release && first_release > 0) {
        expect(first_release >= 37, "real lower hunter at sequence36 cannot release early");
    }
    // Whole actor pause policy is rechecked on the actual initial and final
    // frames; replaying paused post-deployment records does not invent legal
    // deployments.
    BlackFlowHunterPreludeAnalyzer paused;
    expect(paused.load(manifest, &error), "pause regression detector loads unchanged visual profiles");
    HunterPreludeFrameContext context;
    context.battle_key = "guard-regression";
    context.normal_battle_confirmed = true;
    context.stage_geometry_verified = true;
    context.spawn_window_closed_verified = true;
    context.lower_visibility_verified = true;
    context.battlefield_visibility_verified = true;
    context.frame_sequence = 1;
    context.observed_at_ms = 100;
    auto first_image = files.front().second;
    first_image.replace_extension(L".png");
    const auto positive = paused.analyze(MAA_NS::imread(first_image), context);
    expect(
        positive.battlefield_presence == HunterPresence::Present,
        "actual initial hunter image primes positive history for guard tests");
    HunterPreludeObservation clear;
    for (const auto& [sequence, path] : files) {
        if (sequence < 37) {
            continue;
        }
        auto clear_image = path;
        clear_image.replace_extension(L".png");
        ++context.frame_sequence;
        context.observed_at_ms += 1000;
        clear = paused.analyze(MAA_NS::imread(clear_image), context);
    }
    expect(
        !require_release || clear.all_hunters_cleared,
        "three actual negative frames reach candidate clearance before counterfactual pause");
    auto last = files.back().second;
    last.replace_extension(L".png");
    const auto image = MAA_NS::imread(last);
    ++context.frame_sequence;
    context.observed_at_ms += 1000;
    context.normal_battle_confirmed = false;
    expect(!paused.analyze(image, context).all_hunters_cleared, "paused view revokes an actually reached clearance");
    context.normal_battle_confirmed = true;
    for (int i = 0; i < 4; ++i) {
        ++context.frame_sequence;
        context.observed_at_ms += 1000;
        expect(
            !paused.analyze(image, context).all_hunters_cleared,
            "repeated cached image cannot regain clearance after pause");
    }
    ++context.frame_sequence;
    expect(
        !paused.analyze(image, context).all_hunters_cleared,
        "nonmonotonic timestamp cannot certify current absence");
    ++context.frame_sequence;
    context.observed_at_ms += 1000;
    context.occlusion_rois = { { 136, 71, 914, 511 } };
    expect(!paused.analyze(image, context).all_hunters_cleared, "explicit UI cover revokes complete visibility");
    context.occlusion_rois.clear();
    context.battlefield_visibility_verified = false;
    ++context.frame_sequence;
    context.observed_at_ms += 1000;
    expect(!paused.analyze(image, context).all_hunters_cleared, "missing full visibility remains unknown");
    context.battlefield_visibility_verified = true;
    context.spawn_window_closed_verified = false;
    ++context.frame_sequence;
    context.observed_at_ms += 1000;
    expect(!paused.analyze(image, context).all_hunters_cleared, "unclosed spawn window cannot release the main team");
    std::filesystem::create_directories(output);
    std::ofstream(output / utils::path("replay.json")) << json::value(results).format();
    std::cout << "HUNTER_REPLAY checks=" << checks << " failures=" << failures << " frames=" << files.size()
              << " first_release=" << first_release
              << " connections=0 inputs=0 modeled_actor_receipts=true "
                 "live_pass=false\n";
    return failures ? 1 : 0;
}
