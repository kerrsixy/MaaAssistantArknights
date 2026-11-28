#pragma once

#include "BlackFlowScrapPurchase.h"
#include "BlackFlowScrapRefresh.h"

#include <deque>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "BlackFlowScrapTrade.h"
#include "BlackFlowScrapTradeInventory.h"
#include "BlackFlowTaskPluginBase.h"
#include "Common/AsstTypes.h"

namespace asst::blackflow
{
// 秘境行商交易：持有板藤或多生苔藓时买入后卖回让它们增值；兑现店最后卖出全部可售自然物。
class BlackFlowScrapTradeTaskPlugin final : public BlackFlowTaskPluginBase, private BlackFlowScrapTradeInventoryContext
{
public:
    using BlackFlowTaskPluginBase::BlackFlowTaskPluginBase;

    virtual bool verify(AsstMsg msg, const json::value& details) const override;
    virtual void reset_in_run_variables() override;

protected:
    virtual bool _run() override;

private:
    enum class PendingWork
    {
        None,
        Enter,
        Decide,
        ClickAtMost,
        PurchaseInputObserved,
        RefreshInputObserved,
    };

    enum class Phase
    {
        Cultivate,
        Observe,
        Prepare,
        Trade,
        Liquidate,
        Leave,
    };

    // 店内可花的源石锭：钱包扣除策略预留后的部分。
    struct Funds
    {
        int wallet = 0;
        int reserved = 0;
        int budget = 0;
    };

    void enter();
    void decide();
    void apply_purchase(const ScrapPurchaseReceipt& receipt);
    [[nodiscard]] bool reconcile_purchase(const cv::Mat& image);
    [[nodiscard]] bool reconcile_refresh(const cv::Mat& image);
    bool issue_purchase(const TextRect& offer, bool keep, const Funds& funds, int maximum_price);

    // 当前页面与所需页面不同时切换，返回是否已安排切换动作。
    [[nodiscard]] bool switch_tab(bool want_selling, bool selling);
    [[nodiscard]] bool sell_matching(bool liquidating);
    [[nodiscard]] bool sell_back();
    // skipped 收集买表中在货架上却没有买的商品及原因。
    [[nodiscard]] bool buy(const cv::Mat& image, const Funds& funds, std::vector<std::string>& skipped);
    bool complete_sale(const ScrapTradeInventoryTarget& target, bool sell_back);
    void click_at_most();
    [[nodiscard]] bool refresh(const cv::Mat& image, const Funds& funds);
    void leave();
    // 交易无法继续时上报原因再离店；用户停止任务时只离店。
    void abort_trade(std::string_view reason, const std::string& item = {});

    [[nodiscard]] bool on_buying_page(const cv::Mat& image) const;
    [[nodiscard]] bool sold_at(const cv::Mat& image, const Rect& title) const;
    [[nodiscard]] std::optional<ScrapRefreshShelf> observe_refresh_shelf(const cv::Mat& image) const;
    [[nodiscard]] std::optional<std::pair<int, int>> observe_capacity(const cv::Mat& image) const;
    [[nodiscard]] bool on_selling_page(const cv::Mat& image) const;
    [[nodiscard]] std::vector<TextRect> recognize(const cv::Mat& image, const std::vector<std::string>& names) const;
    [[nodiscard]] bool already_purchased(const Rect& rect) const;
    void set_action(std::string_view task) const;

    [[nodiscard]] cv::Mat capture() const override;
    bool interrupted() const override;
    bool precise_swipe_supported() const override;
    bool swipe_by(std::string_view task, std::optional<int> distance, std::string* error) override;
    bool click(const Rect& rect) override;
    bool execute(std::string_view task, std::string* error) override;
    std::string last_task() const override;
    void wait(unsigned milliseconds) const override;
    void on_inventory_item(const ScrapTradeInventoryItem& item) override;

    BlackFlowScrapTradeInventory m_inventory;
    std::vector<std::string> m_inventory_names;
    mutable PendingWork m_pending = PendingWork::None;
    Phase m_phase = Phase::Cultivate;
    ScrapLedger m_ledger;
    std::unordered_set<std::string> m_sell_table;
    bool m_liquidating = false;
    std::deque<std::string> m_sell_backs;
    std::string m_inventory_last_task;
    ScrapPurchaseTransaction m_purchase;
    ScrapRefreshTransaction m_refresh;
    std::uint64_t m_frame_sequence = 0;
    std::optional<int> m_inventory_total;
    bool m_inventory_complete = false;
    std::string m_refresh_search_target;
    std::vector<Rect> m_purchased_rects;
    std::unordered_set<std::string> m_kept;
    std::optional<std::string> m_shop_type;
    int m_refresh_count = 0;
    int m_toggle_attempts = 0;
};
} // namespace asst::blackflow
