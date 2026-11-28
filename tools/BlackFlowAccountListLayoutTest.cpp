#include <filesystem>
#include <iostream>
#include <set>

#include "AsstCaller.h"
#include "Config/Roguelike/BlackFlow/BlackFlowAccountScanCatalog.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Task/Roguelike/BlackFlow/BlackFlowAccountRefreshTask.h"
#include "Vision/Oper/OperBoxImageAnalyzer.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowAccountListDisplayAnalyzer.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowModulePanelAnalyzer.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path resources(argv[1]), fixtures(argv[2]), output(argv[3]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(resources.native()).c_str())) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto check = [&](bool value, const char* reason) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << reason << '\n';
        }
    };
    const auto scan = fixtures / L"b_priority_scan_only_03";
    OperBoxInfo damaged_list;
    damaged_list.own = true;
    damaged_list.elite = -1;
    damaged_list.level = 9;
    check(
        account_scan_should_open_detail(damaged_list, true),
        "exact mandatory target bypasses unreadable list strength and reaches strict detail verification");
    check(
        !account_scan_should_open_detail(damaged_list, false),
        "ordinary list unknown does not become proved training");
    damaged_list.own = false;
    check(!account_scan_should_open_detail(damaged_list, true), "required identity does not invent account ownership");
    damaged_list.own = true;
    damaged_list.elite = 2;
    damaged_list.level = 1;
    check(account_scan_should_open_detail(damaged_list, false), "ordinary E2 level reset remains observable");
    for (const auto& [file, expected] :
         { std::pair { L"display_mode_before_fix.png", AccountListBatchEdit::Disabled },
           std::pair { L"display_mode_after_toggle.png", AccountListBatchEdit::Enabled },
           std::pair { L"display_mode_off_verified.png", AccountListBatchEdit::Disabled } }) {
        const auto image = MAA_NS::imread(scan / file);
        const auto result = BlackFlowAccountListDisplayAnalyzer(image).analyze();
        check(result && result->batch_edit == expected, "actual same-page rook and batch-label state");
    }
    check(
        !BlackFlowAccountListDisplayAnalyzer(cv::Mat::zeros(720, 1280, CV_8UC3)).analyze(),
        "blank frame cannot authorize a toggle");
    const auto image = MAA_NS::imread(scan / L"display_mode_off_verified.png");
    OperBoxImageAnalyzer analyzer(image);
    check(analyzer.analyze(), "actual disabled-state operator list is recognized");
    check(
        analyzer.get_result().size() >= 14 && analyzer.get_result().size() <= 16,
        "native current-list page has fourteen to sixteen recognized cards");
    std::set<std::string> ids;
    for (const auto& oper : analyzer.get_result()) {
        std::cout << "CARD " << oper.name << " elite=" << oper.elite << " level=" << oper.level
                  << " rect=" << oper.rect.x << ',' << oper.rect.y << '\n';
        check(
            oper.own && oper.elite == 2 && oper.level == 90,
            "every admitted card in actual all-E2/90 fixture keeps exact strength");
        check(ids.insert(oper.id).second, "role and fallback observations cannot duplicate the same card");
    }
    const auto catalog =
        BlackFlowAccountScanCatalog::load(resources / L"resource/roguelike/BlackFlow/account_scan_catalog.json");
    check(catalog.has_value(), "module catalog loads");
    if (catalog) {
        const auto target = catalog->find_target("提丰", "uniequip_004_typhon", 3);
        check(target.has_value(), "Typhon maps to its exact required limited module");
        if (target) {
            const auto module_image = MAA_NS::imread(fixtures / L"typhon_module_default_verified.png");
            const auto observed = BlackFlowModulePanelAnalyzer(module_image).analyze(target->get().panel);
            check(
                observed && observed->evidence.id == "uniequip_004_typhon" && observed->evidence.unlocked &&
                    observed->evidence.verified && observed->evidence.level == 3,
                "fresh actual Typhon panel proves exact limited slot MAX3");
            auto wrong = target->get().panel;
            wrong.display_title = "号角特限证章";
            check(
                !BlackFlowModulePanelAnalyzer(module_image).analyze(wrong),
                "another operator title cannot reuse Typhon panel evidence");
        }
    }
    std::cout << "ACCOUNT_LIST_LAYOUT checks=" << checks << " failures=" << failures
              << " device_inputs=0 automated_toggle_verified=false live_scan_verified=false\n";
    return failures ? 1 : 0;
}
