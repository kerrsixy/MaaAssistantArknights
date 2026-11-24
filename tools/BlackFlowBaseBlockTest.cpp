#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "AsstCaller.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeTrainingConfig.h"
#include "MaaUtils/Encoding.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"

class BaseBlockProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    BaseBlockProbe(const std::shared_ptr<asst::RoguelikeConfig>& config) :
        RoguelikeBattleTaskPlugin({}, nullptr, "BaseBlockProbe", config, nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_requirements_met;

    void observe(const std::string& name, int elite, int level)
    {
        m_oper_elite[name] = elite;
        m_config->status().opers[name] = { .elite = elite, .level = level };
    }

    void contradict_phase(const std::string& name, int elite) { m_oper_elite[name] = elite; }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        return 2;
    }
    using namespace asst;
    using namespace asst::battle;
    const std::filesystem::path root(argv[1]), output(argv[2]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(root.native()).c_str())) {
        return 3;
    }
    const auto path = root / utils::path("resource/roguelike/BlackFlow/operator_training.json");
    if (!RoguelikeTraining.load(path) ||
        !RoguelikeCopilot.load(root / utils::path("resource/roguelike/BlackFlow/autopilot"))) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool result, const char* label) {
        ++checks;
        if (!result) {
            ++failures;
            std::cout << "FAIL " << label << '\n';
        }
    };
    expect(RoguelikeTraining.base_block_count("机械师", 2, 90) == 3, "actual E2L90 Mech base block3");
    expect(RoguelikeTraining.base_block_count("机械师", 2, 1) == 3, "low level does not invent lower base blocking");
    expect(RoguelikeTraining.base_block_count("晓歌", 2, 20) == 1, "actual Xiaoge base block1 cannot intercept a car");
    expect(RoguelikeTraining.base_block_count("古米", 0, 1) == 2, "E0 guardian capacity is not its promoted capacity");
    expect(RoguelikeTraining.base_block_count("古米", 1, 1) == 3, "promotion changes real guardian capacity");
    expect(RoguelikeTraining.base_block_count("幽灵鲨", 1, 70) == 2, "E1 centurion cannot borrow E2 blocking");
    expect(
        RoguelikeTraining.base_block_count("幽灵鲨", 2, 1) == 3,
        "E2 centurion qualifies without a defender name list");
    expect(
        RoguelikeTraining.base_block_count("卡达", 1, 60) == 1,
        "ranged base count is not proof of a legal ground tile");
    expect(!RoguelikeTraining.base_block_count("机械师", -1, 90), "unknown phase stays unknown");
    expect(!RoguelikeTraining.base_block_count("机械师", 2, 0), "unknown level stays unknown");
    expect(!RoguelikeTraining.base_block_count("机械师", 2, 91), "illegal level stays unknown");
    expect(!RoguelikeTraining.base_block_count("not-an-operator", 2, 90), "unknown identity gets no role default");
    auto config = std::make_shared<RoguelikeConfig>();
    expect(config->verify_and_load_params(json::object { { "theme", "BlackFlow" }, { "mode", 0 } }), "load theme");
    BaseBlockProbe probe(config);
    DeploymentOper mech { .role = Role::Tank, .name = "机械师" };
    DeploymentOper agent { .role = Role::Pioneer, .name = "晓歌" };
    battle::roguelike::DeployInfoWithRank capacity;
    capacity.minimum_base_block_count = 3;
    capacity.require_persistent_blocking = true;
    expect(!probe.deployment_requirements_met(mech, capacity), "unobserved run phase cannot use account capacity");
    probe.observe(mech.name, 2, 90);
    probe.observe(agent.name, 2, 20);
    expect(
        probe.deployment_requirements_met(mech, capacity),
        "native deployment consumer accepts actual Mech capacity");
    expect(!probe.deployment_requirements_met(agent, capacity), "native consumer rejects cheap block1 for block3 task");
    probe.contradict_phase(mech.name, 1);
    expect(!probe.deployment_requirements_met(mech, capacity), "conflicting run phase cannot qualify");
    probe.observe(mech.name, 2, 90);
    DeploymentOper centurion { .role = Role::Warrior, .name = "幽灵鲨" };
    probe.observe(centurion.name, 1, 70);
    expect(!probe.deployment_requirements_met(centurion, capacity), "native E1 centurion fails capacity task");
    probe.observe(centurion.name, 2, 1);
    expect(probe.deployment_requirements_met(centurion, capacity), "native E2 centurion qualifies by data");
    DeploymentOper liberator { .role = Role::Warrior, .name = "玛恩纳" };
    probe.observe(liberator.name, 2, 90);
    expect(!probe.deployment_requirements_met(liberator, capacity), "base count cannot override skill-only blocking");
    const auto stage = RoguelikeCopilot.get_stage_data("预算否决");
    expect(stage && stage->reserve_deploy_cost, "budget plan opts into DP reservation");
    if (!stage) {
        return 4;
    }
    const auto& blocker = stage->deploy_plan.at("机械师");
    expect(
        blocker.size() == 2 && blocker[0].minimum_base_block_count == 3 && blocker[1].minimum_base_block_count == 3,
        "both primary and fallback use the typed capacity requirement");
    expect(
        blocker[0].location == Point(7, 5) && blocker[1].location == Point(7, 4),
        "teaching primary and valid fallback");
    expect(
        blocker[0].exclusive_locations == std::vector<Point> { { 7, 4 } } &&
            blocker[1].exclusive_locations == std::vector<Point> { { 7, 5 } },
        "blocking alternatives are mutually exclusive");
    const auto& main = stage->deploy_plan.at("益达");
    const auto& med = stage->deploy_plan.at("奶");
    const auto main_entry = std::ranges::find_if(main, [](const auto& p) { return p.location == Point(9, 4); });
    const auto med_entry = std::ranges::find_if(med, [](const auto& p) { return p.location == Point(8, 5); });
    expect(
        main_entry != main.end() && med_entry != med.end() && main_entry->rank < med_entry->rank,
        "main output reserves cost before optional healing");
    auto old_rules = json::open(path);
    (*old_rules)["operators"]["char_4230_mcnist"].as_object().erase("base_block_counts");
    const auto legacy = output / utils::path("legacy-rules.json");
    std::ofstream(legacy) << *old_rules;
    expect(RoguelikeTraining.load(legacy), "legacy resource without blocking data still loads");
    expect(!RoguelikeTraining.base_block_count(mech.name, 2, 90), "missing legacy capacity is unknown");
    expect(!probe.deployment_requirements_met(mech, capacity), "missing capacity cannot qualify a new constraint");
    capacity.minimum_base_block_count = 0;
    expect(probe.deployment_requirements_met(mech, capacity), "old unconstrained plans keep prior behavior");
    for (const auto& value : std::vector<json::value> { true, -1, 2.5, "3" }) {
        auto invalid = *json::open(path);
        invalid["operators"]["char_4230_mcnist"]["base_block_counts"][2] = value;
        const auto malformed = output / utils::path("malformed-rules.json");
        std::ofstream(malformed) << invalid;
        expect(!RoguelikeTraining.load(malformed), "malformed capacity is rejected");
        expect(RoguelikeTraining.load(path), "valid game rules restore after rejection");
    }
    const auto plan_folder = output / utils::path("plan-schema");
    std::filesystem::create_directories(plan_folder);
    const json::object legacy_plan {
        { "stage_name", "base_block_legacy_fixture" },
        { "deploy_plan",
          json::array { json::object {
              { "groups", json::array { "机械师" } },
              { "location", json::array { 7, 5 } },
          } } },
    };
    for (const auto& value : std::vector<json::value> { true, -1, 2.5, "3" }) {
        json::value malformed = legacy_plan;
        malformed["deploy_plan"][0]["minimum_base_block_count"] = value;
        std::ofstream(plan_folder / utils::path("plan.json")) << malformed;
        expect(!RoguelikeCopilot.load(plan_folder), "malformed deployment capacity constraint is rejected");
    }
    std::ofstream(plan_folder / utils::path("plan.json")) << json::value(legacy_plan);
    expect(RoguelikeCopilot.load(plan_folder), "legacy deployment without capacity constraint still loads");
    const auto legacy_stage = RoguelikeCopilot.get_stage_data("base_block_legacy_fixture");
    expect(
        legacy_stage && legacy_stage->deploy_plan.at("机械师").front().minimum_base_block_count == 0,
        "legacy deployment keeps an unconstrained zero default");
    std::ofstream(output / utils::path("base_block_probe.json")) << json::value(
        json::object {
            { "checks", checks },
            { "failures", failures },
            { "gameplay_inputs", 0 },
            { "current_effective_blocking_proven", false },
            { "live_budget_clear_proven", false },
        });
    std::cout << "BASE_BLOCK110 checks=" << checks << " failures=" << failures << " inputs=0\n";
    return failures ? 1 : 0;
}
