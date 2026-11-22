#include <filesystem>
#include <fstream>
#include <iostream>

#include "Arknights-Tile-Pos/TileCalc2.hpp"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Task/Roguelike/BlackFlow/BlackFlowHunterPreludeController.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"

class AlternativeBaitProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    AlternativeBaitProbe() :
        RoguelikeBattleTaskPlugin(
            {},
            nullptr,
            "AlternativeBaitProbe",
            std::make_shared<asst::RoguelikeConfig>(),
            nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_requirements_met;

    void elite(const std::string& name, int value) { m_oper_elite[name] = value; }

    void attach(std::unique_ptr<asst::blackflow::BlackFlowHunterPreludeController> controller)
    {
        m_hunter_prelude = std::move(controller);
    }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    using namespace asst::battle;
    using namespace asst::blackflow;
    const std::filesystem::path repo(argv[1]), plans(argv[2]), output(argv[3]);
    std::filesystem::create_directories(output);
    const auto level_json =
        json::open(repo / utils::path("resource/Arknights-Tile-Pos/ro6_n_2_5-obt-roguelike-ro6-level_rogue6_2-5.json"));
    auto manifest = json::open(repo / utils::path("resource/template/Roguelike/BlackFlow/HunterPrelude/manifest.json"));
    const auto stage = json::open(plans / utils::path("枝头陷阱.json"));
    if (!level_json || !manifest || !stage || !BattleData.load(repo / utils::path("resource/battle_data.json")) ||
        !RoguelikeRecruit.load(repo / utils::path("resource/roguelike/BlackFlow/recruitment.json")) ||
        !RoguelikeCopilot.load(plans)) {
        return 3;
    }
    const Map::Level level(*level_json);
    const auto pixel = Map::TileCalc2::get_tile_screen_pos(level, 4, 1, false);
    auto& projections = (*manifest)["native_stage_geometry"]["tile_projections"].as_array();
    std::size_t calibration_index = projections.size();
    for (std::size_t index = 0; index < projections.size(); ++index) {
        if (projections[index].get("x", -1) == 1 && projections[index].get("y", -1) == 4) {
            calibration_index = index;
            break;
        }
    }
    if (calibration_index == projections.size()) {
        projections.emplace_back(
            json::object { { "x", 1 }, { "y", 4 }, { "pixel_x", pixel.x }, { "pixel_y", pixel.y } });
    }
    // Only calibration metadata changes. Work-only bitmap copies are byte
    // identical to production; the publication preflight verifies their hashes.
    const auto asset_root = repo / utils::path("resource/template/Roguelike/BlackFlow/HunterPrelude");
    for (const auto& [file, expected_hash] : manifest->at("assets_sha256").as_object()) {
        const auto name = utils::path(file);
        if (name.has_parent_path()) {
            return 5;
        }
        std::filesystem::copy_file(asset_root / name, output / name, std::filesystem::copy_options::overwrite_existing);
    }
    const auto candidate_manifest = output / utils::path("manifest.json");
    std::ofstream(candidate_manifest) << manifest->format();
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    std::string error;
    const auto opening = HunterOpening::parse(*stage, &error);
    expect(
        opening && opening->bait_plans.size() == 3 && opening->attempt_limit == 2,
        "three alternative plans retain two actual deployment attempts");
    const auto data = RoguelikeCopilot.get_stage_data("枝头陷阱");
    expect(bool(data), "real parser loads alternative bait stage");
    if (!opening || !data) {
        return 4;
    }
    AlternativeBaitProbe probe;
    auto controller = std::make_unique<BlackFlowHunterPreludeController>();
    const auto raw = repo / utils::path("resource/roguelike/BlackFlow/stage_mechanics/branch_trap.json");
    const bool prepared = controller->prepare(candidate_manifest, raw, level, *opening, "alternative-native", &error);
    expect(prepared, "actual controller validates new projection, complete terrain and unchanged visual assets");
    if (!prepared) {
        std::cerr << error << '\n';
        return 6;
    }
    probe.attach(std::move(controller));
    const auto& steward = data->deploy_plan.at("术师").front();
    const auto& carda = data->deploy_plan.at("术师").at(1);
    const auto& mech = data->deploy_plan.at("机械师").front();
    expect(
        steward.rank < carda.rank && carda.rank < mech.rank,
        "ranged target-specific candidates precede ground fallback");
    expect(
        steward.operator_names == std::vector<std::string> { "史都华德" },
        "defense-priority bait does not silently authorize all casters");
    expect(
        steward.location == Point(1, 4) && steward.direction == DeployDirection::Up,
        "left-specific high tile and orientation are explicit");
    expect(level.get_item(4, 1).buildableType == 2, "alternative actor has a real ranged tile");
    const auto make = [&](Role role, const std::string& name, int elite) {
        probe.elite(name, elite);
        return DeploymentOper { .role = role, .name = name };
    };
    const auto st = make(Role::Caster, "史都华德", 1);
    expect(probe.deployment_requirements_met(st, steward), "Steward real E1 base range reaches left hunter");
    auto right = steward;
    right.required_attack_tiles = { { 4, 2 } };
    expect(!probe.deployment_requirements_met(st, right), "Steward cannot touch the right hunter from left candidate");
    auto random_object = steward;
    random_object.required_attack_tiles = { { 2, 5 } };
    expect(!probe.deployment_requirements_met(st, random_object), "Steward base attack excludes lower random object");
    expect(
        probe.deployment_requirements_met(make(Role::Caster, "卡达", 1), carda),
        "Carda retains real right-side coverage");
    expect(
        probe.deployment_requirements_met(make(Role::Tank, "机械师", 1), mech),
        "ground fallback retains required left coverage");
    projections[calibration_index]["pixel_x"] = pixel.x + 10;
    const auto invalid_manifest = output / utils::path("invalid_manifest.json");
    std::ofstream(invalid_manifest) << manifest->format();
    BlackFlowHunterPreludeController invalid;
    expect(
        !invalid.prepare(invalid_manifest, raw, level, *opening, "invalid-projection", &error),
        "new bait calibration is checked rather than trusted");
    std::ofstream(output / utils::path("projection.json"))
        << json::object { { "x", 1 },
                          { "y", 4 },
                          { "pixel_x", pixel.x },
                          { "pixel_y", pixel.y },
                          { "source", "real Map::TileCalc2, current native level" } }
               .format();
    std::cout << "ALTERNATIVE_BAIT checks=" << checks << " failures=" << failures << " pixel=" << pixel.x << ','
              << pixel.y << " connections=0 inputs=0 live_pass=false\n";
    return failures ? 1 : 0;
}
