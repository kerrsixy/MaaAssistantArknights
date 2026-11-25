#pragma once

#include <array>
#include <cstdint>

namespace asst::blackflow
{
// The existing route-cost terms, shared by continuation and first-action ranking.
// The extra persistent term is one existing processing-cost unit of carry value;
// it is a policy preference, not a claim about future item or resource income.
constexpr std::int64_t soft_route_cost(
    int battles,
    int processing,
    int persistent,
    int duration,
    int backtracking,
    int rewardless,
    int interactions = 0) noexcept
{
    return 2LL * battles + processing + persistent + duration + 2LL * backtracking + 3LL * rewardless + interactions;
}

// A total lexicographic order, avoiding a non-transitive pairwise interval test.
// Unknown upside is retained, but cannot buy a charge using a guaranteed-reward claim.
// This is deliberately conservative: no probability or expected value is invented.
constexpr std::array<std::int64_t, 5> soft_reward_key(
    std::int64_t lower,
    std::int64_t upper,
    int battles,
    int processing,
    int persistent,
    int duration,
    int backtracking,
    int rewardless,
    int interactions = 0) noexcept
{
    const auto resource_cost = soft_route_cost(battles, processing, persistent, 0, 0, 0, interactions);
    const auto transit_cost = soft_route_cost(0, 0, 0, duration, backtracking, rewardless);
    return { lower - resource_cost,
             -static_cast<std::int64_t>(persistent),
             upper - resource_cost - transit_cost,
             lower,
             upper };
}
} // namespace asst::blackflow
