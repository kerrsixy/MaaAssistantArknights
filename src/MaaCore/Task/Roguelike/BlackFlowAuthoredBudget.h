#pragma once

#include <unordered_set>

namespace asst::blackflow
{
enum class AuthoredPlanResult
{
    NoAction,
    Handled,
    Reobserve,
    WaitingForCost
};

enum class AuthoredBudgetReobservation
{
    KeepExisting,
    NeedsReobservation
};

// These are the actor's current observations, not a claim of same-frame
// affordability. The only permitted effect is requesting another ordinary frame.
struct AuthoredBudgetObservations
{
    bool strict_authored = false;
    bool duty_eligible = false;
    bool identity_known = false;
    bool preparation_valid = false;
    bool already_deployed = false;
    bool available = false;
    bool cooling = false;
    int card_cost = 0;
    int observed_cost = -1;
};

class AuthoredBudgetReobserver
{
public:
    [[nodiscard]] AuthoredBudgetReobservation request(int plan_step, const AuthoredBudgetObservations& o)
    {
        if (plan_step < 0 || m_requested.contains(plan_step) || !o.strict_authored || !o.duty_eligible ||
            !o.identity_known || !o.preparation_valid || o.already_deployed || o.available || o.cooling ||
            o.card_cost <= 0 || o.observed_cost < o.card_cost) {
            return AuthoredBudgetReobservation::KeepExisting;
        }
        m_requested.insert(plan_step);
        return AuthoredBudgetReobservation::NeedsReobservation;
    }

    void reset() noexcept { m_requested.clear(); }

    [[nodiscard]] std::size_t requested_steps() const noexcept { return m_requested.size(); }

private:
    std::unordered_set<int> m_requested;
};
}
