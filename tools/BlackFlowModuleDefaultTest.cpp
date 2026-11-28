#include "AsstCaller.h"
#include "Config/Roguelike/BlackFlow/BlackFlowAccountScanCatalog.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Task/Roguelike/BlackFlow/BlackFlowAccountSnapshotStore.h"
#include "Utils/Platform.hpp"
#include "Vision/Roguelike/BlackFlow/BlackFlowCurrentModuleAnalyzer.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowModulePanelAnalyzer.h"
#include <filesystem>
#include <iostream>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path repo(argv[1]), fixtures(argv[2]), output(argv[3]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str())) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto ck = [&](bool v, const char* msg) {
        ++checks;
        if (!v) {
            ++failures;
            std::cerr << "FAIL " << msg << '\n';
        }
    };
    const auto catalog =
        BlackFlowAccountScanCatalog::load(repo / L"resource/roguelike/BlackFlow/account_scan_catalog.json");
    if (!catalog) {
        return 3;
    }
    const auto ref = catalog->find_target("提丰", "uniequip_004_typhon", 3);
    if (!ref) {
        return 3;
    }
    auto target = ref->get();
    const auto alpha = MAA_NS::imread(fixtures / L"typhon_default_detail_readback.png");
    const auto ordinary = MAA_NS::imread(fixtures / L"typhon_normal_default_detail_readback.png");
    const auto a = BlackFlowCurrentModuleAnalyzer(alpha).analyze(target),
               b = BlackFlowCurrentModuleAnalyzer(ordinary).analyze(target);
    ck(a && a->module_id == "uniequip_004_typhon" && a->level == 3,
       "exact current limited module on real detail frame");
    ck(b && b->module_id == "uniequip_002_typhon" && b->level == 3,
       "real current normal module is distinct from unlocked limited slot");
    const auto preview = BlackFlowCurrentModuleAnalyzer(
                             MAA_NS::imread(fixtures / L"typhon_special_preview_still_normal_default_detail.png"))
                             .analyze(target);
    ck(preview && preview->module_id == "uniequip_002_typhon",
       "merely previewing special card does not change current default");
    for (const auto& required : catalog->targets()) {
        const auto image = MAA_NS::imread(
            fixtures / L"b_default_switch_scan_only_05c/BlackFlowEvidence/account_refresh" /
            utils::path(required.operator_id) / L"detail-strength.png");
        const auto current = BlackFlowCurrentModuleAnalyzer(image).analyze(required);
        const auto expected =
            required.operator_id == "char_2012_typhon" ? "uniequip_002_typhon" : required.panel.module_id;
        ck(current && current->module_id == expected && current->level == 3,
           "real scan detail verifies its current module without lowering confidence");
        auto without_stage = image.clone();
        without_stage(cv::Rect(1152, 526, 88, 36)).setTo(cv::Scalar(0, 0, 0));
        ck(!BlackFlowCurrentModuleAnalyzer(without_stage).analyze(required),
           "module code without its current stage cannot establish readiness");
    }
    auto wrong = target;
    wrong.operator_id = "char_4039_horn";
    wrong.operator_name = "号角";
    ck(!BlackFlowCurrentModuleAnalyzer(alpha).analyze(wrong), "wrong operator cannot borrow current module");
    auto ambiguous = target;
    ambiguous.owner_module_codes.push_back(target.owner_module_codes.back());
    ck(!BlackFlowCurrentModuleAnalyzer(alpha).analyze(ambiguous), "ambiguous owner code cannot prove module ID");
    const auto panel = MAA_NS::imread(fixtures / L"typhon_normal_selected_setdefault_button.png");
    const ModulePanelTarget normal { "uniequip_002_typhon", "自然的包容", 3 };
    ck(BlackFlowModulePanelAnalyzer(panel).set_default_button(normal).has_value(),
       "actual selected MAX normal card has complete blue two-line default button");
    ck(!BlackFlowModulePanelAnalyzer(panel).set_default_button(target.panel),
       "another card's button cannot authorize the required card");
    ck(BlackFlowModulePanelAnalyzer(MAA_NS::imread(fixtures / L"typhon_special_selected_setdefault_button.png"))
           .set_default_button(target.panel)
           .has_value(),
       "fresh selected required MAX card has its own blue default button");
    ck(!BlackFlowModulePanelAnalyzer(MAA_NS::imread(fixtures / L"typhon_special_nondefault_visible.png"))
            .set_default_button(target.panel),
       "unselected visible required card cannot borrow a control");
    const std::string uid = "10000000"; // Synthetic test identity, never an account binding.
    json::value baseline = json::object { { "schema", 1 },
                                          { "package", "com.hypergryph.arknights" },
                                          { "account", json::object { { "uid", uid } } },
                                          { "operators", json::object {} } };
    baseline["operators"]["提丰"] =
        json::object { { "id", target.operator_id }, { "own", true }, { "elite", 2 }, { "level", 90 } };
    baseline["operators"]["提丰"]["module_slots"] =
        json::array { json::object { { "module_id", target.panel.module_id },
                                     { "level", 3 },
                                     { "unlocked", true },
                                     { "verified", true } } };
    std::string error;
    auto p = BlackFlowAccountProfile::parse(baseline, "Official", uid, &error);
    ck(p && p->module_available("提丰", target.panel.module_id, 3) &&
           !p->module_default_available("提丰", target.panel.module_id, 3),
       "old MAX-only cache is ownership, never default proof");
    AccountRefreshObservation obs;
    obs.client = "Official";
    obs.uid = uid;
    obs.returned_same_uid = true;
    obs.default_attempted.insert("提丰");
    obs.module_defaults["提丰"] = { { target.operator_id, target.panel.module_id, "ISWA", 3, true },
                                    fixtures / L"typhon_default_detail_readback.png" };
    const auto merge = [&](const AccountRefreshObservation& o, const json::value& old) {
        return BlackFlowAccountSnapshotStore::merge(old, o, { target }, {}, "20261128180001", &error);
    };
    const auto merged = merge(obs, baseline);
    ck(merged.has_value(), "typed same-account default receipt merges");
    if (merged) {
        p = BlackFlowAccountProfile::parse(*merged, "Official", uid, &error);
        ck(p && p->module_default_available("提丰", target.panel.module_id, 3), "typed positive default reloads");
        ck(p && p->default_operator_count() == 1, "default receipt count is independent from owned MAX count");
        auto foreign = *merged;
        foreign["operators"]["提丰"]["current_default_module"]["uid"] = "10000001";
        const auto foreign_profile = BlackFlowAccountProfile::parse(foreign, "Official", uid, &error);
        ck(foreign_profile && !foreign_profile->module_default_available("提丰", target.panel.module_id, 3),
           "nested other-account receipt stays default unknown");
        ck(BlackFlowAccountSnapshotStore::save_atomic(
               output / L"synthetic_cache.json",
               *merged,
               "20261128180002",
               &error),
           "typed cache saves atomically");
        const auto loaded = BlackFlowAccountProfile::load(output / L"synthetic_cache.json", "Official", uid, &error);
        ck(loaded && loaded->module_default_available("提丰", target.panel.module_id, 3),
           "same identity on-disk reload keeps default receipt");
        ck(!BlackFlowAccountProfile::load(output / L"synthetic_cache.json", "Bilibili", uid, &error),
           "cross-client default borrowing rejected");
        ck(!BlackFlowAccountProfile::load(output / L"synthetic_cache.json", "Official", "10000001", &error),
           "different identity default borrowing rejected");
        auto unknown = obs;
        unknown.module_defaults.clear();
        const auto retained = merge(unknown, *merged);
        const auto parsed =
            retained ? BlackFlowAccountProfile::parse(*retained, "Official", uid, &error) : std::nullopt;
        ck(parsed && !parsed->module_default_available("提丰", target.panel.module_id, 3),
           "fresh unknown suppresses old default readiness");
        ck(retained && retained->at("operators").at("提丰").contains("current_default_module"),
           "unknown keeps historical receipt instead of deleting evidence");
    }
    auto normal_obs = obs;
    normal_obs.module_defaults["提丰"].observation.module_id = "uniequip_002_typhon";
    normal_obs.module_defaults["提丰"].observation.code = "SIEX";
    const auto normal_cache = merge(normal_obs, baseline);
    p = normal_cache ? BlackFlowAccountProfile::parse(*normal_cache, "Official", uid, &error) : std::nullopt;
    ck(p && !p->module_default_available("提丰", target.panel.module_id, 3),
       "known ordinary default does not satisfy limited gate");
    auto low = obs;
    low.module_defaults["提丰"].observation.level = 1;
    const auto low_cache = merge(low, baseline);
    p = low_cache ? BlackFlowAccountProfile::parse(*low_cache, "Official", uid, &error) : std::nullopt;
    ck(p && !p->module_default_available("提丰", target.panel.module_id, 3),
       "equipped but insufficient stage cannot satisfy required default");
    for (int scenario = 0; scenario < 4; ++scenario) {
        auto bad = obs;
        if (scenario == 0) {
            bad.module_defaults["提丰"].observation.operator_id = "wrong";
        }
        if (scenario == 1) {
            bad.module_defaults["提丰"].observation.code = "SIEX";
        }
        if (scenario == 2) {
            bad.module_defaults["提丰"].observation.level = 4;
        }
        if (scenario == 3) {
            bad.module_defaults["提丰"].evidence_file = output / L"missing.png";
        }
        ck(!merge(bad, baseline), "bad owner/code/stage/missing evidence fails typed merge");
    }
    std::cout << "MODULE_DEFAULT checks=" << checks << " failures=" << failures
              << " device_inputs=0 auto_setting_proven=false\n";
    return failures ? 1 : 0;
}
