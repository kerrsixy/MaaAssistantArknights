#pragma once
#include <string>

namespace asst::spa
{
struct ResultEvidence
{
    bool result_page = false;
    bool result_text_verified = false;
    bool return_home_verified = false;
    bool difficulty_known = false;
    int difficulty = 0;
    bool passed_rounds_known = false;
    int passed_rounds = 0;
    bool boss_identity_known = false;
    std::string boss_id;
    bool hidden_boss_marker = false;
    bool strategy_report_badge = false;
    bool contradictory = false;
    // Must be supplied by the settlement observation; interruption is never
    // a valid victory even when the round/difficulty OCR is complete.
    bool result_completed = false;
    // A badge from another report cannot establish this strategy's progress.
    // These fields must come from the same observed report as its badge.
    bool strategy_report_identity_verified = false;
    std::string strategy_report_id;
    int strategy_report_wins = -1;
};

struct ResultAssessment
{
    bool valid_victory = false;
    bool hidden_boss_victory = false;
    bool strategy_win_eligible = false;
    bool medal_eligible = false;
    std::string reason;
};

ResultAssessment assess_result(const ResultEvidence& evidence, int required_difficulty, const std::string& strategy_id);
}
