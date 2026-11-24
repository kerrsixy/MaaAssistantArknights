#include <cmath>
#include <iostream>
#include <limits>

#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Task/Roguelike/RoguelikeOperEvidence.h"
#include "Task/Roguelike/RoguelikeRecruitPolicy.h"
#include "Vision/Roguelike/RoguelikeRecruitEvidence.h"

class CurrentRunStrengthProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    explicit CurrentRunStrengthProbe(const std::shared_ptr<asst::RoguelikeConfig>& config) :
        RoguelikeBattleTaskPlugin({}, nullptr, "CurrentRunStrengthProbe", config, nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_requirements_met;

    void elite(const std::string& name, int value) { m_oper_elite.insert_or_assign(name, value); }
};

int main()
{
    using namespace asst;
    int checks = 0, failures = 0;
    const auto expect = [&](bool value, const char* label) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    RecruitEliteEvidence absent;
    expect(absent.elite() == -1, "no matching elite template is unknown, not E0");
    for (int elite : { 0, 1, 2 }) {
        RecruitEliteEvidence match;
        match.consider(elite, 0.99);
        expect(match.elite() == elite, "an explicit E0/E1/E2 observation retains its own value");
    }
    for (double score :
         { 0.0, -0.1, 1.1, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() }) {
        absent.consider(2, score);
        expect(absent.elite() == -1, "invalid scores do not invent a verified elite phase");
    }
    absent.consider(-1, 0.99);
    absent.consider(3, 0.99);
    expect(absent.elite() == -1, "unsupported elite values are not observations");
    RecruitEliteEvidence tied;
    tied.consider(0, 0.9);
    tied.consider(2, 0.9);
    expect(tied.elite() == -1, "conflicting equal matches are independent of hash iteration order");
    tied.consider(1, 0.95);
    expect(tied.elite() == 1, "a strictly stronger unambiguous observation can resolve a tie");
    tied.consider(2, 0.8);
    expect(tied.elite() == 1, "a weaker later match cannot replace the stronger evidence");

    std::unordered_map<std::string, RoguelikeOper> operators;
    expect(
        merge_current_run_operator(operators, "维什戴尔", -1, 55),
        "confirmed identity and level survive missing elite");
    expect(
        operators.at("维什戴尔").elite == -1 && operators.at("维什戴尔").level == 55,
        "new unknown phase is preserved explicitly");
    expect(
        !is_recruitment_strength_sufficient(-1, 55),
        "missing phase does not establish ordinary strength eligibility");
    expect(
        classify_recruit_candidate(false, false, true, true, 6) == RoguelikeRecruitTier::FreeTemporarySixStar,
        "observed free temporary recruitment remains possible without inventing strength");
    expect(merge_current_run_operator(operators, "维什戴尔", 2, 1), "low-level E2 is a valid distinct observation");
    expect(
        operators.at("维什戴尔").elite == 2 && operators.at("维什戴尔").level == 1,
        "level one is not rewritten as E1 or maximum training");
    expect(
        merge_current_run_operator(operators, "维什戴尔", -1, 55),
        "duel may update an independently observed level");
    expect(
        operators.at("维什戴尔").elite == 2 && operators.at("维什戴尔").level == 55,
        "unknown duel phase does not downgrade a confirmed current-run E2");
    expect(
        merge_current_run_operator(operators, "维什戴尔", 1, 70),
        "a genuinely observed lower phase is not suppressed");
    expect(
        operators.at("维什戴尔").elite == 1 && operators.at("维什戴尔").level == 70,
        "state is observation-driven, not artificially monotonic");
    expect(
        merge_current_run_operator(operators, "维什戴尔", -1, -1),
        "a later missing panel preserves independent evidence");
    expect(
        operators.at("维什戴尔").elite == 1 && operators.at("维什戴尔").level == 70,
        "no missing field becomes a fresh zero observation");
    expect(
        merge_current_run_operator(operators, "新成员", -1, 0),
        "a confirmed roster identity can precede verified strength");
    expect(
        operators.at("新成员").elite == -1 && operators.at("新成员").level == 0,
        "identity-only observation does not fabricate E0 or level");
    expect(merge_current_run_operator(operators, "新成员", 0, 1), "real E0 remains accepted");
    expect(
        operators.at("新成员").elite == 0 && operators.at("新成员").level == 1,
        "unknown and E0 stay distinguishable after merging");
    const auto count = operators.size();
    for (const auto elite : { -2, 3 }) {
        expect(!merge_current_run_operator(operators, "invalid", elite, 50), "invalid phase cannot create an entry");
    }
    expect(!merge_current_run_operator(operators, "", 2, 90), "missing identity cannot update the roster");
    expect(!merge_current_run_operator(operators, "invalid", 1, -2), "invalid level cannot create an entry");
    expect(operators.size() == count, "invalid observations preserve existing roster membership");

    auto config = std::make_shared<RoguelikeConfig>();
    config->status().opers = operators;
    CurrentRunStrengthProbe probe(config);
    battle::DeploymentOper oper;
    oper.name = "维什戴尔";
    oper.role = battle::Role::Sniper;
    battle::roguelike::DeployInfoWithRank e2_plan;
    e2_plan.minimum_elite = 2;
    probe.elite(oper.name, config->status().opers.at(oper.name).elite);
    expect(!probe.deployment_requirements_met(oper, e2_plan), "actual battle consumer sees E1, not account E2");
    merge_current_run_operator(config->status().opers, oper.name, 2, 1);
    merge_current_run_operator(config->status().opers, oper.name, -1, 55);
    probe.elite(oper.name, config->status().opers.at(oper.name).elite);
    expect(
        probe.deployment_requirements_met(oper, e2_plan),
        "actual battle consumer retains current-run E2 after unknown duel detail");
    oper.name = "unknown_this_run";
    merge_current_run_operator(config->status().opers, oper.name, -1, 75);
    probe.elite(oper.name, config->status().opers.at(oper.name).elite);
    expect(!probe.deployment_requirements_met(oper, e2_plan), "a new unknown cannot activate an elite-dependent plan");
    battle::roguelike::DeployInfoWithRank basic_plan;
    expect(
        probe.deployment_requirements_met(oper, basic_plan),
        "unknown elite does not globally forbid ordinary utility deployment");

    std::cout << "OPER_EVIDENCE checks=" << checks << " failures=" << failures
              << " device_inputs=0 live_ocr_verified=false dynamic_battle_verified=false\n";
    return failures ? 1 : 0;
}
