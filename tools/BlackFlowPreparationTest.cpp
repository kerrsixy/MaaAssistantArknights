#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

#include "AsstCaller.h"
#include "Config/TaskData.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "MaaUtils/NoWarningCV.hpp"
#ifndef PREPARATION_READER_ONLY
#include "Task/Roguelike/RoguelikeOperEvidence.h"
#include "Task/Roguelike/RoguelikePreparationTask.h"
#endif
#include "Vision/Roguelike/RoguelikeFormationImageAnalyzer.h"
#include "Vision/Roguelike/RoguelikePreparationAnalyzer.h"
#include "Vision/Roguelike/RoguelikeSkillSelectionImageAnalyzer.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 5) {
        return 2;
    }
    using namespace asst;
    const std::filesystem::path repo(argv[1]), candidate(argv[2]), evidence(argv[3]), output(argv[4]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str()) ||
        !Task.load(candidate / "resource/tasks/Roguelike/BlackFlow.json") ||
        !RoguelikeTraining.load(candidate / "resource/roguelike/BlackFlow/operator_training.json")) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto check = [&](bool condition, const std::string& detail) {
        ++checks;
        if (!condition) {
            ++failures;
            std::cout << "FAIL " << detail << '\n';
        }
    };
    check(RoguelikeTraining.operator_count() == 467, "catalog actual source count");
    check(RoguelikeTraining.skill_availability("维什戴尔", 3, 1, 80) == SkillAvailability::Locked, "E1 EW S3 locked");
    check(
        RoguelikeTraining.skill_availability("维什戴尔", 3, 2, 1) == SkillAvailability::Available,
        "E2 LV1 is not E1");
    check(RoguelikeTraining.skill_availability("卡达", 3, 1, 60) == SkillAvailability::Absent, "Carda has no S3");
    check(
        RoguelikeTraining.skill_availability("unknown", 1, 2, 90) == SkillAvailability::Unknown,
        "unlisted form stays unknown");
    check(
        RoguelikeTraining.skill_availability("维什戴尔", 3, -1, -1) == SkillAvailability::Unknown,
        "unknown phase not unlocked");
    check(RoguelikeTraining.maximum_level("维什戴尔", 2) == 90, "metadata cap not observed level");
    check(RoguelikeTraining.unique_identity("暮落").empty(), "same-name forms are ambiguous");
    check(
        RoguelikeTraining.skill_availability("暮落", 2, 2, 80) == SkillAvailability::Unknown,
        "ambiguous form skill count stays unknown");
    auto unlock_fixture = json::open(candidate / "resource/roguelike/BlackFlow/operator_training.json", true, true);
    if (!unlock_fixture) {
        return 3;
    }
    auto& slot = (*unlock_fixture)["operators"]["char_1035_wisdel"]["skills"][std::size_t { 0 }];
    slot["unlock_elite"] = 0;
    slot["unlock_level"] = 10;
    const auto fixture_path = output / "unlock_fixture.json";
    {
        std::ofstream stream(fixture_path);
        stream << *unlock_fixture;
    }
    check(RoguelikeTraining.load(fixture_path), "synthetic unlock-level fixture parses");
    check(
        RoguelikeTraining.skill_availability("维什戴尔", 1, 1, 1) == SkillAvailability::Available,
        "higher phase retains an earlier level unlock after level reset");
    check(
        RoguelikeTraining.skill_availability("维什戴尔", 1, 1, -1) == SkillAvailability::Available,
        "higher phase independently proves earlier unlock");
    check(
        RoguelikeTraining.skill_availability("维什戴尔", 1, 0, 1) == SkillAvailability::Locked,
        "same phase below unlock level stays locked");
    check(
        RoguelikeTraining.skill_availability("维什戴尔", 1, -1, 1) == SkillAvailability::Unknown,
        "unknown phase low level is not proof of a locked earlier skill");
    check(
        RoguelikeTraining.load(candidate / "resource/roguelike/BlackFlow/operator_training.json"),
        "restore actual source before UI and state tests");
#ifndef PREPARATION_READER_ONLY
    PreparationOperatorObservation planning;
    planning.availability = { SkillAvailability::Available, SkillAvailability::Available, SkillAvailability::Locked };
    check(choose_preparation_skill(3, 2, planning) == 2, "locked preferred chooses available alternate");
    planning.availability[2] = SkillAvailability::Unknown;
    check(choose_preparation_skill(3, 2, planning) == 3, "unknown not silently classified locked");
    planning.availability.fill(SkillAvailability::Absent);
    check(choose_preparation_skill(3, 2, planning) == 0, "no available skill cannot issue invalid slot");
#endif

    json::array pages;
    for (int number : { 9, 253 }) {
        const auto path = evidence / (number == 9 ? "screen_00009.png" : "screen_00253.png");
        const auto image = MAA_NS::imread(path);
        if (image.empty()) {
            return 4;
        }
        std::vector<PreparationCardAnchor> cards;
        const auto page = number == 9 ? PreparationPage::Skills : PreparationPage::Formation;
        if (number == 9) {
            RoguelikeSkillSelectionImageAnalyzer names(image);
            check(names.analyze(), "actual skill page names");
            const auto move = Task.get("RoguelikeSkillSelectionMove1")->rect_move;
            const auto size = Task.get("BlackFlowPreparationSkillAnchor")->rect_move;
            for (const auto& [name, skills] : names.get_result()) {
                if (!skills.empty()) {
                    cards.push_back({ name, skills.front().move({ -move.x, -move.y, size.width, size.height }) });
                }
            }
        }
        else {
            RoguelikeFormationImageAnalyzer names(image);
            check(names.analyze(), "actual formation names");
            const auto move = Task.get("BlackFlowPreparationFormationAnchor")->rect_move;
            for (const auto& oper : names.get_result()) {
                cards.push_back({ oper.name, oper.rect.move(move) });
            }
        }
        RoguelikePreparationAnalyzer reader(image);
        const auto full_start = std::chrono::steady_clock::now();
        const auto observations = reader.analyze(cards, page);
        const auto full_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - full_start)
                .count();
        check(observations.size() == (number == 9 ? 3 : 7), "actual page card count");
        const std::map<std::string, std::pair<int, int>> expected {
            { "维什戴尔", { 2, 90 } }, { "机械师", { 1, 80 } },   { "卡达", { 1, 60 } },         { "白雪", { 1, 60 } },
            { "晓歌", { 1, 70 } },     { "史都华德", { 1, 55 } }, { "纯烬艾雅法拉", { 1, 80 } },
        };
        json::array rows;
#ifndef PREPARATION_READER_ONLY
        RoguelikeStatus status;
#endif
        for (const auto& observed : observations) {
            const auto found = expected.find(observed.name);
            check(found != expected.end(), "ground truth name " + observed.name);
            if (found == expected.end()) {
                continue;
            }
            check(observed.anchor_verified, "LV anchor " + observed.name);
            check(observed.elite == found->second.first, "actual elite " + observed.name);
            check(observed.level == found->second.second, "actual level " + observed.name);
            if (observed.name == "维什戴尔") {
                for (int value : observed.displayed_skill_levels) {
                    check(value == 10, "actual EW displayed M3");
                }
            }
            else {
                check(observed.displayed_skill_levels[0] == 7, "actual base skill7 " + observed.name);
                if (observed.name != "史都华德") {
                    check(observed.displayed_skill_levels[1] == 7, "actual second base skill7 " + observed.name);
                }
                check(
                    observed.displayed_skill_levels[2] == -1,
                    "absent or locked third slot not false rank1 " + observed.name);
            }
            if (number == 9) {
                check(
                    observed.selected_skill == (observed.name == "维什戴尔" ? 3 : 2),
                    "actual selected skill " + observed.name);
            }
            else {
                check(observed.selected_skill == -1, "blue formation overlay not selection evidence " + observed.name);
            }
            rows.push_back(observed.to_json());
        }
#ifndef PREPARATION_READER_ONLY
        auto receipt = commit_preparation_observations(status, image, observations, page);
        check(status.opers.size() == observations.size(), "commit all current cards");
        check(
            status.opers.at("维什戴尔").elite == 2 && status.opers.at("维什戴尔").level == 90,
            "runtime status consumes observed training");
        check(receipt.get("frame_saved", false), "raw source frame persisted");
        const auto training_sequence = status.opers.at("维什戴尔").preparation.training_sequence;
#endif
        const auto selection_start = std::chrono::steady_clock::now();
        const auto selection_only = reader.analyze(cards, page, PreparationReadMode::SelectionOnly);
        const auto selection_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - selection_start)
                .count();
        for (const auto& selected : selection_only) {
            check(
                !selected.training_observed && selected.elite == -1 && selected.level == -1,
                "selection confirmation does not reobserve training");
            check(
                selected.displayed_skill_levels == std::array<int, 3> { -1, -1, -1 },
                "selection confirmation skips badge OCR");
            if (number == 9) {
                check(
                    selected.selected_skill == (selected.name == "维什戴尔" ? 3 : 2),
                    "fast actual selected skill " + selected.name);
            }
        }
#ifndef PREPARATION_READER_ONLY
        commit_preparation_observations(status, image, selection_only, page);
        check(
            status.opers.at("维什戴尔").preparation.displayed_skill_levels[2] == 10,
            "selection-only preserves prior training evidence");
        check(
            status.opers.at("维什戴尔").preparation.training_sequence == training_sequence,
            "selection-only keeps original training provenance");
        check(
            status.opers.at("维什戴尔").preparation.selection_sequence > training_sequence,
            "selection and training sources remain distinct");
#endif
        const cv::Mat black = cv::Mat::zeros(image.size(), image.type());
        RoguelikePreparationAnalyzer absent(black);
        const auto unknown = absent.analyze(cards, page);
        for (const auto& observed : unknown) {
            check(
                !observed.anchor_verified && observed.elite == -1 && observed.level == -1 &&
                    observed.selected_skill == -1,
                "unobservable stays unknown");
        }
#ifndef PREPARATION_READER_ONLY
        commit_preparation_observations(status, black, unknown, page);
        check(
            status.opers.at("维什戴尔").preparation.selected_skill == -1,
            "new unobserved selection invalidates old selection");
        check(
            status.opers.at("维什戴尔").preparation.displayed_skill_levels[2] == -1,
            "new unobserved badge invalidates old badge");
        check(status.opers.at("维什戴尔").elite == 2, "unknown frame does not overwrite observed phase");
#endif
        pages.emplace_back(
            json::object { { "frame", MAA_NS::from_u16(path.native()) },
                           { "operators", rows },
                           { "full_training_ms", full_ms },
                           { "selection_only_ms", selection_ms } });
    }
#ifndef PREPARATION_READER_ONLY
    RoguelikeStatus fresh;
    PreparationOperatorObservation low;
    low.name = "维什戴尔";
    low.anchor_verified = true;
    low.elite = 2;
    low.level = 1;
    commit_preparation_observations(fresh, cv::Mat {}, { low }, PreparationPage::Skills);
    check(
        fresh.opers.at(low.name).elite == 2 && fresh.opers.at(low.name).level == 1,
        "E2 LV1 preserved without max filling");
    fresh.opers.at(low.name).preparation.selected_skill = 3;
    merge_current_run_operator(fresh.opers, low.name, 1, 80);
    check(fresh.opers.at(low.name).preparation.selected_skill == -1, "phase change invalidates preparation");
    merge_current_run_operator(fresh.opers, low.name, 2, -1);
    check(fresh.opers.at(low.name).level == 0, "phase change without new level invalidates previous phase level");
    merge_current_run_operator(fresh.opers, low.name, 1, 60);
    low.elite = -1;
    low.level = 90;
    commit_preparation_observations(fresh, cv::Mat {}, { low }, PreparationPage::Skills);
    check(
        fresh.opers.at(low.name).elite == 1 && fresh.opers.at(low.name).level == 60,
        "unknown phase level90 cannot create E1LV90");
    low.availability = { SkillAvailability::Available, SkillAvailability::Available, SkillAvailability::Unknown };
    check(
        choose_preparation_skill(3, 2, low, &fresh.opers.at(low.name)) == 2,
        "accepted E1 evidence prevents clicking locked S3 after missed phase");
    low.level = -1;
    low.selected_skill = 3;
    low.availability[2] = SkillAvailability::Available;
    const auto conflict = commit_preparation_observations(fresh, cv::Mat {}, { low }, PreparationPage::Skills);
    check(
        fresh.opers.at(low.name).preparation.selected_skill == -1,
        "fresh S3 vs cached E1 is not a consistent selection");
    check(
        conflict.at("operators").at(0).get("phase_vs_selected_skill_conflict", false),
        "phase and selection conflict has an explicit receipt");
    low.elite = 2;
    low.level = 1;
    commit_preparation_observations(fresh, cv::Mat {}, { low }, PreparationPage::Skills);
    check(
        fresh.opers.at(low.name).elite == 2 && fresh.opers.at(low.name).level == 1 &&
            fresh.opers.at(low.name).preparation.selected_skill == 3,
        "fresh phase proof resolves the conflicting selection");
    low.name = "暮落";
    low.elite = 2;
    low.level = 80;
    commit_preparation_observations(fresh, cv::Mat {}, { low }, PreparationPage::Skills);
    check(!fresh.opers.contains(low.name), "ambiguous form cannot become confirmed training");
#endif
    std::ofstream(output / "preparation_probe.json") << json::value(
        json::object {
            { "checks", checks },
            { "failures", failures },
            { "device_inputs", 0 },
            { "pages", pages },
        });
    std::cout << "PREPARATION106 checks=" << checks << " failures=" << failures << " inputs=0\n";
    return failures == 0 ? 0 : 1;
}
