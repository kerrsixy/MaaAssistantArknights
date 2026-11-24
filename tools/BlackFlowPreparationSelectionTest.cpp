#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

#include "AsstCaller.h"
#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/TaskData.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Task/Roguelike/RoguelikePreparationTask.h"
#include "Vision/Roguelike/RoguelikeSkillSelectionImageAnalyzer.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 6) {
        return 2;
    }
    using namespace asst;
    const std::filesystem::path resource_root(argv[1]), catalog_path(argv[2]), image_path(argv[3]), output(argv[4]),
        override_path(argv[5]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(resource_root.native()).c_str()) || !RoguelikeTraining.load(catalog_path)) {
        return 3;
    }
    if (override_path != L"none" && !Task.load(override_path)) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto check = [&](bool value, const std::string& message) {
        ++checks;
        if (!value) {
            ++failures;
            std::cout << "FAIL " << message << '\n';
        }
    };
    const auto frame = MAA_NS::imread(image_path);
    RoguelikeSkillSelectionImageAnalyzer names(frame);
    check(names.analyze(), "actual failed preparation names");
    const auto move = Task.get("RoguelikeSkillSelectionMove1")->rect_move;
    const auto size = Task.get("BlackFlowPreparationSkillAnchor")->rect_move;
    std::vector<PreparationCardAnchor> cards;
    for (const auto& [name, skills] : names.get_result()) {
        if (!skills.empty()) {
            cards.push_back({ name, skills.front().move({ -move.x, -move.y, size.width, size.height }) });
        }
    }
    const auto observations =
        RoguelikePreparationAnalyzer(frame).analyze(cards, PreparationPage::Skills, PreparationReadMode::SelectionOnly);
    check(observations.size() == 4, "actual failed preparation has four operators");
    const std::map<std::string, int> expected {
        { "维什戴尔", 3 },
        { "晓歌", 1 },
        { "机械师", 2 },
        { "卡达", 2 },
    };
    json::array observed;
    for (const auto& oper : observations) {
        const auto found = expected.find(oper.name);
        check(found != expected.end() && oper.selected_skill == found->second, "actual selected skill " + oper.name);
        observed.push_back(oper.to_json());
    }
    auto ambiguous = frame.clone();
    const auto ew = std::ranges::find(cards, std::string("维什戴尔"), &PreparationCardAnchor::name);
    if (ew == cards.end()) {
        return 4;
    }
    const auto marker = ew->flag.move(Task.get("BlackFlowPreparationSelected")->rect_move);
    ambiguous(make_rect<cv::Rect>(marker)).setTo(cv::Scalar::all(255));
    const auto ambiguous_observations = RoguelikePreparationAnalyzer(ambiguous).analyze(
        { *ew },
        PreparationPage::Skills,
        PreparationReadMode::SelectionOnly);
    check(
        ambiguous_observations.size() == 1 && ambiguous_observations[0].selected_skill == -1,
        "two candidate selection borders remain ambiguous");

    check(RoguelikeTraining.unique_identity("阿米娅").empty(), "partial catalog cannot prove caster identity");
    check(!RoguelikeTraining.maximum_level("阿米娅", 2), "incomplete form coverage cannot prove a level cap");
    check(!RoguelikeTraining.maximum_base_skill("阿米娅"), "incomplete form coverage cannot prove a base skill cap");
    check(!RoguelikeTraining.maximum_training("阿米娅", 3), "incomplete form coverage cannot prove mastery cap");
    check(
        RoguelikeTraining.skill_availability("阿米娅", 3, 2, 80) == SkillAvailability::Unknown,
        "incomplete form coverage cannot prove third-skill availability");
    RoguelikeStatus status;
    PreparationOperatorObservation amiya;
    amiya.name = "阿米娅";
    amiya.anchor_verified = true;
    amiya.elite = 2;
    amiya.level = 80;
    commit_preparation_observations(status, cv::Mat {}, { amiya }, PreparationPage::Skills);
    check(!status.opers.contains(amiya.name), "ambiguous real form cannot become confirmed preparation");
    const auto rules = json::open(catalog_path);
    if (!rules) {
        return 5;
    }
    std::size_t checked_names = 0, incorrect = 0, unique = 0;
    for (const auto& [name, list] : rules->at("names").as_object()) {
        auto actual_ids = BattleData.get_ids(battle::Role::Unknown, name);
        std::erase_if(actual_ids, [](const auto& id) { return !id.starts_with("char_"); });
        const auto& source_ids = list.as_array();
        const bool proved_unique =
            source_ids.size() == 1 && actual_ids.size() == 1 && source_ids[0].as_string() == actual_ids[0];
        const auto identified = RoguelikeTraining.unique_identity(name);
        incorrect += proved_unique ? identified != actual_ids[0] : !identified.empty();
        unique += !identified.empty();
        ++checked_names;
    }
    check(incorrect == 0, "all catalog names agree with the full character identity census");
    check(unique > 0, "identity guard does not reject every valid operator");
    std::ofstream(output / "selection_probe.json") << json::value(
        json::object {
            { "checks", checks },
            { "failures", failures },
            { "operators", observed },
            { "catalog_names_checked", checked_names },
            { "unexpected_identity_results", incorrect },
            { "unique_operator_names", unique },
            { "training_config_size", sizeof(RoguelikeTrainingConfig) },
            { "roguelike_operator_size", sizeof(RoguelikeOper) },
            { "gameplay_inputs", 0 },
        });
    std::cout << "PREPARATION_SELECTION107 checks=" << checks << " failures=" << failures << " inputs=0\n";
    return failures == 0 ? 0 : 1;
}
