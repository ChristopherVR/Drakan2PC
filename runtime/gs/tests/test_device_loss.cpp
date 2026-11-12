// Which API results mean a removed graphics device, how a loss is described, and the forced-loss test hook
// (src/device_loss.h).
#include "device_loss.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {
int g_failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}
} // namespace

int main() {
    using namespace gsr::device_loss;
    // Results that name a loss by themselves.
    expect(classify(0x887A0005u) == Cause::Removed, "DXGI_ERROR_DEVICE_REMOVED");
    expect(classify(0x887A0006u) == Cause::Hung, "DXGI_ERROR_DEVICE_HUNG");
    expect(classify(0x887A0007u) == Cause::Reset, "DXGI_ERROR_DEVICE_RESET");
    expect(classify(0x887A0020u) == Cause::DriverError, "DXGI_ERROR_DRIVER_INTERNAL_ERROR");
    // Results that do not.
    expect(classify(0u) == Cause::None, "S_OK");
    expect(classify(0x087A0001u) == Cause::None, "DXGI_STATUS_OCCLUDED is a success");
    expect(classify(0x8007000Eu) == Cause::None, "E_OUTOFMEMORY alone is not a loss");
    expect(classify(0x80070057u) == Cause::None, "E_INVALIDARG alone is not a loss");
    expect(classify(0x887A0001u) == Cause::None, "DXGI_ERROR_INVALID_CALL alone is not a loss");
    expect(classify(0x887A000Au) == Cause::None, "DXGI_ERROR_WAS_STILL_DRAWING is not a loss");
    // Failures that need the device's answer, and how it decides.
    expect(!needsDeviceAnswer(0u) && !needsDeviceAnswer(0x087A0001u), "successes need no answer");
    expect(!needsDeviceAnswer(kDeviceRemoved), "a loss result needs no answer");
    expect(needsDeviceAnswer(0x8007000Eu) && needsDeviceAnswer(0x80004005u), "other failures ask the device");
    expect(afterFailure(0x8007000Eu, 0) == Cause::None, "out of memory on a live device is not a loss");
    expect(afterFailure(0x80004005u, kDeviceHung) == Cause::Hung, "a failed Map on a hung device is a loss");
    expect(afterFailure(kDeviceRemoved, kDeviceReset) == Cause::Reset, "the device's reason is the better one");
    expect(afterFailure(kDeviceRemoved, 0) == Cause::Removed, "a loss result stands when the device says nothing");
    expect(afterFailure(0x80004005u, 0x80004005u) == Cause::Unknown, "a removed device with an unlisted reason");
    // Descriptions.
    expect(std::string(codeName(kDeviceHung)) == "DXGI_ERROR_DEVICE_HUNG" && !*codeName(0u), "code names");
    for (const Cause c : {Cause::None, Cause::Removed, Cause::Hung, Cause::Reset, Cause::DriverError, Cause::Unknown}) {
        const std::string words = plainWords(c);
        expect(!words.empty(), "every cause has words");
        expect(words.find("DXGI") == std::string::npos && words.find("device") == std::string::npos &&
                   words.find("\xE2\x80\x94") == std::string::npos,
               "plain words: no API terms, no em-dash");
    }
    // The test hook: off unless a frame is given; the site picks where the loss is found.
    expect(parsePlan(nullptr, nullptr).site == Site::Off, "no frame: off");
    expect(parsePlan("", "map").site == Site::Off, "empty frame: off");
    expect(parsePlan("x", nullptr).site == Site::Off && parsePlan("-3", nullptr).site == Site::Off,
           "a frame that is not a number: off");
    const TestPlan frame = parsePlan("900", nullptr);
    expect(frame.site == Site::Frame && frame.frame == 900, "a frame alone: at the end of that frame");
    expect(parsePlan("5", "map").site == Site::Map && parsePlan("5", "present").site == Site::Present &&
               parsePlan("5", "texture").site == Site::Texture && parsePlan("5", "upscale").site == Site::Upscale &&
               parsePlan("5", "dlss").site == Site::Upscale && parsePlan("5", "frame").site == Site::Frame,
           "every site parses");
    const TestPlan unknown = parsePlan("5", "nowhere");
    expect(unknown.site == Site::Off && unknown.frame == -1, "an unknown site switches the hook off");
    for (const Site s : {Site::Frame, Site::Map, Site::Present, Site::Texture, Site::Upscale})
        expect(parseSite(siteName(s)) == s, "site names round trip");
    std::printf("%s\n", g_failures ? "device loss: FAILED" : "device loss: ok");
    return g_failures ? 1 : 0;
}
