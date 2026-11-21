#include "Task/MiniGame/SPA/SPAControlEvidence.h"
#include <cassert>

int main()
{
    using asst::spa::ShopToggleEvidence;
    ShopToggleEvidence evidence;
    cv::Mat frame(720, 1280, CV_8UC3, cv::Scalar(40, 80, 120));
    assert(!evidence.observe_unchanged(frame));
    evidence.remember(frame, false);
    assert(evidence.observe_unchanged(frame) == false);
    auto changed_elsewhere = frame.clone();
    changed_elsewhere.at<cv::Vec3b>(100, 100) = cv::Vec3b(1, 2, 3);
    assert(evidence.observe_unchanged(changed_elsewhere) == false);
    auto changed_control = frame.clone();
    changed_control.at<cv::Vec3b>(670, 1190)[0] += 1;
    assert(!evidence.observe_unchanged(changed_control));
    evidence.remember(changed_control, true);
    assert(evidence.observe_unchanged(changed_control) == true);
    assert(!evidence.observe_unchanged(frame));
    // The evidence owns its pixels; later edits to the caller's image cannot
    // silently rewrite the remembered control or certify an opposite state.
    changed_control.at<cv::Vec3b>(670, 1190)[0] += 1;
    assert(!evidence.observe_unchanged(changed_control));
    assert(!evidence.observe_unchanged(cv::Mat(720, 1280, CV_8UC1)));
    assert(!evidence.observe_unchanged(cv::Mat(360, 640, CV_8UC3)));
    evidence.clear();
    assert(!evidence.observe_unchanged(frame));
    evidence.remember(cv::Mat(), true);
    assert(!evidence.observe_unchanged(frame));
}
