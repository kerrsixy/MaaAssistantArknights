#include <algorithm>
#include <iostream>
#include <limits>

#include "Task/Roguelike/BlackFlow/BlackFlowMovementCapacity.h"
#include "Task/Roguelike/BlackFlow/BlackFlowScrapRefresh.h"

int main()
{
    using namespace asst::blackflow;
    int checks = 0, failures = 0;
    const auto expect = [&](bool value, const char* reason) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << reason << '\n';
        }
    };
    const auto shelf = [] {
        ScrapRefreshShelf result;
        result.complete = true;
        for (int slot = 0; slot < 6; ++slot) {
            result.slots[slot] = { slot, "canonical-" + std::to_string(slot), false };
        }
        return result;
    };
    const auto before = shelf();
    auto changed = before;
    changed.slots[2].canonical_name = "canonical-replacement";
    const auto intent = [&](std::uint64_t sequence = 10, int index = 0) {
        return ScrapRefreshIntent { 20, 4, index, sequence, before };
    };
    const auto observed =
        [&](std::optional<ScrapRefreshShelf> value = std::nullopt, int wallet = 16, std::uint64_t sequence = 11) {
            return ScrapRefreshObservation { sequence, true, false, wallet, value ? *value : changed };
        };
    {
        ScrapRefreshTransaction tx;
        expect(tx.begin(intent()), "owned refresh starts with complete canonical shelf");
        expect(!tx.begin(intent(12, 1)), "pending refresh cannot be overwritten");
        tx.note_confirmation_input(10);
        expect(!tx.take_confirmed(), "confirmation input alone cannot settle refresh");
        expect(tx.observe(observed()) == ScrapRefreshState::Confirmed, "fresh debit and changed complete shelf settle");
        const auto receipt = tx.take_confirmed();
        expect(
            receipt && receipt->paid == 4 && receipt->intent.refresh_index == 0 && receipt->intent.frame_sequence == 10,
            "receipt retains exact owned intent and actual cost");
        expect(!tx.take_confirmed(), "one refresh produces at most one receipt");
        tx.note_confirmation_input(10);
        expect(
            tx.observe(observed()) == ScrapRefreshState::Idle && !tx.take_confirmed(),
            "duplicate callback and old frame do not recreate receipt");
        expect(!tx.begin(intent()), "consumed capture token cannot be reused");
        expect(!tx.begin(intent(11)), "next intent must be newer than the consumed receipt frame");
        expect(tx.begin(intent(20, 1)), "later independent owned intent can start");
        tx.note_confirmation_input(10);
        expect(
            tx.observe(observed(changed, 16, 21)) == ScrapRefreshState::AwaitingReceipt,
            "late prior callback cannot acknowledge new refresh");
        tx.note_confirmation_input(20);
        expect(
            tx.observe(observed(changed, 16, 22)) == ScrapRefreshState::Confirmed,
            "matching new callback acknowledges only current intent");
    }
    {
        ScrapRefreshTransaction tx;
        expect(tx.begin(intent()), "discounted refresh starts");
        tx.note_confirmation_input(10);
        expect(
            tx.observe(observed(changed, 18)) == ScrapRefreshState::Confirmed,
            "positive discount debit bounded by approved maximum qualifies");
        const auto receipt = tx.take_confirmed();
        expect(receipt && receipt->paid == 2, "discount receipt records actual two ingots");
    }
    for (int after : { -1, 21, 15 }) {
        ScrapRefreshTransaction tx;
        expect(tx.begin(intent()), "invalid debit scenario starts");
        tx.note_confirmation_input(10);
        expect(
            tx.observe(observed(changed, after)) == ScrapRefreshState::Failed &&
                tx.failure() == ScrapRefreshFailure::InvalidDebit && !tx.take_confirmed(),
            "negative, increased or excessive debit fails without receipt");
        expect(!tx.begin(intent(20)), "failed refresh cannot silently be replaced");
    }
    auto fewer = changed;
    fewer.complete = false;
    auto missing_name = changed;
    missing_name.slots[1].canonical_name.clear();
    auto duplicate_slot = changed;
    duplicate_slot.slots[1].slot = 0;
    auto out_of_range = changed;
    out_of_range.slots[1].slot = 6;
    auto reordered = before;
    std::reverse(reordered.slots.begin(), reordered.slots.end());
    for (const auto& invalid : { before, reordered, fewer, missing_name, duplicate_slot, out_of_range }) {
        ScrapRefreshTransaction tx;
        expect(tx.begin(intent()), "unproven shelf scenario starts");
        tx.note_confirmation_input(10);
        for (std::uint64_t sequence : { 11, 12 }) {
            expect(
                tx.observe(observed(invalid, 16, sequence)) == ScrapRefreshState::AwaitingReceipt,
                "unchanged, reordered or incomplete shelf is not a new generation");
        }
        expect(
            tx.observe(observed(invalid, 16, 13)) == ScrapRefreshState::Failed &&
                tx.failure() == ScrapRefreshFailure::ReceiptUnconfirmed && !tx.take_confirmed(),
            "unproven shelf fails within exactly three observations");
    }
    {
        auto unsold = before;
        unsold.slots[3].sold = true;
        auto request = intent();
        request.shelf_before = unsold;
        ScrapRefreshTransaction tx;
        expect(tx.begin(request), "sold-slot generation scenario starts");
        tx.note_confirmation_input(10);
        expect(
            tx.observe(observed(before)) == ScrapRefreshState::Confirmed,
            "same canonical names with sold slot reset prove changed shelf");
    }
    for (int scenario = 0; scenario < 7; ++scenario) {
        ScrapRefreshTransaction tx;
        expect(tx.begin(intent()), "receipt counterexample starts");
        if (scenario != 0) {
            tx.note_confirmation_input(10);
        }
        auto value = observed();
        if (scenario == 1) {
            value.buying_page = false;
        }
        if (scenario == 2) {
            value.confirmation_visible = true;
        }
        if (scenario == 3) {
            value.wallet.reset();
        }
        if (scenario == 4) {
            value.wallet = 20;
        }
        if (scenario == 5) {
            value.frame_sequence = 10;
        }
        if (scenario == 6) {
            value.shelf.reset();
        }
        expect(
            tx.observe(value) == ScrapRefreshState::AwaitingReceipt && !tx.take_confirmed(),
            "missing input, wrong page, dialog, unknown/unchanged wallet, old frame or missing shelf cannot settle");
        value.frame_sequence = scenario == 5 ? 10 : 12;
        expect(tx.observe(value) == ScrapRefreshState::AwaitingReceipt, "second invalid observation stays pending");
        value.frame_sequence = scenario == 5 ? 10 : 13;
        expect(
            tx.observe(value) == ScrapRefreshState::Failed && !tx.take_confirmed(),
            "unknown receipt does not wait or retry indefinitely");
    }
    {
        ScrapRefreshTransaction tx;
        expect(tx.begin(intent()), "freshness retry scenario starts");
        tx.note_confirmation_input(10);
        auto value = observed(before);
        expect(tx.observe(value) == ScrapRefreshState::AwaitingReceipt, "unchanged first frame remains pending");
        value.shelf = changed;
        expect(
            tx.observe(value) == ScrapRefreshState::AwaitingReceipt,
            "same capture token cannot be edited into a fresh receipt");
        value.frame_sequence = 12;
        expect(tx.observe(value) == ScrapRefreshState::Confirmed, "third genuinely fresh frame can confirm");
    }
    {
        ScrapRefreshTransaction tx;
        expect(tx.begin(intent()), "duplicate canonical names in distinct slots are allowed");
        tx.note_confirmation_input(10);
        auto repeated_names = changed;
        repeated_names.slots[4].canonical_name = repeated_names.slots[3].canonical_name;
        expect(
            tx.observe(observed(repeated_names)) == ScrapRefreshState::Confirmed,
            "slot identity is distinct from canonical item identity");
    }
    for (int scenario = 0; scenario < 8; ++scenario) {
        auto request = intent();
        if (scenario == 0) {
            request.wallet_before = -1;
        }
        if (scenario == 1) {
            request.maximum_cost = 0;
        }
        if (scenario == 2) {
            request.maximum_cost = 21;
        }
        if (scenario == 3) {
            request.refresh_index = -1;
        }
        if (scenario == 4) {
            request.frame_sequence = 0;
        }
        if (scenario == 5) {
            request.shelf_before = fewer;
        }
        if (scenario == 6) {
            request.shelf_before = duplicate_slot;
        }
        if (scenario == 7) {
            request.shelf_before = missing_name;
        }
        ScrapRefreshTransaction tx;
        expect(
            !tx.begin(request) && tx.state() == ScrapRefreshState::Idle,
            "invalid intent is rejected before input including unproven free refresh");
    }
    {
        const auto context = [] {
            ScrapMovementRefreshContext value;
            value.wallet = 30;
            value.reserved = 10;
            value.maximum_cost = 4;
            value.inventory_complete = true;
            value.capacity_known_and_available = true;
            value.shop_pool_known = true;
            return value;
        };
        const auto target = [](std::string name = "wheel", int price = 4) {
            return ScrapMovementRefreshTarget { std::move(name), price, true, true, false, true, true };
        };
        const auto decide = [&](const ScrapMovementRefreshContext& value, ScrapMovementRefreshTarget item) {
            return decide_scrap_movement_refresh(value, { std::move(item) });
        };
        auto allowed = decide(context(), target());
        expect(
            allowed.allow && allowed.canonical_name == "wheel" && allowed.maximum_cost == 4 &&
                allowed.reason == "budgeted_search",
            "known missing movement target qualifies exactly at price plus four refresh costs");
        auto value = context();
        value.wallet = 29;
        auto result = decide(value, target());
        expect(
            !result.allow && result.reason == "insufficient_budget" && result.canonical_name.empty() &&
                result.maximum_cost == 0,
            "one ingot below protected budget cannot produce an actionable intent");
        value = context();
        value.reserved = 11;
        expect(!decide(value, target()).allow, "reserve is deducted before considering search");
        value.reserved = 31;
        expect(!decide(value, target()).allow, "reserve exceeding wallet does not create negative or spendable funds");
        value = context();
        value.inventory_complete = false;
        expect(decide(value, target()).reason == "inventory_unknown", "incomplete inventory cannot prove a shape gap");
        value = context();
        value.capacity_known_and_available = false;
        expect(
            decide(value, target()).reason == "capacity_unavailable",
            "unknown or unavailable capacity rejects search");
        value = context();
        value.shop_pool_known = false;
        expect(decide(value, target()).reason == "shop_pool_unknown", "unknown shop pool is not inferred from targets");
        value = context();
        value.refresh_index = 1;
        expect(
            decide(value, target()).reason == "already_refreshed",
            "ordinary shop allows at most one refresh per visit");
        for (int scenario = 0; scenario < 6; ++scenario) {
            auto item = target();
            if (scenario == 0) {
                item.fills_missing_shape = false;
            }
            if (scenario == 1) {
                item.already_bought_this_visit = true;
            }
            if (scenario == 2) {
                item.in_current_shop_pool = false;
            }
            if (scenario == 3) {
                item.canonical_source_known = false;
            }
            if (scenario == 4) {
                item.capacity_legal_after_purchase = false;
            }
            if (scenario == 5) {
                item.trusted_price.reset();
            }
            expect(
                decide(context(), item).reason == "no_eligible_target",
                "covered, already supplemented, out-of-pool, unknown source, illegal capacity or unknown price target "
                "excluded");
        }
        auto expensive = target("expensive", 5);
        auto cheapest = target("a-cheapest", 4);
        auto tied = target("z-cheapest", 4);
        auto untrusted = target("untrusted-cheaper", 1);
        untrusted.canonical_source_known = false;
        auto outside_pool = target("outside-pool-cheaper", 1);
        outside_pool.in_current_shop_pool = false;
        std::vector<ScrapMovementRefreshTarget> choices { expensive, tied, untrusted, outside_pool, cheapest };
        expect(
            decide_scrap_movement_refresh(context(), choices).canonical_name == "a-cheapest",
            "cheapest eligible target wins with deterministic canonical-name tie break");
        std::reverse(choices.begin(), choices.end());
        expect(
            decide_scrap_movement_refresh(context(), choices).canonical_name == "a-cheapest",
            "input order cannot alter deterministic target selection");
        expect(
            decide_scrap_movement_refresh(context(), {}).reason == "no_eligible_target",
            "empty target list cannot authorize refresh");
        for (int scenario = 0; scenario < 2; ++scenario) {
            value = context();
            if (scenario == 0) {
                value.wallet.reset();
            }
            if (scenario == 1) {
                value.reserved.reset();
            }
            expect(decide(value, target()).reason == "funds_unknown", "wallet and reserve must both be trusted");
        }
        for (int scenario = 0; scenario < 7; ++scenario) {
            value = context();
            if (scenario == 0) {
                value.wallet = -1;
            }
            if (scenario == 1) {
                value.reserved = -1;
            }
            if (scenario == 2) {
                value.maximum_cost = 0;
            }
            if (scenario == 3) {
                value.maximum_cost = -1;
            }
            if (scenario == 4) {
                value.refresh_index = -1;
            }
            if (scenario == 5) {
                value.budget_denominator = 0;
            }
            if (scenario == 6) {
                value.budget_denominator = -1;
            }
            expect(
                decide(value, target()).reason == "invalid_input",
                "negative inputs, unproven free refresh and invalid budget denominator reject");
        }
        auto invalid = target();
        invalid.trusted_price = -1;
        expect(decide(context(), invalid).reason == "invalid_input", "negative trusted price is malformed");
        invalid = target();
        invalid.canonical_name.clear();
        expect(
            decide(context(), invalid).reason == "invalid_input",
            "known source cannot have an empty canonical name");
        value = context();
        value.budget_denominator = 2;
        value.wallet = 22;
        expect(decide(value, target()).allow, "explicit positive denominator tunes the protected search budget");
        value = context();
        value.wallet = std::numeric_limits<int>::max();
        value.reserved = 0;
        value.maximum_cost = std::numeric_limits<int>::max();
        value.budget_denominator = std::numeric_limits<int>::max();
        expect(decide(value, target()).reason == "budget_overflow", "product overflow cannot wrap into approval");
        value = context();
        value.wallet = std::numeric_limits<int>::max();
        value.reserved = 0;
        value.maximum_cost = std::numeric_limits<int>::max() / 4;
        expect(decide(value, target()).reason == "budget_overflow", "sum overflow cannot wrap into approval");
        value.maximum_cost = 1;
        expect(
            decide(value, target("large-but-valid", std::numeric_limits<int>::max() - 4)).allow,
            "largest representable exact budget remains valid without overflow");
    }
    {
        expect(
            recognize_movement_capacity_from_text(" 8 / 8 ") == std::pair { 8, 8 },
            "full inventory remains a valid used/capacity reading");
        expect(
            recognize_movement_capacity_from_text("0/64") == std::pair { 0, 64 },
            "native zero-used and maximum capacity bounds preserved");
        for (const auto& invalid :
             { "9/8", "0/0", "0/65", "-1/8", "8", "8//8", "/8", "8/", "O/8", "8/8x", "999999999999999999999/8" }) {
            expect(!recognize_movement_capacity_from_text(invalid), "malformed capacity cannot imply free slots");
        }
        const std::vector<MovementCapacityTextFragment> split { { 777, 684, 9, 18, "8" },
                                                                { 746, 684, 9, 18, "7" },
                                                                { 761, 684, 9, 18, "/" } };
        expect(
            movement_capacity_from_fragments(split) == std::pair { 7, 8 },
            "split footer OCR merges by verified horizontal geometry rather than detector order");
        auto invalid = split;
        invalid[0].y = 704;
        expect(!movement_capacity_from_fragments(invalid), "different OCR lines cannot be concatenated");
        invalid = split;
        invalid[0].x = 764;
        expect(!movement_capacity_from_fragments(invalid), "overlapping duplicate OCR fragments are rejected");
        invalid = split;
        invalid[0].x = 805;
        expect(!movement_capacity_from_fragments(invalid), "distant numbers cannot be joined into capacity");
        invalid = split;
        invalid[1].text = "8/8";
        expect(
            !movement_capacity_from_fragments(invalid),
            "conflicting whole and split readings do not select a convenient result");
        expect(scrap_refresh_capacity_can_fit(7, 8, 7, 1), "one remaining slot can hold exactly one acquisition");
        expect(!scrap_refresh_capacity_can_fit(8, 8, 8, 1), "full eight of eight has no spare slot");
        expect(!scrap_refresh_capacity_can_fit(7, 8, 7, 2), "bundle cannot exceed remaining space");
        expect(!scrap_refresh_capacity_can_fit(7, 8, 6, 1), "unaccounted inventory invalidates shape knowledge");
        expect(!scrap_refresh_capacity_can_fit(0, 8, std::nullopt, 1), "unknown inventory is not assumed empty");
        expect(!scrap_refresh_capacity_can_fit(0, 65, 0, 1), "unknown capacity bounds fail closed");
        expect(scrap_refresh_slot_from_title(625, 178, 85, 26) == 0, "verified wheel title maps to first buying slot");
        expect(scrap_refresh_slot_from_title(831, 179, 85, 25) == 1, "verified limb title maps to second buying slot");
        expect(
            scrap_refresh_slot_from_title(1038, 180, 123, 22) == 2,
            "verified mother title maps to third buying slot");
        expect(scrap_refresh_slot_from_title(832, 389, 63, 27) == 4, "verified sold fish maps to lower middle slot");
        expect(
            !scrap_refresh_slot_from_title(420, 179, 85, 25),
            "selling-only first column is not a buying shelf slot");
        expect(
            !scrap_refresh_slot_from_title(831, 250, 85, 25),
            "unsettled vertical title cannot prove a complete shelf");
        expect(
            !scrap_refresh_slot_from_title(1038, 180, std::numeric_limits<int>::max(), 22),
            "overflowing OCR rectangles never enter slot arithmetic");
    }
    std::cout << "SCRAP_REFRESH_NATIVE checks=" << checks << " failures=" << failures
              << " connections=0 input_calls=0 live_refresh_proven=false\n";
    return failures ? 1 : 0;
}
