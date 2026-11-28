#include <filesystem>
#include <iostream>

#include "AsstCaller.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Vision/Roguelike/RoguelikeRecruitImageAnalyzer.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    using namespace asst;
    const std::filesystem::path resources(argv[1]), fixtures(argv[2]), output(argv[3]);
    std::filesystem::create_directories(output);
    if (!AsstSetUserDir(MAA_NS::from_u16(output.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(resources.native()).c_str())) {
        return 3;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](const cv::Mat& image, std::optional<int> expected, const char* message) {
        const auto reading = RoguelikeRecruitImageAnalyzer::read_blackflow_hope_balance(image);
        ++checks;
        if (reading.balance() != expected) {
            ++failures;
            std::cerr << "FAIL " << message << '\n';
        }
        std::cout << message << " displayed=" << reading.displayed.value_or(-1)
                  << " selected_price=" << reading.selected_cost.value_or(-1)
                  << " state=" << static_cast<int>(reading.state) << " balance=" << reading.balance().value_or(-1)
                  << '\n';
    };
    auto selected = MAA_NS::imread(fixtures / L"page_2026.11.28-00.04.31.811_1.png");
    expect(selected, 6, "actual selected Typhon frame restores six hope");
    expect(
        MAA_NS::imread(fixtures / L"page_2026.11.28-00.06.14.315_6.png"),
        6,
        "actual unselected next voucher reads six hope");
    auto missing_identity = selected.clone();
    missing_identity(cv::Rect(0, 68, 335, 55)).setTo(cv::Scalar(255, 255, 255));
    expect(missing_identity, {}, "missing selection identity cannot borrow the card price");
    auto signed_preview = selected.clone();
    signed_preview(cv::Rect(589, 32, 8, 2)).setTo(cv::Scalar(0, 255, 255));
    expect(signed_preview, {}, "possible negative preview is not read as positive digits");
    auto missing_header = selected.clone();
    missing_header(cv::Rect(430, 0, 135, 62)).setTo(cv::Scalar(0, 0, 0));
    expect(missing_header, {}, "missing recruitment header remains unknown");
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
