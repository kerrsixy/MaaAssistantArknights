#include "Task/MiniGame/SPA/SPAFirstPactRecruitment.h"

#include <cassert>
#include <iostream>
#include <limits>
#include <utility>

using namespace asst::spa;

struct PhoneFixture
{
    FirstPactRecruitmentTransaction transaction;
    RecruitmentPhysicalSnapshot before;
    RecruitmentPhysicalSnapshot after;
    std::vector<ConfirmedRecruitmentPromotionReward> rewards;

    FirstPactRecruitmentReconciliation reconcile() const
    {
        return reconcile_first_pact_recruitment(transaction, before, after, rewards);
    }
};

ConfirmedRecruitmentPromotionReward
    reward_for(const RecruitmentEvidenceScope& scope, std::string id, std::string name, std::uint64_t sequence)
{
    return { scope, std::move(id), std::move(name), sequence, sequence + 1, true, true, true };
}

PhoneFixture base_phone()
{
    PhoneFixture fixture;
    fixture.transaction.scope = { "execution-a", "phone-1", 7 };
    fixture.transaction.recipient_name = "“暴躁铁皮”";
    fixture.transaction.recipient_first_pact = "协防干员";
    fixture.transaction.initial_grants_quote = 1;
    fixture.transaction.contact_sequence = 20;
    fixture.transaction.first_pact_verified = true;
    fixture.transaction.quote_verified = true;
    fixture.transaction.item_consumption_verified = true;
    fixture.transaction.isolated_roster_changes_verified = true;
    fixture.transaction.promotion_rewards_resolved = true;
    fixture.before = { fixture.transaction.scope, 10, { true, { { "“暴躁铁皮”", { 1, 0 } } } } };
    fixture.after = fixture.before;
    fixture.after.observation_sequence = 100;
    fixture.after.inventory.members["初雪"] = { 1, 0 };
    return fixture;
}

PhoneFixture promoted_phone()
{
    auto fixture = base_phone();
    fixture.before.inventory.members["“暴躁铁皮”"] = { 2, 1 };
    fixture.after.inventory.members = { { "“暴躁铁皮”", { 0, 2 } }, { "灰喉", { 1, 0 } } };
    fixture.rewards = { reward_for(fixture.transaction.scope, "reward-1", "灰喉", 30) };
    return fixture;
}

void assert_rejected(const PhoneFixture& fixture)
{
    const auto result = fixture.reconcile();
    assert(!result.verified);
    assert(!result.reason.empty());
    assert(result.random_initial_grants.empty());
    assert(result.confirmed_initial_rewards.empty());
    assert(result.promoted_operators.empty());
    assert(!result.final_inventory.complete && result.final_inventory.members.empty());
}

int main()
{
    // These are pure evidence-contract examples, not claims of live phone pulls.
    auto fixture = base_phone();
    auto result = fixture.reconcile();
    assert(result.verified);
    assert(result.random_initial_grants == std::vector<std::string> { "初雪" });
    assert(result.promoted_operators.empty() && result.confirmed_initial_rewards.empty());
    assert(result.initial_value_before == 1 && result.initial_value_after == 2);
    assert(result.final_inventory.members.at("“暴躁铁皮”").initial == 1);

    // Advanced phone can add two different first-pact members. No tier/pool
    // assumption forces either random grant to be the strategy core.
    fixture.transaction.initial_grants_quote = 2;
    fixture.after.inventory.members["夜莺"] = { 1, 0 };
    result = fixture.reconcile();
    assert(result.verified && result.random_initial_grants.size() == 2);
    assert(result.final_inventory.members.at("夜莺").initial == 1);

    // The third initial copy synthesizes once, preserving an existing elite.
    // Its separately confirmed reward may belong to an entirely different pact.
    fixture = promoted_phone();
    result = fixture.reconcile();
    assert(result.verified);
    assert(result.random_initial_grants == std::vector<std::string> { "“暴躁铁皮”" });
    assert(result.confirmed_initial_rewards == std::vector<std::string> { "灰喉" });
    assert(result.promoted_operators == std::vector<std::string> { "“暴躁铁皮”" });
    assert(result.initial_value_before == 5 && result.initial_value_after == 7);
    assert(result.final_inventory.members.at("“暴躁铁皮”").initial == 0);
    assert(result.final_inventory.members.at("“暴躁铁皮”").elite == 2);
    assert(fixture.before.inventory.members.at("“暴躁铁皮”").initial == 2);
    assert(fixture.before.inventory.members.at("“暴躁铁皮”").elite == 1);

    // Two grants do not apply a second three-copy deduction to the new elite.
    fixture.transaction.initial_grants_quote = 2;
    fixture.after.inventory.members["“暴躁铁皮”"].initial = 1;
    result = fixture.reconcile();
    assert(result.verified && result.random_initial_grants.size() == 2);
    assert(result.promoted_operators.size() == 1);
    assert(result.final_inventory.members.at("“暴躁铁皮”").elite == 2);

    // A confirmed reward can share the phone pact without becoming a phone draw.
    fixture = promoted_phone();
    fixture.after.inventory.members.erase("灰喉");
    fixture.after.inventory.members["初雪"] = { 1, 0 };
    fixture.rewards.front().initial_operator_name = "初雪";
    result = fixture.reconcile();
    assert(result.verified && result.random_initial_grants.size() == 1);
    assert(result.confirmed_initial_rewards == std::vector<std::string> { "初雪" });
    fixture.transaction.initial_grants_quote = 2;
    assert_rejected(fixture);

    // An actual ordered reward chain includes 星极's two-copy synthesis rule.
    fixture = promoted_phone();
    fixture.before.inventory.members["星极"] = { 1, 0 };
    fixture.after.inventory.members["星极"] = { 0, 1 };
    fixture.rewards = { reward_for(fixture.transaction.scope, "reward-1", "星极", 30),
                        reward_for(fixture.transaction.scope, "reward-2", "灰喉", 40) };
    result = fixture.reconcile();
    assert(result.verified && result.promoted_operators.size() == 2);
    assert(result.random_initial_grants.size() == 1 && result.confirmed_initial_rewards.size() == 2);
    assert(result.initial_value_after - result.initial_value_before == 3);
    auto bad = fixture;
    std::swap(bad.rewards[0].initial_operator_name, bad.rewards[1].initial_operator_name);
    assert_rejected(bad); // The second reward cannot precede the synthesis that enables it.

    // Equal total value alone cannot justify a reward cycle with no triggering promotion.
    bad = base_phone();
    bad.before.inventory.members["星极"] = { 1, 0 };
    bad.after.inventory.members = { { "“暴躁铁皮”", { 2, 0 } }, { "星极", { 0, 1 } }, { "灰喉", { 1, 0 } } };
    bad.rewards = fixture.rewards;
    assert_rejected(bad);

    // Missing, guessed, repeated or unchanged reward pages cannot manufacture receipts.
    for (int variation = 0; variation < 12; ++variation) {
        bad = promoted_phone();
        auto& receipt = bad.rewards.front();
        switch (variation) {
        case 0:
            bad.rewards.clear();
            break;
        case 1:
            receipt.receipt_id.clear();
            break;
        case 2:
            receipt.initial_operator_name = "unknown";
            break;
        case 3:
            receipt.initial_form_verified = false;
            break;
        case 4:
            receipt.selection_confirmed = false;
            break;
        case 5:
            receipt.page_advanced = false;
            break;
        case 6:
            receipt.scope.round += 1;
            break;
        case 7:
            receipt.scope.transaction_id = "other-phone";
            break;
        case 8:
            receipt.scope.execution_id = "other-run";
            break;
        case 9:
            receipt.selection_sequence = 20;
            break;
        case 10:
            receipt.confirmation_sequence = receipt.selection_sequence;
            break;
        case 11:
            receipt.confirmation_sequence = 100;
            break;
        }
        assert_rejected(bad);
    }
    bad = promoted_phone();
    bad.after.inventory.members["灰喉"].initial = 2;
    bad.rewards.push_back(reward_for(bad.transaction.scope, "reward-1", "灰喉", 40));
    assert_rejected(bad); // Duplicate receipt ID, even with otherwise distinct frames.
    bad.rewards.back().receipt_id = "reward-2";
    assert_rejected(bad); // No second synthesis reward opportunity.
    bad.rewards.back().selection_sequence = bad.rewards.front().selection_sequence;
    bad.rewards.back().confirmation_sequence = bad.rewards.front().confirmation_sequence;
    assert_rejected(bad); // Re-labelled same-frame receipt is still duplicated evidence.
    bad = promoted_phone();
    bad.after.inventory.members.erase("灰喉");
    bad.rewards.clear();
    assert_rejected(bad); // A resolved transaction cannot omit the real synthesis reward.

    // All evidence belongs to one known, uncommitted execution/transaction/round.
    for (int variation = 0; variation < 18; ++variation) {
        bad = base_phone();
        switch (variation) {
        case 0:
            bad.transaction.scope.execution_id.clear();
            break;
        case 1:
            bad.transaction.scope.transaction_id.clear();
            break;
        case 2:
            bad.transaction.scope.round = 0;
            break;
        case 3:
            bad.before.scope.round += 1;
            break;
        case 4:
            bad.after.scope.round += 1;
            break;
        case 5:
            bad.after.scope.execution_id = "other-run";
            break;
        case 6:
            bad.before.scope.transaction_id = "old-phone";
            break;
        case 7:
            bad.transaction.already_committed = true;
            break;
        case 8:
            bad.transaction.first_pact_verified = false;
            break;
        case 9:
            bad.transaction.quote_verified = false;
            break;
        case 10:
            bad.transaction.item_consumption_verified = false;
            break;
        case 11:
            bad.transaction.isolated_roster_changes_verified = false;
            break;
        case 12:
            bad.transaction.promotion_rewards_resolved = false;
            break;
        case 13:
            bad.before.observation_sequence = 0;
            break;
        case 14:
            bad.before.observation_sequence = bad.transaction.contact_sequence;
            break;
        case 15:
            bad.after.observation_sequence = bad.transaction.contact_sequence;
            break;
        case 16:
            bad.transaction.initial_grants_quote = 0;
            break;
        case 17:
            bad.transaction.initial_grants_quote = 3;
            break;
        }
        assert_rejected(bad);
    }

    // Both physical snapshots require every identity and form, not name hints.
    for (int variation = 0; variation < 13; ++variation) {
        bad = base_phone();
        switch (variation) {
        case 0:
            bad.before.inventory.complete = false;
            break;
        case 1:
            bad.after.inventory.complete = false;
            break;
        case 2:
            bad.before.inventory.members["unknown"] = { 1, 0 };
            break;
        case 3:
            bad.after.inventory.members["unknown"] = { 1, 0 };
            break;
        case 4:
            bad.before.inventory.members["“暴躁铁皮”"].initial = -1;
            break;
        case 5:
            bad.after.inventory.members["初雪"].elite = -1;
            break;
        case 6:
            bad.after.inventory.members["初雪"].initial = 3;
            break;
        case 7:
            bad.before.inventory.members["“暴躁铁皮”"] = { 0, 0 };
            break;
        case 8:
            bad.before.inventory.members["“暴躁铁皮”"] = { 18, 18 };
            break;
        case 9:
            bad.before.inventory.members["“暴躁铁皮”"] = { 0, 19 };
            break;
        case 10:
            bad.after.inventory.members["幽灵鲨"] = { 0, 18 };
            break;
        case 11:
            bad.before.inventory.members["“暴躁铁皮”"].initial = std::numeric_limits<int>::max();
            break;
        case 12:
            bad.after.inventory.members.erase("“暴躁铁皮”");
            break;
        }
        assert_rejected(bad);
    }
    bad = base_phone();
    bad.after.inventory.members.erase("初雪");
    bad.after.inventory.members["灰喉"] = { 1, 0 };
    assert_rejected(bad); // Correct grant count, wrong pact.
    bad = base_phone();
    bad.transaction.recipient_first_pact = "谢拉格";
    assert_rejected(bad);
    bad.transaction.recipient_name = "初雪";
    bad.before.inventory.members["初雪"] = { 1, 0 };
    bad.transaction.recipient_first_pact = "协防干员";
    assert_rejected(bad); // A secondary pact cannot be silently used as the first pact.

    // A successful result is a replacement snapshot, not additive instructions.
    fixture = promoted_phone();
    result = fixture.reconcile();
    assert(result.verified);
    fixture.transaction.already_committed = true;
    assert_rejected(fixture);

    std::cout << "First-pact phone reconciliation and confirmed reward separation passed\n";
}
