#include <filesystem>
#include <iostream>

#include "AsstCaller.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Vision/Roguelike/BlackFlow/BlackFlowAccountListOrderAnalyzer.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    using namespace asst::blackflow;
    const std::filesystem::path resources(argv[1]), fixtures(argv[2]), output(argv[3]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(resources.native()).c_str())) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](const cv::Mat& image, std::optional<AccountListOrder> expected, const char* message) {
        ++checks;
        if (BlackFlowAccountListOrderAnalyzer(image).analyze() != expected) {
            ++failures;
            std::cerr << "FAIL " << message << '\n';
        }
    };
    const auto ascending = MAA_NS::imread(fixtures / L"b_stopped_scan.png");
    const auto descending = MAA_NS::imread(fixtures / L"b_sort_descending.png");
    expect(ascending, AccountListOrder::Ascending, "actual ascending sort is detected");
    expect(descending, AccountListOrder::Descending, "actual descending sort is detected");
    auto missing_arrow = descending.clone();
    missing_arrow(cv::Rect(840, 42, 70, 24)).setTo(cv::Scalar(40, 40, 40));
    expect(missing_arrow, {}, "missing direction must not cause a blind toggle");
    auto missing_label = descending.clone();
    missing_label(cv::Rect(820, 15, 90, 30)).setTo(cv::Scalar(40, 40, 40));
    expect(missing_label, {}, "unanchored arrow is not a sort receipt");
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
