#include <iostream>

#include "Task/Roguelike/RoguelikeConfig.h"
#include "Task/Roguelike/RoguelikeDeploymentBudget.h"

int main()
{
    using namespace asst;
    using namespace asst::battle;
    using namespace asst::battle::roguelike;
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << message << '\n';
        }
    };

    RoguelikeConfig config;
    config.set_start_opers({ { "维什戴尔", false }, { "机械师", false }, { "卡达", false } });
    for (int recruitment = 0; recruitment < 4; ++recruitment) {
        config.advance_start_oper_index();
        expect(config.get_core_char() == "维什戴尔", "recruitment cursor does not erase starting core");
    }
    config.reset_start_oper_index();
    expect(config.get_core_char() == "维什戴尔", "new run retains configured starting core");

    DeploymentOper core;
    core.name = config.get_core_char();
    core.role = Role::Sniper;
    core.cost = 26;
    core.available = true;
    const auto decide = [&](int dp, bool blocking, bool urgent, bool legal) {
        return starting_core_action(core, dp, blocking, urgent, legal);
    };
    expect(decide(26, false, false, true) == StartingCoreAction::Deploy, "ready core can establish first damage");
    expect(decide(-1, true, false, true) == StartingCoreAction::Deploy, "visible ready card needs no guessed DP");
    expect(decide(26, true, true, true) == StartingCoreAction::Fallback, "actual urgent home keeps rescue priority");
    expect(
        decide(26, true, false, false) == StartingCoreAction::Fallback,
        "no legal coverage cannot monopolize planner");
    core.cooling = true;
    expect(
        decide(99, true, false, true) == StartingCoreAction::Fallback,
        "cooling core does not suppress replacements");
    core.cooling = false;
    core.role = Role::Drone;
    expect(
        decide(99, true, false, true) == StartingCoreAction::Fallback,
        "reusable summon cannot be starting operator");
    core.role = Role::Unknown;
    expect(decide(99, true, false, true) == StartingCoreAction::Fallback, "unknown identity does not claim budget");
    core.role = Role::Sniper;
    core.name.clear();
    expect(decide(99, true, false, true) == StartingCoreAction::Fallback, "empty card name does not claim budget");
    core.name = config.get_core_char();
    core.available = false;
    expect(decide(10, false, false, true) == StartingCoreAction::Fallback, "initial blocker can deploy before waiting");
    expect(
        decide(10, true, false, true) == StartingCoreAction::Reserve,
        "after blocker avoid spending core budget on fillers");
    expect(decide(10, true, true, true) == StartingCoreAction::Fallback, "lost home cancels saving immediately");
    expect(decide(10, true, false, false) == StartingCoreAction::Fallback, "blocked core location releases saving");
    expect(
        decide(-1, true, false, true) == StartingCoreAction::Fallback,
        "failed DP observation cannot reuse stale cost");
    expect(decide(26, true, false, true) == StartingCoreAction::Fallback, "dim card with enough DP is not a fee wait");
    expect(
        decide(99, true, false, true) == StartingCoreAction::Fallback,
        "dim card at maximum DP cannot deadlock defence");
    core.cooling = true;
    expect(decide(10, true, false, true) == StartingCoreAction::Fallback, "dim cooldown cannot reserve DP");
    core.cooling = false;
    for (int cost : { 0, -1 }) {
        core.cost = cost;
        expect(decide(0, true, false, true) == StartingCoreAction::Fallback, "unknown fee cannot be fabricated");
    }
    for (int cost : { 7, 18, 26, 36, 40 }) {
        core.cost = cost;
        expect(
            decide(cost - 1, true, false, true) == StartingCoreAction::Reserve,
            "live fee changes are used without fixed EW price");
        core.available = true;
        expect(decide(cost, true, false, true) == StartingCoreAction::Deploy, "core deploys when DP arrives");
        core.available = false;
    }
    config.set_start_opers({ { "望", false } });
    core.name = config.get_core_char();
    core.role = Role::Support;
    core.available = true;
    expect(
        decide(99, true, false, true) == StartingCoreAction::Deploy,
        "priority follows configured core rather than fixed EW identity");
    // Preserve the older bounded policy for secondary key operators.
    expect(reserve_core_budget(true, false, 20, 25), "secondary-key five-DP reserve is preserved");
    expect(!reserve_core_budget(true, false, 19, 25), "secondary-key reserve does not expand to six DP");
    expect(!reserve_core_budget(false, false, 20, 25), "secondary key cannot delay initial defence");
    expect(!reserve_core_budget(true, true, 20, 25), "secondary key cannot override urgent rescue");
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
