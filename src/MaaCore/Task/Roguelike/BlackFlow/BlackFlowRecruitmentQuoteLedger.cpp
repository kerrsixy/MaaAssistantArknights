#include "Task/Roguelike/BlackFlow/BlackFlowRecruitmentQuoteLedger.h"

#include <algorithm>

#include <atomic>

#include <chrono>
#include <cmath>
#include <fstream>

#include <iomanip>

#include <limits>

#include <sstream>

#ifdef _WIN32

#include <Windows.h>

#endif

#include "Config/Roguelike/RoguelikeTrainingConfig.h"
#include "Utils/Platform.hpp"

namespace asst::blackflow::recruitment_quote_detail

{

bool same_account(const RecruitmentQuoteScope& a, const RecruitmentQuoteScope& b)

{
    return a.client == b.client && a.uid == b.uid && a.package == b.package && a.source_cohort == b.source_cohort;
}

json::value scope_json(const RecruitmentQuoteScope& s)

{
    return json::object { { "client", s.client },

                          { "uid", s.uid },

                          { "package", s.package },

                          { "squad", s.squad },

                          { "squad_source", s.squad_source },

                          { "source_cohort", s.source_cohort },

                          { "price_context", s.price_context },

                          { "price_context_complete", s.price_context_complete } };
}

bool integer_field(const json::value& value)
{
    if (!value.is_number()) {
        return false;
    }
    const auto number = value.as_double();
    return std::isfinite(number) && std::floor(number) == number && number >= std::numeric_limits<int>::min() &&
           number <= std::numeric_limits<int>::max();
}

bool fields(const json::value& v, std::initializer_list<const char*> names, bool strings)

{
    if (!v.is_object()) {
        return false;
    }

    return std::ranges::all_of(names, [&](const char* key) {
        return v.contains(key) && (strings ? v.at(key).is_string() : integer_field(v.at(key)));
    });
}

std::optional<RecruitmentObservedQuote> parse(const json::value& row)

{
    if (!fields(row, { "frame_path", "observation_id", "native_producer" }, true) ||

        row.at("native_producer").as_string() != "general_same_analysis_quote_v2" || !row.contains("scope") ||

        !row.contains("offer") || !row.contains("original_scan_eligible") ||

        !row.at("original_scan_eligible").is_boolean()) {
        return std::nullopt;
    }

    const auto& s = row.at("scope");

    const auto& o = row.at("offer");

    if (!fields(s, { "client", "uid", "package", "squad", "squad_source", "source_cohort", "price_context" }, true) ||

        !s.contains("price_context_complete") || !s.at("price_context_complete").is_boolean() ||

        !fields(

            o,

            { "client",

              "uid",

              "run_id",

              "name",

              "operator_id",

              "operation",

              "observed_at",

              "training_binding",

              "temporary_rule_source" },

            true) ||

        !fields(

            o,

            { "elite", "level", "hope_cost", "analysis_id", "observation_index", "price_index", "page_index" },

            false)) {
        return std::nullopt;
    }

    if (!o.contains("ordinary_origin") || !o.at("ordinary_origin").is_boolean() || !o.contains("positive_temporary") ||

        !o.at("positive_temporary").is_boolean() || !o.contains("observed_temporary") ||

        (!o.at("observed_temporary").is_null() && !o.at("observed_temporary").is_boolean())) {
        return std::nullopt;
    }

    for (const auto key : { "non_economic_eligible", "affordable_at_scan_balance" }) {
        if (!row.contains(key) || (!row.at(key).is_null() && !row.at(key).is_boolean())) {
            return std::nullopt;
        }
    }
    if (!row.contains("scan_hope_balance") ||
        (!row.at("scan_hope_balance").is_null() &&
         (!integer_field(row.at("scan_hope_balance")) || row.at("scan_hope_balance").as_integer() < 0))) {
        return std::nullopt;
    }
    RecruitmentObservedQuote out;

    out.scope = { s.at("client").as_string(),        s.at("uid").as_string(),

                  s.at("package").as_string(),       s.at("squad").as_string(),

                  s.at("squad_source").as_string(),  s.at("source_cohort").as_string(),

                  s.at("price_context").as_string(), s.at("price_context_complete").as_boolean() };

    auto& offer = out.offer;

    offer.client = o.at("client").as_string();

    offer.uid = o.at("uid").as_string();

    offer.run_id = o.at("run_id").as_string();

    offer.name = o.at("name").as_string();

    offer.id = o.at("operator_id").as_string();

    offer.operation = o.at("operation").as_string();

    offer.observed_at = o.at("observed_at").as_string();

    offer.training_binding = o.at("training_binding").as_string();

    offer.temporary_rule_source = o.at("temporary_rule_source").as_string();

    offer.elite = o.at("elite").as_integer();

    offer.level = o.at("level").as_integer();

    offer.hope_cost = o.at("hope_cost").as_integer();

    const auto analysis = o.at("analysis_id").as_integer();

    const auto card = o.at("observation_index").as_integer();

    const auto price = o.at("price_index").as_integer();

    offer.page_index = o.at("page_index").as_integer();

    if (analysis <= 0 || card < 0 || price < 0 || offer.page_index < 0 || offer.elite < 0 || offer.elite > 2 ||

        offer.level <= 0 || *offer.hope_cost < 0 || *offer.hope_cost > 999 || offer.operation != "招募" ||

        offer.run_id.empty() || offer.observed_at.empty() ||

        offer.id != RoguelikeTraining.unique_identity(offer.name) || offer.client != out.scope.client ||

        offer.uid != out.scope.uid || !out.scope.valid()) {
        return std::nullopt;
    }

    offer.analysis_id = static_cast<std::uint64_t>(analysis);

    offer.observation_index = static_cast<std::size_t>(card);

    offer.price_index = static_cast<std::size_t>(price);

    out.frame_path = row.at("frame_path").as_string();

    out.observation_id = row.at("observation_id").as_string();

    out.original_scan_eligible = row.at("original_scan_eligible").as_boolean();
    if (!row.at("non_economic_eligible").is_null()) {
        out.non_economic_eligible = row.at("non_economic_eligible").as_boolean();
    }
    if (!row.at("affordable_at_scan_balance").is_null()) {
        out.affordable_at_scan_balance = row.at("affordable_at_scan_balance").as_boolean();
    }
    if (!row.at("scan_hope_balance").is_null()) {
        out.scan_hope_balance = row.at("scan_hope_balance").as_integer();
    }

    if (out.frame_path.empty() || out.observation_id.empty()) {
        return std::nullopt;
    }

    // A positive price source rule is not UI negative-badge evidence.

    offer.ordinary_origin = o.at("ordinary_origin").as_boolean();

    offer.positive_temporary = o.at("positive_temporary").as_boolean();

    if (!o.at("observed_temporary").is_null()) {
        offer.observed_temporary = o.at("observed_temporary").as_boolean();
    }

    return out;
}

bool same_quote(const RecruitmentObservedQuote& a, const RecruitmentObservedQuote& b)

{
    return a.scope.squad == b.scope.squad && a.scope.price_context == b.scope.price_context &&

           a.offer.id == b.offer.id && a.offer.elite == b.offer.elite && a.offer.level == b.offer.level &&

           a.offer.ordinary_origin == b.offer.ordinary_origin &&
           a.offer.positive_temporary == b.offer.positive_temporary &&
           a.offer.observed_temporary == b.offer.observed_temporary &&
           a.offer.training_binding == b.offer.training_binding;
}

}

namespace asst::blackflow

{

bool RecruitmentQuoteScope::valid() const

{
    const bool package_matches = (client == "Official" && package == "com.hypergryph.arknights") ||

                                 (client == "Bilibili" && package == "com.hypergryph.arknights.bilibili");

    const auto context = json::parse(price_context);
    if (!context || !context->is_object() || !context->contains("observed_difficulty") ||
        !recruitment_quote_detail::integer_field(context->at("observed_difficulty")) ||
        context->at("observed_difficulty").as_integer() < 0 || !context->contains("all_price_modifiers_complete") ||
        !context->at("all_price_modifiers_complete").is_boolean() ||
        context->at("all_price_modifiers_complete").as_boolean() != price_context_complete ||
        !context->contains("observed_collections_partial") || !context->at("observed_collections_partial").is_array() ||
        !std::ranges::all_of(context->at("observed_collections_partial").as_array(), [](const auto& value) {
            return value.is_string();
        })) {
        return false;
    }
    return package_matches && !uid.empty() && uid.size() <= 64 && !squad.empty() && squad.size() <= 256 &&

           squad_source == "native_unlocked_squad_card_selection" && !source_cohort.empty() &&

           source_cohort.size() <= 128 && !price_context.empty() && price_context.size() <= 16'384;
}

std::string RecruitmentQuoteLedger::source_cohort(const std::filesystem::path& resource)

{
    // Content fingerprint, not a cryptographic signature or game-version claim.

    std::uint64_t value = 14'695'981'039'346'656'037ULL;

    for (const auto name : { "battle_data.json",

                             "roguelike/BlackFlow/operator_training.json",

                             "roguelike/BlackFlow/recruitment.json",

                             "roguelike/BlackFlow/recruitment_duties.json" }) {
        std::ifstream stream(resource / utils::path(name), std::ios::binary);

        if (!stream) {
            return {};
        }

        for (char c; stream.get(c);) {
            value = (value ^ static_cast<unsigned char>(c)) * 1'099'511'628'211ULL;
        }

        if (!stream.eof()) {
            return {};
        }

        value = (value ^ 0xffU) * 1'099'511'628'211ULL;
    }

    std::ostringstream output;

    output << "fnv1a64-content-v1:" << std::hex << value;

    return output.str();
}

std::vector<RecruitmentObservedQuote> RecruitmentQuoteLedger::capture(

    const RoguelikeRecruitImageAnalyzer& analyzer,

    const std::vector<RecruitmentDutyOffer>& offers,

    const json::value& page,

    const RecruitmentQuoteScope& scope)

{
    std::vector<RecruitmentObservedQuote> out;

    if (!scope.valid() || !page.is_object() || !page.get("frame_saved", false) ||

        !page.get("analysis_succeeded", false) || page.get("initial_recruitment", true) ||
        page.get("analysis_id", 0ULL) != analyzer.get_analysis_id() || page.get("client", "") != scope.client ||
        page.get("uid", "") != scope.uid || page.get("frame_path", "").empty() ||
        page.get("observation_id", "").empty()) {
        return out;
    }

    for (const auto& offer : offers) {
        if (offer.analysis_id != analyzer.get_analysis_id() || offer.client != scope.client || offer.uid != scope.uid ||

            offer.run_id != page.get("run_id", "") || offer.run_id.empty() || offer.observed_at.empty() ||

            offer.observed_at != page.get("frame_read_completed_at_local", "") || offer.operation != "招募" ||

            !offer.hope_cost || *offer.hope_cost < 0 || offer.elite < 0 || offer.elite > 2 || offer.level <= 0 ||

            !offer.price_index || offer.id != RoguelikeTraining.unique_identity(offer.name)) {
            continue;
        }

        const auto& cards = analyzer.get_candidate_evidence();

        const auto found = std::ranges::find_if(cards, [&](const auto& c) {
            return c.analysis_id == offer.analysis_id && c.observation_index == offer.observation_index &&

                   c.name == offer.name && c.elite == offer.elite && c.level == offer.level;
        });

        if (found == cards.end()) {
            continue;
        }

        const auto exact = analyzer.resolve_candidate_hope_cost(*found);

        if (!exact || found->hope_cost_evidence_index != offer.price_index ||

            exact->get().hope_cost != offer.hope_cost || exact->get().operation != offer.operation ||

            exact->get().operation_score < 0.9) {
            continue;
        }

        RecruitmentObservedQuote quote { scope,
                                         offer,
                                         page.at("frame_path").as_string(),
                                         page.at("observation_id").as_string(),
                                         false };
        if (page.contains("legacy_filter_hope") &&
            recruitment_quote_detail::integer_field(page.at("legacy_filter_hope")) &&
            page.at("legacy_filter_hope").as_integer() >= 0) {
            quote.scan_hope_balance = page.at("legacy_filter_hope").as_integer();
        }
        if (*offer.hope_cost == 0) {
            quote.affordable_at_scan_balance = true;
        }
        else if (quote.scan_hope_balance) {
            quote.affordable_at_scan_balance = *offer.hope_cost <= *quote.scan_hope_balance;
        }
        out.push_back(std::move(quote));
    }

    return out;
}

bool RecruitmentQuoteLedger::bind(const RecruitmentQuoteScope& scope)

{
    if (!scope.valid()) {
        m_scope = {};

        m_entries.clear();

        return false;
    }

    if (!recruitment_quote_detail::same_account(m_scope, scope)) {
        m_entries.clear();
    }

    m_scope = scope;

    return true;
}

void RecruitmentQuoteLedger::append(
    std::vector<RecruitmentObservedQuote> observations,
    const std::vector<std::pair<std::string, int>>& eligible)
{
    // Compatibility for existing qualified callers: a selected-list witness
    // proves non-economic eligibility only for its positively admitted cards.
    // An absent card is unknown, never proof of a non-economic veto.
    std::vector<RecruitmentQuoteEligibility> evidence;
    for (const auto& quote : observations) {
        const bool accepted =
            std::ranges::find(eligible, std::pair { quote.offer.name, quote.offer.page_index }) != eligible.end();
        evidence.push_back(
            { quote.offer.analysis_id,
              quote.offer.observation_index,
              quote.offer.name,
              quote.offer.page_index,
              accepted ? std::optional<bool>(true) : std::nullopt,
              quote.affordable_at_scan_balance.value_or(false),
              accepted });
    }
    append_with_non_economic_eligibility(std::move(observations), evidence);
}

void RecruitmentQuoteLedger::append_with_non_economic_eligibility(
    std::vector<RecruitmentObservedQuote> observations,
    const std::vector<RecruitmentQuoteEligibility>& eligibility)
{
    for (auto& quote : observations) {
        if (!m_scope.valid() || !recruitment_quote_detail::same_account(m_scope, quote.scope)) {
            continue;
        }
        const auto same = [&](const auto& e) {
            return e.analysis_id == quote.offer.analysis_id && e.observation_index == quote.offer.observation_index &&
                   e.name == quote.offer.name && e.page_index == quote.offer.page_index;
        };
        const auto found = std::ranges::find_if(eligibility, same);
        quote.original_scan_eligible = false;
        quote.non_economic_eligible.reset();
        if (found != eligibility.end() && std::ranges::count_if(eligibility, same) == 1) {
            quote.original_scan_eligible = found->selection_eligible;
            quote.non_economic_eligible = found->non_economic_eligible;
            if (quote.scan_hope_balance || quote.offer.hope_cost == 0) {
                quote.affordable_at_scan_balance = found->affordable;
            }
        }
        const auto existing = std::ranges::find_if(m_entries, [&](const auto& row) {
            return recruitment_quote_detail::same_quote(row, quote);
        });
        if (existing != m_entries.end()) {
            m_entries.erase(existing);
        }
        m_entries.push_back(std::move(quote));
        if (m_entries.size() > MaximumEntries) {
            m_entries.erase(m_entries.begin());
        }
    }
}

json::value recruitment_quote_to_json(const RecruitmentObservedQuote& q)

{
    const auto& o = q.offer;

    return json::object {
        { "native_producer", "general_same_analysis_quote_v2" },

        { "scope", recruitment_quote_detail::scope_json(q.scope) },

        { "frame_path", q.frame_path },

        { "observation_id", q.observation_id },

        { "original_scan_eligible", q.original_scan_eligible },
        { "non_economic_eligible",
          q.non_economic_eligible ? json::value(*q.non_economic_eligible) : json::value(nullptr) },
        { "affordable_at_scan_balance",
          q.affordable_at_scan_balance ? json::value(*q.affordable_at_scan_balance) : json::value(nullptr) },
        { "scan_hope_balance", q.scan_hope_balance ? json::value(*q.scan_hope_balance) : json::value(nullptr) },

        { "offer",

          json::object { { "client", o.client },

                         { "uid", o.uid },

                         { "run_id", o.run_id },

                         { "name", o.name },

                         { "operator_id", o.id },

                         { "elite", o.elite },

                         { "level", o.level },

                         { "hope_cost", o.hope_cost ? json::value(*o.hope_cost) : json::value(nullptr) },

                         { "analysis_id", o.analysis_id },

                         { "observation_index", o.observation_index },

                         { "price_index", o.price_index ? json::value(*o.price_index) : json::value(nullptr) },

                         { "page_index", o.page_index },

                         { "operation", o.operation },

                         { "observed_at", o.observed_at },

                         { "ordinary_origin", o.ordinary_origin },

                         { "positive_temporary", o.positive_temporary },

                         { "observed_temporary",

                           o.observed_temporary ? json::value(*o.observed_temporary) : json::value(nullptr) },

                         { "training_binding", o.training_binding },

                         { "temporary_rule_source", o.temporary_rule_source } } }
    };
}

json::value RecruitmentQuoteLedger::to_json() const

{
    json::array entries;

    for (const auto& quote : m_entries) {
        entries.emplace_back(recruitment_quote_to_json(quote));
    }

    return json::object { { "schema", 2 },

                          { "producer", "native_general_same_analysis_quote_ledger" },

                          { "scope", recruitment_quote_detail::scope_json(m_scope) },

                          { "entries", entries } };
}

bool RecruitmentQuoteLedger::load(const std::filesystem::path& path, std::string* diagnostic)

{
    const auto fail = [&](const std::string& reason) {
        if (diagnostic) {
            *diagnostic = reason;
        }

        m_entries.clear();

        return false;
    };

    if (!m_scope.valid()) {
        return fail("quote_scope_unknown");
    }

    std::error_code size_error;

    if (std::filesystem::file_size(path, size_error) > 16U * 1024U * 1024U || size_error) {
        return fail("quote_file_missing_unreadable_or_oversized");
    }

    const auto value = json::open(path);

    if (!value || !value->is_object() || value->get("schema", 0) != 2 ||

        value->get("producer", "") != "native_general_same_analysis_quote_ledger" || !value->contains("entries") ||

        !value->at("entries").is_array() || value->at("entries").as_array().size() > MaximumEntries) {
        return fail("quote_file_missing_legacy_invalid_or_unbounded");
    }

    std::vector<RecruitmentObservedQuote> accepted;

    for (const auto& row : value->at("entries").as_array()) {
        auto parsed = recruitment_quote_detail::parse(row);

        if (!parsed || !recruitment_quote_detail::same_account(parsed->scope, m_scope)) {
            return fail("quote_row_invalid_or_account_cohort_mismatch");
        }

        const auto prior = std::ranges::find_if(accepted, [&](const auto& existing) {
            return recruitment_quote_detail::same_quote(existing, *parsed);
        });
        if (prior != accepted.end()) {
            accepted.erase(prior);
        }
        accepted.push_back(std::move(*parsed));
    }

    m_entries = std::move(accepted);

    if (diagnostic) {
        *diagnostic = "native_quotes_loaded_not_current_offers";
    }

    return true;
}

bool RecruitmentQuoteLedger::save(const std::filesystem::path& path, std::string* diagnostic) const

{
    std::filesystem::path staged;

    const auto fail = [&](const std::string& reason) {
        if (!staged.empty()) {
            std::error_code cleanup_error;

            std::filesystem::remove(staged, cleanup_error);
        }

        if (diagnostic) {
            *diagnostic = reason;
        }

        return false;
    };

    if (!m_scope.valid() || path.empty()) {
        return fail("quote_scope_or_sidecar_path_unknown");
    }

    std::error_code ec;

    std::filesystem::create_directories(path.parent_path(), ec);

    if (ec) {
        return fail("quote_sidecar_directory_unavailable");
    }

    static std::atomic<unsigned long long> sequence { 0 };

    staged = path;

    staged += utils::path(

        ".staged-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +

        std::to_string(++sequence));

    const auto value = to_json();

    const auto text = value.to_string();

    if (text.size() > 16U * 1024U * 1024U) {
        return fail("quote_sidecar_bounded_size_exceeded");
    }

    {
        std::ofstream stream(staged, std::ios::binary | std::ios::trunc);

        stream << text;

        stream.flush();

        if (!stream) {
            return fail("quote_sidecar_staging_failed");
        }
    }

    const auto reread = json::open(staged);

    if (!reread || reread->to_string() != value.to_string()) {
        return fail("quote_sidecar_reread_failed");
    }

#ifdef _WIN32

    if (!MoveFileExW(staged.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return fail("quote_sidecar_atomic_replace_failed");
    }

#else

    std::filesystem::rename(staged, path, ec);

    if (ec) {
        return fail("quote_sidecar_atomic_replace_failed");
    }

#endif

    if (diagnostic) {
        *diagnostic = "native_quote_sidecar_saved";
    }

    return true;
}

}

