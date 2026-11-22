#include "SPAResultPolicy.h"
#include "SPABossPolicy.h"

asst::spa::ResultAssessment
    asst::spa::assess_result(const ResultEvidence& evidence, int required_difficulty, const std::string& strategy_id)
{
    ResultAssessment result;
    if (required_difficulty < 1 || required_difficulty > 20 || strategy_id.empty() || evidence.contradictory ||
        !evidence.result_page || !evidence.result_text_verified || !evidence.return_home_verified ||
        !evidence.result_completed || !evidence.difficulty_known || evidence.difficulty != required_difficulty ||
        !evidence.passed_rounds_known || evidence.passed_rounds < 1 || evidence.passed_rounds > 20) {
        result.reason = "Result page or required difficulty/round evidence is incomplete";
        return result;
    }
    result.valid_victory = true;
    result.strategy_win_eligible = evidence.strategy_report_badge && evidence.strategy_report_identity_verified &&
                                   evidence.strategy_report_id == strategy_id && evidence.strategy_report_wins >= 3;
    result.hidden_boss_victory =
        evidence.hidden_boss_marker && evidence.boss_identity_known && current_hidden_boss_enemy(evidence.boss_id);
    result.medal_eligible = result.strategy_win_eligible && result.valid_victory;
    result.reason = result.hidden_boss_victory     ? "Hidden boss identity and result evidence verified"
                    : result.strategy_win_eligible ? "Valid result and strategy report badge evidence verified"
                                                   : "Valid result verified; strategy report badge still required";
    return result;
}
