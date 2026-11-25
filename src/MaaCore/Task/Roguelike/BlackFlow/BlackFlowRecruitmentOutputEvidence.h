#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "BlackFlowRecruitmentDuty.h"

namespace asst::blackflow
{
// Optional producer contract, not proof inferred from an empty collections list.
// Bounds must enclose every covered operator's actual non-intrinsic ATK/ASPD
// modifiers, including squad/collectibles/modules/other conditional effects.
struct RecruitmentModifierEvidence
{
    std::string client;
    std::string uid;
    std::string run_id;
    std::string source;
    std::string observed_at;
    bool complete_for_covered_operators = false;
    // ATK/ASPD alone cannot close initial SP, recovery, scale, hits or range.
    bool primary_components_covered = false;
    std::vector<std::string> unclosed_dimensions;
    std::unordered_set<std::string> covered_operator_ids;
    std::array<double, 2> atk_fraction { 0, 0 };
    std::array<double, 2> aspd { 0, 0 };
};

// Both the first offer analysis and final same-page re-find use this adapter.
// Profile attributes retain their own source stamp. Potential is not inherited
// from the legacy roster number: it lacks field-level fresh detail proof.
[[nodiscard]] inline RecruitmentOutputEvidence recruitment_profile_output_evidence(
    const BlackFlowAccountProfile* profile,
    const std::string& client,
    const std::string& uid,
    const std::string& run_id,
    const std::vector<std::string>& candidate_and_roster_names,
    const std::optional<RecruitmentModifierEvidence>& modifiers = std::nullopt)
{
    RecruitmentOutputEvidence result { .client = client,
                                       .uid = uid,
                                       .run_id = run_id,
                                       .source =
                                           "uid_profile_attributes;external_modifiers_unknown_no_complete_ledger" };
    if (!profile || profile->client() != client || profile->uid() != uid || uid.empty() || run_id.empty()) {
        result.source.clear();
        return result;
    }
    std::unordered_set<std::string> required_ids;
    bool identities_known = !candidate_and_roster_names.empty();
    for (const auto& name : candidate_and_roster_names) {
        const auto oper = profile->find_operator(name);
        if (!oper) {
            identities_known = false;
            continue;
        }
        const auto& row = oper->get();
        required_ids.emplace(row.id);
        ProjectedOutputAttributes attributes;
        attributes.source = "unknown_trust:" + row.trust_quality;
        if (row.trust) {
            attributes.trust_percent = row.trust->percentage;
            attributes.source =
                "exact_same_uid_operator_detail:" + row.trust->observed_at + ":" + row.trust->evidence_file;
        }
        result.attributes.emplace(row.id, std::move(attributes));
    }
    if (!modifiers || !identities_known) {
        return result;
    }
    const auto& evidence = *modifiers;
    const auto bounded = [](const auto& interval, double minimum) {
        return std::isfinite(interval[0]) && std::isfinite(interval[1]) && interval[0] >= minimum &&
               interval[0] <= interval[1];
    };
    if (evidence.client != client || evidence.uid != uid || evidence.run_id != run_id || evidence.source.empty() ||
        evidence.observed_at.empty() || !evidence.complete_for_covered_operators ||
        !evidence.primary_components_covered || !evidence.unclosed_dimensions.empty() ||
        !bounded(evidence.atk_fraction, -1) || !bounded(evidence.aspd, -99) ||
        !std::ranges::all_of(required_ids, [&](const auto& id) {
            return evidence.covered_operator_ids.contains(id);
        })) {
        result.source = "uid_profile_attributes;external_modifier_receipt_invalid_or_scope_incomplete";
        return result;
    }
    result.external_modifiers_bounded = true;
    result.external_atk_fraction = evidence.atk_fraction;
    result.external_aspd = evidence.aspd;
    result.source = evidence.source + ":" + evidence.observed_at;
    return result;
}

[[nodiscard]] inline json::value recruitment_profile_output_to_json(const RecruitmentOutputEvidence& evidence)
{
    json::object attributes;
    for (const auto& [id, row] : evidence.attributes) {
        attributes[id] = json::object {
            { "trust_percent", row.trust_percent ? json::value(*row.trust_percent) : json::value(nullptr) },
            { "potential", row.potential ? json::value(*row.potential) : json::value(nullptr) },
            { "source", row.source },
        };
    }
    return json::object {
        { "client", evidence.client },
        { "uid", evidence.uid },
        { "run_id", evidence.run_id },
        { "source", evidence.source },
        { "external_modifiers_bounded", evidence.external_modifiers_bounded },
        { "external_atk_fraction",
          evidence.external_modifiers_bounded
              ? json::value(json::array { evidence.external_atk_fraction[0], evidence.external_atk_fraction[1] })
              : json::value(nullptr) },
        { "external_aspd",
          evidence.external_modifiers_bounded
              ? json::value(json::array { evidence.external_aspd[0], evidence.external_aspd[1] })
              : json::value(nullptr) },
        { "attributes", std::move(attributes) },
        { "scope", "outside_detail_trust_not_panel_ATK_or_current_battle_equipment" },
    };
}
}
