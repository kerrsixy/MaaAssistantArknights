#include "Task/MiniGame/SPA/SPASelectedPanelEvidence.h"
#include <cassert>
#include <iostream>
#include <opencv2/imgcodecs.hpp>

int main(int argc, char** argv)
{
    // The standalone test copies unmodified archived quick3 frames to ASCII
    // paths. No recognition result is fabricated by this pixel-cache test.
    assert(argc == 3);
    const auto selected = cv::imread(argv[1]);
    const auto clean = cv::imread(argv[2]);
    assert(selected.size() == cv::Size(1280, 720));
    assert(clean.size() == selected.size());
    asst::spa::SelectedPanelEvidence evidence;
    assert(!evidence.consume_unchanged(selected));

    evidence.remember_recognized_title(selected);
    assert(evidence.consume_unchanged(selected));
    // One observation cannot bypass the post-click disappearance check.
    assert(!evidence.consume_unchanged(selected));

    evidence.remember_recognized_title(selected);
    assert(!evidence.consume_unchanged(clean));
    assert(!evidence.consume_unchanged(selected));

    auto changed = selected.clone();
    changed.at<cv::Vec3b>(190, 20)[0] ^= 1;
    evidence.remember_recognized_title(selected);
    assert(!evidence.consume_unchanged(changed));
    assert(!evidence.consume_unchanged(selected));

    auto elsewhere = selected.clone();
    elsewhere.at<cv::Vec3b>(400, 800)[0] ^= 1;
    evidence.remember_recognized_title(selected);
    assert(evidence.consume_unchanged(elsewhere));

    auto caller_owned = selected.clone();
    evidence.remember_recognized_title(caller_owned);
    caller_owned.at<cv::Vec3b>(190, 20)[0] ^= 1;
    assert(!evidence.consume_unchanged(caller_owned));
    evidence.remember_recognized_title(selected);
    evidence.clear();
    assert(!evidence.consume_unchanged(selected));
    evidence.remember_recognized_title(cv::Mat());
    assert(!evidence.consume_unchanged(selected));
    evidence.remember_recognized_title(selected);
    assert(!evidence.consume_unchanged(cv::Mat(720, 1280, CV_8UC1)));
    assert(!evidence.consume_unchanged(selected));
    evidence.remember_recognized_title(selected);
    assert(!evidence.consume_unchanged(cv::Mat(360, 640, CV_8UC3)));
    assert(!evidence.consume_unchanged(selected));
    std::cout << "Selected-panel evidence: archived-frame exact match, one-use, changed/invalid fallback passed.\n";
}
