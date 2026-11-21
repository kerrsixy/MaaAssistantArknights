#include "SPASkillPolicy.h"
#include <algorithm>

const std::vector<asst::spa::SkillProfile>& asst::spa::skill_profiles()
{
    static const std::vector<SkillProfile> profiles {
        { "车尔尼", "曲惊四座", SkillTrigger::AutomaticBattle, 35, 0, 0, 2, true, false },
        { "乌啾", "捉迷藏", SkillTrigger::AutomaticBattle, 25, 0, 0, 1, false, true },
        { "雪猎", "风雪连弩", SkillTrigger::AutomaticBattle, 30, 0, 0, 1, true, false },
        { "卡达", "同步索敌", SkillTrigger::AutomaticBattle, 25, 0, 0, 2, true, false },
        { "华法琳", "紧急包扎", SkillTrigger::AutomaticBattle, 20, 0, 0, 2, true, true },
        { "角峰", "雪境护卫", SkillTrigger::Passive, 0, 0, 0, 0, false, false },
    };
    return profiles;
}

asst::spa::SkillDecision asst::spa::choose_skill(const SkillState& state, bool prioritize_growth, int enemy_count)
{
    SkillDecision result { false,
                           false,
                           state.operator_name,
                           state.skill_name,
                           "No verified automatic skill activation condition" };
    const auto found = std::find_if(skill_profiles().begin(), skill_profiles().end(), [&](const auto& profile) {
        return profile.operator_name == state.operator_name && profile.skill_name == state.skill_name;
    });
    if (found == skill_profiles().end() || found->trigger != SkillTrigger::AutomaticBattle || !state.deployed ||
        !state.battle_started || !state.automatic_condition_known || !state.automatic_condition_met ||
        state.cooldown_rounds > 0 || state.sp < found->sp_cost || enemy_count < 0 ||
        (found->max_uses_per_round > 0 && state.uses_this_round >= found->max_uses_per_round) ||
        (found->requires_target && !state.target_available)) {
        return result;
    }
    result.automatic_trigger_expected = true;
    result.target_required = found->requires_target;
    result.reason = prioritize_growth && !found->affects_growth
                        ? "Automatic skill may add no growth; its engine timing cannot be manually held"
                        : "Game-controlled automatic skill condition is observed; no manual input is authorized";
    return result;
}

bool asst::spa::can_queue_temporary_elite(
    int funds,
    bool preparation,
    bool identity_verified,
    bool elite,
    bool deployed,
    bool on_bench)
{
    return funds >= 1 && funds <= 999 && preparation && identity_verified && !elite && (deployed != on_bench);
}
