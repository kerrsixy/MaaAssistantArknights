#include <iostream>

#include "Task/Roguelike/BlackFlow/BlackFlowScrapPurchase.h"
#include "Task/Roguelike/BlackFlow/BlackFlowScrapTradeTaskPlugin.h"

int main()
{
    using namespace asst;
    using namespace asst::blackflow;
    int checks = 0;
    int failures = 0;
    const auto expect = [&](bool value, const char* reason) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << reason << '\n';
        }
    };
    const auto intent = [](std::string name = "老妈妈的融雪", int wallet = 14, int price = 8) {
        return ScrapPurchaseIntent { std::move(name), Rect(1038, 180, 123, 22), true, wallet, price };
    };
    const ScrapPurchaseObservation paid { true, false, "老妈妈的融雪", 6 };
    {
        ScrapPurchaseTransaction tx;
        expect(tx.begin(intent()), "historical 14-ingot purchase starts");
        expect(!tx.begin(intent("报废假肢", 6, 2)), "pending mother cannot be overwritten by next offer");
        tx.note_confirmation_input();
        expect(!tx.take_confirmed(), "input acknowledgement alone is not a purchase");
        expect(tx.observe(paid) == ScrapPurchaseState::Confirmed, "14 to 6 plus matching sold slot settles mother");
        const auto receipt = tx.take_confirmed();
        expect(
            receipt && receipt->intent.name == "老妈妈的融雪" && receipt->paid == 8 && receipt->intent.keep,
            "exact pending SKU and actual debit are retained");
        expect(!tx.take_confirmed(), "receipt can update the ledger only once");
        tx.note_confirmation_input();
        expect(!tx.take_confirmed(), "duplicate late callback cannot create another receipt");
        expect(tx.begin(intent("报废假肢", 6, 2)), "next offer allowed only after mother receipt is consumed");
    }
    {
        ScrapPurchaseTransaction tx;
        expect(tx.begin(intent("白模鱼", 4, 4)), "historical already-sold fish attempt is represented");
        tx.note_confirmation_input();
        for (int i = 0; i < 2; ++i) {
            expect(
                tx.observe({ true, false, "白模鱼", 4 }) == ScrapPurchaseState::AwaitingReceipt,
                "unchanged wallet plus old sold badge is not proof");
        }
        expect(
            tx.observe({ true, false, "白模鱼", 4 }) == ScrapPurchaseState::Failed,
            "unchanged wallet fails within bounded observations");
        expect(!tx.take_confirmed(), "already-sold slot never creates inventory");
        expect(!tx.begin(intent()), "failed pending purchase cannot silently be replaced");
    }
    for (const auto& observation : { ScrapPurchaseObservation { false, false, "老妈妈的融雪", 6 },
                                     ScrapPurchaseObservation { true, true, "老妈妈的融雪", 6 },
                                     ScrapPurchaseObservation { true, false, "报废假肢", 6 },
                                     ScrapPurchaseObservation { true, false, "老妈妈的融雪", std::nullopt } }) {
        ScrapPurchaseTransaction tx;
        expect(tx.begin(intent()), "counterexample starts");
        tx.note_confirmation_input();
        expect(
            tx.observe(observation) == ScrapPurchaseState::AwaitingReceipt,
            "wrong page, visible dialog, different SKU or unknown wallet cannot "
            "settle");
        expect(!tx.take_confirmed(), "counterexample produces no ledger update");
    }
    for (int after : { -1, 15, 5 }) {
        ScrapPurchaseTransaction tx;
        expect(tx.begin(intent()), "inconsistent debit case starts");
        tx.note_confirmation_input();
        expect(
            tx.observe({ true, false, "老妈妈的融雪", after }) == ScrapPurchaseState::Failed,
            "invalid, increased or excessive wallet change is rejected");
        expect(!tx.take_confirmed(), "inconsistent change has no receipt");
    }
    {
        ScrapPurchaseTransaction tx;
        expect(tx.begin(intent()), "discount case starts");
        tx.note_confirmation_input();
        expect(
            tx.observe({ true, false, "老妈妈的融雪", 10 }) == ScrapPurchaseState::Confirmed,
            "discounted real debit plus matching sold slot qualifies");
        const auto receipt = tx.take_confirmed();
        expect(receipt && receipt->paid == 4, "discount receipt uses actual four ingots");
    }
    {
        ScrapPurchaseTransaction tx;
        expect(tx.begin(intent()), "missing confirmation case starts");
        expect(
            tx.observe(paid) == ScrapPurchaseState::AwaitingReceipt,
            "debit and sold badge without this transaction input do not settle");
        expect(!tx.take_confirmed(), "missing input produces no receipt");
    }
    for (const auto& invalid : { intent("", 14, 8), intent("A", -1, 8), intent("A", 3, 4), intent("A", 8, 0) }) {
        ScrapPurchaseTransaction tx;
        expect(!tx.begin(invalid), "invalid intent rejected before any input");
    }
    {
        BlackFlowScrapTradeTaskPlugin plugin(
            {},
            nullptr,
            "Roguelike",
            std::make_shared<RoguelikeConfig>(),
            nullptr,
            std::make_shared<BlackFlowSession>(),
            nullptr);
        const auto callback = [](std::string task, std::string subtask = "ProcessTask") {
            return json::object { { "subtask", subtask }, { "details", json::object { { "task", task } } } };
        };
        expect(
            plugin.verify(AsstMsg::SubTaskCompleted, callback("BlackFlow@Roguelike@ScrapTradeBuyConfirm")),
            "actual plugin observes the first confirmation input");
        expect(
            plugin.verify(AsstMsg::SubTaskCompleted, callback("BlackFlow@Roguelike@ScrapTradeBuyDialog")),
            "actual plugin observes the formerly missed dialog input");
        expect(
            !plugin.verify(AsstMsg::SubTaskStart, callback("BlackFlow@Roguelike@ScrapTradeBuyDialog")),
            "unexecuted dialog is not an acknowledged input");
        expect(
            !plugin.verify(AsstMsg::SubTaskError, callback("BlackFlow@Roguelike@ScrapTradeBuyDialog")),
            "failed dialog cannot acknowledge purchase input");
        expect(
            !plugin.verify(AsstMsg::SubTaskCompleted, callback("Unrelated")),
            "unrelated leaf cannot acknowledge purchase input");
        expect(
            !plugin.verify(AsstMsg::SubTaskCompleted, callback("BlackFlow@Roguelike@ScrapTradeBuyDialog", "OtherTask")),
            "other subtask cannot acknowledge a purchase");
    }
    std::cout << "SCRAP_PURCHASE_NATIVE checks=" << checks << " failures=" << failures
              << " connections=0 input_calls=0 live_purchase_proven=false\n";
    return failures ? 1 : 0;
}
