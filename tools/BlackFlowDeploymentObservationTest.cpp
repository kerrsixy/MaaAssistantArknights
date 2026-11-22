#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "AsstCaller.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Task/BattleHelper.h"
#include "Utils/Platform.hpp"
#include "Vision/Battle/BattlefieldMatcher.h"

class DeploymentObservationProbe : public asst::BattleHelper
{
public:
    DeploymentObservationProbe() :
        BattleHelper(nullptr)
    {
    }

    asst::AbstractTask& this_task() override { std::abort(); }

    void seed(const asst::battle::DeploymentOper& card)
    {
        m_cur_deployment_opers = { card };
        register_deployed_oper(card.role, card.name, { 2, 5 });
    }

    bool observe(std::vector<asst::battle::DeploymentOper> cards)
    {
        const auto previous = m_cur_deployment_opers;
        return update_deployment_(cards, previous, false);
    }

    const auto& cards() const { return m_cur_deployment_opers; }

    bool occupied() const { return m_used_tiles.contains({ 2, 5 }); }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    const std::filesystem::path repo(argv[1]), manifest(argv[2]), out(argv[3]);
    std::filesystem::create_directories(out);
    if (!AsstSetUserDir(MAA_NS::from_u16(out.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str())) {
        return 3;
    }
    const auto input = json::open(manifest);
    if (!input || !input->is_array() || input->as_array().size() != 4) {
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
    std::vector<std::vector<asst::battle::DeploymentOper>> frames;
    for (const auto& image : input->as_array()) {
        asst::BattlefieldMatcher matcher(MAA_NS::imread(asst::utils::path(image.as_string())));
        matcher.set_object_of_interest({ .deployment = true });
        const auto result = matcher.analyze();
        expect(bool(result), "actual run34 battlefield remains observable");
        if (!result) {
            return 5;
        }
        frames.push_back(result->deployment);
    }
    expect(frames[0].empty() && frames[1].empty(), "two actual first-battle frames contain no recognized cards");
    expect(frames[2].size() == 1 && frames[3].size() == 1, "later actual frames recover the cooling summon card");
    if (frames[2].size() != 1 || frames[3].size() != 1) {
        return 5;
    }
    auto prior = frames[2].front();
    expect(
        prior.role == asst::battle::Role::Drone && prior.cooling && !prior.available,
        "actual recovered summon card is cooling and unavailable");
    prior.name = "结构性原理";
    prior.cost = 10;
    prior.available = true;
    prior.cooling = false;
    // This prior actor state is synthetic; only the four matcher inputs are actual frames.
    DeploymentObservationProbe probe;
    probe.seed(prior);
    for (int i = 0; i != 2; ++i) {
        expect(!probe.observe(frames[i]), "empty current bar reports no usable observation to the actor");
        expect(probe.cards().size() == 1, "identity cache survives transient empty observations");
        if (probe.cards().size() != 1) {
            continue;
        }
        const auto& retained = probe.cards().front();
        expect(!retained.available, "prior availability never authorizes input after an empty observation");
        expect(
            retained.name == prior.name && retained.role == prior.role && retained.rect == prior.rect,
            "identity and last card geometry remain available for rematching only");
        expect(!retained.cooling && retained.cost == 10, "empty observation does not invent cooldown or new cost");
        expect(
            cv::norm(retained.avatar, prior.avatar, cv::NORM_INF) == 0,
            "exact avatar template survives for rematching");
        expect(probe.occupied(), "missing card observation is not field retirement evidence");
    }
    if (!failures) {
        expect(probe.observe(frames[2]), "fresh actual cooling card rematches preserved identity");
        expect(
            probe.cards().size() == 1 && probe.cards().front().name == prior.name && probe.cards().front().cooling &&
                !probe.cards().front().available,
            "fresh cooldown replaces stale availability without losing identity");
        expect(probe.occupied(), "summon card cooldown does not retire its existing field entity");
        auto ready = frames[3];
        ready.front().available = true;
        ready.front().cooling = false;
        expect(probe.observe(ready), "explicit synthetic fresh ready observation can recover availability");
        expect(
            probe.cards().size() == 1 && probe.cards().front().available && !probe.cards().front().cooling,
            "availability is restored by a fresh observation rather than a timer or cached permission");
    }
    std::cout << "DEPLOYMENT_OBSERVATION checks=" << checks << " failures=" << failures
              << " actual_frames=4 synthetic_prior_and_ready=true connections=0 inputs=0 live_fix_proven=false\n";
    return failures ? 1 : 0;
}
