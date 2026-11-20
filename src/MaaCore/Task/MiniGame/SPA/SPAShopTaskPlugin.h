#pragma once

#include "SPAObservation.h"
#include "Task/AbstractTaskPlugin.h"

namespace asst::spa
{
class ShopTaskPlugin final : public AbstractTaskPlugin
{
public:
    using AbstractTaskPlugin::AbstractTaskPlugin;
    bool configure(std::string name, int price, int funds, int round, int slot = -1);
    bool configure_refresh(int price, int funds, int round);
    bool configure_strategy_refresh(int funds, int round, int shop_level);
    bool configure_equipment_discount(std::string name, int base_price, int funds, int round, int slot);
    bool configure_freeze(int funds, int round);
    bool configure_upgrade(int price, int funds, int round);
    bool configure_supply(std::string name, int slot = -1);
    bool configure_bounty(std::string name, bool confirm = false, int slot = -1);
    bool configure_tactical(std::string name, bool confirm, int slot);

    // Phase-one and dynamic automation can use shorter, bounded settle
    // delays because the caller owns the preparation-page observation and
    // the plugin still verifies the payment/state postcondition.
    void set_fast_mode(bool enabled) { m_fast_mode = enabled; }

    // The preparation observer already verified the card name, slot, price
    // and funds.  Phase one may reuse that authoritative frame and only OCR
    // the small confirmation ROI after the click; dynamic/standalone calls
    // keep the strict full-page identity path.
    void set_prevalidated_offer(bool enabled) { m_prevalidated_offer = enabled; }

    bool input_reconciliation_required() const { return m_input_reconciliation_required; }

    bool bench_capacity_rejection_verified() const { return m_bench_capacity_rejection_verified; }

    bool supply_confirmation_issued() const { return m_supply_confirmation_issued; }

    bool verify(AsstMsg msg, const json::value& details) const override;

protected:
    bool _run() override;
    bool on_run_fails() override;

private:
    bool require_input_reconciliation(std::string_view event, std::string_view reason, bool confirmation_issued);
    bool reconcile_bench_capacity_rejection(const cv::Mat& frame, const std::vector<TextObservation>& detail, int slot);
    bool run_strategy_refresh(const cv::Mat& image);
    bool run_equipment_discount(const cv::Mat& image);
    std::string m_name;
    int m_price = 0;
    int m_funds = 0;
    int m_round = 0;
    int m_slot = -1;
    bool m_upgrade = false;
    bool m_refresh = false;
    bool m_strategy_refresh = false;
    bool m_equipment_discount = false;
    int m_strategy_shop_level = 0;
    bool m_freeze = false;
    bool m_supply = false;
    bool m_supply_confirmation_issued = false;
    int m_supply_slot = -1;
    bool m_bounty = false;
    bool m_tactical = false;
    bool m_bounty_confirm = false;
    int m_bounty_slot = -1;
    bool m_prevalidated_offer = false;
    bool m_fast_mode = false;
    bool m_input_reconciliation_required = false;
    bool m_bench_capacity_rejection_verified = false;
};
}
