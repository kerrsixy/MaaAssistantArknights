#include "BlackFlowCurrentModuleAnalyzer.h"
#include "Vision/OCRer.h"
#include "Vision/RegionOCRer.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowDetailStrengthAnalyzer.h"

std::optional<asst::blackflow::AccountModuleDefaultEvidence>
    asst::blackflow::BlackFlowCurrentModuleAnalyzer::analyze(const AccountModuleScanTarget& target) const
{
    if (target.owner_module_codes.empty() ||
        !BlackFlowDetailStrengthAnalyzer(m_image).analyze(target.operator_id, target.operator_name)) {
        return std::nullopt;
    }
    OCRer code_reader(m_image), stage_reader(m_image);
    code_reader.set_task_info("BlackFlowAccountScan@CurrentModuleCode");
    stage_reader.set_task_info("BlackFlowAccountScan@CurrentModuleStage");
    auto codes = code_reader.analyze();
    const auto stages = stage_reader.analyze();
    if (!codes || codes->size() != 1 || codes->front().score < 0.95) {
        // Low-contrast code glyphs need a foreground crop. Keep the same
        // confidence and unique owner-code checks for this bounded fallback.
        RegionOCRer contrast_reader(m_image);
        contrast_reader.set_task_info("BlackFlowAccountScan@CurrentModuleCodeContrast");
        if (const auto contrast = contrast_reader.analyze()) {
            codes = OCRer::ResultsVec { *contrast };
        }
    }
    if (!codes || !stages || codes->size() != 1 || stages->size() != 1 || codes->front().score < 0.95 ||
        stages->front().score < 0.95) {
        return std::nullopt;
    }
    const auto code = normalize_account_module_code(codes->front().text);
    const AccountModuleScanTarget::OwnerModuleCode* found = nullptr;
    for (const auto& candidate : target.owner_module_codes) {
        if (normalize_account_module_code(candidate.code) == code) {
            if (found) {
                return std::nullopt;
            }
            found = &candidate;
        }
    }
    if (!found) {
        return std::nullopt;
    }
    auto stage = normalize_account_module_code(stages->front().text);
    if (!stage.starts_with("STAGE")) {
        return std::nullopt;
    }
    stage.erase(0, 5);
    if (stage.size() != 1 || stage.front() < '1' || stage.front() > '3') {
        return std::nullopt;
    }
    return AccountModuleDefaultEvidence { target.operator_id, found->module_id, code, stage.front() - '0', true };
}
