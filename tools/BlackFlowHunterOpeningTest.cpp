#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>

#include "Arknights-Tile-Pos/TileCalc2.hpp"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "MaaUtils/ImageIo.h"
#include "Task/Roguelike/BlackFlow/BlackFlowHunterPreludeController.h"
#include "Utils/Platform.hpp"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 5) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    using namespace asst::battle;
    const std::filesystem::path repo(argv[1]), work(argv[2]), out(argv[3]), corpus(argv[4]);
    std::filesystem::create_directories(out);
    const auto stage_path = repo / utils::path("resource/roguelike/BlackFlow/autopilot/枝头陷阱.json");
    const auto original = json::open(stage_path);
    if (!original || !BattleData.load(repo / utils::path("resource/battle_data.json"))) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << message << '\n';
        }
    };
    std::string error;
    auto baseline = *original;
    const auto source_plans = baseline.at("deploy_plan").as_array();
    const std::size_t general_index = baseline.contains("hunter_bait_plans") ? 1 : 0;
    auto legacy_bait = source_plans.at(general_index);
    legacy_bait["groups"] = json::array { "提丰", "术师", "速狙", "狙击", "辅助", "其他高台" };
    legacy_bait["excluded_operator_names"] = json::array { "望" };
    legacy_bait["required_attack_tiles"] = json::array { json::array { 2, 2 }, json::array { 4, 2 } };
    legacy_bait["max_deployments"] = 2;
    json::array legacy_plans { legacy_bait };
    for (std::size_t index = general_index + 1; index < source_plans.size(); ++index) {
        legacy_plans.push_back(source_plans[index]);
    }
    baseline.as_object().erase("hunter_bait_plans");
    baseline.as_object().erase("hunter_bait_attempt_limit");
    baseline["hunter_bait_plan"] = 0;
    baseline["deploy_plan"] = legacy_plans;
    const auto legacy = HunterOpening::parse(baseline, &error);
    expect(
        legacy && legacy->bait_plans.size() == 1 && legacy->attempt_limit == 2,
        "existing scalar opening remains valid");
    json::value candidate = baseline;
    candidate.as_object().erase("hunter_bait_plan");
    candidate["hunter_bait_plans"] = json::array { 0, 1 };
    candidate["hunter_bait_attempt_limit"] = 2;
    const auto old_plans = baseline.at("deploy_plan").as_array();
    json::value typhon = old_plans[0];
    typhon["groups"] = json::array { "提丰" };
    typhon["operator_names"] = json::array { "提丰" };
    typhon["minimum_elite"] = 1;
    typhon["location"] = json::array { 5, 4 };
    typhon["direction"] = "left";
    typhon["max_deployments"] = 1;
    typhon["required_attack_tiles"] = json::array { json::value(json::array { 2, 2 }) };
    typhon["forbidden_attack_tiles"] = json::array { json::array { 1, 1 },
                                                     json::array { 2, 1 },
                                                     json::array { 3, 1 },
                                                     json::array { 4, 1 },
                                                     json::array { 4, 2 } };
    json::value general = old_plans[0];
    general["groups"] = json::array { "术师", "速狙", "狙击", "辅助", "其他高台" };
    general["excluded_operator_names"] = json::array { "望", "提丰" };
    json::array plans { typhon, general };
    for (std::size_t index = 1; index < old_plans.size(); ++index) {
        plans.push_back(old_plans[index]);
    }
    candidate["deploy_plan"] = plans;
    const auto opening = HunterOpening::parse(candidate, &error);
    expect(
        opening && opening->bait_plans.size() == 2 && opening->attempt_limit == 2,
        "multiple locations share the same two-attempt budget");
    if (!opening) {
        std::cerr << error << '\n';
        return 4;
    }
    const auto invalid = [&](json::value changed, const char* reason) {
        expect(!HunterOpening::parse(changed, &error), reason);
    };
    for (auto indices : { json::array {},
                          json::array { 0, 0 },
                          json::array { -1, 1 },
                          json::array { 0, 99 },
                          json::array { 0, "1" } }) {
        auto changed = candidate;
        changed["hunter_bait_plans"] = indices;
        invalid(changed, "invalid or duplicate plan indices are rejected");
    }
    for (int limit : { 0, 5 }) {
        auto changed = candidate;
        changed["hunter_bait_attempt_limit"] = limit;
        invalid(changed, "attempt budget cannot escape supported bounds");
    }
    for (const auto& key : { "deploy_plan_only", "reserve_deploy_cost" }) {
        auto changed = candidate;
        changed[key] = false;
        invalid(changed, "opening cannot enable generic deployment or disable cost reservation");
    }
    auto contradictory = candidate;
    contradictory["deploy_plan"][0]["forbidden_attack_tiles"] = json::array { json::value(json::array { 2, 2 }) };
    invalid(contradictory, "required and forbidden targets cannot contradict");
    auto unbound = candidate;
    unbound["deploy_plan"][0]["exclude_starting_core"] = false;
    invalid(unbound, "a bait plan must exclude the starting core");
    auto ambiguous = candidate;
    ambiguous["hunter_bait_plan"] = 0;
    invalid(ambiguous, "scalar and list declarations cannot coexist");
    const auto stages = out / "stages";
    std::filesystem::create_directories(stages);
    std::ofstream(stages / utils::path("枝头陷阱.json")) << candidate.to_string();
    expect(RoguelikeCopilot.load(stages), "ordinary copilot loader accepts the separate opening metadata");
    const auto loaded = RoguelikeCopilot.get_hunter_opening("枝头陷阱");
    expect(loaded && loaded->bait_plans.size() == 2, "loaded metadata preserves both bait locations");
    const auto raw =
        json::open(repo / utils::path("resource/Arknights-Tile-Pos/ro6_n_2_5-obt-roguelike-ro6-level_rogue6_2-5.json"));
    if (!raw) {
        return 5;
    }
    const Map::Level level(*raw);
    const auto model_dir = out / "model";
    std::filesystem::create_directories(model_dir);
    const auto model_source = repo / utils::path("resource/template/Roguelike/BlackFlow/HunterPrelude");
    for (const auto& file : std::filesystem::directory_iterator(model_source)) {
        if (file.is_regular_file()) {
            std::filesystem::copy_file(
                file.path(),
                model_dir / file.path().filename(),
                std::filesystem::copy_options::overwrite_existing);
        }
    }
    auto manifest = *json::open(model_dir / "manifest.json");
    auto& projections = manifest["native_stage_geometry"]["tile_projections"].as_array();
    const auto projected = Map::TileCalc2::get_tile_screen_pos(level, 4, 5, false);
    if (std::ranges::none_of(projections, [](const json::value& tile) {
            return tile.get("x", -1) == 5 && tile.get("y", -1) == 4;
        })) {
        projections.emplace_back(
            json::object { { "x", 5 }, { "y", 4 }, { "pixel_x", projected.x }, { "pixel_y", projected.y } });
    }
    std::ofstream(model_dir / "manifest.json") << manifest.to_string();
    const auto prepare = [&](BlackFlowHunterPreludeController& controller, const HunterOpening& spec) {
        const bool ok = controller.prepare(
            model_dir / "manifest.json",
            work / "branch_teaching_raw_level.json",
            level,
            spec,
            "opening-contract",
            &error);
        expect(ok, "actual terrain and projection validate configured bait positions");
        if (!ok) {
            std::cerr << error << '\n';
        }
        return ok;
    };
    BlackFlowHunterPreludeController controller;
    if (!prepare(controller, *opening)) {
        return 6;
    }
    const auto rotate = [](const AttackRange& range, bool left) {
        AttackRange result;
        for (Point tile : range) {
            result.emplace_back(left ? Point(-tile.x, -tile.y) : Point(tile.y, -tile.x));
        }
        return result;
    };
    for (int elite : { 1, 2 }) {
        const auto range = BattleData.get_range(Role::Sniper, "提丰", elite);
        expect(
            !range.empty() && controller.attack_range_allowed(0, rotate(range, true)),
            "actual promoted Typhon left range covers only the intended hunter and excludes eggs");
        expect(
            !controller.attack_range_allowed(0, rotate(range, false)),
            "wrong direction cannot silently satisfy the target-specific contract");
    }
    expect(
        controller.attack_range_allowed(1, rotate(BattleData.get_range(Role::Caster, "卡达", 1), false)),
        "existing Carda up plan remains eligible for either residual spawn");
    const auto first = MAA_NS::imread(work / "hunter_prelude/dense/00000.png");
    static_cast<void>(controller.observe(first, true, {}, {}, 1000));
    expect(!controller.allows_plan(2), "main operators remain locked before clearance");
    expect(controller.allows_plan(0), "verified left hunter permits the left-specific first bait");
    const OperNameTag bait { Role::Sniper, "提丰" };
    controller.deployed(0, bait, { 5, 4 });
    expect(
        controller.bait_skill_allowed({ { bait, { 5, 4 } } }),
        "only the registered bait may use an already configured ready skill");
    expect(!controller.bait_skill_allowed({ { bait, { 3, 4 } } }), "wrong bait location cannot gain skill permission");
    expect(
        !controller.bait_skill_allowed({ { bait, { 5, 4 } }, { { Role::Sniper, "维什戴尔" }, { 5, 1 } } }),
        "extra allies cannot borrow the bait-only skill exception");
    expect(!controller.allows_plan(1), "another bait cannot deploy before verified retirement");
    static_cast<void>(controller.observe(first, false, { { bait, { 5, 4 } } }, {}, 1100));
    expect(
        !controller.bait_skill_allowed({ { bait, { 5, 4 } } }),
        "paused or unknown observations revoke bait skill permission");
    BlackFlowHunterPreludeController mismatch;
    if (prepare(mismatch, *opening)) {
        static_cast<void>(mismatch.observe(first, true, {}, {}, 1000));
        mismatch.deployed(0, bait, { 3, 4 });
        const auto rejected = mismatch.observe(first, true, {}, {}, 1200);
        expect(
            !rejected.main_allowed && !mismatch.allows_plan(1),
            "a drag to the old unapproved tile breaches the opening contract");
    }
    // The following is a synthetic caller-state contract using unchanged real images.
    // It does not claim that a new-position bait or withdrawal was executed in game.
    auto right_first = *opening;
    right_first.bait_plans[1].required_targets = { { 4, 2 } };
    BlackFlowHunterPreludeController recovery;
    if (prepare(recovery, right_first)) {
        bool primed = false;
        std::uint64_t clock = 1000;
        for (int source_ms = 0; source_ms <= 2000 && !primed; source_ms += 200) {
            const auto image_path = work / "hunter_prelude/dense" / utils::path(std::format("{:05}.png", source_ms));
            const auto image = MAA_NS::imread(image_path);
            if (!image.empty()) {
                static_cast<void>(recovery.observe(image, true, {}, {}, clock));
                clock += 200;
                primed = recovery.allows_plan(1);
            }
        }
        expect(primed, "a real positive right-spawn view authorizes the right-specific synthetic bait");
        if (primed) {
            const OperNameTag caster { Role::Caster, "卡达" };
            recovery.deployed(1, caster, { 3, 4 });
            bool withdrew = false, then_left_allowed = false;
            std::vector<std::pair<int, std::filesystem::path>> frames;
            for (const auto& file : std::filesystem::directory_iterator(corpus)) {
                if (file.path().extension() != L".json") {
                    continue;
                }
                const auto data = json::open(file.path());
                if (data && data->get("what", "") == "BlackFlowHunterPrelude") {
                    const auto sequence = data->at("details").at("sequence").as_integer();
                    if (sequence >= 2 && sequence <= 5) {
                        frames.emplace_back(sequence, file.path());
                    }
                }
            }
            std::ranges::sort(frames);
            expect(frames.size() == 4, "the real one-sided spawn-empty regression images are complete");
            for (const auto& [sequence, path] : frames) {
                const auto data = json::open(path);
                const auto& observation = data->at("details").at("observation");
                auto image_path = path;
                image_path.replace_extension(L".png");
                const auto image = MAA_NS::imread(image_path);
                std::map<OperNameTag, Point> field;
                if (observation.get("bait_present", false)) {
                    field.emplace(caster, Point { 3, 4 });
                }
                const bool cooling = observation.get("bait_cooling_verified", false);
                const std::vector<DeploymentOper> cards { { .role = caster.role,
                                                            .available = !cooling && field.empty(),
                                                            .cooling = cooling,
                                                            .name = caster.name } };
                const auto decision = recovery.observe(
                    image,
                    true,
                    field,
                    cards,
                    static_cast<std::uint64_t>(observation.at("observed_ms").as_integer()));
                expect(
                    !decision.main_allowed,
                    "one empty spawn never releases main operators while the other hunter survives");
                if (decision.retreat_bait) {
                    withdrew = true;
                    expect(
                        *decision.retreat_bait == Point(3, 4),
                        "target-specific recovery withdraws only the actual active bait tile");
                    expect(!recovery.allows_plan(0), "a withdrawal request is not yet verified retirement");
                }
                if (sequence == 5) {
                    then_left_allowed = recovery.allows_plan(0);
                    expect(
                        !recovery.allows_plan(1),
                        "the empty right spawn cannot consume another right-specific attempt");
                }
            }
            expect(
                withdrew,
                "two distinct right-empty views permit observation recovery while the left remains present");
            expect(
                then_left_allowed,
                "verified retirement and current left-positive evidence permit the other location");
            if (then_left_allowed) {
                recovery.deployed(0, bait, { 5, 4 });
                expect(!recovery.allows_plan(1), "two locations still share a global two-attempt cap");
            }
        }
    }
    std::ofstream(out / "projection.json")
        << json::value(json::object { { "x", 5 }, { "y", 4 }, { "pixel_x", projected.x }, { "pixel_y", projected.y } })
               .to_string();
    std::cout
        << "HUNTER_OPENING checks=" << checks << " failures=" << failures
        << " synthetic_actor_contract=true real_range_and_images=true connections=0 inputs=0 live_clear_proven=false\n";
    return failures ? 1 : 0;
}
