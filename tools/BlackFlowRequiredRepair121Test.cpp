#include "Task/Miscellaneous/OperBoxPageCoverage.h"
#include "Task/Roguelike/BlackFlow/BlackFlowRequiredModuleRepair.h"

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
    const auto ck = [&](bool value, const char* reason) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << reason << '\n';
        }
    };
    std::vector<RequiredRepairTarget> required;
    for (const auto& r : fixture->at("required").as_array()) {
        required.push_back({ r.at("id").as_string(), r.at("name").as_string() });
    }
    const auto strings = [&](const char* key) {
        std::set<std::string> value;
        for (const auto& id : fixture->at(key).as_array()) {
            value.insert(id.as_string());
        }
        return value;
    };
    auto unknown = strings("unknown_names"), seen = strings("primary_seen_ids"), attempted = strings("attempted_names");
    const auto missing = required_module_repair_targets(required, unknown, seen, attempted);
    ck(missing.size() == 1 && missing.front().id == fixture->at("old_gnosis_reference").at("id").as_string(),
       "actual run73 unseen required target is eligible despite retained old cache");
    ck(!fixture->at("complete_current_gnosis_frame_available").as_boolean(),
       "no current target source frame is invented");
    ck(!asst::oper_box_pages_overlap(strings("before_ids"), strings("after_ids")),
       "real14-to15 adjacent OCR sets have no certified ID overlap");
    ck(!asst::oper_box_pages_overlap({}, strings("after_ids")), "empty previous page is not overlap proof");
    ck(asst::oper_box_pages_overlap({ "shared" }, { "shared", "new" }), "controlled positive anchor overlap");
    auto module_failed_seen = seen;
    module_failed_seen.insert(missing.front().id);
    ck(required_module_repair_targets(required, unknown, module_failed_seen, attempted).empty(),
       "seen-but-module-unknown cannot enter the unseen-target supplement");
    auto module_attempted = attempted;
    module_attempted.insert(missing.front().name);
    ck(required_module_repair_targets(required, unknown, seen, module_attempted).empty(),
       "already visited unknown target is not retried");
    ck(required_module_repair_targets(required, {}, seen, attempted).empty(), "complete targets need no supplement");
    auto bad = required;
    bad.push_back(required.front());
    ck(required_module_repair_targets(bad, unknown, seen, attempted).empty(),
       "ambiguous duplicate targets cannot authorize a locating pass");
    bad = required;
    bad.front().id.clear();
    ck(required_module_repair_targets(bad, unknown, seen, attempted).empty(), "missing target ID rejects the plan");
    const auto original_unknown = unknown;
    (void)required_module_repair_targets(required, unknown, seen, attempted);
    ck(unknown == original_unknown, "planning cannot mark a module complete");
    ck(required_repair_scanner_budget(1) == 126 && required_repair_scanner_budget(5) == 118 &&
           required_repair_scanner_budget(64) == 0,
       "target locator/restored analyses reserved inside total128 list-frame budget");
    asst::OperBoxTraversalBudget budget(128, 1);
    int analyses = 0;
    while (budget.take_analysis()) {
        ++analyses;
    }
    ck(analyses == 128 && !budget.can_analyze() && !budget.take_analysis(),
       "head/shot/backfill analysis budget terminates at128");
    ck(budget.request_backtrack() && !budget.request_backtrack(), "entire supplementary pass has at mostone backtrack");
    ck(budget.backtracks() == 1 && budget.analyses() == 128, "independent budget counters preserve limits");
    asst::OperBoxTraversalBudget zero(0, 0);
    ck(!zero.take_analysis() && !zero.request_backtrack(), "zero budget cannot create traversal work");
    std::cout << "REQUIRED_REPAIR121 checks=" << checks << " failures=" << failures
              << " scanner_workflow_executed=false ui_visited=false target_fresh_success=false device_inputs=0\n";
    return failures ? 1 : 0;
}
