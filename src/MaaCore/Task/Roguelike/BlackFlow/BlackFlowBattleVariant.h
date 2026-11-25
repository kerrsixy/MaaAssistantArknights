#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Common/AsstBattleDef.h"
#include "MaaUtils/NoWarningCVMat.hpp"

namespace asst
{
class RoguelikeCopilotConfig;
}

namespace asst::blackflow
{
using VariantDeployPlan = decltype(battle::roguelike::CombatData {}.deploy_plan);

struct BattleVariantObservation
{
    std::uint64_t capture_sequence = 0;
    bool capture_success = false;
    bool fresh_success = false;
    int kills = -1;
    int total = -1;
    std::string frame_path;
    bool frame_saved = false;
};

struct BattleVariantReceipt
{
    std::string selected_key;
    std::string reason;
    std::optional<int> stable_total;
    std::vector<BattleVariantObservation> observations;
    bool applied = false;
};

[[nodiscard]] std::optional<std::pair<std::string, int>> parse_battle_variant_key(const std::string& key);
[[nodiscard]] BattleVariantObservation
    recognize_battle_variant_total(const cv::Mat& image, std::uint64_t capture_sequence);
// Called exactly once before the normal deployment loop. This consumer applies
// only deploy_plan; every other rule and all deployment counters remain base.
[[nodiscard]] BattleVariantReceipt consume_predeploy_battle_variant(
    const RoguelikeCopilotConfig& config,
    const std::string& base_key,
    bool deployment_started,
    VariantDeployPlan& active_plan,
    const std::function<BattleVariantObservation()>& capture);
}
