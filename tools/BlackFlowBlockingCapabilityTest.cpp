#include <filesystem>
#include <iostream>

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"

class BlockingProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    BlockingProbe(const std::shared_ptr<asst::RoguelikeConfig>& config) :
        RoguelikeBattleTaskPlugin({}, nullptr, "BlockingProbe", config, nullptr)
    {
        m_homes.push_back({ { 9, 4 }, asst::battle::DeployDirection::Left });
        m_homes_status.resize(1);
        m_role_order = { asst::battle::Role::Warrior, asst::battle::Role::Tank };
    }

    using RoguelikeBattleTaskPlugin::deployment_requirements_met;
    using RoguelikeBattleTaskPlugin::get_oper_position;
    using RoguelikeBattleTaskPlugin::persistent_blocking_available;

    void place(const asst::battle::DeploymentOper& oper)
    {
        postproc_of_deployment_conditions(oper, { 8, 4 }, asst::battle::DeployDirection::Up);
    }

    bool waiting_for_blocker() const { return m_homes_status.front().wait_blocking; }

    bool blocker_recorded() const { return !m_blocking_for_home_index.empty(); }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    using namespace asst::battle;
    const std::filesystem::path repo(argv[1]), plans(argv[2]);
    if (!BattleData.load(repo / utils::path("resource/battle_data.json")) ||
        !RoguelikeRecruit.load(repo / utils::path("resource/roguelike/BlackFlow/recruitment.json")) ||
        !RoguelikeCopilot.load(plans)) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool passed, const char* label) {
        ++checks;
        if (!passed) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    auto config = std::make_shared<RoguelikeConfig>();
    expect(config->verify_and_load_params(json::object { { "theme", "BlackFlow" }, { "mode", 0 } }), "load theme");
    BlockingProbe probe(config);
    battle::roguelike::DeployInfoWithRank blocking;
    blocking.require_persistent_blocking = true;
    battle::roguelike::DeployInfoWithRank damage;
    for (const auto& name : { "司霆惊蛰", "玛恩纳", "龙舌兰" }) {
        DeploymentOper oper;
        oper.name = name;
        oper.role = Role::Warrior;
        expect(probe.get_oper_position({ oper.role, oper.name }) == OperPosition::None, "Liberator is not a lane seal");
        expect(!probe.deployment_requirements_met(oper, blocking), "blocking plan rejects skill-only blocking");
        expect(probe.deployment_requirements_met(oper, damage), "ordinary damage plan still permits Liberators");
    }
    DeploymentOper gummy;
    gummy.name = "古米";
    gummy.role = Role::Tank;
    gummy.available = true;
    gummy.cost = 16;
    expect(probe.persistent_blocking_available({ gummy.role, gummy.name }), "Gummy is a real blocker");
    expect(probe.deployment_requirements_met(gummy, blocking), "blocking plan accepts Gummy");
    expect(!probe.persistent_blocking_available({ Role::Pioneer, "桃金娘" }), "skill-time zero block is not permanent");
    expect(!probe.persistent_blocking_available({ Role::Warrior, "unidentified" }), "unknown branch cannot qualify");
    expect(!probe.persistent_blocking_available({ Role::Medic, "蜜莓" }), "medic cannot fill a blocker requirement");
    DeploymentOper leizi;
    leizi.name = "司霆惊蛰";
    leizi.role = Role::Warrior;
    leizi.available = true;
    leizi.cost = 12;
    probe.place(leizi);
    expect(probe.waiting_for_blocker() && !probe.blocker_recorded(), "Liberator deployment cannot mark home defended");
    probe.place(gummy);
    expect(!probe.waiting_for_blocker() && probe.blocker_recorded(), "real blocker establishes the lane");
    auto other_config = std::make_shared<RoguelikeConfig>();
    expect(
        other_config->verify_and_load_params(json::object { { "theme", "Mizuki" }, { "mode", 0 } }),
        "load legacy theme");
    BlockingProbe legacy(other_config);
    expect(
        legacy.get_oper_position({ Role::Warrior, leizi.name }) == OperPosition::Blocking,
        "other themes retain legacy classification");
    const auto stage = RoguelikeCopilot.get_stage_data("败叶");
    expect(stage.has_value(), "mechanism candidate parses");
    if (stage) {
        expect(stage->deploy_plan.at("奶盾").front().require_persistent_blocking, "middle blocker is explicit");
        expect(stage->deploy_plan.at("奶盾").back().require_persistent_blocking, "upper blocker is explicit");
        expect(
            !stage->deploy_plan.at("益达").front().require_persistent_blocking,
            "damage slot has no blocker restriction");
    }
    const std::filesystem::path schemas(argv[3]);
    expect(!RoguelikeCopilot.load(schemas / "invalid"), "non-boolean blocker requirement is rejected");
    expect(RoguelikeCopilot.load(schemas / "legacy"), "old plans still load");
    const auto old = RoguelikeCopilot.get_stage_data("blocking_legacy_fixture");
    expect(
        old && !old->deploy_plan.at("near").front().require_persistent_blocking,
        "absent requirement preserves old plans");
    std::cout << "BLOCKING_CAPABILITY checks=" << checks << " failures=" << failures << " device_inputs=0\n";
    return failures ? 1 : 0;
}
