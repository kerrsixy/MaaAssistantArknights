#pragma once

#include "BlackFlow/BlackFlowRecruitmentDuty.h"
#include "RoguelikeConfig.h"
#include "Vision/Roguelike/RoguelikePreparationAnalyzer.h"

namespace asst
{
namespace blackflow
{
struct PreparedPrimaryEntry
{
    std::string name;
    RoguelikeOper observed;
    ConditionalPrimaryEstimate component;
};

struct PreparedComponentBatch
{
    std::string client;
    std::string uid;
    std::string run_id;
    std::vector<PreparedPrimaryEntry> entries;
};

struct PreparedPrimaryChoice
{
    std::optional<std::size_t> preferred;
    std::string reason;
};

PreparedComponentBatch build_prepared_primary_components(
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const RoguelikeStatus& status,
    const std::string& client,
    const std::string& uid,
    const std::vector<std::string>& selected_names);

bool prepared_primary_matches(
    const PreparedComponentBatch& batch,
    const RoguelikeStatus& status,
    const std::string& client,
    const std::string& uid);

PreparedPrimaryChoice choose_prepared_primary_reference(
    const PreparedComponentBatch& batch,
    const std::vector<std::size_t>& candidates,
    std::size_t legacy);

json::value prepared_primary_to_json(const PreparedComponentBatch& batch);
}

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
