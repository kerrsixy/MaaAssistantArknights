#include "BlackFlowAccountTrainingAnalyzer.h"

#include <charconv>

#include "Config/TaskData.h"
#include "Vision/OCRer.h"
#include "Vision/Oper/OperFilesImageAnalyzer.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowDetailStrengthAnalyzer.h"

namespace asst::blackflow
{
std::optional<AccountTrainingEvidence>
    BlackFlowAccountTrainingAnalyzer::analyze(const std::string& operator_id, const std::string& operator_name) const
{
    const auto strength = BlackFlowDetailStrengthAnalyzer(m_image).analyze(operator_id, operator_name);
    if (!strength) {
        return std::nullopt;
    }
    AccountTrainingEvidence observation { .operator_id = operator_id,
                                          .operator_name = operator_name,
                                          .elite = strength->elite,
                                          .level = strength->level };
    const auto definition = Task.get<OcrTaskInfo>("OperProgress@CurrentSkillLevel");
    const auto cap = RoguelikeTraining.maximum_base_skill(operator_name);
    if (definition && cap && definition->roi.x >= 0 && definition->roi.y >= 0 && definition->roi.width > 0 &&
        definition->roi.height > 0 && definition->roi.x + definition->roi.width <= m_image.cols &&
        definition->roi.y + definition->roi.height <= m_image.rows) {
        OCRer rank(m_image);
        rank.set_task_info(definition);
        const auto values = rank.analyze();
        if (values && values->size() == 1 && values->front().score >= 0.95) {
            const auto& text = values->front().text;
            int parsed = -1;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
            if (result.ec == std::errc {} && result.ptr == text.data() + text.size() && parsed > 0 && parsed <= *cap &&
                (observation.elite > 0 || parsed <= 4)) {
                observation.base_skill_level = parsed;
            }
        }
    }
    OperFilesImageAnalyzer detail(m_image);
    for (int skill = 1; skill <= 3; ++skill) {
        auto& available = observation.availability[skill - 1];
        available = RoguelikeTraining.skill_availability(operator_name, skill, observation.elite, observation.level);
        if (available != SkillAvailability::Available) {
            continue;
        }
        if (observation.elite < 2 || (observation.base_skill_level > 0 && observation.base_skill_level < 7)) {
            // Verified prerequisites rule out mastery; this is not a guessed dark icon.
            observation.mastery_levels[skill - 1] = 0;
        }
        else if (observation.base_skill_level == 7) {
            const auto mastery = detail.mastery_level(skill);
            if (mastery && *mastery >= 0 && *mastery <= 3) {
                observation.mastery_levels[skill - 1] = *mastery;
            }
        }
    }
    return observation;
}
}
