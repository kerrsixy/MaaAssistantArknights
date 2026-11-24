#pragma once

#include "RoguelikeConfig.h"
#include "Vision/Roguelike/RoguelikePreparationAnalyzer.h"

namespace asst
{
json::value commit_preparation_observations(
    RoguelikeStatus& status,
    const cv::Mat& frame,
    const std::vector<PreparationOperatorObservation>& observations,
    PreparationPage page);

int choose_preparation_skill(
    int preferred,
    int alternate,
    const PreparationOperatorObservation& observation,
    const RoguelikeOper* current_run = nullptr);
}
