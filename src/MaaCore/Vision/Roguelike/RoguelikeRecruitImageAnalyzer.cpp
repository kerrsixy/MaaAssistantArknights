#include "RoguelikeRecruitImageAnalyzer.h"
#include "RoguelikeRecruitEvidence.h"

#include <algorithm>
#include <atomic>

#include "Config/TaskData.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Utils/Logger.hpp"
#include "Vision/Matcher.h"
#include "Vision/MultiMatcher.h"
#include "Vision/RegionOCRer.h"
#include "Vision/TemplDetOCRer.h"

bool asst::RoguelikeRecruitImageAnalyzer::analyze()
{
    LogTraceFunction;
    m_result.clear();
    m_hope_cost_evidence.clear();
    m_candidate_evidence.clear();
    // Evidence identity only: never used to change recruitment decisions.
    static std::atomic<std::uint64_t> analysis_sequence { 0 };
    m_analysis_id = ++analysis_sequence;

    Matcher temporary_matcher(m_image);
    temporary_matcher.set_task_info("Roguelike@TempRecruitFlag");
    const auto temporary_match = temporary_matcher.analyze();
    const bool temporary_badge = temporary_match.has_value();
    const auto list_roi = Task.get("RoguelikeRecruitOcrFlag")->roi;

    // Some disabled cards have name/level text below the name OCR threshold.
    // Their own currency flags still give independent known/unknown price
    // evidence without promoting them into usable recruitment candidates.
    MultiMatcher currency_flags(m_image);
    currency_flags.set_task_info("RoguelikeRecruitOcrFlag");
    if (const auto flags = currency_flags.analyze()) {
        for (const auto& flag : *flags) {
            m_hope_cost_evidence.emplace_back(analyze_hope_cost(m_image, flag.rect, flag.score));
        }
    }

    TemplDetOCRer analyzer(m_image);
    analyzer.set_task_info("RoguelikeRecruitOcrFlag", "RoguelikeRecruitOcr");
    analyzer.set_replace(
        Task.get<OcrTaskInfo>("CharsNameOcrReplace")->replace_map,
        Task.get<OcrTaskInfo>("CharsNameOcrReplace")->replace_full);
    analyzer.set_bin_threshold(Task.get("RoguelikeRecruitOcr")->specific_rect.x);

    auto result_opt = analyzer.analyze();
    if (!result_opt) {
        return false;
    }

    // using blue channel only when matching level to filter out the yellow ring
    cv::Mat bbb_image;
    {
        cv::Mat blue;
        cv::extractChannel(m_image, blue, 0);
        cv::merge(std::array { blue, blue, blue }, bbb_image);
    }

    sort_by_vertical_(*result_opt);
    for (const auto& res : *result_opt) {
        int elite = match_elite(res.rect);
        int level = match_level(bbb_image, res.rect);

        const auto found = std::ranges::find_if(m_hope_cost_evidence, [&](const auto& evidence) {
            return evidence.currency_rect == res.flag_rect;
        });
        const std::size_t evidence_index = static_cast<std::size_t>(found - m_hope_cost_evidence.begin());
        if (found == m_hope_cost_evidence.end()) {
            m_hope_cost_evidence.emplace_back(analyze_hope_cost(m_image, res.flag_rect, res.flag_score));
        }
        auto& hope_evidence = m_hope_cost_evidence[evidence_index];
        hope_evidence.name = res.text;
        RoguelikeRecruitCandidateEvidence observed {
            .analysis_id = m_analysis_id,
            .observation_index = m_candidate_evidence.size(),
            .name = res.text,
            .name_rect = res.rect,
            .name_score = res.score,
            .currency_flag_rect = res.flag_rect,
            .currency_flag_score = res.flag_score,
            .hope_cost_evidence_index = evidence_index,
            .elite = elite,
            .level = level,
            .temporary_search_roi = Task.get("Roguelike@TempRecruitFlag")->roi,
        };
        const bool positively_temporary = has_first_slot_temporary_badge(
            temporary_badge,
            res.rect.x,
            res.rect.y,
            list_roi.x,
            list_roi.y,
            list_roi.width,
            list_roi.height);
        if (positively_temporary) {
            observed.temporary = true;
            observed.temporary_badge_rect = temporary_match->rect;
            observed.temporary_badge_score = temporary_match->score;
            observed.temporary_reason = "positive_first_slot_badge_rule";
        }
        else {
            observed.temporary_reason = temporary_match ? "positive_badge_not_attributed_to_this_card"
                                                        : "missing_badge_is_not_negative_identity_proof";
        }

        if (level < 0) {
            // Keep the independently observed price as evidence, while retaining
            // the existing rule that an unverified level is not a usable recruit.
            observed.usability_reason = "level_OCR_unverified_legacy_result_excluded";
            m_candidate_evidence.emplace_back(std::move(observed));
            continue;
        }

        battle::roguelike::Recruitment info;
        info.rect = res.rect;
        info.name = res.text;
        info.elite = elite;
        info.level = level;
        info.hope_cost = hope_evidence.hope_cost;
        info.temporary = has_first_slot_temporary_badge(
            temporary_badge,
            res.rect.x,
            res.rect.y,
            list_roi.x,
            list_roi.y,
            list_roi.width,
            list_roi.height);
        if (info.temporary) {
            info.hope_cost = 0;
            hope_evidence.hope_cost = 0;
            hope_evidence.reason = "first_slot_temporary_badge";
            LogInfo << __FUNCTION__ << "First-slot temporary recruit" << info.name << "elite" << elite;
        }

        Log.info(__FUNCTION__, info.name, elite, level, info.rect);
        observed.usable_result_index = m_result.size();
        observed.legacy_temporary_flag = info.temporary;
        observed.usability_reason = level > 0 ? "included_in_legacy_result" : "included_with_unverified_level_zero";
        m_result.emplace_back(std::move(info));
        m_candidate_evidence.emplace_back(std::move(observed));
    }

    for (auto& observed : m_candidate_evidence) {
        observed.exact_price_link = resolve_candidate_hope_cost(observed).has_value();
        observed.price_link_reason = observed.exact_price_link ? "same_analysis_exact_currency_flag_and_index"
                                                               : "currency_link_ambiguous_stale_or_conflicting";
    }
    return !m_result.empty();
}

std::optional<std::reference_wrapper<const asst::RoguelikeRecruitHopeCostEvidence>>
    asst::RoguelikeRecruitImageAnalyzer::resolve_candidate_hope_cost(
        const RoguelikeRecruitCandidateEvidence& candidate) const noexcept
{
    if (candidate.analysis_id == 0 || candidate.analysis_id != m_analysis_id ||
        candidate.observation_index >= m_candidate_evidence.size() || !candidate.hope_cost_evidence_index ||
        *candidate.hope_cost_evidence_index >= m_hope_cost_evidence.size()) {
        return std::nullopt;
    }
    const auto& canonical = m_candidate_evidence[candidate.observation_index];
    if (candidate.name != canonical.name || candidate.name_rect != canonical.name_rect ||
        candidate.currency_flag_rect != canonical.currency_flag_rect ||
        candidate.hope_cost_evidence_index != canonical.hope_cost_evidence_index ||
        candidate.usable_result_index != canonical.usable_result_index) {
        return std::nullopt;
    }
    const auto& price = m_hope_cost_evidence[*candidate.hope_cost_evidence_index];
    if (price.currency_rect != candidate.currency_flag_rect || price.name != candidate.name ||
        std::ranges::count_if(
            m_hope_cost_evidence,
            [&](const auto& value) { return value.currency_rect == candidate.currency_flag_rect; }) != 1 ||
        std::ranges::count_if(m_candidate_evidence, [&](const auto& value) {
            return value.currency_flag_rect == candidate.currency_flag_rect;
        }) != 1) {
        return std::nullopt;
    }
    return std::cref(price);
}

int asst::RoguelikeRecruitImageAnalyzer::match_elite(const Rect& raw_roi)
{
    LogTraceFunction;

    static const std::unordered_map<std::string, int> EliteTaskName = {
        { "RoguelikeRecruitElite0", 0 },
        { "RoguelikeRecruitElite1", 1 },
        { "RoguelikeRecruitElite2", 2 },
    };

    RecruitEliteEvidence elite_result;

    for (const auto& [task_name, elite] : EliteTaskName) {
        Matcher analyzer(m_image);
        auto task_ptr = Task.get(task_name);
        analyzer.set_task_info(task_ptr);
        analyzer.set_roi(raw_roi.move(task_ptr->rect_move));

        if (!analyzer.analyze()) {
            continue;
        }

        elite_result.consider(elite, analyzer.get_result().score);
    }
    return elite_result.elite();
}

int asst::RoguelikeRecruitImageAnalyzer::match_level(const cv::Mat& image, const Rect& raw_roi)
{
    LogTraceFunction;

    RegionOCRer analyzer(image);
    analyzer.set_task_info("NumberOcrReplace");
    analyzer.set_roi(raw_roi.move(Task.get("RoguelikeRecruitLevel")->rect_move));
    analyzer.set_bin_expansion(1);

    if (!analyzer.analyze()) {
        return -1;
    }

    const std::string& level = analyzer.get_result().text;
    if (level.empty() || !std::ranges::all_of(level, [](char c) -> bool { return std::isdigit(c); })) {
        return 0;
    }
    return std::stoi(level);
}

asst::RoguelikeRecruitHopeCostEvidence asst::RoguelikeRecruitImageAnalyzer::analyze_hope_cost(
    const cv::Mat& image,
    const Rect& currency_flag,
    double flag_score)
{
    RoguelikeRecruitHopeCostEvidence evidence;
    evidence.currency_rect = currency_flag;
    evidence.reason = "unverified_currency_anchor";
    if (image.empty() || image.type() != CV_8UC3 || !std::isfinite(flag_score) || flag_score < 0.9 ||
        currency_flag.width != 36 || currency_flag.height != 16) {
        return evidence;
    }

    // The fixed hope flag is a stable anchor; the name OCR rectangle is tightly
    // trimmed and therefore cannot safely locate these neighbouring fields.
    evidence.operation_roi = currency_flag.move({ 15, -25, 58, 23 });
    evidence.price_roi = currency_flag.move({ 38, -3, 60, 23 });
    const cv::Rect bounds(0, 0, image.cols, image.rows);
    const auto inside = [&](const Rect& roi) {
        const auto rect = make_rect<cv::Rect>(roi);
        return !roi.empty() && (rect & bounds) == rect;
    };
    if (!inside(currency_flag) || !inside(evidence.operation_roi) || !inside(evidence.price_roi) ||
        evidence.price_roi.y + evidence.price_roi.height > 630) {
        evidence.reason = "clipped_card_price_field";
        return evidence;
    }

    cv::Mat price_gray;
    cv::cvtColor(image(make_rect<cv::Rect>(evidence.price_roi)), price_gray, cv::COLOR_BGR2GRAY);
    double minimum = 0.0;
    double maximum = 0.0;
    cv::minMaxLoc(price_gray, &minimum, &maximum);
    if (maximum - minimum < 12.0) {
        evidence.reason = "no_price_ink";
        return evidence;
    }
    cv::Mat price_ink;
    const double price_threshold = cv::threshold(price_gray, price_ink, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    const auto ink = cv::boundingRect(price_ink);
    if (ink.empty() || ink.x < 2 || ink.y < 1 || ink.br().x > price_ink.cols - 2 || ink.br().y > price_ink.rows - 1 ||
        ink.width > 45 || ink.height < 7 || ink.height > 20) {
        evidence.reason = "clipped_or_ambiguous_price_ink";
        return evidence;
    }
    evidence.digits_rect = { evidence.price_roi.x + ink.x, evidence.price_roi.y + ink.y, ink.width, ink.height };

    cv::Mat operation_gray;
    cv::cvtColor(image(make_rect<cv::Rect>(evidence.operation_roi)), operation_gray, cv::COLOR_BGR2GRAY);
    cv::Mat operation_ink;
    const double operation_threshold =
        cv::threshold(operation_gray, operation_ink, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    RegionOCRer operation(image, evidence.operation_roi);
    operation.set_bin_threshold(static_cast<int>(operation_threshold) + 1);
    operation.set_bin_expansion(1);
    if (const auto result = operation.analyze()) {
        evidence.operation = result->text;
        evidence.operation_score = result->score;
    }
    if ((evidence.operation != "招募" && evidence.operation != "进阶") || evidence.operation_score < 0.9) {
        evidence.reason = "unverified_card_operation";
        return evidence;
    }

    RegionOCRer price(image, evidence.price_roi);
    price.set_use_char_model(true);
    price.set_bin_threshold(static_cast<int>(price_threshold) + 1);
    price.set_bin_expansion(1);
    // No NumberOcrReplace: a clipped glyph such as I must never turn into 1.
    if (const auto result = price.analyze()) {
        evidence.raw_digits = result->text;
        evidence.digits_score = result->score;
    }
    if (evidence.raw_digits.empty() || evidence.raw_digits.size() > 2 || evidence.digits_score < 0.92 ||
        !std::ranges::all_of(evidence.raw_digits, [](unsigned char value) { return value >= '0' && value <= '9'; })) {
        evidence.reason = "unverified_price_digits";
        return evidence;
    }
    evidence.hope_cost = std::stoi(evidence.raw_digits);
    evidence.reason = "card_operation_and_price_digits";
    LogTrace << __FUNCTION__ << evidence.operation << "hope cost" << *evidence.hope_cost << evidence.price_roi
             << "score" << evidence.digits_score;
    return evidence;
}
