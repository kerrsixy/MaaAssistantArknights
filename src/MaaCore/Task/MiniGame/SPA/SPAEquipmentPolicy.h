#pragma once
#include "SPAObservation.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace asst::spa
{
enum class EquipmentRole
{
    Damage,
    Defense,
    Growth,
    Economy,
    Sustain,
    Tradeoff,
    SkillRecovery,
    Control,
    Redeployment,
    Unknown
};

struct EquipmentProfile
{
    std::string name;
    int price = 0;
    EquipmentRole role = EquipmentRole::Unknown;
    bool consumable = false;
    bool requires_healing = false;
    int score = 0;
    // Equipment tier follows the dispatch-center/shop tier.  It is not the
    // same value as the purchase price: a tier-4 item may cost 1/5 or 2,
    // while tier-6 items are late-game offers.  Zero means “legacy profile
    // without a reviewed tier”, never “tier 0 available in shop”.
    int tier = 0;
    std::string normal_effect;
    std::string advanced_effect;
};

struct EquipmentContext
{
    int funds = 0;
    int round = 1;
    int boss_round = 0;
    bool target_deployed = false;
    bool target_elite = false;
    bool healer_covers = false;
    bool primary_active = false;
    bool target_is_carry = false;
    // Intentional self-damage/redeploy loops (for example Gravel + source
    // stone solvent under Unyielding) must be allowed to use damage-over-time
    // equipment even without healer coverage.
    bool intentional_redeploy_loop = false;
    int shop_level = 0;
    bool free_supply = false;
    // Set only by the single-body projection scheduler after recipient,
    // initial form, free slot and a positive modeled benefit are available.
    bool direct_promotion_usable = false;
    bool freeze_engine_assignment = false;
};

struct EquipmentDecision
{
    bool buy = false;
    std::string name;
    EquipmentRole role = EquipmentRole::Unknown;
    int score = 0;
    std::string reason;
};

const std::vector<EquipmentProfile>& equipment_profiles();
EquipmentDecision choose_equipment(const EquipmentContext&, const std::vector<EquipmentProfile>& offers);
// Only a selected coin's explicit destruction/cash description is accepted;
// a currency icon or an unrelated number is not an amount quote.
std::optional<int> equipment_cash_gain(const std::string& name, const std::string& description);
std::optional<int> identity_tag_growth_quote(std::string_view description);
bool safe_identity_tag_recipient(
    const std::string& name,
    const std::string& primary,
    bool elite,
    const std::vector<std::string>& equipped);
bool is_deferred_cash_equipment(std::string_view name);
bool is_recurring_cash_equipment(std::string_view name);
std::optional<int> equipment_next_round_cash_gain(const std::string& name, const std::string& description);
bool profitable_next_round_cash_offer(const EquipmentContext& context, const EquipmentProfile& offer);
std::optional<int> next_round_cash_funding_deficit(
    EquipmentContext context,
    const EquipmentProfile& offer,
    int reserved_purchase_price);
bool equipment_title_matches(const std::string& expected, const std::string& observed);
int equipment_capacity(const std::string& recipient, bool elite);
// A visible same-frame recipient title is required even for an empty list.
// This returns only this viewport; it does not imply hidden slots were read.
std::optional<std::vector<std::string>> equipment_panel_items(
    const std::vector<TextObservation>& texts,
    const std::string& recipient,
    const std::vector<TextObservation>& same_frame_title = {});
// Complete, same-recipient slot observations only. A transformation item
// supplies membership; ordinary equipment only selects its possible pact.
// Multiple distinct selectors have no verified ordering rule and stay unknown.
std::vector<std::string> equipment_added_pacts(const std::vector<std::string>& equipped, bool slots_known = false);
// The pager uses the first ordered season pact. Require an observed empty
// equipment list to rule out a transfer item changing that pact.
bool safe_pact_recruitment_recipient(
    const std::string& name,
    const std::string& primary,
    bool equipment_known,
    const std::vector<std::string>& equipped);
bool pact_recruitment_offer_eligible(const std::string& name, const std::string& pact, int shop_level);

using RejectedEquipmentPairs = std::set<std::pair<std::string, std::string>>;

// Verified shop purchases provide hints for another live source search.
// This tracks unresolved purchases, not a complete equipment inventory.
class PersistentEquipmentPreparation
{
public:
    void begin_round(int round);
    bool within_attempt_budget(bool solo_boss_finalization, bool solo_shop_follow_up = false) const;

    bool has_verified_assignment(int round) const { return m_round == round && m_verified_assignments > 0; }

    bool may_try(const std::string& name) const;
    void record_purchase(const std::string& name);
    void observe_field(const std::string& map, const std::multimap<std::string, std::pair<int, int>>& poses);
    const RejectedEquipmentPairs& rejected_pairs() const { return m_rejected_pairs; }
    // Returns true when a verified assignment leaves another purchased item to find.
    bool record_assignment(
        const std::string& name,
        bool verified,
        bool committed_unverified,
        bool solo_boss_finalization,
        std::optional<std::string_view> rejected_recipient = std::nullopt);

private:
    int m_round = -1;
    int m_attempts = 0;
    int m_verified_assignments = 0;
    int m_boss_finalization_failures = 0;
    std::set<std::string> m_failed_names;
    std::map<std::string, int> m_unassigned_purchases;
    std::string m_field_map;
    std::multimap<std::string, std::pair<int, int>> m_field_poses;
    RejectedEquipmentPairs m_rejected_pairs;
};

struct EquipmentRecipient
{
    std::string name;
    bool slots_known = false;
    std::vector<std::string> equipped;
    bool frontline = false;
    bool healer = false;
    bool attacks = false;
    bool carry = false;
    bool skill_growth_engine = false;
    bool active_own_pact = false;
    int max_range_distance = 0;
    bool deliberate_redeploy = false;
    bool elite = false;
    bool freeze_skill_engine = false;
};

struct EquipmentAssignmentContext
{
    bool field_has_healer = false;
    bool status_source_known = false;
    bool elemental_source_known = false;
    int special_hammer_types = 0;
    bool growth_battle_available = true;
    bool kjerag_freeze_growth_active = false;
    int first_equipment_layers = 0;
};

struct EquipmentAssignment
{
    bool found = false;
    std::string equipment;
    std::string recipient;
    int score = 0;
};

struct IdentityTagTarget
{
    std::string recipient;
    std::string pact;
};

std::optional<IdentityTagTarget> choose_identity_tag_target(
    const std::string& primary,
    const std::vector<std::string>& support_pacts,
    const std::vector<std::string>& disabled_pacts,
    const std::map<std::string, int>& observed_layers,
    const std::vector<EquipmentRecipient>& recipients,
    bool allow_observed_secondary);

// Persistent items only. Candidate names are inventory hints; source identity,
// target identity, available slots and the post-drop item need live checks.
EquipmentAssignment choose_equipment_assignment(
    const EquipmentAssignmentContext& context,
    const std::vector<std::string>& candidates,
    const std::vector<EquipmentRecipient>& recipients,
    const RejectedEquipmentPairs& rejected_pairs = {});
}
