// Synthetic audio output contracts: no disc or physical output device required.
#include "spu2/AudioOutput.h"

#include <array>
#include <cassert>
#include <chrono>
#include <string_view>
#include <thread>

int main(int argc, char** argv) {
    spu2::Spu2 spu;
    spu.sdInit(0);
    spu2::AudioOutput output(spu);
    spu2::AudioOutput::Options options;
    options.useDevice = argc > 1 && std::string_view(argv[1]) == "--alsa-null";
    options.device = options.useDevice ? "null" : "default";
    output.setOptions(options);

    std::array<float, 480 * 2> samples{};
    output.renderBlock(samples.data(), 480);
    assert(output.framesRendered() == 480);
    output.setMuted(true);
    output.renderBlock(samples.data(), 480);
    assert(output.framesRendered() == 960); // Mute advances voices; pause freezes them.
    output.setPaused(true);
    output.renderBlock(samples.data(), 480); // Finish the pause ramp.
    const auto held = output.framesRendered();
    output.renderBlock(samples.data(), 480);
    assert(output.framesRendered() == held);
    for (float sample : samples)
        assert(sample == 0.0f);
    output.setPaused(false);
    output.renderBlock(samples.data(), 480);
    assert(output.framesRendered() == held + 480);

    const auto before = output.framesRendered();
    assert(output.start());
    if (options.useDevice)
        assert(output.running() && output.deviceRate() == 48000 && output.currentDeviceId() == "null");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (output.framesRendered() == before && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(output.framesRendered() > before); // No-device mode must keep guest audio progressing.
    output.stop();
    assert(!output.running());
    const auto stopped = output.framesRendered();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(output.framesRendered() == stopped);
}
