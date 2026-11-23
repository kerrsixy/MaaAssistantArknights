#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include "AsstCaller.h"
#include "Config/TaskData.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Task/BattleHelper.h"

class SkillProbe : public asst::AbstractTask, public asst::BattleHelper
{
public:
    SkillProbe() :
        AbstractTask({}, nullptr, "SkillButton80Probe"),
        BattleHelper(nullptr)
    {
    }

    using BattleHelper::classify_skill_button;
    using BattleHelper::dispatch_skill_button;
    using BattleHelper::record_automatic_skill_result;
    using BattleHelper::skill_button_click_allowed;
    using BattleHelper::SkillButtonIntent;
    using BattleHelper::SkillButtonState;
    using BattleHelper::SkillInputResult;

    bool geometry() { return calc_tiles_info("光荣远征"); }

    cv::Mat top(const cv::Mat& image) { return get_top_view(image); }

    bool running(const cv::Mat& image) { return check_pause_button(image); }

protected:
    asst::AbstractTask& this_task() override { return *this; }

    bool _run() override { return false; }
};

int Checks = 0;

void require(bool passed, const std::string& description)
{
    if (!passed) {
        std::cerr << "FAIL " << description << std::endl;
        std::_Exit(86);
    }
    ++Checks;
    std::cout << "PASS " << description << '\n';
}

int wmain(int argc, wchar_t** argv)
{
#ifdef _MSC_VER
    _CrtSetReportHook([](int, char* message, int*) {
        std::cerr << "CRT " << message << std::endl;
        std::_Exit(86);
        return 1;
    });
#endif
    if (argc != 4) {
        return 2;
    }
    const std::filesystem::path repo(argv[1]), root(argv[2]), output(argv[3]);
    std::filesystem::create_directories(output);
    require(AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()), "isolated user directory");
    require(AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str()), "real native resource load without device");
    require(asst::Task.load(root / "tasks_with_candidate.json"), "candidate task definitions loaded by native parser");
    SkillProbe probe;
    require(probe.geometry(), "actual Glory geometry");
    using State = SkillProbe::SkillButtonState;
    using Intent = SkillProbe::SkillButtonIntent;
    using Result = SkillProbe::SkillInputResult;
    require(SkillProbe::skill_button_click_allowed(State::Ready, Intent::ActivateOnly), "Ready permits activation");
    require(!SkillProbe::skill_button_click_allowed(State::Active, Intent::ActivateOnly), "Active vetoes activation");
    require(SkillProbe::skill_button_click_allowed(State::Active, Intent::StopOnly), "Active permits explicit stop");
    require(!SkillProbe::skill_button_click_allowed(State::Ready, Intent::StopOnly), "Ready vetoes stop");
    require(!SkillProbe::skill_button_click_allowed(State::Unknown, Intent::ActivateOnly), "unknown cannot activate");
    require(!SkillProbe::skill_button_click_allowed(State::Unknown, Intent::StopOnly), "unknown cannot stop");
    int click_count = 0, close_count = 0;
    const auto click = [&]() {
        ++click_count;
        return true;
    };
    const auto close = [&]() {
        ++close_count;
        return true;
    };
    auto result = SkillProbe::dispatch_skill_button(State::Active, Intent::ActivateOnly, true, click, close);
    require(
        result == Result::AlreadyActive && click_count == 0 && close_count == 1,
        "active auto preservation closes owned panel once with zero skill inputs");
    result = SkillProbe::dispatch_skill_button(State::Ready, Intent::ActivateOnly, true, click, close);
    require(
        result == Result::InputIssued && click_count == 1 && close_count == 1,
        "ready activation dispatches exactly one input");
    result = SkillProbe::dispatch_skill_button(State::Active, Intent::StopOnly, true, click, close);
    require(result == Result::InputIssued && click_count == 2, "explicit stop remains a separate positive input");
    result = SkillProbe::dispatch_skill_button(State::Ready, Intent::StopOnly, true, click, close);
    require(
        result == Result::AlreadyInactive && click_count == 2 && close_count == 2,
        "repeated pure stop never reactivates an already ready skill");
    result = SkillProbe::dispatch_skill_button(State::Active, Intent::ActivateOnly, false, click, close);
    require(
        result == Result::Interrupted && click_count == 2 && close_count == 2,
        "user pause or exit permits neither skill input nor selection cleanup");
    result = SkillProbe::dispatch_skill_button(State::Unknown, Intent::ActivateOnly, true, click, close);
    require(result == Result::Failed && click_count == 2 && close_count == 2, "unknown state has no premature input");
    result = SkillProbe::dispatch_skill_button(State::Ready, Intent::ActivateOnly, true, []() { return false; }, close);
    require(result == Result::Failed && close_count == 3, "rejected controller input not reported issued");
    result =
        SkillProbe::dispatch_skill_button(State::Active, Intent::ActivateOnly, true, click, []() { return false; });
    require(
        result == Result::Failed && click_count == 2,
        "panel cleanup failure cannot pretend successful active preservation");
    auto now = std::chrono::steady_clock::now();
    auto last_poll = now;
    auto usage = asst::battle::SkillUsage::Times;
    int times = 2, errors = 0;
    require(
        SkillProbe::record_automatic_skill_result(Result::InputIssued, usage, times, errors, last_poll, now, false) &&
            times == 1 && usage == asst::battle::SkillUsage::Times,
        "actual issued activation consumes one Times allowance");
    for (int index = 0; index < 20; ++index) {
        SkillProbe::record_automatic_skill_result(Result::AlreadyActive, usage, times, errors, last_poll, now, false);
    }
    require(
        times == 1 && errors == 0 && last_poll == now && usage == asst::battle::SkillUsage::Times,
        "twenty active observations consume no Times or retries and schedule no15s backoff");
    SkillProbe::record_automatic_skill_result(Result::InputIssued, usage, times, errors, last_poll, now, false);
    require(
        times == 0 && usage == asst::battle::SkillUsage::TimesUsed,
        "second real activation exhausts exactly two Times");
    times = 2;
    errors = 2;
    last_poll = now;
    SkillProbe::record_automatic_skill_result(Result::Interrupted, usage, times, errors, last_poll, now, false);
    require(times == 2 && errors == 2 && last_poll == now, "interruption changes no accounting");
    SkillProbe::record_automatic_skill_result(Result::Failed, usage, times, errors, last_poll, now, false);
    require(
        times == 2 && errors == 0 && last_poll == now + std::chrono::seconds(15),
        "genuine third failure retains bounded legacy error backoff without consuming Times");
    const auto fixtures = json::open(root / "fixtures.json");
    require(fixtures && fixtures->is_array(), "frozen actual image index");
    json::array results;
    for (const auto& fixture : fixtures->as_array()) {
        const auto image = MAA_NS::imread(asst::utils::path(fixture.at("path").as_string()));
        require(!image.empty(), "actual frame loaded:" + fixture.at("id").as_string());
        const auto state = SkillProbe::classify_skill_button(probe.top(image));
        const auto expected = fixture.at("expected").as_string();
        const bool correct = expected == "Ready"    ? state == State::Ready
                             : expected == "Active" ? state == State::Active
                                                    : state == State::Unknown;
        require(correct, "actual selected state:" + fixture.at("id").as_string());
        const bool running = probe.running(image);
        require(running, "selected skill panel remains permitted running context:" + fixture.at("id").as_string());
        results.emplace_back(
            json::object {
                { "id", fixture.at("id") },
                { "running_guard", running },
                { "state",
                  state == State::Ready    ? "Ready"
                  : state == State::Active ? "Active"
                                           : "Unknown" },
                { "activate_input_permitted", SkillProbe::skill_button_click_allowed(state, Intent::ActivateOnly) },
                { "stop_input_permitted", SkillProbe::skill_button_click_allowed(state, Intent::StopOnly) } });
    }
    const auto guards = json::open(root / "guard_fixtures.json");
    require(guards && guards->is_array(), "actual paused and lobby negative fixture index");
    for (const auto& fixture : guards->as_array()) {
        const auto image = MAA_NS::imread(asst::utils::path(fixture.at("path").as_string()));
        require(
            !image.empty() && !probe.running(image),
            "actual paused/nonbattle rejects input:" + fixture.at("id").as_string());
    }
    const auto active_image =
        MAA_NS::imread(asst::utils::path((*fixtures)[fixtures->as_array().size() - 1].at("path").as_string()));
    require(
        asst::Task.lazy_parse(
            json::object { { "BattleSkillActivateOnClick-TopView",
                             json::object { { "baseTask", "BattleSkillReadyOnClick-TopView" },
                                            { "template", "BattleSkillStopOnClick-TopView.png" },
                                            { "templThreshold", .8 } } } }),
        "ambiguity test uses same actual STOP template as both positive signals");
    require(
        SkillProbe::classify_skill_button(probe.top(active_image)) == State::Active,
        "simultaneous Ready and Stop positives preserve active rather than activate");
    require(SkillProbe::classify_skill_button(cv::Mat {}) == State::Unknown, "empty image unknown");
    require(
        SkillProbe::classify_skill_button(cv::Mat(500, 500, CV_8UC1)) == State::Unknown,
        "wrong pixel type unknown");
    std::ofstream stream(output / "native_result.json", std::ios::binary);
    stream << json::value(
                  json::object { { "checks", Checks },
                                 { "results", results },
                                 { "device_inputs", 0 },
                                 { "engine_transition_verified", false } })
                  .to_string()
           << '\n';
    std::cout << "PASS_TOTAL " << Checks << " device_inputs=0" << std::endl;
    return 0;
}
