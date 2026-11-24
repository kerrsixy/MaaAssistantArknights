#include <algorithm>
#include <filesystem>
#include <iostream>

#include "AsstCaller.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Task/Roguelike/BlackFlow/BlackFlowScrapPurchase.h"
#include "Vision/Matcher.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 5) {
        return 2;
    }
    const std::filesystem::path repo(argv[1]), overlay(argv[2]), fixtures(argv[3]), output(argv[4]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(overlay.native()).c_str())) {
        return 3;
    }
    int checks = 0;
    int failures = 0;
    const auto expect = [&](bool value, const char* reason) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << reason << '\n';
        }
    };
    const auto buying = [](const cv::Mat& image) {
        asst::Matcher matcher(image);
        matcher.set_task_info("BlackFlow@Roguelike@ScrapTradeBuyingFlag");
        return matcher.analyze().has_value();
    };
    const auto sold = [](const cv::Mat& image, const asst::Rect& title) {
        asst::Matcher matcher(image);
        matcher.set_task_info("BlackFlow@Roguelike@ScrapTradeSoldBadge");
        const auto region = asst::blackflow::scrap_purchase_sold_region(title, image.cols, image.rows);
        if (!region) {
            return false;
        }
        matcher.set_roi(*region);
        return matcher.analyze().has_value();
    };
    const auto before = MAA_NS::imread(fixtures / "screen_00161.png");
    const auto after = MAA_NS::imread(fixtures / "screen_00162.png");
    const auto selling = MAA_NS::imread(fixtures / "screen_00160.png");
    expect(!before.empty() && !after.empty() && !selling.empty(), "three original independent device frames");
    expect(buying(before) && buying(after), "purchase page is positively recognized before and after debit");
    expect(!buying(selling), "gray purchase button on selling page is rejected");
    const asst::Rect fish(832, 389, 63, 27), mother(1038, 180, 123, 22), limb(831, 179, 85, 25),
        wheel(625, 178, 85, 26);
    expect(sold(before, fish), "old fish sold badge is visible");
    expect(!sold(before, mother), "mother is not already sold in the before frame");
    expect(sold(after, mother), "mother has a new sold badge after the actual debit");
    expect(sold(after, limb), "limb has its own sold badge");
    expect(!sold(after, wheel), "neighboring sold badges cannot authorize an unsold wheel");
    expect(!buying(cv::Mat::zeros(720, 1280, CV_8UC3)), "blank cannot prove a purchase page");
    std::cout << "SCRAP_PURCHASE_VISION checks=" << checks << " failures=" << failures
              << " connections=0 input_calls=0\n";
    return failures ? 1 : 0;
}
