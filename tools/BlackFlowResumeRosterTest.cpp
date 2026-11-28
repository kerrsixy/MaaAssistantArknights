#include <filesystem>
#include <iostream>

#include "Task/Roguelike/BlackFlow/BlackFlowResumeRoster.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        return 2;
    }
    using namespace asst::blackflow;
    const auto account = json::open(std::filesystem::path(argv[1]));
    const auto snapshot = json::open(std::filesystem::path(argv[2]));
    if (!account || !snapshot) {
        return 3;
    }
    std::string error;
    const auto profile = BlackFlowAccountProfile::parse(*account, "Official", "90000002", &error);
    if (!profile) {
        return 4;
    }
    int checks = 0, failures = 0;
    const auto expect = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    };
    const auto parse = [&](const json::value& value) {
        error.clear();
        return parse_resume_roster(value, *profile, &error);
    };
    const auto restored = parse(*snapshot);
    expect(restored && restored->size() == 4, "actual run30 confirmed roster restores without changing account cache");
    expect(
        restored && restored->at("罗小黑").elite == 1 && restored->at("罗小黑").level == 60,
        "account E2LV1 permits observed E1LV60 and preserves observed promotion");
    expect(
        restored && restored->at("机械师").elite == 1 && restored->at("机械师").level == 80,
        "resume does not replace current-run strength with account maximum");
    const auto reject = [&](json::value value, const char* label) {
        expect(!parse(value) && !error.empty(), label);
    };
    auto value = *snapshot;
    value["uid"] = "other";
    reject(value, "different UID rejected");
    value = *snapshot;
    value["client"] = "Bilibili";
    reject(value, "different client rejected");
    value = *snapshot;
    value["schema"] = 2;
    reject(value, "unsupported schema rejected");
    value = *snapshot;
    value["acceptance_eligible"] = true;
    reject(value, "assisted resume cannot claim unattended acceptance");
    value = *snapshot;
    value["requires_fresh_same_uid_verification_before_import"] = false;
    reject(value, "fresh UID declaration remains mandatory");
    value = *snapshot;
    value["operators"] = json::array {};
    reject(value, "empty roster rejected");
    value = *snapshot;
    value["operators"] = json::object {};
    reject(value, "wrong collection type rejected");
    value = *snapshot;
    value["operators"][0] = json::value();
    reject(value, "malformed entry rejected");
    value = *snapshot;
    value["operators"][0]["name"] = "UnknownOperator";
    reject(value, "unknown ownership rejected");
    value = *snapshot;
    value["operators"][1] = value["operators"][0];
    reject(value, "duplicate operator rejected");
    for (int elite : { -1, 3 }) {
        value = *snapshot;
        value["operators"][0]["elite"] = elite;
        reject(value, "invalid promotion rejected");
    }
    for (int level : { 0, 91, 1000 }) {
        value = *snapshot;
        value["operators"][0]["level"] = level;
        reject(value, "unknown or globally invalid level rejected");
    }
    value = *snapshot;
    value["operators"][0]["elite"] = "2";
    reject(value, "string promotion rejected");
    value = *snapshot;
    value["operators"][0]["evidence"] = "";
    reject(value, "missing evidence rejected");
    value = *snapshot;
    json::value solo = snapshot->at("operators").at(3);
    expect(solo.get("name", "") == "罗小黑", "actual final receipt belongs to the promoted low-level account operator");
    solo["elite"] = 2;
    solo["level"] = 1;
    value["operators"] = json::array { solo };
    expect(parse(value).has_value(), "same-promotion equal account level accepted");
    solo["level"] = 2;
    value["operators"] = json::array { solo };
    reject(value, "same-promotion level above account rejected");
    solo["elite"] = 1;
    solo["level"] = 91;
    value["operators"] = json::array { solo };
    reject(value, "lower-promotion globally invalid level rejected");
    solo["elite"] = 0;
    solo["level"] = 50;
    value["operators"] = json::array { solo };
    expect(parse(value).has_value(), "lower promotion does not compare visible level against E2LV1");
    auto lower_account = *account;
    lower_account["operators"]["罗小黑"]["elite"] = 1;
    lower_account["operators"]["罗小黑"]["level"] = 60;
    const auto lower_profile = BlackFlowAccountProfile::parse(lower_account, "Official", "90000002", &error);
    expect(lower_profile.has_value(), "controlled E1 account fixture is valid");
    if (lower_profile) {
        solo["elite"] = 2;
        solo["level"] = 1;
        value["operators"] = json::array { solo };
        expect(
            !parse_resume_roster(value, *lower_profile, &error),
            "higher promotion remains forbidden even at level1");
        solo["elite"] = 1;
        solo["level"] = 61;
        value["operators"] = json::array { solo };
        expect(!parse_resume_roster(value, *lower_profile, &error), "same-promotion account cap remains enforced");
    }
    const auto account_oper = profile->find_operator("罗小黑");
    expect(
        account_oper && account_oper->get().elite == 2 && account_oper->get().level == 1,
        "restoration never mutates verified account strength");
    solo["elite"] = 0;
    solo["level"] = 50;
    value["operators"] = json::array { solo };
    expect(parse(value).has_value(), "lower-promotion observed level does not compare against reset account level");
    auto limited = *account;
    limited["operators"]["罗小黑"]["elite"] = 1;
    limited["operators"]["罗小黑"]["level"] = 60;
    const auto limited_profile = BlackFlowAccountProfile::parse(limited, "Official", "90000002", &error);
    expect(limited_profile.has_value(), "lower-promotion account fixture remains valid");
    solo["elite"] = 2;
    solo["level"] = 1;
    value["operators"] = json::array { solo };
    expect(
        limited_profile && !parse_resume_roster(value, *limited_profile, &error),
        "promotion above account rejected even at level1");
    solo["elite"] = 1;
    solo["level"] = 61;
    value["operators"] = json::array { solo };
    expect(
        limited_profile && !parse_resume_roster(value, *limited_profile, &error),
        "same lower-promotion level above account rejected");
    reject(json::array {}, "nonobject snapshot rejected");
    expect(parse(*snapshot).has_value(), "rejected import does not poison following valid import");
    std::cout << "RESUME_ROSTER checks=" << checks << " failures=" << failures << " connections=0 input=0\n";
    return failures ? 1 : 0;
}
