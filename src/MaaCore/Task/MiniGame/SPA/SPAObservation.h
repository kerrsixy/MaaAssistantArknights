#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace asst::spa
{
std::optional<int> parse_shop_level(const std::string& text);
// True means expanded (the control offers collapse), false means collapsed.
// The caller must restrict recognition to the shop-toggle control ROI.
std::optional<bool> parse_shop_toggle(const std::string& text);

struct TextObservation
{
    std::string text;
    double score = 0;
    int x = 0;
    int y = 0;
};

inline constexpr int HiddenCoreRound = 15;
enum class HiddenRoundMode
{
    Independent,
    JointRisk
};

bool is_round_badge_position(int x, int y);
bool is_battle_round_label(const TextObservation& text);
bool is_hidden_round_badge(const TextObservation& text);
std::optional<HiddenRoundMode> observe_hidden_round(const std::vector<TextObservation>& texts);

// Read only the CRT countdown digits; callers must verify the preparation
// page. Missing/ambiguous digits are unknown, never zero remaining seconds.
std::optional<int> preparation_countdown(const std::vector<TextObservation>& texts);
bool verified_loading_page(const std::vector<TextObservation>& texts);
// The scrollable pact list may hide its heading. Require fixed stage/ready
// anchors and pact-specific content on the same frame, including split OCR.
bool verified_pact_setup_page(const std::vector<TextObservation>& texts);

// A unique catalogue identity in the selected operator title, not a name
// mentioned in a description or an unreadable neighboring panel.
std::optional<std::string>
    operator_panel_identity(const std::vector<TextObservation>& texts, const std::vector<std::string>& known_names);
bool operator_title_contains_rank_artifact(const std::vector<TextObservation>& texts, const std::string& expected);
std::optional<double>
    single_operator_title_confidence(const std::vector<TextObservation>& texts, const std::string& expected);
bool repeated_single_operator_title(std::optional<double> first, std::optional<double> second);

struct OperatorCardObservation
{
    std::string name;
    int training_level = 0;
    bool elite = false;
};

// Identity and numeric-only LV observations must come from the same selected
// card frame. Unexpected/ambiguous levels remain unknown, not initial form.
std::optional<OperatorCardObservation> observe_operator_card(
    const std::vector<TextObservation>& texts,
    const std::vector<std::string>& known_names,
    std::string_view verified_panel_name = {});

std::optional<bool> observe_shop_toggle(const TextObservation& text);
// Only a live preparation frame for the expected round may expand the shop.
std::optional<TextObservation> shop_expansion_control(const std::vector<TextObservation>& texts, int expected_round);
// A unique, readable confirmation in the selected card column is required.
std::optional<TextObservation> purchase_confirmation_control(const std::vector<TextObservation>& texts, int slot);
bool bench_capacity_warning(const TextObservation& text);
bool equipment_capacity_warning(const TextObservation& text);
bool selected_purchase_capacity_warning(const TextObservation& text, bool equipment);
bool bench_replacement_title_needs_settle(const std::vector<TextObservation>& texts, const std::string& expected_name);
bool selected_purchase_unavailable(const std::vector<TextObservation>& texts, int slot);
// A reward-card selection opens its operator panel; it is not a purchase.
// Confirm only the expected panel identity and the matching card column.
std::optional<TextObservation> promotion_confirmation_control(
    const std::vector<TextObservation>& texts,
    const std::string& expected_name,
    int slot);
// A short name may read below the normal title threshold. Callers must
// independently verify a free reward shop and repeat this on a fresh frame.
std::optional<TextObservation> promotion_short_title_confirmation(
    const std::vector<TextObservation>& texts,
    const std::string& expected_name,
    int slot);
std::optional<bool> observe_shop_freeze(const TextObservation& text);
// Require a known warning and both unique modal buttons on the same frame.
// This includes the temporary inventory warning, not just remaining funds.
bool observe_ready_confirmation(const std::vector<TextObservation>& texts);
// Reuse the page observer's immutable frame. An unreviewed budget returns
// Cancel; only a reviewed preparation may return Confirm.
std::optional<TextObservation>
    ready_confirmation_control(const std::vector<TextObservation>& texts, bool spending_reviewed);

struct PactHudAnchor
{
    std::string pact;
    int center_x = 0;
    int label_y = 0;
};

// Both vectors must originate from one immutable preparation frame. Numeric
// token x coordinates are their centers, unlike general TextObservation OCR.
std::map<std::string, int> observe_pact_layer_numbers(
    const std::vector<PactHudAnchor>& anchors,
    const std::vector<TextObservation>& centered_numbers);

struct DeploymentCounterObservation
{
    std::optional<int> value;
    int label_y = -1;
    bool contradictory = false;
};

DeploymentCounterObservation observe_deployment_counter(const std::vector<TextObservation>& texts);

struct StrategyIdentity
{
    std::string band_id;
    std::string name;
    std::string leader;
};

// Text prerequisites for the selected strategy's completion mark. The
// caller must match the actual mark on this same frame before crediting it.
bool
    strategy_report_identity_text_verified(const std::vector<TextObservation>& texts, const StrategyIdentity& selected);
bool strategy_report_progress_text_verified(
    const std::vector<TextObservation>& texts,
    const StrategyIdentity& selected,
    int wins);
bool strategy_badge_report_text_verified(
    const std::vector<TextObservation>& texts,
    const StrategyIdentity& selected,
    int badge_wins);

enum class Page
{
    Unknown,
    Lobby,
    StrategyReport,
    Difficulty,
    Preparation,
    EquipmentSupply,
    Placement,
    Battle,
    Result,
    RunSetup,
    AuthenticationExpired,
    BountySelection,
    TacticalDecision,
    DataUpdated,
    ReadyConfirmation,
    ResultSummary,
    Spectating,
    ReadyWaiting
};

struct PageObservation
{
    Page page = Page::Unknown;
    std::optional<std::string> band_id;
    std::optional<int> wins;
    std::optional<int> selected_difficulty;
    std::optional<int> round;
    // The Sequence I extra round displays ?? in eligible modes.
    std::optional<HiddenRoundMode> bonus_round;
    std::optional<int> remaining_deployment;
    std::optional<int> funds;
    std::optional<int> free_refreshes;
    // Independent paid badge anchored immediately above the refresh control.
    // A missing badge is unknown, never evidence that free charges are zero.
    std::optional<int> paid_refresh_price;
    std::optional<int> shop_level;
    // The number shown above the LEVEL tile is the price of the next shop
    // upgrade, not the current shop level. Keep both values separate.
    std::optional<int> upgrade_price;
    std::optional<int> enemies_resolved;
    std::optional<int> enemies_total;
    std::optional<int> passed_rounds;
    // Local summary and alliance aggregate are distinct game metrics.
    std::optional<int> personal_passed_rounds;
    bool personal_result_completed = false;
    bool personal_result_interrupted = false;
    std::string personal_result_difficulty_name;
    std::string personal_result_identity;
    std::optional<int> result_difficulty;
    // Named joint difficulty is not a solo regulation level.
    bool result_joint = false;
    std::string result_difficulty_name;
    // Settlement pages share the same layout for completion and interruption.
    // Keep the explicit banner state so a result cannot be credited as a win
    // merely because the page contains a round count.
    bool result_completed = false;
    bool result_interrupted = false;
    bool contradictory = false;
};

// Coordinates refer to the controller's normalized 1280x720 game image.
bool settlement_text_verified(const PageObservation& observed);

PageObservation
    observe_page(const std::vector<TextObservation>& texts, const std::vector<StrategyIdentity>& strategies);
std::vector<StrategyIdentity> strategy_identities();
std::optional<int> shop_column_from_right(int name_x);
std::optional<int> equipment_supply_slot(int title_x);
bool deployment_source_is_bench(int x, int y);
}
