#include "SPAFirstPactRecruitment.h"
#include "SPAFormation.h"
#include "SPAPlanner.h"

#include <algorithm>
#include <set>
#include <utility>

asst::spa::FirstPactRecruitmentReconciliation asst::spa::reconcile_first_pact_recruitment(
    const FirstPactRecruitmentTransaction& transaction,
    const RecruitmentPhysicalSnapshot& before,
    const RecruitmentPhysicalSnapshot& after,
    const std::vector<ConfirmedRecruitmentPromotionReward>& rewards)
{
    const auto reject = [&transaction](std::string reason) {
        FirstPactRecruitmentReconciliation result;
        result.scope = transaction.scope;
        result.reason = std::move(reason);
        return result;
    };
    if (transaction.scope.execution_id.empty() || transaction.scope.transaction_id.empty() ||
        transaction.scope.round <= 0 || before.scope != transaction.scope || after.scope != transaction.scope ||
        transaction.already_committed) {
        return reject("Missing, mismatched or already committed transaction scope");
    }
    if (!transaction.first_pact_verified || !transaction.quote_verified || !transaction.item_consumption_verified ||
        !transaction.isolated_roster_changes_verified || !transaction.promotion_rewards_resolved ||
        (transaction.initial_grants_quote != 1 && transaction.initial_grants_quote != 2)) {
        return reject("Phone source, consumption, quote or isolated reward completion is unverified");
    }
    if (!before.inventory.complete || !after.inventory.complete || before.observation_sequence == 0 ||
        before.observation_sequence >= transaction.contact_sequence ||
        transaction.contact_sequence >= after.observation_sequence) {
        return reject("Complete physical snapshots must bracket the phone contact");
    }

    const auto pacts = operator_pacts();
    const auto recipient_pacts = pacts.find(transaction.recipient_name);
    const auto recipient = before.inventory.members.find(transaction.recipient_name);
    if (recipient_pacts == pacts.end() || recipient_pacts->second.empty() ||
        recipient_pacts->second.front() != transaction.recipient_first_pact ||
        recipient == before.inventory.members.end() ||
        (recipient->second.initial <= 0 && recipient->second.elite <= 0)) {
        return reject("Recipient body and verified native first pact are required");
    }

    FirstPactRecruitmentReconciliation result;
    result.scope = transaction.scope;
    std::set<std::string> names;
    int values[2] {};
    const ObservedOperatorInventory* inventories[] { &before.inventory, &after.inventory };
    for (int index = 0; index < 2; ++index) {
        int physical_count = 0;
        for (const auto& [name, counts] : inventories[index]->members) {
            if (!pacts.contains(name) || counts.initial < 0 || counts.elite < 0 || counts.initial > 18 ||
                counts.elite > 18) {
                return reject("Unknown identity or invalid physical form counts");
            }
            const int required = promotion_copies_required(name);
            if (required < 2 || counts.initial >= required) {
                return reject("A physical snapshot contains unresolved automatic synthesis");
            }
            physical_count += counts.initial + counts.elite;
            if (physical_count > 18) {
                return reject("Physical roster exceeds the supported field and bench capacity");
            }
            values[index] += counts.initial + required * counts.elite;
            names.insert(name);
        }
    }

    // There are at most 18 initial bodies before contact and two phone grants.
    // Every synthesis consumes at least two initial copies and adds one reward,
    // reducing that stock by at least one. This is not a server draw-pool limit.
    if (rewards.size() > 20) {
        return reject("Promotion reward chain exceeds the supported transaction bound");
    }
    std::set<std::string> receipt_ids;
    std::map<std::string, int> reward_counts;
    std::uint64_t previous_confirmation = transaction.contact_sequence;
    for (const auto& reward : rewards) {
        if (reward.scope != transaction.scope || reward.receipt_id.empty() ||
            !receipt_ids.insert(reward.receipt_id).second || !pacts.contains(reward.initial_operator_name) ||
            !reward.initial_form_verified || !reward.selection_confirmed || !reward.page_advanced ||
            reward.selection_sequence <= previous_confirmation ||
            reward.confirmation_sequence <= reward.selection_sequence ||
            reward.confirmation_sequence >= after.observation_sequence) {
            return reject("Reward is unknown, duplicated, unconfirmed or outside the ordered transaction window");
        }
        previous_confirmation = reward.confirmation_sequence;
        ++reward_counts[reward.initial_operator_name];
        names.insert(reward.initial_operator_name);
        result.confirmed_initial_rewards.push_back(reward.initial_operator_name);
    }
    if (values[1] - values[0] != transaction.initial_grants_quote + static_cast<int>(rewards.size())) {
        return reject("Total initial-copy value does not match phone grants plus confirmed rewards");
    }

    const auto counts_for = [](const ObservedOperatorInventory& inventory, const std::string& name) {
        const auto found = inventory.members.find(name);
        return found == inventory.members.end() ? OperatorFormCounts {} : found->second;
    };
    std::map<std::string, int> random_counts;
    int random_total = 0;
    for (const auto& name : names) {
        const auto source = counts_for(before.inventory, name);
        const auto target = counts_for(after.inventory, name);
        const int required = promotion_copies_required(name);
        const int net_value = target.initial + required * target.elite - source.initial - required * source.elite;
        const int random_grants = net_value - reward_counts[name];
        if (random_grants < 0 || random_grants > transaction.initial_grants_quote) {
            return reject("A roster change cannot be explained by the phone and its confirmed rewards");
        }
        if (random_grants > 0) {
            const auto& membership = pacts.at(name);
            if (std::find(membership.begin(), membership.end(), transaction.recipient_first_pact) == membership.end()) {
                return reject("A random grant does not belong to the recipient first pact");
            }
            random_total += random_grants;
            random_counts[name] = random_grants;
            result.random_initial_grants.insert(result.random_initial_grants.end(), random_grants, name);
        }
    }
    if (random_total != transaction.initial_grants_quote) {
        return reject("Random first-pact grant count differs from the live phone quote");
    }

    // Replay additions in a private working copy, never against the live roster.
    // Existing elites stay intact. A reward needs an already available synthesis
    // opportunity; it cannot create its own justification through circular accounting.
    auto simulated = before.inventory;
    int reward_opportunities = 0;
    const auto add_initial = [&](const std::string& name, int count) {
        auto& forms = simulated.members[name];
        const int required = promotion_copies_required(name);
        const int copies = forms.initial + count;
        const int promotions = copies / required;
        forms.initial = copies % required;
        forms.elite += promotions;
        reward_opportunities += promotions;
        result.promoted_operators.insert(result.promoted_operators.end(), promotions, name);
    };
    for (const auto& [name, count] : random_counts) {
        add_initial(name, count);
    }
    for (const auto& reward : rewards) {
        if (reward_opportunities == 0) {
            return reject("A confirmed reward has no preceding synthesis in this phone transaction");
        }
        --reward_opportunities;
        add_initial(reward.initial_operator_name, 1);
    }
    if (reward_opportunities != 0) {
        return reject("A synthesis reward is missing from the resolved transaction");
    }
    for (const auto& name : names) {
        const auto expected = counts_for(simulated, name);
        const auto observed = counts_for(after.inventory, name);
        if (expected.initial != observed.initial || expected.elite != observed.elite) {
            return reject("Observed physical forms do not match initial-copy synthesis");
        }
    }
    result.verified = true;
    result.initial_value_before = values[0];
    result.initial_value_after = values[1];
    result.final_inventory = after.inventory;
    result.reason = "Complete scoped snapshots reconcile phone grants and separately confirmed synthesis rewards";
    return result;
}
