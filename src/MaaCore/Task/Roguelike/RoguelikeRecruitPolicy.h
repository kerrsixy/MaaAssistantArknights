#pragma once

namespace asst
{
// Own operators need the same strength gate in explicit and generic recruitment paths.
constexpr bool is_recruitment_strength_sufficient(int elite, int level)
{
    return elite == 2 || (elite == 1 && (level >= 55 || level == 0));
}

// Promotion value ranks suitable new recruits; it cannot establish suitability.
constexpr int score_elite_two_recruit(int suitability, int promotion_value)
{
    return suitability > 0 ? suitability + (promotion_value > 0 ? promotion_value : 0) : suitability;
}

enum class RoguelikeRecruitTier
{
    EliteTwo = 0,
    Suitable = 1,
    FreeTemporarySixStar = 2,
    FreeLowRarity = 3,
    Skip = 4,
};

// free_recruit requires observed zero cost, not inferred rarity or elite status.
constexpr RoguelikeRecruitTier
    classify_recruit_candidate(bool elite_two, bool suitable, bool free_recruit, bool temporary, int rarity)
{
    if (elite_two && suitable) {
        return RoguelikeRecruitTier::EliteTwo;
    }
    if (suitable) {
        return RoguelikeRecruitTier::Suitable;
    }
    if (free_recruit && temporary && rarity == 6) {
        return RoguelikeRecruitTier::FreeTemporarySixStar;
    }
    if (free_recruit && rarity > 0 && rarity <= 3) {
        return RoguelikeRecruitTier::FreeLowRarity;
    }
    return RoguelikeRecruitTier::Skip;
}
}
