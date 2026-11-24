#include <filesystem>
#include <iostream>

#include "AsstCaller.h"
#include "Config/TaskData.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Task/Roguelike/BlackFlow/BlackFlowScrapTradeInventory.h"
#include "Utils/Platform.hpp"
#include "Vision/Matcher.h"
#include "Vision/OCRer.h"

class SaleContext final : public asst::blackflow::BlackFlowScrapTradeInventoryContext
{
public:
    cv::Mat shelf;
    cv::Mat bulk;
    bool change_to_bulk = false;
    int clicks = 0;
    int executions = 0;
    asst::Rect last_click;

    cv::Mat capture() const override { return clicks || !change_to_bulk ? bulk : shelf; }

    bool interrupted() const override { return false; }

    bool precise_swipe_supported() const override { return true; }

    bool swipe_by(std::string_view, std::optional<int>, std::string*) override { return false; }

    bool click(const asst::Rect& value) override
    {
        ++clicks;
        last_click = value;
        return true;
    }

    bool execute(std::string_view, std::string*) override
    {
        ++executions;
        return false;
    }

    std::string last_task() const override { return {}; }

    void wait(unsigned) const override {}

    void on_inventory_item(const asst::blackflow::ScrapTradeInventoryItem&) override {}
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path repo(argv[1]), cases(argv[2]), output(argv[3]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str()) || !Task.load(cases / "BlackFlow.json")) {
        return 3;
    }
    const auto shelf = MAA_NS::imread(cases / "selling.png");
    const auto bulk = MAA_NS::imread(cases / "bulk.png");
    if (shelf.empty() || bulk.empty()) {
        return 4;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool value, const char* reason) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << reason << '\n';
        }
    };
    const Rect roi { 395, 159, 857, 325 };
    const auto click = scrap_trade_sale_click_rect({ 835, 159, 86, 24 }, roi);
    expect(click.has_value(), "observed clipped top-row label has a card-interior target");
    if (click) {
        expect(click->y >= 215 && click->y + click->height <= 484, "click clears top controls and lower leave overlay");
        expect(click->x >= 822 && click->x + click->width < 1017, "click stays within observed SKU column");
    }
    const auto lower = scrap_trade_sale_click_rect({ 1040, 459, 100, 24 }, roi);
    expect(lower && lower->y + lower->height <= 484, "low right-column title cannot target leave overlay");
    expect(!scrap_trade_sale_click_rect({ 30, 159, 86, 24 }, roi), "bulk-layout title is not a normal shelf target");
    expect(!scrap_trade_sale_click_rect({ 835, 159, 0, 24 }, roi), "zero-width OCR cannot generate input");
    const auto match = [&](const cv::Mat& image, const char* task) {
        Matcher matcher(image);
        matcher.set_task_info(task);
        return matcher.analyze().has_value();
    };
    const auto ocr = [&](const cv::Mat& image, const char* task) {
        OCRer analyzer(image);
        analyzer.set_task_info(task);
        return analyzer.analyze().has_value();
    };
    expect(match(shelf, "BlackFlow@Roguelike@CultivateSellingFlag"), "actual selling shelf recognized");
    expect(!match(bulk, "BlackFlow@Roguelike@CultivateSellingFlag"), "bulk selection is not a selling shelf");
    expect(ocr(bulk, "BlackFlow@Roguelike@ScrapTradeBulkSelectionFlag"), "native OCR identifies actual bulk page");
    expect(ocr(bulk, "BlackFlow@Roguelike@ScrapTradeBulkSelectionCancel"), "native OCR finds actual cancel control");
    expect(
        !ocr(shelf, "BlackFlow@Roguelike@ScrapTradeBulkSelectionFlag"),
        "normal shelf does not authorize bulk cancel");
    expect(
        !ocr(shelf, "BlackFlow@Roguelike@ScrapTradeBulkSelectionCancel"),
        "normal shelf has no bulk cancellation target");
    ScrapTradeInventoryTarget target;
    target.text.text = "雾滚草";
    target.text.rect = { 835, 159, 86, 24 };
    SaleContext wrong;
    wrong.shelf = shelf;
    wrong.bulk = bulk;
    BlackFlowScrapTradeInventory inventory;
    std::string error;
    expect(!inventory.sell(wrong, target, &error), "seller rejects actual bulk page before input");
    expect(wrong.clicks == 0 && wrong.executions == 0, "page rejection emits no click or confirm");
    SaleContext changed;
    changed.shelf = shelf;
    changed.bulk = bulk;
    changed.change_to_bulk = true;
    expect(!inventory.sell(changed, target, &error), "page transition aborts the existing sale intent");
    expect(changed.clicks == 1 && changed.executions == 0, "bulk transition cannot cause blind retry or confirmation");
    expect(click && changed.last_click == *click, "production seller uses the qualified card-interior rectangle");
    std::cout << "SCRAP_SALE_GUARD checks=" << checks << " failures=" << failures
              << " connections=0 input_calls=0 live_recovery=false\n";
    return failures ? 1 : 0;
}
