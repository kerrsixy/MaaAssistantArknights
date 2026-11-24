#include <filesystem>
#include <iostream>
#include <unordered_map>

#include "Config/Roguelike/RoguelikeShoppingConfig.h"
#include "Task/Roguelike/BlackFlow/BlackFlowShopPriority.h"
#include "Task/Roguelike/RoguelikeRecruitPolicy.h"
#include "Utils/Platform.hpp"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) {
        return 2;
    }
    using namespace asst;
    using Role = battle::Role;
    const std::filesystem::path repo(argv[1]);
    if (!RoguelikeShopping.load(repo / utils::path("resource/roguelike/BlackFlow/shopping.json"))) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool value, const char* label) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const auto names = [](const auto& goods) {
        std::vector<std::string> result;
        for (const auto& item : goods) {
            result.emplace_back(item.name);
        }
        return result;
    };
    const auto original = RoguelikeShopping.get_shop_goods("BlackFlow", "default", "ending1");
    const auto ticket = std::ranges::find(original, "医疗招募券", &RoguelikeGoods::name);
    const auto seed = std::ranges::find(original, "种子", &RoguelikeGoods::name);
    if (ticket == original.end() || seed == original.end()) {
        return 4;
    }
    expect(seed < ticket, "run67 original production profile ranks seed before medical voucher");
    expect(ticket->min_hope == 1 && ticket->recruit_role == Role::Medic, "actual voucher gate accepts four hope");
    const std::unordered_map<Role, std::size_t> actual_team { { Role::Tank, 1 },
                                                              { Role::Sniper, 2 },
                                                              { Role::Caster, 2 },
                                                              { Role::Special, 1 } };
    auto candidate = original;
    blackflow::prioritize_first_medic(candidate, actual_team, 4);
    const auto first_available = [](const auto& goods, std::optional<int> wallet, int reserved) {
        const std::unordered_map<std::string, int> shelf { { "种子", 4 }, { "医疗招募券", 4 } };
        for (const auto& item : goods) {
            const auto price = shelf.find(item.name);
            if (wallet && price != shelf.end() && price->second <= std::max(0, *wallet - reserved)) {
                return item.name;
            }
        }
        return std::string();
    };
    expect(first_available(original, 5, 0) == "种子", "captured shelf and wallet reproduce old first purchase");
    expect(first_available(candidate, 5, 0) == "医疗招募券", "same stock and wallet first address the missing medic");
    expect(first_available(candidate, 3, 0).empty(), "priority cannot make an unaffordable voucher affordable");
    expect(first_available(candidate, 5, 2).empty(), "reserved cash remains unavailable");
    expect(first_available(candidate, std::nullopt, 0).empty(), "unknown wallet is not a spending allowance");
    auto nonpriority_before = original;
    auto nonpriority_after = candidate;
    std::erase_if(nonpriority_before, [&](const auto& item) {
        return blackflow::prioritizes_first_medic(item, actual_team, 4);
    });
    std::erase_if(nonpriority_after, [&](const auto& item) {
        return blackflow::prioritizes_first_medic(item, actual_team, 4);
    });
    expect(names(nonpriority_before) == names(nonpriority_after), "all other relative priorities remain stable");
    for (const std::optional<int> hope : { std::optional<int>(), std::optional<int>(0), std::optional<int>(-1) }) {
        auto limited = original;
        blackflow::prioritize_first_medic(limited, actual_team, hope);
        expect(names(limited) == names(original), "unknown or insufficient hope cannot activate the override");
    }
    auto with_healer = actual_team;
    with_healer[Role::Medic] = 1;
    auto restored = original;
    blackflow::prioritize_first_medic(restored, with_healer, 4);
    expect(names(restored) == names(original), "one trained healer restores the original collection ordering");
    auto mixed = *ticket;
    mixed.recruit_roles = { Role::Sniper, Role::Medic };
    mixed.recruit_role = Role::Caster;
    expect(blackflow::prioritizes_first_medic(mixed, actual_team, 4), "explicit mixed voucher can fill healing duty");
    mixed.recruit_roles = { Role::Sniper, Role::Caster };
    mixed.recruit_role = Role::Medic;
    expect(
        !blackflow::prioritizes_first_medic(mixed, actual_team, 4),
        "legacy role cannot widen a precise mixed ticket");
    auto unspecified = *ticket;
    unspecified.recruit_roles.clear();
    unspecified.recruit_role.reset();
    expect(
        !blackflow::prioritizes_first_medic(unspecified, actual_team, 4),
        "unspecified voucher is not assumed medical");
    auto gated = *ticket;
    gated.min_hope = 6;
    expect(
        !blackflow::prioritizes_first_medic(gated, actual_team, 4),
        "voucher-specific hope threshold remains enforced");
    gated.min_hope = 0;
    expect(
        !blackflow::prioritizes_first_medic(gated, actual_team, 4),
        "unconfigured hope budget retains original rejection");
    gated = *ticket;
    gated.is_recruit_ticket = false;
    expect(!blackflow::prioritizes_first_medic(gated, actual_team, 4), "collection cannot enter voucher priority tier");
    expect(
        is_recruitment_strength_sufficient(1, 55),
        "trained three-star healer counts below old level60 shopping cutoff");
    expect(!is_recruitment_strength_sufficient(1, 1), "undertrained E1 member cannot close the qualified role gap");
    expect(is_recruitment_strength_sufficient(2, 1), "shared recruitment gate accepts an already-promoted member");
    std::cout << "SHOP_FIRST_MEDIC checks=" << checks << " failures=" << failures
              << " connections=0 inputs=0 damage_simulated=false live_purchase=false\n";
    return failures ? 1 : 0;
}
