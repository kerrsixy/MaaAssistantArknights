#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "AsstCaller.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Task/Roguelike/BlackFlow/BlackFlowBattleVariant.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"

class VariantConsumerProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    explicit VariantConsumerProbe(const std::shared_ptr<asst::RoguelikeConfig>& config) :
        RoguelikeBattleTaskPlugin({}, nullptr, "Variant132Offline", config, nullptr)
    {
    }

    void base(const asst::battle::roguelike::CombatData& plan) { m_deploy_plan = plan.deploy_plan; }

    const asst::blackflow::VariantDeployPlan& plan() const { return m_deploy_plan; }

    asst::blackflow::BattleVariantReceipt preflight(
        const asst::RoguelikeCopilotConfig& config,
        const std::string& stage,
        const std::function<asst::blackflow::BattleVariantObservation()>& capture)
    {
        return asst::blackflow::consume_predeploy_battle_variant(
            config,
            stage,
            !m_first_deploy,
            m_deploy_plan,
            capture);
    }

    void deployed(int index)
    {
        m_first_deploy = false;
        ++m_plan_deployment_counts[index];
    }

    using RoguelikeBattleTaskPlugin::deployment_plan_exhausted;
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path resources(argv[1]), candidate(argv[2]), output(argv[3]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(resources.native()).c_str())) {
        return 3;
    }
    const auto base = json::open(candidate / utils::path("fixtures/base.json"));
    const auto variant =
        json::open(candidate / utils::path("resource/roguelike/BlackFlow/autopilot/进退趋同#total34.json"));
    if (!base || !variant) {
        return 4;
    }
    int checks = 0, failures = 0;
    const auto check = [&](bool passed, const char* label) {
        ++checks;
        if (!passed) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const auto write = [&](const std::filesystem::path& folder, const json::value& a, const json::value& b) {
        std::filesystem::create_directories(folder);
        std::ofstream(folder / utils::path("base.json")) << a;
        std::ofstream(folder / utils::path("variant.json")) << b;
    };
    const auto valid_folder = output / utils::path("valid_config");
    write(valid_folder, *base, *variant);
    RoguelikeCopilotConfig config;
    check(config.load(valid_folder), "actual config loader accepts explicit C34 variant and base");
    check(config.load(valid_folder), "same singleton/config object repeat load is idempotent across batches");
    const std::string stage = "进退趋同";
    const auto base_plan = config.get_stage_data(stage), c_plan = config.get_stage_data(stage + "#total34");
    if (!base_plan || !c_plan) {
        return 5;
    }
    check(
        config.get_stage_variant_keys(stage) == std::vector<std::pair<int, std::string>> { { 34, stage + "#total34" } },
        "actual registry query exposes declared condition once");
    const auto cfg = std::make_shared<RoguelikeConfig>();
    check(
        cfg->verify_and_load_params(json::object { { "theme", "BlackFlow" }, { "mode", 0 } }),
        "real BlackFlow configuration");
    const auto sequence = [&](std::vector<BattleVariantObservation> observations,
                              const char* label,
                              bool expected_applied,
                              std::size_t expected_attempts,
                              std::optional<int> expected_total) {
        VariantConsumerProbe consumer(cfg);
        consumer.base(*base_plan);
        std::size_t calls = 0;
        const auto result = consumer.preflight(config, stage, [&]() {
            const auto i = calls++;
            return i < observations.size() ? observations[i] : BattleVariantObservation {};
        });
        check(
            result.applied == expected_applied && calls == expected_attempts && result.stable_total == expected_total,
            label);
        const auto& expected = expected_applied ? *c_plan : *base_plan;
        const auto& received = consumer.plan().at("地面阻挡").front();
        const auto& wanted = expected.deploy_plan.at("地面阻挡").front();
        check(
            received.location == wanted.location && received.rank == wanted.rank &&
                received.minimum_base_block_count == wanted.minimum_base_block_count,
            "actual BattlePlugin protected deployment-plan consumer receives selected/base instructions");
        return result;
    };
    const auto fresh = [](std::uint64_t n, int total, int kills = 0) {
        return BattleVariantObservation { .capture_sequence = n,
                                          .capture_success = true,
                                          .fresh_success = true,
                                          .kills = kills,
                                          .total = total };
    };
    const auto c =
        sequence({ fresh(1, 34), fresh(2, 34) }, "C34 two fresh observations choose only declared C plan", true, 2, 34);
    sequence({ fresh(1, 38), fresh(2, 38) }, "A38 remains original base, never C", false, 2, 38);
    sequence({ fresh(1, 32), fresh(2, 32) }, "B32 remains original base, never C", false, 2, 32);
    sequence(
        { fresh(1, 34, 1), fresh(2, 34, 2) },
        "kills may advance before first deployment without assuming fresh0",
        true,
        2,
        34);
    sequence({ {}, {}, {}, {} }, "four invalid observations finitely degrade to base", false, 4, std::nullopt);
    sequence(
        { fresh(1, 34), fresh(1, 34), fresh(1, 34), fresh(1, 34) },
        "same capture cannot vote twice",
        false,
        4,
        std::nullopt);
    sequence(
        { fresh(2, 34), fresh(1, 34), fresh(2, 34), fresh(1, 34) },
        "old and reused capture values cannot stabilize",
        false,
        4,
        std::nullopt);
    sequence(
        { fresh(1, 34), fresh(2, 38), fresh(3, 34), fresh(4, 38) },
        "conflicting totals exhaust bounded preflight and retain base",
        false,
        4,
        std::nullopt);
    sequence(
        { fresh(1, 34), {}, fresh(3, 34), fresh(4, 34) },
        "fresh agreement must be consecutive valid observations",
        true,
        4,
        34);
    sequence(
        { fresh(1, 0), fresh(2, -1), fresh(3, 34, 35), fresh(4, 34, -1) },
        "invalid ranges cannot form positive evidence",
        false,
        4,
        std::nullopt);
    sequence({ fresh(1, 35), fresh(2, 35) }, "stable unknown variant explicitly retains base", false, 2, 35);
    auto cached1 = fresh(1, 34), cached2 = fresh(2, 34);
    cached1.capture_success = cached2.capture_success = false;
    sequence(
        { cached1, cached2, cached1, cached2 },
        "new labels without successful physical capture are not fresh votes",
        false,
        4,
        std::nullopt);
    VariantConsumerProbe started(cfg);
    started.base(*base_plan);
    int calls = 0;
    const auto applied = started.preflight(config, stage, [&]() { return fresh(++calls, 34); });
    const auto opener = started.plan().at("回费").front();
    started.deployed(opener.plan_index);
    check(
        applied.applied && started.deployment_plan_exhausted(opener),
        "real deployment counter consumes max1 C opener");
    calls = 0;
    const auto locked = started.preflight(config, stage, [&]() { return fresh(++calls, 38); });
    check(
        !locked.applied && calls == 0 && started.deployment_plan_exhausted(opener) &&
            started.plan().at("地面阻挡").front().minimum_base_block_count == 3,
        "after deployment no recapture/plan switch/counter reset occurs");
    auto invalid = *variant;
    invalid["when"]["total_enemies"] = 38;
    const auto invalid_folder = output / utils::path("mismatched_condition");
    write(invalid_folder, *base, invalid);
    check(!config.load(invalid_folder), "suffix/declared total mismatch rejected");
    check(
        config.get_stage_variant_keys(stage).size() == 1,
        "failed load rolls back registry instead of leaving partial variant");
    invalid = *variant;
    invalid["stage_name"] = stage + "#total034";
    const auto alias_folder = output / utils::path("duplicate_total_alias");
    write(alias_folder, *base, invalid);
    check(!config.load(alias_folder), "noncanonical total alias rejected rather than duplicate total identity");
    invalid = *variant;
    invalid["reserve_deploy_cost"] = false;
    const auto budget_folder = output / utils::path("changed_budget");
    write(budget_folder, *base, invalid);
    check(!config.load(budget_folder), "variant cannot alter base reserve budget semantics");
    invalid = *variant;
    invalid["reserve_starting_core_groups"] = json::array {};
    const auto groups_folder = output / utils::path("changed_core_groups");
    write(groups_folder, *base, invalid);
    check(!config.load(groups_folder), "variant cannot alter reserved starting core groups");
    invalid = *variant;
    invalid["replacement_home"] = json::array {};
    const auto field_folder = output / utils::path("other_override");
    write(field_folder, *base, invalid);
    check(!config.load(field_folder), "other base rule overrides rejected rather than silently ignored");
    const auto duplicate_folder = output / utils::path("duplicate_batch");
    write(duplicate_folder, *base, *variant);
    std::ofstream(duplicate_folder / utils::path("duplicate.json")) << *variant;
    check(
        !config.load(duplicate_folder),
        "same batch duplicate variant key rejected without breaking repeat-load idempotence");
    const auto duplicate_bases_folder = output / utils::path("second_batch_duplicate_retained_base");
    std::filesystem::create_directories(duplicate_bases_folder);
    std::ofstream(duplicate_bases_folder / utils::path("base1.json")) << *base;
    std::ofstream(duplicate_bases_folder / utils::path("base2.json")) << *base;
    check(!config.load(duplicate_bases_folder), "retained variant rejects conflicting duplicate base-only later batch");
    invalid = *variant;
    invalid["deploy_plan"] = json::array {};
    const auto empty_folder = output / utils::path("empty_plan");
    write(empty_folder, *base, invalid);
    check(!config.load(empty_folder), "empty damaged variant plan rejected");
    const auto exception_folder = output / utils::path("exception_batch");
    std::filesystem::create_directories(exception_folder);
    auto prefix = *base;
    prefix["stage_name"] = "valid_prefix_before_debug_exception";
    std::ofstream(exception_folder / utils::path("a_valid.json")) << prefix;
    auto broken = *base;
    broken.as_object().erase("stage_name");
    std::ofstream(exception_folder / utils::path("z_bad.json")) << broken;
    bool debug_exception = false;
    try {
        (void)config.load(exception_folder);
    }
    catch (const std::out_of_range&) {
        debug_exception = true;
    }
    check(
        debug_exception && !config.get_stage_data("valid_prefix_before_debug_exception") &&
            config.get_stage_variant_keys(stage).size() == 1,
        "DEBUG parse exception is preserved while all registry maps roll back");
    int captures = 0;
    VariantConsumerProbe unrelated(cfg);
    unrelated.base(*base_plan);
    const auto untouched = unrelated.preflight(config, "unconfigured_stage", [&]() {
        ++captures;
        return fresh(captures, 34);
    });
    check(captures == 0 && !untouched.applied, "unconfigured stages do not run delayed preflight");
    for (const auto& name : { "C34_020.png", "C34_021.png" }) {
        const auto image = MAA_NS::imread(candidate / utils::path("fixtures") / utils::path(name));
        const auto result = recognize_battle_variant_total(image, 1);
        check(
            image.cols == 1280 && image.rows == 720 && result.fresh_success && !result.capture_success &&
                result.kills == 0 && result.total == 34,
            "independent no-prompt actual Matcher reads full historicalC34 HUD; freshness remains unproved from cached "
            "fixture");
    }
    check(!recognize_battle_variant_total(cv::Mat {}, 1).fresh_success, "failed capture cannot reuse old cached HUD");
    const json::value result = json::object { { "checks", checks },
                                              { "failures", failures },
                                              { "selected_C34_key", c.selected_key },
                                              { "C34_reason", c.reason },
                                              { "capture_limit", 4 },
                                              { "actual_protected_plan_and_counter_consumers", true },
                                              { "typed_capture_sequence_cases_not_live_UI", true },
                                              { "historical_C34_fixture_source_cached", true },
                                              { "A38_B32_OCR_fixtures_available", false },
                                              { "device_inputs", 0 },
                                              { "actual_deploy_clicks", 0 } };
    std::ofstream(output / utils::path("variant132.json")) << result;
    std::cout << "VARIANT132 checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
