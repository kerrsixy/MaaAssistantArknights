#include "Task/MiniGame/SPA/SPACashEquipmentPolicy.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace asst::spa;
    static_assert(profitable_passbook_purchase(9, 14, 2));
    static_assert(!profitable_passbook_purchase(12, 14, 2));
    static_assert(!profitable_passbook_purchase(14, 14, 1));
    static_assert(!profitable_passbook_purchase(0, 14, 2));
    // These are synthetic panel transcriptions. No archived selected-book
    // screenshot was found, so this test does not establish live OCR/use.
    CashEquipmentPanelEvidence panel { "定期存折", "定期存折", "装备时销毁，获得5资金（持有时每回合+1）", 8, 42,
                                       true,       true };
    auto quote = cash_equipment_redemption_quote(panel, 8, 42);
    assert(quote && quote->amount == 5 && quote->round == 8 && quote->frame_id == 42);
    for (const int amount : { 1, 2, 10, 20, 21, 99, 999 }) {
        panel.effect_text = "装备时销毁，获得" + std::to_string(amount) + "资金(持有时每回合+1)";
        quote = cash_equipment_redemption_quote(panel, 8, 42);
        assert(quote && quote->amount == amount);
    }
    for (const auto* invalid : { "装备时销毁，获得0资金（持有时每回合+1）",
                                 "装备时销毁，获得{showtag_value_1}资金（持有时每回合+1）",
                                 "装备时销毁，获得当前累计的资金（持有时每回合+1）",
                                 "装备时销毁，获得5资金",
                                 "装备时销毁，获得05资金（持有时每回合+1）",
                                 "装备时销毁，获得-5资金（持有时每回合+1）",
                                 "装备时销毁，获得5.0资金（持有时每回合+1）",
                                 "装备时销毁，获得1000资金（持有时每回合+1）",
                                 "装备时销毁，获得999999999999999999999资金（持有时每回合+1）",
                                 "装备时销毁，获得5资金（持有时每回合+2）",
                                 "装备时销毁，下回合获得5资金（持有时每回合+1）",
                                 "装备时销毁，获得5资金（持有时每回合+1），获得6资金",
                                 "装备时销毁，获得5资金（持有时每回合+1），获得5资金" }) {
        panel.effect_text = invalid;
        assert(!cash_equipment_redemption_quote(panel, 8, 42));
    }
    panel.effect_text = "装备时销毁，\n获得 5资金（持有时每回合+1）。";
    assert(cash_equipment_redemption_quote(panel, 8, 42));
    assert(!cash_equipment_redemption_quote(panel, 9, 42));
    assert(!cash_equipment_redemption_quote(panel, 8, 43));
    assert(!cash_equipment_redemption_quote(panel, 8, 0));
    panel.title_verified = false;
    assert(!cash_equipment_redemption_quote(panel, 8, 42));
    panel.title_verified = true;
    panel.effect_complete = false;
    assert(!cash_equipment_redemption_quote(panel, 8, 42));
    panel.effect_complete = true;
    panel.selected_title = "盟约之币";
    assert(!cash_equipment_redemption_quote(panel, 8, 42));

    panel.equipment = "盟约之币";
    for (const int amount : { 1, 2 }) {
        panel.effect_text = "装备时销毁，并获得" + std::to_string(amount) + "资金";
        quote = cash_equipment_redemption_quote(panel, 8, 42);
        assert(quote && quote->amount == amount);
    }
    for (const auto* invalid : { "装备时销毁，并获得3资金",
                                 "装备时销毁，并获得1资金，获得2资金",
                                 "装备时销毁，并获得1资金，下回合额外获得2资金" }) {
        panel.effect_text = invalid;
        assert(!cash_equipment_redemption_quote(panel, 8, 42));
    }
    panel.equipment = panel.selected_title = "见钱眼开玩偶";
    panel.effect_text = "装备时销毁，下回合额外获得2资金";
    assert(!cash_equipment_redemption_quote(panel, 8, 42));
    std::cout << "Stored cash and coin quote parsing passed; live stored-cash OCR/use remains unverified\n";
}
