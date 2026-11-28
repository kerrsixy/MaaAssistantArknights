#pragma once

#include <string>
#include <utility>

#include "BlackFlowInventoryModel.h"
#include "MaaUtils/NoWarningCVMat.hpp"

namespace asst::blackflow
{
enum class InventoryDiscardReadiness
{
    Ready,
    Unknown,
    Unavailable,
};

enum class InventoryDiscardOutcome
{
    Discarded,
    ReadinessUnresolved,
    ReadinessUnavailable,
    ActionFailed,
    Interrupted,
};

// Exact existing task OCR only; this producer never runs a ProcessTask, clicks,
// relaxes text matching, or represents an unrecognized frame as ready.
InventoryDiscardReadiness observe_inventory_discard_readiness(const cv::Mat& image, std::string* error);

// The actual cleanup consumer uses this same bounded operation. Context keeps
// its existing capture/wait/execute contract; no controller or task flow is owned
// here. Tests may supply typed logged observations but cannot call them pixels.
template <typename Context, typename ObserveReady>
InventoryDiscardOutcome discard_inventory_item(
    Context& context,
    const VisibleScrap& scrap,
    ObserveReady&& observe_ready,
    std::string* error)
{
    constexpr int MaxReadyObservations = 3;
    constexpr unsigned DiscardSettleDelay = 300;
    const auto fail = [&](std::string reason) {
        if (error != nullptr) {
            *error = std::move(reason);
        }
    };
    const auto interrupted = [&] {
        if (context.interrupted()) {
            fail("inventory discard interrupted");
            return true;
        }
        return false;
    };
    if (interrupted()) {
        return InventoryDiscardOutcome::Interrupted;
    }
    // The caller has already located and verified the actual card identity and
    // position. Unknown button OCR must not invalidate that proof by re-clicking.
    if (!context.click(scrap.rect)) {
        fail("failed to click the scrap card: " + scrap.name);
        return InventoryDiscardOutcome::ActionFailed;
    }
    context.wait(DiscardSettleDelay);
    for (int observation = 0; observation < MaxReadyObservations; ++observation) {
        if (interrupted()) {
            return InventoryDiscardOutcome::Interrupted;
        }
        const cv::Mat image = context.capture();
        if (interrupted()) {
            return InventoryDiscardOutcome::Interrupted;
        }
        const auto readiness = observe_ready(image, error);
        if (readiness == InventoryDiscardReadiness::Unavailable) {
            return InventoryDiscardOutcome::ReadinessUnavailable;
        }
        if (readiness == InventoryDiscardReadiness::Ready) {
            if (interrupted()) {
                return InventoryDiscardOutcome::Interrupted;
            }
            // Preserve the original actual discard/confirmation chain and its
            // failures. Optional readiness is the only operation removed from it.
            if (!context.execute({ "BlackFlow@Roguelike@InventoryCleanupDiscard" }, error)) {
                return InventoryDiscardOutcome::ActionFailed;
            }
            if (!context.last_task().ends_with("BlackFlow@Roguelike@InventoryCleanupDiscarded")) {
                fail("discard was not confirmed for: " + scrap.name);
                return InventoryDiscardOutcome::ActionFailed;
            }
            return InventoryDiscardOutcome::Discarded;
        }
        if (observation + 1 < MaxReadyObservations) {
            context.wait(DiscardSettleDelay);
        }
    }
    fail("discard readiness remained unresolved after bounded observations: " + scrap.name);
    return InventoryDiscardOutcome::ReadinessUnresolved;
}
}
