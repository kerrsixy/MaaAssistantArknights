#include "Task/Roguelike/BlackFlow/BlackFlowSummonRetreat.h"
#include <filesystem>
#include <iostream>
#include <tuple>

using namespace asst;
using namespace asst::blackflow;

SummonRetreatObservation read_observation(const json::value& value)
{
    SummonRetreatObservation o;
    o.battle_key = value.at("battle_key").as_string();
    o.sequence = value.at("sequence").as_integer();
    o.observed_ms = value.at("observed_ms").as_integer();
    o.received_ms = value.at("received_ms").as_integer();
    o.boss = static_cast<BossVitalState>(value.at("boss_state").as_integer());
    o.owner_present = value.at("owner_present").as_boolean();
    o.normal_view = value.at("normal_view").as_boolean();
    o.selection_view = value.at("selection_view").as_boolean();
    o.user_paused = value.at("user_paused").as_boolean();
    o.selected_name = value.at("selected_name").as_string();
    o.selected_id = value.at("selected_id").as_string();
    o.retreat_button_present = value.at("retreat_button_present").as_boolean();
    for (const auto& tile : value.at("candidates").as_array()) {
        o.candidates.emplace_back(tile.at(0).as_integer(), tile.at(1).as_integer());
    }
    return o;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) {
        return 2;
    }
    const auto path = std::filesystem::path(argv[1]);
    const auto first = json::open(path / "151532272-1-select_input_receipt.json");
    const auto second = json::open(path / "151533803-2-cancel_input_receipt.json");
    if (!first || !second) {
        return 3;
    }
    for (const auto* value : { &*first, &*second }) {
        for (const auto* key : { "battle_key",
                                 "sequence",
                                 "observed_ms",
                                 "received_ms",
                                 "boss_state",
                                 "owner_present",
                                 "normal_view",
                                 "selection_view",
                                 "user_paused",
                                 "selected_name",
                                 "selected_id",
                                 "retreat_button_present",
                                 "candidates" }) {
            if (!value->contains(key)) {
                std::cerr << "Missing recorded observation field: " << key << '\n';
                return 3;
            }
        }
    }
    if (!first->contains("input_accepted") || !first->at("input_accepted").is_boolean()) {
        std::cerr << "Missing or invalid recorded input receipt\n";
        return 3;
    }
    const auto normal = read_observation(*first);
    const auto selected = read_observation(*second);
    int checks = 0, failures = 0;
    const auto expect = [&](bool pass, const char* label) {
        ++checks;
        if (!pass) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const auto make = [&] {
        return BlackFlowSummonRetreat(
            normal.battle_key,
            "魂灵之影",
            "token_10035_wisdel_wward",
            { 7, 4 },
            { { 7, 5 } });
    };
    const auto armed = [&] {
        auto policy = make();
        policy.observe(normal);
        policy.input_result(SummonRetreatAction::Select, true);
        return policy;
    };
    const auto retreat = [&](BlackFlowSummonRetreat& policy, const SummonRetreatObservation& frame, std::uint64_t now) {
        policy.observe(frame);
        return policy.authorize_input(SummonRetreatAction::RetreatSelection, frame, now, Point(7, 4), Point(7, 5));
    };
    expect(
        normal.observed_ms == 151'531'701 && normal.received_ms == 151'532'081,
        "real normal receipt clocks unchanged");
    expect(
        selected.observed_ms == 151'532'786 && selected.received_ms == 151'533'078,
        "real selected receipt clocks unchanged");
    expect(
        normal.boss == BossVitalState::ConfirmedAlive && selected.boss == BossVitalState::Unknown,
        "real Boss states unchanged");
    expect(selected.received_ms - normal.observed_ms == 1377, "real transaction processing age is 1377ms");
    auto policy = make();
    auto decision = policy.observe(normal);
    auto authority = policy.authorize_input(decision.action, normal, normal.received_ms, Point(7, 4), decision.tile);
    expect(
        authority.allowed && authority.basis == SummonRetreatEvidenceBasis::FreshNormalAlive,
        "fresh real normal authorizes selection at processing completion");
    policy.input_result(decision.action, first->at("input_accepted").as_boolean());
    decision = policy.observe(selected);
    expect(
        decision.action == SummonRetreatAction::RetreatSelection,
        "real selected Unknown now proposes one exact-identity retreat");
    authority = policy.authorize_input(decision.action, selected, selected.received_ms, Point(7, 4), decision.tile);
    expect(
        authority.allowed && authority.basis == SummonRetreatEvidenceBasis::InitialNormalAliveLease,
        "lease basis is explicit rather than selected Alive");
    expect(
        authority.initial_alive_sequence == 1 && authority.initial_alive_observed_ms == normal.observed_ms &&
            authority.initial_alive_received_ms == normal.received_ms,
        "lease source retains original normal clocks");
    expect(
        authority.transaction_valid_before_ms == 151'533'201 &&
            authority.transaction_valid_before_ms - authority.checked_ms == 123,
        "only 123ms remains at real processing completion");
    expect(selected.boss == BossVitalState::Unknown, "authorization never rewrites Boss Unknown");
    expect(
        !policy.authorize_input(decision.action, selected, selected.received_ms, Point(7, 4), decision.tile).allowed,
        "input permission is consumed once");
    policy = armed();
    authority = retreat(policy, selected, normal.observed_ms + 1499);
    expect(authority.allowed, "input at transaction deadline minus one is allowed");
    policy = armed();
    authority = retreat(policy, selected, normal.observed_ms + 1500);
    expect(!authority.allowed, "input at exact deadline is denied despite fresh selected frame");
    policy = armed();
    expect(
        !retreat(policy, selected, selected.received_ms - 1).allowed,
        "input clock before processing completion is denied");
    policy = armed();
    expect(!retreat(policy, selected, selected.observed_ms + 501).allowed, "selected input aged 501ms is denied");
    for (int variant = 0; variant < 10; ++variant) {
        auto frame = selected;
        if (variant == 0) {
            frame.battle_key += ":other";
        }
        if (variant == 1) {
            frame.owner_present = false;
        }
        if (variant == 2) {
            frame.user_paused = true;
        }
        if (variant == 3) {
            frame.boss = BossVitalState::ConfirmedDead;
        }
        if (variant == 4) {
            frame.selected_name = "维什戴尔";
        }
        if (variant == 5) {
            frame.selected_id = "char_1035_wisdel";
        }
        if (variant == 6) {
            frame.retreat_button_present = false;
        }
        if (variant == 7) {
            frame.selected_name.clear();
        }
        if (variant == 8) {
            frame.normal_view = true;
        }
        if (variant == 9) {
            frame.received_ms = frame.observed_ms + 501;
        }
        policy = armed();
        expect(
            !retreat(policy, frame, frame.received_ms).allowed,
            "invalid selected evidence cannot use initial lease");
    }
    policy = armed();
    policy.observe(selected);
    expect(
        !policy
             .authorize_input(
                 SummonRetreatAction::RetreatSelection,
                 selected,
                 selected.received_ms,
                 Point(6, 4),
                 Point(7, 5))
             .allowed,
        "moved owner invalidates input context");
    policy = armed();
    policy.observe(selected);
    expect(
        !policy
             .authorize_input(
                 SummonRetreatAction::RetreatSelection,
                 selected,
                 selected.received_ms,
                 Point(7, 4),
                 Point(6, 5))
             .allowed,
        "different target invalidates input context");
    policy = make();
    policy.observe(normal);
    expect(!retreat(policy, selected, selected.received_ms).allowed, "no successful Select receipt means no lease");
    policy = make();
    policy.observe(normal);
    policy.input_result(SummonRetreatAction::Select, false);
    expect(!retreat(policy, selected, selected.received_ms).allowed, "failed Select receipt means no lease");
    policy = armed();
    auto normal_unknown = selected;
    normal_unknown.normal_view = true;
    normal_unknown.selection_view = false;
    policy.observe(normal_unknown);
    auto later = selected;
    ++later.sequence;
    ++later.observed_ms;
    ++later.received_ms;
    expect(
        !retreat(policy, later, later.received_ms).allowed,
        "normal Unknown revokes initial evidence rather than lending it onward");
    policy = armed();
    auto paused = selected;
    paused.user_paused = true;
    policy.observe(paused);
    expect(!retreat(policy, later, later.received_ms).allowed, "pause permanently revokes this initial credential");
    policy = armed();
    auto dead = selected;
    dead.boss = BossVitalState::ConfirmedDead;
    policy.observe(dead);
    expect(
        !retreat(policy, later, later.received_ms).allowed && policy.boss_dead(),
        "confirmed death remains a hard sticky veto");
    policy = armed();
    auto wrong = selected;
    wrong.selected_id = "char_1035_wisdel";
    auto cancel = policy.observe(wrong);
    policy.input_result(cancel.action, false);
    expect(!retreat(policy, later, later.received_ms).allowed, "failed cleanup cannot revive cancelled credential");
    policy = armed();
    retreat(policy, selected, selected.received_ms);
    policy.input_result(SummonRetreatAction::RetreatSelection, false);
    expect(
        !retreat(policy, later, later.received_ms).allowed,
        "failed retreat cannot reuse initial evidence for another attempt");
    policy = armed();
    auto old = selected;
    old.observed_ms = normal.observed_ms + 1500;
    old.received_ms = old.observed_ms;
    decision = policy.observe(old);
    authority = policy.authorize_input(decision.action, old, old.received_ms, Point(7, 4), decision.tile);
    expect(
        decision.action == SummonRetreatAction::CancelSelection && authority.allowed,
        "transaction expiry still permits fresh owned-panel cleanup");
    policy = armed();
    auto live = selected;
    live.boss = BossVitalState::ConfirmedAlive;
    authority = retreat(policy, live, live.received_ms);
    expect(
        authority.allowed && authority.basis == SummonRetreatEvidenceBasis::FreshSelectedAlive,
        "genuine selected Alive keeps its separate evidence basis");
    policy = make();
    auto stale = normal;
    stale.received_ms = stale.observed_ms + 501;
    expect(
        policy.observe(stale).action == SummonRetreatAction::None,
        "initial 501ms observation cannot start transaction");
    policy = make();
    stale.received_ms = stale.observed_ms + 500;
    decision = policy.observe(stale);
    expect(
        policy.authorize_input(decision.action, stale, stale.received_ms, Point(7, 4), decision.tile).allowed,
        "initial 500ms boundary is retained");
    policy = armed();
    policy.abandon(paused);
    expect(!retreat(policy, later, later.received_ms).allowed, "abandonment during pause cannot preserve a credential");
    policy = armed();
    policy.observe(selected);
    policy.input_result(SummonRetreatAction::RetreatSelection, true);
    expect(
        !retreat(policy, later, later.received_ms).allowed,
        "successful retreat cannot reissue in the same transaction");
    policy = armed();
    policy.observe(selected);
    expect(
        !policy
             .authorize_input(
                 SummonRetreatAction::RetreatSelection,
                 normal,
                 selected.received_ms,
                 Point(7, 4),
                 Point(7, 5))
             .allowed,
        "preselection frame cannot authorize a selected input");
    policy = armed();
    policy.observe(normal_unknown);
    expect(
        !policy
             .authorize_input(
                 SummonRetreatAction::RetreatSelection,
                 normal_unknown,
                 normal_unknown.received_ms,
                 Point(7, 4),
                 Point(7, 5))
             .allowed,
        "normal-view Unknown is not covered by the selected-view exception");
    policy = armed();
    auto ack = selected;
    ack.normal_view = true;
    ack.selection_view = false;
    policy.acknowledge_normal(ack);
    expect(
        !retreat(policy, later, later.received_ms).allowed,
        "a normal acknowledgment permanently consumes the selected credential");
    policy = make();
    decision = policy.observe(normal);
    expect(
        !policy.authorize_input(decision.action, normal, normal.observed_ms + 501, Point(7, 4), decision.tile).allowed,
        "normal input also checks actual clock after an initially fresh analysis");
    policy = armed();
    auto boundary = selected;
    boundary.observed_ms = normal.observed_ms + 900;
    boundary.received_ms = boundary.observed_ms + 100;
    expect(
        retreat(policy, boundary, boundary.observed_ms + 500).allowed,
        "synthetic selected 500ms boundary remains valid before transaction expiry");
    policy = armed();
    expect(
        !retreat(policy, boundary, boundary.observed_ms + 501).allowed,
        "synthetic selected 501ms boundary is rejected independently of transaction expiry");
    policy = armed();
    wrong = selected;
    wrong.selected_id = "char_1035_wisdel";
    decision = policy.observe(wrong);
    expect(
        !policy.authorize_input(decision.action, wrong, wrong.observed_ms + 501, Point(7, 4), decision.tile).allowed,
        "aged cancel input is not sent");
    ++wrong.sequence;
    wrong.observed_ms += 600;
    wrong.received_ms = wrong.observed_ms;
    decision = policy.observe(wrong);
    expect(
        decision.action == SummonRetreatAction::CancelSelection &&
            policy.authorize_input(decision.action, wrong, wrong.received_ms, Point(7, 4), decision.tile).allowed,
        "fresh cleanup can recover after an input-time cancel refusal without stranding UI ownership");
    std::cout << "LEASE_RECEIPT_REPLAY checks=" << checks << " failures=" << failures
              << " device_inputs=0 actual_engine_BossAlive_at_retreat=unproven\n";
    return failures ? 1 : 0;
}
