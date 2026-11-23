#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "Config/Roguelike/BlackFlow/BlackFlowStrategyConfig.h"
#include "Config/Roguelike/RoguelikeShoppingConfig.h"
#include "Task/Roguelike/BlackFlow/BlackFlowPolicy.h"
#include "Utils/Platform.hpp"

namespace black_birthday_probe
{
using namespace asst::blackflow;

struct VisibleOption
{
    std::string text;
    bool enabled = false;
};

struct Fixture
{
    std::string name;
    std::string title;
    std::vector<VisibleOption> visible;
    bool available_fact_known = true;
    bool expect_override = false;
};

FactStore enabled_facts(const Fixture& fixture)
{
    FactStore facts;
    if (fixture.available_fact_known) {
        std::vector<std::string> enabled;
        for (const auto& option : fixture.visible) {
            if (option.enabled) {
                enabled.push_back(option.text);
            }
        }
        facts.set("encounter.available_options", enabled);
    }
    return facts;
}

bool matches(const EncounterRule* rule, bool expected)
{
    if (!expected) {
        return rule == nullptr;
    }
    return rule != nullptr && rule->id == "black_birthday_verified_cage_control" && rule->event_name == "黑诞" &&
           rule->option_text == std::vector<std::string> { "掏出笼控器控制猎狗" } && !rule->allow_fallback &&
           rule->choose == 0 && rule->option_num == 0 && rule->on_selected.empty();
}
} // namespace black_birthday_probe

int wmain(int argc, wchar_t** argv)
{
    using namespace black_birthday_probe;
    if (argc != 4) {
        return 2;
    }
    auto& shopping = asst::RoguelikeShoppingConfig::get_instance();
    auto& strategy = asst::BlackFlowStrategyConfig::get_instance();
    if (!shopping.load(std::filesystem::path(argv[3]))) {
        return 3;
    }
    const std::vector<std::string> profiles {
        "start_run_animals",
        "start_run_rewards",
        "start_run_rewards_with_investment",
        "ending1",
        "investment",
        "burn",
        "burn_with_investment",
        "baby_animal",
        "baby_animal_floor3",
        "ending2",
        "ending3",
    };
    const std::vector<Fixture> cases {
        { "control_first",
          "黑诞",
          { { "掏出笼控器控制猎狗", true }, { "和猎狗战斗到底", true }, { "落荒而逃", true } },
          true,
          true },
        { "control_reordered", "黑诞", { { "落荒而逃", true }, { "掏出笼控器控制猎狗", true } }, true, true },
        { "only_control", "黑诞", { { "掏出笼控器控制猎狗", true } }, true, true },
        { "control_disabled",
          "黑诞",
          { { "掏出笼控器控制猎狗", false }, { "和猎狗战斗到底", true }, { "落荒而逃", true } } },
        { "control_missing", "黑诞", { { "和猎狗战斗到底", true }, { "落荒而逃", true } } },
        { "unknown_enabled_state", "黑诞", { { "掏出笼控器控制猎狗", false } }, false },
        { "empty_options", "黑诞", {} },
        { "other_scene_same_control_text", "未涉足之树", { { "掏出笼控器控制猎狗", true } } },
        { "unknown_scene_same_control_text", "", { { "掏出笼控器控制猎狗", true } } },
        { "partial_title", "黑", { { "掏出笼控器控制猎狗", true } } },
        { "similar_title", "黑诞续篇", { { "掏出笼控器控制猎狗", true } } },
        { "title_suffix_space", "黑诞 ", { { "掏出笼控器控制猎狗", true } } },
        { "similar_option", "黑诞", { { "掏出笼控器控制猎犬", true }, { "落荒而逃", true } } },
        { "choice_text_without_title", "狭路相逢", { { "掏出笼控器控制猎狗", true } } },
    };
    std::size_t passed = 0;
    std::size_t failed = 0;
    auto check = [&](bool ok, const std::string& name) {
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
        ok ? ++passed : ++failed;
    };
    // Baseline uses the real owning parser/resolver, not a reimplemented JSON
    // evaluator.
    if (!strategy.load(std::filesystem::path(argv[1]))) {
        return 4;
    }
    for (const auto& id : profiles) {
        const auto policy = strategy.resolve_profile(id);
        if (!policy) {
            return 5;
        }
        check(
            resolve_encounter_rule(*policy, enabled_facts(cases.front()), "黑诞") == nullptr,
            "baseline_no_override/" + id);
    }
    if (!strategy.load(std::filesystem::path(argv[2]))) {
        return 6;
    }
    for (const auto& id : profiles) {
        const auto policy = strategy.resolve_profile(id);
        if (!policy) {
            return 7;
        }
        for (const auto& fixture : cases) {
            check(
                matches(
                    resolve_encounter_rule(*policy, enabled_facts(fixture), fixture.title),
                    fixture.expect_override),
                id + "/" + fixture.name);
        }
        // Missing facts and disabled fixtures cannot carry a previously selected
        // rule forward.
        check(resolve_encounter_rule(*policy, FactStore {}, "黑诞") == nullptr, id + "/fresh_unknown_after_positive");
        FactStore silent;
        silent.set("encounter.available_options", std::vector<std::string> { "试图和人形藤蔓交流", "离开" });
        const auto previous = resolve_encounter_rule(*policy, silent, "沉寂之屋");
        check(
            id == "ending1" ? previous != nullptr && previous->id == "ending1_silent_house_preserve_life"
                            : previous == nullptr,
            id + "/silent_house_unchanged");
    }
    std::cout << "SUMMARY passed=" << passed << " failed=" << failed << " devices=0 inputs=0\n";
    return failed == 0 ? 0 : 1;
}
