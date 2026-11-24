#include "BlackFlowOpeningDutyCatalog.h"
#include <algorithm>

namespace asst::blackflow
{
BlackFlowOpeningDutyCatalog& opening_duty_catalog()
{
    static BlackFlowOpeningDutyCatalog value;
    return value;
}

void BlackFlowOpeningDutyCatalog::clear()
{
    m_rules.clear();
}

bool BlackFlowOpeningDutyCatalog::load(
    const std::filesystem::path& f,
    const std::filesystem::path& t,
    const std::filesystem::path& b)
{
    clear();
    const auto facts = json::open(f), training = json::open(t), battle = json::open(b);
    return facts && training && battle && configure(*facts, *training, *battle);
}

bool BlackFlowOpeningDutyCatalog::configure(const json::value& f, const json::value& t, const json::value& b)
{
    clear();
    if (!f.is_object() || !t.is_object() || !b.is_object() || f.get("schema", 0) != 1) {
        return false;
    }
    const auto binding = f.find<json::object>("catalog_binding");
    const auto rules = f.find<json::array>("rules");
    const auto ops = t.find<json::object>("operators");
    const auto chars = b.find<json::object>("chars");
    if (!binding || !rules || !ops || !chars) {
        return false;
    }
    if (binding->get("game_data_revision", "").empty() ||
        binding->get("game_data_revision", "") != t.get("game_data_revision", "") ||
        binding->get("character_table_sha256", "").empty() ||
        binding->get("character_table_sha256", "") != t.get("character_table_sha256", "")) {
        return false;
    }
    std::map<std::pair<std::string, int>, Rule> parsed;
    for (const auto& entry : *rules) {
        if (!entry.is_object()) {
            return false;
        }
        const auto id = entry.get("operator_id", "");
        const int skill = entry.get("selected_skill", 0);
        const auto def = entry.find<json::object>("expected_runtime_definition");
        const auto levels = entry.find<json::array>("reviewed_skill_levels");
        if (id.empty() || skill < 1 || skill > 3 || !def || !levels || entry.get("review_status", "") != "reviewed" ||
            entry.get("duty", "") != "base_healing_only" || !entry.contains("all_skill_levels") ||
            !entry.at("all_skill_levels").is<bool>()) {
            return false;
        }
        const auto runtime = chars->find<json::object>(id);
        const auto training = ops->find<json::object>(id);
        if (!runtime || !training) {
            return false;
        }
        const auto training_skills = training->find<json::array>("skills");
        if (!training_skills || training_skills->size() < static_cast<std::size_t>(skill)) {
            return false;
        }
        const auto& training_slot = training_skills->at(static_cast<std::size_t>(skill - 1));
        const auto skill_id = entry.get("skill_id", "");
        if (!training_slot.is_object() || skill_id.empty() || training_slot.get("index", 0) != skill ||
            !training_slot.get("known", false) || training_slot.get("skill_id", "") != skill_id ||
            training_slot.get("unlock_elite", -1) != entry.get("minimum_elite", -1)) {
            return false;
        }
        Rule rule;
        rule.minimum_elite = entry.get("minimum_elite", -1);
        rule.all_skill_levels = entry.at("all_skill_levels").as_boolean();
        if (rule.minimum_elite < 0 || rule.minimum_elite > 2) {
            return false;
        }
        for (const auto& level : *levels) {
            if (!level.is<int>() || level.as_integer() < 1 || level.as_integer() > 10) {
                return false;
            }
            rule.levels.push_back(level.as_integer());
        }
        std::ranges::sort(rule.levels);
        if (rule.levels.empty() || std::adjacent_find(rule.levels.begin(), rule.levels.end()) != rule.levels.end()) {
            return false;
        }
        if (rule.all_skill_levels && rule.levels != std::vector<int> { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }) {
            return false;
        }
        // Schema1 supports this reviewed ability shape, never all units of this subclass.
        if (def->get("profession", "") != "MEDIC" || def->get("subProfessionId", "") != "wandermedic") {
            continue;
        }
        if (runtime->get("profession", "") != def->get("profession", "") ||
            runtime->get("subProfessionId", "") != def->get("subProfessionId", "")) {
            return false;
        }
        const auto ranges = def->find<json::array>("rangeId");
        const auto actual_ranges = runtime->find<json::array>("rangeId");
        const auto tokens = def->find<json::array>("tokens");
        if (!ranges || ranges->size() != 3 || !actual_ranges || actual_ranges->size() != 3 || !tokens) {
            return false;
        }
        for (std::size_t i = 0; i < 3; ++i) {
            if (!ranges->at(i).is_string() || !actual_ranges->at(i).is_string() ||
                ranges->at(i).as_string() != actual_ranges->at(i).as_string()) {
                return false;
            }
            rule.ranges[i] = ranges->at(i).as_string();
        }
        for (const auto& token : *tokens) {
            if (!token.is_string()) {
                return false;
            }
            rule.tokens.push_back(token.as_string());
        }
        std::vector<std::string> actual_tokens;
        if (const auto list = runtime->find<json::array>("tokens")) {
            for (const auto& token : *list) {
                if (!token.is_string()) {
                    return false;
                }
                actual_tokens.push_back(token.as_string());
            }
        }
        else if (runtime->contains("tokens")) {
            return false;
        }
        std::ranges::sort(rule.tokens);
        std::ranges::sort(actual_tokens);
        // Reviewed healing-only facts cannot silently acquire a summoning dependency.
        if (!rule.tokens.empty() || rule.tokens != actual_tokens) {
            return false;
        }
        if (!parsed.emplace(std::make_pair(id, skill), std::move(rule)).second) {
            return false;
        }
    }
    m_rules = std::move(parsed);
    return true;
}

BaseOpeningDuty BlackFlowOpeningDutyCatalog::resolve(const OpeningDutyRuntime& e) const
{
    if (e.unique_operator_id.empty() || e.prepared_operator_id != e.unique_operator_id ||
        e.properties_operator_id != e.unique_operator_id || e.selection_sequence == 0 || !e.properties_known ||
        !e.medic_role || !e.wander_subrole || e.elite < 0 || e.elite > 2) {
        return BaseOpeningDuty::Unknown;
    }
    const auto it = m_rules.find({ std::string(e.unique_operator_id), e.selected_skill });
    if (it == m_rules.end()) {
        return BaseOpeningDuty::Unknown;
    }
    const auto& r = it->second;
    if (e.elite < r.minimum_elite || e.ranges != r.ranges || !e.tokens.empty()) {
        return BaseOpeningDuty::Unknown;
    }
    if (!r.all_skill_levels &&
        (e.training_sequence == 0 || std::ranges::find(r.levels, e.displayed_skill_level) == r.levels.end())) {
        return BaseOpeningDuty::Unknown;
    }
    return BaseOpeningDuty::HealingOnly;
}

OpeningDutyChoice BlackFlowOpeningDutyCatalog::choose(const OpeningDutyRuntime& e, const OpeningDutyContext& c) const
{
    if (c.authored_plan || c.uncertain_placement || c.explicit_nonhealing_task ||
        c.verified_incoming_covered_ally_task) {
        return OpeningDutyChoice::KeepForTask;
    }
    if (resolve(e) != BaseOpeningDuty::HealingOnly || c.patient == HealingPatientState::Unknown) {
        return OpeningDutyChoice::KeepUnknown;
    }
    return c.patient == HealingPatientState::Covered ? OpeningDutyChoice::KeepForTask
                                                     : OpeningDutyChoice::DeferIdleHealing;
}
}
