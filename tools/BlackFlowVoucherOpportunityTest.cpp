#include <iostream>

#include "Task/Roguelike/BlackFlow/BlackFlowVoucherOpportunity.h"

int main()
{
    using namespace asst;
    using namespace asst::blackflow;
    int checks = 0;
    int failures = 0;
    const auto expect = [&](bool value, const char* message) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << message << '\n';
        }
    };
    const VoucherOptionPreference free { "current-free", RoguelikeRecruitTier::FreeLowRarity, 0, 0 };
    const VoucherOptionPreference paid { "current-paid", RoguelikeRecruitTier::Suitable, 200, 2 };
    const VoucherOptionPreference preferred { "observed-qualified", RoguelikeRecruitTier::Suitable, 800, 6 };
    const std::vector<VoucherOptionPreference> deferred { preferred };
    VoucherOpportunityContext context { .hope = 4, .selected_offer_verified = true, .baseline_roles_present = true };
    auto result = choose_voucher_opportunity(context, free, deferred);
    expect(result.needs_storage_observation && !result.request_stash, "free voucher still has opportunity cost");
    expect(result.deferred_name == preferred.name, "bind to actually observed deferred identity");
    context.storage_observed = true;
    context.can_store = true;
    expect(choose_voucher_opportunity(context, free, deferred).request_stash, "zero hope choice can be retained");
    expect(choose_voucher_opportunity(context, paid, deferred).request_stash, "same tier strictly better priority");
    auto denied = context;
    denied.can_store = false;
    expect(
        !choose_voucher_opportunity(denied, free, deferred).request_stash,
        "full or unknown stash control uses old choice");
    denied = context;
    denied.selected_offer_verified = false;
    expect(
        !choose_voucher_opportunity(denied, free, deferred).request_stash,
        "unbound selected offer cannot authorize retention");
    denied = context;
    denied.fixed_opening = true;
    expect(!choose_voucher_opportunity(denied, free, deferred).request_stash, "fixed opening protected");
    denied = context;
    denied.baseline_roles_present = false;
    expect(
        !choose_voucher_opportunity(denied, free, deferred).request_stash,
        "missing core blocker or second output protected");
    denied = context;
    denied.selected_fills_immediate_need = true;
    expect(
        !choose_voucher_opportunity(denied, paid, deferred).request_stash,
        "urgent healer including resource grouped support protected");
    denied = context;
    denied.selected_is_promotion = true;
    expect(!choose_voucher_opportunity(denied, paid, deferred).request_stash, "promotion protected");
    denied = context;
    denied.selected_is_temporary = true;
    expect(!choose_voucher_opportunity(denied, free, deferred).request_stash, "exclusive temporary value protected");
    denied = context;
    denied.hope = -1;
    expect(!choose_voucher_opportunity(denied, free, deferred).request_stash, "unknown fresh hope protected");
    denied = context;
    denied.hope = 6;
    expect(
        !choose_voucher_opportunity(denied, free, deferred).request_stash,
        "now payable is not a deferred opportunity");
    expect(
        !choose_voucher_opportunity(context, free, {}).request_stash,
        "no observed future target cannot justify retention");
    expect(
        !choose_voucher_opportunity(context, paid, { { "equal", paid.tier, paid.score, 6 } }).request_stash,
        "equal configured preference is insufficient");
    expect(
        !choose_voucher_opportunity(context, paid, { { "lower", paid.tier, 100, 6 } }).request_stash,
        "weaker configured priority insufficient");
    expect(
        !choose_voucher_opportunity(context, paid, { { "lower-tier", RoguelikeRecruitTier::FreeLowRarity, 999, 6 } })
             .request_stash,
        "tier precedes numeric priority");
    expect(
        !choose_voucher_opportunity(context, free, { { "unknown-price", preferred.tier, 800, {} } }).request_stash,
        "missing exact price insufficient");
    expect(
        !choose_voucher_opportunity(context, free, { { free.name, preferred.tier, 800, 6 } }).request_stash,
        "same identity is not an independent future choice");
    expect(
        !choose_voucher_opportunity(context, { "unaffordable", paid.tier, 200, 5 }, deferred).request_stash,
        "invalid selected payable set cannot store by this rule");
    expect(
        !choose_voucher_opportunity(context, { "e2", RoguelikeRecruitTier::EliteTwo, 10, 2 }, deferred).request_stash,
        "current qualified elite-two acquisition protected");
    expect(
        choose_voucher_opportunity(context, paid, { { "tier-better", RoguelikeRecruitTier::EliteTwo, 1, 6 } })
            .request_stash,
        "same actual tier order used for deferred choices");
    expect(recruitment_scan_observation_limit(5, 16) == 16, "configured BlackFlow ceiling exceeds five pages");
    expect(recruitment_scan_observation_limit(5, 0) == 5, "invalid zero cap keeps old limit");
    expect(recruitment_scan_observation_limit(5, -1) == 5, "invalid negative cap keeps old limit");
    expect(recruitment_scan_observation_limit(5, 65) == 5, "unbounded cap rejected");
    std::cout << "VOUCHER_OPPORTUNITY checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
