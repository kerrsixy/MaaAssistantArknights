#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
    if (argc != 3) {
        return 2;
    }
#ifdef _MSC_VER
    _CrtSetReportHook([](int type, char* message, int*) {
        if (type == _CRT_ASSERT) {
            std::fputs(message, stderr);
            std::fflush(stderr);
            std::_Exit(86);
        }
        return 0;
    });
#endif
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path repo(argv[1]), evidence(argv[2]);
    const auto level_json =
        json::open(repo / utils::path("resource/Arknights-Tile-Pos/ro6_n_2_5-obt-roguelike-ro6-level_rogue6_2-5.json"));
    const auto frames = json::open(evidence);
    if (!level_json || !frames || frames->as_array().size() < 3) {
        return 3;
    }
    const Map::Level level(*level_json);
    const auto image = [&](int index) {
        return MAA_NS::imread(utils::path(frames->at(index).at("image_file").as_string()));
    };
    const auto manifest = repo / utils::path("resource/template/Roguelike/BlackFlow/HunterPrelude/manifest.json");
    const auto raw = repo / utils::path("resource/roguelike/BlackFlow/stage_mechanics/branch_trap.json");
    const battle::OperNameTag carda { battle::Role::Caster, "卡达" }, typhon { battle::Role::Sniper, "提丰" };
    battle::DeploymentOper cooling { .role = carda.role, .cooling = true, .name = carda.name };
    auto ready = cooling;
    ready.cooling = false;
    ready.available = true;
    battle::DeploymentOper replacement { .role = typhon.role, .available = true, .name = typhon.name };
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << message << '\n';
        }
    };
    const auto start = [&](BlackFlowHunterPreludeController& controller, int limit) {
        std::string error;
        const bool prepared = controller.prepare(manifest, raw, level, 0, "retry-replay", limit, &error);
        expect(prepared, "real stage geometry and bounded retry prepare");
        if (!prepared) {
            std::cerr << error << '\n';
            return false;
        }
        const auto first = controller.observe(image(0), true, {}, {}, 100);
        expect(!first.main_allowed && controller.allows_plan(0), "actual first frame permits only the initial bait");
        controller.deployed(0, carda, { 3, 4 });
        return true;
    };
    BlackFlowHunterPreludeController retry;
    if (!start(retry, 2)) {
        return 4;
    }
    auto result = retry.observe(image(1), true, {}, { cooling, replacement }, 1000);
    expect(
        result.diagnostics.get("replacement_bait_allowed", false),
        "real upper residual plus confirmed removal permits replacement review");
    expect(
        retry.allows_plan(0) && !retry.allows_plan(1) && !result.main_allowed,
        "replacement permission never releases the main team");
    result = retry.observe(image(2), true, {}, { ready, replacement }, 2000);
    expect(retry.allows_plan(0), "verified removal remains valid when the original card becomes ready");
    result = retry.observe(image(2), true, {}, { ready, replacement }, 2000);
    expect(!retry.allows_plan(0) && !result.main_allowed, "a stale capture revokes previous replacement permission");
    result = retry.observe(image(1), true, {}, { cooling, replacement }, 2500);
    expect(retry.allows_plan(0), "fresh positive evidence can reacquire replacement permission");
    retry.deployed(0, typhon, { 3, 4 });
    result = retry.observe(image(2), true, { { typhon, { 3, 4 } } }, { ready, replacement }, 3000);
    expect(
        result.diagnostics.get("bait_attempts", 0) == 2,
        "only confirmed replacement registration consumes the second attempt");
    expect(
        !retry.allows_plan(0) && !result.main_allowed,
        "occupied replacement tile and exhausted budget block further bait and main plans");
    auto second_cooling = replacement;
    second_cooling.cooling = true;
    second_cooling.available = false;
    result = retry.observe(image(1), true, {}, { ready, second_cooling }, 4000);
    expect(
        !retry.allows_plan(0) && !result.main_allowed,
        "a second removal cannot exceed the configured attempt budget");

    for (int mode = 0; mode < 6; ++mode) {
        BlackFlowHunterPreludeController guarded;
        if (!start(guarded, 2)) {
            return 4;
        }
        std::vector<battle::DeploymentOper> cards { cooling, replacement };
        std::map<battle::OperNameTag, Point> field;
        if (mode == 0) {
            cards = { ready, replacement };
        }
        if (mode == 1) {
            cards.clear();
        }
        if (mode == 2) {
            cards.push_back(cooling);
        }
        if (mode == 3) {
            field.emplace(carda, Point(3, 4));
        }
        if (mode == 5) {
            field.emplace(battle::OperNameTag { battle::Role::Tank, "机械师" }, Point(3, 4));
        }
        result = guarded.observe(image(1), mode != 4, field, cards, 1000);
        expect(
            !guarded.allows_plan(0) && !result.main_allowed,
            "unproven removal, missing/duplicate card, occupancy or paused frame prevents replacement");
    }
    BlackFlowHunterPreludeController stale;
    if (!start(stale, 2)) {
        return 4;
    }
    static_cast<void>(stale.observe(image(1), true, {}, { cooling, replacement }, 1000));
    result = stale.observe(image(2), true, {}, { cooling, replacement }, 1000);
    expect(!stale.allows_plan(0), "duplicate timestamp invalidates earlier replacement permission");
    BlackFlowHunterPreludeController legacy;
    if (!start(legacy, 1)) {
        return 4;
    }
    result = legacy.observe(image(1), true, {}, { cooling, replacement }, 1000);
    expect(!legacy.allows_plan(0) && !result.main_allowed, "one-attempt legacy mode remains conservative");
    for (const int limit : { 0, 5 }) {
        BlackFlowHunterPreludeController invalid;
        expect(
            !invalid.prepare(manifest, raw, level, 0, "bad-limit", limit),
            "unbounded or invalid attempt limit is rejected");
    }
    std::cout << "HUNTER_RETRY checks=" << checks << " failures=" << failures
              << " real_frames=3 synthetic_unit_context=1 connections=0 inputs=0\n";
    return failures == 0 ? 0 : 1;
}
