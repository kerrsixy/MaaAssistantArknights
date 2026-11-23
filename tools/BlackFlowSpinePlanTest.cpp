#include <algorithm>
#include <filesystem>
#include <iostream>

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"

class SpineProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    SpineProbe() :
        RoguelikeBattleTaskPlugin({}, nullptr, "Spine81", std::make_shared<asst::RoguelikeConfig>(), nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_requirements_met;

    void elite(const std::string& name, int level) { m_oper_elite[name] = level; }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
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
    const auto stage = RoguelikeCopilot.get_stage_data("刺生灾害");
    if (!stage || !stage->deploy_plan.contains("地面阻挡")) {
        return 4;
    }
    int checks = 0, failures = 0;
    const auto check = [&](bool value, const char* name) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << name << '\n';
        }
    };
    const auto& blockers = stage->deploy_plan.at("地面阻挡");
    check(blockers.size() == 3, "all three permanent duties admit the capability group");
    check(blockers.at(0).location == Point(4, 6), "first left defense retained");
    check(
        blockers.at(1).location == Point(7, 6) && blockers.at(1).direction == DeployDirection::Right,
        "later right lane precedes central lane");
    check(blockers.at(2).location == Point(6, 5), "central backup remains available");
    check(
        blockers.at(0).rank < blockers.at(1).rank && blockers.at(1).rank < blockers.at(2).rank,
        "native parser preserves the authored duty order");
    const auto& names = RoguelikeRecruit.get_group_names(std::string(RoguelikeTheme::BlackFlow));
    const auto ids =
        RoguelikeRecruit.get_group_ids_of_oper(std::string(RoguelikeTheme::BlackFlow), { Role::Pioneer, "凛御银灰" });
    check(
        std::ranges::any_of(ids, [&](int id) { return names.at(id) == "地面阻挡"; }),
        "observed operator truly belongs to the real capability group");
    SpineProbe probe;
    const auto unit = [&](Role role, const std::string& name, int elite) {
        DeploymentOper oper;
        oper.role = role;
        oper.name = name;
        probe.elite(name, elite);
        return oper;
    };
    const auto svash = unit(Role::Pioneer, "凛御银灰", 1);
    for (const auto& slot : blockers) {
        check(slot.require_persistent_blocking, "capability fallback retains permanent-block guard");
        check(probe.deployment_requirements_met(svash, slot), "actual recruited counsellor is eligible");
        check(
            !probe.deployment_requirements_met(unit(Role::Warrior, "司霆惊蛰", 2), slot),
            "Liberator cannot fill permanent duty");
        check(
            !probe.deployment_requirements_met(unit(Role::Pioneer, "桃金娘", 1), slot),
            "flag skill cannot promise permanent block");
    }
    auto coverage = stage->deploy_plan.at("益达").front();
    const auto ew = unit(Role::Sniper, "维什戴尔", 2);
    coverage.required_attack_tiles = { { 6, 5 } };
    check(probe.deployment_requirements_met(ew, coverage), "EW directly covers central approach");
    coverage.required_attack_tiles = { { 7, 6 } };
    check(!probe.deployment_requirements_met(ew, coverage), "EW basic footprint does not cover right interception");
    check(
        std::ranges::all_of(blockers, [](const auto& slot) { return slot.operator_names.empty(); }),
        "capability fallback does not hardcode the observed operator name");
    std::cout << "SPINE81 checks=" << checks << " failures=" << failures << " live_verified=0 block_count_verified=0\n";
    return failures ? 1 : 0;
}
