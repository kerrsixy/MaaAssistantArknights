#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "AsstCaller.h"
#include "MaaUtils/Encoding.h"
#include "Task/BattleHelper.h"

class TerrainOccupancyProbe : public asst::BattleHelper
{
public:
    TerrainOccupancyProbe() :
        BattleHelper(nullptr)
    {
    }

    asst::AbstractTask& this_task() override { std::abort(); }

    using BattleHelper::clear;
    using BattleHelper::is_terrain_roadblock;
    using BattleHelper::register_deployed_oper;
    using BattleHelper::remove_cooling_from_battlefield;

    bool occupied(asst::Point tile) const { return m_used_tiles.contains(tile); }

    std::size_t units() const { return m_battlefield_opers.size(); }

    std::size_t skill_timers() const { return m_last_use_skill_time.size(); }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        return 2;
    }
    const std::filesystem::path repo(argv[1]), out(argv[2]);
    std::filesystem::create_directories(out);
    if (!AsstSetUserDir(MAA_NS::from_u16(out.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str())) {
        return 3;
    }
    using namespace asst;
    using namespace asst::battle;
    int checks = 0, failures = 0;
    const auto expect = [&](bool result, const char* label) {
        ++checks;
        if (!result) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    TerrainOccupancyProbe probe;
    DeploymentOper crate { .role = Role::Drone, .cooling = true, .name = "障碍物" };
    DeploymentOper mech { .role = Role::Tank, .cooling = true, .name = "机械师" };
    DeploymentOper worker { .role = Role::Drone, .cooling = true, .name = "工人之路" };
    expect(probe.is_terrain_roadblock(crate), "real BattleData identifies a terrain roadblock");
    expect(
        !probe.is_terrain_roadblock(mech) && !probe.is_terrain_roadblock(worker),
        "ordinary units and directional workers keep their lifecycle");
    probe.register_deployed_oper(crate.role, crate.name, { 3, 3 });
    probe.register_deployed_oper(crate.role, crate.name, { 6, 1 });
    expect(probe.occupied({ 3, 3 }) && probe.occupied({ 6, 1 }), "both placed barriers occupy distinct tiles");
    expect(probe.units() == 0, "terrain is not counted as combat units");
    expect(probe.skill_timers() == 0, "terrain does not acquire skill activation timers");
    for (int frame = 0; frame < 3; ++frame) {
        probe.remove_cooling_from_battlefield(crate);
        expect(
            probe.occupied({ 3, 3 }) && probe.occupied({ 6, 1 }),
            "reusable card cooldown does not release either physical barrier tile");
    }
    probe.register_deployed_oper(mech.role, mech.name, { 5, 1 });
    expect(probe.units() == 1 && probe.occupied({ 5, 1 }), "normal defender registers as a unit");
    probe.remove_cooling_from_battlefield(mech);
    expect(probe.units() == 0 && !probe.occupied({ 5, 1 }), "normal defender cooldown still releases its tile");
    expect(probe.occupied({ 3, 3 }) && probe.occupied({ 6, 1 }), "normal retreat does not erase nearby terrain");
    probe.register_deployed_oper(worker.role, worker.name, { 5, 3 });
    expect(probe.units() == 1, "worker registration is unchanged");
    probe.remove_cooling_from_battlefield(worker);
    expect(probe.units() == 0 && !probe.occupied({ 5, 3 }), "worker removal is unchanged");
    probe.clear();
    expect(
        !probe.occupied({ 3, 3 }) && !probe.occupied({ 6, 1 }) && probe.units() == 0 && probe.skill_timers() == 0,
        "battle reset clears terrain occupancy and unit state");
    std::cout << "TERRAIN_OCCUPANCY checks=" << checks << " failures=" << failures << " connections=0 inputs=0\n";
    return failures == 0 ? 0 : 1;
}
