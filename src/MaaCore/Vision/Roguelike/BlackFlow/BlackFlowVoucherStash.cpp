#include "BlackFlowVoucherStash.h"

#include <regex>

#include "Utils/Logger.hpp"
#include "Utils/StringMisc.hpp"
#include "Vision/Matcher.h"
#include "Vision/Miscellaneous/PipelineAnalyzer.h"
#include "Vision/RegionOCRer.h"

bool asst::blackflow::VoucherStashObservation::can_store() const noexcept
{
    return recruitment_page && enabled_button && count && count->used >= 0 && count->capacity > 0 &&
           count->used < count->capacity;
}

std::optional<asst::blackflow::VoucherStashCount>
    asst::blackflow::BlackFlowVoucherStash::parse_count(std::string_view text)
{
    static const std::regex pattern(R"((?:^|[^0-9])([0-9]{1,2})\s*/\s*([0-9]{1,2})(?:$|[^0-9]))");
    const std::string value(text);
    std::smatch match;
    if (!std::regex_search(value, match, pattern)) {
        return std::nullopt;
    }
    int used = 0;
    int capacity = 0;
    if (!utils::chars_to_number(match[1].str(), used) || !utils::chars_to_number(match[2].str(), capacity) ||
        capacity < 1 || used > capacity) {
        return std::nullopt;
    }
    return VoucherStashCount { used, capacity };
}

asst::blackflow::VoucherStashObservation asst::blackflow::BlackFlowVoucherStash::observe() const
{
    VoucherStashObservation observation;
    if (m_image.empty()) {
        return observation;
    }

    PipelineAnalyzer recruitment(m_image);
    recruitment.set_tasks({ "BlackFlow@Roguelike@VoucherStashRecruitmentPage" });
    observation.recruitment_page = recruitment.analyze().has_value();

    Matcher button(m_image);
    button.set_task_info("BlackFlow@Roguelike@ReserveRecruitmentVoucher");
    if (const auto matched = button.analyze()) {
        observation.enabled_button = true;
        observation.button = matched->rect;
    }

    RegionOCRer counter(m_image);
    counter.set_task_info("BlackFlow@Roguelike@VoucherStashCount");
    if (const auto result = counter.analyze(); result && result->score >= 0.85) {
        observation.count = parse_count(result->text);
        LogDebug << __FUNCTION__ << "Voucher stash counter" << result->text << "recognized"
                 << observation.count.has_value();
    }

    // A missing button alone is animation/occlusion evidence, not proof of storage.
    if (!observation.recruitment_page && !observation.enabled_button && !observation.count) {
        PipelineAnalyzer successor(m_image);
        successor.set_tasks(
            { "BlackFlow@Roguelike@GetDrop",
              "BlackFlow@Roguelike@GetDropCompleted",
              "BlackFlow@Roguelike@RecruitMain",
              "BlackFlow@Roguelike@NextLevel" });
        if (const auto result = successor.analyze()) {
            observation.successor_page = result->task_ptr->name;
        }
    }
    return observation;
}

asst::blackflow::VoucherStashProof asst::blackflow::BlackFlowVoucherStash::verify(
    const VoucherStashObservation& before,
    const VoucherStashObservation& after) noexcept
{
    if (!before.can_store()) {
        return VoucherStashProof::Pending;
    }
    if ((after.recruitment_page || after.enabled_button) && after.count &&
        after.count->capacity == before.count->capacity && after.count->used == before.count->used + 1) {
        return VoucherStashProof::CounterIncreased;
    }
    if (!after.recruitment_page && !after.enabled_button && !after.count && !after.successor_page.empty()) {
        return VoucherStashProof::SuccessorPage;
    }
    return VoucherStashProof::Pending;
}

bool asst::blackflow::VoucherStashAttempt::begin(const VoucherStashObservation& observation)
{
    if (m_before || !observation.can_store()) {
        return false;
    }
    m_before = observation;
    return true;
}
