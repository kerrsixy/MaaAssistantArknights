#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include "Arknights-Tile-Pos/TileCalc2.hpp"
#include "MaaUtils/ImageIo.h"
#include "Task/Roguelike/BlackFlow/BlackFlowHunterPreludeController.h"
#include "Utils/Platform.hpp"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 5 && argc != 6) {
        return 2;
    }
    const bool overlap = argc == 6 && std::wstring_view(argv[5]) == L"run32-overlap";
    if (argc == 6 && !overlap) {
        return 2;
    }
#ifdef _MSC_VER
    _CrtSetReportHook([](int type, char* message, int*) {
        if (type == _CRT_ASSERT) {
            std::fputs(message, stderr);
            std::_Exit(86);
        }
        return 0;
    });
#endif
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path repo(argv[1]), frames(argv[2]), manifest(argv[3]), out(argv[4]);
    std::filesystem::create_directories(out);
    const auto level_json =
        json::open(repo / utils::path("resource/Arknights-Tile-Pos/ro6_n_2_5-obt-roguelike-ro6-level_rogue6_2-5.json"));
    if (!level_json) {
        return 3;
    }
    const Map::Level level(*level_json);
    std::vector<std::pair<int, std::filesystem::path>> files;
    for (const auto& file : std::filesystem::directory_iterator(frames)) {
        if (file.path().extension() != L".json") {
            continue;
        }
        const auto data = json::open(file.path());
        if (data && data->get("what", "") == "BlackFlowHunterPrelude") {
            files.emplace_back(data->at("details").at("sequence").as_integer(), file.path());
        }
    }
    std::ranges::sort(files);
    if (files.size() != (overlap ? 33 : 24)) {
        return 4;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << message << '\n';
        }
    };
    json::array results;
    const auto replay = [&](bool heldout_only, bool paused) {
        BlackFlowHunterPreludeController controller;
        std::string error;
        const bool prepared = controller.prepare(
            manifest,
            repo / utils::path("resource/roguelike/BlackFlow/stage_mechanics/branch_trap.json"),
            level,
            0,
            overlap ? "run32-actual-replay" : "run31-actual-replay",
            2,
            &error);
        expect(prepared, "actual native geometry and opening spawn contract prepare");
        if (!prepared) {
            return 0;
        }
        int recorded_attempts = 0, first_release = 0, first_retry = 0;
        for (const auto& [sequence, path] : files) {
            if (heldout_only && (sequence == 13 || sequence == 14)) {
                continue;
            }
            const auto data = json::open(path);
            const auto& observation = data->at("details").at("observation");
            const int attempts = observation.get("bait_attempts", 0);
            const std::string name = observation.get("bait_name", "");
            const battle::OperNameTag bait { name == "白雪" ? battle::Role::Sniper : battle::Role::Caster, name };
            if (attempts > recorded_attempts) {
                expect(attempts == recorded_attempts + 1, "recorded confirmed deployment consumes one bounded attempt");
                controller.deployed(0, bait, { 3, 4 });
                recorded_attempts = attempts;
            }
            std::map<battle::OperNameTag, Point> battlefield;
            if (observation.get("bait_present", false)) {
                battlefield.emplace(bait, Point { 3, 4 });
            }
            std::vector<battle::DeploymentOper> cards;
            if (observation.get("bait_matching_card_count", 0) == 1) {
                const bool cooling = observation.get("bait_cooling_verified", false);
                cards.emplace_back(
                    battle::DeploymentOper { .role = bait.role,
                                             .available = !cooling && battlefield.empty(),
                                             .cooling = cooling,
                                             .name = name });
            }
            auto image_path = path;
            image_path.replace_extension(L".png");
            const auto image = MAA_NS::imread(image_path);
            expect(!image.empty(), "actual paired native image exists");
            // These are original timestamps and recorded actor facts, not video timers or kill-count gates.
            const auto observed_ms = static_cast<std::uint64_t>(observation.at("observed_ms").as_integer());
            const bool normal = !(paused && sequence >= 13);
            const auto decision = controller.observe(image, normal, battlefield, cards, observed_ms);
            const bool retry = attempts > 0 && controller.allows_plan(0);
            if (retry && first_retry == 0) {
                first_retry = sequence;
            }
            if (decision.main_allowed && first_release == 0) {
                first_release = sequence;
            }
            if (sequence <= (overlap ? 31 : 12) || !normal) {
                expect(!decision.main_allowed, "real residual hunters or paused context cannot release main operators");
            }
            if (overlap && (sequence < 5 || !normal)) {
                expect(!retry, "unverified bait retirement or paused context cannot retry bait");
            }
            if (overlap && sequence >= 28 && sequence <= 31 && normal) {
                expect(
                    decision.diagnostics.at("detector").get("battlefield_presence", 0) != 2,
                    "independent moving-hunter and obscured frames cannot be reported absent");
            }
            results.emplace_back(
                json::object { { "sequence", sequence },
                               { "heldout_only", heldout_only },
                               { "paused", paused },
                               { "original_timestamp_ms", observed_ms },
                               { "main_allowed", decision.main_allowed },
                               { "replacement_bait_allowed", retry },
                               { "diagnostics", decision.diagnostics } });
        }
        if (overlap) {
            expect(
                first_retry >= 5 && first_retry <= 17,
                "localized positive overlap evidence enables the bounded second bait after verified retirement");
            expect(first_release == 0, "run32 has no three-frame fully visible empty-spawn quorum");
        }
        else if (paused) {
            expect(first_release == 0, "paused absence images never release");
        }
        else {
            expect(first_release > 12, "visible timed whole-field absence eventually releases main operators");
            if (heldout_only) {
                expect(
                    first_release >= 17,
                    "release after excluded calibration/exploration frames needs new temporal quorum");
            }
        }
        return first_release;
    };
    const int full_release = replay(false, false);
    int heldout_release = 0;
    if (overlap) {
        // Removing the pre-deployment first frame cannot reconstruct the controller's
        // earlier approval. Validate held-out appearance independently, without importing
        // a deployment into an unobserved controller or fabricating an earlier frame.
        BlackFlowHunterPreludeAnalyzer analyzer;
        std::string error;
        expect(analyzer.load(manifest, &error), "held-out native visual analyzer loads the same validated model");
        for (const auto& [sequence, path] : files) {
            if (sequence < 5) {
                continue;
            }
            const auto data = json::open(path);
            const auto& observation = data->at("details").at("observation");
            expect(
                !observation.get("bait_present", true) && observation.get("battlefield_unit_occlusion_count", -1) == 0,
                "held-out fixture has no recorded allied occlusion after verified bait retirement");
            auto image_path = path;
            image_path.replace_extension(L".png");
            const auto image = MAA_NS::imread(image_path);
            HunterPreludeFrameContext context;
            context.battle_key = "run32-independent-visual";
            context.frame_sequence = sequence;
            context.normal_battle_confirmed = true;
            // The full controller replay above validates this same raw stage and
            // opening-only spawn contract. Visibility is imported from recorded facts.
            context.stage_geometry_verified = true;
            context.spawn_window_closed_verified = true;
            context.lower_visibility_verified = observation.get("lower_visibility_verified", false);
            context.battlefield_visibility_verified = observation.get("battlefield_visibility_verified", false);
            context.observed_at_ms = static_cast<std::uint64_t>(observation.at("observed_ms").as_integer()) + 1;
            const auto visual = analyzer.analyze(image, context);
            expect(visual.frame_observable, "independent actual image retains verified stage anchors");
            if (sequence <= 17) {
                expect(
                    visual.spawns[0].presence != HunterPresence::Absent,
                    "a real early overlapping hunter cannot become an empty spawn");
            }
            if (sequence == 5 || sequence == 7 || sequence == 10 || sequence == 17) {
                expect(
                    visual.spawns[0].presence == HunterPresence::Present,
                    "independent strong overlap views localize a positive head inside the left spawn");
            }
            if (sequence >= 28 && sequence <= 31) {
                expect(
                    visual.battlefield_presence != HunterPresence::Absent,
                    "held-out moving hunter or obscuration remains present or unknown");
            }
            expect(!visual.all_hunters_cleared, "two final clear images cannot certify the missing empty-spawn quorum");
            results.emplace_back(
                json::object {
                    { "sequence", sequence },
                    { "heldout_only", true },
                    { "independent_visual_only", true },
                    { "paused", false },
                    { "battlefield_presence", static_cast<int>(visual.battlefield_presence) },
                    { "left_spawn_presence", static_cast<int>(visual.spawns[0].presence) },
                    { "all_hunters_cleared", visual.all_hunters_cleared },
                });
        }
    }
    else {
        heldout_release = replay(true, false);
    }
    static_cast<void>(replay(false, true));
    std::ofstream(out / "replay.json") << json::value(results).to_string();
    std::cout << "HUNTER_CLEARANCE scenario=" << (overlap ? "run32-overlap" : "run31-clear") << " checks=" << checks
              << " failures=" << failures << " native_frames=" << files.size() << " full_release=" << full_release
              << " heldout_release=" << heldout_release << " independent_visual_only=" << overlap
              << " connections=0 input=0 live_clear_proven=false\n";
    return failures ? 1 : 0;
}
