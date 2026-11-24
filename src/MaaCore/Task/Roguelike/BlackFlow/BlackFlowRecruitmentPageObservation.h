#pragma once

#include <filesystem>
#include <meojson/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "Common/AsstBattleDef.h"
#include "Vision/Roguelike/RoguelikeRecruitImageAnalyzer.h"

namespace asst::blackflow
{
struct RecruitmentPageObservationContext
{
    std::string client;
    std::string uid;
    std::string run_id;
    std::string frame_read_completed_at_local;
    std::filesystem::path evidence_directory;
    int recruit_index = 0;
    int page_index = 0;
    bool analysis_succeeded = false;
    std::optional<int> legacy_filter_hope;
    bool initial_recruitment = false;
};

// Serialize the existing analysis and persist its frame. No game input or scoring.
// A missing directory/frame or failed write remains evidence failure only.
[[nodiscard]] json::value make_recruitment_page_observation(
    const cv::Mat& frame,
    const std::vector<battle::roguelike::Recruitment>& usable,
    const std::vector<RoguelikeRecruitHopeCostEvidence>& price_evidence,
    const RecruitmentPageObservationContext& context);
}
