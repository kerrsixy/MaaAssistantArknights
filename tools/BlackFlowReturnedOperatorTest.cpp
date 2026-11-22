#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "AsstCaller.h"
#include "Config/Roguelike/RoguelikeCopilotConfig.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Task/BattleHelper.h"
#include "Task/Roguelike/RoguelikeBattleTaskPlugin.h"
#include "Utils/Platform.hpp"
#include "Vision/Battle/BattlefieldMatcher.h"

class ReturnedOperatorProbe : public asst::BattleHelper
{
public:
    ReturnedOperatorProbe() :
        BattleHelper(nullptr)
    {
    }

    asst::AbstractTask& this_task() override { std::abort(); }

    using BattleHelper::register_deployed_oper;
    using BattleHelper::remove_cooling_from_battlefield;
    using BattleHelper::remove_returned_operator_from_battlefield;

    bool occupied(asst::Point tile) const { return m_used_tiles.contains(tile); }

    bool tracked(const asst::battle::DeploymentOper& card) const
    {
        return m_battlefield_opers.contains({ card.role, card.name });
    }

    void replace_owner(asst::Point tile, const asst::battle::DeploymentOper& replacement)
    {
        m_used_tiles[tile] = { replacement.role, replacement.name };
    }

    void seed_card(asst::battle::DeploymentOper card) { m_cur_deployment_opers = { std::move(card) }; }

    bool observe(std::vector<asst::battle::DeploymentOper> cards)
    {
        const auto previous = m_cur_deployment_opers;
        return update_deployment_(cards, previous, false);
    }

    const auto& cards() const { return m_cur_deployment_opers; }
};

class ReturnedPlanProbe : public asst::RoguelikeBattleTaskPlugin
{
public:
    ReturnedPlanProbe() :
        RoguelikeBattleTaskPlugin({}, nullptr, "ReturnedPlanProbe", std::make_shared<asst::RoguelikeConfig>(), nullptr)
    {
    }

    using RoguelikeBattleTaskPlugin::deployment_cost_wait_required;
    using RoguelikeBattleTaskPlugin::deployment_requirements_met;

    void elite(const std::string& name, int value) { m_oper_elite[name] = value; }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    using namespace asst::battle;
    const std::filesystem::path repo(argv[1]), manifest(argv[2]), out(argv[3]);
    std::filesystem::create_directories(out);
    if (!AsstSetUserDir(MAA_NS::from_u16(out.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str())) {
        return 3;
    }
    const auto input = json::open(manifest);
    if (!input || !input->is_array() || input->as_array().size() != 6) {
        return 4;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    // The frames are actual observations. Prior field ownership is explicitly
    // modeled from confirmed deployment, not recovered from an old process dump.
    std::optional<DeploymentOper> recovered_card;
    for (const auto& frame : input->as_array()) {
        BattlefieldMatcher matcher(MAA_NS::imread(utils::path(frame.at("path").as_string())));
        matcher.set_object_of_interest({ .deployment = true, .oper_cost = true });
        const auto result = matcher.analyze();
        expect(bool(result), "real run44 frame is observable by production matcher");
        if (!result) {
            continue;
        }
        const int number = frame.at("frame").as_integer();
        int tanks = 0;
        for (auto card : result->deployment) {
            if (card.role != Role::Tank) {
                continue;
            }
            ++tanks;
            expect(!card.cooling, "actual mech card has no recognized cooldown");
            expect(card.cost == (number == 9 ? 21 : 31), "actual initial and returned costs remain distinct");
            std::cout << "FRAME " << number << " cost=" << card.cost << " cooling=" << card.cooling
                      << " available=" << card.available << '\n';
            if (number == 9) {
                continue;
            }
            card.name = "机械师";
            if (number == 31) {
                recovered_card = card;
            }
            ReturnedOperatorProbe probe;
            probe.register_deployed_oper(card.role, card.name, { 5, 1 });
            probe.remove_cooling_from_battlefield(card);
            expect(probe.occupied({ 5, 1 }), "old cooldown-only path cannot reconcile this return");
            expect(
                probe.remove_returned_operator_from_battlefield(card),
                "fresh ordinary return releases stale record");
            expect(
                !probe.occupied({ 5, 1 }) && !probe.tracked(card),
                "returned card releases matching prior field tile");
            expect(!probe.remove_returned_operator_from_battlefield(card), "repeat observation is idempotent");
        }
        expect(tanks == 1, "unique tank role identifies actual mech card without OCR name guessing");
    }
    expect(recovered_card.has_value(), "actual ready return remains available for empty-observation regression");
    if (recovered_card) {
        ReturnedOperatorProbe probe;
        probe.seed_card(*recovered_card);
        probe.register_deployed_oper(recovered_card->role, recovered_card->name, { 5, 1 });
        for (int i = 0; i < 2; ++i) {
            expect(
                !probe.observe({}),
                "synthetic empty bar cannot authorize the fresh-observation reconciliation branch");
            expect(
                probe.cards().size() == 1 && !probe.cards().front().available,
                "empty observation retains identity but revokes permission");
            expect(probe.occupied({ 5, 1 }), "empty observation does not release modeled physical occupancy");
        }
        expect(probe.observe({ *recovered_card }), "actual recovered avatar rematches retained identity");
        expect(
            probe.cards().size() == 1 && probe.cards().front().name == "机械师" && probe.cards().front().available,
            "fresh actual observation restores availability rather than cached permission");
        expect(
            probe.remove_returned_operator_from_battlefield(probe.cards().front()),
            "fresh return can now release modeled prior occupancy");
    }
    DeploymentOper mech { .role = Role::Tank, .cost = 31, .name = "机械师" };
    ReturnedOperatorProbe ownership;
    ownership.register_deployed_oper(mech.role, mech.name, { 5, 1 });
    DeploymentOper replacement { .role = Role::Warrior, .name = "罗小黑" };
    ownership.replace_owner({ 5, 1 }, replacement);
    expect(ownership.remove_returned_operator_from_battlefield(mech), "obsolete exact identity is removed");
    expect(ownership.occupied({ 5, 1 }), "a replacement owner's physical occupancy is preserved");
    for (const auto& name : { "障碍物", "工人之路", "结构性原理", "触手" }) {
        DeploymentOper summon { .role = Role::Drone, .name = name };
        ReturnedOperatorProbe probe;
        probe.register_deployed_oper(summon.role, summon.name, { 3, 3 });
        expect(
            !probe.remove_returned_operator_from_battlefield(summon),
            "reusable card is not ordinary return evidence");
        expect(probe.occupied({ 3, 3 }), "terrain and summon entities retain their tile");
    }
    expect(ReturnedPlanProbe::deployment_cost_wait_required(mech, 30), "known insufficient DP reserves cost");
    expect(!ReturnedPlanProbe::deployment_cost_wait_required(mech, 31), "unavailable at exact DP is not cost wait");
    expect(
        !ReturnedPlanProbe::deployment_cost_wait_required(mech, 99),
        "unavailable with ample DP cannot starve fallback");
    expect(!ReturnedPlanProbe::deployment_cost_wait_required(mech, -1), "unknown DP does not invent cost shortage");
    mech.cost = 0;
    expect(!ReturnedPlanProbe::deployment_cost_wait_required(mech, 0), "unknown card cost does not reserve forever");
    mech.cost = 31;
    mech.cooling = true;
    expect(!ReturnedPlanProbe::deployment_cost_wait_required(mech, 0), "cooldown is not insufficient DP");
    mech.cooling = false;
    mech.available = true;
    expect(!ReturnedPlanProbe::deployment_cost_wait_required(mech, 0), "available card does not enter waiting branch");

    expect(
        RoguelikeCopilot.load(repo / utils::path("resource/roguelike/BlackFlow/autopilot")),
        "actual plan parser loads candidates");
    const auto data = RoguelikeCopilot.get_stage_data("焦土之家");
    expect(data.has_value(), "scorched plan exists");
    if (data) {
        ReturnedPlanProbe probe;
        const auto op = [&](Role role, const std::string& name, int elite) {
            probe.elite(name, elite);
            return DeploymentOper { .role = role, .name = name };
        };
        const auto& core = data->deploy_plan.at("益达").front();
        const auto& block = data->deploy_plan.at("机械师").front();
        const auto& medic = data->deploy_plan.at("元素奶").front();
        const auto& caster = data->deploy_plan.at("术师").at(1);
        expect(data->deploy_plan_only && data->reserve_deploy_cost, "authored geometry and core budget retained");
        expect(
            data->replacement_home.size() == 1 && data->replacement_home[0].location == Point(5, 0),
            "only real gate is a home");
        expect(
            block.require_persistent_blocking && block.max_deployments > 4,
            "multiple real devours do not exhaust arbitrary two deployment cap");
        const auto ew = op(Role::Sniper, "维什戴尔", 2);
        expect(probe.deployment_requirements_met(ew, core), "EW upward range covers right approach and convergence");
        auto wrong = core;
        wrong.direction = DeployDirection::Left;
        expect(!probe.deployment_requirements_met(ew, wrong), "old left-facing EW misses right approach");
        auto far_target = core;
        far_target.required_attack_tiles = { { 9, 1 } };
        expect(
            !probe.deployment_requirements_met(ew, far_target),
            "EW does not claim far-right tile outside real range");
        expect(
            probe.deployment_requirements_met(op(Role::Tank, "机械师", 2), block),
            "right-facing mech reaches adjacent approach");
        const auto carda = op(Role::Caster, "卡达", 1);
        expect(
            caster.location == Point(6, 3) && probe.deployment_requirements_met(carda, caster),
            "Carda relocated upward covers left final three tiles");
        auto old = caster;
        old.location = { 6, 4 };
        expect(!probe.deployment_requirements_met(carda, old), "old caster tile cannot cover convergence");
        const auto honey = op(Role::Medic, "蜜莓", 1);
        expect(probe.deployment_requirements_met(honey, medic), "Honey actual healing range covers core and block");
        auto three = medic;
        three.required_range_tiles.emplace_back(6, 3);
        expect(
            probe.deployment_requirements_met(honey, three),
            "Honey also reaches secondary output without generalizing all medics");
        expect(!probe.deployment_requirements_met(honey, caster), "ordinary medic cannot fill damage role");
        expect(
            !probe.deployment_requirements_met(op(Role::Support, "浊心斯卡蒂", 2), caster),
            "bard cannot fill damage role");
        const auto terrain = json::open(
            repo / utils::path("resource/Arknights-Tile-Pos/ro6_n_3_5-obt-roguelike-ro6-level_rogue6_3-5.json"));
        expect(bool(terrain), "actual native terrain is available");
        if (terrain) {
            for (const auto& plan : { block, core, medic, caster }) {
                const auto& tile = terrain->at("tiles")[plan.location.y][plan.location.x];
                expect(
                    tile.at("buildableType").as_integer() == (plan.location == Point(5, 1) ? 1 : 2),
                    "each role has correct native terrain");
                expect(tile.at("tileKey").as_string() != "tile_volcano", "main role avoids lava terrain");
            }
        }
    }
    std::cout << "RETURNED_OPERATOR checks=" << checks << " failures=" << failures
              << " actual_frames=6 modeled_prior=true connections=0 inputs=0 unlock_proven=false live_pass=false\n";
    return failures ? 1 : 0;
}
