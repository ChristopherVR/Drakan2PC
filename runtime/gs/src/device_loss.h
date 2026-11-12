// A removed graphics device: which API results mean it, how it is described, and the test hook that pretends one.
// Standard C++ only. The Direct3D 11 backend asks these rules at its call sites (d3d11_device_loss.cpp); the WebGPU
// backend learns of a loss from wgpu-native (drakan::gpu::Context::deviceLost) and reads the same GSR_TEST_DEVICE_LOSS
// frame (wgpu_present.cpp). Both report the loss through BackendApi::deviceLost, which the latch in gsr::Backend
// (backend_select.inl) and the host's ordered end (gsr/renderer_session.h) act on.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace gsr::device_loss {

// DXGI results (winerror.h values, spelled out so this header needs no Windows headers).
constexpr uint32_t kDeviceRemoved = 0x887A0005u;
constexpr uint32_t kDeviceHung = 0x887A0006u;
constexpr uint32_t kDeviceReset = 0x887A0007u;
constexpr uint32_t kDriverInternalError = 0x887A0020u;

enum class Cause : uint8_t {
    None,        // not a lost device
    Removed,     // DXGI_ERROR_DEVICE_REMOVED: the driver was updated, restarted or the adapter went away
    Hung,        // DXGI_ERROR_DEVICE_HUNG: the GPU took too long and Windows reset it
    Reset,       // DXGI_ERROR_DEVICE_RESET: a badly formed command or another program reset it
    DriverError, // DXGI_ERROR_DRIVER_INTERNAL_ERROR
    Unknown,     // the device reports itself removed with a result not listed here
};

// The cause a call's result names, or None when the result itself does not say the device is gone.
constexpr Cause classify(uint32_t result) {
    switch (result) {
        case kDeviceRemoved:
            return Cause::Removed;
        case kDeviceHung:
            return Cause::Hung;
        case kDeviceReset:
            return Cause::Reset;
        case kDriverInternalError:
            return Cause::DriverError;
        default:
            return Cause::None;
    }
}

constexpr bool failed(uint32_t result) {
    return (result & 0x80000000u) != 0;
}

// A call failed with `result`; `removedReason` is what the device answered when asked (GetDeviceRemovedReason: 0
// while it is alive). A result that names a loss is one. Any other failure (a Map that failed, out of memory, an
// invalid call) is a loss only when the device says it is removed; the device's reason is the better description.
constexpr Cause afterFailure(uint32_t result, uint32_t removedReason) {
    if (removedReason != 0) {
        const Cause reason = classify(removedReason);
        return reason != Cause::None ? reason : Cause::Unknown;
    }
    return classify(result);
}

// True when the caller must ask the device (GetDeviceRemovedReason) before it can decide: a failure whose result
// does not name a loss by itself.
constexpr bool needsDeviceAnswer(uint32_t result) {
    return failed(result) && classify(result) == Cause::None;
}

// The driver's name for a result, for the log (empty when not listed).
constexpr const char* codeName(uint32_t result) {
    switch (result) {
        case kDeviceRemoved:
            return "DXGI_ERROR_DEVICE_REMOVED";
        case kDeviceHung:
            return "DXGI_ERROR_DEVICE_HUNG";
        case kDeviceReset:
            return "DXGI_ERROR_DEVICE_RESET";
        case kDriverInternalError:
            return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
        default:
            return "";
    }
}

// What happened, in plain words for the player (no API names). It completes "The graphics card stopped working: ...".
constexpr const char* plainWords(Cause cause) {
    switch (cause) {
        case Cause::Hung:
            return "it took too long to answer and Windows restarted the graphics driver";
        case Cause::Reset:
            return "the graphics driver was reset";
        case Cause::DriverError:
            return "the graphics driver ran into an internal error";
        case Cause::Removed:
            return "the graphics driver was restarted, updated or the graphics card stopped responding";
        case Cause::Unknown:
        case Cause::None:
            break;
    }
    return "the graphics driver stopped responding";
}

// ---- test hook: pretend the device was removed (off unless GSR_TEST_DEVICE_LOSS is set) ----
//
// GSR_TEST_DEVICE_LOSS=<frame>: from that renderer frame on. GSR_TEST_DEVICE_LOSS_SITE=<site> says where the loss is
// found (Direct3D 11; the WebGPU backend destroys its device at the end of the frame whatever the site):
//   frame    at the end of that frame, as if the frame-end check found it (default)
//   map      the next Map of the draw path (vertex buffer or constants) fails
//   present  the next Present of the window image fails
//   texture  the next sampled texture creation fails (on a texture worker thread for upscaled and replaced
//            textures, on the renderer thread for plain decodes)
//   upscale  the next DLSS evaluation finds the device gone
// A level load, the settings overlay or any other moment is chosen by the frame number.
enum class Site : uint8_t { Off, Frame, Map, Present, Texture, Upscale };

struct TestPlan {
    Site site = Site::Off;
    long long frame = -1;
};

inline Site parseSite(const char* text) {
    if (!text || !*text || !std::strcmp(text, "frame"))
        return Site::Frame;
    if (!std::strcmp(text, "map"))
        return Site::Map;
    if (!std::strcmp(text, "present"))
        return Site::Present;
    if (!std::strcmp(text, "texture"))
        return Site::Texture;
    if (!std::strcmp(text, "upscale") || !std::strcmp(text, "dlss"))
        return Site::Upscale;
    return Site::Off; // an unknown site switches the hook off rather than testing something else
}

constexpr const char* siteName(Site site) {
    switch (site) {
        case Site::Frame:
            return "frame";
        case Site::Map:
            return "map";
        case Site::Present:
            return "present";
        case Site::Texture:
            return "texture";
        case Site::Upscale:
            return "upscale";
        case Site::Off:
            break;
    }
    return "off";
}

inline TestPlan parsePlan(const char* frame, const char* site) {
    TestPlan plan;
    if (!frame || !*frame)
        return plan;
    char* end = nullptr;
    const long long at = std::strtoll(frame, &end, 10);
    if (end == frame || at < 0)
        return plan;
    plan.site = parseSite(site);
    plan.frame = plan.site == Site::Off ? -1 : at;
    return plan;
}

inline TestPlan testPlanFromEnvironment() {
    return parsePlan(std::getenv("GSR_TEST_DEVICE_LOSS"), std::getenv("GSR_TEST_DEVICE_LOSS_SITE"));
}

} // namespace gsr::device_loss
