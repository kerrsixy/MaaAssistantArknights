#include "Task/MiniGame/SPA/SPAFormation.h"
#include "Task/MiniGame/SPA/SPAPreparationPolicy.h"
#include <cassert>
#include <iostream>

using namespace asst::spa;

static PreparationPolicy active_iberia()
{
    PreparationPolicy policy;
    policy.primary_pact = "伊比利亚";
    policy.primary_locked_after_activation = true;
    policy.dynamic_decision_mode = true;
    policy.shop_level = 2;
    policy.round = 5;
    policy.early_board_ready = true;
    policy.last_round_life_loss = 0;
    policy.deployed_population = 4;
    policy.bench_occupied = 0;
    policy.owned = { { "温蒂", { 1, false, true } },
                     { "流明", { 1, false, true } },
                     { "蓝毒", { 1, false, true } },
                     { "幽灵鲨", { 1, false, true } } };
    policy.observed_pact_layers["伊比利亚"] = 17;
    policy.growth_primary_active_verified = true;
    policy.growth_scenario_horizon_round = 9;
    return policy;
}

int main()
{
    const auto pacts = operator_pacts();
    const std::vector<ShopOffer> deep { { "深靛", 2, 0 } };
    auto policy = active_iberia();
    auto reserved = fixed_growth_purchase_reservation(policy, 9, 8, deep, pacts);
    assert(reserved && reserved->name == "深靛" && reserved->price == 2);
    assert(reserved->current_layers == 17 && reserved->scenario_horizon_round == 9);
    assert(reserved->remaining_phase_triggers == 5 && reserved->fixed_gain_per_trigger == 2);
    assert(reserved->fixed_layer_additions_if_retained == 10 && reserved->placement_required);
    assert(reserved->reserve_before_upgrade);
    assert(!should_upgrade_dynamic_shop(policy, 9, 8, deep, pacts));

    // The same board used to take the early-book upgrade and lose this card's
    // budget. Unknown facts retain that existing behavior, rather than
    // inventing a minimum yield from a conditional cap.
    auto unknown = policy;
    unknown.growth_primary_active_verified = false;
    assert(!fixed_growth_purchase_reservation(unknown, 9, 8, deep, pacts));
    assert(should_upgrade_dynamic_shop(unknown, 9, 8, deep, pacts));
    unknown = policy;
    unknown.observed_pact_layers.clear();
    assert(!fixed_growth_purchase_reservation(unknown, 9, 8, deep, pacts));
    unknown = policy;
    unknown.observed_pact_layers["伊比利亚"] = -1;
    assert(!fixed_growth_purchase_reservation(unknown, 9, 8, deep, pacts));
    unknown = policy;
    unknown.growth_scenario_horizon_round = 0;
    assert(!fixed_growth_purchase_reservation(unknown, 9, 8, deep, pacts));

    // Merely buying and upgrading in the same preparation is not a conflict.
    reserved = fixed_growth_purchase_reservation(policy, 10, 8, deep, pacts);
    assert(reserved && !reserved->reserve_before_upgrade);
    assert(should_upgrade_dynamic_shop(policy, 10, 8, deep, pacts));
    assert(!fixed_growth_purchase_reservation(policy, 1, 1, deep, pacts));
    assert(!fixed_growth_purchase_reservation(policy, 9, 8, {}, pacts));
    assert(!fixed_growth_purchase_reservation(policy, 9, 8, { { "深靛", 2, -1 } }, pacts));

    auto blocked = policy;
    blocked.disabled_pacts = { "伊比利亚" };
    assert(!fixed_growth_purchase_reservation(blocked, 9, 8, deep, pacts));
    blocked = policy;
    blocked.deployed_population = 8;
    blocked.full_field_offer_improvements["深靛"] = 5000;
    assert(!fixed_growth_purchase_reservation(blocked, 9, 8, deep, pacts));
    blocked = policy;
    blocked.deployed_population = -1;
    assert(!fixed_growth_purchase_reservation(blocked, 9, 8, deep, pacts));
    blocked = policy;
    blocked.bench_occupied = 10;
    assert(!fixed_growth_purchase_reservation(blocked, 9, 8, deep, pacts));

    // A duplicate initial card is not a new guaranteed fixed source; an
    // actual third-copy promotion remains handled by the existing guard.
    blocked = policy;
    blocked.owned["深靛"] = { 1, false, true };
    assert(!fixed_growth_purchase_reservation(blocked, 9, 8, deep, pacts));
    blocked.owned["深靛"].copies = 2;
    assert(!should_upgrade_dynamic_shop(blocked, 9, 8, deep, pacts));

    // The current preparation-end trigger is still ahead at the horizon.
    auto terminal = policy;
    terminal.growth_scenario_horizon_round = terminal.round;
    reserved = fixed_growth_purchase_reservation(terminal, 9, 8, deep, pacts);
    assert(reserved && reserved->remaining_phase_triggers == 1);
    assert(reserved->fixed_layer_additions_if_retained == 2);

    // Preparation-start already happened. Only the printed self-recipient
    // gets modeled; no unverified operator in front of Archetto is added.
    auto precision = policy;
    precision.primary_pact = "精准";
    precision.shop_level = 3;
    precision.observed_pact_layers = { { "精准", 20 } };
    precision.growth_scenario_horizon_round = precision.round;
    const std::vector<ShopOffer> archetto { { "空弦", 3, 0 } };
    assert(!fixed_growth_purchase_reservation(precision, 9, 8, archetto, pacts));
    precision.growth_scenario_horizon_round = precision.round + 2;
    reserved = fixed_growth_purchase_reservation(precision, 9, 8, archetto, pacts);
    assert(reserved && reserved->remaining_phase_triggers == 2);
    assert(reserved->fixed_layer_additions_if_retained == 6);

    // A capped deployment/kill source cannot become a fixed lower bound.
    auto egir = policy;
    egir.primary_pact = "阿戈尔";
    egir.observed_pact_layers = { { "阿戈尔", 48 } };
    const std::vector<ShopOffer> andreana { { "安哲拉", 3, 0 } };
    assert(!fixed_growth_purchase_reservation(egir, 9, 8, andreana, pacts));

    // Unmodeled on-acquisition and temporary/converted growth are excluded.
    auto arcane = policy;
    arcane.primary_pact = "奥术";
    arcane.observed_pact_layers = { { "奥术", 25 } };
    assert(!fixed_growth_purchase_reservation(arcane, 9, 8, { { "逻各斯", 4, 0 } }, pacts));
    auto ursus = policy;
    ursus.primary_pact = "乌萨斯";
    ursus.observed_pact_layers = { { "乌萨斯", 25 } };
    assert(!fixed_growth_purchase_reservation(ursus, 9, 8, { { "折桠", 2, 0 } }, pacts));

    std::cout << "Fixed growth reservation invariants passed\n";
}
