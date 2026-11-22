#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "AsstCaller.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Task/BattleHelper.h"

class MapEvidenceProbe : public asst::BattleHelper
{
public:
    MapEvidenceProbe() :
        BattleHelper(nullptr)
    {
        m_stage_name = "遗忘时间";
        m_normal_tile_info[{ 4, 1 }].pos = { 640, 177 };
    }

    asst::AbstractTask& this_task() override { std::abort(); }

    using BattleHelper::save_map;
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) {
        return 2;
    }
    const std::filesystem::path repo(argv[1]), source(argv[2]), out(argv[3]);
    std::filesystem::create_directories(out);
    if (!AsstSetUserDir(MAA_NS::from_u16(out.native()).c_str()) ||
        !AsstLoadResource(MAA_NS::from_u16(repo.native()).c_str())) {
        return 3;
    }
    const auto observed = MAA_NS::imread(source);
    if (observed.empty()) {
        return 4;
    }
    const auto original = observed.clone();
    MapEvidenceProbe probe;
    probe.save_map(observed);
    probe.save_map(observed);
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    expect(cv::norm(original, observed, cv::NORM_INF) == 0, "annotating does not mutate the observed frame");
    int raw = 0, annotated = 0;
    for (const auto& entry : std::filesystem::directory_iterator(out / "debug/map_raw")) {
        if (entry.path().extension() != ".png") {
            continue;
        }
        ++raw;
        const auto saved = MAA_NS::imread(entry.path());
        expect(
            !saved.empty() && saved.size() == original.size() && cv::norm(saved, original, cv::NORM_INF) == 0,
            "raw evidence is a lossless copy of the original pixels");
    }
    for (const auto& entry : std::filesystem::directory_iterator(out / "debug/map")) {
        if (entry.path().extension() != ".jpeg") {
            continue;
        }
        ++annotated;
        const auto saved = MAA_NS::imread(entry.path());
        expect(!saved.empty() && saved.size() == original.size(), "annotated recognition map is still available");
    }
    expect(raw == 2, "both camera observations retain raw PNGs");
    expect(annotated == 2, "both camera observations retain recognition maps");
    std::cout << "MAP_EVIDENCE checks=" << checks << " failures=" << failures << " inputs=0 connections=0\n";
    return failures ? 1 : 0;
}
