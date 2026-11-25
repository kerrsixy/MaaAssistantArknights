#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>

#include "AsstCaller.h"
#include "Config/Roguelike/RoguelikeRecruitConfig.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Task/Roguelike/AbstractRoguelikeTaskPlugin.h"
#include "Task/Roguelike/BlackFlow/BlackFlowRecruitmentDuty.h"
#include "Task/Roguelike/RoguelikeRecruitPolicy.h"
#include "Utils/Platform.hpp"
#include "Vision/Roguelike/BlackFlow/BlackFlowVoucherStash.h"

// Test-only access to the real pending/confirmation transaction. No select,
// controller, _run, or device API is called by this executable.
#include "Task/Roguelike/RoguelikeRecruitTaskPlugin.h"

namespace asst
{
class RoguelikeRecruitmentTestAccess : public asst::RoguelikeRecruitTaskPlugin
{
public:
    explicit RoguelikeRecruitmentTestAccess(const std::shared_ptr<asst::RoguelikeConfig>& config) :
        RoguelikeRecruitTaskPlugin({}, nullptr, "Recruit127Offline", config, nullptr)
    {
    }

    using RoguelikeRecruitTaskPlugin::reset_in_run_variables;

    void pending(
        const asst::battle::roguelike::Recruitment& oper,
        const std::optional<asst::blackflow::RecruitmentDutyOffer>& origin,
        bool confirmed)
    {
        ++m_recruit_count;
        m_pending_recruitment = PendingRecruitment { oper,
                                                     confirmed,
                                                     false,
                                                     false,
                                                     origin,
                                                     m_config->account_client(),
                                                     m_config->account_uid(),
                                                     m_config->status().run_id };
    }

    bool commit() { return commit_pending_recruitment(); }

    void cancel() { discard_pending_recruitment("offline_cancel_fixture"); }

    const auto& ledger() const { return m_origin_ledger; }
};
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path resources(argv[1]), data(argv[2]), output(argv[3]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(resources.native()).c_str())) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto check = [&](bool value, const std::string& name) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << name << '\n';
        }
    };
    const auto Available = SkillAvailability::Available, Locked = SkillAvailability::Locked,
               Unknown = SkillAvailability::Unknown, Absent = SkillAvailability::Absent;
    check(
        resolve_recruitment_skill_plan(3, 2, { Available, Available, Locked }).selected == 2,
        "locked preferred3 uses verified alternate2 without changing configured preferred");
    check(
        resolve_recruitment_skill_plan(3, 2, { Available, Unknown, Locked }).selected == 0,
        "unknown alternate is not available");
    check(
        resolve_recruitment_skill_plan(3, 2, { Available, Available, Unknown }).selected == 2,
        "unknown preferred cannot bypass verified alternate");
    check(
        resolve_recruitment_skill_plan(3, 0, { Available, Available, Locked }).selected == 0,
        "no configured alternate does not invent another skill");
    check(
        resolve_recruitment_skill_plan(1, 2, { Available, Available, Absent }).selected == 1,
        "available preferred retains selection semantics");
    check(
        resolve_recruitment_skill_plan(9, -1, { Available, Available, Available }).selected == 0,
        "invalid configured slots remain unknown");

    BlackFlowRecruitmentDutyCatalog catalog;
    check(
        catalog.load(
            data / utils::path("resource/roguelike/BlackFlow/recruitment_duties.json"),
            data / utils::path("inputs/operator_training.json"),
            data / utils::path("inputs/battle_data.json")),
        "baseline Nov1 catalog");
    std::string error;
    const auto account = BlackFlowAccountProfile::load(
        data / utils::path("inputs/account_217_official.json"),
        "Official",
        "280875771",
        &error);
    check(account.has_value(), "frozen217 fixture account parses; not a claim about current live cache count");
    std::array<SkillAvailability, 3> ew_e1;
    for (int slot = 1; slot <= 3; ++slot) {
        ew_e1[slot - 1] = RoguelikeTraining.skill_availability("维什戴尔", slot, 1, 80);
    }
    const auto& ew_config = RoguelikeRecruit.get_oper_info("BlackFlow", "维什戴尔");
    const auto ew_plan = resolve_recruitment_skill_plan(ew_config.skill, ew_config.alternate_skill, ew_e1);
    check(
        ew_config.skill == 3 && ew_config.alternate_skill == 2 && ew_plan.selected == 2,
        "actual unchanged EW preferred3/alternate2 resolves to unlocked S2 at observed E1");
    check(
        account && account->skill_level_for_run("维什戴尔", ew_plan.selected, 1, 80) == 7,
        "outside account M3 binds to current offer E1 S2 rank7 without entering a run");
    check(
        !account || !account->skill_level_for_run("维什戴尔", 3, 1, 80),
        "outside account E2 does not unlock S3 on an E1 offer");
    const auto config = std::make_shared<RoguelikeConfig>();
    check(
        config->verify_and_load_params(json::object { { "theme", "BlackFlow" }, { "mode", 0 } }),
        "real recruitment config");
    config->bind_account("Official", "280875771");
    config->status().run_id = "offline127-real-lifecycle-fixture";
    RoguelikeRecruitmentTestAccess actor(config);
    const RecruitmentDutyContext context { .client = "Official",
                                           .uid = "280875771",
                                           .run_id = config->status().run_id,
                                           .observed_at = "offline-real-frame-replay-not-current-run",
                                           .hope = 30,
                                           .mode = 0 };
    const auto image = MAA_NS::imread(data / utils::path("fixtures/live112_first_page.png"));
    RoguelikeRecruitImageAnalyzer analyzer(image);
    check(analyzer.analyze(), "real live112 prior recruitment image is recognized");
    const std::unordered_map<std::string, RoguelikeOper> empty;
    const auto observed = observe_recruitment_duties(analyzer, catalog, account ? &*account : nullptr, empty, context);
    check(!observed.empty(), "actual analyzer feeds same-analysis duty offers");
    const auto ordinary = std::ranges::find_if(observed, [](const auto& x) {
        return x.operation == "招募" && x.ordinary_origin && x.price_index && x.elite >= 0 && x.level > 0;
    });
    check(ordinary != observed.end(), "real positive-priced ordinary offer exists");
    const auto promotion =
        std::ranges::find_if(observed, [](const auto& x) { return x.name == "机械师" && x.operation == "进阶"; });
    check(promotion != observed.end(), "real promotion glyph/card exists in frozen112 frame");
    json::array receipts;
    if (ordinary != observed.end()) {
        const auto card =
            std::ranges::find_if(analyzer.get_result(), [&](const auto& x) { return x.name == ordinary->name; });
        check(card != analyzer.get_result().end(), "ordinary raw card has usable native result");
        if (card != analyzer.get_result().end()) {
            const auto final = resolve_recruitment_final_offer(analyzer, observed, *card, std::nullopt);
            check(
                final.observed && final.intent_matches,
                "actual final refind resolver captures unchanged legacy choice");
            check(
                final.observed && final.observed->analysis_id == analyzer.get_analysis_id() &&
                    final.observed->price_index == ordinary->price_index,
                "final receipt preserves exact native analysis/card/price link");
            auto different_price = *ordinary;
            ++*different_price.hope_cost;
            check(
                !resolve_recruitment_final_offer(analyzer, observed, *card, different_price).intent_matches,
                "actual final refind rejects changed intended price before input");
            for (const auto& field : std::vector<std::string> { "rank", "plan", "origin", "phase" }) {
                auto conflict = *ordinary;
                if (field == "rank") {
                    ++conflict.effective_rank;
                }
                if (field == "plan") {
                    ++conflict.planned_skill;
                }
                if (field == "origin") {
                    conflict.training_binding = "different_origin";
                }
                if (field == "phase") {
                    ++conflict.elite;
                }
                check(
                    !resolve_recruitment_final_offer(analyzer, observed, *card, conflict).intent_matches,
                    "actual final refind rejects changed " + field);
            }
            auto wrong_selected = *card;
            ++wrong_selected.rect.x;
            check(
                !resolve_recruitment_final_offer(analyzer, observed, wrong_selected, std::nullopt).observed,
                "selected source rectangle must be the actual current card");
            auto stale = observed;
            for (auto& x : stale) {
                ++x.analysis_id;
            }
            check(
                !resolve_recruitment_final_offer(analyzer, stale, *card, std::nullopt).observed,
                "stale analysis epoch cannot become a final selection receipt");
            auto duplicate = observed;
            duplicate.push_back(*ordinary);
            check(
                !resolve_recruitment_final_offer(analyzer, duplicate, *card, std::nullopt).observed,
                "ambiguous same-name alternatives do not create an origin receipt");
            actor.pending(*card, final.observed, false);
            check(
                actor.commit() && actor.ledger().size() == 0 && config->status().opers.empty(),
                "actual commit leaves unconfirmed pending selection uncommitted");
            actor.cancel();
            check(
                actor.commit() && actor.ledger().size() == 0 && config->status().opers.empty(),
                "actual cancel discards pending without roster/origin writes");
            actor.pending(*card, final.observed, true);
            check(
                actor.commit() && config->status().opers.contains(card->name),
                "actual confirmed commit updates current roster");
            const auto proof = actor.ledger().find(
                context.client,
                context.uid,
                context.run_id,
                ordinary->id,
                config->status().opers.at(card->name));
            check(
                proof && proof->ordinary && proof->analysis_id == analyzer.get_analysis_id(),
                "actual confirmed legacy choice writes exact ordinary source proof");
            if (proof) {
                receipts.emplace_back(recruitment_origin_to_json(*proof));
            }
            const auto count = actor.ledger().size();
            check(
                actor.commit() && actor.ledger().size() == count,
                "duplicate receipt event does not invent another transaction");
            actor.pending(*card, std::nullopt, true);
            check(
                actor.commit() && actor.ledger().size() == 0,
                "unknown new confirmed same identity invalidates old origin rather than inheriting it");
        }
    }
    if (promotion != observed.end()) {
        actor.reset_in_run_variables();
        config->status().opers.clear();
        check(config->status().run_id == context.run_id, "plugin reset never invents or changes global exploration ID");
        // Controlled earlier-transaction precondition for the real promotion
        // frame. This is NOT a claim that its earlier recruit UI was observed.
        auto prior = *promotion;
        prior.elite = 1;
        prior.level = 80;
        prior.operation = "招募";
        prior.hope_cost = 4;
        prior.ordinary_origin = true;
        prior.observed_temporary.reset();
        prior.positive_temporary = false;
        prior.training_binding = "source_conditioned_non_temporary";
        prior.temporary_rule_source = catalog.temporary_rule_source();
        prior.observed_at = "controlled-prior-transaction-precondition-not-observed-UI";
        battle::roguelike::Recruitment old;
        old.name = prior.name;
        old.elite = prior.elite;
        old.level = prior.level;
        old.hope_cost = prior.hope_cost;
        actor.pending(old, prior, true);
        check(actor.commit(), "actual commit accepts controlled earlier ordinary transaction precondition");
        auto promoted = observe_recruitment_duties(
            analyzer,
            catalog,
            account ? &*account : nullptr,
            config->status().opers,
            context,
            &actor.ledger());
        const auto found = std::ranges::find_if(promoted, [&](const auto& x) { return x.name == prior.name; });
        check(
            found != promoted.end() && found->ordinary_origin &&
                found->training_binding == "confirmed_current_run_non_temporary" &&
                !found->inherited_origin_receipt.empty(),
            "actual promotion page binds a confirmed same-run ordinary old instance");
        check(
            found != promoted.end() && found->effective_rank == 10 && found->account_training_used,
            "promotion post-offer rank is actual same-UID account training, not old E1 cap or invented prep sequence");
        const auto no_origin = observe_recruitment_duties(
            analyzer,
            catalog,
            account ? &*account : nullptr,
            config->status().opers,
            context);
        const auto unknown = std::ranges::find_if(no_origin, [&](const auto& x) { return x.name == prior.name; });
        check(
            unknown != no_origin.end() && !unknown->ordinary_origin && unknown->effective_rank < 0,
            "restored roster without origin proof cannot inherit M3");
        auto wrong_run = context;
        wrong_run.run_id = "other-run";
        const auto crossed = observe_recruitment_duties(
            analyzer,
            catalog,
            account ? &*account : nullptr,
            config->status().opers,
            wrong_run,
            &actor.ledger());
        check(
            std::ranges::none_of(
                crossed,
                [&](const auto& x) { return x.name == prior.name && x.account_training_used; }),
            "same UID different exploration cannot reuse origin");
        auto mismatch = config->status().opers;
        --mismatch.at(prior.name).level;
        const auto changed = observe_recruitment_duties(
            analyzer,
            catalog,
            account ? &*account : nullptr,
            mismatch,
            context,
            &actor.ledger());
        check(
            std::ranges::none_of(
                changed,
                [&](const auto& x) { return x.name == prior.name && x.account_training_used; }),
            "changed old instance fingerprint is not silently equivalent");
        if (found != promoted.end()) {
            const auto card =
                std::ranges::find_if(analyzer.get_result(), [&](const auto& x) { return x.name == found->name; });
            if (card != analyzer.get_result().end()) {
                const auto final = resolve_recruitment_final_offer(analyzer, promoted, *card, *found);
                actor.pending(*card, final.observed, true);
                check(
                    final.intent_matches && actor.commit(),
                    "actual final refind and commit consume the promotion source chain");
                const auto source = actor.ledger().find(
                    context.client,
                    context.uid,
                    context.run_id,
                    found->id,
                    config->status().opers.at(found->name));
                check(
                    source && source->ordinary && !source->previous_receipt_id.empty() && source->elite == 2,
                    "confirmed promotion replaces old instance proof and preserves receipt ancestry");
                if (source) {
                    receipts.emplace_back(recruitment_origin_to_json(*source));
                }
            }
        }
        actor.reset_in_run_variables();
        config->status().opers.clear();
        prior.observed_temporary = true;
        prior.positive_temporary = true;
        prior.ordinary_origin = false;
        actor.pending(old, prior, true);
        check(actor.commit(), "actual confirmed temporary prior fixture");
        const auto temporary = observe_recruitment_duties(
            analyzer,
            catalog,
            account ? &*account : nullptr,
            config->status().opers,
            context,
            &actor.ledger());
        const auto temp = std::ranges::find_if(temporary, [&](const auto& x) { return x.name == prior.name; });
        check(
            temp != temporary.end() && temp->training_binding == "confirmed_current_run_temporary" &&
                !temp->account_training_used && temp->effective_rank < 0,
            "temporary old instance never becomes account-trained on promotion");
        actor.reset_in_run_variables();
        config->status().opers.clear();
        prior.observed_temporary.reset();
        prior.positive_temporary = false;
        prior.hope_cost = 0;
        prior.training_binding = "unknown_origin";
        old.hope_cost = 0;
        actor.pending(old, prior, true);
        check(actor.commit(), "actual zero-price unknown-origin prior transaction is recorded as unknown");
        const auto zero = observe_recruitment_duties(
            analyzer,
            catalog,
            account ? &*account : nullptr,
            config->status().opers,
            context,
            &actor.ledger());
        check(
            std::ranges::none_of(zero, [&](const auto& x) { return x.name == prior.name && x.account_training_used; }),
            "zero price and absent temporary badge do not prove ordinary origin or inherit M3");
        actor.pending(old, prior, true);
        config->bind_account("Official", "other-UID");
        const auto before = config->status().opers;
        check(
            !actor.commit() && actor.ledger().size() == 0 && config->status().opers.size() == before.size() &&
                config->status().opers.at(old.name).elite == before.at(old.name).elite &&
                config->status().opers.at(old.name).level == before.at(old.name).level,
            "actual pending commit refuses cross-UID confirmation before merging");
        config->bind_account("Official", "280875771");
        actor.pending(old, prior, true);
        config->status().run_id = "another-offline-exploration";
        check(
            !actor.commit() && actor.ledger().size() == 0,
            "actual pending commit refuses changed global exploration identity");
    }
    json::array explicit_false_results;
    if (promotion != observed.end()) {
        const auto card =
            std::ranges::find_if(analyzer.get_result(), [&](const auto& x) { return x.name == promotion->name; });
        if (card != analyzer.get_result().end()) {
            auto& raw = const_cast<std::vector<RoguelikeRecruitCandidateEvidence>&>(analyzer.get_candidate_evidence());
            const auto raw_index = promotion->observation_index;
            const auto saved_temporary = raw.at(raw_index).temporary;
            const auto saved_legacy = raw.at(raw_index).legacy_temporary_flag;
            for (const auto& kind :
                 std::vector<std::string> { "no_prior", "ordinary_prior", "temporary_prior", "positive_conflict" }) {
                config->bind_account("Official", "280875771");
                config->status().run_id = context.run_id;
                actor.reset_in_run_variables();
                config->status().opers.clear();
                auto prior = *promotion;
                prior.elite = 1;
                prior.level = 80;
                prior.operation = "招募";
                prior.hope_cost = 4;
                prior.ordinary_origin = kind != "temporary_prior";
                prior.observed_temporary = kind == "temporary_prior" ? std::optional<bool>(true) : std::nullopt;
                prior.positive_temporary = kind == "temporary_prior";
                prior.training_binding = "source_conditioned_non_temporary";
                prior.temporary_rule_source = catalog.temporary_rule_source();
                prior.observed_at = "controlled-prior-transaction-not-observed-UI";
                battle::roguelike::Recruitment old;
                old.name = prior.name;
                old.elite = prior.elite;
                old.level = prior.level;
                old.hope_cost = prior.hope_cost;
                if (kind == "no_prior") {
                    config->status().opers[prior.name] = RoguelikeOper { .elite = 1, .level = 80 };
                }
                else {
                    actor.pending(old, prior, true);
                    check(actor.commit(), "actual prior commit precondition " + kind);
                }
                // Counterfactual future producer output only. The real image,
                // current analysis ID, card and exact price link remain intact.
                // This false is never claimed as a detector/UI observation.
                raw.at(raw_index).temporary = false;
                raw.at(raw_index).legacy_temporary_flag =
                    kind == "positive_conflict" ? std::optional<bool>(true) : saved_legacy;
                const auto values = observe_recruitment_duties(
                    analyzer,
                    catalog,
                    account ? &*account : nullptr,
                    config->status().opers,
                    context,
                    &actor.ledger());
                const auto offer = std::ranges::find_if(values, [&](const auto& x) { return x.name == prior.name; });
                check(offer != values.end(), "actual observe retains explicit-false promotion " + kind);
                if (offer != values.end()) {
                    const bool expected_ordinary = kind == "ordinary_prior";
                    check(
                        offer->ordinary_origin == expected_ordinary &&
                            offer->account_training_used == expected_ordinary &&
                            (expected_ordinary ? offer->effective_rank == 10 : offer->effective_rank < 0),
                        "explicit false cannot create account-training origin " + kind);
                    const auto final = resolve_recruitment_final_offer(analyzer, values, *card, *offer);
                    actor.pending(*card, final.observed, true);
                    check(final.intent_matches && actor.commit(), "actual final refind and confirmed commit " + kind);
                    const auto source = actor.ledger().find(
                        context.client,
                        context.uid,
                        context.run_id,
                        offer->id,
                        config->status().opers.at(offer->name));
                    check(
                        source && source->ordinary == expected_ordinary &&
                            ((kind != "temporary_prior" && kind != "positive_conflict") || source->temporary == true),
                        "post-promotion origin agrees with observed training binding " + kind);
                    explicit_false_results.emplace_back(
                        json::object { { "case", kind },
                                       { "ordinary", offer->ordinary_origin },
                                       { "rank", offer->effective_rank },
                                       { "observed_offer", recruitment_duty_to_json(*offer) },
                                       { "confirmed_origin",
                                         source ? recruitment_origin_to_json(*source) : json::value(nullptr) } });
                }
            }
            raw.at(raw_index).temporary = saved_temporary;
            raw.at(raw_index).legacy_temporary_flag = saved_legacy;
        }
    }
    RecruitmentOriginLedger empty_run;
    check(
        !empty_run.bind("Official", "280875771", "") && empty_run.size() == 0,
        "empty restored run ID remains unknown and is never fabricated");
    json::object report { { "checks", checks },
                          { "failures", failures },
                          { "receipts", receipts },
                          { "explicit_false_results", explicit_false_results },
                          { "explicit_false_is_future_producer_counterfactual_not_UI_observation", true },
                          { "fixture_context_is_offline", true },
                          { "controlled_prior_is_not_observed_UI", true },
                          { "device_connections", 0 },
                          { "game_inputs", 0 },
                          { "account_cache_writes", 0 },
                          { "output_scoring_integrated", false },
                          { "live_run_claim", false } };
    std::ofstream(output / utils::path("recruit127.json")) << json::value(report);
    std::cout << "RECRUIT127 checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
