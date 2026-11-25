#include "RoguelikePreparationTask.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <unordered_set>

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

namespace asst::blackflow::prepared_primary_detail
{
std::optional<std::reference_wrapper<const ConditionalPrimaryModeEstimate>>
    initial_mode(const ConditionalPrimaryEstimate& e)
{
    if (!e.qualified || e.modes.empty() || (e.modes[0].key != "normal" && e.modes[0].key != "ammo_main")) {
        return std::nullopt;
    }
    return std::cref(e.modes[0]);
}

bool rate_better(const ConditionalPrimaryEstimate& a, const ConditionalPrimaryEstimate& b)
{
    const auto am = initial_mode(a), bm = initial_mode(b);
    if (!am || !bm || am->get().variants.empty() || bm->get().variants.empty()) {
        return false;
    }
    bool strict = false;
    for (const auto& av : am->get().variants) {
        for (const auto& bv : bm->get().variants) {
            std::vector<double> points { 0 };
            double maximum = 0;
            for (const auto& sequence : { av.lower, bv.upper }) {
                for (const auto& hit : sequence.hits) {
                    if (!std::isfinite(hit.damage) || hit.damage <= 0) {
                        return false;
                    }
                    points.push_back(0.95 * hit.damage);
                    maximum = std::max(maximum, hit.damage);
                }
            }
            points.push_back(maximum + 1);
            for (double defense : points) {
                const auto ar = physical_sequence_rate(av.lower, defense);
                const auto br = physical_sequence_rate(bv.upper, defense);
                if (!ar || !br || *ar + 1e-9 < *br) {
                    return false;
                }
                strict = strict || *ar > *br + 1e-9;
            }
        }
    }
    return strict;
}
}

namespace asst::blackflow
{
PreparedComponentBatch build_prepared_primary_components(
    const BlackFlowRecruitmentDutyCatalog& catalog,
    const RoguelikeStatus& status,
    const std::string& client,
    const std::string& uid,
    const std::vector<std::string>& selected_names)
{
    PreparedComponentBatch batch { .client = client, .uid = uid, .run_id = status.run_id };
    std::unordered_set<std::string> unique;
    for (const auto& name : selected_names) {
        const auto current = status.opers.find(name);
        if (name.empty() || !unique.emplace(name).second || current == status.opers.end()) {
            continue;
        }
        PreparedPrimaryEntry entry { .name = name, .observed = current->second };
        const auto& p = entry.observed.preparation;
        if (client.empty() || uid.empty() || status.run_id.empty() || p.operator_id.empty() ||
            p.operator_id != RoguelikeTraining.unique_identity(name) || p.training_sequence == 0 ||
            p.selection_sequence < p.training_sequence || p.selected_skill < 1 || p.selected_skill > 3) {
            entry.component.reason = "verified_preparation_identity_phase_or_selection_unavailable";
        }
        else {
            entry.component = catalog.conditional_primary(
                name,
                entry.observed.elite,
                entry.observed.level,
                p.selected_skill,
                p.displayed_skill_levels[p.selected_skill - 1],
                {},
                PrimarySequenceConditions { .target = ConditionalPrimaryTarget::Ground,
                                            .source = "preparation_ground_reference_not_live_enemy" },
                "verified_preparation:" + status.run_id + ":" + std::to_string(p.training_sequence) + ":" +
                    std::to_string(p.selection_sequence));
        }
        batch.entries.push_back(std::move(entry));
    }
    return batch;
}

bool prepared_primary_matches(
    const PreparedComponentBatch& batch,
    const RoguelikeStatus& status,
    const std::string& client,
    const std::string& uid)
{
    if (client.empty() || uid.empty() || status.run_id.empty() || batch.client != client || batch.uid != uid ||
        batch.run_id != status.run_id || batch.entries.empty()) {
        return false;
    }
    for (const auto& entry : batch.entries) {
        const auto current = status.opers.find(entry.name);
        if (current == status.opers.end()) {
            return false;
        }
        const auto& old = entry.observed;
        const auto& now = current->second;
        if (old.elite != now.elite || old.level != now.level ||
            old.preparation.operator_id != now.preparation.operator_id ||
            old.preparation.selected_skill != now.preparation.selected_skill ||
            old.preparation.displayed_skill_levels != now.preparation.displayed_skill_levels ||
            old.preparation.training_sequence != now.preparation.training_sequence ||
            old.preparation.selection_sequence != now.preparation.selection_sequence) {
            return false;
        }
    }
    return true;
}

PreparedPrimaryChoice choose_prepared_primary_reference(
    const PreparedComponentBatch& batch,
    const std::vector<std::size_t>& candidates,
    std::size_t legacy)
{
    PreparedPrimaryChoice result { .reason = "initial_ground_reference_incomparable_keep_legacy" };
    if (legacy >= batch.entries.size() || std::ranges::find(candidates, legacy) == candidates.end()) {
        result.reason = "legacy_not_in_qualified_comparison";
        return result;
    }
    std::vector<std::size_t> frontier;
    for (const auto i : candidates) {
        if (i >= batch.entries.size() || i == legacy ||
            !prepared_primary_detail::rate_better(batch.entries[i].component, batch.entries[legacy].component)) {
            continue;
        }
        if (std::ranges::none_of(candidates, [&](const auto j) {
                return j < batch.entries.size() && j != i &&
                       prepared_primary_detail::rate_better(batch.entries[j].component, batch.entries[i].component);
            })) {
            frontier.push_back(i);
        }
    }
    if (frontier.size() == 1) {
        result.preferred = frontier[0];
        result.reason = "initial_acquired_ground_main_rate_reference_not_total_DPS_or_survival";
    }
    else if (frontier.size() > 1) {
        result.reason = "multiple_incomparable_reference_preferences_keep_legacy";
    }
    return result;
}

json::value prepared_primary_to_json(const PreparedComponentBatch& batch)
{
    json::array entries;
    for (const auto& entry : batch.entries) {
        const auto& p = entry.observed.preparation;
        entries.emplace_back(
            json::object { { "name", entry.name },
                           { "operator_id", p.operator_id },
                           { "elite", entry.observed.elite },
                           { "level", entry.observed.level },
                           { "selected_skill", p.selected_skill },
                           { "training_sequence", p.training_sequence },
                           { "selection_sequence", p.selection_sequence },
                           { "displayed_levels", p.displayed_skill_levels },
                           { "component", conditional_primary_to_json(entry.component) } });
    }
    return json::object { { "client", batch.client },
                          { "uid", batch.uid },
                          { "run_id", batch.run_id },
                          { "entries", entries },
                          { "account_mastery_substituted", false },
                          { "training_read_in_battle", false },
                          { "unknown_external_assumed_zero", false },
                          { "objective", "first_skill_mode_acquired_ground_main_rate_reference" } };
}
}
