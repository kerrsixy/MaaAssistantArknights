#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Task/Roguelike/BlackFlow/BlackFlowAccountSnapshotStore.h"
#include "Utils/Platform.hpp"

// Arguments: read-only cache.json, module-observations.json, catalog.json,
// native_store_sandbox output directory, optional repository root.
int wmain(int argc, wchar_t** argv)
{
    if (argc != 5 && argc != 6) {
        return 2;
    }
#ifdef _MSC_VER
    _CrtSetReportHook([](int type, char* message, int*) {
        if (type == _CRT_ASSERT) {
            std::fputs(message, stderr);
            std::fflush(stderr);
            std::_Exit(86);
        }
        return 0;
    });
#endif
    using namespace asst;
    using namespace asst::blackflow;
    if (argc == 6 && !BattleData.load(std::filesystem::path(argv[5]) / utils::path("resource/battle_data.json"))) {
        return 3;
    }
    const auto baseline = json::open(std::filesystem::path(argv[1]));
    const auto ledger = json::open(std::filesystem::path(argv[2]));
    const auto catalog = BlackFlowAccountScanCatalog::load(std::filesystem::path(argv[3]));
    if (!baseline || !ledger || !catalog) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool value, const char* label) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    AccountRefreshObservation observation;
    observation.client = "Official";
    observation.uid = ledger->at("account_uid").as_string();
    observation.returned_same_uid = true;
    std::vector<std::filesystem::path> images;
    for (const auto& target : catalog->targets()) {
        const auto& saved = ledger->at("operators").at(target.operator_name);
        FreshAccountModulePanel panel;
        panel.operator_name = target.operator_name;
        panel.observation.evidence = { saved.at("module_id").as_string(),
                                       static_cast<int>(saved.at("module_level").as_integer()),
                                       saved.at("unlocked").as_boolean(),
                                       true };
        panel.observation.state_text = panel.observation.evidence.unlocked ? "MAX" : "解锁";
        observation.modules.push_back(std::move(panel));
        images.push_back(utils::path(saved.at("evidence").at(0).at("file").as_string()));
    }
    const auto& targets = catalog->targets();
    std::string error;
    const auto run = [&](const AccountRefreshObservation& value, const json::value& prior) {
        return BlackFlowAccountSnapshotStore::merge(prior, value, targets, images, "20261122000001", &error);
    };
    const std::string before = baseline->to_string();
    const auto merged = run(observation, *baseline);
    expect(merged.has_value(), "actual five historical slot observations merge");
    if (!merged) {
        std::cerr << error << '\n';
        return 4;
    }
    expect(
        merged->at("operators").as_object().size() == baseline->at("operators").as_object().size(),
        "existing roster retained");
    expect(baseline->to_string() == before, "pure merge does not mutate original");
    expect(
        merged->get("modules", "last_required_scan", "module_refresh_complete", false),
        "five known incl locked complete modules only");
    expect(
        !merged->get("modules", "last_required_scan", "fresh_roster_complete", true),
        "modules cannot promote full fresh roster");
    expect(
        !merged->get("modules", "last_required_scan", "current_battle_equipment_proven", true),
        "modules do not prove current gear");
    for (const auto& [name, old] : baseline->at("operators").as_object()) {
        expect(
            merged->at("operators").at(name).at("elite") == old.at("elite") &&
                merged->at("operators").at(name).at("level") == old.at("level"),
            "module-only pass preserves each existing strength");
    }
    auto bad = observation;
    bad.uid = "another-account";
    expect(!run(bad, *baseline), "wrong UID rejected");
    bad = observation;
    bad.client = "Bilibili";
    expect(!run(bad, *baseline), "wrong client rejected");
    bad = observation;
    bad.returned_same_uid = false;
    expect(!run(bad, *baseline), "missing final UID receipt rejected");
    bad = observation;
    bad.modules[0].observation.evidence.id = "wrong-slot";
    expect(!run(bad, *baseline), "wrong module ID rejected");
    bad = observation;
    bad.modules[0].observation.evidence.level = 4;
    expect(!run(bad, *baseline), "out of range module level rejected");
    bad = observation;
    bad.modules[0].observation.evidence.verified = false;
    expect(!run(bad, *baseline), "unverified observation rejected");
    bad = observation;
    bad.modules[0].observation.evidence.unlocked = !bad.modules[0].observation.evidence.unlocked;
    expect(!run(bad, *baseline), "level and unlock inconsistency rejected");
    bad = observation;
    bad.modules[1] = bad.modules[0];
    expect(!run(bad, *baseline), "duplicate observed target rejected");
    auto extra = *baseline;
    extra["operators"]["提丰"]["module_slots"].as_array().push_back(
        json::object { { "module_id", "unrelated-preserved-slot" }, { "level", 0 }, { "verified", false } });
    const auto with_extra = run(observation, extra);
    expect(
        with_extra && with_extra->at("operators").at("提丰").at("module_slots").as_array()[1].get("module_id", "") ==
                          "unrelated-preserved-slot",
        "unrelated slot retained");
    bad = observation;
    OperBoxInfo weaker;
    weaker.id = "char_2012_typhon";
    weaker.name = "提丰";
    weaker.level = 1;
    weaker.elite = 0;
    weaker.own = true;
    bad.strengths.emplace(weaker.name, weaker);
    const auto without_downgrade = run(bad, *baseline);
    expect(
        without_downgrade && without_downgrade->at("operators").at("提丰").at("elite") ==
                                 baseline->at("operators").at("提丰").at("elite"),
        "module pass cannot downgrade old elite using grid defaults");

    if (argc == 6) {
        auto legacy = *baseline;
        legacy["operators"]["断罪者"]["id"] = "trap_059_peacokfool";
        auto repair = observation;
        OperBoxInfo character;
        character.id = "char_159_peacok";
        character.name = "断罪者";
        character.own = true;
        character.elite = 1;
        character.level = 60;
        repair.strengths.emplace(character.name, character);
        const auto repaired = run(repair, legacy);
        expect(
            repaired && repaired->at("operators").at(character.name).get("id", "") == character.id,
            "actual legacy trap alias migrates to observed canonical character");
        expect(
            repaired && repaired->at("operators").at(character.name).get("level", -1) == 0,
            "identifier repair cannot invent verified operator strength");
        expect(
            repaired && repaired->at("operators").at(character.name).at("module_slots").as_array().empty(),
            "legacy facility module claims do not transfer to character");
        expect(
            repaired && repaired->at("operators").at(character.name).at("legacy_identifier_repair").at("previous") ==
                            legacy.at("operators").at(character.name),
            "identifier repair preserves exact old entry");
        repair.strengths.at(character.name).id = "char_002_amiya";
        expect(!run(repair, legacy), "mismatched canonical character name remains rejected");
        repair.strengths.at(character.name).id = character.id;
        legacy["operators"][character.name]["id"] = "char_002_amiya";
        expect(!run(repair, legacy), "character-to-character form conflicts are not silently repaired");
        auto forms = observation;
        character.name = "阿米娅";
        character.id = "char_002_amiya";
        forms.strengths.emplace(character.name, character);
        auto prior_forms = *baseline;
        prior_forms["operators"][character.name]["id"] = "char_1037_amiya3";
        const auto with_form = run(forms, prior_forms);
        expect(
            with_form && with_form->at("operators").at(character.name).get("id", "") == "char_1037_amiya3",
            "verified alternative form cannot overwrite primary identity");
        expect(
            with_form && with_form->at("operators").at(character.name).at("observed_forms").contains(character.id),
            "same-name canonical alternative is archived by exact character ID");
        expect(
            with_form && with_form->at("operators").at(character.name).at("elite") ==
                             prior_forms.at("operators").at(character.name).at("elite"),
            "alternative form does not invent or overwrite primary strength");
    }

    auto partial = observation;
    partial.modules.pop_back();
    auto partial_images = images;
    partial_images.pop_back();
    const auto partial_result =
        BlackFlowAccountSnapshotStore::merge(*baseline, partial, targets, partial_images, "20261122000002", &error);
    expect(
        partial_result && !partial_result->get("modules", "last_required_scan", "module_refresh_complete", true),
        "partial keeps fresh completion false");
    const auto& omitted = targets.back();
    expect(
        partial_result && partial_result->at("operators").at(omitted.operator_name) ==
                              baseline->at("operators").at(omitted.operator_name),
        "unknown omitted target retains all old evidence/timestamps");

    const auto work = std::filesystem::absolute(std::filesystem::path(argv[4])).lexically_normal();
    // Never touch a cache supplied as the read-only input; output must be the
    // dedicated work folder.
    if (work.filename() != utils::path("native_store_sandbox")) {
        return 5;
    }
    const auto stamp = std::to_string(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
    const auto output = work / utils::path("snapshot-" + stamp + ".json");
    expect(
        BlackFlowAccountSnapshotStore::save_atomic(output, *baseline, stamp, &error),
        "work-only create atomic snapshot");
    expect(
        BlackFlowAccountSnapshotStore::save_atomic(output, *merged, stamp + "1", &error),
        "work-only replace snapshot");
    const auto current = json::open(output);
    expect(current && current->to_string() == merged->to_string(), "replaced snapshot rereads exact data");
    auto backup = output;
    backup += utils::path(".bak-" + stamp + "1");
    const auto preserved = json::open(backup);
    expect(preserved && preserved->to_string() == before, "old snapshot preserved as exact backup");
    expect(
        !BlackFlowAccountSnapshotStore::save_atomic(output, *baseline, "invalid/transaction", &error),
        "invalid suffix rejected before file operation");
    expect(json::open(output)->to_string() == merged->to_string(), "failed save leaves current data untouched");
    expect(json::open(std::filesystem::path(argv[1]))->to_string() == before, "source cache unchanged after all tests");
    std::cout << "ACCOUNT_STORE_NATIVE checks=" << checks << " failures=" << failures << " device=0\n";
    return failures ? 1 : 0;
}
