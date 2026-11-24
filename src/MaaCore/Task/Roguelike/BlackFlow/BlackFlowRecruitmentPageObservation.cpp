#include "BlackFlowRecruitmentPageObservation.h"

#include <algorithm>
#include <atomic>
#include <utility>

#include "Config/Roguelike/RoguelikeTrainingConfig.h"
#include "MaaUtils/ImageIo.h"
#include "MaaUtils/Time.hpp"
#include "Utils/Logger.hpp"

namespace asst::blackflow::recruitment_page_observation_detail
{
json::array rect_json(const Rect& rect)
{
    return json::array { rect.x, rect.y, rect.width, rect.height };
}

}

namespace asst::blackflow
{
// Observe the existing analysis result. This helper performs no game input,
// extra recognition, account scan, resource mutation, or candidate scoring.
json::value make_recruitment_page_observation(
    const cv::Mat& frame,
    const std::vector<battle::roguelike::Recruitment>& usable,
    const std::vector<RoguelikeRecruitHopeCostEvidence>& price_evidence,
    const RecruitmentPageObservationContext& context)
{
    using recruitment_page_observation_detail::rect_json;
    const auto& client = context.client;
    const auto& uid = context.uid;
    const auto& run_id = context.run_id;
    const auto& frame_read_completed_at = context.frame_read_completed_at_local;
    const int recruit_index = context.recruit_index;
    const int page_index = context.page_index;
    const bool analysis_succeeded = context.analysis_succeeded;
    const int legacy_filter_hope = context.legacy_filter_hope.value_or(-1);
    const bool initial_recruitment = context.initial_recruitment;
    // This sequence identifies evidence only; it never enters gameplay state.
    static std::atomic<unsigned long long> sequence { 0 };
    const auto observation_sequence = ++sequence;
    const auto evidence_id = MAA_NS::format_now_for_filename() + "_" + std::to_string(observation_sequence);
    const auto& directory = context.evidence_directory;
    const auto filename = "page_" + evidence_id + ".png";
    const auto path = directory / utils::path(filename);
    bool frame_saved = false;
    std::string frame_error;
    std::error_code error;
    if (directory.empty() || frame.empty()) {
        frame_error = "user_directory_unconfigured_or_frame_empty";
    }
    else {
        std::filesystem::create_directories(directory, error);
        if (error) {
            frame_error = "evidence_directory_unavailable";
        }
        else {
            try {
                frame_saved = MAA_NS::imwrite(path, frame);
                if (!frame_saved) {
                    frame_error = "evidence_frame_write_failed";
                }
            }
            catch (const cv::Exception& exception) {
                frame_error = "evidence_frame_encoder_failed";
                LogWarn << __FUNCTION__ << "Recruitment evidence frame encoding failed" << exception.what();
            }
        }
    }
    if (!frame_saved) {
        LogWarn << __FUNCTION__ << "Recruitment frame evidence unavailable" << frame_error;
    }

    json::array candidates;
    for (std::size_t index = 0; index < usable.size(); ++index) {
        const auto& candidate = usable[index];
        const auto identity = RoguelikeTraining.unique_identity(candidate.name);
        const bool price_known = candidate.hope_cost && *candidate.hope_cost >= 0;
        candidates.emplace_back(
            json::object {
                { "candidate_observation_index", index },
                { "canonical_offer_id", nullptr },
                { "name", candidate.name },
                { "operator_id", identity.empty() ? json::value(nullptr) : json::value(identity) },
                { "identity_source", "RoguelikeTraining.unique_identity" },
                { "identity_catalog_cohort", nullptr },
                { "selection_rect", rect_json(candidate.rect) },
                { "selection_rect_source", "analyzer_name_OCR_rect_not_full_card_boundary" },
                { "raw_elite", candidate.elite },
                { "elite",
                  candidate.elite >= 0 && candidate.elite <= 2 ? json::value(candidate.elite) : json::value(nullptr) },
                { "elite_source", "RoguelikeRecruitImageAnalyzer.match_elite" },
                { "elite_match_score", nullptr },
                { "raw_level", candidate.level },
                { "level", candidate.level > 0 ? json::value(candidate.level) : json::value(nullptr) },
                { "level_source", "RoguelikeRecruitImageAnalyzer.match_level" },
                { "level_OCR_score", nullptr },
                { "analyzer_temporary_flag", candidate.temporary },
                { "temporary", candidate.temporary ? json::value(true) : json::value(nullptr) },
                { "temporary_source",
                  candidate.temporary ? "positive_first_slot_badge_classifier"
                                      : "negative_identity_evidence_not_exposed" },
                { "hope_cost", price_known ? json::value(*candidate.hope_cost) : json::value(nullptr) },
                { "hope_cost_source",
                  candidate.temporary ? "first_slot_temporary_badge_zero_cost"
                                      : "analyzer_associated_currency_flag_price_or_unknown" },
                { "card_operation", nullptr },
                { "card_operation_source", "see_separate_price_evidence_exact_flag_link_not_exposed_by_Recruitment" },
                { "priority_adjusted", false },
            });
    }

    json::array prices;
    std::size_t unpaired_prices = 0;
    for (std::size_t index = 0; index < price_evidence.size(); ++index) {
        const auto& price = price_evidence[index];
        const auto name_matches = price.name.empty() ? 0 : std::ranges::count_if(usable, [&](const auto& candidate) {
            return candidate.name == price.name;
        });
        const bool unpaired = name_matches == 0;
        unpaired_prices += unpaired ? 1 : 0;
        prices.emplace_back(
            json::object {
                { "price_evidence_index", index },
                { "recognized_name", price.name.empty() ? json::value(nullptr) : json::value(price.name) },
                { "currency_rect", rect_json(price.currency_rect) },
                { "currency_anchor_match_score", nullptr },
                { "operation_roi", rect_json(price.operation_roi) },
                { "price_roi", rect_json(price.price_roi) },
                { "digits_rect", rect_json(price.digits_rect) },
                { "raw_operation", price.operation },
                { "raw_digits", price.raw_digits },
                { "operation_score", price.operation_score },
                { "digits_score", price.digits_score },
                { "hope_cost",
                  price.hope_cost && *price.hope_cost >= 0 ? json::value(*price.hope_cost) : json::value(nullptr) },
                { "reason", price.reason },
                { "same_name_usable_count", name_matches },
                { "unpaired_with_usable_result", unpaired },
                { "exact_usable_pairing_verified", nullptr },
                { "pairing_limit", "name_only_relationship_not_exact_currency_flag_link" },
            });
    }

    return json::object {
        { "schema", 1 },
        { "mode", "raw_prefilter_page_observation" },
        { "observation_id", evidence_id },
        { "observation_sequence", observation_sequence },
        { "run_id", run_id },
        { "decision_id", nullptr },
        { "client", client },
        { "uid", uid },
        { "account_identity_source", "RoguelikeConfig_bound_account_not_reverified_on_this_page" },
        { "account_snapshot_sha256", nullptr },
        { "recruit_index", recruit_index },
        { "page_index", page_index },
        { "consumer_path", "general_candidate_scan_only" },
        { "initial_recruitment", initial_recruitment },
        { "fixed_support_and_appointed_paths_covered", false },
        { "frame_read_completed_at_local", frame_read_completed_at },
        { "clock", "local_system_time" },
        { "frame_saved", frame_saved },
        { "frame_path", frame_saved ? json::value(utils::path_to_utf8_string(path)) : json::value(nullptr) },
        { "frame_error", frame_error },
        { "frame_width", frame.cols },
        { "frame_height", frame.rows },
        { "analysis_succeeded", analysis_succeeded },
        { "legacy_filter_hope", legacy_filter_hope >= 0 ? json::value(legacy_filter_hope) : json::value(nullptr) },
        { "legacy_filter_hope_source", "read_current_hope_before_page_scan_frame_not_retained_here" },
        { "hope_balance_verified_on_this_frame", nullptr },
        { "usable_candidate_count", usable.size() },
        { "price_anchor_count", price_evidence.size() },
        { "unpaired_price_evidence_count", unpaired_prices },
        { "usable_candidates_before_consumer_filters", std::move(candidates) },
        { "raw_price_evidence_including_unpaired", std::move(prices) },
        { "visible_set_complete", nullptr },
        { "all_pages_complete", nullptr },
        { "completeness_reason", "usable_OCR_result_and_currency_anchors_do_not_prove_full_card_or_page_coverage" },
        { "executed_choice", nullptr },
        { "valuation_consumer_enabled", false },
        { "priority_adjusted", false },
        { "gameplay_inputs", 0 },
    };
}

json::value make_recruitment_page_observation(
    const cv::Mat& frame,
    const RoguelikeRecruitImageAnalyzer& analyzer,
    const RecruitmentPageObservationContext& context)
{
    using recruitment_page_observation_detail::rect_json;
    auto page =
        make_recruitment_page_observation(frame, analyzer.get_result(), analyzer.get_hope_cost_evidence(), context);
    const auto& observations = analyzer.get_candidate_evidence();
    const auto& prices = analyzer.get_hope_cost_evidence();
    std::vector<json::array> named_links(prices.size()), usable_links(prices.size());
    json::array links;
    for (const auto& observed : observations) {
        const auto exact = analyzer.resolve_candidate_hope_cost(observed);
        const bool operation_known = exact && exact->get().operation_score >= 0.9 &&
                                     (exact->get().operation == "招募" || exact->get().operation == "进阶");
        const bool cost_known = exact && exact->get().hope_cost && *exact->get().hope_cost >= 0;
        links.emplace_back(
            json::object {
                { "analysis_id", observed.analysis_id },
                { "candidate_observation_index", observed.observation_index },
                { "usable_result_index",
                  observed.usable_result_index ? json::value(*observed.usable_result_index) : json::value(nullptr) },
                { "name", observed.name },
                { "name_rect", rect_json(observed.name_rect) },
                { "name_OCR_score", observed.name_score },
                { "currency_flag_rect", rect_json(observed.currency_flag_rect) },
                { "currency_flag_score", observed.currency_flag_score },
                { "price_evidence_index",
                  observed.hope_cost_evidence_index ? json::value(*observed.hope_cost_evidence_index)
                                                    : json::value(nullptr) },
                { "exact_same_analysis_price_link", exact.has_value() },
                { "price_link_reason", observed.price_link_reason },
                { "raw_elite", observed.elite },
                { "raw_level", observed.level },
                { "card_operation", operation_known ? json::value(exact->get().operation) : json::value(nullptr) },
                { "hope_cost", cost_known ? json::value(*exact->get().hope_cost) : json::value(nullptr) },
                { "temporary", observed.temporary ? json::value(*observed.temporary) : json::value(nullptr) },
                { "temporary_reason", observed.temporary_reason },
                { "temporary_search_roi", rect_json(observed.temporary_search_roi) },
                { "temporary_badge_rect",
                  observed.temporary_badge_rect ? json::value(rect_json(*observed.temporary_badge_rect))
                                                : json::value(nullptr) },
                { "temporary_badge_score",
                  observed.temporary_badge_score ? json::value(*observed.temporary_badge_score)
                                                 : json::value(nullptr) },
                { "negative_temporary_rule_source", nullptr },
                { "usability_reason", observed.usability_reason },
            });
        if (exact) {
            named_links[*observed.hope_cost_evidence_index].emplace_back(observed.observation_index);
            if (observed.usable_result_index) {
                usable_links[*observed.hope_cost_evidence_index].emplace_back(*observed.usable_result_index);
            }
        }
        auto& usable = page["usable_candidates_before_consumer_filters"].as_array();
        if (observed.usable_result_index && *observed.usable_result_index < usable.size()) {
            auto& row = usable[*observed.usable_result_index];
            row["analysis_id"] = observed.analysis_id;
            row["candidate_evidence_index"] = observed.observation_index;
            row["price_evidence_index"] = observed.hope_cost_evidence_index
                                              ? json::value(*observed.hope_cost_evidence_index)
                                              : json::value(nullptr);
            row["currency_flag_rect"] = rect_json(observed.currency_flag_rect);
            row["currency_flag_score"] = observed.currency_flag_score;
            row["name_OCR_score"] = observed.name_score;
            row["exact_same_analysis_price_link"] = exact.has_value();
            row["card_operation"] = operation_known ? json::value(exact->get().operation) : json::value(nullptr);
            row["card_operation_source"] = "same_analysis_exact_currency_flag_and_price_evidence_index";
            row["temporary"] = observed.temporary ? json::value(*observed.temporary) : json::value(nullptr);
            row["temporary_source"] = observed.temporary_reason;
            row["temporary_search_roi"] = rect_json(observed.temporary_search_roi);
            row["temporary_badge_rect"] = observed.temporary_badge_rect
                                              ? json::value(rect_json(*observed.temporary_badge_rect))
                                              : json::value(nullptr);
            row["temporary_badge_score"] =
                observed.temporary_badge_score ? json::value(*observed.temporary_badge_score) : json::value(nullptr);
            row["negative_temporary_rule_source"] = nullptr;
        }
    }
    auto& raw_prices = page["raw_price_evidence_including_unpaired"].as_array();
    std::size_t no_named_link = 0, no_usable_link = 0;
    for (std::size_t index = 0; index < raw_prices.size(); ++index) {
        auto& row = raw_prices[index];
        row["linked_candidate_observation_indexes"] = named_links[index];
        row["linked_usable_result_indexes"] = usable_links[index];
        row["exact_usable_pairing_verified"] = usable_links[index].size() == 1;
        row["unpaired_with_usable_result"] = usable_links[index].empty();
        row["pairing_limit"] = "exact_same_analysis_currency_flag_and_index_or_unpaired_no_name_fallback";
        no_named_link += named_links[index].empty() ? 1 : 0;
        no_usable_link += usable_links[index].empty() ? 1 : 0;
    }
    page["analysis_id"] = analyzer.get_analysis_id();
    page["candidate_evidence_count"] = observations.size();
    page["candidate_price_links"] = std::move(links);
    page["price_evidence_without_exact_named_link_count"] = no_named_link;
    page["unpaired_price_evidence_count"] = no_usable_link;
    page["unpaired_price_evidence_count_source"] = "same_analysis_exact_usable_links";
    page["same_analysis_sidecar_used"] = true;
    return page;
}

}
