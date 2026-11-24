#include "BlackFlowDetailStrengthAnalyzer.h"

#include <algorithm>
#include <charconv>

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/TaskData.h"
#include "Vision/Matcher.h"
#include "Vision/OCRer.h"
#include "Vision/Oper/OperFilesImageAnalyzer.h"

namespace asst::blackflow
{
std::vector<std::string>
    BlackFlowDetailStrengthAnalyzer::expected_labels(const std::string& operator_id, const std::string& operator_name)
{
    const auto canonical = BattleData.find_oper_by_id(operator_id);
    if (!operator_id.starts_with("char_") || !canonical || canonical->name != operator_name ||
        canonical->role == battle::Role::Drone) {
        return {};
    }
    std::vector<std::string> labels { operator_name };
    if (!canonical->name_en.empty() && canonical->name_en != operator_name) {
        labels.push_back(canonical->name_en);
    }
    return labels;
}

std::optional<DetailStrengthObservation>
    BlackFlowDetailStrengthAnalyzer::analyze(const std::string& operator_id, const std::string& operator_name) const
{
    const auto labels = expected_labels(operator_id, operator_name);
    if (m_image.empty() || m_image.cols != 1280 || m_image.rows != 720 || m_image.type() != CV_8UC3 || labels.empty()) {
        return std::nullopt;
    }
    const auto detail_task = Task.get<MatchTaskInfo>("BlackFlowAccount@StrengthDetail");
    const auto name_task = Task.get<OcrTaskInfo>("BlackFlowAccount@StrengthName");
    const auto inside = [&](const Rect& roi) {
        return roi.x >= 0 && roi.y >= 0 && roi.width > 0 && roi.height > 0 && roi.x + roi.width <= m_image.cols &&
               roi.y + roi.height <= m_image.rows;
    };
    if (!detail_task || !name_task || !inside(detail_task->roi) || !inside(name_task->roi)) {
        return std::nullopt;
    }
    Matcher page(m_image);
    page.set_task_info(detail_task);
    if (!page.analyze()) {
        return std::nullopt;
    }
    const auto read_identity = [&](const std::shared_ptr<OcrTaskInfo>& task) -> std::optional<OCRer::Result> {
        if (!task || !inside(task->roi)) {
            return std::nullopt;
        }
        OCRer name(m_image);
        name.set_task_info(task);
        name.set_required(labels);
        std::optional<OCRer::Result> result;
        if (const auto names = name.analyze()) {
            for (const auto& match : *names) {
                if (std::ranges::find(labels, match.text) != labels.end() && match.score >= 0.95 &&
                    (!result || match.score > result->score)) {
                    result = match;
                }
            }
        }
        return result;
    };
    auto identity = read_identity(name_task);
    if (!identity) {
        // Short titles can be missed when the narrow crop changes OCR detection
        // scale. Reuse the detail gate's broader region on this same frame,
        // keeping exact canonical labels and the original confidence floor.
        identity = read_identity(Task.get<OcrTaskInfo>("BlackFlowAccountScan@DetailName"));
    }
    if (!identity) {
        return std::nullopt;
    }
    const auto elite = OperFilesImageAnalyzer(m_image).elite_level();
    if (!elite || *elite < 0 || *elite > 2) {
        return std::nullopt;
    }
    const auto read_number = [&](const std::string& task) -> std::optional<std::pair<int, Rect>> {
        const auto definition = Task.get<OcrTaskInfo>(task);
        if (!definition || !inside(definition->roi)) {
            return std::nullopt;
        }
        OCRer number(m_image);
        number.set_task_info(definition);
        const auto values = number.analyze();
        if (!values || values->size() != 1 || values->front().score < 0.95) {
            return std::nullopt;
        }
        const auto& value = values->front();
        if (value.text.empty() || value.text.size() > 2) {
            return std::nullopt;
        }
        int parsed = 0;
        const auto conversion = std::from_chars(value.text.data(), value.text.data() + value.text.size(), parsed);
        if (conversion.ec != std::errc {} || conversion.ptr != value.text.data() + value.text.size()) {
            return std::nullopt;
        }
        return std::pair { parsed, value.rect };
    };
    const auto level = read_number("BlackFlowAccount@StrengthLevel");
    const auto maximum = read_number("BlackFlowAccount@StrengthMaximum");
    if (!level || !maximum || level->first < 1 || maximum->first < level->first || maximum->first > 90) {
        return std::nullopt;
    }
    return DetailStrengthObservation { .operator_id = operator_id,
                                       .operator_name = operator_name,
                                       .recognized_name = identity->text,
                                       .elite = *elite,
                                       .level = level->first,
                                       .maximum_level = maximum->first,
                                       .name_rect = identity->rect,
                                       .level_rect = level->second,
                                       .maximum_level_rect = maximum->second };
}
}
