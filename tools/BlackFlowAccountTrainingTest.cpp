#include <filesystem>
#include <fstream>
#include <iostream>
#include <tuple>
#include <vector>

#include "AsstCaller.h"
#include "Config/Roguelike/BlackFlow/BlackFlowAccountTraining.h"
#include "Config/TaskData.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Task/Roguelike/BlackFlow/BlackFlowAccountSnapshotStore.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowAccountTrainingAnalyzer.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowDetailStrengthAnalyzer.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 5) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path resource_root(argv[1]), fixtures(argv[2]), output(argv[3]), original_cache(argv[4]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(resource_root.native()).c_str())) {
        return 3;
    }
    const auto baseline = json::open(original_cache);
    if (!baseline) {
        return 3;
    }
    const std::string uid = baseline->get("account", "uid", "");
    if (uid.empty()) {
        return 3;
    }
    const std::string other_uid = uid + "0";
    int checks = 0, failures = 0;
    const auto check = [&](bool value, const std::string& label) {
        ++checks;
        if (!value) {
            ++failures;
            std::cout << "FAIL " << label << '\n';
        }
    };
    json::array observations;
    AccountRefreshObservation refresh;
    refresh.client = "Official";
    refresh.uid = uid;
    refresh.returned_same_uid = true;
    std::map<std::string, std::filesystem::path> strength_images;
    const std::vector<std::tuple<std::string, std::string, int, int, std::array<int, 3>>> cases {
        { "char_2012_typhon", "提丰", 2, 90, { 0, 3, 3 } },   { "char_4039_horn", "号角", 2, 90, { 3, 0, 0 } },
        { "char_206_gnosis", "灵知", 2, 20, { 0, 0, 2 } },    { "char_437_mizuki", "水月", 2, 60, { 0, 0, 0 } },
        { "char_4230_mcnist", "机械师", 2, 90, { 3, 3, 3 } },
    };
    for (const auto& [id, name, elite, level, expected_mastery] : cases) {
        const auto path = fixtures / MAA_NS::to_u16(id) / L"detail-strength.png";
        const auto frame = MAA_NS::imread(path);
        const auto result = BlackFlowAccountTrainingAnalyzer(frame).analyze(id, name);
        check(result.has_value(), "verified real account detail " + name);
        if (!result) {
            continue;
        }
        check(result->elite == elite && result->level == level, "account detail training identity " + name);
        check(result->base_skill_level == 7, "actual visible account rank7 " + name);
        check(result->mastery_levels == expected_mastery, "visually reviewed account mastery " + name);
        const auto strength = BlackFlowDetailStrengthAnalyzer(frame).analyze(id, name);
        check(strength.has_value(), "training and detail strength both verify " + name);
        if (strength) {
            refresh.verified_strengths.emplace(name, FreshAccountDetailStrength { *strength, frame.clone(), *result });
            strength_images.emplace(name, path);
        }
        json::array mastery, ranks;
        for (int skill = 1; skill <= 3; ++skill) {
            mastery.emplace_back(result->mastery_levels[skill - 1]);
            const auto rank = account_skill_level_for_run(*result, skill, elite, level);
            ranks.emplace_back(rank.value_or(-1));
        }
        observations.emplace_back(
            json::object {
                { "id", id },
                { "name", name },
                { "elite", result->elite },
                { "level", result->level },
                { "base_skill_level", result->base_skill_level },
                { "mastery", mastery },
                { "account_skill_levels", ranks },
            });
        check(!BlackFlowAccountTrainingAnalyzer(frame).analyze(id, "维什戴尔"), "wrong operator identity rejected");
        cv::Mat missing_rank = frame.clone();
        const auto roi = Task.get("OperProgress@CurrentSkillLevel")->roi;
        missing_rank(make_rect<cv::Rect>(roi)).setTo(cv::Scalar::all(0));
        const auto unknown = BlackFlowAccountTrainingAnalyzer(missing_rank).analyze(id, name);
        check(unknown && unknown->base_skill_level == -1, "unobserved rank does not become1 or7");
        check(
            unknown && !account_skill_level_for_run(*unknown, 1, elite, level),
            "missing rank remains unavailable to evaluator");
        cv::Mat missing_mastery = frame.clone();
        for (int skill = 1; skill <= 3; ++skill) {
            const auto marker = Task.get("OperProgress@CurrentSkill" + std::to_string(skill) + "MasterLevel")->roi;
            missing_mastery(make_rect<cv::Rect>(marker)).setTo(cv::Scalar::all(0));
        }
        const auto unseen = BlackFlowAccountTrainingAnalyzer(missing_mastery).analyze(id, name);
        check(
            unseen && unseen->mastery_levels == std::array<int, 3> { -1, -1, -1 },
            "missing mastery marker cannot become M0 or M3");
    }
    AccountTrainingEvidence ew {
        .operator_id = "char_1035_wisdel",
        .operator_name = "维什戴尔",
        .elite = 2,
        .level = 90,
        .base_skill_level = 7,
        .mastery_levels = { 3, 3, 3 },
        .availability = { SkillAvailability::Available, SkillAvailability::Available, SkillAvailability::Available }
    };
    check(account_skill_level_for_run(ew, 2, 1, 80) == 7, "account M3 is capped at rank7 in run E1");
    check(!account_skill_level_for_run(ew, 3, 1, 80), "account M3 does not unlock run E1 skill3");
    check(account_skill_level_for_run(ew, 3, 2, 1) == 10, "run E2 low level retains observed account M3");
    check(account_skill_level_for_run(ew, 1, 0, 1) == 4, "run E0 caps an owned rank7 skill at4");
    ew.base_skill_level = 1;
    ew.mastery_levels = { 0, 0, 0 };
    check(account_skill_level_for_run(ew, 2, 1, 1) == 1, "low account skill rank remains1 before battle");
    ew.base_skill_level = 7;
    ew.mastery_levels = { -1, -1, -1 };
    check(!account_skill_level_for_run(ew, 3, 2, 1), "unknown E2 mastery is not silently M0 or M3");
    check(account_skill_level_for_run(ew, 2, 1, 80) == 7, "unknown mastery does not hide proved E1 rank7");
    ew.elite = 1;
    ew.level = 80;
    check(!account_skill_level_for_run(ew, 2, 2, 1), "account cache cannot supply higher temporary-offer phase");
    ew.elite = 2;
    ew.level = 1;
    check(!account_skill_level_for_run(ew, 2, 2, 90), "same-phase current level cannot exceed account");
    check(account_skill_level_for_run(ew, 2, 1, 80) == 7, "promotion level reset does not reject lower run phase");
    ew.operator_id = "char_2012_typhon";
    check(!account_skill_level_for_run(ew, 2, 1, 80), "different operator IDs cannot share training");
    check(
        !BlackFlowAccountTrainingAnalyzer(cv::Mat::zeros(720, 1280, CV_8UC3)).analyze("char_2012_typhon", "提丰"),
        "unrelated black image cannot prove account detail");
    check(baseline.has_value(), "frozen same-account cache exists");
    if (!baseline) {
        return 4;
    }
    const auto original = baseline->to_string();
    std::string error;
    const auto staged =
        BlackFlowAccountSnapshotStore::merge(*baseline, refresh, {}, {}, "202611250108", &error, strength_images);
    check(staged.has_value(), "real outside training merges into retained account cache");
    if (!staged) {
        std::cerr << error << '\n';
        return 4;
    }
    check(baseline->to_string() == original, "account source remains immutable");
    auto profile = BlackFlowAccountProfile::parse(*staged, "Official", uid, &error);
    check(
        profile && profile->operator_count() == baseline->at("operators").as_object().size(),
        "all cached operators retained");
    check(profile && profile->training_operator_count() == 5, "five real training sources load before exploration");
    check(
        profile && profile->skill_level_for_run("提丰", 2, 2, 90) == 10,
        "cached Typhon S2 M3 available before battle");
    check(profile && profile->skill_level_for_run("灵知", 3, 2, 20) == 9, "cached Gnosis S3 M2 remains rank9");
    check(profile && profile->skill_level_for_run("水月", 1, 2, 60) == 7, "observed M0 remains rank7");
    check(
        profile && !profile->skill_level_for_run("维什戴尔", 3, 2, 90),
        "unscanned EW is unknown instead of assumed M3");
    bool untargeted_unchanged = true;
    for (const auto& [name, value] : baseline->at("operators").as_object()) {
        if (!refresh.verified_strengths.contains(name)) {
            untargeted_unchanged &= staged->at("operators").at(name).to_string() == value.to_string();
        }
    }
    check(untargeted_unchanged, "untargeted account records are byte-equivalent JSON");
    check(
        profile && profile->module_available("提丰", "uniequip_004_typhon", 3),
        "existing exact module slot survives training merge");
    const auto local_cache = output / L"account_cache.json";
    check(
        BlackFlowAccountSnapshotStore::save_atomic(local_cache, *staged, "202611250108", &error),
        "work-only training snapshot saves atomically");
    const auto loaded = BlackFlowAccountProfile::load(local_cache, "Official", uid, &error);
    check(
        loaded && loaded->skill_level_for_run("机械师", 2, 1, 80) == 7,
        "scan-disabled load reuses mastery with E1 cap");
    check(!BlackFlowAccountProfile::load(local_cache, "Bilibili", uid, &error), "same UID on another client rejected");
    check(
        !BlackFlowAccountProfile::load(local_cache, "Official", other_uid, &error),
        "different account cannot borrow training");
    auto corrupted = *staged;
    corrupted["operators"]["提丰"]["skill_training"]["uid"] = other_uid;
    check(!BlackFlowAccountProfile::parse(corrupted, "Official", uid, &error), "nested training UID mismatch rejected");
    corrupted = *staged;
    corrupted["operators"]["提丰"]["skill_training"]["mastery_levels"] = json::array { 0, 4, 3 };
    check(
        !BlackFlowAccountProfile::parse(corrupted, "Official", uid, &error),
        "invalid mastery cannot enter cache consumer");
    corrupted = *staged;
    corrupted["operators"]["提丰"]["skill_training"]["base_skill_level"] = 1;
    check(!BlackFlowAccountProfile::parse(corrupted, "Official", uid, &error), "M3 with base rank1 is contradictory");
    corrupted = *staged;
    corrupted["operators"]["提丰"]["skill_training"]["mastery_levels"] = json::array { 0, 2.5, 3 };
    check(!BlackFlowAccountProfile::parse(corrupted, "Official", uid, &error), "fractional mastery is not truncated");
    corrupted = *staged;
    corrupted["operators"]["提丰"]["skill_training"]["level"] = 89.5;
    check(!BlackFlowAccountProfile::parse(corrupted, "Official", uid, &error), "fractional account level is rejected");
    corrupted = *staged;
    corrupted["operators"]["提丰"]["skill_training"]["base_skill_level"] = true;
    check(!BlackFlowAccountProfile::parse(corrupted, "Official", uid, &error), "boolean is not a skill rank");
    auto unreadable = refresh;
    unreadable.verified_strengths.at("提丰").training.reset();
    const auto cleared =
        BlackFlowAccountSnapshotStore::merge(*staged, unreadable, {}, {}, "202611250109", &error, strength_images);
    const auto cleared_profile =
        cleared ? BlackFlowAccountProfile::parse(*cleared, "Official", uid, &error) : std::nullopt;
    check(
        cleared_profile && cleared_profile->skill_level_for_run("提丰", 2, 2, 90) == 10,
        "fresh unreadable detail retains verified same-account mastery");
    check(
        cleared_profile && cleared_profile->training_operator_count() == 5,
        "fresh unknown does not erase account training");
    check(
        cleared && cleared->at("operators").at("提丰").at("skill_training").get("observed_at", "") == "202611250108",
        "retained cache keeps its original observation source");
    check(
        cleared && cleared->at("operators")
                       .at("提丰")
                       .at("skill_training_latest_attempt")
                       .get("retained_previous_source", false),
        "fresh unreadable attempt explicitly reports cache fallback");
    auto partial = refresh;
    partial.verified_strengths.at("提丰").training->base_skill_level = -1;
    partial.verified_strengths.at("提丰").training->mastery_levels = { -1, -1, -1 };
    const auto partial_staged =
        BlackFlowAccountSnapshotStore::merge(*staged, partial, {}, {}, "202611250111", &error, strength_images);
    const auto partial_profile =
        partial_staged ? BlackFlowAccountProfile::parse(*partial_staged, "Official", uid, &error) : std::nullopt;
    check(
        partial_profile && partial_profile->skill_level_for_run("提丰", 2, 2, 90) == 10,
        "partial OCR retains independent prior source");
    check(
        partial_staged &&
            partial_staged->at("operators").at("提丰").at("skill_training_latest_attempt").get("state", "") ==
                "partial",
        "partial fresh evidence is recorded independently from retained training");
    auto conflicting = refresh;
    auto& lower_detail = conflicting.verified_strengths.at("提丰");
    lower_detail.observation.elite = 1;
    lower_detail.observation.level = 80;
    lower_detail.observation.maximum_level = 80;
    lower_detail.training.reset();
    const auto conflict_staged =
        BlackFlowAccountSnapshotStore::merge(*staged, conflicting, {}, {}, "202611250112", &error, strength_images);
    const auto conflict_profile =
        conflict_staged ? BlackFlowAccountProfile::parse(*conflict_staged, "Official", uid, &error) : std::nullopt;
    check(
        conflict_profile && !conflict_profile->skill_level_for_run("提丰", 2, 1, 80),
        "contradicting typed detail strength invalidates prior higher-phase training");
    unreadable = refresh;
    unreadable.returned_same_uid = false;
    check(
        !BlackFlowAccountSnapshotStore::merge(*baseline, unreadable, {}, {}, "202611250110", &error, strength_images),
        "missing UID return rejects full transaction");
    const auto training_select = Task.get("BlackFlowAccountScan@TrainingSelect");
    const auto training_detail = Task.get("BlackFlowAccountScan@TrainingDetail");
    check(
        training_select && training_detail && training_detail->next.empty(),
        "detail-only scan ends before module management or training");
    check(json::open(original_cache)->to_string() == original, "all checks leave real source cache unchanged");
    std::ofstream(output / "account_training_probe.json") << json::value(
        json::object {
            { "checks", checks },
            { "failures", failures },
            { "observations", observations },
            { "gameplay_inputs", 0 },
            { "account_cache_written", false },
            { "native_cache_profile_consumer_tested", true },
            { "live_scan_extended_verified", false },
        });
    std::cout << "ACCOUNT_TRAINING108 checks=" << checks << " failures=" << failures << " inputs=0\n";
    return failures == 0 ? 0 : 1;
}
