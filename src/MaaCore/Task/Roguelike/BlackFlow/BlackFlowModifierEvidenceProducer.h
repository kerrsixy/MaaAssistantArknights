#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "BlackFlowRecruitmentOutputEvidence.h"
#include "Config/Roguelike/BlackFlow/BlackFlowAccountModifierEvidence.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowLifeGameAnalyzer.h"

namespace asst::blackflow
{
// A complete producer must close every primary component, not just ATK/ASPD.
// This receipt does not originate from the legacy purchase-name vector.
struct RunModifierClosure
{
    std::string client;
    std::string uid;
    std::string run_id;
    std::string source;
    std::string observed_at;
    std::unordered_map<std::string, std::string> dimension_sources;
    std::array<double, 2> additive_atk_fraction { 0, 0 };
    std::array<double, 2> aspd { 0, 0 };
};

struct ModifierEvidenceRequest
{
    const BlackFlowAccountProfile* profile = nullptr;
    std::string client;
    std::string uid;
    std::string run_id;
    std::string observed_at;
    std::vector<std::string> operator_names;
    std::string configured_squad;
    // False for the current config name alone: click attempted != final selection.
    bool selected_squad_verified = false;
    std::string selected_squad_source;
    std::vector<std::string> observed_purchase_names;
    std::optional<LifeModifierReceipt> life;
    std::optional<RunModifierClosure> closure;
};

struct ModifierEvidenceParts
{
    bool identity_valid = false;
    bool source_valid = false;
    std::string client;
    std::string uid;
    std::string run_id;
    std::string source_hash;
    std::string observed_at;
    std::string configured_squad;
    std::string squad_scope;
    std::string life_scope;
    std::array<double, 2> life_display_atk_percent { 0, 0 };
    std::array<double, 2> squad_declared_atk_percent { 0, 0 };
    std::vector<std::string> unknown_life_ids;
    std::vector<std::string> unsupported_source_effects;
    std::vector<std::string> open_dimensions;
    std::vector<std::string> reasons;
    std::optional<RecruitmentModifierEvidence> comparison_receipt;
};

class BlackFlowModifierEvidenceProducer
{
public:
    bool load(const std::filesystem::path& path);

    [[nodiscard]] bool valid() const noexcept { return m_valid; }

    [[nodiscard]] ModifierEvidenceParts observe(const ModifierEvidenceRequest& request) const;
    [[nodiscard]] std::optional<LifeModifierReceipt> completed_life_game(
        const LifeGameScreenObservation& screen,
        const std::vector<LifeGameNode>& owner_nodes,
        const std::string& client,
        const std::string& uid,
        const std::string& observed_at,
        const std::string& evidence_file,
        std::string* error = nullptr) const;
    [[nodiscard]] static const std::vector<std::string>& required_dimensions();

private:
    bool m_valid = false;
    std::string m_source_hash;
    json::value m_life;
    json::value m_bands;
    json::value m_items;
};

[[nodiscard]] json::value modifier_evidence_parts_to_json(const ModifierEvidenceParts& value);
}
