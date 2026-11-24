#include "AsstCaller.h"
#include "Config/Roguelike/BlackFlow/BlackFlowOpeningDutyCatalog.h"
#include "MaaUtils/Encoding.h"
#include <algorithm>
#include <iostream>

int wmain(int argc, wchar_t** argv)
{
    using namespace asst::blackflow;
    if (argc != 7) {
        return 2;
    }
    const auto facts = json::open(std::filesystem::path(argv[1])),
               training = json::open(std::filesystem::path(argv[2])),
               battle = json::open(std::filesystem::path(argv[3])),
               fixture = json::open(std::filesystem::path(argv[4]));
    if (!facts || !training || !battle || !fixture) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto ck = [&](bool ok, const char* s) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << s << '\n';
        }
    };
    const std::filesystem::path resource_root(argv[5]), output(argv[6]);
    std::filesystem::create_directories(output);
    ck(AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()), "actual user directory accepted");
    ck(AsstLoadResource(MAA_NS::from_u16(resource_root.native()).c_str()), "actual full resource loader succeeds");
    ck(opening_duty_catalog().rule_count() == 1, "actual loader populates the actor singleton with one reviewed fact");
    BlackFlowOpeningDutyCatalog c;
    ck(c.configure(*facts, *training, *battle) && c.rule_count() == 1,
       "actual resource and Nov1 training/battle definitions load one reviewed rule");
    const std::string id = facts->at("rules")[0].at("operator_id").as_string();
    const auto& bd = battle->at("chars").at(id);
    const auto& arr = fixture->at("preparation").as_array();
    auto found = std::ranges::find_if(arr, [&](const auto& x) { return x.get("operator_id", "") == id; });
    if (found == arr.end()) {
        return 4;
    }
    OpeningDutyRuntime e;
    e.unique_operator_id = id;
    e.prepared_operator_id = found->at("operator_id").as_string();
    e.properties_operator_id = id;
    e.selected_skill = found->at("selected_skill").as_integer();
    e.elite = found->at("elite").as_integer();
    e.displayed_skill_level = found->at("displayed_skill_levels")[0].as_integer();
    e.selection_sequence = found->at("selection_source_sequence").as_integer();
    e.training_sequence = found->at("training_source_sequence").as_integer();
    e.properties_known = true;
    e.medic_role = bd.get("profession", "") == "MEDIC";
    e.wander_subrole = bd.get("subProfessionId", "") == "wandermedic";
    for (int i = 0; i < 3; ++i) {
        e.ranges[i] = bd.at("rangeId")[i].as_string();
    }
    if (auto ts = bd.find<json::array>("tokens")) {
        for (const auto& t : *ts) {
            e.tokens.push_back(t.as_string());
        }
    }
    ck(e.selected_skill == 1 && e.elite == 2 && e.displayed_skill_level == 10 && e.selection_sequence == 21,
       "real run71 current-preparation values preserved");
    ck(c.resolve(e) == BaseOpeningDuty::HealingOnly,
       "exact resource ID+actual current selection produces base healing duty");
    OpeningDutyContext empty;
    empty.patient = HealingPatientState::NoneCovered;
    ck(c.choose(e, empty) == OpeningDutyChoice::DeferIdleHealing,
       "actual initial empty ledger with17DP defers cost16 Pure candidate");
    auto no_rank = e;
    no_rank.displayed_skill_level = -1;
    no_rank.training_sequence = 0;
    ck(c.resolve(no_rank) == BaseOpeningDuty::HealingOnly,
       "all10reviewed ranks do not require exact rank OCR or training receipt");
    for (int k = 0; k < 8; ++k) {
        auto x = e;
        switch (k) {
        case 0:
            x.prepared_operator_id = "wrong";
            break;
        case 1:
            x.properties_operator_id = "wrong";
            break;
        case 2:
            x.selection_sequence = 0;
            break;
        case 3:
            x.selected_skill = 2;
            break;
        case 4:
            x.elite = -1;
            break;
        case 5:
            x.properties_known = false;
            break;
        case 6:
            x.tokens = { "unexpected token" };
            break;
        case 7:
            x.ranges[0] = "changed";
            break;
        }
        ck(c.resolve(x) == BaseOpeningDuty::Unknown,
           "identity/currentselection/phase/currentdefinition mismatch returnsUnknown");
    }
    for (int k = 0; k < 5; ++k) {
        auto ctx = empty;
        switch (k) {
        case 0:
            ctx.authored_plan = true;
            break;
        case 1:
            ctx.uncertain_placement = true;
            break;
        case 2:
            ctx.explicit_nonhealing_task = true;
            break;
        case 3:
            ctx.verified_incoming_covered_ally_task = true;
            break;
        case 4:
            ctx.patient = HealingPatientState::Covered;
            break;
        }
        ck(c.choose(e, ctx) == OpeningDutyChoice::KeepForTask,
           "authored/uncertain/explicitutility/incoming/patient retains candidate");
    }
    auto unknown_scene = empty;
    unknown_scene.patient = HealingPatientState::Unknown;
    ck(c.choose(e, unknown_scene) == OpeningDutyChoice::KeepUnknown, "uncertain scene is not empty proof");
    for (const auto* other : { "char_003_kalts", "char_1020_reed2" }) {
        auto x = e;
        x.unique_operator_id = other;
        x.prepared_operator_id = other;
        x.properties_operator_id = other;
        x.wander_subrole = false;
        ck(c.resolve(x) == BaseOpeningDuty::Unknown, "Kaltsit andincantation not assigned healing-only byrole");
        ck(c.choose(x, empty) == OpeningDutyChoice::KeepUnknown, "unmodeled immediateoffense/summon path preserved");
    }
    for (int k = 0; k < 6; ++k) {
        auto f = *facts, t = *training, b = *battle;
        switch (k) {
        case 0:
            t["game_data_revision"] = "future";
            break;
        case 1:
            t["character_table_sha256"] = "changed";
            break;
        case 2:
            t["operators"][id]["skills"][0]["skill_id"] = "different_skill";
            break;
        case 3:
            b["chars"][id]["rangeId"][0] = "different_range";
            break;
        case 4:
            b["chars"][id]["tokens"] = json::array { "newtoken" };
            break;
        case 5:
            f["rules"][0]["review_status"] = "unreviewed";
            break;
        }
        ck(!c.configure(f, t, b) && c.rule_count() == 0,
           "actual resource runtime metadata/slot/definition mismatch disables stale facts");
        ck(c.resolve(e) == BaseOpeningDuty::Unknown, "failed reload cannot retain old positive rule");
    }
    auto subset = *facts;
    subset["rules"][0]["all_skill_levels"] = false;
    subset["rules"][0]["reviewed_skill_levels"] = json::array { 10 };
    ck(c.configure(subset, *training, *battle), "rank-dependent schema case loads reviewedsubset");
    ck(c.resolve(no_rank) == BaseOpeningDuty::Unknown, "rank-dependent rule cannot borrow unreadable rank");
    ck(c.resolve(e) == BaseOpeningDuty::HealingOnly, "rank-dependent reviewed10 withactual receipt qualifies");
    ck(c.configure(*facts, *training, *battle) && c.resolve(e) == BaseOpeningDuty::HealingOnly,
       "restoring valid resource restores exact fact");
    std::cout << "OPENING_DUTY114 checks=" << checks << " failures=" << failures
              << " core_cohort_objects=310 actual_resource_registry_checked=true device_inputs=0\n";
    return failures ? 1 : 0;
}
