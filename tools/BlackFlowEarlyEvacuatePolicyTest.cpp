#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "Config/Roguelike/BlackFlow/BlackFlowStrategyConfig.h"
#include "Config/Roguelike/RoguelikeShoppingConfig.h"
#include "Task/Roguelike/BlackFlow/BlackFlowPolicy.h"
#include "Utils/Platform.hpp"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path repo(argv[1]), candidate(argv[2]), baseline_path(argv[3]);
    if (!RoguelikeShopping.load(repo / utils::path("resource/roguelike/BlackFlow/shopping.json"))) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool value, const std::string& label) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const auto make_facts = [](std::optional<int> floor,
                               std::optional<bool> known,
                               std::optional<int> battles,
                               std::optional<std::vector<std::string>> options) {
        FactStore facts;
        if (floor) {
            facts.set("current_floor", static_cast<std::int64_t>(*floor));
        }
        if (known) {
            facts.set("encounter.route_known", *known);
        }
        if (battles) {
            facts.set("encounter.remaining_battles", static_cast<std::int64_t>(*battles));
        }
        if (options) {
            facts.set("encounter.available_options", *options);
        }
        return facts;
    };
    const std::vector<std::string> visible { "接受它的提议", "自己找路" };
    const auto observed = make_facts(1, true, 2, visible);
    if (!BlackFlowStrategy.load(baseline_path)) {
        return 4;
    }
    const auto baseline = BlackFlowStrategy.resolve_profile("ending1");
    expect(
        baseline && resolve_encounter_rule(*baseline, observed, "三重身") == nullptr,
        "run68 production profile falls back to the default accept choice");
    if (!BlackFlowStrategy.load(candidate)) {
        return 5;
    }
    const auto policy = BlackFlowStrategy.resolve_profile("ending1");
    if (!policy) {
        return 6;
    }
    const auto matches = [&](const FactStore& facts, const std::string& name = "三重身") {
        const auto* rule = resolve_encounter_rule(*policy, facts, name);
        return rule && rule->id == "ending1_triplicate_keep_planned_floor1_battles" &&
               rule->option_text == std::vector<std::string> { "自己找路" } && !rule->allow_fallback &&
               rule->choose == 0 && rule->option_num == 0 && rule->on_selected.empty();
    };
    expect(matches(observed), "actual floor1 planned two-battle context keeps the current route");
    for (int battles : { 1, 3, 7 }) {
        expect(matches(make_facts(1, true, battles, visible)), "no fixed target battle count is invented");
    }
    expect(
        matches(make_facts(1, true, 2, std::vector<std::string> { "自己找路", "接受它的提议" })),
        "rule uses enabled text rather than option position");
    expect(
        matches(make_facts(1, true, 1, std::vector<std::string> { "自己找路" })),
        "single enabled matching text remains explicit");
    for (int floor : { 2, 3, 4, 5 }) {
        expect(!matches(make_facts(floor, true, 2, visible)), "later floors retain existing evacuation policy");
    }
    expect(!matches(make_facts(std::nullopt, true, 2, visible)), "unknown floor cannot establish early-floor scope");
    expect(!matches(make_facts(1, false, 2, visible)), "stale count with unknown route cannot activate the rule");
    expect(!matches(make_facts(1, std::nullopt, 2, visible)), "missing route evidence cannot activate the rule");
    expect(!matches(make_facts(1, true, std::nullopt, visible)), "unknown remaining battle count preserves defaults");
    expect(!matches(make_facts(1, true, 0, visible)), "no planned battles leaves early evacuation available");
    expect(!matches(make_facts(1, true, 2, std::nullopt)), "unknown enabled options cannot imply a selectable choice");
    expect(
        !matches(make_facts(1, true, 2, std::vector<std::string> { "接受它的提议" })),
        "missing or disabled self-routing option does not activate the override");
    expect(
        !matches(make_facts(1, true, 2, std::vector<std::string> { "自行找路" })),
        "similar text is not exact option evidence");
    expect(!matches(observed, "三重身续篇"), "similar title cannot claim this rule");
    expect(!matches(observed, "沉寂之屋"), "other encounter cannot inherit the evacuation override");
    expect(!matches(FactStore {}), "positive result is not retained for an unknown following context");
    for (const auto& profile :
         { "start_run_animals", "start_run_rewards", "investment", "burn", "ending2", "ending3" }) {
        const auto other = BlackFlowStrategy.resolve_profile(profile);
        expect(
            other && resolve_encounter_rule(*other, observed, "三重身") == nullptr,
            std::string("other strategy keeps its original behavior: ") + profile);
    }
    FactStore silent;
    silent.set("encounter.available_options", std::vector<std::string> { "试图和人形藤蔓交流", "离开" });
    const auto* prior = resolve_encounter_rule(*policy, silent, "沉寂之屋");
    expect(
        prior && prior->id == "ending1_silent_house_preserve_life",
        "existing life-preservation rule remains reachable");
    std::cout << "EARLY_EVACUATE checks=" << checks << " failures=" << failures
              << " connections=0 inputs=0 live_choice=false combat_benefit_proven=false\n";
    return failures ? 1 : 0;
}
