#pragma once
#include <string>
#include <vector>

namespace asst::spa
{
enum class SkillTrigger
{
    Acquisition,
    BattleStart,
    PreparationEnd,
    AutomaticBattle,
    Passive
};

struct SkillProfile
{
    std::string operator_name;
    std::string skill_name;
    SkillTrigger trigger = SkillTrigger::AutomaticBattle;
    int sp_cost = 0;
    int initial_sp = 0;
    int cooldown_rounds = 0;
    int max_uses_per_round = 0;
    bool affects_growth = false;
    bool requires_target = false;
};

struct SkillState
{
    std::string operator_name;
    std::string skill_name;
    int sp = 0;
    int cooldown_rounds = 0;
    int uses_this_round = 0;
    bool deployed = false;
    bool target_available = false;
    bool battle_started = false;
    bool automatic_condition_known = false;
    bool automatic_condition_met = false;
};

struct SkillDecision
{
    // Operator skills are controlled by the game in this mode.
    bool issue = false;
    bool target_required = false;
    std::string operator_name;
    std::string skill_name;
    std::string reason;
    bool automatic_trigger_expected = false;
};

bool can_queue_temporary_elite(
    int funds,
    bool preparation,
    bool identity_verified,
    bool elite,
    bool deployed,
    bool on_bench);
const std::vector<SkillProfile>& skill_profiles();
SkillDecision choose_skill(const SkillState& state, bool prioritize_growth, int enemy_count);
}
