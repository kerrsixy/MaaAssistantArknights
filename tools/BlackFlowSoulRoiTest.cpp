#include "AsstCaller.h"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Task/AbstractTask.h"
#include "Task/BattleHelper.h"
#include "Task/Roguelike/BlackFlow/BlackFlowSummonRetreat.h"
#include "Utils/Platform.hpp"
#include "Vision/Roguelike/BlackFlow/BlackFlowSoulObserver.h"
#include "test_helpers/blackflow_soul/BlackFlowSoulObserverBaseline.h"
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>

using namespace asst;
using namespace asst::blackflow;
using Clock = std::chrono::steady_clock;

double elapsed(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

class NameProbe final : public AbstractTask, public BattleHelper
{
public:
    NameProbe() :
        AbstractTask({}, nullptr, "Soul125RoiProbe"),
        BattleHelper(nullptr)
    {
    }

    using BattleHelper::analyze_detail_page_oper_name;

protected:
    AbstractTask& this_task() override { return *this; }

    bool _run() override { return false; }
};

struct Result
{
    BossBodyEvidence boss;
    std::vector<Point> souls;
    std::string name;
    bool paused = false, normal = false, glyph = false;
    double total = 0, ui = 0, boss_ms = 0, souls_ms = 0, identity_ms = 0, join_wait = 0;
};

template <class Observer>
Result measure(
    const Observer& observer,
    const cv::Mat& frame,
    const std::vector<SoulSite>& sites,
    NameProbe& name,
    bool selected,
    std::optional<Rect> roi)
{
    Result r;
    const auto start = Clock::now();
    r.paused = observer.paused(frame);
    r.normal = observer.normal_running(frame);
    r.ui = elapsed(start);
    if (!r.paused && (r.normal || selected)) {
        auto future = std::async(std::launch::async | std::launch::deferred, [&observer, frame, selected, roi] {
            const auto begin = Clock::now();
            BossBodyEvidence b;
            if constexpr (requires { observer.boss_in_roi(frame, Rect {}); }) {
                b = roi && !selected ? observer.boss_in_roi(frame, *roi) : observer.boss(frame, selected);
            }
            else {
                b = observer.boss(frame, selected);
            }
            return std::pair { b, elapsed(begin) };
        });
        auto begin = Clock::now();
        if (selected) {
            r.name = name.analyze_detail_page_oper_name(frame, battle::Role::Unknown);
            r.glyph = observer.retreat_button(frame, { 615, 230 });
            r.identity_ms = elapsed(begin);
        }
        else {
            r.souls = observer.souls(frame, sites);
            r.souls_ms = elapsed(begin);
        }
        begin = Clock::now();
        const auto [body, ms] = future.get();
        r.join_wait = elapsed(begin);
        r.boss = body;
        r.boss_ms = ms;
        if (!selected && !body.alive()) {
            r.souls.clear();
        }
    }
    r.total = elapsed(start);
    return r;
}

json::value value(const Result& r)
{
    json::array souls;
    for (const auto& p : r.souls) {
        souls.emplace_back(json::array { p.x, p.y });
    }
    return json::object {
        { "total_compute_without_capture_ms", r.total },
        { "ui_ms", r.ui },
        { "boss_ms", r.boss_ms },
        { "souls_ms", r.souls_ms },
        { "identity_ms", r.identity_ms },
        { "join_wait_ms", r.join_wait },
        { "normal", r.normal },
        { "paused", r.paused },
        { "boss_alive", r.boss.alive() },
        { "score", r.boss.score },
        { "body", json::array { r.boss.body.x, r.boss.body.y, r.boss.body.width, r.boss.body.height } },
        { "hp", json::array { r.boss.hp_fill.x, r.boss.hp_fill.y, r.boss.hp_fill.width, r.boss.hp_fill.height } },
        { "souls", souls },
        { "selected_name", r.name },
        { "glyph", r.glyph }
    };
}

bool equal(const Result& a, const Result& b)
{
    return a.normal == b.normal && a.paused == b.paused && a.boss.alive() == b.boss.alive() &&
           std::abs(a.boss.score - b.boss.score) < 1e-6 && a.boss.body == b.boss.body &&
           a.boss.hp_fill == b.boss.hp_fill && a.souls == b.souls && a.name == b.name && a.glyph == b.glyph;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3 && argc != 4) {
        return 2;
    }
    const std::filesystem::path root(argv[1]), repo(argv[2]);
    AsstSetUserDir(MAA_NS::from_u16((root / "user").wstring()).c_str());
    if (!AsstLoadResource(MAA_NS::from_u16(repo.wstring()).c_str())) {
        return 3;
    }
    BlackFlowSoulObserver observer;
    BlackFlowSoulObserverBaseline baseline;
    std::string error;
    if (!observer.load(root / "assets", &error) || !baseline.load(root / "assets", &error)) {
        return 4;
    }
    const auto config = json::open(root / "fixtures.json");
    if (!config) {
        return 5;
    }
    std::vector<SoulSite> sites;
    for (const auto& p : config->at("sites").as_array()) {
        sites.push_back(
            { { p.at("tile").at(0).as_integer(), p.at("tile").at(1).as_integer() },
              { p.at("pixel").at(0).as_integer(), p.at("pixel").at(1).as_integer() } });
    }
    NameProbe name;
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const std::string& label) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    if (argc == 4) {
        for (int i = 0; i < 3; ++i) {
            const auto& fixture = config->at("cases").at(i);
            const auto receipt = json::open(root / utils::path(fixture.at("receipt").as_string()));
            if (!receipt) {
                return 7;
            }
            const auto& d = *receipt;
            SummonRetreatObservation o;
            o.battle_key = d.at("battle_key").as_string();
            o.sequence = d.at("sequence").as_integer();
            o.observed_ms = d.at("observed_ms").as_integer();
            o.received_ms = d.at("received_ms").as_integer();
            o.normal_view = d.at("normal_view").as_boolean();
            o.owner_present = d.at("owner_present").as_boolean();
            o.user_paused = d.at("user_paused").as_boolean();
            o.boss = static_cast<BossVitalState>(d.at("boss_state").as_integer());
            std::vector<Point> allowed;
            for (const auto& p : d.at("allowed_sites").as_array()) {
                allowed.emplace_back(p.at(0).as_integer(), p.at(1).as_integer());
            }
            for (const auto& p : d.at("candidates").as_array()) {
                o.candidates.emplace_back(p.at(0).as_integer(), p.at(1).as_integer());
            }
            const Point owner(d.at("owner_tile").at(0).as_integer(), d.at("owner_tile").at(1).as_integer());
            BlackFlowSummonRetreat policy(o.battle_key, "魂灵之影", "token_10035_wisdel_wward", owner, allowed);
            check(
                o.received_ms - o.observed_ms > 500 && policy.observe(o).action == SummonRetreatAction::None &&
                    !policy.authorize_input(SummonRetreatAction::Select, o, o.received_ms, owner, std::nullopt).allowed,
                "real run73 expired Alive keeps original clock and cannot authorize input");
        }
        const auto selected = MAA_NS::imread(root / utils::path(config->at("cases").at(8).at("path").as_string()));
        const auto terminal = MAA_NS::imread(root / utils::path(config->at("cases").at(7).at("path").as_string()));
        check(
            !observer.normal_running(selected) && observer.retreat_button(selected, { 615, 230 }),
            "additional capture may reveal real selected panel; never ordinary deployment");
        const auto terminal_task = observer.battle_end_scene(terminal);
        check(
            !observer.normal_running(terminal),
            "real terminal frame is nonnormal and yields regardless of extra terminal classifier");
        std::ofstream(root / "transition_evidence.json")
            << json::value(
                   json::object {
                       { "selected_normal", observer.normal_running(selected) },
                       { "terminal_normal", observer.normal_running(terminal) },
                       { "terminal_task_recognized", terminal_task.has_value() },
                       { "terminal_task", terminal_task.value_or("") },
                       { "actual_controller_transition_executed", false },
                   })
                   .format();
        std::cout << "SOUL125_CONTRACT checks=" << checks << " failures=" << failures << " device_inputs=0\n";
        return failures ? 1 : 0;
    }
    json::array rows;
    // Two short passes, not a repeated full-corpus replay during a live run.
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& fixture : config->at("cases").as_array()) {
            const auto frame = MAA_NS::imread(root / utils::path(fixture.at("path").as_string()));
            if (frame.empty()) {
                return 6;
            }
            const auto label = fixture.at("label").as_string();
            const bool selected = fixture.at("mode").as_string() == "selected";
            const auto old = measure(baseline, frame, sites, name, selected, std::nullopt);
            const auto full = measure(observer, frame, sites, name, selected, std::nullopt);
            check(equal(old, full), label + " original full result unchanged");
            rows.emplace_back(
                json::object { { "label", label },
                               { "pass", pass },
                               { "mode", "baseline_full" },
                               { "result", value(old) } });
            rows.emplace_back(
                json::object { { "label", label },
                               { "pass", pass },
                               { "mode", "refactored_full" },
                               { "result", value(full) } });
            if (selected) {
                check(
                    !full.normal && full.name == "魂灵之影" && full.glyph,
                    "real selected Soul remains selected; normal ROI cannot own this panel");
                continue;
            }
            const auto& h = fixture.at("hint");
            const Rect body(h.at(0).as_integer(), h.at(1).as_integer(), h.at(2).as_integer(), h.at(3).as_integer());
            for (int padding : { 0, 24, 48, 96 }) {
                const Rect roi(body.x - padding, body.y - padding, body.width + 2 * padding, body.height + 2 * padding);
                const auto local = measure(observer, frame, sites, name, false, roi);
                if (fixture.at("expect_unknown").as_boolean()) {
                    check(!local.boss.alive(), label + " absent body does not inherit prior Alive");
                }
                rows.emplace_back(
                    json::object { { "label", label },
                                   { "pass", pass },
                                   { "mode", "roi" },
                                   { "padding", padding },
                                   { "hint_source", fixture.at("hint_source") },
                                   { "result", value(local) } });
            }
            check(
                !observer.boss_in_roi(frame, { 500, 300, 1, 1 }).alive(),
                "too small ROI produces Unknown without exception");
            check(!observer.boss_in_roi(frame, { 2000, 2000, 10, 10 }).alive(), "offscreen ROI produces Unknown");
        }
    }
    std::ofstream(root / "results1.json") << json::value(rows).format();
    std::cout << "SOUL125_ROI checks=" << checks << " failures=" << failures
              << " capture_included=false device_inputs=0 live_fix=false\n";
    return failures ? 1 : 0;
}
