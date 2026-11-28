#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "AsstCaller.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Task/Roguelike/BlackFlow/BlackFlowRecruitmentDuty.h"

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
    const auto facts = json::open(candidate / L"resource/roguelike/BlackFlow/recruitment_duties.json");
    const auto training = json::open(candidate / L"inputs/operator_training.json");
    const auto battle = json::open(candidate / L"inputs/battle_data.json");
    if (!facts || !training || !battle) {
        return 3;
    }
    BlackFlowRecruitmentDutyCatalog catalog;
    int checks = 0, failures = 0;
    const auto check = [&](bool value, const std::string& text) {
        ++checks;
        if (!value) {
            ++failures;
            std::cout << "FAIL " << text << '\n';
        }
    };
    check(catalog.configure(*facts, *training, *battle), "actual production-Nov1-bound catalog loads");
    std::string error;
    const auto account =
        BlackFlowAccountProfile::load(candidate / L"inputs/account_217_official.json", "Official", "90000002", &error);
    check(account.has_value(), "exact frozen account profile parses without cache mutation");
    std::unordered_map<std::string, RoguelikeOper> empty;
    json::array pages;
    json::value healing_counterfactual = nullptr;
    for (const auto& fixture : std::vector<std::string> { "prior_run21_recruit_list.png", "live112_first_page.png" }) {
        const auto frame = MAA_NS::imread(candidate / L"fixtures" / MAA_NS::to_u16(fixture));
        RoguelikeRecruitImageAnalyzer analyzer(frame);
        check(analyzer.analyze(), "real analyzer succeeds " + fixture);
        RecruitmentDutyContext context { .client = "Official",
                                         .uid = fixture.starts_with("live112") ? "90000002" : "",
                                         .observed_at = "offline_replay_not_actor117",
                                         .hope = 20,
                                         .mode = 0 };
        const auto offers =
            observe_recruitment_duties(analyzer, catalog, account ? &*account : nullptr, empty, context);
        check(
            offers.size() == analyzer.get_candidate_evidence().size(),
            "same-analysis raw candidates retained " + fixture);
        json::array rows;
        for (const auto& offer : offers) {
            check(offer.analysis_id == analyzer.get_analysis_id(), "offer belongs to this exact analysis");
            const auto& raw = analyzer.get_candidate_evidence()[offer.observation_index];
            const auto price = analyzer.resolve_candidate_hope_cost(raw);
            check(!price || offer.hope_cost == price->get().hope_cost, "cost is exact linked price not rarity formula");
            if (offer.observed_temporary == true) {
                check(
                    !offer.account_training_used && offer.effective_rank < 0,
                    "actual temporary never inherits account rank");
            }
            if (offer.operation == "进阶" || offer.hope_cost == 0) {
                check(
                    offer.training_binding != "source_conditioned_non_temporary",
                    "promotion and zero price never use positive-recruit rule");
            }
            rows.emplace_back(recruitment_duty_to_json(offer));
        }
        if (fixture.starts_with("live112")) {
            check(
                std::ranges::any_of(offers, [](const auto& x) { return x.account_training_used; }),
                "real positive-price frame reaches actual account-training binding");
            auto other = context;
            other.uid = "counterfactual_other_uid";
            const auto cross =
                observe_recruitment_duties(analyzer, catalog, account ? &*account : nullptr, empty, other);
            check(
                std::ranges::none_of(cross, [](const auto& x) { return x.account_training_used; }),
                "cross UID never borrows training");
            auto poor = context;
            poor.hope = 0;
            const auto insufficient =
                observe_recruitment_duties(analyzer, catalog, account ? &*account : nullptr, empty, poor);
            check(
                std::ranges::none_of(insufficient, [](const auto& x) { return x.feasible; }),
                "actual positive-priced tank frame refuses insufficient hope");
            // The actual frame says promotion for Mechanist. An empty roster
            // cannot establish the old instance, even though its price is lower.
            std::vector<std::pair<std::string, int>> eligible;
            for (const auto& offer : offers) {
                eligible.emplace_back(offer.name, offer.page_index);
            }
            const auto choice = choose_recruitment_duty(offers, eligible, "雷蛇", 0, false);
            const auto mech = std::ranges::find_if(offers, [](const auto& x) { return x.name == "机械师"; });
            check(
                mech != offers.end() && mech->operation == "进阶" && mech->hope_cost == 1,
                "real live112 Mechanist card is a precisely linked promotion not a recruit");
            check(
                mech != offers.end() && !mech->replacement_valid && !mech->feasible,
                "actual promotion cannot be used without an existing original instance");
            check(
                !choice.adjusted && !choice.selected_offer,
                "real live112 page with empty roster retains legacy instead of forcing cheap invalid promotion");
            std::unordered_map<std::string, RoguelikeOper> with_mech {
                { "机械师", { .elite = 1, .level = 80 } },
            };
            const auto replacement_offers =
                observe_recruitment_duties(analyzer, catalog, account ? &*account : nullptr, with_mech, context);
            const auto replacement =
                std::ranges::find_if(replacement_offers, [](const auto& x) { return x.name == "机械师"; });
            check(
                replacement != replacement_offers.end() && replacement->replacement_valid && replacement->feasible,
                "controlled existing E1 Mechanist makes the real promotion a valid instance replacement");
            check(
                replacement != replacement_offers.end() && replacement->marginal[0].lower == 0 &&
                    replacement->marginal[0].upper == 0,
                "existing Mechanist already supplies blocker duty so promotion adds no new blocking presence");
            check(
                !choose_recruitment_duty(replacement_offers, eligible, "机械师", 0, false).adjusted &&
                    !choose_recruitment_duty(replacement_offers, eligible, "雷蛇", 0, false).adjusted,
                "real page plus controlled existing Mechanist retains either legacy choice when all blocker gains are "
                "zero");
        }
        pages.emplace_back(
            json::object { { "fixture", fixture },
                           { "source_kind", "prior_real_frame_offline_reanalysis" },
                           { "hope_context", "counterfactual20_not_read_from_UI" },
                           { "rows", rows } });
    }

    const auto make = [&](const std::string& name,
                          int elite,
                          int level,
                          int hope,
                          const std::string& operation,
                          const std::unordered_map<std::string, RoguelikeOper>& roster) {
        RecruitmentDutyOffer x;
        x.client = "Official";
        x.uid = "90000002";
        x.name = name;
        x.id = RoguelikeTraining.unique_identity(name);
        x.elite = elite;
        x.level = level;
        x.hope_cost = hope;
        x.operation = operation;
        x.feasible = true;
        evaluate_recruitment_duties(x, catalog, roster);
        x.feasible = x.feasible && x.replacement_valid;
        return x;
    };
    const auto known = [](DutyBound x, int expected) {
        return x.lower == expected && x.upper == expected;
    };
    check(known(catalog.body("玛恩纳", 2, 90)[0], 0), "Warrior role cannot invent persistent blocking for liberator");
    check(
        known(catalog.body("龙舌兰", 2, 80)[0], 0),
        "actual librator subclass also excludes Tequila persistent blocking");
    check(known(catalog.body("桃金娘", 2, 70)[0], 0), "bearer not counted as persistent defence");
    check(known(catalog.body("临光", 1, 70)[0], 1), "actual phase base block and persistent eligibility are combined");
    check(
        catalog.body("临光", -1, 70)[0].lower == 0 && catalog.body("临光", -1, 70)[0].upper == 1,
        "unknown phase is not account E2");
    check(
        known(catalog.body("纯烬艾雅法拉", -1, -1)[1], 1),
        "independent normal-healing fact survives unknown skill or numeric training");
    check(
        known(catalog.body("焰影苇草", 2, 90)[1], 0),
        "enemy-hit-conditioned healing not called unconditional normal-medic duty");
    std::unordered_map<std::string, RoguelikeOper> shield { { "临光", { .elite = 1, .level = 70 } } };
    auto promotion = make("临光", 2, 80, 2, "进阶", shield);
    auto healer = make("纯烬艾雅法拉", 1, 80, 2, "招募", shield);
    check(known(promotion.marginal[0], 0), "promotion replaces old blocker instead of adding another body");
    check(
        known(healer.marginal[1], 1) && known(healer.marginal[0], 0),
        "remaining missing medical duty receives one bounded marginal");
    std::vector<RecruitmentDutyOffer> choices { promotion, healer };
    const std::vector<std::pair<std::string, int>> eligible { { promotion.name, 0 }, { healer.name, 0 } };
    auto selected = choose_recruitment_duty(choices, eligible, promotion.name, 0, false);
    check(
        selected.adjusted && selected.selected_offer == 1,
        "native consumer can choose cross-name healer over zero-structural-gain promotion");
    auto high_medic = make("纯烬艾雅法拉", 2, 90, 4, "招募", shield);
    auto lower_medic = make("嘉维尔", 1, 60, 2, "招募", shield);
    high_medic.planned_skill = 1;
    lower_medic.planned_skill = 1;
    if (account) {
        high_medic.effective_rank = account->skill_level_for_run(high_medic.name, 1, 2, 90).value_or(-1);
        lower_medic.effective_rank = account->skill_level_for_run(lower_medic.name, 1, 1, 60).value_or(-1);
    }
    check(
        high_medic.effective_rank == 10 && lower_medic.effective_rank == 7,
        "actual frozen account supplies E2M3 and E1 rank7 for the controlled-price medical pair");
    check(
        known(high_medic.marginal[1], 1) && known(lower_medic.marginal[1], 1),
        "two actual-model medical categories both fill the same missing presence duty without proving equal healing");
    check(
        !recruitment_duty_strictly_dominates(lower_medic, high_medic),
        "cheaper lower-trained medic is not a proven duty improvement over higher-trained medic");
    const std::vector<RecruitmentDutyOffer> price_only { high_medic, lower_medic };
    const std::vector<std::pair<std::string, int>> price_eligible { { high_medic.name, 0 }, { lower_medic.name, 0 } };
    check(
        !choose_recruitment_duty(price_only, price_eligible, high_medic.name, 0, false).adjusted,
        "native chooser keeps high-trained legacy when identical binary duty and cheaper price are the only advantage");
    auto cheaper_identical = high_medic;
    cheaper_identical.hope_cost = 1;
    check(
        !recruitment_duty_strictly_dominates(cheaper_identical, high_medic),
        "price alone cannot establish strict duty gain even for identical proved facts");
    check(
        !choose_recruitment_duty(choices, eligible, promotion.name, 0, true).adjusted,
        "fixed opening never silently overridden");
    choices[1].hope_cost = 3;
    check(
        !choose_recruitment_duty(choices, eligible, promotion.name, 0, false).adjusted,
        "higher price is not declared strict resource dominance");
    choices[1] = healer;
    choices[1].marginal[1] = { 0, 1 };
    check(
        !choose_recruitment_duty(choices, eligible, promotion.name, 0, false).adjusted,
        "uncertain relevant duty does not manufacture an improvement");
    choices[1] = healer;
    choices[1].feasible = false;
    check(
        !choose_recruitment_duty(choices, eligible, promotion.name, 0, false).adjusted,
        "budget or eligibility failure cannot override");
    choices[1] = healer;
    choices[1].uid = "different_uid";
    check(
        !choose_recruitment_duty(choices, eligible, promotion.name, 0, false).adjusted,
        "two accounts cannot enter the same comparison");
    auto duplicate = healer;
    duplicate.name = "controlled_second_same_capability";
    choices = { promotion, healer, duplicate };
    auto two_eligible = eligible;
    two_eligible.emplace_back(duplicate.name, 0);
    check(
        !choose_recruitment_duty(choices, two_eligible, promotion.name, 0, false).adjusted,
        "indistinguishable dominators retain explicit legacy fallback");
    std::unordered_map<std::string, RoguelikeOper> complete = shield;
    complete.emplace("纯烬艾雅法拉", RoguelikeOper { .elite = 1, .level = 80 });
    auto extra = make("褐果", 1, 60, 1, "招募", complete);
    check(known(extra.marginal[1], 0), "duplicate fulfilled medical duty has zero marginal regardless training");
    auto expensive = extra;
    expensive.name = "controlled_expensive_same_zero_duties";
    expensive.hope_cost = 5;
    check(
        !recruitment_duty_strictly_dominates(extra, expensive),
        "all duties fulfilled does not trigger cheapness-only override of future utility");
    auto attack = make("维什戴尔", 2, 90, 2, "招募", empty);
    auto block = make("临光", 1, 70, 2, "招募", empty);
    auto heal = make("纯烬艾雅法拉", 1, 80, 2, "招募", empty);
    check(
        !recruitment_duty_strictly_dominates(block, heal) && !recruitment_duty_strictly_dominates(heal, block),
        "missing blocker versus missing healer remains Pareto conflict");
    auto actual = healer;
    check(recruitment_duty_refind_matches(healer, actual), "actual final refind can match precise intended offer");
    actual.hope_cost = 3;
    check(!recruitment_duty_refind_matches(healer, actual), "changed final price prohibits input");
    actual = healer;
    actual.elite = 2;
    check(!recruitment_duty_refind_matches(healer, actual), "changed final phase prohibits input");
    actual = healer;
    actual.uid = "other";
    check(!recruitment_duty_refind_matches(healer, actual), "changed final UID prohibits input");
    actual = healer;
    actual.effective_rank = 10;
    check(!recruitment_duty_refind_matches(healer, actual), "changed actual training does not match stale intent");

    if (account) {
        const auto e1 = account->skill_level_for_run("维什戴尔", 2, 1, 80);
        const auto locked = account->skill_level_for_run("维什戴尔", 3, 1, 80);
        const auto e2 = account->skill_level_for_run("维什戴尔", 3, 2, 90);
        check(e1 == 7 && !locked && e2 == 10, "actual account M3 maps through actual offer phase and selected slot");
        const auto row7 = catalog.skill("char_1035_wisdel", 3, 7), row10 = catalog.skill("char_1035_wisdel", 3, 10);
        check(
            row7.get("sp_cost", -1) == 58 && row7.get("initial_sp", -1) == 35 && row10.get("sp_cost", -1) == 50 &&
                row10.get("initial_sp", -1) == 40,
            "actual bound Nov1 rows differ with effective rank without inventing deadline or DPS");
        check(
            row10.get("used_for_output_winner", true) == false &&
                row10.get("deadline_producer_available", true) == false,
            "unproduced battle timing never decides output winner");
        const auto account_json = json::open(candidate / L"inputs/account_217_official.json");
        if (!account_json) {
            return 4;
        }
        auto low = *account_json;
        low["operators"]["维什戴尔"]["skill_training"]["mastery_levels"][2] = 0;
        low["operators"]["纯烬艾雅法拉"]["skill_training"]["mastery_levels"][0] = 0;
        const auto lower_training = BlackFlowAccountProfile::parse(low, "Official", "90000002", &error);
        check(
            lower_training && lower_training->skill_level_for_run("维什戴尔", 3, 2, 90) == 7,
            "counterfactual same account less mastery changes actual effective row instead of a bonus");
        const auto pure_rank = account->skill_level_for_run("纯烬艾雅法拉", 1, 2, 90);
        const auto pure_low =
            lower_training ? lower_training->skill_level_for_run("纯烬艾雅法拉", 1, 2, 90) : std::nullopt;
        const auto packet10 = catalog.heal_packet("纯烬艾雅法拉", 2, 90, 1, pure_rank.value_or(-1));
        const auto packet7 = catalog.heal_packet("纯烬艾雅法拉", 2, 90, 1, pure_low.value_or(-1));
        check(
            pure_rank == 10 && pure_low == 7 && !packet10.is_null() && !packet7.is_null(),
            "actual217 account and counterfactual ranks reach intrinsic packet consumer");
        if (!packet10.is_null() && !packet7.is_null()) {
            check(
                packet10.at("active_packet_bounds").at(0).as_double() >
                        packet7.at("active_packet_bounds").at(0).as_double() &&
                    packet10.at("active_packet_bounds").at(1).as_double() >
                        packet7.at("active_packet_bounds").at(1).as_double(),
                "real M3 versus rank7 changes both packet endpoints from original skill multiplier");
            check(
                packet10.at("base_packet_bounds") == packet7.at("base_packet_bounds"),
                "mastery cannot secretly increase unbuffed base ATK");
            check(
                packet10.at("base_packet_bounds").at(0).as_double() <
                    packet10.at("base_packet_bounds").at(1).as_double(),
                "unknown trust and potential retained as a nonzero interval");
            healing_counterfactual =
                json::object { { "actual217_rank10", packet10 }, { "counterfactual_rank7", packet7 } };
        }
        check(catalog.heal_packet("纯烬艾雅法拉", 1, 80, 1, 10).is_null(), "E1 cannot borrow effective M3 packet");
        check(
            catalog.heal_packet("纯烬艾雅法拉", 0, 50, 1, 7).is_null(),
            "direct API rejects E0 rank7 without account mapping wrapper");
        check(
            catalog.heal_packet("芙蓉", 1, 55, 1, 10).is_null(),
            "direct API enforces low-rarity actual maximum skill rank");
    }
    auto wrong = *facts;
    for (int variant = 0; variant < 4; ++variant) {
        auto broken = *facts;
        if (variant == 0) {
            auto slot = broken["operators"]["char_1016_agoat2"]["slots"][0].as_object();
            slot.erase("rows");
            broken["operators"]["char_1016_agoat2"]["slots"][0] = slot;
        }
        if (variant == 1) {
            broken["operators"]["char_1016_agoat2"]["slots"][0]["rows"] = json::object {};
        }
        if (variant == 2) {
            broken["operators"]["char_1016_agoat2"]["slots"][0]["rows"][9]["rank"] = 7;
        }
        if (variant == 3) {
            broken["operators"]["char_1016_agoat2"]["slots"][0]["rows"][9]["sp_cost"] = -1;
        }
        BlackFlowRecruitmentDutyCatalog safe;
        check(
            safe.configure(broken, *training, *battle) && safe.skill("char_1016_agoat2", 1, 10).is_null(),
            "malformed skill row is unknown without abort or made-up maximum");
    }
    wrong["character_table_sha256"] = "different_source";
    BlackFlowRecruitmentDutyCatalog rejected;
    check(!rejected.configure(wrong, *training, *battle), "wrong production source cohort rejected");
    auto malformed = *facts;
    malformed["operators"]["char_1035_wisdel"]["direct_normal_medic"] = 1;
    check(!rejected.configure(malformed, *training, *battle), "wrong capability type cannot become a proven false");
    malformed = *facts;
    auto missing = malformed["operators"]["char_1035_wisdel"].as_object();
    missing.erase("persistent_blocker_class");
    malformed["operators"]["char_1035_wisdel"] = missing;
    check(!rejected.configure(malformed, *training, *battle), "missing capability cannot default to proven zero");
    check(
        rejected.load(
            candidate / L"resource/roguelike/BlackFlow/recruitment_duties.json",
            candidate / L"inputs/operator_training.json",
            candidate / L"inputs/battle_data.json"),
        "reload valid catalog");
    check(
        !rejected.load(
            candidate / L"missing-file.json",
            candidate / L"inputs/operator_training.json",
            candidate / L"inputs/battle_data.json") &&
            !rejected.valid(),
        "failed reload clears old valid state");
    std::ofstream(output / L"native117.json") << json::value(
        json::object {
            { "checks", checks },
            { "failures", failures },
            { "pages", pages },
            { "healing_counterfactual", healing_counterfactual },
            { "cross_name_scenario_source", "explicit_counterfactual_roster_cost_and_legacy;real_Nov1_capabilities" },
            { "structural_choice", selected.adjusted },
            { "device_inputs", 0 },
            { "network_requests", 0 },
            { "actual_plugin_workflow_executed", false },
            { "live_priority_adjusted", false },
            { "full_DPS", nullptr },
        });
    std::cout << "RECRUIT_HEAL_PACKET120 checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
