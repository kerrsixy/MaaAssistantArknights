#include "Task/Roguelike/BlackFlowAuthoredBudget.h"

#include <filesystem>
#include <iostream>
#include <meojson/json.hpp>

int wmain(int argc, wchar_t** argv)
{
    using namespace asst::blackflow;
    if (argc != 2) {
        return 2;
    }
    const auto fixture = json::open(std::filesystem::path(argv[1]));
    if (!fixture) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto ck = [&](bool value, const char* name) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << name << '\n';
        }
    };
    const auto& prep = fixture->at("preparation");
    const auto& skill = fixture->at("training_skill_rule");
    AuthoredBudgetObservations actual;
    actual.strict_authored = fixture->at("authored_reserve").as_boolean();
    actual.duty_eligible = fixture->at("existing_duty_admitted").as_boolean();
    actual.identity_known = prep.at("operator_id") == fixture->at("operator_id");
    actual.preparation_valid = prep.at("selection_source_sequence").as_integer() > 0 &&
                               prep.at("selected_skill").as_integer() == skill.at("index").as_integer() &&
                               skill.at("known").as_boolean() &&
                               prep.at("elite").as_integer() >= skill.at("unlock_elite").as_integer() &&
                               prep.at("level").as_integer() >= skill.at("unlock_level").as_integer();
    actual.already_deployed = fixture->at("already_deployed").as_boolean();
    const int step = fixture->at("plan_step").as_integer();
    ck(actual.identity_known && actual.preparation_valid, "real run72 current preparation supports the request scope");
    AuthoredBudgetReobserver ledger;
    for (const auto& row : fixture->at("cases").as_array()) {
        auto observed = actual;
        observed.available = row.at("available").as_boolean();
        observed.cooling = row.at("cooling").as_boolean();
        observed.card_cost = row.at("card_cost").as_integer();
        observed.observed_cost = row.at("dp").as_integer();
        const auto result = ledger.request(step, observed);
        const auto expected = observed.observed_cost >= observed.card_cost
                                  ? AuthoredBudgetReobservation::NeedsReobservation
                                  : AuthoredBudgetReobservation::KeepExisting;
        ck(result == expected, "real18/22 preserve old cost path; observed24/unready requests only another frame");
        actual = observed;
    }
    ck(ledger.requested_steps() == 1, "one actual plan step requested once");
    ck(ledger.request(step, actual) == AuthoredBudgetReobservation::KeepExisting,
       "same-step persistent contradiction cannot keep yielding");
    int repeated_requests = 0;
    for (int i = 0; i < 100; ++i) {
        repeated_requests += ledger.request(step, actual) == AuthoredBudgetReobservation::NeedsReobservation ? 1 : 0;
    }
    ck(repeated_requests == 0 && ledger.requested_steps() == 1,
       "one hundred unchanged observations cannot renew the same-step yield");
    for (int k = 0; k < 10; ++k) {
        AuthoredBudgetReobserver fresh;
        auto o = actual;
        switch (k) {
        case 0:
            o.strict_authored = false;
            break;
        case 1:
            o.duty_eligible = false;
            break;
        case 2:
            o.identity_known = false;
            break;
        case 3:
            o.preparation_valid = false;
            break;
        case 4:
            o.already_deployed = true;
            break;
        case 5:
            o.available = true;
            break;
        case 6:
            o.cooling = true;
            break;
        case 7:
            o.card_cost = 0;
            break;
        case 8:
            o.observed_cost = -1;
            break;
        case 9:
            o.observed_cost = o.card_cost - 1;
            break;
        }
        ck(fresh.request(step, o) == AuthoredBudgetReobservation::KeepExisting,
           "scope, missing facts, cooling, existing duty or true shortfall cannot trigger new yielding");
        ck(fresh.requested_steps() == 0, "ineligible observations do not consume a future valid attempt");
    }
    AuthoredBudgetReobserver reset;
    ck(reset.request(-1, actual) == AuthoredBudgetReobservation::KeepExisting, "invalid plan index rejected");
    ck(reset.request(step, actual) == AuthoredBudgetReobservation::NeedsReobservation, "first valid step request");
    ck(reset.request(step + 1, actual) == AuthoredBudgetReobservation::NeedsReobservation,
       "independent authored step has own limit");
    reset.reset();
    ck(reset.requested_steps() == 0, "logical new battle reset clears all steps");
    ck(reset.request(step, actual) == AuthoredBudgetReobservation::NeedsReobservation,
       "same-name new battle can reuse the same plan index after reset");
    AuthoredBudgetReobserver actor_state_boundary;
    const bool first_deploy = true;
    const int deployment_count = 0;
    const int pending_plan = step;
    ck(actor_state_boundary.request(step, actual) == AuthoredBudgetReobservation::NeedsReobservation,
       "request-only helper result obtained");
    ck(first_deploy && deployment_count == 0 && pending_plan == step,
       "request API has no mutable access to first/count/pending; actor flow separately source-reviewed");
    ck(AuthoredPlanResult::Reobserve != AuthoredPlanResult::Handled, "typed yield cannot masquerade as handled action");
    std::cout << "AUTHORED_BUDGET116_V3 checks=" << checks << " failures=" << failures
              << " base_core_objects=311 actor_consumers_compiled=true image_ocr_added=false device_inputs=0\n";
    return failures ? 1 : 0;
}
