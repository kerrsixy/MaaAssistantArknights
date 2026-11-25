#include "BlackFlowModifierEvidenceProducer.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace asst::blackflow::modifier_evidence_detail
{
bool economic_key(const std::string& key)
{
    static const std::set<std::string> keys { "recruit_cost",
                                              "upgrade_cost",
                                              "recruit_cost_sub_profession",
                                              "upgrade_cost_sub_profession",
                                              "change_shop_slot_pool",
                                              "limited_direct_upgrade",
                                              "immediate_reward",
                                              "battle_extra_reward",
                                              "battle_extra_drop",
                                              "immediate_recruit",
                                              "shop_refresh_cost_node",
                                              "shop_recycle_reward",
                                              "zone_into_reward",
                                              "node_into_reward",
                                              "immediate_reward_on_weather_clear",
                                              "random_node_clear_on_vehicle_move",
                                              "item_cover_set",
                                              "level_char_limit_add" };
    return keys.contains(key);
}

std::optional<double> declared_atk(const json::value& buffs, std::vector<std::string>& unsupported)
{
    if (!buffs.is_array()) {
        return std::nullopt;
    }
    double total = 0;
    for (const auto& buff : buffs.as_array()) {
        if (!buff.is_object() || !buff.contains("key") || !buff.at("key").is_string() || !buff.contains("blackboard") ||
            !buff.at("blackboard").is_array()) {
            return std::nullopt;
        }
        const auto& key = buff.at("key").as_string();
        if (key == "char_attribute_mul") {
            for (const auto& row : buff.at("blackboard").as_array()) {
                if (!row.is_object() || !row.contains("key") || !row.at("key").is_string() || !row.contains("value") ||
                    !row.at("value").is_number() || !std::isfinite(row.at("value").as_double())) {
                    return std::nullopt;
                }
                const auto& stat = row.at("key").as_string();
                if (stat == "atk") {
                    total += row.at("value").as_double() * 100;
                }
                else if (stat != "max_hp" && stat != "def") {
                    unsupported.push_back(key + ":" + stat);
                }
            }
        }
        else if (!economic_key(key)) {
            unsupported.push_back(key);
        }
    }
    return total;
}
}

namespace asst::blackflow
{
const std::vector<std::string>& BlackFlowModifierEvidenceProducer::required_dimensions()
{
    static const std::vector<std::string> dimensions { "all_held_collections_and_assigned_effects",
                                                       "actual_run_equipment",
                                                       "account_passive_stacking",
                                                       "squad_effects",
                                                       "attack_and_attack_speed",
                                                       "initial_sp_and_sp_cost",
                                                       "sp_recovery_and_conditions",
                                                       "primary_damage_scale_and_damage_type",
                                                       "hit_count_interval_and_ammunition",
                                                       "range_and_target_rules",
                                                       "additional_damage_and_conditional_effects",
                                                       "difficulty_weather_and_debuffs" };
    return dimensions;
}

bool BlackFlowModifierEvidenceProducer::load(const std::filesystem::path& path)
{
    m_valid = false;
    m_source_hash.clear();
    m_life = m_bands = m_items = json::value {};
    const auto data = json::open(path);
    if (!data || !data->is_object()) {
        return false;
    }
    try {
        if (data->get("schema", 0) != 1 || !data->at("source").is_object() ||
            !data->at("source").at("sha256").is_string() || !data->at("life_nodes").is_object() ||
            !data->at("bands").is_array() || !data->at("items").is_object()) {
            return false;
        }
        const auto hash = data->at("source").at("sha256").as_string();
        if (hash.size() != 64 ||
            !std::ranges::all_of(hash, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
            data->at("life_nodes").as_object().empty() || data->at("bands").as_array().empty()) {
            return false;
        }
        for (const auto& [id, node] : data->at("life_nodes").as_object()) {
            if (!node.is_object() || node.get("id", "") != id || !node.at("display").is_array() ||
                !node.at("raw_description").is_array()) {
                return false;
            }
            for (const auto& text : node.at("raw_description").as_array()) {
                if (!text.is_string()) {
                    return false;
                }
            }
            for (const auto& display : node.at("display").as_array()) {
                if (!display.is_object() || !display.at("displayType").is_string() ||
                    !display.at("displayForm").is_string() || !display.at("displayNum").is_number() ||
                    !std::isfinite(display.at("displayNum").as_double())) {
                    return false;
                }
            }
        }
        for (const auto& band : data->at("bands").as_array()) {
            std::vector<std::string> unsupported;
            if (!band.is_object() || !band.at("id").is_string() || !band.at("name").is_string() ||
                !modifier_evidence_detail::declared_atk(band.at("buffs"), unsupported)) {
                return false;
            }
        }
        m_source_hash = hash;
        m_life = data->at("life_nodes");
        m_bands = data->at("bands");
        m_items = data->at("items");
        m_valid = true;
        return true;
    }
    catch (const json::exception&) {
        return false;
    }
}

ModifierEvidenceParts BlackFlowModifierEvidenceProducer::observe(const ModifierEvidenceRequest& request) const
{
    ModifierEvidenceParts result { .source_valid = m_valid,
                                   .client = request.client,
                                   .uid = request.uid,
                                   .run_id = request.run_id,
                                   .source_hash = m_source_hash,
                                   .observed_at = request.observed_at,
                                   .configured_squad = request.configured_squad };
    result.open_dimensions = required_dimensions();
    result.identity_valid = request.profile && !request.uid.empty() && !request.run_id.empty() &&
                            request.profile->client() == request.client && request.profile->uid() == request.uid;
    if (!m_valid) {
        result.reasons.push_back("source_catalog_unavailable");
        return result;
    }
    if (!result.identity_valid) {
        result.reasons.push_back("profile_UID_client_or_real_run_identity_unbound");
        return result;
    }
    try {
        std::set<std::string> active, inactive;
        bool life_valid = false;
        if (request.life && request.life->client == request.client && request.life->uid == request.uid &&
            !request.life->source.empty() && request.life->source_hash == m_source_hash &&
            !request.life->observed_at.empty()) {
            active.insert(request.life->active_ids.begin(), request.life->active_ids.end());
            inactive.insert(request.life->inactive_ids.begin(), request.life->inactive_ids.end());
            life_valid = active.size() == request.life->active_ids.size() &&
                         inactive.size() == request.life->inactive_ids.size() &&
                         std::ranges::all_of(
                             active,
                             [&](const auto& id) { return m_life.contains(id) && !inactive.contains(id); }) &&
                         std::ranges::all_of(inactive, [&](const auto& id) { return m_life.contains(id); });
        }
        if (!life_valid) {
            active.clear();
            inactive.clear();
            if (request.life) {
                result.reasons.push_back("optional_life_receipt_invalid_full_source_interval_retained");
            }
        }
        result.life_scope = life_valid ? "same_UID_observed_node_states_plus_source_envelope_for_unknown_nodes"
                                       : "full_source_possible_states_not_observed_account_completion";
        for (const auto& [id, node] : m_life.as_object()) {
            double effect = 0;
            for (const auto& display : node.at("display").as_array()) {
                if (display.at("displayType").as_string() == "display_bat_attack") {
                    if (display.at("displayForm").as_string() != "PERCENTAGE") {
                        result.unsupported_source_effects.push_back("life_ATK_form:" + id);
                    }
                    else {
                        effect += display.at("displayNum").as_double();
                    }
                }
            }
            if (active.contains(id)) {
                result.life_display_atk_percent[0] += effect;
                result.life_display_atk_percent[1] += effect;
            }
            else if (!inactive.contains(id)) {
                result.unknown_life_ids.push_back(id);
                result.life_display_atk_percent[0] += std::min(0.0, effect);
                result.life_display_atk_percent[1] += std::max(0.0, effect);
            }
        }
        bool first = true;
        const bool selected = request.selected_squad_verified && !request.selected_squad_source.empty();
        for (const auto& band : m_bands.as_array()) {
            if (selected && band.at("name").as_string() != request.configured_squad) {
                continue;
            }
            const auto atk =
                modifier_evidence_detail::declared_atk(band.at("buffs"), result.unsupported_source_effects);
            if (!atk) {
                result.reasons.push_back("malformed_band_source");
                return result;
            }
            if (first) {
                result.squad_declared_atk_percent = { *atk, *atk };
                first = false;
            }
            else {
                result.squad_declared_atk_percent[0] = std::min(result.squad_declared_atk_percent[0], *atk);
                result.squad_declared_atk_percent[1] = std::max(result.squad_declared_atk_percent[1], *atk);
            }
        }
        result.squad_scope = selected && !first ? "verified_selected_squad_all_source_variants"
                                                : "configuration_not_selection_all_source_squads_envelope";
        if (first) {
            result.reasons.push_back("verified_squad_has_no_source_match");
            return result;
        }
        // Inspect positive purchase facts, without claiming they form the held set.
        for (const auto& name : request.observed_purchase_names) {
            std::vector<json::value> matches;
            for (const auto& [id, item] : m_items.as_object()) {
                if (item.get("name", "") == name) {
                    matches.push_back(item);
                }
            }
            if (matches.size() != 1) {
                result.unsupported_source_effects.push_back("purchase_identity_unknown_or_ambiguous:" + name);
            }
            else {
                std::vector<std::string> keys;
                const auto component = modifier_evidence_detail::declared_atk(matches[0].at("buffs"), keys);
                if (!component) {
                    result.unsupported_source_effects.push_back("malformed_purchase_source:" + name);
                }
                for (const auto& key : keys) {
                    result.unsupported_source_effects.push_back("purchase:" + name + ":" + key);
                }
            }
        }
        result.reasons.push_back("displayed_percent_component_not_engine_stacking_or_full_combat_modifier");
        result.reasons.push_back("purchase_names_not_complete_held_collections_or_actual_run_modules");
        if (!request.closure) {
            return result;
        }
        const auto& closure = *request.closure;
        if (closure.client != request.client || closure.uid != request.uid || closure.run_id != request.run_id ||
            closure.source.empty() || closure.observed_at.empty()) {
            result.reasons.push_back("external_closure_identity_or_provenance_invalid");
            return result;
        }
        const auto ordered = [](const auto& bounds, double minimum) {
            return std::isfinite(bounds[0]) && std::isfinite(bounds[1]) && bounds[0] >= minimum &&
                   bounds[0] <= bounds[1];
        };
        if (!ordered(closure.additive_atk_fraction, -1) || !ordered(closure.aspd, -99)) {
            result.reasons.push_back("external_closure_numeric_bounds_invalid");
            return result;
        }
        result.open_dimensions.clear();
        for (const auto& dimension : required_dimensions()) {
            const auto found = closure.dimension_sources.find(dimension);
            if (found == closure.dimension_sources.end() || found->second.empty()) {
                result.open_dimensions.push_back(dimension);
            }
        }
        // A separate completed, audited producer must supply a closed projection.
        // This partial producer never converts display percentages into guessed
        // additive ATK or assumes no SP/conditional effects from an empty list.
        if (result.open_dimensions.empty() && result.unsupported_source_effects.empty()) {
            RecruitmentModifierEvidence receipt { .client = request.client,
                                                  .uid = request.uid,
                                                  .run_id = request.run_id,
                                                  .source = closure.source,
                                                  .observed_at = closure.observed_at,
                                                  .complete_for_covered_operators = true,
                                                  .primary_components_covered = true,
                                                  .atk_fraction = closure.additive_atk_fraction,
                                                  .aspd = closure.aspd };
            for (const auto& name : request.operator_names) {
                const auto oper = request.profile->find_operator(name);
                if (!oper) {
                    result.reasons.push_back("closure_operator_identity_unknown");
                    return result;
                }
                receipt.covered_operator_ids.emplace(oper->get().id);
            }
            if (!receipt.covered_operator_ids.empty()) {
                result.comparison_receipt = std::move(receipt);
            }
        }
        return result;
    }
    catch (const json::exception&) {
        result.source_valid = false;
        result.reasons.push_back("malformed_optional_source_component_unknown_not_workflow_abort");
        result.comparison_receipt.reset();
        return result;
    }
}

std::optional<LifeModifierReceipt> BlackFlowModifierEvidenceProducer::completed_life_game(
    const LifeGameScreenObservation& screen,
    const std::vector<LifeGameNode>& owner_nodes,
    const std::string& client,
    const std::string& uid,
    const std::string& observed_at,
    const std::string& evidence_file,
    std::string* error) const
{
    const auto reject = [&](const char* reason) -> std::optional<LifeModifierReceipt> {
        if (error) {
            *error = reason;
        }
        return std::nullopt;
    };
    if (!m_valid) {
        return reject("life_modifier_source_unavailable");
    }
    if ((client != "Official" && client != "Bilibili") || uid.empty() ||
        !std::ranges::all_of(uid, [](char c) { return c >= '0' && c <= '9'; }) || observed_at.empty()) {
        return reject("life_modifier_account_identity_or_stamp_unbound");
    }
    if (!screen.page_verified || !screen.collection_complete) {
        return reject("life_modifier_lit_complete_page_unproven");
    }
    if (!screen.collected || !screen.total || *screen.collected != *screen.total) {
        return reject("life_modifier_complete_counter_unproven");
    }
    if (*screen.total != static_cast<int>(m_life.as_object().size()) ||
        owner_nodes.size() != m_life.as_object().size()) {
        return reject("life_modifier_owner_source_count_mismatch");
    }
    std::set<std::string> ids;
    for (const auto& node : owner_nodes) {
        if (!m_life.contains(node.id) || !ids.emplace(node.id).second) {
            return reject("life_modifier_owner_source_ID_mismatch_or_duplicate");
        }
        std::vector<std::string> descriptions;
        for (const auto& text : m_life.at(node.id).at("raw_description").as_array()) {
            descriptions.push_back(text.as_string());
        }
        if (descriptions != node.raw_effects) {
            return reject("life_modifier_owner_source_semantics_mismatch");
        }
    }
    return LifeModifierReceipt { .client = client,
                                 .uid = uid,
                                 .source = "same_uid_stable_life_game_lit_complete_count_and_exact_source_ID_set",
                                 .source_hash = m_source_hash,
                                 .observed_at = observed_at,
                                 .evidence_file = evidence_file,
                                 .active_ids = std::vector<std::string>(ids.begin(), ids.end()) };
}

json::value modifier_evidence_parts_to_json(const ModifierEvidenceParts& value)
{
    const auto strings = [](const auto& items) {
        json::array array;
        for (const auto& item : items) {
            array.emplace_back(item);
        }
        return array;
    };
    return json::object {
        { "client", value.client },
        { "uid", value.uid },
        { "run_id", value.run_id },
        { "source_hash", value.source_hash },
        { "source_valid", value.source_valid },
        { "identity_valid", value.identity_valid },
        { "observed_at", value.observed_at },
        { "configured_squad", value.configured_squad },
        { "squad_scope", value.squad_scope },
        { "life_scope", value.life_scope },
        { "life_display_ATK_percent",
          value.identity_valid && value.source_valid
              ? json::value(json::array { value.life_display_atk_percent[0], value.life_display_atk_percent[1] })
              : json::value(nullptr) },
        { "squad_declared_ATK_percent",
          value.identity_valid && value.source_valid
              ? json::value(json::array { value.squad_declared_atk_percent[0], value.squad_declared_atk_percent[1] })
              : json::value(nullptr) },
        { "unknown_life_ids", strings(value.unknown_life_ids) },
        { "unsupported_source_effects", strings(value.unsupported_source_effects) },
        { "open_dimensions", strings(value.open_dimensions) },
        { "reasons", strings(value.reasons) },
        { "comparison_scope_closed", value.comparison_receipt.has_value() },
        { "full_DPS", nullptr },
        { "current_battle_equipment_proven", false }
    };
}
}
