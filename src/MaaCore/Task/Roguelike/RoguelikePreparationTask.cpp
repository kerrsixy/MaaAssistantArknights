#include "RoguelikePreparationTask.h"

#include <filesystem>

#include "MaaUtils/ImageIo.h"
#include "MaaUtils/Time.hpp"
#include "RoguelikeOperEvidence.h"
#include "Utils/Logger.hpp"
#include "Utils/WorkingDir.hpp"

json::value asst::commit_preparation_observations(
    RoguelikeStatus& status,
    const cv::Mat& frame,
    const std::vector<PreparationOperatorObservation>& observations,
    PreparationPage page)
{
    const auto sequence = ++status.decision_seq;
    json::array records;
    for (const auto& observed : observations) {
        if (observed.name.empty() ||
            (!RoguelikeTraining.contains_name(observed.name) && !status.opers.contains(observed.name))) {
            continue;
        }
        if (!observed.anchor_verified && !status.opers.contains(observed.name)) {
            continue;
        }
        auto record = observed.to_json();
        const auto identity = RoguelikeTraining.unique_identity(observed.name);
        if (identity.empty() || (!observed.operator_id.empty() && observed.operator_id != identity)) {
            record["applied"] = false;
            record["reason"] = "operator_identity_unresolved";
            records.push_back(std::move(record));
            continue;
        }
        const auto existing = status.opers.find(observed.name);
        const auto selection_conflicts = [&](int elite, int level) {
            if (observed.selected_skill <= 0) {
                return false;
            }
            const auto available =
                RoguelikeTraining.skill_availability(observed.name, observed.selected_skill, elite, level);
            return available == SkillAvailability::Locked || available == SkillAvailability::Absent;
        };
        if (!observed.training_observed) {
            if (existing == status.opers.end() || existing->second.preparation.operator_id != identity) {
                record["applied"] = false;
                record["reason"] = "training_snapshot_required";
            }
            else {
                auto& prepared = existing->second.preparation;
                const bool conflict = selection_conflicts(existing->second.elite, existing->second.level);
                record["phase_vs_selected_skill_conflict"] = conflict;
                prepared.selected_skill = conflict ? -1 : observed.selected_skill;
                prepared.selection_sequence = static_cast<std::uint64_t>(sequence);
                record["applied"] = true;
                record["training_source_sequence"] = prepared.training_sequence;
                record["selection_source_sequence"] = prepared.selection_sequence;
            }
            records.push_back(std::move(record));
            continue;
        }
        const int merged_elite = observed.elite >= 0              ? observed.elite
                                 : existing != status.opers.end() ? existing->second.elite
                                                                  : -1;
        const auto cap = RoguelikeTraining.maximum_level(observed.name, merged_elite);
        const int level = cap && observed.level > 0 && observed.level <= *cap ? observed.level : -1;
        record["level_compatible_with_current_phase"] = level > 0;
        if (!merge_current_run_operator(status.opers, observed.name, observed.elite, level)) {
            continue;
        }
        auto& oper = status.opers.at(observed.name);
        const bool conflict = selection_conflicts(oper.elite, oper.level);
        record["phase_vs_selected_skill_conflict"] = conflict;
        // A new preparation view invalidates the previous selection/training
        // observation even when some of the new fields cannot be read.
        oper.preparation = {
            .operator_id = identity,
            .displayed_skill_levels = observed.displayed_skill_levels,
            .selected_skill = conflict ? -1 : observed.selected_skill,
            .training_sequence = static_cast<std::uint64_t>(sequence),
            .selection_sequence = page == PreparationPage::Skills ? static_cast<std::uint64_t>(sequence) : 0,
        };
        record["applied"] = true;
        record["current_run_elite"] = oper.elite;
        record["current_run_level"] = oper.level;
        record["training_source_sequence"] = oper.preparation.training_sequence;
        record["selection_source_sequence"] = oper.preparation.selection_sequence;
        records.push_back(std::move(record));
    }
    const auto directory = UserDir.get() / utils::path("debug/blackflow/preparation");
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const auto filename = MAA_NS::format_now_for_filename() + "_preparation_" + std::to_string(sequence) + ".png";
    const auto path = directory / utils::path(filename);
    const bool saved = !error && !frame.empty() && MAA_NS::imwrite(path, frame);
    if (!saved) {
        LogWarn << __FUNCTION__ << "Preparation frame evidence could not be persisted" << path;
    }
    return json::object {
        { "run_id", status.run_id },
        { "observation_sequence", sequence },
        { "page", page == PreparationPage::Skills ? "skill_selection" : "formation" },
        { "operators", std::move(records) },
        { "frame_saved", saved },
        { "frame_path", saved ? utils::path_to_utf8_string(path) : std::string {} },
        { "account_strength_used", false },
        { "gameplay_inputs", 0 },
    };
}

int asst::choose_preparation_skill(
    int preferred,
    int alternate,
    const PreparationOperatorObservation& observation,
    const RoguelikeOper* current_run)
{
    const auto selectable = [&](int skill) {
        if (skill <= 0 || skill > static_cast<int>(observation.availability.size())) {
            return false;
        }
        auto availability = observation.availability[skill - 1];
        if (availability == SkillAvailability::Unknown && current_run &&
            !current_run->preparation.operator_id.empty() &&
            current_run->preparation.operator_id == RoguelikeTraining.unique_identity(observation.name)) {
            availability =
                RoguelikeTraining.skill_availability(observation.name, skill, current_run->elite, current_run->level);
        }
        return availability != SkillAvailability::Locked && availability != SkillAvailability::Absent;
    };
    if (selectable(preferred)) {
        return preferred;
    }
    if (selectable(alternate)) {
        return alternate;
    }
    return 0;
}
