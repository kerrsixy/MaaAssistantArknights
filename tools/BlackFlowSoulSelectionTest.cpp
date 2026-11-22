#include <filesystem>
#include <iostream>

#include "AsstCaller.h"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/TaskData.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Task/AbstractTask.h"
#include "Task/BattleHelper.h"
#include "Utils/Platform.hpp"
#include "Vision/Matcher.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowSoulObserver.h"

class SelectionProbe : public asst::AbstractTask, public asst::BattleHelper
{
public:
    SelectionProbe() :
        AbstractTask({}, nullptr, "SelectionProbe"),
        BattleHelper(nullptr)
    {
    }

    using BattleHelper::analyze_detail_page_oper_name;

protected:
    asst::AbstractTask& this_task() override { return *this; }

    bool _run() override { return false; }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    AsstSetUserDir(MAA_NS::from_u16(std::wstring(argv[3])).c_str());
    if (!AsstLoadResource(MAA_NS::from_u16(std::wstring(argv[1])).c_str())) {
        return 3;
    }
    const auto fixtures = json::open(std::filesystem::path(argv[2]));
    if (!fixtures) {
        return 4;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool passed, const std::string& name) {
        ++checks;
        if (!passed) {
            ++failures;
            std::cerr << "FAIL " << name << '\n';
        }
    };
    SelectionProbe probe;
    asst::blackflow::BlackFlowSoulObserver observer;
    std::string error;
    if (!observer.load(std::filesystem::path(argv[2]).parent_path() / "assets", &error)) {
        std::cerr << error << '\n';
        return 5;
    }
    for (const auto& entry : fixtures->as_array()) {
        const auto image = MAA_NS::imread(asst::utils::path(entry.at("path").as_string()));
        const auto name = probe.analyze_detail_page_oper_name(image, asst::battle::Role::Unknown);
        const auto ids = asst::BattleData.get_ids(asst::battle::Role::Drone, name);
        const bool soul = name == "魂灵之影" && ids == std::vector<std::string> { "token_10035_wisdel_wward" };
        asst::Matcher retreat(image);
        retreat.set_task_info("BattleOperRetreat");
        const auto button = retreat.analyze();
        const bool qualified_button = observer.retreat_button(image, { 615, 230 });
        std::cout << entry.at("label").as_string() << " name=" << name << " soul=" << soul
                  << " legacy_button=" << static_cast<bool>(button) << " qualified_button=" << qualified_button
                  << " score=" << (button ? button->score : -1) << '\n';
        expect(soul == entry.at("soul").as_boolean(), entry.at("label").as_string() + " exact identity");
        expect(qualified_button == entry.at("button").as_boolean(), entry.at("label").as_string() + " button presence");
        if (soul) {
            expect(qualified_button, "selected soul has a visible retreat button");
        }
    }
    std::cout << "SOUL_SELECTION checks=" << checks << " failures=" << failures << " device_inputs=0\n";
    return failures ? 1 : 0;
}
