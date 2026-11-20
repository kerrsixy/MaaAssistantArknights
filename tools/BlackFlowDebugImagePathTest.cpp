#include <filesystem>
#include <iostream>

#include "Utils/DebugImageHelper.hpp"

int main()
try {
    const auto root = std::filesystem::current_path() / asst::utils::path("中文截图路径 regression") /
                      asst::utils::path(MAA_NS::format_now_for_filename());
    std::filesystem::create_directories(root);
    if (!asst::UserDir.set(root)) {
        return 1;
    }

    const cv::Mat image(8, 8, CV_8UC3, cv::Scalar(20, 40, 60));
    for (const std::string suffix : { "灌水贤者", "光荣远征", "日本語", "ASCII" }) {
        if (!asst::utils::save_debug_image(image, asst::utils::path("debug") / "skill", false, "", suffix)) {
            return 2;
        }
    }
    if (!asst::utils::save_debug_image(image, root / "absolute", false, "", "灌水贤者")) {
        return 3;
    }

    size_t checked = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.path().extension() != ".png") {
            continue;
        }
        const auto saved = MAA_NS::imread(entry.path());
        if (saved.empty() || cv::norm(saved, image, cv::NORM_INF) != 0) {
            return 4;
        }
        ++checked;
    }
    std::cout << "Debug image round trips: " << checked << '\n';
    return checked == 5 ? 0 : 5;
}
catch (const std::exception& error) {
    std::cerr << "Debug image save failed: " << error.what() << '\n';
    return 6;
}
