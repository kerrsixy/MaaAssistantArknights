#include "BlackFlowScrapTradeTaskPlugin.h"
#include "BlackFlowMovementCapacity.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string_view>

#include "Config/Roguelike/RoguelikeShoppingConfig.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "Task/ProcessTask.h"
#include "Utils/Logger.hpp"
#include "Vision/Matcher.h"
#include "Vision/OCRer.h"
#include "Vision/Roguelike/RoguelikeParameterAnalyzer.h"

namespace asst::blackflow
{
constexpr std::string_view ScrapTradeEnterTask = "BlackFlow@Roguelike@ScrapTradeEnter";
constexpr std::string_view ScrapTradeDecisionTask = "BlackFlow@Roguelike@ScrapTradeDecision";
constexpr std::string_view ScrapTradeContinueTask = "BlackFlow@Roguelike@ScrapTradeContinue";
constexpr std::string_view ScrapTradeAction = "BlackFlow@Roguelike@ScrapTradeAction";
constexpr std::string_view ScrapTradeToggleTask = "BlackFlow@Roguelike@ScrapTradeToggle";
constexpr std::string_view ScrapTradeBuyConfirmEntry = "BlackFlow@Roguelike@ScrapTradeBuyConfirm-Enter";
constexpr std::string_view ScrapTradeBuyDialogTask = "BlackFlow@Roguelike@ScrapTradeBuyDialog";
constexpr std::string_view ScrapTradeReceiptWaitTask = "BlackFlow@Roguelike@ScrapTradeReceiptWait";
constexpr std::string_view ScrapTradeBuyingFlagTask = "BlackFlow@Roguelike@ScrapTradeBuyingFlag";
constexpr std::string_view ScrapTradeSoldBadgeTask = "BlackFlow@Roguelike@ScrapTradeSoldBadge";
constexpr std::string_view ScrapTradeBuyConfirmTask = "BlackFlow@Roguelike@ScrapTradeBuyConfirm";
constexpr std::string_view ScrapTradeRefreshEntry = "BlackFlow@Roguelike@ScrapTradeRefresh-Enter";
constexpr std::string_view ScrapTradeRefreshConfirmTask = "BlackFlow@Roguelike@ScrapTradeRefreshConfirm";
constexpr std::string_view ScrapTradeRefreshWaitTask = "BlackFlow@Roguelike@ScrapTradeRefreshReceiptWait";
constexpr std::string_view ScrapTradeCapacityTask = "BlackFlow@Roguelike@CurrentMovementCapacity";
constexpr std::string_view ScrapTradeLeaveEntry = "BlackFlow@Roguelike@ScrapTradeLeave-Enter";
constexpr std::string_view ScrapTradeCultivateEntry = "BlackFlow@Roguelike@ScrapTradeCultivate-Enter";
constexpr std::string_view ScrapTradeCultivateAtMostTask = "BlackFlow@Roguelike@ScrapTradeCultivateAtMost";
constexpr std::string_view ScrapTradeHeldSeedCountTask = "BlackFlow@Roguelike@CultivateHeldSeedCount";
constexpr std::string_view ScrapTradeItemsTask = "BlackFlow@Roguelike@ScrapTradeItems";
constexpr std::string_view ScrapTradeSellingFlagTask = "BlackFlow@Roguelike@CultivateSellingFlag";
constexpr std::string_view ScrapTradeWalletTask = "BlackFlow@Roguelike@CurrentIngots";

constexpr int ScrapTradeMaxToggleAttempts = 3;
constexpr int ScrapTradeSameSlotTolerance = 20;
constexpr int ScrapTradeAtMostClickTimes = 3;
constexpr int ScrapTradeRepeatedClickInterval = 100;

bool BlackFlowScrapTradeTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    if (details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }
    const std::string task = details.get("details", "task", "");
    if (msg == AsstMsg::SubTaskStart) {
        if (task == ScrapTradeEnterTask) {
            m_pending = PendingWork::Enter;
            return true;
        }
        if (task == ScrapTradeDecisionTask) {
            m_pending = PendingWork::Decide;
            return true;
        }
        if (task == ScrapTradeCultivateAtMostTask) {
            m_pending = PendingWork::ClickAtMost;
            return true;
        }
    }
    if (msg == AsstMsg::SubTaskCompleted) {
        if (task == ScrapTradeBuyConfirmTask || task == ScrapTradeBuyDialogTask) {
            m_pending = PendingWork::PurchaseInputObserved;
            return true;
        }
        if (task == ScrapTradeRefreshConfirmTask) {
            m_pending = PendingWork::RefreshInputObserved;
            return true;
        }
    }
    return false;
}

void BlackFlowScrapTradeTaskPlugin::reset_in_run_variables()
{
    m_pending = PendingWork::None;
    enter();
}

bool BlackFlowScrapTradeTaskPlugin::_run()
{
    LogTraceFunction;
    const PendingWork work = m_pending;
    m_pending = PendingWork::None;

    switch (work) {
    case PendingWork::Enter:
        enter();
        break;
    case PendingWork::Decide:
        decide();
        break;
    case PendingWork::ClickAtMost:
        click_at_most();
        break;
    case PendingWork::PurchaseInputObserved:
        m_purchase.note_confirmation_input();
        break;
    case PendingWork::RefreshInputObserved:
        if (m_refresh.intent()) {
            m_refresh.note_confirmation_input(m_refresh.intent()->frame_sequence);
        }
        break;
    case PendingWork::None:
        break;
    }
    return true;
}

void BlackFlowScrapTradeTaskPlugin::enter()
{
    m_phase = Phase::Cultivate;
    m_ledger = {};
    m_inventory_names.clear();
    for (const auto& item : BlackFlowScrapMarket.items()) {
        m_inventory_names.emplace_back(item.name);
    }
    m_sell_table.clear();
    m_sell_backs.clear();
    m_inventory.invalidate();
    m_inventory_last_task.clear();
    m_purchase = {};
    m_refresh = {};
    m_frame_sequence = 0;
    m_inventory_total.reset();
    m_inventory_complete = false;
    m_refresh_search_target.clear();
    m_purchased_rects.clear();
    m_kept.clear();
    m_shop_type.reset();
    m_refresh_count = 0;
    m_toggle_attempts = 0;
    m_liquidating = false;

    const auto& table = m_config->status().shopping_sell_table;
    if (!table.empty()) {
        const auto& names = RoguelikeShopping.get_sell_goods(m_config->get_theme(), table);
        m_sell_table.insert(names.begin(), names.end());
    }
    // 卖表含有增长物，表示这家店是兑现店：交易后卖出全部可售自然物，并按期望刷新。
    m_liquidating = std::ranges::any_of(m_sell_table, [](const std::string& name) {
        const auto item = BlackFlowScrapMarket.find(name);
        return item && item->get().growth_per_acquisition > 0;
    });
}

void BlackFlowScrapTradeTaskPlugin::decide()
{
    if (!BlackFlowScrapMarket.available()) {
        LogWarn << __FUNCTION__ << "BlackFlow scrap market is unavailable";
        abort_trade("market_unavailable");
        return;
    }

    const cv::Mat image = ctrler()->get_image();
    if (m_frame_sequence == std::numeric_limits<std::uint64_t>::max()) {
        abort_trade("capture_sequence_exhausted");
        return;
    }
    ++m_frame_sequence;
    if (reconcile_purchase(image)) {
        return;
    }
    if (reconcile_refresh(image)) {
        return;
    }
    const bool selling = on_selling_page(image);
    while (true) {
        switch (m_phase) {
        case Phase::Cultivate: {
            // 培育出的零件可能是板藤，先培育再计数。培育前后都停在买入页，只进行一次。
            if (switch_tab(false, selling)) {
                return;
            }
            m_phase = Phase::Observe;
            RoguelikeParameterAnalyzer analyzer(image);
            const int seeds = analyzer.get_number(image, std::string(ScrapTradeHeldSeedCountTask));
            if (seeds > 0) {
                LogInfo << __FUNCTION__ << "BlackFlow scrap trade cultivates seeds" << seeds;
                set_action(ScrapTradeCultivateEntry);
                return;
            }
            continue;
        }
        case Phase::Observe: {
            if (switch_tab(true, selling)) {
                return;
            }
            std::string error;
            const auto survey = m_inventory.survey(*this, m_inventory_names, &error);
            if (!survey) {
                LogWarn << "BlackFlow scrap trade inventory survey failed" << error;
                abort_trade("inventory_incomplete");
                return;
            }
            m_ledger.reset(survey->items, survey->exact_counts);
            m_inventory_complete = survey->exact_counts && survey->unknown_slots == 0 && survey->items.size() <= 64;
            m_inventory_total =
                m_inventory_complete ? std::optional<int>(static_cast<int>(survey->items.size())) : std::nullopt;
            LogInfo << __FUNCTION__ << "BlackFlow scrap trade survey" << "items" << survey->items << "exact counts"
                    << survey->exact_counts << "unknown" << survey->unknown_slots << "processing"
                    << m_ledger.held_in(ScrapCategory::Processing, BlackFlowScrapMarket) << "growth"
                    << m_ledger.growth(BlackFlowScrapMarket);
            m_phase = Phase::Prepare;
            continue;
        }
        case Phase::Prepare:
            // 交易前先卖出卖表内不随零件增长的自然物换钱。
            if (switch_tab(true, selling) || sell_matching(false)) {
                return;
            }
            m_phase = Phase::Trade;
            continue;
        case Phase::Trade:
            if (!m_sell_backs.empty()) {
                if (switch_tab(true, selling) || sell_back()) {
                    return;
                }
                continue;
            }
            if (switch_tab(false, selling)) {
                return;
            }
            {
                // 策略预留的余额不参与店内消费；卖出到账后下一轮按新余额重算，未卖回的估价不计入。
                RoguelikeParameterAnalyzer analyzer(image);
                const auto observed_wallet =
                    RoguelikeParameterAnalyzer::read_number(image, std::string(ScrapTradeWalletTask));
                if (!observed_wallet || *observed_wallet < 0 || !on_buying_page(image)) {
                    abort_trade("purchase_page_or_wallet_unknown");
                    return;
                }
                const int wallet = *observed_wallet;
                const int reserved = m_session->reserved_amount("ingots");
                if (reserved < 0) {
                    abort_trade("reserved_budget_invalid");
                    return;
                }
                const Funds funds { wallet, reserved, std::max(0, wallet - reserved) };
                std::vector<std::string> skipped;
                if (buy(image, funds, skipped) || refresh(image, funds)) {
                    return;
                }
                LogInfo << __FUNCTION__ << "BlackFlow scrap trade buying finished"
                        << "budget" << funds.budget << "wallet" << funds.wallet << "reserved" << funds.reserved
                        << "skipped" << skipped;
            }
            m_phase = Phase::Liquidate;
            continue;
        case Phase::Liquidate:
            if (!m_liquidating) {
                m_phase = Phase::Leave;
                continue;
            }
            if (switch_tab(true, selling) || sell_matching(true)) {
                return;
            }
            m_phase = Phase::Leave;
            continue;
        case Phase::Leave:
            leave();
            return;
        }
    }
}

bool BlackFlowScrapTradeTaskPlugin::switch_tab(bool want_selling, bool selling)
{
    if (want_selling == selling) {
        m_toggle_attempts = 0;
        return false;
    }
    if (++m_toggle_attempts > ScrapTradeMaxToggleAttempts) {
        LogWarn << __FUNCTION__ << "BlackFlow scrap trade cannot switch page"
                << "want selling" << want_selling;
        abort_trade("page_switch_failed");
        return true;
    }
    m_inventory.forget_position();
    set_action(ScrapTradeToggleTask);
    return true;
}

// 预卖只卖不随零件获得增长的自然物，兑现时再卖增长物，交易期间保留它们。
bool BlackFlowScrapTradeTaskPlugin::sell_matching(bool liquidating)
{
    std::vector<std::string> names;
    for (const auto& name : m_sell_table) {
        const auto item = BlackFlowScrapMarket.find(name);
        if (item && item->get().category == ScrapCategory::Natural &&
            (liquidating || item->get().growth_per_acquisition == 0) && m_ledger.may_sell(item->get(), m_sell_table)) {
            names.emplace_back(name);
        }
    }
    std::string error;
    const auto item = m_inventory.find(*this, m_inventory_names, names, &error);
    if (!item) {
        if (!error.empty()) {
            LogWarn << "BlackFlow scrap trade sale search failed" << error;
            abort_trade("sale_search_failed");
            return true;
        }
        return false;
    }
    return complete_sale(*item, false);
}

bool BlackFlowScrapTradeTaskPlugin::sell_back()
{
    const std::string name = m_sell_backs.front();
    const auto item = BlackFlowScrapMarket.find(name);
    std::string error;
    const auto target = item && m_ledger.may_sell(item->get(), m_sell_table)
                            ? m_inventory.find(*this, m_inventory_names, { name }, &error)
                            : std::optional<ScrapTradeInventoryTarget> {};
    if (!target) {
        if (!error.empty()) {
            LogWarn << "BlackFlow scrap trade sell-back search failed" << name << error;
            abort_trade("sell_back_search_failed", name);
            return true;
        }
        // 已完整查找仍找不到时，沿用放弃卖回这一件的处理。
        LogWarn << __FUNCTION__ << "BlackFlow scrap trade cannot sell back" << name;
        m_sell_backs.pop_front();
        return false;
    }
    return complete_sale(*target, true);
}

bool BlackFlowScrapTradeTaskPlugin::buy(const cv::Mat& image, const Funds& funds, std::vector<std::string>& skipped)
{
    std::vector<std::string> names;
    for (const auto& item : BlackFlowScrapMarket.items()) {
        if (item.price) {
            names.emplace_back(item.name);
        }
    }
    std::vector<TextRect> shelf = recognize(image, names);
    std::erase_if(shelf, [this, &image](const TextRect& offer) {
        return already_purchased(offer.rect) || sold_at(image, offer.rect);
    });

    if (!m_shop_type && m_purchased_rects.empty()) {
        std::vector<std::string> shelf_names;
        // A partial OCR subset is insufficient evidence of the current pool.
        if (const auto complete = observe_refresh_shelf(image)) {
            for (const auto& slot : complete->slots) {
                shelf_names.emplace_back(slot.canonical_name);
            }
            m_shop_type = infer_scrap_shop_type(shelf_names, BlackFlowScrapMarket);
            if (m_shop_type) {
                const auto& types = BlackFlowScrapMarket.shop_types();
                const auto type = std::ranges::find(types, *m_shop_type, &ScrapShopType::id);
                if (type == types.end() || !std::ranges::all_of(shelf_names, [&](const auto& name) {
                        const auto rate = type->offer_rates.find(name);
                        return rate != type->offer_rates.end() && std::isfinite(rate->second) && rate->second > 0;
                    })) {
                    m_shop_type.reset();
                }
            }
        }
        LogInfo << __FUNCTION__ << "BlackFlow scrap shop type" << m_shop_type.value_or("unknown") << "shelf"
                << shelf_names;
    }

    // 价格按名称查表，不识别货架价格；已知的价格效果只会降价，因此表价不会让余额不足。
    // 保留购买先于倒转；非兑现店按买表积累玉米和雾滚草，加工品每种每次进店最多补一件。
    const auto skip_reason = [&](const ScrapItem& item, bool keep_natural) -> std::string_view {
        if (*item.price > funds.budget) {
            return "over budget";
        }
        if (keep_natural) {
            return {};
        }
        if (m_kept.contains(item.name)) {
            return "bought this visit";
        }
        // 买表里的概念体与自然物也要能买：概念体只做一次性激活，自然物用来滚估价，各自给一个
        // 小上限，避免把零件箱塞满。
        if (item.category == ScrapCategory::Concept) {
            return m_ledger.held_in(ScrapCategory::Concept, BlackFlowScrapMarket) >= 3 ? "concept limit reached"
                                                                                       : std::string_view {};
        }
        if (item.category == ScrapCategory::Natural) {
            return m_ledger.held_in(ScrapCategory::Natural, BlackFlowScrapMarket) >= 4 ? "natural limit reached"
                                                                                       : std::string_view {};
        }
        if (scrap_keep_wanted(item, m_ledger, BlackFlowScrapMarket)) {
            return {};
        }
        if (item.category != ScrapCategory::Processing) {
            return "not kept here";
        }
        return m_ledger.held_in(ScrapCategory::Processing, BlackFlowScrapMarket) >=
                       BlackFlowScrapMarket.processing_keep_limit()
                   ? "keep limit reached"
                   : "shapes covered";
    };
    if (!m_refresh_search_target.empty()) {
        const auto offer = std::ranges::find(shelf, m_refresh_search_target, &TextRect::text);
        const auto item = BlackFlowScrapMarket.find(m_refresh_search_target);
        if (offer != shelf.end() && item && item->get().price && skip_reason(item->get(), false).empty()) {
            const auto capacity = observe_capacity(image);
            if (!m_inventory_complete || !capacity ||
                !scrap_refresh_capacity_can_fit(
                    capacity->first,
                    capacity->second,
                    m_inventory_total,
                    scrap_acquisitions(item->get()))) {
                abort_trade("refresh_target_capacity_unknown", m_refresh_search_target);
                return true;
            }
            return issue_purchase(*offer, true, funds, *item->get().price);
        }
        // The paid search can miss its target. It never authorizes another roll.
        m_refresh_search_target.clear();
    }
    const auto& table = m_config->status().shopping_buy_table;
    for (const auto& goods : RoguelikeShopping.get_goods(m_config->get_theme(), table)) {
        const auto offer = std::ranges::find(shelf, goods.name, &TextRect::text);
        const auto item = BlackFlowScrapMarket.find(goods.name);
        if (offer == shelf.end() || !item || !item->get().price) {
            continue;
        }
        const bool keep_natural = !m_liquidating && (goods.name == "回声玉米" || goods.name == "雾滚草");
        if (const auto reason = skip_reason(item->get(), keep_natural); !reason.empty()) {
            skipped.emplace_back(goods.name + " " + std::string(reason));
            continue;
        }
        LogInfo << __FUNCTION__ << "BlackFlow scrap trade buys to keep" << goods.name << "price" << *item->get().price
                << "budget" << funds.budget << "wallet" << funds.wallet << "reserved" << funds.reserved;
        return issue_purchase(*offer, true, funds, *item->get().price);
    }

    const int growth = m_ledger.growth(BlackFlowScrapMarket);
    const TextRect* best = nullptr;
    int best_net = 0;
    for (const auto& offer : shelf) {
        const auto item = BlackFlowScrapMarket.find(offer.text);
        if (!item || !item->get().price || *item->get().price > funds.budget) {
            continue;
        }
        // 加工品必须先通过形状覆盖/容量策略；否则这里的净收益兜底会绕过
        // scrap_keep_wanted，把已经覆盖的移动能力再次买进来并挤满零件箱。
        if (item->get().category == ScrapCategory::Processing &&
            !scrap_keep_wanted(item->get(), m_ledger, BlackFlowScrapMarket)) {
            continue;
        }
        const auto net = scrap_trade_net(item->get(), growth, BlackFlowScrapMarket);
        if (net && *net > best_net) {
            best = &offer;
            best_net = *net;
        }
    }
    if (best == nullptr) {
        return false;
    }
    LogInfo << __FUNCTION__ << "BlackFlow scrap trade buys" << best->text << "net" << best_net << "growth" << growth
            << "budget" << funds.budget << "wallet" << funds.wallet << "reserved" << funds.reserved;
    return issue_purchase(*best, false, funds, *BlackFlowScrapMarket.find(best->text)->get().price);
}

bool BlackFlowScrapTradeTaskPlugin::refresh(const cv::Mat& image, const Funds& funds)
{
    const auto& costs = BlackFlowScrapMarket.refresh_costs();
    if (!m_shop_type || m_refresh_count < 0 || m_refresh_count >= static_cast<int>(costs.size())) {
        return false;
    }
    const auto& types = BlackFlowScrapMarket.shop_types();
    const auto type = std::ranges::find(types, *m_shop_type, &ScrapShopType::id);
    if (type == types.end()) {
        return false;
    }
    const int cost = costs[static_cast<std::size_t>(m_refresh_count)];
    std::string reason;
    std::string target_name;
    bool worth = false;
    if (m_liquidating) {
        // Preserve the liquidation policy; only its execution receipt changes.
        const int growth = m_ledger.growth(BlackFlowScrapMarket);
        const double expected = expected_scrap_page_profit(*type, growth, BlackFlowScrapMarket);
        const auto cheapest = cheapest_profitable_price(*type, growth, BlackFlowScrapMarket);
        worth = cheapest && cost > 0 && expected > cost &&
                static_cast<std::int64_t>(funds.budget) >= static_cast<std::int64_t>(cost) + *cheapest;
        reason = "liquidation_expected_profit";
    }
    else {
        const auto capacity = observe_capacity(image);
        const bool inventory_verified =
            m_inventory_complete && m_inventory_total && capacity && capacity->first == *m_inventory_total;
        ScrapMovementRefreshContext context;
        context.wallet = funds.wallet;
        context.reserved = funds.reserved;
        context.maximum_cost = cost;
        context.refresh_index = m_refresh_count;
        context.inventory_complete = inventory_verified;
        context.capacity_known_and_available = capacity && capacity->first < capacity->second;
        context.shop_pool_known = true;
        std::vector<ScrapMovementRefreshTarget> targets;
        const auto& table = m_config->status().shopping_buy_table;
        for (const auto& goods : RoguelikeShopping.get_goods(m_config->get_theme(), table)) {
            const auto item = BlackFlowScrapMarket.find(goods.name);
            if (!item || item->get().category != ScrapCategory::Processing || item->get().covers.empty() ||
                !item->get().price) {
                continue;
            }
            const auto pool = type->offer_rates.find(goods.name);
            const bool in_pool = pool != type->offer_rates.end() && std::isfinite(pool->second) && pool->second > 0;
            const bool fits = inventory_verified && scrap_refresh_capacity_can_fit(
                                                        capacity->first,
                                                        capacity->second,
                                                        m_inventory_total,
                                                        scrap_acquisitions(item->get()));
            targets.push_back(
                { goods.name,
                  item->get().price,
                  true,
                  scrap_keep_wanted(item->get(), m_ledger, BlackFlowScrapMarket),
                  m_kept.contains(goods.name),
                  in_pool,
                  fits });
        }
        const auto decision = decide_scrap_movement_refresh(context, targets);
        worth = decision.allow;
        reason = decision.reason;
        target_name = decision.canonical_name;
    }
    LogInfo << __FUNCTION__ << "BlackFlow scrap trade refresh" << "worth" << worth << "reason" << reason << "target"
            << target_name << "cost" << cost << "budget" << funds.budget << "wallet" << funds.wallet << "reserved"
            << funds.reserved;
    if (!worth) {
        return false;
    }
    const auto before = observe_refresh_shelf(image);
    if (!before || !m_refresh.begin({ funds.wallet, cost, m_refresh_count, m_frame_sequence, *before })) {
        abort_trade("refresh_intent_or_complete_shelf_unknown");
        return true;
    }
    m_refresh_search_target = std::move(target_name);
    auto info = basic_info_with_what("BlackFlowScrapRefreshPlanned");
    info["details"] = json::object { { "reason", reason },       { "target", m_refresh_search_target },
                                     { "maximum_cost", cost },   { "refresh_index", m_refresh_count },
                                     { "wallet", funds.wallet }, { "reserved", funds.reserved } };
    callback(AsstMsg::SubTaskExtraInfo, info);
    set_action(ScrapTradeRefreshEntry);
    return true;
}

// 正常结束和页面异常都直接离店，沿用路由的普通完成；第五层目标按到访计数，异常离店不会挡住后面的商店。
void BlackFlowScrapTradeTaskPlugin::leave()
{
    m_phase = Phase::Leave;
    set_action(ScrapTradeLeaveEntry);
}

void BlackFlowScrapTradeTaskPlugin::abort_trade(std::string_view reason, const std::string& item)
{
    if (!need_exit()) {
        auto info = basic_info_with_what("BlackFlowScrapTradeAborted");
        info["details"] = json::object {
            { "reason", std::string(reason) },
            { "item", item },
        };
        callback(AsstMsg::SubTaskExtraInfo, info);
    }
    leave();
}

bool BlackFlowScrapTradeTaskPlugin::issue_purchase(
    const TextRect& offer,
    bool keep,
    const Funds& funds,
    int maximum_price)
{
    if (!m_purchase.begin({ offer.text, offer.rect, keep, funds.wallet, maximum_price })) {
        abort_trade("purchase_already_pending_or_invalid", offer.text);
        return true;
    }
    if (!ctrler()->click(offer.rect)) {
        abort_trade("purchase_selection_failed", offer.text);
        return true;
    }
    set_action(ScrapTradeBuyConfirmEntry);
    return true;
}

bool BlackFlowScrapTradeTaskPlugin::reconcile_purchase(const cv::Mat& image)
{
    if (!m_purchase.intent()) {
        return false;
    }
    const auto& intent = *m_purchase.intent();
    const bool buying = on_buying_page(image);
    OCRer dialog(image);
    dialog.set_task_info(std::string(ScrapTradeBuyDialogTask));
    const bool confirmation_visible = dialog.analyze().has_value();
    const bool sold = buying && !confirmation_visible && sold_at(image, intent.rect);
    const auto wallet = buying && !confirmation_visible
                            ? RoguelikeParameterAnalyzer::read_number(image, std::string(ScrapTradeWalletTask))
                            : std::nullopt;
    const auto state =
        m_purchase.observe({ buying, confirmation_visible, sold ? intent.name : std::string {}, wallet });
    if (state == ScrapPurchaseState::Confirmed) {
        const auto receipt = m_purchase.take_confirmed();
        apply_purchase(*receipt);
        return false;
    }
    if (state == ScrapPurchaseState::Failed) {
        abort_trade("purchase_receipt_unconfirmed", intent.name);
    }
    else {
        set_action(ScrapTradeReceiptWaitTask);
    }
    return true;
}

bool BlackFlowScrapTradeTaskPlugin::on_buying_page(const cv::Mat& image) const
{
    Matcher matcher(image);
    matcher.set_task_info(std::string(ScrapTradeBuyingFlagTask));
    return matcher.analyze().has_value();
}

std::optional<std::pair<int, int>> BlackFlowScrapTradeTaskPlugin::observe_capacity(const cv::Mat& image) const
{
    const auto task = Task.get<OcrTaskInfo>(std::string(ScrapTradeCapacityTask));
    if (!task || image.empty() || image.cols != 1280 || image.rows != 720) {
        return std::nullopt;
    }
    OCRer analyzer(image);
    analyzer.set_task_info(task);
    const auto results = analyzer.analyze();
    if (!results) {
        return std::nullopt;
    }
    std::vector<MovementCapacityTextFragment> fragments;
    for (const auto& text : *results) {
        const auto& rect = text.rect;
        if (!std::isfinite(text.score) || text.score < 0.9 || rect.x < task->roi.x || rect.y < task->roi.y ||
            rect.x + rect.width > task->roi.x + task->roi.width ||
            rect.y + rect.height > task->roi.y + task->roi.height) {
            return std::nullopt;
        }
        fragments.push_back({ rect.x, rect.y, rect.width, rect.height, text.text });
    }
    return movement_capacity_from_fragments(std::move(fragments));
}

std::optional<ScrapRefreshShelf> BlackFlowScrapTradeTaskPlugin::observe_refresh_shelf(const cv::Mat& image) const
{
    if (image.empty() || image.cols != 1280 || image.rows != 720 || !on_buying_page(image)) {
        return std::nullopt;
    }
    const auto names = recognize(image, m_inventory_names);
    if (names.size() != 6) {
        return std::nullopt;
    }
    ScrapRefreshShelf shelf;
    std::array<bool, 6> seen {};
    for (const auto& offer : names) {
        const auto item = BlackFlowScrapMarket.find(offer.text);
        const auto slot =
            scrap_refresh_slot_from_title(offer.rect.x, offer.rect.y, offer.rect.width, offer.rect.height);
        if (!item || !item->get().price || !std::isfinite(offer.score) || offer.score < 0.9 || !slot ||
            seen[static_cast<std::size_t>(*slot)]) {
            return std::nullopt;
        }
        seen[static_cast<std::size_t>(*slot)] = true;
        shelf.slots[static_cast<std::size_t>(*slot)] = { *slot, item->get().name, sold_at(image, offer.rect) };
    }
    shelf.complete = true;
    return shelf;
}

bool BlackFlowScrapTradeTaskPlugin::reconcile_refresh(const cv::Mat& image)
{
    if (!m_refresh.intent()) {
        return false;
    }
    Matcher dialog(image);
    dialog.set_task_info(std::string(ScrapTradeRefreshConfirmTask));
    const bool confirmation_visible = dialog.analyze().has_value();
    const bool buying = on_buying_page(image);
    const auto wallet = buying && !confirmation_visible
                            ? RoguelikeParameterAnalyzer::read_number(image, std::string(ScrapTradeWalletTask))
                            : std::nullopt;
    const auto shelf = buying && !confirmation_visible ? observe_refresh_shelf(image) : std::nullopt;
    const auto state = m_refresh.observe({ m_frame_sequence, buying, confirmation_visible, wallet, shelf });
    if (state == ScrapRefreshState::Confirmed) {
        const auto receipt = m_refresh.take_confirmed();
        ++m_refresh_count;
        // Only shelf generation changes. Keep held inventory and visit limits.
        m_purchased_rects.clear();
        auto info = basic_info_with_what("BlackFlowScrapRefreshConfirmed");
        info["details"] = json::object { { "refresh_index", receipt->intent.refresh_index },
                                         { "paid", receipt->paid },
                                         { "count", m_refresh_count },
                                         { "target", m_refresh_search_target },
                                         { "complete_shelf_changed", true } };
        callback(AsstMsg::SubTaskExtraInfo, info);
        LogInfo << __FUNCTION__ << "BlackFlow scrap trade refresh receipt confirmed" << "count" << m_refresh_count
                << "spent" << receipt->paid;
        return false;
    }
    if (state == ScrapRefreshState::Failed) {
        abort_trade(
            m_refresh.failure() == ScrapRefreshFailure::InvalidDebit ? "refresh_invalid_debit"
                                                                     : "refresh_receipt_unconfirmed");
    }
    else {
        set_action(ScrapTradeRefreshWaitTask);
    }
    return true;
}

bool BlackFlowScrapTradeTaskPlugin::sold_at(const cv::Mat& image, const Rect& title) const
{
    const auto region = scrap_purchase_sold_region(title, image.cols, image.rows);
    if (!region) {
        return false;
    }
    Matcher matcher(image);
    matcher.set_task_info(std::string(ScrapTradeSoldBadgeTask));
    matcher.set_roi(*region);
    return matcher.analyze().has_value();
}

void BlackFlowScrapTradeTaskPlugin::apply_purchase(const ScrapPurchaseReceipt& receipt)
{
    const ScrapPurchaseIntent& purchase = receipt.intent;
    m_purchased_rects.emplace_back(purchase.rect);
    const auto item = BlackFlowScrapMarket.find(purchase.name);
    if (!item) {
        return;
    }
    m_ledger.record_purchase(item->get());
    const int acquired = scrap_acquisitions(item->get());
    if (m_inventory_total && acquired > 0 && acquired <= 64 - *m_inventory_total) {
        *m_inventory_total += acquired;
    }
    else {
        m_inventory_total.reset();
        m_inventory_complete = false;
    }
    // 新获得的物品可能插入不同分类，下一次出售前重建位置，保留进店账本。
    m_inventory.invalidate();
    if (purchase.keep) {
        m_kept.insert(purchase.name);
    }
    else {
        // 增长物留到兑现，其余买入物与附带零件立即卖回，零件总数回到买入前。
        if (item->get().growth_per_acquisition == 0) {
            m_sell_backs.emplace_back(purchase.name);
        }
        for (const auto& [name, count] : item->get().bundle) {
            m_sell_backs.insert(m_sell_backs.end(), static_cast<std::size_t>(count), name);
        }
    }
    LogInfo << __FUNCTION__ << "BlackFlow scrap trade purchase confirmed" << purchase.name << "spent" << receipt.paid
            << "keep" << purchase.keep << "held" << m_ledger.held(purchase.name) << "growth"
            << m_ledger.growth(BlackFlowScrapMarket) << "pending sell backs" << m_sell_backs.size();
}

void BlackFlowScrapTradeTaskPlugin::click_at_most()
{
    const auto hit = get_hit_detail<Matcher::Result>();
    if (hit == nullptr) {
        return;
    }
    for (int times = 0; times < ScrapTradeAtMostClickTimes; ++times) {
        ctrler()->click(hit->rect);
        sleep(ScrapTradeRepeatedClickInterval);
    }
}

bool BlackFlowScrapTradeTaskPlugin::complete_sale(const ScrapTradeInventoryTarget& target, bool sell_back)
{
    std::string error;
    if (!m_inventory.sell(*this, target, &error)) {
        LogWarn << "BlackFlow scrap trade sale failed" << target.text.text << error;
        abort_trade("sale_failed", target.text.text);
        return true;
    }
    m_ledger.record_sale(target.text.text);
    if (m_inventory_total && *m_inventory_total > 0) {
        --*m_inventory_total;
    }
    else {
        m_inventory_total.reset();
        m_inventory_complete = false;
    }
    if (sell_back) {
        m_sell_backs.pop_front();
    }
    LogInfo << "BlackFlow scrap trade sale confirmed" << target.text.text << "remaining"
            << m_ledger.held(target.text.text) << "growth" << m_ledger.growth(BlackFlowScrapMarket);
    set_action(ScrapTradeContinueTask);
    return true;
}

cv::Mat BlackFlowScrapTradeTaskPlugin::capture() const
{
    return ctrler()->get_image();
}

bool BlackFlowScrapTradeTaskPlugin::interrupted() const
{
    return need_exit();
}

bool BlackFlowScrapTradeTaskPlugin::precise_swipe_supported() const
{
    return ControlFeat::support(ctrler()->support_features(), ControlFeat::PRECISE_SWIPE);
}

bool
    BlackFlowScrapTradeTaskPlugin::swipe_by(std::string_view task_name, std::optional<int> distance, std::string* error)
{
    const auto task = Task.get(std::string(task_name));
    bool succeeded = false;
    if (task != nullptr) {
        if (!distance) {
            succeeded = ProcessTask(*this, { std::string(task_name) }).run();
        }
        else {
            const Rect& start = task->specific_rect;
            const Rect end { start.x, start.y + *distance, start.width, start.height };
            const auto& params = task->special_params;
            succeeded = ctrler()->swipe(
                start,
                end,
                params.empty() ? 0 : params.at(0),
                params.size() < 2 ? SwipeExtraDirection::None : to_swipe_extra_direction(params.at(1)),
                params.size() < 3 ? 1 : params.at(2) / 10.0,
                params.size() < 4 ? 1 : params.at(3) / 10.0);
            if (succeeded) {
                sleep(task->post_delay);
            }
        }
    }
    if (!succeeded && error != nullptr) {
        *error = "scrap trade inventory swipe failed: " + std::string(task_name);
    }
    return succeeded;
}

bool BlackFlowScrapTradeTaskPlugin::click(const Rect& rect)
{
    return ctrler()->click(rect);
}

bool BlackFlowScrapTradeTaskPlugin::execute(std::string_view task, std::string* error)
{
    ProcessTask process(*this, { std::string(task) });
    process.set_retry_times(0);
    m_inventory_last_task.clear();
    const bool succeeded = process.run();
    m_inventory_last_task = process.get_last_task_name();
    if (!succeeded && error != nullptr) {
        *error = "scrap trade sale task failed: " + std::string(task);
    }
    return succeeded;
}

std::string BlackFlowScrapTradeTaskPlugin::last_task() const
{
    return m_inventory_last_task;
}

void BlackFlowScrapTradeTaskPlugin::wait(unsigned milliseconds) const
{
    sleep(milliseconds);
}

void BlackFlowScrapTradeTaskPlugin::on_inventory_item(const ScrapTradeInventoryItem& observed)
{
    const auto item = BlackFlowScrapMarket.find(observed.name);
    if (item && item->get().growth_per_acquisition > 0) {
        LogInfo << "BlackFlow scrap trade growth item observed" << observed.name << "row" << observed.row << "column"
                << observed.column << "growth per acquisition" << item->get().growth_per_acquisition;
    }
}

bool BlackFlowScrapTradeTaskPlugin::on_selling_page(const cv::Mat& image) const
{
    Matcher analyzer(image);
    analyzer.set_task_info(std::string(ScrapTradeSellingFlagTask));
    return analyzer.analyze().has_value();
}

std::vector<TextRect>
    BlackFlowScrapTradeTaskPlugin::recognize(const cv::Mat& image, const std::vector<std::string>& names) const
{
    // 空名单交给 OCR 会匹配任意文字。
    if (names.empty()) {
        return {};
    }
    OCRer analyzer(image);
    analyzer.set_task_info(std::string(ScrapTradeItemsTask));
    analyzer.set_required(names);
    const auto results = analyzer.analyze();
    return results.has_value() ? std::move(*results) : std::vector<TextRect> {};
}

bool BlackFlowScrapTradeTaskPlugin::already_purchased(const Rect& rect) const
{
    const int center_x = rect.x + rect.width / 2;
    const int center_y = rect.y + rect.height / 2;
    return std::ranges::any_of(m_purchased_rects, [&](const Rect& bought) {
        return std::abs(center_x - (bought.x + bought.width / 2)) <= ScrapTradeSameSlotTolerance &&
               std::abs(center_y - (bought.y + bought.height / 2)) <= ScrapTradeSameSlotTolerance;
    });
}

void BlackFlowScrapTradeTaskPlugin::set_action(std::string_view task) const
{
    Task.set_task_base(std::string(ScrapTradeAction), std::string(task));
}
} // namespace asst::blackflow
