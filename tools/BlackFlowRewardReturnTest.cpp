#include <filesystem>
#include <iostream>

#include <opencv2/imgproc.hpp>

#include "AsstCaller.h"
#include "Config/TaskData.h"
#include "MaaUtils/Encoding.h"
#include "MaaUtils/ImageIo.h"
#include "Vision/Miscellaneous/PipelineAnalyzer.h"

int wmain(int argc, wchar_t **argv) {
  if (argc != 5) {
    return 2;
  }
  const auto output = MAA_NS::from_u16(std::wstring(argv[4]));
  std::filesystem::create_directories(std::filesystem::path(argv[4]));
  AsstSetUserDir(output.c_str());
  if (!AsstLoadResource(MAA_NS::from_u16(std::wstring(argv[1])).c_str())) {
    return 3;
  }
  const auto overlay = json::open(std::filesystem::path(argv[2]));
  if (!overlay || !asst::Task.lazy_parse(*overlay)) {
    return 4;
  }
  const auto original = MAA_NS::imread(std::filesystem::path(argv[3]));
  if (original.empty()) {
    return 5;
  }
  int checks = 0, failures = 0;
  const auto check =
      [&](const cv::Mat &frame, const std::vector<std::string> &names,
          const std::string &expected, const std::string &label) {
        asst::PipelineAnalyzer analyzer(frame);
        analyzer.set_tasks(names);
        const auto result = analyzer.analyze();
        const std::string actual = result ? result->task_ptr->name : "";
        ++checks;
        failures += actual != expected;
        std::cout << label << " expected=" << expected << " actual=" << actual
                  << '\n';
      };
  const std::string guard = "BlackFlow@Roguelike@RewardReturnMap";
  const std::string drops = "BlackFlow@Roguelike@DropsFlag";
  check(original, {drops}, drops, "OriginalFalseRewardMatch");
  check(original, {guard, drops}, guard, "MapBeforeFooterMatch");
  check(original, asst::Task.get("BlackFlow@Roguelike@GetDrops")->next, guard,
        "RewardNextMapReturn");
  auto masked = original.clone();
  cv::rectangle(masked, cv::Rect(1080, 70, 195, 65), cv::Scalar::all(0),
                cv::FILLED);
  check(masked, {guard}, "", "MissingActionPointsRejected");
  masked = original.clone();
  cv::rectangle(masked, cv::Rect(524, 0, 246, 42), cv::Scalar::all(0),
                cv::FILLED);
  check(masked, {guard}, "", "MissingFloorTitleRejected");
  check(cv::Mat::zeros(original.size(), original.type()), {guard}, "",
        "BlankRejected");
  std::cout << "REWARD_RETURN checks=" << checks << " failures=" << failures
            << " device_inputs=0\n";
  return failures ? 1 : 0;
}
