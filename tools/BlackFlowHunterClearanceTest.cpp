#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include "Arknights-Tile-Pos/TileCalc2.hpp"
#include "MaaUtils/ImageIo.h"
#include "Task/Roguelike/BlackFlow/BlackFlowHunterPreludeController.h"
#include "Utils/Platform.hpp"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 5) {
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
    if (files.size() != 24) {
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
            "run31-actual-replay",
            2,
            &error);
        expect(prepared, "actual native geometry and opening spawn contract prepare");
        if (!prepared) {
            return 0;
        }
        int recorded_attempts = 0, first_release = 0;
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
            if (decision.main_allowed && first_release == 0) {
                first_release = sequence;
            }
            if (sequence <= 12 || !normal) {
                expect(!decision.main_allowed, "real residual hunters or paused context cannot release main operators");
            }
            results.emplace_back(
                json::object { { "sequence", sequence },
                               { "heldout_only", heldout_only },
                               { "paused", paused },
                               { "original_timestamp_ms", observed_ms },
                               { "main_allowed", decision.main_allowed },
                               { "diagnostics", decision.diagnostics } });
        }
        if (paused) {
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
    const int heldout_release = replay(true, false);
    static_cast<void>(replay(false, true));
    std::ofstream(out / "replay.json") << json::value(results).to_string();
    std::cout << "HUNTER_CLEARANCE checks=" << checks << " failures=" << failures << " native_frames=" << files.size()
              << " full_release=" << full_release << " heldout_release=" << heldout_release
              << " connections=0 input=0 live_clear_proven=false\n";
    return failures ? 1 : 0;
}
