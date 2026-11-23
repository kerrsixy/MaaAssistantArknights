#include <iostream>

#include "Task/Roguelike/BlackFlow/BlackFlowSummonRetreat.h"

int main()
{
    using namespace asst;
    using namespace asst::blackflow;
    int checks = 0, failures = 0;
    const auto expect = [&](bool result, const char* label) {
        ++checks;
        if (!result) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const auto make = [] {
        return BlackFlowSummonRetreat(
            "run:devotion",
            "魂灵之影",
            "token_10035_wisdel_wward",
            { 7, 4 },
            { { 7, 5 }, { 6, 4 }, { 6, 5 }, { 4, 3 } });
    };
    SummonRetreatObservation frame;
    frame.battle_key = "run:devotion";
    frame.normal_view = true;
    frame.owner_present = true;
    frame.boss = BossVitalState::ConfirmedAlive;
    frame.candidates = { { 7, 4 }, { 10, 4 }, { 7, 5 } };
    const auto advance = [&] {
        ++frame.sequence;
        frame.observed_ms += 100;
        frame.received_ms = frame.observed_ms;
    };
    const auto observe = [&](BlackFlowSummonRetreat& policy) {
        advance();
        return policy.observe(frame);
    };
    auto policy = make();
    auto result = observe(policy);
    expect(
        result.action == SummonRetreatAction::Select && result.tile == Point(7, 5),
        "body and out-of-range tiles are skipped");
    expect(policy.authorized(result.action), "select request requires an actor acknowledgment");
    expect(observe(policy).action == SummonRetreatAction::None, "unacknowledged selection is not repeated");
    policy.input_result(SummonRetreatAction::Select, true);
    frame.normal_view = false;
    frame.selection_view = true;
    frame.selected_name = "维什戴尔";
    frame.selected_id = "char_1035_wisdel";
    frame.retreat_button_present = true;
    expect(
        observe(policy).action == SummonRetreatAction::CancelSelection,
        "selecting EW cancels instead of retreating her");
    policy.input_result(SummonRetreatAction::CancelSelection, true);

    const auto selected_soul = [&] {
        frame.normal_view = true;
        frame.selection_view = false;
        frame.boss = BossVitalState::ConfirmedAlive;
        frame.owner_present = true;
        frame.user_paused = false;
        auto value = make();
        expect(observe(value).action == SummonRetreatAction::Select, "fresh normal frame proposes a token selection");
        value.input_result(SummonRetreatAction::Select, true);
        frame.normal_view = false;
        frame.selection_view = true;
        frame.selected_name = "魂灵之影";
        frame.selected_id = "token_10035_wisdel_wward";
        frame.retreat_button_present = true;
        return value;
    };
    policy = selected_soul();
    frame.selected_id = "token_10069_mcnist_mcgraf";
    expect(observe(policy).action == SummonRetreatAction::CancelSelection, "a conflicting identity cancels selection");
    policy = selected_soul();
    frame.selected_id = "token_10035_wisdel_wward";
    frame.selected_name.clear();
    expect(observe(policy).action == SummonRetreatAction::None, "unreadable name does not authorize retreat");
    frame.selected_name = "魂灵之影";
    frame.retreat_button_present = false;
    expect(observe(policy).action == SummonRetreatAction::None, "name alone cannot replace retreat UI evidence");
    frame.retreat_button_present = true;
    expect(
        observe(policy).action == SummonRetreatAction::RetreatSelection,
        "fresh identity and live Boss authorize current selection only");
    expect(observe(policy).action == SummonRetreatAction::None, "retreat is not reissued before an actor receipt");
    frame.boss = BossVitalState::ConfirmedDead;
    expect(observe(policy).action == SummonRetreatAction::CancelSelection, "Boss death cancels an uncommitted retreat");
    expect(
        !policy.authorized(SummonRetreatAction::RetreatSelection),
        "a cached retreat decision loses authorization immediately");
    policy.input_result(SummonRetreatAction::CancelSelection, true);
    frame.normal_view = true;
    frame.selection_view = false;
    frame.boss = BossVitalState::ConfirmedAlive;
    expect(
        observe(policy).action == SummonRetreatAction::None && policy.boss_dead(),
        "later sprite matches cannot revive a confirmed dead Boss");

    policy = selected_soul();
    frame.boss = BossVitalState::Unknown;
    expect(
        observe(policy).action == SummonRetreatAction::RetreatSelection,
        "selected occlusion may use the accepted initial normal evidence "
        "within this transaction");
    expect(!policy.boss_dead(), "absence is not death");
    policy = selected_soul();
    frame.user_paused = true;
    expect(
        observe(policy).action == SummonRetreatAction::None,
        "manual pause produces no cancel, resume or retreat input");
    expect(!policy.authorized(SummonRetreatAction::RetreatSelection), "manual pause invalidates cached authorization");

    policy = selected_soul();
    expect(
        observe(policy).action == SummonRetreatAction::RetreatSelection,
        "fresh selected soul creates one authorization");
    expect(policy.authorized(SummonRetreatAction::RetreatSelection), "current decision is authorized");
    frame.received_ms = frame.observed_ms + 501;
    expect(policy.observe(frame).action == SummonRetreatAction::None, "stale frames cannot issue input");
    expect(!policy.authorized(SummonRetreatAction::RetreatSelection), "stale frame invalidates the prior decision");
    policy = selected_soul();
    frame.normal_view = true;
    frame.selection_view = false;
    expect(observe(policy).action == SummonRetreatAction::None, "lost selection cannot authorize retreat");
    frame.selection_view = true;
    frame.normal_view = false;
    frame.observed_ms += 1501;
    frame.received_ms = frame.observed_ms;
    ++frame.sequence;
    expect(
        policy.observe(frame).action == SummonRetreatAction::CancelSelection,
        "expired verification cancels its selection");

    policy = selected_soul();
    expect(observe(policy).action == SummonRetreatAction::RetreatSelection, "verified soul can request retreat");
    policy.input_result(SummonRetreatAction::RetreatSelection, true);
    frame.normal_view = true;
    frame.selection_view = false;
    frame.candidates.clear();
    expect(!observe(policy).removal_confirmed, "missing token detection alone does not prove removal");
    frame.confirmed_empty_tiles = { { 7, 5 } };
    result = observe(policy);
    expect(
        result.removal_confirmed && result.tile == Point(7, 5),
        "explicit empty tile and visible owner confirm this token removal");
    frame.candidates = { { 6, 4 }, { 6, 5 } };
    result = observe(policy);
    expect(
        result.action == SummonRetreatAction::Select && result.tile == Point(6, 4),
        "S3 new tokens are selected from fresh observations");
    policy.input_result(SummonRetreatAction::Select, false);
    frame.candidates = { { 4, 3 } };
    result = observe(policy);
    expect(
        result.action == SummonRetreatAction::Select && result.tile == Point(4, 3),
        "full attack range is supported beyond adjacent tiles");

    policy = make();
    frame.battle_key = "another:devotion";
    expect(observe(policy).action == SummonRetreatAction::None, "cross-battle observations never authorize input");
    frame.battle_key = "run:devotion";
    frame.owner_present = false;
    expect(observe(policy).action == SummonRetreatAction::None, "missing owner prevents new selection");
    frame.owner_present = true;
    advance();
    policy.observe(frame);
    policy.input_result(SummonRetreatAction::Select, true);
    frame.normal_view = false;
    frame.selection_view = true;
    expect(
        policy.observe(frame).action == SummonRetreatAction::None,
        "the preselection frame cannot authorize retreat");

    const auto reclaim = [](std::vector<Point> allowed = { { 7, 5 } }) {
        return BlackFlowSummonRetreat(
            "run:devotion",
            "魂灵之影",
            "token_10035_wisdel_wward",
            { 7, 4 },
            std::move(allowed),
            SummonRetreatIntent::ReclaimDeploymentTile);
    };
    frame = {};
    frame.battle_key = "run:devotion";
    frame.owner_present = true;
    frame.normal_view = true;
    frame.candidates = { { 6, 4 }, { 7, 5 } };
    policy = reclaim();
    result = observe(policy);
    expect(
        result.action == SummonRetreatAction::Select && result.tile == Point(7, 5),
        "reclamation selects only the requested tile without invented Boss "
        "evidence");
    expect(
        policy.intent() == SummonRetreatIntent::ReclaimDeploymentTile &&
            policy.allowed_tiles() == std::vector<Point> { { 7, 5 } },
        "intent and target are bound to the transaction");
    policy.input_result(SummonRetreatAction::Select, true);
    frame.normal_view = false;
    frame.selection_view = true;
    frame.selected_name = "魂灵之影";
    frame.selected_id = "token_10035_wisdel_wward";
    frame.retreat_button_present = true;
    expect(
        observe(policy).action == SummonRetreatAction::RetreatSelection,
        "same verified Soul identity and retreat glyph are mandatory outside "
        "Boss fights");
    policy.input_result(SummonRetreatAction::RetreatSelection, true);
    expect(policy.awaiting_normal_ack(), "retreat receipt keeps the UI transaction pending");
    frame.normal_view = true;
    frame.selection_view = false;
    frame.candidates.clear();
    expect(!observe(policy).removal_confirmed, "a panel closing is not a certified empty tile");
    policy = reclaim({ { 7, 5 }, { 6, 4 } });
    frame.candidates = { { 7, 5 }, { 6, 4 } };
    expect(observe(policy).action == SummonRetreatAction::None, "reclamation cannot broaden to multiple tiles");
    policy = reclaim({ { 7, 4 } });
    expect(observe(policy).action == SummonRetreatAction::None, "a requested owner tile is never reclaimable");
    policy = reclaim({});
    expect(observe(policy).action == SummonRetreatAction::None, "empty target scope authorizes nothing");
    policy = reclaim();
    frame.candidates = { { 7, 5 } };
    expect(observe(policy).action == SummonRetreatAction::Select, "a single target starts a fresh reclaim");
    policy.input_result(SummonRetreatAction::Select, true);
    frame.normal_view = false;
    frame.selection_view = true;
    frame.selected_name = "维什戴尔";
    frame.selected_id = "char_1035_wisdel";
    expect(observe(policy).action == SummonRetreatAction::CancelSelection, "reclamation never retreats the owner");
    policy.input_result(SummonRetreatAction::CancelSelection, true);
    expect(policy.awaiting_normal_ack(), "cancel receipt does not release UI ownership");
    frame.normal_view = false;
    frame.selection_view = true;
    expect(observe(policy).action == SummonRetreatAction::None, "unclosed detail panel cannot restart selection");
    frame.normal_view = true;
    frame.selection_view = false;
    expect(
        observe(policy).action == SummonRetreatAction::None && policy.awaiting_normal_ack(),
        "a normal observation alone cannot release the UI lease");
    expect(!policy.complete_handoff(), "bar handoff cannot complete before two normal acknowledgments");
    advance();
    expect(!policy.acknowledge_normal(frame), "first normal acknowledgment still retains the lease");
    advance();
    expect(
        policy.acknowledge_normal(frame) && policy.handoff_pending(),
        "second distinct normal frame starts a pending ordinary handoff");
    expect(
        observe(policy).action == SummonRetreatAction::None && policy.handoff_pending(),
        "a failed card refresh may retain the handoff without another selection");
    expect(
        policy.complete_handoff() && !policy.handoff_pending(),
        "only the ordinary actor may finish a pending handoff after "
        "refreshing cards");
    policy = reclaim();
    frame.user_paused = true;
    expect(observe(policy).action == SummonRetreatAction::None, "user pause blocks reclamation too");
    frame.user_paused = false;
    expect(observe(policy).action == SummonRetreatAction::Select, "running reclaim may start after observed resume");
    policy.input_result(SummonRetreatAction::Select, true);
    frame.normal_view = false;
    frame.selection_view = true;
    frame.selected_name.clear();
    frame.selected_id.clear();
    advance();
    expect(
        policy.abandon(frame).action == SummonRetreatAction::CancelSelection,
        "explicit abandonment cancels only an acknowledged own selection on "
        "fresh evidence");
    policy.input_result(SummonRetreatAction::CancelSelection, true);
    frame.user_paused = true;
    advance();
    expect(policy.abandon(frame).action == SummonRetreatAction::None, "abandonment never dismisses a user pause");
    frame.user_paused = false;
    advance();
    frame.received_ms += 501;
    expect(policy.abandon(frame).action == SummonRetreatAction::None, "stale cleanup cannot authorize a click");
    frame.normal_view = true;
    frame.selection_view = false;
    frame.boss = BossVitalState::Unknown;
    policy = make();
    expect(observe(policy).action == SummonRetreatAction::None, "protection intent still requires a live Boss");

    std::cout << "SUMMON_RETREAT checks=" << checks << " failures=" << failures << " device_inputs=0\n";
    return failures ? 1 : 0;
}
