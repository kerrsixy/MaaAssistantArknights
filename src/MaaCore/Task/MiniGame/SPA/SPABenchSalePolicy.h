#pragma once

#include "SPAFormation.h"
#include "SPAGrowth.h"
#include "SPAObservation.h"
#include "SPAOperatorCatalogue.h"
#include "SPAPlanner.h"
#include "SPAPreparationPolicy.h"
#include "SPAStrategyCore.h"
#include <algorithm>
#include <optional>

namespace asst::spa
{
// Quotes are accepted only above the unique identity-checked sale button.
// A bare number could be a level or attribute, not a sale quote.
inline std::optional<int> sale_credit_from_panel(const std::vector<TextObservation>& texts, int button_x, int button_y)
{
    std::optional<int> result;
    for (const auto& text : texts) {
        if (text.score < 0.9 || text.x < button_x - 100 || text.x > button_x + 100 || text.y < button_y - 90 ||
            text.y > button_y - 10) {
            continue;
        }
        std::string quote;
        for (const char c : text.text) {
            if (c != ' ' && c != '\t') {
                quote += c;
            }
        }
        if (quote.size() < 2 || quote.front() != '+' || quote.size() > 3 ||
            !std::all_of(quote.begin() + 1, quote.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            continue;
        }
        int amount = 0;
        for (size_t index = 1; index < quote.size(); ++index) {
            amount = amount * 10 + quote[index] - '0';
        }
        if (amount < 1 || amount > 99 || (result && *result != amount)) {
            return std::nullopt;
        }
        result = amount;
    }
    return result;
}

// Free one slot when the physical bench is full. Do not infer which copy is
// on the bench when a name is also deployed, sell pairs/elites, or trigger an
// unhandled sale reward. Printed bench-counting pacts and growth sources are
// retained even when that auxiliary pact is not currently active.
inline std::optional<std::string> choose_full_bench_sale(
    const PreparationPolicy& policy,
    const std::vector<std::string>& protected_names = {},
    int funding_deficit = 0)
{
    const bool fund_planned_card = funding_deficit > 0;
    if ((policy.bench_occupied < 10 && !fund_planned_card) || policy.shop_level < (fund_planned_card ? 1 : 2) ||
        policy.primary_pact.empty() || policy.primary_pact == "auto") {
        return std::nullopt;
    }
    const auto pacts = operator_pacts();
    const auto rules = pact_requirements();
    const auto& growth = growth_profiles();
    std::optional<std::string> result;
    int lowest_tier = 7;
    for (const auto& [name, owned] : policy.owned) {
        if (is_strategy_core(policy.selected_strategy_id, name) ||
            std::find(protected_names.begin(), protected_names.end(), name) != protected_names.end() ||
            owned.deployed || owned.elite || owned.copies != 1 ||
            ((policy.primary_locked_after_activation || fund_planned_card) &&
             belongs_to_planned_lineup(name, policy, pacts)) ||
            std::find(policy.carries.begin(), policy.carries.end(), name) != policy.carries.end() ||
            is_primary_growth_engine(name, policy.primary_pact) ||
            is_primary_output_candidate(name, policy.primary_pact)) {
            continue;
        }
        const auto facts = std::find_if(OperatorCatalogue.begin(), OperatorCatalogue.end(), [&](const auto& entry) {
            return entry.name == name;
        });
        const auto memberships = pacts.find(name);
        if (facts == OperatorCatalogue.end() || memberships == pacts.end() || facts->sale_has_side_effect ||
            facts->trait_mentions_bench || facts->price <= 0 ||
            (!policy.primary_locked_after_activation && !fund_planned_card && facts->tier >= policy.shop_level) ||
            facts->tier >= lowest_tier ||
            std::find(memberships->second.begin(), memberships->second.end(), policy.primary_pact) !=
                memberships->second.end()) {
            continue;
        }
        if (std::any_of(rules.begin(), rules.end(), [&](const auto& rule) {
                return rule.bench_counts &&
                       std::find(policy.disabled_pacts.begin(), policy.disabled_pacts.end(), rule.name) ==
                           policy.disabled_pacts.end() &&
                       std::find(memberships->second.begin(), memberships->second.end(), rule.name) !=
                           memberships->second.end();
            })) {
            continue;
        }
        if (const auto source = growth.find(name); source != growth.end() && source->second.works_on_bench) {
            continue;
        }
        result = name;
        lowest_tier = facts->tier;
    }
    return result;
}

struct FundingPurchase
{
    ShopOffer offer;
    int deficit = 0;
};

struct BenchCapacityTrade
{
    std::string spare_initial;
    ShopOffer purchase;
};

inline std::optional<BenchCapacityTrade> choose_bench_capacity_trade(
    const PreparationPolicy& policy,
    int funds,
    const std::vector<ShopOffer>& blocked_offers,
    const std::map<std::string, int>& verified_field_counts,
    const std::vector<std::string>& reserved_names)
{
    if (policy.bench_occupied != 10 || !policy.primary_locked_after_activation || policy.shop_level < 2 ||
        verified_field_counts.empty()) {
        return std::nullopt;
    }
    const auto pacts = operator_pacts();
    std::vector<ShopOffer> worthwhile_offers;
    std::array<bool, 6> observed_slots {};
    for (const auto& offer : blocked_offers) {
        if (offer.right_slot < 0 || offer.right_slot >= static_cast<int>(observed_slots.size()) ||
            observed_slots[offer.right_slot]) {
            return std::nullopt;
        }
        observed_slots[offer.right_slot] = true;
        if (offer.price <= 0 || offer.price > funds || !belongs_to_planned_lineup(offer.name, policy, pacts)) {
            continue;
        }
        const auto on_field = verified_field_counts.find(offer.name);
        const bool promotes = on_field != verified_field_counts.end() && on_field->second == 1 &&
                              ordinary_purchase_completes_promotion(policy, offer.name);
        const auto fit = policy.full_field_offer_improvements.find(offer.name);
        if (promotes || (fit != policy.full_field_offer_improvements.end() && fit->second > 0)) {
            worthwhile_offers.push_back(offer);
        }
    }
    auto available = policy;
    --available.bench_occupied;
    const auto purchase = choose_preparation(available, funds, worthwhile_offers, pacts);
    if (purchase.action != PreparationAction::Buy || purchase.banked_value_purchase) {
        return std::nullopt;
    }
    auto protected_names = reserved_names;
    for (const auto& offer : worthwhile_offers) {
        protected_names.push_back(offer.name);
    }
    if (const auto expendable = choose_full_bench_sale(policy, protected_names)) {
        return BenchCapacityTrade { *expendable, { purchase.name, purchase.price, purchase.right_slot } };
    }
    const auto growth = growth_profiles();
    const OperatorCatalogueEntry* lowest = nullptr;
    for (const auto& facts : OperatorCatalogue) {
        const std::string name(facts.name);
        const auto owned = policy.owned.find(name);
        const auto field = verified_field_counts.find(name);
        if (owned == policy.owned.end() || field == verified_field_counts.end() || field->second != 1 ||
            !owned->second.deployed || facts.sale_has_side_effect || facts.trait_mentions_bench || facts.price <= 0 ||
            is_strategy_core(policy.selected_strategy_id, name) ||
            std::find(protected_names.begin(), protected_names.end(), name) != protected_names.end()) {
            continue;
        }
        const auto forms = owned_form_counts(owned->second);
        // This name is already represented on the field, so the sale
        // preserves its distinct contribution even for bench-counting pacts.
        // Per-instance effects in the bench still block the sale.
        if (!forms || forms->initial <= field->second || !pacts.contains(name)) {
            continue;
        }
        if (const auto source = growth.find(name); source != growth.end() && source->second.works_on_bench) {
            continue;
        }
        if (!lowest || facts.tier < lowest->tier) {
            lowest = &facts;
        }
    }
    if (!lowest) {
        return std::nullopt;
    }
    return BenchCapacityTrade { std::string(lowest->name), { purchase.name, purchase.price, purchase.right_slot } };
}

// Compare the actual shop under a hypothetical funded budget, then request
// a verified bench sale. This does not grant funds or authorize a purchase.
inline std::optional<FundingPurchase> choose_funding_purchase(
    const PreparationPolicy& policy,
    int funds,
    const std::vector<ShopOffer>& offers,
    const std::map<std::string, std::vector<std::string>>& pacts)
{
    if (funds < 0 || policy.primary_pact.empty() || policy.primary_pact == "auto" || policy.bench_occupied < 1 ||
        std::find(policy.disabled_pacts.begin(), policy.disabled_pacts.end(), policy.primary_pact) !=
            policy.disabled_pacts.end()) {
        return std::nullopt;
    }
    int funded_budget = funds;
    for (const auto& offer : offers) {
        if (offer.price >= 0 && offer.price <= 99) {
            funded_budget = std::max(funded_budget, offer.price);
        }
    }
    const auto choice = choose_preparation(policy, funded_budget, offers, pacts);
    if (choice.action != PreparationAction::Buy || choice.price <= funds ||
        choice.selected_pact != policy.primary_pact) {
        return std::nullopt;
    }
    const auto membership = pacts.find(choice.name);
    if (membership == pacts.end() ||
        std::find(membership->second.begin(), membership->second.end(), policy.primary_pact) ==
            membership->second.end()) {
        return std::nullopt;
    }
    int members = 0;
    for (const auto& [name, owned] : policy.owned) {
        const auto traits = pacts.find(name);
        members += (owned.copies > 0 || owned.elite) && traits != pacts.end() &&
                   std::find(traits->second.begin(), traits->second.end(), policy.primary_pact) != traits->second.end();
    }
    const auto rules = pact_requirements();
    const auto rule =
        std::find_if(rules.begin(), rules.end(), [&](const auto& item) { return item.name == policy.primary_pact; });
    const auto owned = policy.owned.find(choice.name);
    const int copies = owned == policy.owned.end() ? 0 : owned->second.copies;
    const bool activation =
        copies == 0 && rule != rules.end() && members < rule->members && members + 1 == rule->members;
    const bool role = is_primary_growth_engine(choice.name, policy.primary_pact) ||
                      is_primary_output_candidate(choice.name, policy.primary_pact);
    const bool promotion = role && ordinary_purchase_completes_promotion(policy, choice.name);
    if (!activation && !promotion && !(policy.primary_locked_after_activation && role && copies == 0)) {
        return std::nullopt;
    }
    return FundingPurchase { { choice.name, choice.price, choice.right_slot }, choice.price - funds };
}
}
