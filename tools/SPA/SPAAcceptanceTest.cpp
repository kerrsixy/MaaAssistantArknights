#include "Task/MiniGame/SPA/SPAResultPolicy.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace asst::spa;

    ResultEvidence result;
    result.result_page = true;
    result.result_text_verified = true;
    result.return_home_verified = true;
    result.difficulty_known = true;
    result.difficulty = 14;
    result.passed_rounds_known = true;
    result.passed_rounds = 14;
    result.result_completed = true;
    result.strategy_report_badge = true;

    // A complete settlement and an unbound badge used to credit any supplied ID.
    const auto unbound = assess_result(result, 14, "band_sarkazb");
    assert(unbound.valid_victory);
    assert(!unbound.strategy_win_eligible && !unbound.medal_eligible);

    result.strategy_report_identity_verified = true;
    result.strategy_report_id = "band_bonus";
    result.strategy_report_wins = 3;
    const auto wrong_report = assess_result(result, 14, "band_sarkazb");
    assert(wrong_report.valid_victory);
    assert(!wrong_report.strategy_win_eligible && !wrong_report.medal_eligible);

    result.strategy_report_id = "band_sarkazb";
    result.strategy_report_wins = 2;
    assert(!assess_result(result, 14, "band_sarkazb").medal_eligible);
    result.strategy_report_wins = 4;
    assert(!assess_result(result, 14, "band_sarkazb").medal_eligible);
    result.strategy_report_wins = 3;
    assert(assess_result(result, 14, "band_sarkazb").medal_eligible);
    result.strategy_report_identity_verified = false;
    assert(!assess_result(result, 14, "band_sarkazb").medal_eligible);
    result.strategy_report_identity_verified = true;
    result.strategy_report_badge = false;
    assert(!assess_result(result, 14, "band_sarkazb").medal_eligible);

    result.strategy_report_badge = true;
    result.result_completed = false;
    const auto interrupted = assess_result(result, 14, "band_sarkazb");
    assert(!interrupted.valid_victory && !interrupted.medal_eligible);
    result.result_completed = true;
    assert(!assess_result(result, 13, "band_sarkazb").valid_victory);

    result.hidden_boss_marker = true;
    result.boss_id = "enemy_9037_aceel_2";
    assert(!assess_result(result, 14, "band_sarkazb").hidden_boss_victory);
    result.boss_identity_known = true;
    assert(assess_result(result, 14, "band_sarkazb").hidden_boss_victory);
    result.boss_id = "enemy_9037_aceel";
    assert(!assess_result(result, 14, "band_sarkazb").hidden_boss_victory);
    std::cout << "SPA acceptance evidence checks passed\n";
}
