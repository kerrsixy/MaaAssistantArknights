#pragma once

#include "BlackFlow/BlackFlowRecruitmentDuty.h"
#include "RoguelikeConfig.h"
#include "Vision/Roguelike/RoguelikePreparationAnalyzer.h"

namespace asst
{
namespace blackflow
{
class BlackFlowSession;

struct PreparationStageContext
{
    std::string client;
    std::string uid;
    std::string run_id;
    std::string stage_name;
    std::string transaction_id;
    std::string action_id;
    std::string frame_key;
    std::string frame_path;
    bool frame_saved = false;
    std::uint64_t run_revision = 0;
    std::uint64_t page_revision = 0;
    std::uint64_t map_revision = 0;
    std::uint64_t viewport_revision = 0;
    std::uint64_t node = 0;
    bool operator==(const PreparationStageContext&) const = default;
};

std::optional<PreparationStageContext> current_preparation_stage(
    const BlackFlowSession& session,
    const std::string& client,
    const std::string& uid,
    const std::string& run_id);
json::value preparation_stage_to_json(const PreparationStageContext& context);

struct PlannedPrimaryEntry
{
    std::string name;
    RoguelikeOper full_observed;
    int planned_skill = -1;
    ConditionalPrimaryEstimate component;
};

struct FormationRoleProposal
{
    PreparationStageContext stage;
    std::vector<std::string> requested_members;
    std::vector<std::string> primary_candidates;
    std::vector<PlannedPrimaryEntry> full_observations;
    bool selected_set_verified = false;
    std::string reason;
};

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
    std::optional<PreparationStageContext> stage;
    std::vector<std::string> primary_role_candidates;
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

PreparedPrimaryChoice choose_initial_primary_reference(
    const std::vector<ConditionalPrimaryEstimate>& components,
    const std::vector<std::size_t>& candidates,
    std::optional<std::size_t> legacy);

std::vector<PlannedPrimaryEntry> build_planned_primary_components(
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const RoguelikeStatus& status,
    const std::vector<std::string>& names);

FormationRoleProposal choose_formation_primary(
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const RoguelikeStatus& status,
    const PreparationStageContext& stage,
    const std::vector<std::string>& all_names,
    const std::vector<std::string>& legacy_members,
    const std::string& opening_core);

bool verify_formation_selected_set(FormationRoleProposal& proposal, const std::vector<std::string>& observed_selected);

bool formation_proposal_matches_actual(
    const FormationRoleProposal& proposal,
    const PreparationStageContext& current,
    const RoguelikeStatus& status,
    const std::vector<std::string>& actual_members);

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
