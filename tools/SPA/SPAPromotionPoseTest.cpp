#include "Task/MiniGame/SPA/SPAPromotionPosePolicy.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace asst::spa;
    using Points = std::vector<std::pair<int, int>>;
    const Points prior { { 580, 280 }, { 660, 280 }, { 740, 280 }, { 820, 280 } };
    const Points first { prior[0] };
    const Points two_elites { prior[0], prior[2] };

    // One existing elite plus three initial copies can leave two elite bodies.
    // The former single-survivor check discarded both independently verified poses.
    assert(retain_promoted_elite_poses({ 0, 2 }, prior, two_elites) == two_elites);
    // An elite remaining on the bench must not force an invented field pose.
    assert(retain_promoted_elite_poses({ 0, 2 }, prior, first) == first);
    assert(retain_promoted_elite_poses({ 0, 1 }, prior, first) == first);
    // A residual initial body is not implicitly located or labelled elite.
    assert(retain_promoted_elite_poses({ 1, 1 }, prior, first) == first);
    assert(retain_promoted_elite_poses({ 1, 1 }, prior, two_elites).empty());
    assert(retain_promoted_elite_poses({ 0, 1 }, prior, two_elites).empty());
    assert(retain_promoted_elite_poses({ 0, 2 }, prior, { prior[0], prior[0] }).empty());
    assert(retain_promoted_elite_poses({ 0, 2 }, { prior[0], prior[0] }, first).empty());
    assert(retain_promoted_elite_poses({ 0, 2 }, prior, { { 500, 400 } }).empty());
    assert(retain_promoted_elite_poses({ 0, 2 }, prior, {}).empty());
    assert(retain_promoted_elite_poses({ 1, 0 }, prior, first).empty());
    assert(retain_promoted_elite_poses({ -1, 1 }, prior, first).empty());
    assert(retain_promoted_elite_poses({ 18, 1 }, prior, first).empty());
    std::cout << "Promotion pose retention: 13 cases passed\n";
}
