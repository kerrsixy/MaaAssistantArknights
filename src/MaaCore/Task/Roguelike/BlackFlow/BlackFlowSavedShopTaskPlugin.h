#pragma once

#include "BlackFlowTaskPluginBase.h"

#ifdef ASST_DEBUG
namespace asst::blackflow
{
struct SavedShopServiceRequest
{
    std::string uid;
    std::string client;
    int floor = 0;
    std::string roster_file;
};

std::optional<SavedShopServiceRequest>
    parse_saved_shop_service_request(const json::value& params, std::string* error = nullptr);
std::optional<int> recognize_saved_shop_service(const cv::Mat& image, int expected_floor);
bool recognize_saved_shop_service_map(const cv::Mat& image);

class BlackFlowSavedShopTaskPlugin final : public BlackFlowTaskPluginBase
{
public:
    using BlackFlowTaskPluginBase::BlackFlowTaskPluginBase;
    bool load_params(const json::value& params) override;
    bool verify(AsstMsg msg, const json::value& details) const override;

protected:
    bool _run() override;

private:
    enum class PendingWork
    {
        None,
        Account,
        Ready,
        Map
    };
    std::optional<SavedShopServiceRequest> m_request;
    mutable PendingWork m_pending = PendingWork::None;
    bool m_account_verified = false;
    bool m_started = false;
    bool m_finished = false;
};
}
#endif
