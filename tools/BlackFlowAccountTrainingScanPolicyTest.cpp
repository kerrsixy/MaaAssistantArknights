#include <iostream>

#include "Task/Roguelike/RoguelikeRecruitPolicy.h"

int main()
{
    using asst::is_account_training_scan_candidate;
    int checks = 0, failures = 0;
    const auto expect = [&](bool value, const char* message) {
        ++checks;
        if (!value) {
            ++failures;
            std::cerr << "FAIL " << message << '\n';
        }
    };
    expect(!is_account_training_scan_candidate(true, 0, 1), "untrained level one is not opened");
    expect(!is_account_training_scan_candidate(true, 1, 1), "elite one level one stays below recruitment gate");
    expect(!is_account_training_scan_candidate(true, 1, 54), "elite one below 55 is not opened");
    expect(is_account_training_scan_candidate(true, 1, 55), "trained low rarity support remains observable");
    expect(is_account_training_scan_candidate(true, 2, 1), "elite two level one is already promoted");
    expect(is_account_training_scan_candidate(true, 2, 90), "trained core remains observable");
    expect(!is_account_training_scan_candidate(false, 2, 90), "unowned cards cannot create training evidence");
    expect(!is_account_training_scan_candidate(true, 1, 0), "unknown list level does not justify a visit");
    expect(!is_account_training_scan_candidate(true, 2, 0), "unknown promoted level stays unknown");
    expect(!is_account_training_scan_candidate(true, -1, 90), "unknown elite stays unknown");
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
