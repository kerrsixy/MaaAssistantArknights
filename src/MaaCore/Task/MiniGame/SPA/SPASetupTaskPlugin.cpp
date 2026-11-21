#include "SPASetupTaskPlugin.h"
#include "SPABossPolicy.h"
#include "SPAFormation.h"
#include "SPAImageEvidence.h"
#include "SPAObservation.h"
#include "SPAProjection.h"
#include "SPASetupSelection.h"

#include <algorithm>
#include <set>

#include "Controller/Controller.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Task/ProcessTask.h"
#include "Vision/OCRer.h"
#include <opencv2/imgproc.hpp>

namespace
{
std::string compact(std::string value)
{
    std::erase(value, ' ');
    std::erase(value, '\t');
    return value;
}

bool is_known_core(const std::string& text)
{
    static const auto rules = asst::spa::pact_requirements();
    return std::any_of(rules.begin(), rules.end(), [&](const auto& rule) { return rule.core && rule.name == text; });
}

bool is_known_support(const std::string& text)
{
    static const auto rules = asst::spa::pact_requirements();
    return std::any_of(rules.begin(), rules.end(), [&](const auto& rule) { return !rule.core && rule.name == text; });
}
}

bool asst::spa::SetupTaskPlugin::configure(
    std::string core_pact,
    std::vector<std::string> support_pacts,
    int strategy_index)
{
    if (core_pact.empty() || core_pact.size() > 64 || support_pacts.size() > 8 || strategy_index < -1 ||
        strategy_index >= 16) {
        return false;
    }
    for (const auto& pact : support_pacts) {
        if (pact.empty() || pact.size() > 64) {
            return false;
        }
    }
    m_core_pact = std::move(core_pact);
    m_auto_core_probe = m_core_pact == "auto";
    m_support_pacts = std::move(support_pacts);
    m_disabled_pacts.clear();
    m_strategy_index = strategy_index;
    return true;
}

bool asst::spa::SetupTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    return msg == AsstMsg::SubTaskStart && details.get("subtask", std::string()) == "ProcessTask" &&
           details.get("details", "task", std::string()) == "MiniGame@SPA@SetupPacts";
}

bool asst::spa::SetupTaskPlugin::selected(const cv::Mat& image, const PactText& pact) const
{
    return pact_icon_available(image, make_rect<cv::Rect>(pact.rect), pact.core) == true;
}

std::optional<asst::spa::SetupTaskPlugin::PactText>
    asst::spa::SetupTaskPlugin::find_pact(const std::vector<PactText>& pacts, const std::string& name) const
{
    for (const auto& pact : pacts) {
        if (pact.name == name) {
            return pact;
        }
    }
    return std::nullopt;
}

bool asst::spa::SetupTaskPlugin::_run()
{
    if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
        process->set_task_delay(180);
    }
    if (m_core_pact.empty()) {
        return false;
    }
    bool availability_after_selection_verified = false;
    cv::Mat parsed_image;
    auto parse = [&]() -> std::optional<std::vector<PactText>> {
        parsed_image = ctrler()->get_image();
        const auto& image = parsed_image;
        if (image.empty() || image.size() != cv::Size(1280, 720)) {
            return std::nullopt;
        }
        // Pact names and the support row occupy the left/centre 1050 px. The
        // boss preview on the far right is irrelevant to selection and makes
        // full-frame OCR needlessly expensive on MuMu.
        OCRer ocr(image, Rect(0, 80, 1050, 640));
        const auto recognized = ocr.analyze();
        if (!recognized) {
            return std::nullopt;
        }
        std::vector<PactText> pacts;
        for (const auto& text : *recognized) {
            if (text.score < 0.82) {
                continue;
            }
            const auto name = compact(text.text);
            // Both groups move with the left list. Fixed unscrolled y bands
            // discarded the auxiliary names precisely when scrolling made
            // the lower banned row visible.
            const bool in_list = text.rect.y >= 180 && text.rect.y < 710;
            const bool core = in_list && text.rect.x < 900 && is_known_core(name);
            const bool support = in_list && text.rect.x < 1000 && is_known_support(name);
            if (core || support) {
                if (std::none_of(pacts.begin(), pacts.end(), [&](const auto& item) { return item.name == name; })) {
                    pacts.push_back({ name, text.rect, core });
                }
            }
        }
        return pacts;
    };
    const auto visible_selected_core = [&](const std::vector<PactText>& visible,
                                           const std::set<std::string>& allowed_group = {}) -> std::optional<PactText> {
        std::vector<SetupCoreObservation> cores;
        for (const auto& pact : visible) {
            if (pact.core) {
                cores.push_back({ pact.name, pact_icon_available(parsed_image, make_rect<cv::Rect>(pact.rect), true) });
            }
        }
        const auto name = choose_verified_setup_core(cores, m_core_pact, allowed_group);
        return name ? find_pact(visible, *name) : std::nullopt;
    };

    std::set<std::string> observed_pacts;
    const auto collect_disabled = [&](const std::vector<PactText>& visible) {
        // Label coordinates and icon pixels must come from the same frame.
        // Inertial scrolling can move the list during an OCR call.
        const auto& image = parsed_image;
        if (image.empty()) {
            return;
        }
        for (const auto& pact : visible) {
            // A label can remain visible while its icon is clipped under the
            // fixed header. Missing icon pixels are unknown, never a ban.
            const int center_y = pact.rect.y - (pact.core ? 50 : 34);
            if (center_y - 24 < 175 || center_y + 24 >= 710) {
                continue;
            }
            const auto available = pact_icon_available(image, make_rect<cv::Rect>(pact.rect), pact.core);
            if (!available) {
                continue;
            }
            observed_pacts.insert(pact.name);
            // Core gray means the other, unselected group. Only the final
            // selected-group snapshot describes this run's availability.
            // A later green observation supersedes an earlier gray one.
            if (*available) {
                std::erase(m_disabled_pacts, pact.name);
            }
            if (!*available &&
                std::find(m_disabled_pacts.begin(), m_disabled_pacts.end(), pact.name) == m_disabled_pacts.end()) {
                m_disabled_pacts.emplace_back(pact.name);
            }
        }
    };
    const auto disabled_json = [&]() {
        json::array values;
        for (const auto& name : m_disabled_pacts) {
            values.emplace_back(name);
        }
        return values;
    };

    // Parse the pact cards first.  On the normal 1/3 page this is sufficient
    // to continue and avoids an extra header-only OCR pass.  The header probe
    // is kept as a fallback for retries that start after the page has already
    // advanced to 2/3 or 3/3.
    const auto stage_image = ctrler()->get_image();
    OCRer boss_ocr(stage_image, Rect(900, 310, 370, 120));
    if (const auto recognized = boss_ocr.analyze()) {
        std::vector<TextObservation> names;
        for (const auto& text : *recognized) {
            names.push_back({ text.text, text.score, text.rect.x, text.rect.y });
        }
        if (const auto family = advertised_boss_family(names)) {
            auto advertised = basic_info_with_what("SPABossAdvertised");
            advertised["details"] = json::object { { "boss_family_name", *family },
                                                   { "boss_advertisement_verified", true },
                                                   { "source", "setup_enemy_leader_title" },
                                                   { "variant_verified", false },
                                                   { "boss_battle_started", false } };
            callback(AsstMsg::SubTaskExtraInfo, advertised);
        }
    }
    auto pacts = parse();
    if (!pacts || pacts->empty()) {
        OCRer stage_probe(stage_image, Rect(400, 0, 520, 90));
        if (const auto stage_text = stage_probe.analyze()) {
            bool stage_three = false;
            bool stage_two = false;
            for (const auto& text : *stage_text) {
                const auto value = compact(text.text);
                if (text.score < 0.8) {
                    continue;
                }
                stage_three = stage_three || value.find("3/3") != std::string::npos ||
                              value.find("确定自选编队") != std::string::npos;
                stage_two =
                    stage_two || value.find("2/3") != std::string::npos || value.find("选择策略") != std::string::npos;
            }
            if (stage_three || stage_two) {
                if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
                    process->override_next(
                        "MiniGame@SPA@SetupPacts",
                        { stage_three ? "MiniGame@SPA@SelectFormation" : "MiniGame@SPA@SelectStrategy" });
                }
                auto info = basic_info_with_what("SPARunSetupVerified");
                info["details"] =
                    json::object { { "core_pact", "" },
                                   { "requested_core_pact", m_core_pact },
                                   { "core_selection_mode", m_auto_core_probe ? "provisional_auto" : "explicit" },
                                   { "provisional_core", true },
                                   { "support_pacts", json::array {} },
                                   { "disabled_pacts", disabled_json() },
                                   { "availability_after_selection_verified", availability_after_selection_verified },
                                   { "selection_verified", false },
                                   { "ready_clicked", false },
                                   { "destination_verified", true },
                                   { "advanced_before_task", true },
                                   { "stage", stage_three ? "3/3" : "2/3" } };
                callback(AsstMsg::SubTaskExtraInfo, info);
                return true;
            }
        }
    }
    if (!pacts) {
        LogError << "SPA setup OCR did not produce a verified pact list";
        return false;
    }
    collect_disabled(*pacts);

    // The 1/3 panel is vertically scrollable.  A requested/auto faction may
    // be below the first viewport, so make a small bounded number of scrolls
    // before declaring it unavailable.  Keep the gesture inside the pact
    // list; never scroll the right boss panel or the bottom ready button.
    const auto scroll_left_pact_list = [&]() {
        const Point from(430, 430);
        const Point to(430, 245);
        if (!ctrler()->swipe(from, to, 300)) {
            return false;
        }
        return sleep(260);
    };
    const auto scan_selected_availability = [&](const std::set<std::string>& selected_group) {
        observed_pacts.clear();
        m_disabled_pacts.clear();
        pacts = parse();
        if (!pacts) {
            return false;
        }
        collect_disabled(*pacts);
        // Read the full availability list after the core group is selected.
        // On this client the first auxiliary row fits in the
        // viewport while the remaining (often banned) row starts below it.
        // At most two short scrolls expose it; then restore and re-read the core
        // coordinates before any selection input.
        int availability_scrolls = 0;
        const auto expected_pacts = pact_requirements().size();
        save_evidence(parsed_image, utils::path("debug/SPA/setup_availability_top"), false);
        while (observed_pacts.size() < expected_pacts && availability_scrolls < 2) {
            if (!scroll_left_pact_list()) {
                return false;
            }
            ++availability_scrolls;
            pacts = parse();
            if (!pacts) {
                return false;
            }
            collect_disabled(*pacts);
            save_evidence(parsed_image, utils::path("debug/SPA/setup_availability_scroll"), false);
        }
        if (availability_scrolls > 0) {
            if (!ctrler()->swipe(Point(430, 245), Point(430, 660), 350) || !sleep(300)) {
                return false;
            }
            pacts = parse();
            if (!pacts) {
                return false;
            }
            collect_disabled(*pacts);
        }
        // A 300ms return swipe can still be in flight. In batch1 the first
        // restored labels were y223/y176, leaving every selected icon clipped
        // under the fixed header. Re-read before declaring the group lost;
        // if the return undershot, make at most one further upward-list reset.
        for (int probe = 0; probe < 3 && !visible_selected_core(*pacts, selected_group); ++probe) {
            if (probe < 2) {
                if (!sleep(200)) {
                    return false;
                }
            }
            else if (!ctrler()->swipe(Point(430, 245), Point(430, 660), 350) || !sleep(300)) {
                return false;
            }
            pacts = parse();
            if (!pacts) {
                return false;
            }
            collect_disabled(*pacts);
        }
        if (!visible_selected_core(*pacts, selected_group)) {
            return false;
        }
        availability_after_selection_verified = true;
        auto availability = basic_info_with_what("SPAPactAvailabilityScanned");
        json::array available_names;
        for (const auto& name : observed_pacts) {
            if (std::find(m_disabled_pacts.begin(), m_disabled_pacts.end(), name) == m_disabled_pacts.end()) {
                available_names.emplace_back(name);
            }
        }
        availability["details"] =
            json::object { { "complete", observed_pacts.size() == expected_pacts },
                           { "observed_count", observed_pacts.size() },
                           { "expected_count", expected_pacts },
                           { "available_pacts", available_names },
                           { "disabled_pacts", disabled_json() },
                           { "availability_after_selection_verified", availability_after_selection_verified },
                           { "after_core_selection_verified", true },
                           { "scrolls", availability_scrolls } };
        callback(AsstMsg::SubTaskExtraInfo, availability);
        return true;
    };
    if (!m_auto_core_probe && !find_pact(*pacts, m_core_pact)) {
        for (int scroll = 0; scroll < 3 && !find_pact(*pacts, m_core_pact); ++scroll) {
            if (!scroll_left_pact_list()) {
                return false;
            }
            pacts = parse();
            if (!pacts) {
                return false;
            }
            collect_disabled(*pacts);
        }
    }
    auto core = find_pact(*pacts, m_core_pact);
    if (m_auto_core_probe) {
        // Auto mode keeps the currently selected green group. Gray cores
        // belong to the alternative group and remain selectable; they are
        // not mode restrictions.  This is only an opening preference: the
        // actual faction is selected again from the visible 3/3 formation by
        // stacker/output density.
        // configure(auto) remains auto even after its first provisional name
        // was chosen. A retry may clip that member while a same-group member
        // is still green; do not click the unreadable icon as if it were gray.
        core = visible_selected_core(*pacts);
        for (int probe = 0; probe < 3 && !core; ++probe) {
            if (probe == 0) {
                if (!sleep(200)) {
                    return false;
                }
            }
            else if (!ctrler()->swipe(Point(430, 245), Point(430, 660), 350) || !sleep(300)) {
                return false;
            }
            pacts = parse();
            if (!pacts) {
                return false;
            }
            collect_disabled(*pacts);
            core = visible_selected_core(*pacts);
        }
        if (core) {
            m_core_pact = core->name;
        }
    }
    if (!core || !core->core) {
        // A single task chain may start after the countdown has already moved
        // past stage 1. Preserve that evidence and let the next stage plugin
        // take over rather than pretending the pact was selected here.
        OCRer retry_stage_probe(ctrler()->get_image(), Rect(400, 0, 520, 90));
        const auto stage_text = retry_stage_probe.analyze();
        bool advanced = false;
        if (stage_text) {
            for (const auto& text : *stage_text) {
                const auto value = compact(text.text);
                if ((value.find("2/3") != std::string::npos || value.find("3/3") != std::string::npos ||
                     value.find("选择策略") != std::string::npos || value.find("确定自选编队") != std::string::npos) &&
                    text.score >= 0.8) {
                    advanced = true;
                    break;
                }
            }
        }
        if (advanced) {
            if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
                process->override_next("MiniGame@SPA@SetupPacts", { "MiniGame@SPA@SelectStrategy" });
            }
            auto info = basic_info_with_what("SPARunSetupVerified");
            info["details"] =
                json::object { { "core_pact", "" },
                               { "requested_core_pact", m_core_pact },
                               { "core_selection_mode", m_auto_core_probe ? "provisional_auto" : "explicit" },
                               { "provisional_core", true },
                               { "support_pacts", json::array {} },
                               { "disabled_pacts", disabled_json() },
                               { "availability_after_selection_verified", availability_after_selection_verified },
                               { "selection_verified", false },
                               { "ready_clicked", false },
                               { "destination_verified", true },
                               { "advanced_before_task", true } };
            callback(AsstMsg::SubTaskExtraInfo, info);
            return true;
        }
        LogError << "SPA requested core pact is not visible:" << m_core_pact;
        return false;
    }
    for (const auto& support_name : m_support_pacts) {
        if (!find_pact(*pacts, support_name)) {
            LogError << "SPA requested support pact is not visible:" << support_name;
            return false;
        }
    }

    // `pacts` is already the latest frame (including any bounded scroll
    // above). Reusing it avoids a second full-screen OCR just before the
    // opening click.
    core = find_pact(*pacts, m_core_pact);
    if (!core || !core->core) {
        return false;
    }
    const Point card_label(core->rect.x + core->rect.width / 2, core->rect.y + core->rect.height / 2 + 2);
    // Selecting a core chooses its entire group. Already-green groups need
    // no input. After one click, observe the actual green state instead of
    // clicking the icon a second time because the page did not advance.
    const auto core_state = pact_icon_available(parsed_image, make_rect<cv::Rect>(core->rect), true);
    if (!core_state) {
        LogWarn << "SPA requested core icon is clipped or unreadable; no selection input issued";
        return false;
    }
    if (!*core_state) {
        if (!ctrler()->click(card_label) || !sleep(180)) {
            return false;
        }
        for (int probe = 0; probe < 2; ++probe) {
            pacts = parse();
            if (pacts) {
                core = find_pact(*pacts, m_core_pact);
                if (core && selected(parsed_image, *core)) {
                    break;
                }
            }
            if (probe == 0 && !sleep(180)) {
                return false;
            }
        }
    }
    if (!core || !selected(parsed_image, *core)) {
        LogWarn << "SPA selected core group is not visually verified";
        return false;
    }
    save_evidence(parsed_image, utils::path("debug/SPA/setup_group_selected"), false);
    std::set<std::string> selected_group;
    for (const auto& pact : *pacts) {
        if (pact.core && selected(parsed_image, pact)) {
            selected_group.insert(pact.name);
        }
    }
    if (!scan_selected_availability(selected_group)) {
        return false;
    }
    core = pacts ? visible_selected_core(*pacts, selected_group) : std::nullopt;
    // Scrolling can clip the originally chosen label or lose its OCR, while
    // another member of the same selected group remains visible. Reconcile
    // against that group's pre-scan identities, never an arbitrary green icon.
    if (!core || !selected(parsed_image, *core)) {
        LogWarn << "SPA selected group was not preserved after availability scan";
        return false;
    }

    // In auto-core mode the card has already been selected from the verified
    // green selected group.  The 1/3 page keeps a stable ready button at
    // the lower right.  The old path gated this click on another OCR pass;
    // when that pass missed the compact header, ProcessTask replayed the
    // whole page up to twenty times.  That made a non-dynamic opening take
    // 20+ seconds and could click the same pact repeatedly.  We already have
    // a verified 1/3 pact parse here, so issue one guarded ready click and
    // poll only the header twice.
    if (m_auto_core_probe) {
        if (ctrler()->click(Point(1100, 657)) && sleep(300)) {
            for (int probe = 0; probe < 2; ++probe) {
                OCRer after_ready_probe(ctrler()->get_image(), Rect(400, 0, 520, 90));
                if (const auto after_ready = after_ready_probe.analyze()) {
                    const bool advanced = std::any_of(after_ready->begin(), after_ready->end(), [](const auto& item) {
                        const auto value = compact(item.text);
                        return item.score >= 0.8 &&
                               (value.find("2/3") != std::string::npos || value.find("选择策略") != std::string::npos);
                    });
                    if (advanced) {
                        if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
                            process->override_next("MiniGame@SPA@SetupPacts", { "MiniGame@SPA@SelectStrategy" });
                        }
                        auto info = basic_info_with_what("SPARunSetupVerified");
                        info["details"] = json::object { { "core_pact", m_core_pact },
                                                         { "core_selection_mode", "provisional_auto" },
                                                         { "provisional_core", true },
                                                         { "support_pacts", json::array {} },
                                                         { "disabled_pacts", disabled_json() },
                                                         { "availability_after_selection_verified",
                                                           availability_after_selection_verified },
                                                         { "selection_verified", true },
                                                         { "ready_clicked", true },
                                                         { "destination_verified", true },
                                                         { "stage", "2/3" },
                                                         { "advanced_after_fast_ready_click", true } };
                        callback(AsstMsg::SubTaskExtraInfo, info);
                        return true;
                    }
                }
                if (probe == 0 && !sleep(120)) {
                    return false;
                }
            }
        }

        // Do not return false here: the parent ProcessTask would replay the
        // expensive full-page setup recognition.  Leave an explicit
        // recoverable checkpoint for the observer/runner instead.
        if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
            process->override_next("MiniGame@SPA@SetupPacts", { "MiniGame@SPA@Observe" });
        }
        auto blocked = basic_info_with_what("SPARunSetupBlocked");
        blocked["details"] =
            json::object { { "reason", "Opening pact selection did not advance after one guarded ready click" },
                           { "core_pact", m_core_pact },
                           { "reopen_required", true },
                           { "ready_clicked", true },
                           { "battle_authorized", false },
                           { "run_victory_verified", false } };
        callback(AsstMsg::SubTaskExtraInfo, blocked);
        return true;
    }

    // Some client versions keep the 1/3 selection page visible after the
    // core card is reconciled.  The bottom-right button is the authoritative
    // transition in that layout.  Click it only when OCR still proves both
    // the 1/3 page and the exact ready label; do not guess a coordinate on a
    // later strategy/formation page.
    {
        const auto ready_image = ctrler()->get_image();
        OCRer ready_ocr(ready_image);
        std::optional<Rect> ready;
        bool stage_one = false;
        if (const auto ready_text = ready_ocr.analyze()) {
            std::vector<TextObservation> page_texts;
            for (const auto& text : *ready_text) {
                const auto value = compact(text.text);
                page_texts.push_back({ text.text, text.score, text.rect.x, text.rect.y });
                if (value == "准备就绪" && text.score >= 0.86 && text.rect.x > 850 && text.rect.y > 560) {
                    if (ready) {
                        ready.reset();
                        break;
                    }
                    ready = text.rect;
                }
            }
            stage_one = verified_pact_setup_page(page_texts);
        }
        if (stage_one && ready && ctrler()->click(*ready) && sleep(300)) {
            for (int probe = 0; probe < 5; ++probe) {
                // Only the top stage header changes during this transition.
                // Polling the full map sixteen times cost 15--20 seconds on
                // MuMu and added no information.
                OCRer after_ready_ocr(ctrler()->get_image(), Rect(400, 0, 520, 90));
                if (const auto after_ready = after_ready_ocr.analyze()) {
                    for (const auto& text : *after_ready) {
                        const auto value = compact(text.text);
                        if (text.score >= 0.8 &&
                            (value.find("2/3") != std::string::npos || value.find("选择策略") != std::string::npos)) {
                            if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
                                process->override_next("MiniGame@SPA@SetupPacts", { "MiniGame@SPA@SelectStrategy" });
                            }
                            auto info = basic_info_with_what("SPARunSetupVerified");
                            info["details"] = json::object {
                                { "core_pact", m_core_pact },
                                { "core_selection_mode", m_auto_core_probe ? "provisional_auto" : "explicit" },
                                { "provisional_core", m_auto_core_probe },
                                { "support_pacts", json::array {} },
                                { "disabled_pacts", disabled_json() },
                                { "availability_after_selection_verified", availability_after_selection_verified },
                                { "selection_verified", true },
                                { "ready_clicked", true },
                                { "destination_verified", true },
                                { "stage", "2/3" },
                                { "advanced_after_ready_click", true }
                            };
                            callback(AsstMsg::SubTaskExtraInfo, info);
                            return true;
                        }
                    }
                }
                if (!sleep(180)) {
                    return false;
                }
            }
        }
    }
    LogError << "SPA core pact click did not reach verified strategy-selection stage";
    return false;
}

bool asst::spa::SetupTaskPlugin::on_run_fails()
{
    if (auto* process = dynamic_cast<ProcessTask*>(m_task_ptr)) {
        process->override_next(
            "MiniGame@SPA@SetupPacts",
            m_recovery_task.empty() ? std::vector<std::string> {} : std::vector<std::string> { m_recovery_task });
    }
    return false;
}
