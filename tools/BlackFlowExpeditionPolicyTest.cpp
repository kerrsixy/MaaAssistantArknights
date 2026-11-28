#include <iostream>
#include <string>
#include <vector>

#include "Task/Roguelike/BlackFlow/BlackFlowExpeditionPolicy.h"

using namespace asst::blackflow;

int main()
{
    int checks = 0;
    int failures = 0;
    const auto expect = [&](bool condition, const std::string& label) {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const auto candidate = [](const std::string& id, int phase, int priority, bool output, bool blocking) {
        ExpeditionCandidate value;
        value.id = id;
        value.roster = { true, true, true, phase, false };
        value.account_phase = 2;
        value.account_phase_verified = true;
        value.duties = { true, output, false, blocking };
        value.promote_priority = priority;
        return value;
    };
    ExpeditionContext context;
    context.floor = 3;
    context.promotion_option_verified = true;
    context.return_next_floor_verified = true;
    // Mirrors the six-current-member situation by duties, without name rules.
    auto core = candidate("current_e2_core", 2, 999, true, false);
    auto blocker = candidate("persistent_blocker", 1, 900, true, true);
    auto target = candidate("second_sniper", 1, 850, true, false);
    target.e2_skill_unlock_priority = 2;
    const auto spare1 = candidate("caster_a", 1, 200, false, false);
    const auto spare2 = candidate("caster_b", 1, 100, false, false);
    const auto spare3 = candidate("summoner", 1, 300, false, false);
    std::vector<ExpeditionCandidate> roster { core, blocker, target, spare1, spare2, spare3 };
    const auto baseline = choose_expedition_candidate(context, roster);
    expect(baseline.action == ExpeditionAction::TrainForPromotion, "floor3 dispatch instead of fixed rest");
    expect(baseline.candidate_id == target.id, "promote secondary output while retaining core and blocker");
    expect(baseline.expected_return_phase == 2, "proposal predicts only verified next-floor E2 promotion");
    expect(!baseline.requires_battle_avoidance, "retained duties permit normal-route proposal");
    expect(baseline.remaining.persistent_blocking == 1 && baseline.remaining.output == 2, "remaining duties counted");
    expect(baseline.remaining_available_ids.size() == 5, "remaining roster excludes only selected current member");

    for (int floor : { 5, 6 }) {
        auto c = context;
        c.floor = floor;
        expect(
            choose_expedition_candidate(c, roster).action == ExpeditionAction::RestTwoHope,
            "no return after final floor");
    }
    for (int floor : { 1, 2, 4 }) {
        auto c = context;
        c.floor = floor;
        expect(choose_expedition_candidate(c, roster).candidate_id == target.id, "not hardcoded to floor3");
    }
    auto c = context;
    c.floor.reset();
    expect(
        choose_expedition_candidate(c, roster).action == ExpeditionAction::RestTwoHope,
        "unknown floor preserves rest");
    c = context;
    c.promotion_option_verified = false;
    expect(
        choose_expedition_candidate(c, roster).action == ExpeditionAction::RestTwoHope,
        "beacon or unknown option is not promotion");
    c = context;
    c.return_next_floor_verified = false;
    expect(
        choose_expedition_candidate(c, roster).action == ExpeditionAction::RestTwoHope,
        "unverified return cannot authorize departure");

    auto all_e2 = roster;
    for (auto& value : all_e2) {
        value.roster.phase = 2;
    }
    expect(
        choose_expedition_candidate(context, all_e2).action == ExpeditionAction::RestTwoHope,
        "all current E2 has no invented gain");
    auto locked = target;
    locked.account_phase = 1;
    expect(
        choose_expedition_candidate(context, { core, blocker, locked }).action == ExpeditionAction::RestTwoHope,
        "account E1 does not unlock E2");
    locked = target;
    locked.account_phase_verified = false;
    expect(
        choose_expedition_candidate(context, { core, blocker, locked }).action == ExpeditionAction::RestTwoHope,
        "unverified account training does not unlock E2");
    locked = target;
    locked.roster.temporary = true;
    expect(
        choose_expedition_candidate(context, { core, blocker, locked }).action == ExpeditionAction::RestTwoHope,
        "temporary recruit cannot inherit account E2");
    locked.roster.temporary.reset();
    expect(
        choose_expedition_candidate(context, { core, blocker, locked }).action == ExpeditionAction::RestTwoHope,
        "unknown origin cannot inherit account E2");

    auto only_output = target;
    auto only_block = blocker;
    only_block.duties.output = false;
    only_block.promote_priority = 0;
    only_block.e2_skill_unlock_priority = 0;
    const std::vector<ExpeditionCandidate> scarce { only_output, only_block };
    expect(
        choose_expedition_candidate(context, scarce).action == ExpeditionAction::RestTwoHope,
        "sole output retained with unknown route");
    c = context;
    c.route = ExpeditionRoute::BattlesRequired;
    expect(
        choose_expedition_candidate(c, scarce).action == ExpeditionAction::RestTwoHope,
        "sole output retained before unavoidable battle");
    c.route = ExpeditionRoute::VerifiedAvoidBattlesUntilReturn;
    const auto avoided = choose_expedition_candidate(c, scarce);
    expect(
        avoided.candidate_id == target.id && avoided.requires_battle_avoidance,
        "explicit avoid-until-return can permit temporary missing output");
    expect(
        avoided.missing.output == 1 && avoided.missing.persistent_blocking == 0,
        "avoidance advice names missing duty");

    auto healer = candidate("only_healer", 1, 2000, false, false);
    healer.duties.healing = true;
    c = context;
    c.minimum_remaining.healing = 1;
    expect(
        choose_expedition_candidate(c, { core, blocker, healer }).action == ExpeditionAction::RestTwoHope,
        "required unique healer retained");
    healer.duties.fresh = false;
    expect(
        choose_expedition_candidate(c, { core, blocker, target, healer }).action == ExpeditionAction::RestTwoHope,
        "stale healing role is not proof");

    auto cache_only = candidate("account_only", 1, 9000, true, true);
    cache_only.roster.fresh = false;
    expect(
        choose_expedition_candidate(context, { target, cache_only }).action == ExpeditionAction::RestTwoHope,
        "account-owned member cannot supply missing blocker");
    auto unknown = candidate("unrelated_unknown", 1, 9000, false, false);
    unknown.roster = {};
    unknown.duties = {};
    expect(
        choose_expedition_candidate(context, { core, blocker, target, unknown }).candidate_id == target.id,
        "unrelated unknown does not force all candidates to rest");

    auto observed = target;
    observed.roster.phase.reset();
    observed.roster.available.reset();
    observed.selection = { true, true, true, 1, false };
    expect(
        choose_expedition_candidate(context, { core, blocker, observed }).candidate_id == target.id,
        "fresh selection page fills current unknowns");
    observed = target;
    observed.selection = { true, std::nullopt, false, std::nullopt, std::nullopt };
    expect(
        choose_expedition_candidate(context, { core, blocker, observed }).action == ExpeditionAction::RestTwoHope,
        "fresh unavailable overrides roster availability");
    observed.selection.fresh = false;
    expect(
        choose_expedition_candidate(context, { core, blocker, observed }).candidate_id == target.id,
        "stale selection cannot override current availability");
    observed = target;
    observed.roster.phase = 2;
    observed.selection = { true, std::nullopt, std::nullopt, 1, std::nullopt };
    expect(
        choose_expedition_candidate(context, { core, blocker, observed }).candidate_id == target.id,
        "fresh run page overrides earlier phase while retaining known ordinary origin");

    auto low_gain = target;
    low_gain.promote_priority = 0;
    low_gain.e2_skill_unlock_priority = 0;
    expect(
        choose_expedition_candidate(context, { core, blocker, low_gain }).action == ExpeditionAction::RestTwoHope,
        "no high-value safe target leaves two hope");
    low_gain.promote_priority.reset();
    low_gain.e2_skill_unlock_priority = 1;
    expect(
        choose_expedition_candidate(context, { core, blocker, low_gain }).candidate_id == target.id,
        "known E2 skill benefit works without guessed promote priority");
    auto alternate = target;
    alternate.id = "alternative_output";
    alternate.promote_priority = 1;
    alternate.e2_skill_unlock_priority = 3;
    expect(
        choose_expedition_candidate(context, { core, blocker, target, alternate }).candidate_id == alternate.id,
        "known skill-unlock ordering is generic");
    alternate.e2_skill_unlock_priority = target.e2_skill_unlock_priority;
    alternate.promote_priority = target.promote_priority;
    expect(
        choose_expedition_candidate(context, { core, blocker, target, alternate }).candidate_id == target.id,
        "equal benefit keeps observed candidate order");
    expect(
        choose_expedition_candidate(context, { core, blocker, target, target }).action == ExpeditionAction::RestTwoHope,
        "duplicate current identity cannot double count duties");
    unknown.id.clear();
    expect(
        choose_expedition_candidate(context, { core, blocker, target, unknown }).candidate_id == target.id,
        "unresolved unrelated identity does not suppress proven remaining duties");
    unknown.id = "unrelated_unknown";
    expect(
        choose_expedition_candidate(context, { core, blocker, target, unknown, unknown }).candidate_id == target.id,
        "duplicate irrelevant unknown is not counted and does not veto qualified target");
    auto only_blocker = blocker;
    only_blocker.e2_skill_unlock_priority = 10;
    auto retained_core = core;
    retained_core.duties.output = true;
    expect(
        choose_expedition_candidate(context, { retained_core, only_blocker }).action == ExpeditionAction::RestTwoHope,
        "sole persistent blocker cannot leave despite a retained E2 output");
    auto no_recruit = target;
    no_recruit.selection = { true, false, true, 1, false };
    expect(
        choose_expedition_candidate(context, { core, blocker, no_recruit }).action == ExpeditionAction::RestTwoHope,
        "fresh page explicit not-recruited cannot become an expedition target");
    auto page_only = target;
    page_only.roster = {};
    page_only.selection = { true, std::nullopt, true, 1, false };
    expect(
        choose_expedition_candidate(context, { core, blocker, page_only }).action == ExpeditionAction::RestTwoHope,
        "visible selectable portrait alone does not establish current recruitment");
    auto e0 = target;
    e0.roster.phase = 0;
    expect(
        choose_expedition_candidate(context, { core, blocker, e0 }).action == ExpeditionAction::RestTwoHope,
        "E0 return phase is not invented under the E1-to-E2 contract");
    auto unknown_target_duties = target;
    unknown_target_duties.duties = {};
    expect(
        choose_expedition_candidate(context, { core, blocker, unknown_target_duties }).candidate_id == target.id,
        "unknown departing duties do not matter when retained duties independently satisfy requirements");
    c = context;
    c.minimum_promote_priority = 900;
    auto ordinary_gain = target;
    ordinary_gain.e2_skill_unlock_priority = 0;
    expect(
        choose_expedition_candidate(c, { core, blocker, ordinary_gain }).action == ExpeditionAction::RestTwoHope,
        "configured minimum promotion priority gates low benefit");
    auto safe_spare = spare1;
    c = context;
    c.route = ExpeditionRoute::VerifiedAvoidBattlesUntilReturn;
    expect(
        choose_expedition_candidate(c, { target, only_block, safe_spare }).candidate_id == safe_spare.id,
        "retaining required duties outranks higher gain that requires route avoidance");
    c = context;
    c.minimum_remaining.output = -1;
    expect(
        choose_expedition_candidate(c, roster).action == ExpeditionAction::RestTwoHope,
        "invalid requirements rejected");
    expect(
        choose_expedition_candidate(context, {}).action == ExpeditionAction::RestTwoHope,
        "empty roster cannot use account inventory");

    std::cout << "EXPEDITION_POLICY checks=" << checks << " failures=" << failures
              << " connections=0 inputs=0 live_training=false victory_proven=false\n";
    return failures == 0 ? 0 : 1;
}
